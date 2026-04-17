#ifndef ESU_METHOD_H
#define ESU_METHOD_H

#include "esu_value.h"

/* Signature for a method: first arg is self, rest are regular args */
typedef EsuValue *(*EsuMethodFn)(EsuValue **args, int argc);

/* Register a method implementation */
void esu_register_method(const char *type_name, const char *method_name,
                           EsuMethodFn fn);

/* Look up and invoke a method. Raises if not found. */
EsuValue *esu_call_method(EsuValue *self, const char *method_name,
                              EsuValue **args, int argc);

/* Check if a value has a given method */
int esu_has_method(EsuValue *self, const char *method_name);

/* Register a parent type for inheritance: child inherits from parent */
void esu_register_parent(const char *child, const char *parent);

/* Check if type `child` is `ancestor` or descends from it */
int esu_is_subtype(const char *child, const char *ancestor);

/* Check if value is an instance of type_name (walks inheritance chain).
 * type_name is passed as a string EsuValue. */
EsuValue *esu_is_instance_of(EsuValue *val, EsuValue *type_name);

#endif
