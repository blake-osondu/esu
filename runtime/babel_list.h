#ifndef BABEL_LIST_H
#define BABEL_LIST_H

#include "babel_value.h"

/* List operations */
void babel_list_append(BabelValue *list, BabelValue *item);
BabelValue *babel_list_get(BabelValue *list, BabelValue *index);
void babel_list_set(BabelValue *list, BabelValue *index, BabelValue *value);
void babel_list_remove(BabelValue *list, BabelValue *index);
void babel_list_insert(BabelValue *list, BabelValue *index, BabelValue *value);
BabelValue *babel_list_length(BabelValue *list);
BabelValue *babel_list_contains(BabelValue *list, BabelValue *item);
BabelValue *babel_list_first(BabelValue *list);
BabelValue *babel_list_last(BabelValue *list);
BabelValue *babel_list_is_empty(BabelValue *list);
BabelValue *babel_list_joined(BabelValue *list, BabelValue *separator);

/* Index expression: works for both lists and strings */
BabelValue *babel_index(BabelValue *collection, BabelValue *index);
void babel_index_set(BabelValue *collection, BabelValue *index, BabelValue *value);

#endif
