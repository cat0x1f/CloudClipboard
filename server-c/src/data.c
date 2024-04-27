#include <stdlib.h>
#include <string.h>

#include "data.h"
#include "utils.h"

data_node *head;
data_node_option *data_option;
uint32_t node_index;

void append_to_tail(data_node *node);

void save_data() {
    data_node *tail = head->next;
    size_t len = 25, pos = 0;
    bool empty;
    while (tail) {
        if (tail->format == DATA_FORMAT_TEXT) {
            len += tail->u.text->len + 46; // text node
        } else if (tail->format == DATA_FORMAT_FILE) {
            if (tail->u.file->available) {
                len += MAX_PATH + FILE_ID_LEN + 93; // file node
                len += tail->u.file->thumbnail_len; // thumbnail
                if (!tail->u.file->expire) len += MAX_PATH + FILE_ID_LEN + 88; // file list
            }
        }
        tail = tail->next;
    }
    char *buf = malloc(len);
    pos += mg_snprintf(buf, len, "{\"file\":[");
    tail = head->next;
    empty = true;
    while (tail) {
        if (tail->format == DATA_FORMAT_FILE) {
            if (tail->u.file->available && !tail->u.file->expired) {
                pos += mg_snprintf(buf + pos, len - pos,
                                   "{\"name\":%m,\"uuid\":\"%s\",\"size\":%d,\"uploadTime\":%d,\"expireTime\":%d},",
                                   MG_ESC(tail->u.file->name), tail->u.file->id, tail->u.file->size,
                                   tail->u.file->upload, tail->u.file->expire);
                empty = false;
            }
        }
        tail = tail->next;
    }
    if (!empty) pos--; // Remove last ","
    pos += mg_snprintf(buf + pos, len - pos, "],\"receive\":[");
    tail = head->next;
    empty = true;
    while (tail) {
        if (tail->format == DATA_FORMAT_TEXT) {
            pos += mg_snprintf(buf + pos, len - pos, "{\"id\":%d,\"type\":\"text\",\"content\":%m},",
                               tail->index, MG_ESC(tail->u.text->data));
        } else if (tail->format == DATA_FORMAT_FILE) {
            if (tail->u.file->available) {
                pos += mg_snprintf(buf + pos, len - pos,
                                   "{\"id\":%d,\"type\":\"file\",\"name\":%m,\"size\":%d,\"cache\":\"%s\",\"expire\":%d",
                                   tail->index, MG_ESC(tail->u.file->name), tail->u.file->size, tail->u.file->id,
                                   tail->u.file->expire);
                if (tail->u.file->thumbnail_len) {
                    pos += mg_snprintf(buf + pos, len - pos, ",\"thumbnail\":\"%s\"},", tail->u.file->thumbnail);
                } else {
                    pos += mg_snprintf(buf + pos, len - pos, "},");
                }
            }
        }
        tail = tail->next;
        empty = false;
    }
    if (!empty) pos--; // Remove last ","
    pos += mg_snprintf(buf + pos, len - pos, "]}");
    char path[MAX_PATH];
    mg_snprintf(path, MAX_PATH, "%s/history.json", data_option->storage_path);
    mg_file_write(&mg_fs_posix, path, buf, pos);
}

void load_data() {
    struct stat st = {0};
    char path[MAX_PATH];
    mg_snprintf(path, MAX_PATH, "%s/history.json", data_option->storage_path);
    if (stat(path, &st) == -1) return;
    struct mg_str json = mg_file_read(&mg_fs_posix, path);

    struct mg_str val;
    size_t ofs = 0;
    while ((ofs = mg_json_next(mg_json_get_tok(json, "$.receive"), ofs, NULL, &val)) > 0) {
        long id = mg_json_get_long(val, "$.id", 0);
        if (id == 0) continue;
        if (mg_json_get_str(val, "$.type")[0] == 't') {
            struct mg_str text = mg_str(mg_json_get_str(val, "$.content"));
            data_node *node = (data_node *) malloc(sizeof(data_node));
            node->format = DATA_FORMAT_TEXT;
            node->index = id;
            node_index = max(node_index, id + 1);
            node->next = NULL;
            node->u.text = (CacheText *) malloc(sizeof(CacheText) + text.len + 1);
            node->u.text->len = text.len;
            strncpy(node->u.text->data, text.ptr, text.len);
            node->u.text->data[text.len] = '\0';
            append_to_tail(node);
        } else if (mg_json_get_str(val, "$.type")[0] == 'f') {
            struct mg_str name = mg_str(mg_json_get_str(val, "$.name"));
            struct mg_str cache = mg_str(mg_json_get_str(val, "$.cache"));
            if (cache.len != FILE_ID_LEN) continue;
            data_node *node = (data_node *) malloc(sizeof(data_node));
            node->format = DATA_FORMAT_FILE;
            node->index = id;
            node_index = max(node_index, id + 1);
            node->next = NULL;
            size_t thumbnail_len = strlen(mg_json_get_str(val, "$.thumbnail"));
            node->u.file = (CacheFile *) malloc(sizeof(CacheFile) + thumbnail_len + 1);
            strncpy(node->u.file->name, name.ptr, min(name.len, MAX_PATH));
            node->u.file->name[min(name.len, MAX_PATH)] = '\0';
            strncpy(node->u.file->id, cache.ptr, min(cache.len, FILE_ID_LEN));
            node->u.file->id[min(cache.len, FILE_ID_LEN)] = '\0';
            node->u.file->available = true;
            node->u.file->expired = true; // set every file to expired
            node->u.file->expire = mg_json_get_long(val, "$.expire", 0);
            node->u.file->upload = node->u.file->expire;
            node->u.file->size = mg_json_get_long(val, "$.size", 0);
            node->u.file->fp = NULL;
            node->u.file->thumbnail_len = thumbnail_len;
            if (node->u.file->thumbnail_len) {
                strncpy(node->u.file->thumbnail, mg_json_get_str(val, "$.thumbnail"), node->u.file->thumbnail_len);
                node->u.file->thumbnail[node->u.file->thumbnail_len] = '\0';
            }
            append_to_tail(node);
        }
    }

    while ((ofs = mg_json_next(mg_json_get_tok(json, "$.file"), ofs, NULL, &val)) > 0) {
        struct mg_str uuid = mg_str(mg_json_get_str(val, "$.uuid"));
        data_node * node = find_file_node_by_id(&uuid);
        if (!node) continue;
        node->u.file->expired = false; // set file to not expired
        node->u.file->upload = mg_json_get_long(val, "$.uploadTime", (long) node->u.file->expire);
    }
}

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
        mkdir(option->storage_path, 0755);
    }
    node_index = 0;
    head = (data_node *) malloc(sizeof(data_node));
    head->format = DATA_FORMAT_HEAD;
    head->index = node_index++;
    head->next = NULL;
    load_data();
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
    char path[MAX_PATH];
    while (node) {
        data_node *next = node->next;
        if (node->format == DATA_FORMAT_FILE) {
            if (node->u.file->fp) fclose(node->u.file->fp);
            if (!node->u.file->available) {
                mg_snprintf(path, MAX_PATH, "%s/%s", data_option->storage_path, node->u.file->id);
                remove(path);
            }
            free(node->u.file);
            free(node);
        } else if (node->format == DATA_FORMAT_TEXT) {
            free(node->u.text);
            free(node);
        } else {
            free(node);
        }
        node = next;
    }
}

