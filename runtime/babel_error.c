#include "babel_error.h"
#include "babel_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BabelErrorState babel_error_state = { .depth = 0, .current_error = NULL };

void babel_error_push(void) {
    if (babel_error_state.depth >= BABEL_MAX_ERROR_FRAMES) {
        fprintf(stderr, "Babel: error handler stack overflow\n");
        exit(1);
    }
    babel_error_state.depth++;
}

void babel_error_pop(void) {
    if (babel_error_state.depth > 0) {
        babel_error_state.depth--;
    }
}

void babel_raise(BabelValue *error) {
    if (babel_error_state.depth <= 0) {
        /* No try/catch — unhandled error, print and exit */
        char *msg = babel_to_cstring(error);
        fprintf(stderr, "Unhandled error: %s\n", msg);
        babel_free(msg);
        exit(1);
    }
    babel_error_state.current_error = error;
    babel_error_state.depth--;
    longjmp(babel_error_state.frames[babel_error_state.depth], 1);
}

void babel_raise_msg(const char *msg) {
    babel_raise(babel_string(msg));
}

BabelValue *babel_get_error(void) {
    if (babel_error_state.current_error) {
        return babel_error_state.current_error;
    }
    return babel_string("unknown error");
}

jmp_buf *babel_error_frame(void) {
    return &babel_error_state.frames[babel_error_state.depth - 1];
}
