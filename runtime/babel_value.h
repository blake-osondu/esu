#ifndef BABEL_VALUE_H
#define BABEL_VALUE_H

typedef enum {
    VAL_NOTHING,
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
} ValueType;

typedef struct BabelValue {
    ValueType type;
    union {
        double number;
        char *string;
        int boolean;
    } as;
    int refcount;
} BabelValue;

BabelValue *babel_nothing(void);
BabelValue *babel_number(double n);
BabelValue *babel_string(const char *s);
BabelValue *babel_bool(int b);

BabelValue *babel_retain(BabelValue *val);
void babel_release(BabelValue *val);

const char *babel_type_name(BabelValue *val);
char *babel_to_cstring(BabelValue *val);

#endif
