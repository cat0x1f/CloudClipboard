#include <stdlib.h>
#include <string.h>

#include "data.h"

data_node *head;
data_node_option *data_option;
uint32_t node_index;

void append_to_tail(data_node *node) {
    data_node *tail = head;
    uint32_t num = 1;
    while (tail->next) {
        num++;
        tail = tail->next;
    }
    tail->next = node;
    node->next = NULL;
    if (num > data_option->max_history) {
        delete_node_by_index(head->next->index);
    }
}

data_node *find_file_node_by_id(struct mg_str *id) {
    data_node *node = head->next;
    if (id->len != FILE_ID_LEN) return NULL;
    while (node) {
        if (node->format == DATA_FORMAT_FILE && strncmp(node->u.file->id, id->ptr, FILE_ID_LEN) == 0)
            return node;
        node = node->next;
    }
    return NULL;
}

void init_data(data_node_option *option) {
    struct stat st = {0};
    data_option = option;
    if (stat(option->storage_path, &st) == -1) {
        int res = mkdir(option->storage_path, 0755);
    }
    node_index = 0;
    head = (data_node *) malloc(sizeof(data_node));
    head->format = DATA_FORMAT_HEAD;
    head->index = node_index++;
    head->next = NULL;
}

void delete_node_by_ptr(data_node *node) {
    if (!node) return;
    if (node->format == DATA_FORMAT_FILE) {
        if (node->u.file->fp) fclose(node->u.file->fp);
        char path[MAX_PATH];
        mg_snprintf(path, MAX_PATH, "%s/%s", data_option->storage_path, node->u.file->id);
        remove(path);
        free(node->u.file);
        free(node);
    } else if (node->format == DATA_FORMAT_TEXT) {
        free(node->u.text);
        free(node);
    } else {
        free(node);
    }
}

void destroy_data() {
    data_node *node = head;
    while (node) {
        data_node *next = node->next;
        delete_node_by_ptr(node);
        node = next;
    }
}

int8_t delete_node_by_index(uint32_t index) {
    if (index == 0) return 0;
    MG_INFO(("Delete node: %u", index));
    data_node *node = head->next;
    data_node *last = head;
    while (node) {
        if (node->index != index) {
            last = node;
            node = node->next;
            continue;
        }
        last->next = node->next;
        delete_node_by_ptr(node);
        return 1;
    }
    return 0;
}

void check_file_expire() {
    time_t now = time(NULL);
    data_node *node = head->next;
    while (node) {
        if (node->format == DATA_FORMAT_FILE) {
            if (node->u.file->expired == 0 && node->u.file->available && node->u.file->expire < now) {
                // expired file: only remove file
                node->u.file->expired = 1;
                char path[MAX_PATH];
                mg_snprintf(path, MAX_PATH, "%s/%s", data_option->storage_path, node->u.file->id);
                remove(path);
                MG_INFO(("Remove expired file %s", node->u.file->id));
            } else if (!node->u.file->available &&
                       node->u.file->expire - data_option->expire_time + data_option->upload_timeout < now) {
                // unfinished upload file: remove the node
                MG_INFO(("Remove unfinished file %s", node->u.file->id));
                delete_node_by_index(node->index);
            }
        }
        node = node->next;
    }
}

void list_data(void (*cb)(data_node *, void *), void *data) {
    data_node *node = head->next;
    while (node) {
        cb(node, data);
        node = node->next;
    }
}

data_node *add_text(struct mg_str *text) {
    if (text->len > data_option->text_length) return NULL;
    data_node *node = (data_node *) malloc(sizeof(data_node));
    node->format = DATA_FORMAT_TEXT;
    node->index = node_index++;
    node->u.text = (CacheText *) malloc(sizeof(CacheText) + text->len + 1);
    node->u.text->len = text->len;
    strncpy(node->u.text->data, text->ptr, text->len);
    node->u.text->data[text->len] = '\0';
    append_to_tail(node);
    return node;
}

data_node *new_file(struct mg_str *name) {
    char path[MAX_PATH];
    size_t len = min(name->len, MAX_PATH);
    data_node *node = (data_node *) malloc(sizeof(data_node));
    node->format = DATA_FORMAT_FILE;
    node->index = node_index++;
    node->u.file = (CacheFile *) malloc(sizeof(CacheFile));
    strncpy(node->u.file->name, name->ptr, len);
    node->u.file->name[len] = '\0';
    node->u.file->available = 0;
    node->u.file->expired = 0;
    node->u.file->expire = time(NULL) + data_option->expire_time;
    node->u.file->size = 0;
    node->u.file->fp = NULL;
    mg_random_str(node->u.file->id, FILE_ID_LEN + 1);
    mg_snprintf(path, MAX_PATH, "%s/%s", data_option->storage_path, node->u.file->id);
    node->u.file->fp = fopen(path, "wb");
    if (!node->u.file->fp) {
        free(node);
        return NULL;
    }
    append_to_tail(node);
    return node;
}

data_node *cleanup_file(data_node *node) {
    if (node) delete_node_by_index(node->index);
    return NULL;
}

data_node *append_to_file(struct mg_str *id, struct mg_str *chunk) {
    data_node *node = find_file_node_by_id(id);
    if (!node) return NULL;
    if (node->u.file->fp == NULL) return cleanup_file(node);
    if (chunk->len > data_option->chunk_size) return cleanup_file(node);
    if (node->u.file->size + chunk->len > data_option->file_size) return cleanup_file(node);
    if (node->u.file->expire - data_option->expire_time + data_option->upload_timeout < time(NULL))
        return cleanup_file(node);
    node->u.file->size += chunk->len;
    fwrite(chunk->ptr, 1, chunk->len, node->u.file->fp);
    return node;
}

data_node *add_file(struct mg_str *id) {
    data_node *node = find_file_node_by_id(id);
    if (!node) return NULL;
    if (node->u.file->fp == NULL) return cleanup_file(node);
    fclose(node->u.file->fp);
    node->u.file->fp = NULL;
    node->u.file->available = 1;
    return node;
}
