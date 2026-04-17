#include "esu_mem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *esu_alloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Esu: out of memory\n");
        exit(1);
    }
    return ptr;
}

void *esu_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "Esu: out of memory\n");
        exit(1);
    }
    return new_ptr;
}

void esu_free(void *ptr) {
    free(ptr);
}

char *esu_strdup(const char *s) {
    char *dup = esu_alloc(strlen(s) + 1);
    strcpy(dup, s);
    return dup;
}
