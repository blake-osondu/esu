#ifndef BABEL_VALUE_H
#define BABEL_VALUE_H

typedef struct BabelList BabelList;
typedef struct BabelMap BabelMap;
typedef struct BabelClosure BabelClosure;

typedef enum {
    VAL_NOTHING,
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_LIST,
    VAL_MAP,
    VAL_FUNCTION,
} ValueType;

/* Function value: a C function pointer plus an optional captured environment */
typedef struct BabelValue *(*BabelFnPtr)(struct BabelValue **args, int argc,
                                         struct BabelValue *env);

struct BabelClosure {
    BabelFnPtr fn;          /* C function implementing the body */
    struct BabelValue *env; /* Captured environment (a map of var->value) or NULL */
    int arity;              /* number of declared params (for error messages) */
};

typedef struct BabelValue {
    ValueType type;
    union {
        double number;
        char *string;
        int boolean;
        BabelList *list;
        BabelMap *map;
        BabelClosure *closure;
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
    char *type_name;  /* NULL for plain maps, set for struct instances */
};

BabelValue *babel_nothing(void);
BabelValue *babel_number(double n);
BabelValue *babel_string(const char *s);
BabelValue *babel_bool(int b);
BabelValue *babel_list_new(void);
BabelValue *babel_map_new(void);
BabelValue *babel_struct_new(const char *type_name);
BabelValue *babel_closure_new(BabelFnPtr fn, BabelValue *env, int arity);

/* Call a function value with args */
BabelValue *babel_call(BabelValue *fn_val, BabelValue **args, int argc);

BabelValue *babel_retain(BabelValue *val);
void babel_release(BabelValue *val);

const char *babel_type_name(BabelValue *val);
char *babel_to_cstring(BabelValue *val);

#endif
