#include "error.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

int babel_recoverable_errors = 0;
jmp_buf babel_recovery_jmp;

static void handle_error_exit(void) {
    if (babel_recoverable_errors) {
        longjmp(babel_recovery_jmp, 1);
    }
    exit(1);
}

void babel_error(int line, int col, const char *fmt, ...) {
    fprintf(stderr, "Error at line %d, col %d: ", line, col);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    handle_error_exit();
}

void babel_error_at_token(Token *tok, const char *fmt, ...) {
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