bool delete_node_by_index(uint32_t index) {
    if (index == 0) return false;
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
        return true;
    }
    return false;
}

void check_file_expire() {
    time_t now = time(NULL);
    data_node *node = head->next;
    while (node) {
        if (node->format == DATA_FORMAT_FILE) {
            if (node->u.file->expired == false && node->u.file->available && node->u.file->expire < now) {
                // expired file: only remove file
                node->u.file->expired = true;
                char path[MAX_PATH];
                mg_snprintf(path, MAX_PATH, "%s/%s", data_option->storage_path, node->u.file->id);
                remove(path);
                MG_INFO(("Remove expired file %s", node->u.file->id));
                save_data();
            } else if (!node->u.file->available &&
                       node->u.file->upload + data_option->upload_timeout < now) {
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
    // this check may fail, when there are cjk characters in text
     if (text->len > data_option->text_length * 4) return NULL;
    data_node *node = (data_node *) malloc(sizeof(data_node));
    node->format = DATA_FORMAT_TEXT;
    node->index = node_index++;
    node->next = NULL;
    node->u.text = (CacheText *) malloc(sizeof(CacheText) + text->len + 1);
    node->u.text->len = text->len;
    strncpy(node->u.text->data, text->ptr, text->len);
    node->u.text->data[text->len] = '\0';
    append_to_tail(node);
    save_data();
    return node;
}

data_node *new_file(struct mg_str *name) {
    char path[MAX_PATH];
    size_t len = min(name->len, MAX_PATH);
    data_node *node = (data_node *) malloc(sizeof(data_node));
    node->format = DATA_FORMAT_FILE;
    node->index = node_index++;
    node->next = NULL;
    node->u.file = (CacheFile *) malloc(sizeof(CacheFile));
    strncpy(node->u.file->name, name->ptr, len);
    node->u.file->name[len] = '\0';
    node->u.file->available = false;
    node->u.file->expired = false;
    node->u.file->thumbnail_len = 0;
    node->u.file->upload = time(NULL);
    node->u.file->expire = node->u.file->upload + data_option->expire_time;
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
    if (node->u.file->upload + data_option->upload_timeout < time(NULL))
        return cleanup_file(node);
    node->u.file->size += chunk->len;
    fwrite(chunk->ptr, 1, chunk->len, node->u.file->fp);
    return node;
}

void save_thumbnail(data_node *node, const char *thumbnail, size_t len) {
    if (!node || !thumbnail) return;
    if (node->format != DATA_FORMAT_FILE) return;
    void *new_node = realloc(node->u.file, sizeof(CacheFile) + len + 1);
    if (!new_node) return;
    node->u.file = new_node;
    node->u.file->thumbnail_len = len;
    strncpy(node->u.file->thumbnail, thumbnail, len);
    node->u.file->thumbnail[len] = '\0';
}

data_node *add_file(struct mg_str *id) {
    data_node *node = find_file_node_by_id(id);
    if (!node) return NULL;
    if (node->u.file->fp == NULL) return cleanup_file(node);
    fclose(node->u.file->fp);
    node->u.file->fp = NULL;
    node->u.file->available = true;
    size_t len;
    char path[MAX_PATH];
    mg_snprintf(path, MAX_PATH, "%s/%s", data_option->storage_path, node->u.file->id);
    const char *thumbnail = create_thumbnail(path, &len);
    if (thumbnail) save_thumbnail(node, thumbnail, len);
    save_data();
    return node;
}
