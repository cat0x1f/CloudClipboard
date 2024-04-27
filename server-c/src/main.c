#include <mongoose.h>
#include "data.h"
#include "utils.h"

#define SERVER_VERSION "c-1.2.0"

// HTTP response
#define JSON_SUCCESS    "{\"code\": 200}"
#define JSON_FAILED     "{\"code\":500}"
#define SERVER_FMT      "{\"server\":\"ws://%.*s/push\"}"
#define FILE_UPLOAD_FMT "{\"code\":%d,\"result\":{\"uuid\":\"%.32s\"}}"
// Websocket response
#define REVOKE_FMT      "{\"event\":\"revoke\",\"data\":{\"id\":%u}}"
#define TEXT_FMT        "{\"event\":\"receive\",\"data\":{\"id\":%u,\"type\":\"text\",\"content\":%m}}"
#define FILE_FMT        "{\"event\":\"receive\",\"data\":{\"id\":%u,\"type\":\"file\",\"name\":%m,\"size\":%d,\"cache\":\"%s\",\"expire\":%d}}"
#define IMAGE_FMT       "{\"event\":\"receive\",\"data\":{\"id\":%u,\"type\":\"file\",\"name\":%m,\"size\":%d,\"cache\":\"%s\",\"expire\":%d,\"thumbnail\":\"%s\"}}"
#define CONFIG_FMT      "{\"event\":\"config\",\"data\":{\"version\":\"" SERVER_VERSION "\", \"text\":{\"limit\": %d},\"file\":{\"expire\":%d,\"chunk\":%d,\"limit\":%d}}}"

#define JSON_REPLY(fmt, ...) mg_http_reply(c, 200, s_json_header, fmt "\n", ##__VA_ARGS__);
#define WS_REPLY(fmt, ...) mg_ws_printf(c, WEBSOCKET_OP_TEXT, fmt, ##__VA_ARGS__);
#define NODE_CHECK(node) if (!node) { JSON_REPLY(JSON_FAILED); return;}

#define CLIENT_REQUEST mg_print_ip_port, &c->rem

static int s_sig_num;

static void signal_handler(int sig_num) {
    signal(sig_num, signal_handler);
    s_sig_num = sig_num;
}

static const char *s_json_header =
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache\r\n";

void send_data(data_node *node, void *data) {
    struct mg_connection *c = (struct mg_connection *) data;
    if (node->format == DATA_FORMAT_TEXT) {
        WS_REPLY(TEXT_FMT, node->index, MG_ESC(node->u.text->data));
    } else if (node->format == DATA_FORMAT_FILE && node->u.file->available) {
        if (node->u.file->thumbnail_len) {
            WS_REPLY(IMAGE_FMT, node->index, MG_ESC(node->u.file->name), node->u.file->size, node->u.file->id,
                     node->u.file->expire, node->u.file->thumbnail);
        } else {
            WS_REPLY(FILE_FMT, node->index, MG_ESC(node->u.file->name), node->u.file->size, node->u.file->id,
                     node->u.file->expire);
        }
    }
}

char buf_path[MAX_PATH];

