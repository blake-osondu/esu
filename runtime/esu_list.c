#include "esu_list.h"
#include "esu_mem.h"
#include <stdio.h>
#include <string.h>

static void list_ensure_capacity(EsuList *list, int needed) {
    if (needed <= list->capacity) return;
    int new_cap = list->capacity == 0 ? 8 : list->capacity;
    while (new_cap < needed) new_cap *= 2;
    list->items = esu_realloc(list->items, sizeof(EsuValue *) * new_cap);
    list->capacity = new_cap;
}

void esu_list_append(EsuValue *list_val, EsuValue *item) {
    if (list_val->type != VAL_LIST) return;
    EsuList *list = list_val->as.list;
    list_ensure_capacity(list, list->count + 1);
    list->items[list->count++] = esu_retain(item);
}

EsuValue *esu_list_get(EsuValue *list_val, EsuValue *index) {
    if (list_val->type != VAL_LIST) return esu_nothing();
    EsuList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0) i = list->count + i; /* negative indexing */
    if (i < 0 || i >= list->count) return esu_nothing();
    return esu_retain(list->items[i]);
}

void esu_list_set(EsuValue *list_val, EsuValue *index, EsuValue *value) {
    if (list_val->type != VAL_LIST) return;
    EsuList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0 || i >= list->count) return;
    esu_release(list->items[i]);
    list->items[i] = esu_retain(value);
}

void esu_list_remove(EsuValue *list_val, EsuValue *index) {
    if (list_val->type != VAL_LIST) return;
    EsuList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0 || i >= list->count) return;
    esu_release(list->items[i]);
    for (int j = i; j < list->count - 1; j++) {
        list->items[j] = list->items[j + 1];
    }
    list->count--;
}

void esu_list_insert(EsuValue *list_val, EsuValue *index, EsuValue *value) {
    if (list_val->type != VAL_LIST) return;
    EsuList *list = list_val->as.list;
    int i = (int)index->as.number;
    if (i < 0) i = 0;
    if (i > list->count) i = list->count;
    list_ensure_capacity(list, list->count + 1);
    for (int j = list->count; j > i; j--) {
        list->items[j] = list->items[j - 1];
    }
    list->items[i] = esu_retain(value);
    list->count++;
}

EsuValue *esu_list_length(EsuValue *list_val) {
    if (list_val->type != VAL_LIST) return esu_number(0);
    return esu_number((double)list_val->as.list->count);
}

EsuValue *esu_list_contains(EsuValue *list_val, EsuValue *item) {
    if (list_val->type != VAL_LIST) return esu_bool(0);
    EsuList *list = list_val->as.list;
    for (int i = 0; i < list->count; i++) {
        EsuValue *a = list->items[i];
        if (a->type == item->type) {
            if (a->type == VAL_NUMBER && a->as.number == item->as.number) return esu_bool(1);
            if (a->type == VAL_STRING && strcmp(a->as.string, item->as.string) == 0) return esu_bool(1);
            if (a->type == VAL_BOOL && a->as.boolean == item->as.boolean) return esu_bool(1);
        }
    }
    return esu_bool(0);
}

EsuValue *esu_list_first(EsuValue *list_val) {
    if (list_val->type != VAL_LIST || list_val->as.list->count == 0) return esu_nothing();
    return esu_retain(list_val->as.list->items[0]);
}

EsuValue *esu_list_last(EsuValue *list_val) {
    if (list_val->type != VAL_LIST || list_val->as.list->count == 0) return esu_nothing();
    return esu_retain(list_val->as.list->items[list_val->as.list->count - 1]);
}

EsuValue *esu_list_is_empty(EsuValue *list_val) {
    if (list_val->type != VAL_LIST) return esu_bool(1);
    return esu_bool(list_val->as.list->count == 0);
}

