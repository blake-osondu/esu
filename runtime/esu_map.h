#ifndef ESU_MAP_H
#define ESU_MAP_H

#include "esu_value.h"

/* Map operations */
EsuValue *esu_map_get(EsuValue *map, EsuValue *key);
void esu_map_set(EsuValue *map, EsuValue *key, EsuValue *value);
EsuValue *esu_map_has(EsuValue *map, EsuValue *key);
EsuValue *esu_map_keys(EsuValue *map);
EsuValue *esu_map_values(EsuValue *map);
EsuValue *esu_map_length(EsuValue *map);
void esu_map_remove(EsuValue *map, EsuValue *key);

#endif
