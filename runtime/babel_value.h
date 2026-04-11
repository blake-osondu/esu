#ifndef BABEL_VALUE_H
#define BABEL_VALUE_H

typedef struct BabelList BabelList;
typedef struct BabelMap BabelMap;

typedef enum {
    VAL_NOTHING,
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_LIST,
    VAL_MAP,
} ValueType;

typedef struct BabelValue {
    ValueType type;
    union {
        double number;
        char *string;
        int boolean;
        BabelList *list;
        BabelMap *map;
    } as;
    int refcount;
} BabelValue;

/* List: dynamic array of BabelValue* */
struct BabelList {
    BabelValue **items;
    int count;
    int capacity;
};

/* Map: simple key-value hash map (string keys) */
typedef struct {
    char *key;
    BabelValue *value;
} BabelMapEntry;

struct BabelMap {
    BabelMapEntry *entries;
    int count;
    int capacity;
};

BabelValue *babel_nothing(void);
BabelValue *babel_number(double n);
BabelValue *babel_string(const char *s);
BabelValue *babel_bool(int b);
BabelValue *babel_list_new(void);
BabelValue *babel_map_new(void);

BabelValue *babel_retain(BabelValue *val);
void babel_release(BabelValue *val);

const char *babel_type_name(BabelValue *val);
char *babel_to_cstring(BabelValue *val);

#endif
