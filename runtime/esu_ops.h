#ifndef ESU_OPS_H
#define ESU_OPS_H

#include "esu_value.h"

/* Arithmetic */
EsuValue *esu_add(EsuValue *a, EsuValue *b);
EsuValue *esu_sub(EsuValue *a, EsuValue *b);
EsuValue *esu_mul(EsuValue *a, EsuValue *b);
EsuValue *esu_div(EsuValue *a, EsuValue *b);
EsuValue *esu_mod(EsuValue *a, EsuValue *b);
EsuValue *esu_neg(EsuValue *a);

/* Comparison */
EsuValue *esu_eq(EsuValue *a, EsuValue *b);
EsuValue *esu_neq(EsuValue *a, EsuValue *b);
EsuValue *esu_gt(EsuValue *a, EsuValue *b);
EsuValue *esu_lt(EsuValue *a, EsuValue *b);
EsuValue *esu_gte(EsuValue *a, EsuValue *b);
EsuValue *esu_lte(EsuValue *a, EsuValue *b);

/* Logic */
EsuValue *esu_and(EsuValue *a, EsuValue *b);
EsuValue *esu_or(EsuValue *a, EsuValue *b);
EsuValue *esu_not(EsuValue *a);

/* Truthiness check (returns C int, not EsuValue) */
int esu_is_truthy(EsuValue *a);

#endif
