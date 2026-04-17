#include "esu_error.h"
#include "esu_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EsuErrorState esu_error_state = { .depth = 0, .current_error = NULL };

void esu_error_push(void) {
    if (esu_error_state.depth >= ESU_MAX_ERROR_FRAMES) {
        fprintf(stderr, "Esu: error handler stack overflow\n");
        exit(1);
    }
    esu_error_state.depth++;
}

void esu_error_pop(void) {
    if (esu_error_state.depth > 0) {
        esu_error_state.depth--;
    }
}

void esu_raise(EsuValue *error) {
    if (esu_error_state.depth <= 0) {
        /* No try/catch — unhandled error, print and exit */
        char *msg = esu_to_cstring(error);
        fprintf(stderr, "Unhandled error: %s\n", msg);
        esu_free(msg);
        exit(1);
    }
    esu_error_state.current_error = error;
    esu_error_state.depth--;
    longjmp(esu_error_state.frames[esu_error_state.depth], 1);
}

void esu_raise_msg(const char *msg) {
    esu_raise(esu_string(msg));
}

EsuValue *esu_get_error(void) {
    if (esu_error_state.current_error) {
        return esu_error_state.current_error;
    }
    return esu_string("unknown error");
}

jmp_buf *esu_error_frame(void) {
    return &esu_error_state.frames[esu_error_state.depth - 1];
}
