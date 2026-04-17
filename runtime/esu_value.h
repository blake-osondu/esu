#ifndef ESU_VALUE_H
#define ESU_VALUE_H

typedef struct EsuList EsuList;
typedef struct EsuMap EsuMap;
typedef struct EsuClosure EsuClosure;

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
typedef struct EsuValue *(*EsuFnPtr)(struct EsuValue **args, int argc,
                                         struct EsuValue *env);

struct EsuClosure {
    EsuFnPtr fn;          /* C function implementing the body */
    struct EsuValue *env; /* Captured environment (a map of var->value) or NULL */
    int arity;              /* number of declared params (for error messages) */
};

typedef struct EsuValue {
    ValueType type;
    union {
        double number;
        char *string;
        int boolean;
        EsuList *list;
        EsuMap *map;
        EsuClosure *closure;
    } as;
    int refcount;
} EsuValue;

/* List: dynamic array of EsuValue* */
struct EsuList {
    EsuValue **items;
    int count;
    int capacity;
};

/* Map: simple key-value hash map (string keys) */
typedef struct {
    char *key;
    EsuValue *value;
} EsuMapEntry;

struct EsuMap {
    EsuMapEntry *entries;
    int count;
    int capacity;
    char *type_name;  /* NULL for plain maps, set for struct instances */
};

EsuValue *esu_nothing(void);
EsuValue *esu_number(double n);
EsuValue *esu_string(const char *s);
EsuValue *esu_bool(int b);
EsuValue *esu_list_new(void);
EsuValue *esu_map_new(void);
EsuValue *esu_struct_new(const char *type_name);
EsuValue *esu_closure_new(EsuFnPtr fn, EsuValue *env, int arity);

/* Call a function value with args */
EsuValue *esu_call(EsuValue *fn_val, EsuValue **args, int argc);

EsuValue *esu_retain(EsuValue *val);
void esu_release(EsuValue *val);

const char *esu_type_name(EsuValue *val);
char *esu_to_cstring(EsuValue *val);

#endif
