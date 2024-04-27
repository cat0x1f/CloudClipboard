#include <mongoose.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

void getTempPath(char* buf) {
#ifdef _WIN32
    DWORD answer = GetTempPathA(MAX_PATH, buf);
    mg_snprintf(buf + answer, MAX_PATH - answer, "cloud_clipboard");
#else
    mg_snprintf(buf, MAX_PATH, "/tmp/cloud_clipboard");
#endif
}

#define BUF_THUMB_LEN 4000
char buf_thumb[BUF_THUMB_LEN];
unsigned char image_buf[2000];

static void jpeg_write(void *context, void *data, int len) {
    size_t *base64_len = (size_t *) context;
    if (*base64_len + len > sizeof(image_buf)) {
        MG_ERROR(("Image buffer overflow"));
        return;
    }
    memcpy(image_buf + *base64_len, data, len);
    *base64_len += len;
}

const char *create_thumbnail(const char *path, size_t* len) {
    int width, height, channels;
    unsigned char *image = stbi_load(path, &width, &height, &channels, 0);
    if (image == NULL) return NULL;

    *len = mg_snprintf(buf_thumb, BUF_THUMB_LEN, "data:image/jpeg;base64,");
    size_t jpeg_len   = 0;

    if (max(width, height) > 64) {
        float ratio = 64.0f / max(width, height);
        int new_width = width * ratio;
        int new_height = height * ratio;
        unsigned char *resized_image = malloc(new_width * new_height * channels);
        if (resized_image == NULL) goto free_image;
        if (!stbir_resize_uint8_srgb(image, width, height, 0, resized_image, new_width, new_height, 0, channels)) return false;
        stbi_write_jpg_to_func(jpeg_write, &jpeg_len, new_width, new_height, channels, resized_image, 70);
        free(resized_image);
    } else {
        stbi_write_jpg_to_func(jpeg_write, &jpeg_len, width, height, channels, image, 70);
    }

    *len += mg_base64_encode(image_buf, jpeg_len, buf_thumb + *len, BUF_THUMB_LEN - *len);
    if (*len == 0) {
        MG_ERROR(("Base64 encode failed"));
        return NULL;
    }

    return buf_thumb;
    free_image:
    stbi_image_free(image);
    return NULL;
}