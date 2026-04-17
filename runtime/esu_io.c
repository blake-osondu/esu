#include "esu_io.h"
#include "esu_mem.h"
#include <stdio.h>

void esu_print(EsuValue *val) {
    char *s = esu_to_cstring(val);
    printf("%s\n", s);
    esu_free(s);
}

void esu_print_newline(void) {
    printf("\n");
}
