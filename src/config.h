#pragma once

#define MAX_LINE_SIZE  256
#define MAX_KEY_SIZE   64
#define MAX_VALUE_SIZE 128
#define MAX_LIST_SIZE  16

int config_load(const char *filepath);
void config_set(const char *key, const char *value);
void config_set_default(void);
const char *config_get(const char *key);
int config_get_int(const char *key);
int config_get_list(const char *key, char out[MAX_LIST_SIZE][MAX_VALUE_SIZE]);
int config_get_enum(const char *key);
void config_print(void);
