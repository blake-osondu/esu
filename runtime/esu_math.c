#include "esu_math.h"
#include "esu_mem.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int random_seeded = 0;

static void ensure_seeded(void) {
    if (!random_seeded) {
        srand((unsigned)time(NULL));
        random_seeded = 1;
    }
}

EsuValue *esu_abs(EsuValue *val) {
    if (val->type == VAL_NUMBER) return esu_number(fabs(val->as.number));
    return esu_number(0);
}

EsuValue *esu_floor_val(EsuValue *val) {
    if (val->type == VAL_NUMBER) return esu_number(floor(val->as.number));
    return esu_number(0);
}

EsuValue *esu_ceil_val(EsuValue *val) {
    if (val->type == VAL_NUMBER) return esu_number(ceil(val->as.number));
    return esu_number(0);
}

EsuValue *esu_round_val(EsuValue *val) {
    if (val->type == VAL_NUMBER) return esu_number(round(val->as.number));
    return esu_number(0);
}

EsuValue *esu_sqrt_val(EsuValue *val) {
    if (val->type == VAL_NUMBER) return esu_number(sqrt(val->as.number));
    return esu_number(0);
}

EsuValue *esu_power(EsuValue *base, EsuValue *exp) {
    double b = base->type == VAL_NUMBER ? base->as.number : 0;
    double e = exp->type == VAL_NUMBER ? exp->as.number : 0;
    return esu_number(pow(b, e));
}

EsuValue *esu_random_val(void) {
    ensure_seeded();
    return esu_number((double)rand() / RAND_MAX);
}

EsuValue *esu_random_between(EsuValue *min, EsuValue *max) {
    ensure_seeded();
    double lo = min->type == VAL_NUMBER ? min->as.number : 0;
    double hi = max->type == VAL_NUMBER ? max->as.number : 1;
    double r = (double)rand() / RAND_MAX;
    return esu_number(lo + r * (hi - lo));
}

EsuValue *esu_min_val(EsuValue *a, EsuValue *b) {
    double va = a->type == VAL_NUMBER ? a->as.number : 0;
    double vb = b->type == VAL_NUMBER ? b->as.number : 0;
    return esu_number(va < vb ? va : vb);
}

EsuValue *esu_max_val(EsuValue *a, EsuValue *b) {
    double va = a->type == VAL_NUMBER ? a->as.number : 0;
    double vb = b->type == VAL_NUMBER ? b->as.number : 0;
    return esu_number(va > vb ? va : vb);
}

/* Type conversion */
EsuValue *esu_to_string_val(EsuValue *val) {
    char *s = esu_to_cstring(val);
    EsuValue *result = esu_string(s);
    esu_free(s);
    return result;
}

EsuValue *esu_to_number_val(EsuValue *val) {
    if (val->type == VAL_NUMBER) return esu_number(val->as.number);
    if (val->type == VAL_STRING) {
        char *end;
        double n = strtod(val->as.string, &end);
        if (end != val->as.string) return esu_number(n);
    }
    if (val->type == VAL_BOOL) return esu_number(val->as.boolean ? 1 : 0);
    return esu_number(0);
}

/* Type checking */
EsuValue *esu_type_of(EsuValue *val) {
    return esu_string(esu_type_name(val));
}

EsuValue *esu_is_number(EsuValue *val) { return esu_bool(val->type == VAL_NUMBER); }
EsuValue *esu_is_string(EsuValue *val) { return esu_bool(val->type == VAL_STRING); }
EsuValue *esu_is_list(EsuValue *val) { return esu_bool(val->type == VAL_LIST); }
EsuValue *esu_is_bool(EsuValue *val) { return esu_bool(val->type == VAL_BOOL); }
EsuValue *esu_is_nothing_val(EsuValue *val) { return esu_bool(val->type == VAL_NOTHING); }
