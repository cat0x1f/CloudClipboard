#include <mongoose.h>

#define HOST "0.0.0.0"
#define PORT "8123"
#define HTTP_URL "http://" HOST ":" PORT
#define MAX_HISTORY 50
#define MAX_TEXT_LENGTH 4096
#define MAX_FILE_SIZE (128 * 1024 * 1024)
#define MAX_CHUNK_SIZE (512 * 1024)
#define EXPIRE_TIME 3600
#define SERVER_VERSION "c-1.2.0"

#define SERVER_FMT "{\"server\":\"ws://%.*s/push\"}"

#define JSON_CODE_200 "{\"code\": 200}"
#define TEXT_WS_FMT "{\"event\":\"receive\",\"data\":{\"id\":%d,\"type\":\"text\",\"content\":%m}}"

#define FILE_WS_UPLOAD "{\"code\":%d,\"result\":{\"uuid\":\"%.32s\"}}"
#define FILE_WS_FMT "{\"event\":\"receive\",\"data\":{\"id\":%d,\"type\":\"file\",\"name\":%m,\"size\":%d,\"cache\":\"%s\",\"expire\":%d}}"

#define CONFIG_WS "{\"event\":\"config\",\"data\":{\"version\":\"" SERVER_VERSION "\", \"text\":{\"limit\": %d},\"file\":{\"expire\":%d,\"chunk\":%d,\"limit\":%d}}}"

#define JSON_REPLY(fmt, ...) mg_http_reply(c, 200, s_json_header, fmt "\n", ##__VA_ARGS__);
#define WS_REPLY(fmt, ...) mg_ws_printf(c, WEBSOCKET_OP_TEXT, fmt, ##__VA_ARGS__);


#ifdef _WIN32
#define STORAGE_PATH "."
#else
#define STORAGE_PATH "/tmp/cloud_clipboard"
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static int s_sig_num;
static void signal_handler(int sig_num) {
    signal(sig_num, signal_handler);
    s_sig_num = sig_num;
}

static const char *s_json_header =
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache\r\n";

typedef struct CacheFile {
    char name[256];
    char id[33];
//    char thumbnail[4096];
    size_t size;
    size_t expire;
    int8_t available;
    FILE *fp;
    struct mg_connection* conn;
} CacheFile;

typedef enum data_format {
    MPV_FORMAT_NONE = 0,
    MPV_FORMAT_TEXT = 1,
    MPV_FORMAT_FILE = 2,
} data_format;

typedef struct data_node {
    union {
        char text[MAX_TEXT_LENGTH]; /** valid if format==MPV_FORMAT_TEXT */
        CacheFile file;             /** valid if format==MPV_FORMAT_FILE */
    } u;
    data_format format;
    int32_t index;
} data_node;

static int32_t global_index = 0;
data_node data[MAX_HISTORY];

data_node *getNode(int32_t id) {
    return &data[(id - 1) % MAX_HISTORY];
}

void deleteFile(const char* id);

void genNextIndex() {
    // clean up
    if (global_index > 0) {
        data_node *node = getNode(global_index);
        if (node->format == MPV_FORMAT_FILE && node->u.file.available) {
            deleteFile(node->u.file.id);
        }
    }

    global_index++;
    if (global_index < 1) global_index = 1;
}

data_node *saveText(struct mg_str *body) {
    data_node *item = getNode(global_index);
    strncpy(item->u.text, body->ptr, min(MAX_TEXT_LENGTH - 1, body->len));
    item->format = MPV_FORMAT_TEXT;
    item->index = global_index;
    return item;
}

data_node *saveFileName(struct mg_str *name, struct mg_connection *conn) {
    data_node *item = getNode(global_index);
    if (item->u.file.fp) fclose(item->u.file.fp);
    strncpy(item->u.file.name, name->ptr, min(255, name->len));
    item->format = MPV_FORMAT_FILE;
    item->u.file.available = 0;
    item->u.file.size      = 0;
    item->u.file.fp        = NULL;
    item->u.file.expire    = 0;
    item->index = global_index;
    mg_random_str(item->u.file.id, 32);
    char path[256];
    mg_snprintf(path, 256, STORAGE_PATH "/%s", item->u.file.id);
    item->u.file.fp = fopen(path, "wb");
    if (!item->u.file.fp) {
        return NULL;
    }
    item->u.file.conn = conn;
    return item;
}

