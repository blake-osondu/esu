#include "resolver.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Function name table ---- */

typedef struct {
    char **names;
    int count;
    int capacity;
} NameTable;

static void nametable_init(NameTable *t) {
    t->capacity = 16;
    t->names = malloc(sizeof(char *) * t->capacity);
    t->count = 0;
}

static void nametable_add(NameTable *t, const char *name) {
    if (t->count >= t->capacity) {
        t->capacity *= 2;
        t->names = realloc(t->names, sizeof(char *) * t->capacity);
    }
    t->names[t->count] = malloc(strlen(name) + 1);
    strcpy(t->names[t->count], name);
    t->count++;
}

static void nametable_free(NameTable *t) {
    for (int i = 0; i < t->count; i++) free(t->names[i]);
    free(t->names);
}

static int nametable_has(NameTable *t, const char *name) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->names[i], name) == 0) return 1;
    }
    return 0;
}

/* ---- Verb stem extraction ---- */

/*
 * Try to extract the base verb from a conjugated form.
 * Returns a malloc'd string if successful, NULL if no match.
 *
 * Rules:
 *   doubled   → double    (strip -d when base ends in e)
 *   added     → add       (strip -ed)
 *   multiplied → multiply (strip -ied, add -y)
 *   doubling  → double    (strip -ing, may restore -e)
 *   multiplying → multiply (strip -ying, add -y)
 *   running   → run       (strip -ning when double consonant)
 *   doubles   → double    (strip -s)
 *   multiplies → multiply (strip -ies, add -y)
 */
static char *try_deconjugate(const char *word) {
    int len = strlen(word);
    if (len < 4) return NULL;

    char *stem = NULL;

    /* Try -ying → -y (multiplying → multiply) */
    if (len > 4 && strcmp(word + len - 4, "ying") == 0) {
        stem = malloc(len - 4 + 2);
        memcpy(stem, word, len - 4);
        stem[len - 4] = 'y';
        stem[len - 3] = '\0';
        return stem;
    }

    /* Try -ing → base (doubling → double, adding → add) */
    if (len > 3 && strcmp(word + len - 3, "ing") == 0) {
        /* Try base + e (doubling → double) */
        stem = malloc(len - 3 + 2);
        memcpy(stem, word, len - 3);
        stem[len - 3] = 'e';
        stem[len - 2] = '\0';
        /* Will check if this matches a function name; also try without e */
        return stem;
    }

    /* Try -ied → -y (multiplied → multiply) */
    if (len > 3 && strcmp(word + len - 3, "ied") == 0) {
        stem = malloc(len - 3 + 2);
        memcpy(stem, word, len - 3);
        stem[len - 3] = 'y';
        stem[len - 2] = '\0';
        return stem;
    }

    /* Try -ed → base (doubled → double via -d, added → add) */
    if (len > 2 && strcmp(word + len - 2, "ed") == 0) {
        /* First try just stripping -d (doubled → double) */
        stem = malloc(len);
        memcpy(stem, word, len - 1);
        stem[len - 1] = '\0';
        return stem;
    }

    /* Try -ies → -y (multiplies → multiply) */
    if (len > 3 && strcmp(word + len - 3, "ies") == 0) {
        stem = malloc(len - 3 + 2);
        memcpy(stem, word, len - 3);
        stem[len - 3] = 'y';
        stem[len - 2] = '\0';
        return stem;
    }

    /* Try -s → base (doubles → double) */
    if (len > 1 && word[len - 1] == 's' && word[len - 2] != 's') {
        stem = malloc(len);
        memcpy(stem, word, len - 1);
        stem[len - 1] = '\0';
        return stem;
    }

    return NULL;
}

/*
 * Given a conjugated word, find the matching function name from the table.
 * Tries multiple deconjugation strategies.
 */
