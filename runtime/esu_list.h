#ifndef ESU_LIST_H
#define ESU_LIST_H

#include "esu_value.h"

/* List operations */
void esu_list_append(EsuValue *list, EsuValue *item);
EsuValue *esu_list_get(EsuValue *list, EsuValue *index);
void esu_list_set(EsuValue *list, EsuValue *index, EsuValue *value);
void esu_list_remove(EsuValue *list, EsuValue *index);
void esu_list_insert(EsuValue *list, EsuValue *index, EsuValue *value);
EsuValue *esu_list_length(EsuValue *list);
EsuValue *esu_list_contains(EsuValue *list, EsuValue *item);
EsuValue *esu_list_first(EsuValue *list);
EsuValue *esu_list_last(EsuValue *list);
EsuValue *esu_list_is_empty(EsuValue *list);
EsuValue *esu_list_joined(EsuValue *list, EsuValue *separator);
EsuValue *esu_list_slice(EsuValue *list, EsuValue *start, EsuValue *end);

/* Higher-order: map, filter, reduce */
EsuValue *esu_map_list(EsuValue *list, EsuValue *fn);
EsuValue *esu_filter_list(EsuValue *list, EsuValue *fn);
EsuValue *esu_reduce_list(EsuValue *list, EsuValue *fn, EsuValue *init);

/* Index expression: works for both lists and strings */
EsuValue *esu_index(EsuValue *collection, EsuValue *index);
void esu_index_set(EsuValue *collection, EsuValue *index, EsuValue *value);

#endif
