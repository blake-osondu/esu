#include "esu_value.h"
#include "esu_mem.h"
#include <stdio.h>
#include <string.h>

static EsuValue *esu_value_new(ValueType type) {
    EsuValue *val = esu_alloc(sizeof(EsuValue));
    val->type = type;
    val->refcount = 1;
    return val;
}

EsuValue *esu_nothing(void) {
    return esu_value_new(VAL_NOTHING);
}

EsuValue *esu_number(double n) {
    EsuValue *val = esu_value_new(VAL_NUMBER);
    val->as.number = n;
    return val;
}

EsuValue *esu_string(const char *s) {
    EsuValue *val = esu_value_new(VAL_STRING);
    val->as.string = esu_strdup(s);
    return val;
}

EsuValue *esu_bool(int b) {
    EsuValue *val = esu_value_new(VAL_BOOL);
    val->as.boolean = b;
    return val;
}

EsuValue *esu_list_new(void) {
    EsuValue *val = esu_value_new(VAL_LIST);
    val->as.list = esu_alloc(sizeof(EsuList));
    val->as.list->items = NULL;
    val->as.list->count = 0;
    val->as.list->capacity = 0;
    return val;
}

EsuValue *esu_map_new(void) {
    EsuValue *val = esu_value_new(VAL_MAP);
    val->as.map = esu_alloc(sizeof(EsuMap));
    val->as.map->entries = NULL;
    val->as.map->count = 0;
    val->as.map->capacity = 0;
    val->as.map->type_name = NULL;
    return val;
}

EsuValue *esu_struct_new(const char *type_name) {
    EsuValue *val = esu_map_new();
    val->as.map->type_name = esu_strdup(type_name);
    return val;
}

EsuValue *esu_closure_new(EsuFnPtr fn, EsuValue *env, int arity) {
    EsuValue *val = esu_value_new(VAL_FUNCTION);
    val->as.closure = esu_alloc(sizeof(EsuClosure));
    val->as.closure->fn = fn;
    val->as.closure->env = env ? esu_retain(env) : NULL;
    val->as.closure->arity = arity;
    return val;
}

EsuValue *esu_call(EsuValue *fn_val, EsuValue **args, int argc) {
    if (fn_val->type != VAL_FUNCTION) {
        return esu_nothing();
    }
    EsuClosure *c = fn_val->as.closure;
    return c->fn(args, argc, c->env);
}

EsuValue *esu_retain(EsuValue *val) {
    if (val) val->refcount++;
    return val;
}

void esu_release(EsuValue *val) {
    if (!val) return;
    val->refcount--;
    if (val->refcount <= 0) {
        if (val->type == VAL_STRING) {
            esu_free(val->as.string);
        } else if (val->type == VAL_LIST) {
            EsuList *list = val->as.list;
            for (int i = 0; i < list->count; i++) {
                esu_release(list->items[i]);
            }
            esu_free(list->items);
            esu_free(list);
        } else if (val->type == VAL_MAP) {
            EsuMap *map = val->as.map;
            for (int i = 0; i < map->count; i++) {
                esu_free(map->entries[i].key);
                esu_release(map->entries[i].value);
            }
            esu_free(map->entries);
            if (map->type_name) esu_free(map->type_name);
            esu_free(map);
        } else if (val->type == VAL_FUNCTION) {
            if (val->as.closure->env) esu_release(val->as.closure->env);
            esu_free(val->as.closure);
        }
        esu_free(val);
    }
}

const char *esu_type_name(EsuValue *val) {
    switch (val->type) {
        case VAL_NOTHING: return "nothing";
        case VAL_NUMBER:  return "number";
        case VAL_STRING:  return "string";
        case VAL_BOOL:    return "boolean";
        case VAL_LIST:    return "list";
        case VAL_MAP:
            /* Typed struct? Return the struct's type name */
            if (val->as.map->type_name) return val->as.map->type_name;
            return "map";
        case VAL_FUNCTION: return "function";
    }
    return "unknown";
}

char *esu_to_cstring(EsuValue *val) {
    char buf[64];
    switch (val->type) {
        case VAL_NOTHING:
            return esu_strdup("nothing");
        case VAL_NUMBER: {
            double n = val->as.number;
            if (n == (long long)n) {
                snprintf(buf, sizeof(buf), "%lld", (long long)n);
            } else {
                snprintf(buf, sizeof(buf), "%g", n);
            }
            return esu_strdup(buf);
        }
        case VAL_STRING:
            return esu_strdup(val->as.string);
        case VAL_BOOL:
            return esu_strdup(val->as.boolean ? "true" : "false");
        case VAL_LIST: {
            /* Build "[item1, item2, ...]" representation */
            EsuList *list = val->as.list;
            int cap = 256;
            char *out = esu_alloc(cap);
            int len = 0;
            out[len++] = '[';
            for (int i = 0; i < list->count; i++) {
                if (i > 0) { out[len++] = ','; out[len++] = ' '; }
                char *item_str = esu_to_cstring(list->items[i]);
                int item_len = strlen(item_str);
                while (len + item_len + 4 > cap) { cap *= 2; out = esu_realloc(out, cap); }
                /* Quote strings in the list display */
                if (list->items[i]->type == VAL_STRING) out[len++] = '"';
                memcpy(out + len, item_str, item_len);
                len += item_len;
                if (list->items[i]->type == VAL_STRING) out[len++] = '"';
                esu_free(item_str);
            }
            out[len++] = ']';
            out[len] = '\0';
            return out;
        }
        case VAL_MAP: {
            EsuMap *map = val->as.map;
            int cap = 256;
            char *out = esu_alloc(cap);
            int len = 0;
            /* If this is a typed struct, show "typename{...}" */
            if (map->type_name) {
                int tn_len = strlen(map->type_name);
                while (len + tn_len + 2 > cap) { cap *= 2; out = esu_realloc(out, cap); }
                memcpy(out + len, map->type_name, tn_len);
                len += tn_len;
            }
            out[len++] = '{';
            for (int i = 0; i < map->count; i++) {
                if (i > 0) { out[len++] = ','; out[len++] = ' '; }
                int key_len = strlen(map->entries[i].key);
                char *val_str = esu_to_cstring(map->entries[i].value);
                int val_len = strlen(val_str);
                while (len + key_len + val_len + 8 > cap) { cap *= 2; out = esu_realloc(out, cap); }
                out[len++] = '"';
                memcpy(out + len, map->entries[i].key, key_len);
                len += key_len;
                out[len++] = '"';
                out[len++] = ':';
                out[len++] = ' ';
                memcpy(out + len, val_str, val_len);
                len += val_len;
                esu_free(val_str);
            }
            out[len++] = '}';
            out[len] = '\0';
            return out;
        }
        case VAL_FUNCTION:
            return esu_strdup("<function>");
    }
    return esu_strdup("unknown");
}
