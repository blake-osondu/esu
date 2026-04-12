#ifndef BABEL_MATH_H
#define BABEL_MATH_H

#include "babel_value.h"

BabelValue *babel_abs(BabelValue *val);
BabelValue *babel_floor_val(BabelValue *val);
BabelValue *babel_ceil_val(BabelValue *val);
BabelValue *babel_round_val(BabelValue *val);
BabelValue *babel_sqrt_val(BabelValue *val);
BabelValue *babel_power(BabelValue *base, BabelValue *exp);
BabelValue *babel_random_val(void);
BabelValue *babel_random_between(BabelValue *min, BabelValue *max);
BabelValue *babel_min_val(BabelValue *a, BabelValue *b);
BabelValue *babel_max_val(BabelValue *a, BabelValue *b);

/* Type conversion */
BabelValue *babel_to_string_val(BabelValue *val);
BabelValue *babel_to_number_val(BabelValue *val);

/* Type checking */
BabelValue *babel_type_of(BabelValue *val);
BabelValue *babel_is_number(BabelValue *val);
BabelValue *babel_is_string(BabelValue *val);
BabelValue *babel_is_list(BabelValue *val);
BabelValue *babel_is_bool(BabelValue *val);
BabelValue *babel_is_nothing_val(BabelValue *val);

#endif
