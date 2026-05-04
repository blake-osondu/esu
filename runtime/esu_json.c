#include "esu_json.h"
#include "esu_list.h"
#include "esu_map.h"
#include "esu_mem.h"
#include "esu_error.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

/* Recursively convert a cJSON node to an EsuValue */
static EsuValue *cjson_to_esu(cJSON *item) {
    if (!item) return esu_nothing();

    if (cJSON_IsNull(item))   return esu_nothing();
    if (cJSON_IsBool(item))   return esu_bool(cJSON_IsTrue(item));
    if (cJSON_IsNumber(item)) return esu_number(item->valuedouble);
    if (cJSON_IsString(item)) return esu_string(item->valuestring);

    if (cJSON_IsArray(item)) {
        EsuValue *list = esu_list_new();
        cJSON *child = item->child;
        while (child) {
            EsuValue *val = cjson_to_esu(child);
            esu_list_append(list, val);
            esu_release(val);
            child = child->next;
        }
        return list;
    }

    if (cJSON_IsObject(item)) {
        EsuValue *map = esu_map_new();
        cJSON *child = item->child;
        while (child) {
            EsuValue *key = esu_string(child->string);
            EsuValue *val = cjson_to_esu(child);
            esu_map_set(map, key, val);
            esu_release(key);
            esu_release(val);
            child = child->next;
        }
        return map;
    }

    return esu_nothing();
}

/* Recursively convert an EsuValue to a cJSON node */
static cJSON *esu_to_cjson(EsuValue *val) {
    switch (val->type) {
        case VAL_NOTHING:  return cJSON_CreateNull();
        case VAL_BOOL:     return cJSON_CreateBool(val->as.boolean);
        case VAL_NUMBER:   return cJSON_CreateNumber(val->as.number);
        case VAL_STRING:   return cJSON_CreateString(val->as.string);
        case VAL_LIST: {
            cJSON *arr = cJSON_CreateArray();
            EsuList *list = val->as.list;
            for (int i = 0; i < list->count; i++) {
                cJSON_AddItemToArray(arr, esu_to_cjson(list->items[i]));
            }
            return arr;
        }
        case VAL_MAP: {
            cJSON *obj = cJSON_CreateObject();
            EsuMap *map = val->as.map;
            for (int i = 0; i < map->count; i++) {
                cJSON_AddItemToObject(obj, map->entries[i].key,
                                     esu_to_cjson(map->entries[i].value));
            }
            return obj;
        }
        case VAL_FUNCTION:
            return cJSON_CreateString("<function>");
    }
    return cJSON_CreateNull();
}

EsuValue *esu_json_parse(EsuValue *json_string) {
    if (json_string->type != VAL_STRING) {
        esu_raise_msg("json parse expects a string");
        return esu_nothing();
    }
    cJSON *root = cJSON_Parse(json_string->as.string);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        char msg[256];
        snprintf(msg, sizeof(msg), "invalid JSON: %s",
                 err ? err : "unknown error");
        esu_raise_msg(msg);
        return esu_nothing();
    }
    EsuValue *result = cjson_to_esu(root);
    cJSON_Delete(root);
    return result;
}

EsuValue *esu_to_json(EsuValue *value) {
    cJSON *root = esu_to_cjson(value);
    char *str = cJSON_PrintUnformatted(root);
    EsuValue *result = esu_string(str);
    cJSON_free(str);
    cJSON_Delete(root);
    return result;
}
