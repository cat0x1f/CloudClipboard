#pragma once

#include "data.h"

#define CONFIG_FILE "config.json"

void loadConfig(data_node_option *option, void (*cb)(const char*, void *), void * arg);