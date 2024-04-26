#include <mongoose.h>

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