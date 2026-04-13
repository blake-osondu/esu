#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "token.h"

typedef struct {
    Token *tokens;
    int count;
    int pos;
    /* Method names collected from a pre-scan, for bare method call syntax */
    char **method_names;
    int method_name_count;
    int method_name_capacity;
} Parser;

void parser_init(Parser *p, Token *tokens, int count);
ASTNode *parser_parse(Parser *p);

#endif
