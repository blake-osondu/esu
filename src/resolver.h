#ifndef RESOLVER_H
#define RESOLVER_H

#include "ast.h"

/*
 * Resolve verb conjugations to function calls.
 *
 * After parsing, scans the AST for function definitions to build a
 * name table. Then walks the tree looking for patterns like:
 *   - "x doubled"  → double(x)    (past tense after expression)
 *   - "doubling x" → double(x)    (gerund before expression)
 *   - "doubles x"  → double(x)    (third person before expression)
 *
 * Modifies the AST in place.
 */
void resolver_run(ASTNode *program);

#endif
