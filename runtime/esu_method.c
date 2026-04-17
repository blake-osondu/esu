#include "esu_method.h"
#include "esu_error.h"
#include "esu_mem.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    char *type_name;
    char *method_name;
    EsuMethodFn fn;
} MethodEntry;

static MethodEntry *method_table = NULL;
static int method_count = 0;
static int method_capacity = 0;

typedef struct {
    char *child;
    char *parent;
} ParentEntry;

static ParentEntry *parent_table = NULL;
static int parent_count = 0;
static int parent_capacity = 0;

void esu_register_parent(const char *child, const char *parent) {
    if (parent_count >= parent_capacity) {
        parent_capacity = parent_capacity == 0 ? 8 : parent_capacity * 2;
        parent_table = esu_realloc(parent_table,
                                     sizeof(ParentEntry) * parent_capacity);
    }
    parent_table[parent_count].child = esu_strdup(child);
    parent_table[parent_count].parent = esu_strdup(parent);
    parent_count++;
}

static const char *parent_of(const char *child) {
    for (int i = 0; i < parent_count; i++) {
        if (strcmp(parent_table[i].child, child) == 0) {
            return parent_table[i].parent;
        }
    }
    return NULL;
}

int esu_is_subtype(const char *child, const char *ancestor) {
    if (strcmp(child, ancestor) == 0) return 1;
    const char *p = parent_of(child);
    while (p) {
        if (strcmp(p, ancestor) == 0) return 1;
        p = parent_of(p);
    }
    return 0;
}

EsuValue *esu_is_instance_of(EsuValue *val, EsuValue *type_name) {
    if (type_name->type != VAL_STRING) return esu_bool(0);
    const char *t = esu_type_name(val);
    return esu_bool(esu_is_subtype(t, type_name->as.string));
}

void esu_register_method(const char *type_name, const char *method_name,
                           EsuMethodFn fn) {
    if (method_count >= method_capacity) {
        method_capacity = method_capacity == 0 ? 16 : method_capacity * 2;
        method_table = esu_realloc(method_table,
                                     sizeof(MethodEntry) * method_capacity);
    }
    method_table[method_count].type_name = esu_strdup(type_name);
    method_table[method_count].method_name = esu_strdup(method_name);
    method_table[method_count].fn = fn;
    method_count++;
}

static EsuMethodFn find_method_direct(const char *type_name, const char *method_name) {
    for (int i = 0; i < method_count; i++) {
        if (strcmp(method_table[i].type_name, type_name) == 0 &&
            strcmp(method_table[i].method_name, method_name) == 0) {
            return method_table[i].fn;
        }
    }
    return NULL;
}

/* Look up method walking the inheritance chain */
static EsuMethodFn find_method(const char *type_name, const char *method_name) {
    const char *t = type_name;
    while (t) {
        EsuMethodFn fn = find_method_direct(t, method_name);
        if (fn) return fn;
        t = parent_of(t);
    }
    return NULL;
}

int esu_has_method(EsuValue *self, const char *method_name) {
    const char *t = esu_type_name(self);
    return find_method(t, method_name) != NULL;
}

EsuValue *esu_call_method(EsuValue *self, const char *method_name,
                              EsuValue **args, int argc) {
    const char *t = esu_type_name(self);
    EsuMethodFn fn = find_method(t, method_name);
    if (!fn) {
        char msg[256];
        snprintf(msg, sizeof(msg), "%s has no method '%s'", t, method_name);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    /* Build arg array with self prepended */
    int total = argc + 1;
    EsuValue **full = esu_alloc(sizeof(EsuValue *) * total);
    full[0] = self;
    for (int i = 0; i < argc; i++) full[i + 1] = args[i];
    EsuValue *result = fn(full, total);
    esu_free(full);
    return result;
}