static const char *find_base_function(NameTable *t, const char *word) {
    /* Direct match first */
    if (nametable_has(t, word)) return word;

    char *stem = try_deconjugate(word);
    if (!stem) return NULL;

    /* Check if stem matches */
    if (nametable_has(t, stem)) {
        /* Return the name from the table (not stem, which we'll free) */
        for (int i = 0; i < t->count; i++) {
            if (strcmp(t->names[i], stem) == 0) {
                free(stem);
                return t->names[i];
            }
        }
    }

    /* For -ing: also try without the added -e (running → runn → run? no)
     * and try stripping doubled consonant (running → run) */
    int slen = strlen(stem);
    if (slen > 1 && stem[slen - 1] == 'e') {
        /* We tried base+e, now try just base (stripping the e) */
        stem[slen - 1] = '\0';
        slen--;
        if (nametable_has(t, stem)) {
            for (int i = 0; i < t->count; i++) {
                if (strcmp(t->names[i], stem) == 0) {
                    free(stem);
                    return t->names[i];
                }
            }
        }
        /* Try removing doubled last consonant (running → runn → run) */
        if (slen > 1 && stem[slen - 1] == stem[slen - 2]) {
            stem[slen - 1] = '\0';
            if (nametable_has(t, stem)) {
                for (int i = 0; i < t->count; i++) {
                    if (strcmp(t->names[i], stem) == 0) {
                        free(stem);
                        return t->names[i];
                    }
                }
            }
        }
    }

    /* For -ed: also try stripping both chars (added → add) */
    int wlen = strlen(word);
    if (wlen > 2 && strcmp(word + wlen - 2, "ed") == 0) {
        char *stem2 = malloc(wlen - 1);
        memcpy(stem2, word, wlen - 2);
        stem2[wlen - 2] = '\0';
        if (nametable_has(t, stem2)) {
            for (int i = 0; i < t->count; i++) {
                if (strcmp(t->names[i], stem2) == 0) {
                    free(stem);
                    free(stem2);
                    return t->names[i];
                }
            }
        }
        /* Try removing doubled consonant (stopped → stopp → stop) */
        int s2len = strlen(stem2);
        if (s2len > 1 && stem2[s2len - 1] == stem2[s2len - 2]) {
            stem2[s2len - 1] = '\0';
            if (nametable_has(t, stem2)) {
                for (int i = 0; i < t->count; i++) {
                    if (strcmp(t->names[i], stem2) == 0) {
                        free(stem);
                        free(stem2);
                        return t->names[i];
                    }
                }
            }
        }
        free(stem2);
    }

    free(stem);
    return NULL;
}

/* ---- AST rewriting ---- */

static void collect_functions(ASTNode *program, NameTable *t) {
    NodeList *stmts = &program->as.program.stmts;
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_FUNCTION_DEF) {
            nametable_add(t, stmts->items[i]->as.func_def.name);
        }
    }
}

static void collect_methods(ASTNode *program, NameTable *t) {
    NodeList *stmts = &program->as.program.stmts;
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_METHOD_DEF) {
            nametable_add(t, stmts->items[i]->as.method_def.name);
        }
    }
}

static void resolve_node(ASTNode *node, NameTable *funcs);

/*
 * Check if a word ends in -ed, -ing, -s, -ied, -ies, -ying
 * (i.e., looks like a conjugated verb)
 */
static int looks_conjugated(const char *word) {
    int len = strlen(word);
    if (len < 4) return 0;
    /* Hyphenated identifiers are compound names, not verbs */
    if (strchr(word, '-')) return 0;
    if (len > 4 && strcmp(word + len - 4, "ying") == 0) return 1;
    if (len > 3 && strcmp(word + len - 3, "ing") == 0) return 1;
    if (len > 3 && strcmp(word + len - 3, "ied") == 0) return 1;
    if (len > 3 && strcmp(word + len - 3, "ies") == 0) return 1;
    if (len > 2 && strcmp(word + len - 2, "ed") == 0) return 1;
    if (word[len - 1] == 's' && word[len - 2] != 's') return 1;
    return 0;
}

