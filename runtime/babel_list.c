#include "babel_list.h"
#include "babel_mem.h"
#include <stdio.h>
#include <string.h>

static void list_ensure_capacity(BabelList *list, int needed) {
    if (needed <= list->capacity) return;
    int new_cap = list->capacity == 0 ? 8 : list->capacity;
    while (new_cap < needed) new_cap *= 2;
    list->items = babel_realloc(list->items, sizeof(BabelValue *) * new_cap);
    list->capacity = new_cap;
}

void babel_list_append(BabelValue *list_val, BabelValue *item) {
    if (list_val->type != VAL_LIST) return;
    BabelList *list = list_val->as.list;
    list_ensure_capacity(list, list->count + 1);
    list->items[list->count++] = babel_retain(item);
}

BabelValue *babel_list_get(BabelValue *list_val, BabelValue *index) {
    if (list_val->type != VAL_LIST) return babel_nothing();
    BabelList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0) i = list->count + i; /* negative indexing */
    if (i < 0 || i >= list->count) return babel_nothing();
    return babel_retain(list->items[i]);
}

void babel_list_set(BabelValue *list_val, BabelValue *index, BabelValue *value) {
    if (list_val->type != VAL_LIST) return;
    BabelList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0 || i >= list->count) return;
    babel_release(list->items[i]);
    list->items[i] = babel_retain(value);
}

void babel_list_remove(BabelValue *list_val, BabelValue *index) {
    if (list_val->type != VAL_LIST) return;
    BabelList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0 || i >= list->count) return;
    babel_release(list->items[i]);
    for (int j = i; j < list->count - 1; j++) {
        list->items[j] = list->items[j + 1];
    }
    list->count--;
}

void babel_list_insert(BabelValue *list_val, BabelValue *index, BabelValue *value) {
    if (list_val->type != VAL_LIST) return;
    BabelList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0) i = 0;
    if (i > list->count) i = list->count;
    list_ensure_capacity(list, list->count + 1);
    for (int j = list->count; j > i; j--) {
        list->items[j] = list->items[j - 1];
    }
    list->items[i] = babel_retain(value);
    list->count++;
}

BabelValue *babel_list_length(BabelValue *list_val) {
    if (list_val->type != VAL_LIST) return babel_number(0);
    return babel_number((double)list_val->as.list->count);
}

BabelValue *babel_list_contains(BabelValue *list_val, BabelValue *item) {
    if (list_val->type != VAL_LIST) return babel_bool(0);
    BabelList *list = list_val->as.list;
    for (int i = 0; i < list->count; i++) {
        BabelValue *a = list->items[i];
        if (a->type == item->type) {
            if (a->type == VAL_NUMBER && a->as.number == item->as.number) return babel_bool(1);
            if (a->type == VAL_STRING && strcmp(a->as.string, item->as.string) == 0) return babel_bool(1);
            if (a->type == VAL_BOOL && a->as.boolean == item->as.boolean) return babel_bool(1);
        }
    }
    return babel_bool(0);
}

BabelValue *babel_list_first(BabelValue *list_val) {
    if (list_val->type != VAL_LIST || list_val->as.list->count == 0) return babel_nothing();
    return babel_retain(list_val->as.list->items[0]);
}

BabelValue *babel_list_last(BabelValue *list_val) {
    if (list_val->type != VAL_LIST || list_val->as.list->count == 0) return babel_nothing();
    return babel_retain(list_val->as.list->items[list_val->as.list->count - 1]);
}

BabelValue *babel_list_is_empty(BabelValue *list_val) {
    if (list_val->type != VAL_LIST) return babel_bool(1);
    return babel_bool(list_val->as.list->count == 0);
}

