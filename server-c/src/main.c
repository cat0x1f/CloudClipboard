#include <mongoose.h>

#define HOST "0.0.0.0"
#define PORT "8123"
#define WS_PATH "/push"
#define HTTP_URL "http://" HOST ":" PORT
#define MAX_HISTORY 50

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

static int s_sig_num;
static void signal_handler(int sig_num) {
    signal(sig_num, signal_handler);
    s_sig_num = sig_num;
}

static const char *s_json_header =
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache\r\n";

static int32_t global_index = 0;
static char text[MAX_HISTORY][4096];

// HTTP request handler function
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {  // New HTTP request received
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        if (mg_match(hm->uri, mg_str("/server"), NULL)) {
            struct mg_str *host = mg_http_get_header(hm, "Host");
            mg_http_reply(c, 200, s_json_header, "{%m:\"ws://%.*s" WS_PATH "\"}\n",
                          MG_ESC("server"), host->len, host->ptr);
        } else if (mg_match(hm->uri, mg_str("/text"), NULL)) {
            global_index++;
            if (global_index < 1) global_index = 1;
            mg_http_reply(c, 200, s_json_header, "{%m:%d}\n", MG_ESC("code"), 200);
            struct mg_mgr* mgr = (struct mg_mgr*)c->fn_data;
            strncpy(text[(global_index - 1) % MAX_HISTORY], hm->body.ptr, min(4095, hm->body.len));
            for (c = mgr->conns; c != NULL; c = c->next) {
                if (!c->is_websocket) continue;
                mg_ws_printf(c, WEBSOCKET_OP_TEXT,
                             "{\"event\":\"receive\","
                             "\"data\":{\"id\":%d,\"type\":\"text\",\"room\":\"\",\"content\":%m}}",
                             global_index, MG_ESC(hm->body.ptr));
            }

        } else if (mg_match(hm->uri, mg_str(WS_PATH), NULL)) {
            // Upgrade to websocket. From now on, a connection is a full-duplex
            // Websocket connection, which will receive MG_EV_WS_MSG events.
            mg_ws_upgrade(c, hm, NULL);
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
        mg_ws_printf(c, WEBSOCKET_OP_TEXT,
                     "{\"event\":\"config\","
                     "\"data\":{\"version\":\"c-1.2.0\","
                                "\"text\":{\"limit\":4096},"
                                "\"file\":{\"expire\":3600,\"chunk\":2097152,\"limit\":268435456}}}");
        for (int i = max(0, global_index - MAX_HISTORY); i < global_index; i++) {
            mg_ws_printf(c, WEBSOCKET_OP_TEXT,
                         "{\"event\":\"receive\","
                         "\"data\":{\"id\":%u,\"type\":\"text\",\"room\":\"\",\"content\":%m}}",
                         i + 1, MG_ESC(text[i % MAX_HISTORY]));
        }
    }
}

void web_init(struct mg_mgr *mgr) {
    printf("Starting web server on " HTTP_URL "\n");
    if (mg_http_listen(mgr, HTTP_URL, fn, mgr) == NULL) {
        MG_ERROR(("Cannot listen on %s.", HTTP_URL));
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    struct mg_mgr mgr;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    memset(text, 0, sizeof(char) * 4096 * MAX_HISTORY);

    mg_log_set(MG_LL_ERROR);
    mg_mgr_init(&mgr);
    web_init(&mgr);
    while (s_sig_num == 0) {
        mg_mgr_poll(&mgr, 50);
    }
    mg_mgr_free(&mgr);
    return 0;
}