#include "babel_mem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *babel_alloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Babel: out of memory\n");
        exit(1);
    }
    return ptr;
}

void *babel_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "Babel: out of memory\n");
        exit(1);
    }
    return new_ptr;
}

void babel_free(void *ptr) {
    free(ptr);
}

char *babel_strdup(const char *s) {
    char *dup = babel_alloc(strlen(s) + 1);
    strcpy(dup, s);
    return dup;
}
