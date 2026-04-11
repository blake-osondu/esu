#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

typedef struct {
    char *output;       /* generated C source code */
    int output_len;
    int output_cap;
    int indent;         /* current indentation level */
    /* Track declared variables to distinguish declaration vs reassignment */
    char **declared_vars;
    int declared_count;
    int declared_cap;
} CodeGen;

void codegen_init(CodeGen *cg);
void codegen_generate(CodeGen *cg, ASTNode *program);
char *codegen_get_output(CodeGen *cg);
void codegen_free(CodeGen *cg);

#endif
