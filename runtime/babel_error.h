#ifndef BABEL_ERROR_H
#define BABEL_ERROR_H

#include "babel_value.h"
#include <setjmp.h>

#define BABEL_MAX_ERROR_FRAMES 64

typedef struct {
    jmp_buf frames[BABEL_MAX_ERROR_FRAMES];
    int depth;
    BabelValue *current_error;
} BabelErrorState;

extern BabelErrorState babel_error_state;

/* Push/pop a try frame */
void babel_error_push(void);
void babel_error_pop(void);

/* Raise an error (longjmp to nearest catch) */
void babel_raise(BabelValue *error);
void babel_raise_msg(const char *msg);

/* Get current error (in catch block) */
BabelValue *babel_get_error(void);

/* Get the jmp_buf for setjmp */
jmp_buf *babel_error_frame(void);

#endif
