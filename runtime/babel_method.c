#include "babel_method.h"
#include "babel_error.h"
#include "babel_mem.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    char *type_name;
    char *method_name;
    BabelMethodFn fn;
} MethodEntry;

static MethodEntry *method_table = NULL;
static int method_count = 0;
static int method_capacity = 0;

void babel_register_method(const char *type_name, const char *method_name,
                           BabelMethodFn fn) {
    if (method_count >= method_capacity) {
        method_capacity = method_capacity == 0 ? 16 : method_capacity * 2;
        method_table = babel_realloc(method_table,
                                     sizeof(MethodEntry) * method_capacity);
    }
    method_table[method_count].type_name = babel_strdup(type_name);
    method_table[method_count].method_name = babel_strdup(method_name);
    method_table[method_count].fn = fn;
    method_count++;
}

static BabelMethodFn find_method(const char *type_name, const char *method_name) {
    for (int i = 0; i < method_count; i++) {
        if (strcmp(method_table[i].type_name, type_name) == 0 &&
            strcmp(method_table[i].method_name, method_name) == 0) {
            return method_table[i].fn;
        }
    }
    return NULL;
}

int babel_has_method(BabelValue *self, const char *method_name) {
    const char *t = babel_type_name(self);
    return find_method(t, method_name) != NULL;
}

BabelValue *babel_call_method(BabelValue *self, const char *method_name,
                              BabelValue **args, int argc) {
    const char *t = babel_type_name(self);
    BabelMethodFn fn = find_method(t, method_name);
    if (!fn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s has no method '%s'", t, method_name);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    /* Build arg array with self prepended */
    int total = argc + 1;
    BabelValue **full = babel_alloc(sizeof(BabelValue *) * total);
    full[0] = self;
    for (int i = 0; i < argc; i++) full[i + 1] = args[i];
    BabelValue *result = fn(full, total);
    babel_free(full);
    return result;
}