EsuValue *esu_list_joined(EsuValue *list_val, EsuValue *separator) {
    if (list_val->type != VAL_LIST) return esu_string("");
    EsuList *list = list_val->as.list;
    const char *sep = separator->type == VAL_STRING ? separator->as.string : ", ";
    int cap = 256;
    char *out = esu_alloc(cap);
    int len = 0;
    for (int i = 0; i < list->count; i++) {
        if (i > 0) {
            int sep_len = strlen(sep);
            while (len + sep_len + 1 > cap) { cap *= 2; out = esu_realloc(out, cap); }
            memcpy(out + len, sep, sep_len);
            len += sep_len;
        }
        char *item_str = esu_to_cstring(list->items[i]);
        int item_len = strlen(item_str);
        while (len + item_len + 1 > cap) { cap *= 2; out = esu_realloc(out, cap); }
        memcpy(out + len, item_str, item_len);
        len += item_len;
        esu_free(item_str);
    }
    out[len] = '\0';
    EsuValue *result = esu_string(out);
    esu_free(out);
    return result;
}

#include "esu_ops.h"  /* for truthiness */

EsuValue *esu_map_list(EsuValue *list_val, EsuValue *fn) {
    EsuValue *result = esu_list_new();
    if (list_val->type != VAL_LIST) return result;
    EsuList *list = list_val->as.list;
    for (int i = 0; i < list->count; i++) {
        EsuValue *arg[1] = { list->items[i] };
        EsuValue *transformed = esu_call(fn, arg, 1);
        esu_list_append(result, transformed);
        esu_release(transformed);
    }
    return result;
}

EsuValue *esu_filter_list(EsuValue *list_val, EsuValue *fn) {
    EsuValue *result = esu_list_new();
    if (list_val->type != VAL_LIST) return result;
    EsuList *list = list_val->as.list;
    for (int i = 0; i < list->count; i++) {
        EsuValue *arg[1] = { list->items[i] };
        EsuValue *ok = esu_call(fn, arg, 1);
        if (esu_is_truthy(ok)) {
            esu_list_append(result, list->items[i]);
        }
        esu_release(ok);
    }
    return result;
}

EsuValue *esu_reduce_list(EsuValue *list_val, EsuValue *fn, EsuValue *init) {
    if (list_val->type != VAL_LIST) return esu_retain(init);
    EsuList *list = list_val->as.list;
    EsuValue *acc = esu_retain(init);
    for (int i = 0; i < list->count; i++) {
        EsuValue *args[2] = { acc, list->items[i] };
        EsuValue *next = esu_call(fn, args, 2);
        esu_release(acc);
        acc = next;
    }
    return acc;
}

EsuValue *esu_list_slice(EsuValue *list_val, EsuValue *start, EsuValue *end) {
    EsuValue *result = esu_list_new();
    if (list_val->type != VAL_LIST) return result;
    EsuList *list = list_val->as.list;
    int a = (int)start->as.number;
    int b = (int)end->as.number;
    if (a < 0) a = 0;
    if (b > list->count) b = list->count;
    for (int i = a; i < b; i++) {
        esu_list_append(result, list->items[i]);
    }
    return result;
}

EsuValue *esu_index(EsuValue *collection, EsuValue *index) {
    if (collection->type == VAL_LIST) {
        return esu_list_get(collection, index);
    }
    if (collection->type == VAL_MAP) {
        /* Map index: person["name"] */
        if (index->type == VAL_STRING) {
            EsuMap *map = collection->as.map;
            for (int i = 0; i < map->count; i++) {
                if (strcmp(map->entries[i].key, index->as.string) == 0) {
                    return esu_retain(map->entries[i].value);
                }
            }
        }
        return esu_nothing();
    }
    if (collection->type == VAL_STRING) {
        int i = (int)index->as.number;
        int slen = strlen(collection->as.string);
        if (i < 0) i = slen + i;
        if (i < 0 || i >= slen) return esu_string("");
        char buf[2] = { collection->as.string[i], '\0' };
        return esu_string(buf);
    }
    return esu_nothing();
}

void esu_index_set(EsuValue *collection, EsuValue *index, EsuValue *value) {
    if (collection->type == VAL_LIST) {
        esu_list_set(collection, index, value);
    }
}
