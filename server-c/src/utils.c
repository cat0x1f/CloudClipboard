#include <mongoose.h>
#include "utils.h"

static mg_pfn_t s_log_func = mg_pfn_stdout;
static void *s_log_func_param = NULL;

static void logc(unsigned char c) {
    s_log_func((char) c, s_log_func_param);
}

static void logs(const char *buf, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) logc(((unsigned char *) buf)[i]);
}


void mg_log_prefix(int level, const char *file, int line, const char *fname) {
    (void) file;
    (void) line;
    (void) fname;
    char buf[34];
    size_t n;
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    uint64_t milliseconds = mg_millis() % 1000;
    n = mg_snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d ", lt->tm_hour, lt->tm_min, lt->tm_sec, milliseconds);
    switch (level) {
        case MG_LL_ERROR:
            n += mg_snprintf(buf + n, sizeof(buf) - n, "\033[0;31mERROR\033[0m ");
            break;
        case MG_LL_INFO:
            n += mg_snprintf(buf + n, sizeof(buf) - n, "\033[0;34mINFO\033[0m ");
            break;
        case MG_LL_DEBUG:
            n += mg_snprintf(buf + n, sizeof(buf) - n, "\033[0;32mDEBUG\033[0m ");
            break;
        case MG_LL_VERBOSE:
        default:
            n += mg_snprintf(buf + n, sizeof(buf) - n, "\033[0;37mVERBOSE\033[0m ");
            break;
    }
    if (n > sizeof(buf) - 2) n = sizeof(buf) - 2;
    while (n < sizeof(buf)) buf[n++] = ' ';
    logs(buf, n - 1);
}

void mg_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mg_vxprintf(s_log_func, s_log_func_param, fmt, &ap);
    va_end(ap);
    logs("\n", 1);
}

void loadConfig(data_node_option *option, void (*cb)(const char*, void *), void * arg) {
    struct stat st = {0};
    bool empty_host = true;
    char buf[256];
    long server_port = 8123;
    if (stat(CONFIG_FILE, &st) != -1) {
        // load config
        struct mg_str config = mg_file_read(&mg_fs_posix, CONFIG_FILE);
        option->text_length = mg_json_get_long(config, "$.text.limit", 4096);
        option->expire_time = mg_json_get_long(config, "$.file.expire", 3600);
        option->chunk_size = mg_json_get_long(config, "$.file.chunk", 512 * 1024);
        option->file_size = mg_json_get_long(config, "$.file.limit", 128 * 1024 * 1024);
        option->upload_timeout = mg_json_get_long(config, "$.file.timeout", 60); // file upload timeout
        option->max_history = mg_json_get_long(config, "$.server.history", 10);
        server_port = mg_json_get_long(config, "$.server.port", 8123);

        struct mg_str val;
        size_t ofs = 0;
        while ((ofs = mg_json_next(mg_json_get_tok(config, "$.server.host"), ofs, NULL, &val)) > 0) {
            if (memchr(val.ptr, ':', val.len) == NULL) {
                mg_snprintf(buf, 256, "http://%.*s:%d", (int) val.len - 2, mg_json_get_str(val, "$"), server_port);
            } else {
                mg_snprintf(buf, 256, "http://[%.*s]:%d", (int) val.len - 2, mg_json_get_str(val, "$"), server_port);
            }
            empty_host = false;
            cb(buf, arg);
        }
    }
    if (empty_host) {
        mg_snprintf(buf, 256, "http://0.0.0.0:%d", server_port);
        cb(buf, arg);
    }
}