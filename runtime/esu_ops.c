#include "esu_ops.h"
#include "esu_error.h"
#include "esu_mem.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Helper: coerce to number, returns 0 if not numeric */
static double as_number(EsuValue *v) {
    if (v->type == VAL_NUMBER) return v->as.number;
    if (v->type == VAL_BOOL) return v->as.boolean ? 1.0 : 0.0;
    return 0.0;
}

/* Helper: truthiness */
static int is_truthy(EsuValue *v) {
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

EsuValue *esu_add(EsuValue *a, EsuValue *b) {
    /* String concatenation if either side is a string */
    if (a->type == VAL_STRING || b->type == VAL_STRING) {
        char *sa = esu_to_cstring(a);
        char *sb = esu_to_cstring(b);
        char *result = esu_alloc(strlen(sa) + strlen(sb) + 1);
        strcpy(result, sa);
        strcat(result, sb);
        EsuValue *val = esu_string(result);
        esu_free(sa);
        esu_free(sb);
        esu_free(result);
        return val;
    }
    return esu_number(as_number(a) + as_number(b));
}

EsuValue *esu_sub(EsuValue *a, EsuValue *b) {
    return esu_number(as_number(a) - as_number(b));
}

EsuValue *esu_mul(EsuValue *a, EsuValue *b) {
    return esu_number(as_number(a) * as_number(b));
}

EsuValue *esu_div(EsuValue *a, EsuValue *b) {
    double divisor = as_number(b);
    if (divisor == 0.0) {
        esu_raise_msg("division by zero");
        return esu_nothing();
    }
    return esu_number(as_number(a) / divisor);
}

EsuValue *esu_mod(EsuValue *a, EsuValue *b) {
    double divisor = as_number(b);
    if (divisor == 0.0) {
        esu_raise_msg("modulo by zero");
        return esu_nothing();
    }
    return esu_number(fmod(as_number(a), as_number(b)));
}

EsuValue *esu_neg(EsuValue *a) {
    return esu_number(-as_number(a));
}

/* ---- Comparison ---- */

EsuValue *esu_eq(EsuValue *a, EsuValue *b) {
    if (a->type != b->type) return esu_bool(0);
    switch (a->type) {
        case VAL_NOTHING: return esu_bool(1);
        case VAL_NUMBER:  return esu_bool(a->as.number == b->as.number);
        case VAL_STRING:  return esu_bool(strcmp(a->as.string, b->as.string) == 0);
        case VAL_BOOL:    return esu_bool(a->as.boolean == b->as.boolean);
        case VAL_LIST:    return esu_bool(a->as.list == b->as.list); /* reference equality */
        case VAL_MAP:     return esu_bool(a->as.map == b->as.map);
        case VAL_FUNCTION: return esu_bool(a->as.closure == b->as.closure);
    }
    return esu_bool(0);
}

EsuValue *esu_neq(EsuValue *a, EsuValue *b) {
    EsuValue *eq = esu_eq(a, b);
    int result = !eq->as.boolean;
    esu_release(eq);
    return esu_bool(result);
}

EsuValue *esu_gt(EsuValue *a, EsuValue *b) {
    return esu_bool(as_number(a) > as_number(b));
}

EsuValue *esu_lt(EsuValue *a, EsuValue *b) {
    return esu_bool(as_number(a) < as_number(b));
}

EsuValue *esu_gte(EsuValue *a, EsuValue *b) {
    return esu_bool(as_number(a) >= as_number(b));
}

EsuValue *esu_lte(EsuValue *a, EsuValue *b) {
    return esu_bool(as_number(a) <= as_number(b));
}

/* ---- Logic ---- */

EsuValue *esu_and(EsuValue *a, EsuValue *b) {
    return esu_bool(is_truthy(a) && is_truthy(b));
}

EsuValue *esu_or(EsuValue *a, EsuValue *b) {
    return esu_bool(is_truthy(a) || is_truthy(b));
}

EsuValue *esu_not(EsuValue *a) {
    return esu_bool(!is_truthy(a));
}

int esu_is_truthy(EsuValue *a) {
    return is_truthy(a);
}
