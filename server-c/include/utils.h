#pragma once

#include "data.h"

#define CONFIG_FILE "config.json"

void loadConfig(data_node_option *option, void (*cb)(const char*, void *), void * arg);

void getTempPath(char* buf);

// Create thumbnail from image file
// @param path: image file path
// @param len: output thumbnail length
// @return thumbnail data
const char *create_thumbnail(const char *path, size_t *len);