#include "babel_ops.h"
#include "babel_error.h"
#include "babel_mem.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Helper: coerce to number, returns 0 if not numeric */
static double as_number(BabelValue *v) {
    if (v->type == VAL_NUMBER) return v->as.number;
    if (v->type == VAL_BOOL) return v->as.boolean ? 1.0 : 0.0;
    return 0.0;
}

/* Helper: truthiness */
static int is_truthy(BabelValue *v) {
    switch (v->type) {
        case VAL_NOTHING: return 0;
        case VAL_BOOL:    return v->as.boolean;
        case VAL_NUMBER:  return v->as.number != 0.0;
        case VAL_STRING:  return v->as.string[0] != '\0';
        case VAL_LIST:    return v->as.list->count > 0;
        case VAL_MAP:     return v->as.map->count > 0;
        case VAL_FUNCTION: return 1;
    }
    return 0;
}

/* ---- Arithmetic ---- */

BabelValue *babel_add(BabelValue *a, BabelValue *b) {
    /* String concatenation if either side is a string */
    if (a->type == VAL_STRING || b->type == VAL_STRING) {
        char *sa = babel_to_cstring(a);
        char *sb = babel_to_cstring(b);
        char *result = babel_alloc(strlen(sa) + strlen(sb) + 1);
        strcpy(result, sa);
        strcat(result, sb);
        BabelValue *val = babel_string(result);
        babel_free(sa);
        babel_free(sb);
        babel_free(result);
        return val;
    }
    return babel_number(as_number(a) + as_number(b));
}

BabelValue *babel_sub(BabelValue *a, BabelValue *b) {
    return babel_number(as_number(a) - as_number(b));
}

BabelValue *babel_mul(BabelValue *a, BabelValue *b) {
    return babel_number(as_number(a) * as_number(b));
}

BabelValue *babel_div(BabelValue *a, BabelValue *b) {
    double divisor = as_number(b);
    if (divisor == 0.0) {
        babel_raise_msg("division by zero");
        return babel_nothing();
    }
    return babel_number(as_number(a) / divisor);
}

BabelValue *babel_mod(BabelValue *a, BabelValue *b) {
    double divisor = as_number(b);
    if (divisor == 0.0) {
        babel_raise_msg("modulo by zero");
        return babel_nothing();
    }
    return babel_number(fmod(as_number(a), as_number(b)));
}

BabelValue *babel_neg(BabelValue *a) {
    return babel_number(-as_number(a));
}

/* ---- Comparison ---- */

BabelValue *babel_eq(BabelValue *a, BabelValue *b) {
    if (a->type != b->type) return babel_bool(0);
    switch (a->type) {
        case VAL_NOTHING: return babel_bool(1);
        case VAL_NUMBER:  return babel_bool(a->as.number == b->as.number);
        case VAL_STRING:  return babel_bool(strcmp(a->as.string, b->as.string) == 0);
        case VAL_BOOL:    return babel_bool(a->as.boolean == b->as.boolean);
        case VAL_LIST:    return babel_bool(a->as.list == b->as.list); /* reference equality */
        case VAL_MAP:     return babel_bool(a->as.map == b->as.map);
        case VAL_FUNCTION: return babel_bool(a->as.closure == b->as.closure);
    }
    return babel_bool(0);
}

BabelValue *babel_neq(BabelValue *a, BabelValue *b) {
    BabelValue *eq = babel_eq(a, b);
    int result = !eq->as.boolean;
    babel_release(eq);
    return babel_bool(result);
}

BabelValue *babel_gt(BabelValue *a, BabelValue *b) {
    return babel_bool(as_number(a) > as_number(b));
}

BabelValue *babel_lt(BabelValue *a, BabelValue *b) {
    return babel_bool(as_number(a) < as_number(b));
}

BabelValue *babel_gte(BabelValue *a, BabelValue *b) {
    return babel_bool(as_number(a) >= as_number(b));
}

BabelValue *babel_lte(BabelValue *a, BabelValue *b) {
    return babel_bool(as_number(a) <= as_number(b));
}

/* ---- Logic ---- */

BabelValue *babel_and(BabelValue *a, BabelValue *b) {
    return babel_bool(is_truthy(a) && is_truthy(b));
}

BabelValue *babel_or(BabelValue *a, BabelValue *b) {
    return babel_bool(is_truthy(a) || is_truthy(b));
}

BabelValue *babel_not(BabelValue *a) {
    return babel_bool(!is_truthy(a));
}

int babel_is_truthy(BabelValue *a) {
    return is_truthy(a);
}
