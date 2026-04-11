#ifndef BABEL_OPS_H
#define BABEL_OPS_H

#include "babel_value.h"

/* Arithmetic */
BabelValue *babel_add(BabelValue *a, BabelValue *b);
BabelValue *babel_sub(BabelValue *a, BabelValue *b);
BabelValue *babel_mul(BabelValue *a, BabelValue *b);
BabelValue *babel_div(BabelValue *a, BabelValue *b);
BabelValue *babel_mod(BabelValue *a, BabelValue *b);
BabelValue *babel_neg(BabelValue *a);

/* Comparison */
BabelValue *babel_eq(BabelValue *a, BabelValue *b);
BabelValue *babel_neq(BabelValue *a, BabelValue *b);
BabelValue *babel_gt(BabelValue *a, BabelValue *b);
BabelValue *babel_lt(BabelValue *a, BabelValue *b);
BabelValue *babel_gte(BabelValue *a, BabelValue *b);
BabelValue *babel_lte(BabelValue *a, BabelValue *b);

/* Logic */
BabelValue *babel_and(BabelValue *a, BabelValue *b);
BabelValue *babel_or(BabelValue *a, BabelValue *b);
BabelValue *babel_not(BabelValue *a);

/* Truthiness check (returns C int, not BabelValue) */
int babel_is_truthy(BabelValue *a);

#endif
