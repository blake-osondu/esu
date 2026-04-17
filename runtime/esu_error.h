#ifndef ESU_ERROR_H
#define ESU_ERROR_H

#include "esu_value.h"
#include <setjmp.h>

#define ESU_MAX_ERROR_FRAMES 64

typedef struct {
    jmp_buf frames[ESU_MAX_ERROR_FRAMES];
    int depth;
    EsuValue *current_error;
} EsuErrorState;

extern EsuErrorState esu_error_state;

/* Push/pop a try frame */
void esu_error_push(void);
void esu_error_pop(void);

/* Raise an error (longjmp to nearest catch) */
void esu_raise(EsuValue *error);
void esu_raise_msg(const char *msg);

/* Get current error (in catch block) */
EsuValue *esu_get_error(void);

/* Get the jmp_buf for setjmp */
jmp_buf *esu_error_frame(void);

#endif
