#include "config.h"
#include "logger.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct config_entry {
    char key[MAX_KEY_SIZE];
    char value[MAX_VALUE_SIZE];
    struct config_entry *next;
} Config_Entry;

#define BUCKET_SIZE 32

static Config_Entry backend[BUCKET_SIZE]     = {0};
static int config_size                       = 0;

static Config_Entry *config_map[BUCKET_SIZE] = {0};

static inline int simplehash(const char *key)
{
    unsigned int h  = 0;
    const uint8 *in = (const uint8 *)key;
    while (*in)
        h = (h * 31) + *(in++);
    return h % BUCKET_SIZE;
}

static int scan_delim(const char *ptr, char *buf, char delim)
{
    int size = 0;
    while (*ptr != delim && *ptr != '\0') {
        *buf++ = *ptr++;
        ++size;
    }

    return size;
}

int config_load(const char *filepath)
{
    int off     = 0;
    int line_nr = 0;
    FILE *fp    = fopen(filepath, "r");
    if (!fp)
        return -1;

    char line[MAX_LINE_SIZE] = {0};

    while (fgets(line, sizeof(line), fp)) {
        line_nr++;
        char *ptr = line;

        // Skip spaces and comments
        while (isspace(*ptr))
            ptr++;
        if (*ptr == '#' || *ptr == '\0')
            continue;

        char key[MAX_KEY_SIZE]     = {0};
        char value[MAX_VALUE_SIZE] = {0};
        off                        = scan_delim(ptr, key, ' ');

        ptr += off;

        // Skip spaces and comments
        while (isspace(*ptr))
            ptr++;

        off = scan_delim(ptr, value, '\n');

        if (off != 0)
            config_set(key, value);
        else
            log_error("Error reading config at line %d", line_nr);
    }

    fclose(fp);
    return 0;
}

void config_set(const char *key, const char *value)
{
    unsigned index      = simplehash(key);

    Config_Entry *entry = &backend[config_size++];

    strncpy(entry->key, key, MAX_KEY_SIZE);
    strncpy(entry->value, value, MAX_VALUE_SIZE);

    if (config_map[index] && strncmp(config_map[index]->key, key, MAX_KEY_SIZE) == 0) {
        config_map[index] = entry;
    } else {
        entry->next       = config_map[index];
        config_map[index] = entry;
    }
}

void config_set_default(void)
{
    // TODO
    config_set("log_verbosity", "debug");
}

const char *config_get(const char *key)
{
    unsigned index      = simplehash(key);
    Config_Entry *entry = config_map[index];

    while (entry) {
        if (strncmp(entry->key, key, MAX_KEY_SIZE) == 0)
            return entry->value;
        entry = entry->next;
    }

    return NULL;
}

int config_get_int(const char *key)
{
    const char *value = config_get(key);
    if (!value)
        return -1;

    return atoi(value);
}

int config_get_list(const char *key, char out[MAX_LIST_SIZE][MAX_VALUE_SIZE])
{
    const char *list = config_get(key);
    if (!list)
        return -1;

    int count = 0;
    char item[MAX_VALUE_SIZE];
    strncpy(item, list, MAX_VALUE_SIZE);

    for (char *token = strtok(item, " "); token && count < MAX_LIST_SIZE;
         token       = strtok(NULL, " "), count++) {
        strncpy(out[count], token, MAX_VALUE_SIZE);
    }

    return count;
}

int config_get_enum(const char *key)
{
    const char *value = config_get(key);
    if (!value)
        return -1;

    if (strncasecmp(value, "debug", MAX_VALUE_SIZE) == 0)
        return LL_DEBUG;
    if (strncasecmp(value, "info", MAX_VALUE_SIZE) == 0)
        return LL_INFO;
    if (strncasecmp(value, "warning", MAX_VALUE_SIZE) == 0)
        return LL_WARNING;
    if (strncasecmp(value, "error", MAX_VALUE_SIZE) == 0)
        return LL_ERROR;

    return -1;
}

void config_print(void)
{
    for (int i = 0; i < BUCKET_SIZE; ++i) {
        for (Config_Entry *entry = config_map[i]; entry; entry = entry->next) {
            log_info(">>>>: \t%s %s", entry->key, entry->value);
        }
    }
}
