#ifndef BABEL_MEM_H
#define BABEL_MEM_H

#include <stddef.h>

void *babel_alloc(size_t size);
void *babel_realloc(void *ptr, size_t size);
void babel_free(void *ptr);
char *babel_strdup(const char *s);

#endif
