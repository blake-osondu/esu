#ifndef ESU_STRING_H
#define ESU_STRING_H

#include "esu_value.h"

/* String operations — all take EsuValue* and return EsuValue* */
EsuValue *esu_length(EsuValue *val);
EsuValue *esu_substring(EsuValue *str, EsuValue *start, EsuValue *end);
EsuValue *esu_contains(EsuValue *str, EsuValue *search);
EsuValue *esu_starts_with(EsuValue *str, EsuValue *prefix);
EsuValue *esu_ends_with(EsuValue *str, EsuValue *suffix);
EsuValue *esu_to_upper(EsuValue *str);
EsuValue *esu_to_lower(EsuValue *str);
EsuValue *esu_trim(EsuValue *str);
EsuValue *esu_replace(EsuValue *str, EsuValue *old_s, EsuValue *new_s);
EsuValue *esu_split(EsuValue *str, EsuValue *delim);
EsuValue *esu_char_at(EsuValue *str, EsuValue *index);
EsuValue *esu_last_char(EsuValue *str);

/* I/O */
EsuValue *esu_input(EsuValue *prompt);
EsuValue *esu_input_no_prompt(void);

#endif
