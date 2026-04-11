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
    }
    return babel_strdup("unknown");
}