// HTTP request handler function
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    struct mg_str caps[1];
    if (ev == MG_EV_HTTP_MSG) {  // New HTTP request received
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        if (mg_match(hm->uri, mg_str("/server"), NULL)) {
            MG_INFO(("Request server configuration from %M", CLIENT_REQUEST));
            struct mg_str *host = mg_http_get_header(hm, "Host");
            JSON_REPLY(SERVER_FMT, host->len, host->ptr);
        } else if (mg_match(hm->uri, mg_str("/push"), NULL)) {
            // Upgrade to websocket. From now on, a connection is a full-duplex
            // Websocket connection, which will receive MG_EV_WS_MSG events.
            mg_ws_upgrade(c, hm, NULL);
        } else if (mg_match(hm->uri, mg_str("/revoke/*"), caps)) {
            mg_snprintf(buf_path, sizeof(buf_path), "%.*s", (int) caps[0].len, caps[0].ptr);
            uint32_t res = strtoul(buf_path, NULL, 10);
            MG_INFO(("Request revoke[%u] from %M", res, CLIENT_REQUEST));
            if (delete_node_by_index(res) == 0) {
                JSON_REPLY(JSON_FAILED);
                return;
            }
            save_data();
            JSON_REPLY(JSON_SUCCESS);
            // broadcast to all websocket clients
            struct mg_mgr *mgr = (struct mg_mgr *) c->fn_data;
            for (c = mgr->conns; c != NULL; c = c->next) {
                if (!c->is_websocket) continue;
                WS_REPLY(REVOKE_FMT, res);
            }
        } else if (mg_match(hm->uri, mg_str("/text"), NULL)) {
            MG_INFO(("Post text from %M", CLIENT_REQUEST));
            data_node *node = add_text(&hm->body);
            NODE_CHECK(node);
            JSON_REPLY(JSON_SUCCESS);
            // broadcast to all websocket clients
            struct mg_mgr *mgr = (struct mg_mgr *) c->fn_data;
            for (c = mgr->conns; c != NULL; c = c->next) {
                if (!c->is_websocket) continue;
                WS_REPLY(TEXT_FMT, node->index, MG_ESC(node->u.text->data));
            }
        } else if (mg_match(hm->uri, mg_str("/upload/chunk/*"), caps)) {
            // Upload file chunk
            data_node *node = append_to_file(caps, &hm->body);
            NODE_CHECK(node);
            JSON_REPLY(FILE_UPLOAD_FMT, 200, node->u.file->id);
        } else if (mg_match(hm->uri, mg_str("/upload/finish/*"), caps)) {
            MG_INFO(("File upload is finished from %M", CLIENT_REQUEST));
            data_node *node = add_file(caps);
            NODE_CHECK(node);
            JSON_REPLY(FILE_UPLOAD_FMT, 200, node->u.file->id);
            // broadcast to all websocket clients
            struct mg_mgr *mgr = (struct mg_mgr *) c->fn_data;
            for (c = mgr->conns; c != NULL; c = c->next) {
                if (!c->is_websocket) continue;
                if (node->u.file->thumbnail_len) {
                    WS_REPLY(IMAGE_FMT, node->index, MG_ESC(node->u.file->name), node->u.file->size, node->u.file->id,
                             node->u.file->expire, node->u.file->thumbnail);
                } else {
                    WS_REPLY(FILE_FMT, node->index, MG_ESC(node->u.file->name), node->u.file->size, node->u.file->id,
                             node->u.file->expire);
                }
            }
        } else if (mg_match(hm->uri, mg_str("/upload"), NULL)) {
            MG_INFO(("Start upload file from %M", CLIENT_REQUEST));
            data_node *node = new_file(&hm->body);
            NODE_CHECK(node);
            // Return file id
            JSON_REPLY(FILE_UPLOAD_FMT, 200, node->u.file->id);
        } else if (mg_match(hm->uri, mg_str("/file/*"), caps)) {
            // Download file
            data_node *node = find_file_node_by_id(&caps[0]);
            NODE_CHECK(node);
            struct mg_http_serve_opts sopts;
            memset(&sopts, 0, sizeof(sopts));
            mg_snprintf(buf_path, MAX_PATH, "%s/%.*s", data_option->storage_path, (int) caps[0].len, caps[0].ptr);
            MG_INFO(("Download file: [%s] from %M", buf_path, CLIENT_REQUEST));
            mg_http_serve_file(c, hm, node->u.file->name, buf_path, &sopts);
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
        WS_REPLY(CONFIG_FMT, data_option->text_length, data_option->expire_time, data_option->chunk_size,
                 data_option->file_size);
        MG_INFO(("Websocket Client %M is connected", CLIENT_REQUEST));
        // Send history data to new client
        list_data(send_data, c);
    } else if (ev == MG_EV_WS_CTL) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        uint8_t op = wm->flags & 15;
        if (op == WEBSOCKET_OP_CLOSE) {
            MG_INFO(("Websocket Client %M is left", CLIENT_REQUEST));
        }
    }
}

static void timer_cb(void *arg) {
    (void)arg;
    check_file_expire();
}

static void http_listen(const char* url, void * arg) {
    struct mg_mgr *mgr = (struct mg_mgr *) arg;
    if (mg_http_listen(mgr, url, fn, mgr) == NULL)
    {
        MG_ERROR(("Cannot listen on %s.", url));
    } else {
        MG_INFO(("Starting web server on %s", url));
    }
}

int main(void) {
    struct mg_mgr mgr;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    char storage_path[MAX_PATH];
    getTempPath(storage_path);
    data_node_option option = {
            .storage_path   = storage_path,
            .text_length    = 4096,
            .file_size      = 128 * 1024 * 1024,
            .chunk_size     = 512 * 1024,
            .expire_time    = 3600,
            .upload_timeout = 60,
            .max_history    = 10,
    };
    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);
    loadConfig(&option, http_listen, &mgr);
    init_data(&option);
    mg_timer_add(&mgr, 10000, MG_TIMER_REPEAT | MG_TIMER_RUN_NOW, timer_cb, NULL);
    while (s_sig_num == 0) {
        mg_mgr_poll(&mgr, 50);
    }
    mg_mgr_free(&mgr);
    destroy_data();
    return 0;
}