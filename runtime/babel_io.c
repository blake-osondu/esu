#include "babel_io.h"
#include "babel_mem.h"
#include <stdio.h>

void babel_print(BabelValue *val) {
    char *s = babel_to_cstring(val);
    printf("%s\n", s);
    babel_free(s);
}

void babel_print_newline(void) {
    printf("\n");
}
