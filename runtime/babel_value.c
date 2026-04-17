#include "babel_value.h"
#include "babel_mem.h"
#include <stdio.h>
#include <string.h>

static BabelValue *babel_value_new(ValueType type) {
    BabelValue *val = babel_alloc(sizeof(BabelValue));
    val->type = type;
    val->refcount = 1;
    return val;
}

BabelValue *babel_nothing(void) {
    return babel_value_new(VAL_NOTHING);
}

BabelValue *babel_number(double n) {
    BabelValue *val = babel_value_new(VAL_NUMBER);
    val->as.number = n;
    return val;
}

BabelValue *babel_string(const char *s) {
    BabelValue *val = babel_value_new(VAL_STRING);
    val->as.string = babel_strdup(s);
    return val;
}

BabelValue *babel_bool(int b) {
    BabelValue *val = babel_value_new(VAL_BOOL);
    val->as.boolean = b;
    return val;
}

BabelValue *babel_list_new(void) {
    BabelValue *val = babel_value_new(VAL_LIST);
    val->as.list = babel_alloc(sizeof(BabelList));
    val->as.list->items = NULL;
    val->as.list->count = 0;
    val->as.list->capacity = 0;
    return val;
}

BabelValue *babel_map_new(void) {
    BabelValue *val = babel_value_new(VAL_MAP);
    val->as.map = babel_alloc(sizeof(BabelMap));
    val->as.map->entries = NULL;
    val->as.map->count = 0;
    val->as.map->capacity = 0;
    val->as.map->type_name = NULL;
    return val;
}

BabelValue *babel_struct_new(const char *type_name) {
    BabelValue *val = babel_map_new();
    val->as.map->type_name = babel_strdup(type_name);
    return val;
}

BabelValue *babel_closure_new(BabelFnPtr fn, BabelValue *env, int arity) {
    BabelValue *val = babel_value_new(VAL_FUNCTION);
    val->as.closure = babel_alloc(sizeof(BabelClosure));
    val->as.closure->fn = fn;
    val->as.closure->env = env ? babel_retain(env) : NULL;
    val->as.closure->arity = arity;
    return val;
}

BabelValue *babel_call(BabelValue *fn_val, BabelValue **args, int argc) {
    if (fn_val->type != VAL_FUNCTION) {
        return babel_nothing();
    }
    BabelClosure *c = fn_val->as.closure;
    return c->fn(args, argc, c->env);
}

BabelValue *babel_retain(BabelValue *val) {
    if (val) val->refcount++;
    return val;
}

void babel_release(BabelValue *val) {
    if (!val) return;
    val->refcount--;
    if (val->refcount <= 0) {
        if (val->type == VAL_STRING) {
            babel_free(val->as.string);
        } else if (val->type == VAL_LIST) {
            BabelList *list = val->as.list;
            for (int i = 0; i < list->count; i++) {
                babel_release(list->items[i]);
            }
            babel_free(list->items);
            babel_free(list);
        } else if (val->type == VAL_MAP) {
            BabelMap *map = val->as.map;
            for (int i = 0; i < map->count; i++) {
                babel_free(map->entries[i].key);
                babel_release(map->entries[i].value);
            }
            babel_free(map->entries);
            if (map->type_name) babel_free(map->type_name);
            babel_free(map);
        } else if (val->type == VAL_FUNCTION) {
            if (val->as.closure->env) babel_release(val->as.closure->env);
            babel_free(val->as.closure);
        }
        babel_free(val);
    }
}

const char *babel_type_name(BabelValue *val) {
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

char *babel_to_cstring(BabelValue *val) {
    char buf[64];
    switch (val->type) {
        case VAL_NOTHING:
            return babel_strdup("nothing");
        case VAL_NUMBER: {
            double n = val->as.number;
            if (n == (long long)n) {
                snprintf(buf, sizeof(buf), "%lld", (long long)n);
            } else {
                snprintf(buf, sizeof(buf), "%g", n);
            }
            return babel_strdup(buf);
        }
        case VAL_STRING:
            return babel_strdup(val->as.string);
        case VAL_BOOL:
            return babel_strdup(val->as.boolean ? "true" : "false");
        case VAL_LIST: {
            /* Build "[item1, item2, ...]" representation */
            BabelList *list = val->as.list;
            int cap = 256;
            char *out = babel_alloc(cap);
            int len = 0;
            out[len++] = '[';
            for (int i = 0; i < list->count; i++) {
                if (i > 0) { out[len++] = ','; out[len++] = ' '; }
                char *item_str = babel_to_cstring(list->items[i]);
                int item_len = strlen(item_str);
                while (len + item_len + 4 > cap) { cap *= 2; out = babel_realloc(out, cap); }
                /* Quote strings in the list display */
                if (list->items[i]->type == VAL_STRING) out[len++] = '"';
                memcpy(out + len, item_str, item_len);
                len += item_len;
                if (list->items[i]->type == VAL_STRING) out[len++] = '"';
                babel_free(item_str);
            }
            out[len++] = ']';
            out[len] = '\0';
            return out;
        }
        case VAL_MAP: {
            BabelMap *map = val->as.map;
            int cap = 256;
            char *out = babel_alloc(cap);
            int len = 0;
            /* If this is a typed struct, show "typename{...}" */
            if (map->type_name) {
                int tn_len = strlen(map->type_name);
                while (len + tn_len + 2 > cap) { cap *= 2; out = babel_realloc(out, cap); }
                memcpy(out + len, map->type_name, tn_len);
                len += tn_len;
            }
            out[len++] = '{';
            for (int i = 0; i < map->count; i++) {
                if (i > 0) { out[len++] = ','; out[len++] = ' '; }
                int key_len = strlen(map->entries[i].key);
                char *val_str = babel_to_cstring(map->entries[i].value);
                int val_len = strlen(val_str);
                while (len + key_len + val_len + 8 > cap) { cap *= 2; out = babel_realloc(out, cap); }
                out[len++] = '"';
                memcpy(out + len, map->entries[i].key, key_len);
                len += key_len;
                out[len++] = '"';
                out[len++] = ':';
                out[len++] = ' ';
                memcpy(out + len, val_str, val_len);
                len += val_len;
                babel_free(val_str);
            }
            out[len++] = '}';
            out[len] = '\0';
            return out;
        }
        case VAL_FUNCTION:
            return babel_strdup("<function>");
    }
    return babel_strdup("unknown");
}
