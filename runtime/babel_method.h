#ifndef BABEL_METHOD_H
#define BABEL_METHOD_H

#include "babel_value.h"

/* Signature for a method: first arg is self, rest are regular args */
typedef BabelValue *(*BabelMethodFn)(BabelValue **args, int argc);

/* Register a method implementation */
void babel_register_method(const char *type_name, const char *method_name,
                           BabelMethodFn fn);

/* Look up and invoke a method. Raises if not found. */
BabelValue *babel_call_method(BabelValue *self, const char *method_name,
                              BabelValue **args, int argc);

/* Check if a value has a given method */
int babel_has_method(BabelValue *self, const char *method_name);

#endif