data_node * saveFileChunk(data_node * node, struct mg_str *chunk) {
    if (node->u.file.fp == NULL) return NULL;
    node->u.file.size += chunk->len;
    fwrite(chunk->ptr, 1, chunk->len, node->u.file.fp);
    return node;
}

data_node * saveFileSuccess(data_node * node) {
    if (node->u.file.fp == NULL) return NULL;
    node->u.file.available = 1;
    node->u.file.expire = time(NULL) + EXPIRE_TIME;
    fclose(node->u.file.fp);
    node->u.file.fp = NULL;
    node->u.file.conn = NULL;
    return node;
}

void deleteFile(const char* id) {
    char path[256];
    mg_snprintf(path, 256, STORAGE_PATH "/%s", id);
    remove(path);
}

void cleanUp() {
    for (int i = max(0, global_index - MAX_HISTORY); i < global_index; i++) {
        data_node *node = getNode(i + 1);
        if (node->format == MPV_FORMAT_FILE && node->u.file.available) {
            deleteFile(node->u.file.id);
        }
    }
}

data_node * getFileNodeById(struct mg_str *id) {
    for (int i = max(0, global_index - MAX_HISTORY); i < global_index; i++) {
        data_node *node = getNode(i + 1);
        if (node->format != MPV_FORMAT_FILE) continue;
        if (strncmp(node->u.file.id, id->ptr, id->len) == 0) return node;
    }
    return NULL;
}

data_node * getFileNodeByConn(struct mg_connection *conn) {
    for (int i = max(0, global_index - MAX_HISTORY); i < global_index; i++) {
        data_node *node = getNode(i + 1);
        if (node->format != MPV_FORMAT_FILE) continue;
        if (node->u.file.conn == conn) return node;
    }
    return NULL;
}

