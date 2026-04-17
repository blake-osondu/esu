#include "error.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

int esu_recoverable_errors = 0;
jmp_buf esu_recovery_jmp;

static void handle_error_exit(void) {
    if (esu_recoverable_errors) {
        longjmp(esu_recovery_jmp, 1);
    }
    exit(1);
}

void esu_error(int line, int col, const char *fmt, ...) {
    fprintf(stderr, "Error at line %d, col %d: ", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    handle_error_exit();
}

void esu_error_at_token(Token *tok, const char *fmt, ...) {
    fprintf(stderr, "Error at line %d, col %d", tok->line, tok->col);
    if (tok->type != TOK_EOF && tok->type != TOK_NEWLINE) {
        fprintf(stderr, " near '%s'", tok->value);
    }
    fprintf(stderr, ": ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    handle_error_exit();
}
