#include "babel_math.h"
#include "babel_mem.h"
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

BabelValue *babel_abs(BabelValue *val) {
    if (val->type == VAL_NUMBER) return babel_number(fabs(val->as.number));
    return babel_number(0);
}

BabelValue *babel_floor_val(BabelValue *val) {
    if (val->type == VAL_NUMBER) return babel_number(floor(val->as.number));
    return babel_number(0);
}

BabelValue *babel_ceil_val(BabelValue *val) {
    if (val->type == VAL_NUMBER) return babel_number(ceil(val->as.number));
    return babel_number(0);
}

BabelValue *babel_round_val(BabelValue *val) {
    if (val->type == VAL_NUMBER) return babel_number(round(val->as.number));
    return babel_number(0);
}

BabelValue *babel_sqrt_val(BabelValue *val) {
    if (val->type == VAL_NUMBER) return babel_number(sqrt(val->as.number));
    return babel_number(0);
}

BabelValue *babel_power(BabelValue *base, BabelValue *exp) {
    double b = base->type == VAL_NUMBER ? base->as.number : 0;
    double e = exp->type == VAL_NUMBER ? exp->as.number : 0;
    return babel_number(pow(b, e));
}

BabelValue *babel_random_val(void) {
    ensure_seeded();
    return babel_number((double)rand() / RAND_MAX);
}

BabelValue *babel_random_between(BabelValue *min, BabelValue *max) {
    ensure_seeded();
    double lo = min->type == VAL_NUMBER ? min->as.number : 0;
    double hi = max->type == VAL_NUMBER ? max->as.number : 1;
    double r = (double)rand() / RAND_MAX;
    return babel_number(lo + r * (hi - lo));
}

BabelValue *babel_min_val(BabelValue *a, BabelValue *b) {
    double va = a->type == VAL_NUMBER ? a->as.number : 0;
    double vb = b->type == VAL_NUMBER ? b->as.number : 0;
    return babel_number(va < vb ? va : vb);
}

BabelValue *babel_max_val(BabelValue *a, BabelValue *b) {
    double va = a->type == VAL_NUMBER ? a->as.number : 0;
    double vb = b->type == VAL_NUMBER ? b->as.number : 0;
    return babel_number(va > vb ? va : vb);
}

/* Type conversion */
BabelValue *babel_to_string_val(BabelValue *val) {
    char *s = babel_to_cstring(val);
    BabelValue *result = babel_string(s);
    babel_free(s);
    return result;
}

BabelValue *babel_to_number_val(BabelValue *val) {
    if (val->type == VAL_NUMBER) return babel_number(val->as.number);
    if (val->type == VAL_STRING) {
        char *end;
        double n = strtod(val->as.string, &end);
        if (end != val->as.string) return babel_number(n);
    }
    if (val->type == VAL_BOOL) return babel_number(val->as.boolean ? 1 : 0);
    return babel_number(0);
}

/* Type checking */
BabelValue *babel_type_of(BabelValue *val) {
    return babel_string(babel_type_name(val));
}

BabelValue *babel_is_number(BabelValue *val) { return babel_bool(val->type == VAL_NUMBER); }
BabelValue *babel_is_string(BabelValue *val) { return babel_bool(val->type == VAL_STRING); }
BabelValue *babel_is_list(BabelValue *val) { return babel_bool(val->type == VAL_LIST); }
BabelValue *babel_is_bool(BabelValue *val) { return babel_bool(val->type == VAL_BOOL); }
BabelValue *babel_is_nothing_val(BabelValue *val) { return babel_bool(val->type == VAL_NOTHING); }
