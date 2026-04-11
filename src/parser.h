#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "token.h"

typedef struct {
    Token *tokens;
    int count;
    int pos;
} Parser;

void parser_init(Parser *p, Token *tokens, int count);
ASTNode *parser_parse(Parser *p);

#endif