static char *str_dup_r(const char *s) {
    char *d = malloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

static void resolve_list(NodeList *list, NameTable *funcs) {
    /* First pass: resolve children */
    for (int i = 0; i < list->count; i++) {
        resolve_node(list->items[i], funcs);
    }

    /*
     * Second pass: detect "EXPR conjugated" and "conjugated EXPR" patterns
     * at the expression-statement level and rewrite to function calls.
     *
     * Pattern 1: assignment "x is y doubled" → "x is double(y)"
     *   The assignment value is an identifier "y", followed by...
     *   Wait — "y doubled" parses as a single expression "y" then
     *   "doubled" is a separate expression statement. But in an
     *   assignment "x is y doubled", the parser parses "y" as the
     *   value, then "doubled" causes an error or is a separate statement.
     *
     * Actually the parser would see "x is y" as an assignment, then
     * "doubled" as a separate expression statement. So we need to
     * detect: assignment followed by a bare conjugated identifier,
     * and wrap the assignment value in a function call.
     */
    for (int i = 0; i + 1 < list->count; i++) {
        ASTNode *curr = list->items[i];
        ASTNode *next = list->items[i + 1];

        /* Pattern: [assignment/print/expr] followed by bare conjugated identifier
         * "x is 5 doubled" parses as: assignment(x, 5), expr_stmt(doubled)
         * Rewrite to: assignment(x, double(5)) and remove the extra stmt */
        if (next->type == NODE_IDENTIFIER && looks_conjugated(next->as.identifier.name)) {
            const char *base = find_base_function(funcs, next->as.identifier.name);
            if (!base) continue;

            ASTNode *arg = NULL;

            if (curr->type == NODE_ASSIGNMENT) {
                arg = curr->as.assignment.value;
            } else if (curr->type == NODE_PRINT) {
                arg = curr->as.print_stmt.expr;
            }

            if (arg) {
                /* Wrap arg in function call */
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = arg;
                ASTNode *call = ast_function_call(base, args, 1, next->line, next->col);

                if (curr->type == NODE_ASSIGNMENT) {
                    curr->as.assignment.value = call;
                } else if (curr->type == NODE_PRINT) {
                    curr->as.print_stmt.expr = call;
                }

                /* Remove the conjugated identifier statement */
                ast_free(next);
                for (int j = i + 1; j + 1 < list->count; j++) {
                    list->items[j] = list->items[j + 1];
                }
                list->count--;
                /* Don't advance i — re-check current position */
                continue;
            }
        }
    }

    /*
     * Third pass: detect bare "conjugated EXPR" as expression statements.
     * "doubling x" parses as expression stmt "doubling" then "x".
     * Rewrite to: function call double(x) — merge two stmts into one.
     */
    for (int i = 0; i + 1 < list->count; i++) {
        ASTNode *curr = list->items[i];
        ASTNode *next = list->items[i + 1];

        if (curr->type == NODE_IDENTIFIER && looks_conjugated(curr->as.identifier.name)) {
            const char *base = find_base_function(funcs, curr->as.identifier.name);
            if (!base) continue;

            /* Next must be an expression (identifier, number, string, etc.) */
            if (next->type == NODE_IDENTIFIER || next->type == NODE_NUMBER_LIT ||
                next->type == NODE_STRING_LIT) {
                /* Rewrite curr to function call with next as arg */
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = next;

                /* Overwrite curr in-place */
                free(curr->as.identifier.name);
                curr->type = NODE_FUNCTION_CALL;
                curr->as.func_call.name = str_dup_r(base);
                curr->as.func_call.args = args;
                curr->as.func_call.arg_count = 1;

                /* Remove next from list */
                for (int j = i + 1; j + 1 < list->count; j++) {
                    list->items[j] = list->items[j + 1];
                }
                list->count--;
                continue;
            }
        }
    }
}

static void resolve_node(ASTNode *node, NameTable *funcs) {
    if (!node) return;

    switch (node->type) {
        case NODE_PROGRAM:
            resolve_list(&node->as.program.stmts, funcs);
            break;

        case NODE_PRINT:
            resolve_node(node->as.print_stmt.expr, funcs);
            break;

        case NODE_ASSIGNMENT:
            resolve_node(node->as.assignment.value, funcs);
            break;

        case NODE_BINARY_EXPR:
            resolve_node(node->as.binary.left, funcs);
            resolve_node(node->as.binary.right, funcs);
            break;

        case NODE_UNARY_EXPR:
            resolve_node(node->as.unary.operand, funcs);
            break;

        case NODE_IF_STMT:
            resolve_node(node->as.if_stmt.condition, funcs);
            resolve_list(&node->as.if_stmt.body, funcs);
            resolve_list(&node->as.if_stmt.else_body, funcs);
            resolve_node(node->as.if_stmt.else_if, funcs);
            break;

        case NODE_WHILE_STMT:
            resolve_node(node->as.while_stmt.condition, funcs);
            resolve_list(&node->as.while_stmt.body, funcs);
            break;

        case NODE_FOR_RANGE_STMT:
            resolve_node(node->as.for_range.start, funcs);
            resolve_node(node->as.for_range.end, funcs);
            resolve_list(&node->as.for_range.body, funcs);
            break;

        case NODE_FOR_IN_STMT:
            resolve_node(node->as.for_in.collection, funcs);
            resolve_list(&node->as.for_in.body, funcs);
            break;

        case NODE_LOOP_STMT:
            resolve_list(&node->as.loop_stmt.body, funcs);
            break;

        case NODE_FUNCTION_DEF:
            resolve_list(&node->as.func_def.body, funcs);
            break;

        case NODE_FUNCTION_CALL:
            /* Resolve the function name itself if conjugated */
            {
                const char *base = find_base_function(funcs, node->as.func_call.name);
                if (base && strcmp(base, node->as.func_call.name) != 0) {
                    free(node->as.func_call.name);
                    node->as.func_call.name = malloc(strlen(base) + 1);
                    strcpy(node->as.func_call.name, base);
                }
                for (int i = 0; i < node->as.func_call.arg_count; i++) {
                    resolve_node(node->as.func_call.args[i], funcs);
                }
            }
            break;

        case NODE_RETURN_STMT:
            resolve_node(node->as.return_stmt.value, funcs);
            break;

        case NODE_IDENTIFIER:
            /* Check if this identifier is a conjugated function name used
             * as a bare reference. This handles cases like passing a
             * conjugated name where a function name is expected. */
            {
                const char *base = find_base_function(funcs, node->as.identifier.name);
                if (base && strcmp(base, node->as.identifier.name) != 0) {
                    free(node->as.identifier.name);
                    node->as.identifier.name = malloc(strlen(base) + 1);
                    strcpy(node->as.identifier.name, base);
                }
            }
            break;

        default:
            break;
    }
}

/* Recursively rewrite bare method calls: IDENT METHOD_NAME [args] → method_call
 * at the statement level. Detects patterns where an identifier is followed
 * by a known method name as the next statement. */
static void resolve_bare_methods(NodeList *list, NameTable *methods) {
    for (int i = 0; i + 1 < list->count; i++) {
        ASTNode *curr = list->items[i];
        ASTNode *next = list->items[i + 1];

        /* Pattern: identifier followed by known method name (as expression stmt) */
        if (curr->type == NODE_IDENTIFIER && next->type == NODE_IDENTIFIER &&
            nametable_has(methods, next->as.identifier.name)) {
            /* Merge: curr.method_name([args that follow]) */
            const char *method = next->as.identifier.name;

            /* Collect any additional expression statements as args */
            ASTNode **args = NULL;
            int arg_count = 0;
            int consumed = 1; /* will consume 'next' */

            /* Check if there are more expression stmts on the SAME LINE
             * after method name (args to the method call) */
            int method_line = next->line;
            int j = i + 2;
            int arg_cap = 4;
            args = malloc(sizeof(ASTNode *) * arg_cap);
            while (j < list->count) {
                ASTNode *arg = list->items[j];
                /* Only consume args on the same line */
                if (arg->line != method_line) break;
                if (arg->type == NODE_IDENTIFIER || arg->type == NODE_NUMBER_LIT ||
                    arg->type == NODE_STRING_LIT) {
                    if (arg_count >= arg_cap) {
                        arg_cap *= 2;
                        args = realloc(args, sizeof(ASTNode *) * arg_cap);
                    }
                    args[arg_count++] = arg;
                    consumed++;
                    j++;
                } else {
                    break;
                }
            }

            /* Rewrite curr to method call */
            ASTNode *obj = ast_identifier(curr->as.identifier.name,
                                          curr->line, curr->col);
            free(curr->as.identifier.name);
            curr->type = NODE_METHOD_CALL;
            curr->as.method_call.object = obj;
            curr->as.method_call.method_name = str_dup_r(method);
            curr->as.method_call.args = args;
            curr->as.method_call.arg_count = arg_count;

            /* Remove consumed stmts (the method name + args) */
            /* Free the method name node (not the args — they're reused) */
            free(next->as.identifier.name);
            free(next);
            int remove_count = consumed;
            for (int k = i + 1; k + remove_count < list->count; k++) {
                list->items[k] = list->items[k + remove_count];
            }
            list->count -= remove_count;
            continue;
        }
    }

    /* Recurse into blocks */
    for (int i = 0; i < list->count; i++) {
        ASTNode *n = list->items[i];
        if (n->type == NODE_IF_STMT) {
            resolve_bare_methods(&n->as.if_stmt.body, methods);
            resolve_bare_methods(&n->as.if_stmt.else_body, methods);
        } else if (n->type == NODE_WHILE_STMT) {
            resolve_bare_methods(&n->as.while_stmt.body, methods);
        } else if (n->type == NODE_FOR_RANGE_STMT) {
            resolve_bare_methods(&n->as.for_range.body, methods);
        } else if (n->type == NODE_FOR_IN_STMT) {
            resolve_bare_methods(&n->as.for_in.body, methods);
        } else if (n->type == NODE_LOOP_STMT) {
            resolve_bare_methods(&n->as.loop_stmt.body, methods);
        } else if (n->type == NODE_FUNCTION_DEF) {
            resolve_bare_methods(&n->as.func_def.body, methods);
        } else if (n->type == NODE_METHOD_DEF) {
            resolve_bare_methods(&n->as.method_def.body, methods);
        } else if (n->type == NODE_TRY_STMT) {
            resolve_bare_methods(&n->as.try_stmt.body, methods);
            resolve_bare_methods(&n->as.try_stmt.catch_body, methods);
        }
    }
}

void resolver_run(ASTNode *program) {
    NameTable funcs;
    nametable_init(&funcs);
    collect_functions(program, &funcs);

    if (funcs.count > 0) {
        resolve_node(program, &funcs);
    }

    nametable_free(&funcs);

    /* Resolve bare method calls: "alice greet" → alice.greet() */
    NameTable methods;
    nametable_init(&methods);
    collect_methods(program, &methods);
    if (methods.count > 0) {
        resolve_bare_methods(&program->as.program.stmts, &methods);
    }
    nametable_free(&methods);
}
