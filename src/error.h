#ifndef ERROR_H
#define ERROR_H

#include "token.h"
#include <setjmp.h>

void esu_error(int line, int col, const char *fmt, ...);
void esu_error_at_token(Token *tok, const char *fmt, ...);

/* Recoverable error mode: when enabled, errors longjmp to the set recovery
 * point instead of calling exit. Used by the REPL. */
extern int esu_recoverable_errors;
extern jmp_buf esu_recovery_jmp;

#endif