// HTTP request handler function
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    struct mg_str caps[1];
    if (ev == MG_EV_HTTP_MSG) {  // New HTTP request received
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        if (mg_match(hm->uri, mg_str("/server"), NULL)) {
            // Request server configuration
            struct mg_str *host = mg_http_get_header(hm, "Host");
            JSON_REPLY(SERVER_FMT, host->len, host->ptr);
        } else if (mg_match(hm->uri, mg_str("/push"), NULL)) {
            // Upgrade to websocket. From now on, a connection is a full-duplex
            // Websocket connection, which will receive MG_EV_WS_MSG events.
            mg_ws_upgrade(c, hm, NULL);
        } else if (mg_match(hm->uri, mg_str("/revoke/*"), NULL)) {
            // todo: Delete
            JSON_REPLY(JSON_CODE_200);
        } else if (mg_match(hm->uri, mg_str("/text"), NULL)) {
            // Post text
            JSON_REPLY(JSON_CODE_200);
            // save text to cache
            genNextIndex();
            data_node *node = saveText(&(hm->body));
            // broadcast to all websocket clients
            struct mg_mgr *mgr = (struct mg_mgr *) c->fn_data;
            for (c = mgr->conns; c != NULL; c = c->next) {
                if (!c->is_websocket) continue;
                WS_REPLY(TEXT_WS_FMT, global_index, MG_ESC(node->u.text));
            }
        } else if (mg_match(hm->uri, mg_str("/upload/chunk/*"), caps)) {
            // Upload file chunk
            data_node *node = getFileNodeById(caps);
            if (!node) {
                JSON_REPLY(FILE_WS_UPLOAD, 404, caps[0].ptr);
                return;
            }
            if (!saveFileChunk(node, &(hm->body))) {
                JSON_REPLY(FILE_WS_UPLOAD, 500, node->u.file.id);
                return;
            }
            JSON_REPLY(FILE_WS_UPLOAD, 200, node->u.file.id);
        } else if (mg_match(hm->uri, mg_str("/upload/finish/*"), caps)) {
            // Finish upload file
            data_node *node = getFileNodeById(caps);
            if (!node) {
                JSON_REPLY(FILE_WS_UPLOAD, 404, caps[0].ptr);
                return;
            }
            if (!saveFileSuccess(node)) {
                JSON_REPLY(FILE_WS_UPLOAD, 500, node->u.file.id);
                return;
            }
            JSON_REPLY(FILE_WS_UPLOAD, 200, node->u.file.id);
            // broadcast to all websocket clients
            struct mg_mgr *mgr = (struct mg_mgr *) c->fn_data;
            for (c = mgr->conns; c != NULL; c = c->next) {
                if (!c->is_websocket) continue;
                WS_REPLY(FILE_WS_FMT, node->index, MG_ESC(node->u.file.name), node->u.file.size, node->u.file.id, node->u.file.expire);
            }
        } else if (mg_match(hm->uri, mg_str("/upload"), NULL)) {
            // Start upload file
            genNextIndex();
            data_node *node = saveFileName(&(hm->body), c);
            if (!node) {
                JSON_REPLY(FILE_WS_UPLOAD, 500, "");
                return;
            }
            // Return file id
            JSON_REPLY(FILE_WS_UPLOAD, 200, node->u.file.id);
        } else if (mg_match(hm->uri, mg_str("/file/*"), caps)) {
            // Download file
            char path[256];
            struct mg_http_serve_opts sopts;
            memset(&sopts, 0, sizeof(sopts));
            mg_snprintf(path, 256, STORAGE_PATH "/%.*s", (int)caps[0].len, caps[0].ptr);
            mg_http_serve_file(c, hm, path, &sopts);
        } else {
            // Serve static content
#ifdef PACKAGE_FILE
            struct mg_http_serve_opts opts = {.root_dir = "/static", .fs = &mg_fs_packed};
#else
            struct mg_http_serve_opts opts = {.root_dir = "./static"};
#endif
            mg_http_serve_dir(c, hm, &opts);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        // Send server configuration to new client
        WS_REPLY(CONFIG_WS, MAX_TEXT_LENGTH, EXPIRE_TIME, MAX_CHUNK_SIZE, MAX_FILE_SIZE);
        // Send history data to new client
        for (int i = max(0, global_index - MAX_HISTORY); i < global_index; i++) {
            data_node *node = getNode(i + 1);
            if (node->format == MPV_FORMAT_TEXT) {
                WS_REPLY(TEXT_WS_FMT, node->index, MG_ESC(node->u.text));
            } else if (node->format == MPV_FORMAT_FILE && node->u.file.available) {
                WS_REPLY(FILE_WS_FMT, node->index, MG_ESC(node->u.file.name), node->u.file.size, node->u.file.id, node->u.file.expire);
            }
        }
    } else if (ev == MG_EV_WS_CTL) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        uint8_t op = wm->flags & 15;
        if (op == WEBSOCKET_OP_CLOSE) {
            // Remove unfinished files
            data_node *node = getFileNodeByConn(c);
            if (!node) return;
            if (node->u.file.fp) fclose(node->u.file.fp);
            deleteFile(node->u.file.id);
        }
    }
}

static void timer_cb(void *arg) {
    // Check file expire
    time_t now = time(NULL);
    for (int i = max(0, global_index - MAX_HISTORY); i < global_index; i++) {
        data_node *node = getNode(i + 1);
        if (node->format != MPV_FORMAT_FILE) continue;
        if (node->u.file.expire < now) {
            if (node->u.file.fp) fclose(node->u.file.fp);
            deleteFile(node->u.file.id);
        }
    }
}

int main(void) {
    struct mg_mgr mgr;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    memset(data, 0, sizeof(data_node) * MAX_HISTORY);

    // create file storage directory
    struct stat st = {0};
    if (stat(STORAGE_PATH, &st) == -1) {
        mkdir(STORAGE_PATH, 0755);
    }

    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, HTTP_URL, fn, &mgr) == NULL) {
        MG_ERROR(("Cannot listen on %s.", HTTP_URL));
        mg_mgr_free(&mgr);
        exit(EXIT_FAILURE);
    }
    mg_timer_add(&mgr, 10000, MG_TIMER_REPEAT, timer_cb, NULL);
    MG_INFO(("Starting web server on %s", HTTP_URL));
    while (s_sig_num == 0) {
        mg_mgr_poll(&mgr, 50);
    }
    mg_mgr_free(&mgr);
    cleanUp();
    return 0;
}