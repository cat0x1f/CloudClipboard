#pragma once

#include <stdio.h>
#include <mongoose.h>

#define MAX_PATH 256
#define FILE_ID_LEN 32

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

typedef struct CacheFile {
    char id[FILE_ID_LEN + 1];
    char name[MAX_PATH + 1];
    size_t size;
    size_t expire;
    bool available;
    bool expired;
    FILE *fp;
} CacheFile;

typedef struct CacheText {
    size_t len;
    char data[0];
} CacheText;

typedef enum data_format {
    DATA_FORMAT_HEAD = 0,
    DATA_FORMAT_TEXT = 1,
    DATA_FORMAT_FILE = 2,
} data_format;

typedef struct data_node {
    data_format format;
    uint32_t index;
    struct data_node *next;
    union {
        CacheText *text; /** valid if format==MPV_FORMAT_TEXT */
        CacheFile *file; /** valid if format==MPV_FORMAT_FILE */
    } u;
} data_node;

typedef struct data_node_option {
    const char *storage_path;
    long text_length;
    long chunk_size;
    long file_size;
    long expire_time;
    long upload_timeout;
    long max_history;
} data_node_option;

extern data_node *head;
extern data_node_option *data_option;

void init_data(data_node_option *option);

void destroy_data();

bool delete_node_by_index(uint32_t index);

void delete_node_by_ptr(data_node *node);

void list_data(void (*cb)(data_node *, void *), void *data);

data_node *add_text(struct mg_str *text);

data_node *new_file(struct mg_str *name);

data_node *append_to_file(struct mg_str *id, struct mg_str *chunk);

data_node *add_file(struct mg_str *id);

void check_file_expire();