BabelValue *babel_list_joined(BabelValue *list_val, BabelValue *separator) {
    if (list_val->type != VAL_LIST) return babel_string("");
    BabelList *list = list_val->as.list;
    const char *sep = separator->type == VAL_STRING ? separator->as.string : ", ";
    int cap = 256;
    char *out = babel_alloc(cap);
    int len = 0;
    for (int i = 0; i < list->count; i++) {
        if (i > 0) {
            int sep_len = strlen(sep);
            while (len + sep_len + 1 > cap) { cap *= 2; out = babel_realloc(out, cap); }
            memcpy(out + len, sep, sep_len);
            len += sep_len;
        }
        char *item_str = babel_to_cstring(list->items[i]);
        int item_len = strlen(item_str);
        while (len + item_len + 1 > cap) { cap *= 2; out = babel_realloc(out, cap); }
        memcpy(out + len, item_str, item_len);
        len += item_len;
        babel_free(item_str);
    }
    out[len] = '\0';
    BabelValue *result = babel_string(out);
    babel_free(out);
    return result;
}

#include "babel_ops.h"  /* for truthiness */

BabelValue *babel_map_list(BabelValue *list_val, BabelValue *fn) {
    BabelValue *result = babel_list_new();
    if (list_val->type != VAL_LIST) return result;
    BabelList *list = list_val->as.list;
    for (int i = 0; i < list->count; i++) {
        BabelValue *arg[1] = { list->items[i] };
        BabelValue *transformed = babel_call(fn, arg, 1);
        babel_list_append(result, transformed);
        babel_release(transformed);
    }
    return result;
}

BabelValue *babel_filter_list(BabelValue *list_val, BabelValue *fn) {
    BabelValue *result = babel_list_new();
    if (list_val->type != VAL_LIST) return result;
    BabelList *list = list_val->as.list;
    for (int i = 0; i < list->count; i++) {
        BabelValue *arg[1] = { list->items[i] };
        BabelValue *ok = babel_call(fn, arg, 1);
        if (babel_is_truthy(ok)) {
            babel_list_append(result, list->items[i]);
        }
        babel_release(ok);
    }
    return result;
}

BabelValue *babel_reduce_list(BabelValue *list_val, BabelValue *fn, BabelValue *init) {
    if (list_val->type != VAL_LIST) return babel_retain(init);
    BabelList *list = list_val->as.list;
    BabelValue *acc = babel_retain(init);
    for (int i = 0; i < list->count; i++) {
        BabelValue *args[2] = { acc, list->items[i] };
        BabelValue *next = babel_call(fn, args, 2);
        babel_release(acc);
        acc = next;
    }
    return acc;
}

BabelValue *babel_list_slice(BabelValue *list_val, BabelValue *start, BabelValue *end) {
    BabelValue *result = babel_list_new();
    if (list_val->type != VAL_LIST) return result;
    BabelList *list = list_val->as.list;
    int a = (int)start->as.number;
    int b = (int)end->as.number;
    if (a < 0) a = 0;
    if (b > list->count) b = list->count;
    for (int i = a; i < b; i++) {
        babel_list_append(result, list->items[i]);
    }
    return result;
}

BabelValue *babel_index(BabelValue *collection, BabelValue *index) {
    if (collection->type == VAL_LIST) {
        return babel_list_get(collection, index);
    }
    if (collection->type == VAL_MAP) {
        /* Map index: person["name"] */
        if (index->type == VAL_STRING) {
            BabelMap *map = collection->as.map;
            for (int i = 0; i < map->count; i++) {
                if (strcmp(map->entries[i].key, index->as.string) == 0) {
                    return babel_retain(map->entries[i].value);
                }
            }
        }
        return babel_nothing();
    }
    if (collection->type == VAL_STRING) {
        int i = (int)index->as.number;
        int slen = strlen(collection->as.string);
        if (i < 0) i = slen + i;
        if (i < 0 || i >= slen) return babel_string("");
        char buf[2] = { collection->as.string[i], '\0' };
        return babel_string(buf);
    }
    return babel_nothing();
}

void babel_index_set(BabelValue *collection, BabelValue *index, BabelValue *value) {
    if (collection->type == VAL_LIST) {
        babel_list_set(collection, index, value);
    }
}
