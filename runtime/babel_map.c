#include "babel_map.h"
#include "babel_list.h"
#include "babel_mem.h"
#include <string.h>

static int map_find(BabelMap *map, const char *key) {
    for (int i = 0; i < map->count; i++) {
        if (strcmp(map->entries[i].key, key) == 0) return i;
    }
    return -1;
}

static const char *key_as_string(BabelValue *key) {
    if (key->type == VAL_STRING) return key->as.string;
    return NULL;
}

BabelValue *babel_map_get(BabelValue *map_val, BabelValue *key) {
    if (map_val->type != VAL_MAP) return babel_nothing();
    const char *k = key_as_string(key);
    if (!k) return babel_nothing();
    int idx = map_find(map_val->as.map, k);
    if (idx < 0) return babel_nothing();
    return babel_retain(map_val->as.map->entries[idx].value);
}

void babel_map_set(BabelValue *map_val, BabelValue *key, BabelValue *value) {
    if (map_val->type != VAL_MAP) return;
    const char *k = key_as_string(key);
    if (!k) return;
    BabelMap *map = map_val->as.map;
    int idx = map_find(map, k);
    if (idx >= 0) {
        babel_release(map->entries[idx].value);
        map->entries[idx].value = babel_retain(value);
    } else {
        if (map->count >= map->capacity) {
            map->capacity = map->capacity == 0 ? 8 : map->capacity * 2;
            map->entries = babel_realloc(map->entries, sizeof(BabelMapEntry) * map->capacity);
        }
        map->entries[map->count].key = babel_strdup(k);
        map->entries[map->count].value = babel_retain(value);
        map->count++;
    }
}

BabelValue *babel_map_has(BabelValue *map_val, BabelValue *key) {
    if (map_val->type != VAL_MAP) return babel_bool(0);
    const char *k = key_as_string(key);
    if (!k) return babel_bool(0);
    return babel_bool(map_find(map_val->as.map, k) >= 0);
}

BabelValue *babel_map_keys(BabelValue *map_val) {
    BabelValue *result = babel_list_new();
    if (map_val->type != VAL_MAP) return result;
    BabelMap *map = map_val->as.map;
    for (int i = 0; i < map->count; i++) {
        BabelValue *key = babel_string(map->entries[i].key);
        babel_list_append(result, key);
        babel_release(key);
    }
    return result;
}

BabelValue *babel_map_values(BabelValue *map_val) {
    BabelValue *result = babel_list_new();
    if (map_val->type != VAL_MAP) return result;
    BabelMap *map = map_val->as.map;
    for (int i = 0; i < map->count; i++) {
        babel_list_append(result, map->entries[i].value);
    }
    return result;
}

BabelValue *babel_map_length(BabelValue *map_val) {
    if (map_val->type != VAL_MAP) return babel_number(0);
    return babel_number((double)map_val->as.map->count);
}

void babel_map_remove(BabelValue *map_val, BabelValue *key) {
    if (map_val->type != VAL_MAP) return;
    const char *k = key_as_string(key);
    if (!k) return;
    BabelMap *map = map_val->as.map;
    int idx = map_find(map, k);
    if (idx < 0) return;
    babel_free(map->entries[idx].key);
    babel_release(map->entries[idx].value);
    for (int i = idx; i < map->count - 1; i++) {
        map->entries[i] = map->entries[i + 1];
    }
    map->count--;
}
