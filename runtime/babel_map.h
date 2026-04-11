#ifndef BABEL_MAP_H
#define BABEL_MAP_H

#include "babel_value.h"

/* Map operations */
BabelValue *babel_map_get(BabelValue *map, BabelValue *key);
void babel_map_set(BabelValue *map, BabelValue *key, BabelValue *value);
BabelValue *babel_map_has(BabelValue *map, BabelValue *key);
BabelValue *babel_map_keys(BabelValue *map);
BabelValue *babel_map_values(BabelValue *map);
BabelValue *babel_map_length(BabelValue *map);
void babel_map_remove(BabelValue *map, BabelValue *key);

#endif
