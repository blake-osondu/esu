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

/* Register a parent type for inheritance: child inherits from parent */
void babel_register_parent(const char *child, const char *parent);

/* Check if type `child` is `ancestor` or descends from it */
int babel_is_subtype(const char *child, const char *ancestor);

/* Check if value is an instance of type_name (walks inheritance chain).
 * type_name is passed as a string BabelValue. */
BabelValue *babel_is_instance_of(BabelValue *val, BabelValue *type_name);

#endif
