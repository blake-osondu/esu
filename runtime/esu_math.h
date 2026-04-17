#ifndef ESU_MATH_H
#define ESU_MATH_H

#include "esu_value.h"

EsuValue *esu_abs(EsuValue *val);
EsuValue *esu_floor_val(EsuValue *val);
EsuValue *esu_ceil_val(EsuValue *val);
EsuValue *esu_round_val(EsuValue *val);
EsuValue *esu_sqrt_val(EsuValue *val);
EsuValue *esu_power(EsuValue *base, EsuValue *exp);
EsuValue *esu_random_val(void);
EsuValue *esu_random_between(EsuValue *min, EsuValue *max);
EsuValue *esu_min_val(EsuValue *a, EsuValue *b);
EsuValue *esu_max_val(EsuValue *a, EsuValue *b);

/* Type conversion */
EsuValue *esu_to_string_val(EsuValue *val);
EsuValue *esu_to_number_val(EsuValue *val);

/* Type checking */
EsuValue *esu_type_of(EsuValue *val);
EsuValue *esu_is_number(EsuValue *val);
EsuValue *esu_is_string(EsuValue *val);
EsuValue *esu_is_list(EsuValue *val);
EsuValue *esu_is_bool(EsuValue *val);
EsuValue *esu_is_nothing_val(EsuValue *val);

#endif
