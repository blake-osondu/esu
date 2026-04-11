#ifndef BABEL_STRING_H
#define BABEL_STRING_H

#include "babel_value.h"

/* String operations — all take BabelValue* and return BabelValue* */
BabelValue *babel_length(BabelValue *val);
BabelValue *babel_substring(BabelValue *str, BabelValue *start, BabelValue *end);
BabelValue *babel_contains(BabelValue *str, BabelValue *search);
BabelValue *babel_starts_with(BabelValue *str, BabelValue *prefix);
BabelValue *babel_ends_with(BabelValue *str, BabelValue *suffix);
BabelValue *babel_to_upper(BabelValue *str);
BabelValue *babel_to_lower(BabelValue *str);
BabelValue *babel_trim(BabelValue *str);
BabelValue *babel_replace(BabelValue *str, BabelValue *old_s, BabelValue *new_s);
BabelValue *babel_split(BabelValue *str, BabelValue *delim);
BabelValue *babel_char_at(BabelValue *str, BabelValue *index);
BabelValue *babel_last_char(BabelValue *str);

/* I/O */
BabelValue *babel_input(BabelValue *prompt);
BabelValue *babel_input_no_prompt(void);

#endif
