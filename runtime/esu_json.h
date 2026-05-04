#ifndef ESU_JSON_H
#define ESU_JSON_H

#include "esu_value.h"

/* Parse a JSON string into an EsuValue tree:
 * JSON object → EsuMap, array → EsuList, string/number/bool/null → primitives */
EsuValue *esu_json_parse(EsuValue *json_string);

/* Convert an EsuValue to a JSON string */
EsuValue *esu_to_json(EsuValue *value);

#endif
