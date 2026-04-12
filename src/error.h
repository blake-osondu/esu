#ifndef ERROR_H
#define ERROR_H

#include "token.h"
#include <setjmp.h>

void babel_error(int line, int col, const char *fmt, ...);
void babel_error_at_token(Token *tok, const char *fmt, ...);

/* Recoverable error mode: when enabled, errors longjmp to the set recovery
 * point instead of calling exit. Used by the REPL. */
extern int babel_recoverable_errors;
extern jmp_buf babel_recovery_jmp;

#endif
