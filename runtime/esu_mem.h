#ifndef ESU_MEM_H
#define ESU_MEM_H

#include <stddef.h>

void *esu_alloc(size_t size);
void *esu_realloc(void *ptr, size_t size);
void esu_free(void *ptr);
char *esu_strdup(const char *s);

#endif
