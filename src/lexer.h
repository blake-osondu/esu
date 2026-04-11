#ifndef LEXER_H
#define LEXER_H

#include "token.h"

#define LEXER_QUEUE_MAX 64

typedef struct {
    const char *source;
    int pos;
    int line;
    int col;
    /* Pending token queue for string interpolation expansion */
    Token queue[LEXER_QUEUE_MAX];
    int queue_count;
    int queue_read;
} Lexer;

void lexer_init(Lexer *lex, const char *source);
Token lexer_next(Lexer *lex);
void token_free(Token *tok);

#endif
