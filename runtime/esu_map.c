#include "esu_map.h"
#include "esu_list.h"
#include "esu_mem.h"
#include <string.h>

static int map_find(EsuMap *map, const char *key) {
    for (int i = 0; i < map->count; i++) {
        if (strcmp(map->entries[i].key, key) == 0) return i;
    }
    return -1;
}

static const char *key_as_string(EsuValue *key) {
    if (key->type == VAL_STRING) return key->as.string;
    return NULL;
}

EsuValue *esu_map_get(EsuValue *map_val, EsuValue *key) {
    if (map_val->type != VAL_MAP) return esu_nothing();
    const char *k = key_as_string(key);
    if (!k) return esu_nothing();
    int idx = map_find(map_val->as.map, k);
    if (idx < 0) return esu_nothing();
    return esu_retain(map_val->as.map->entries[idx].value);
}

void esu_map_set(EsuValue *map_val, EsuValue *key, EsuValue *value) {
    if (map_val->type != VAL_MAP) return;
    const char *k = key_as_string(key);
    if (!k) return;
    EsuMap *map = map_val->as.map;
    int idx = map_find(map, k);
    if (idx >= 0) {
        esu_release(map->entries[idx].value);
        map->entries[idx].value = esu_retain(value);
    } else {
        if (map->count >= map->capacity) {
            map->capacity = map->capacity == 0 ? 8 : map->capacity * 2;
            map->entries = esu_realloc(map->entries, sizeof(EsuMapEntry) * map->capacity);
        }
        map->entries[map->count].key = esu_strdup(k);
        map->entries[map->count].value = esu_retain(value);
        map->count++;
    }
}

EsuValue *esu_map_has(EsuValue *map_val, EsuValue *key) {
    if (map_val->type != VAL_MAP) return esu_bool(0);
    const char *k = key_as_string(key);
    if (!k) return esu_bool(0);
    return esu_bool(map_find(map_val->as.map, k) >= 0);
}

EsuValue *esu_map_keys(EsuValue *map_val) {
    EsuValue *result = esu_list_new();
    if (map_val->type != VAL_MAP) return result;
    EsuMap *map = map_val->as.map;
    for (int i = 0; i < map->count; i++) {
        EsuValue *key = esu_string(map->entries[i].key);
        esu_list_append(result, key);
        esu_release(key);
    }
    return result;
}

EsuValue *esu_map_values(EsuValue *map_val) {
    EsuValue *result = esu_list_new();
    if (map_val->type != VAL_MAP) return result;
    EsuMap *map = map_val->as.map;
    for (int i = 0; i < map->count; i++) {
        esu_list_append(result, map->entries[i].value);
    }
    return result;
}

EsuValue *esu_map_length(EsuValue *map_val) {
    if (map_val->type != VAL_MAP) return esu_number(0);
    return esu_number((double)map_val->as.map->count);
}

void esu_map_remove(EsuValue *map_val, EsuValue *key) {
    if (map_val->type != VAL_MAP) return;
    const char *k = key_as_string(key);
    if (!k) return;
    EsuMap *map = map_val->as.map;
    int idx = map_find(map, k);
    if (idx < 0) return;
    esu_free(map->entries[idx].key);
    esu_release(map->entries[idx].value);
    for (int i = idx; i < map->count - 1; i++) {
        map->entries[i] = map->entries[i + 1];
    }
    map->count--;
}
