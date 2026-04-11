#include "parser.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void parser_init(Parser *p, Token *tokens, int count) {
    p->tokens = tokens;
    p->count = count;
    p->pos = 0;
}

static Token *current(Parser *p) {
    return &p->tokens[p->pos];
}

static Token *advance_parser(Parser *p) {
    Token *tok = &p->tokens[p->pos];
    if (p->pos < p->count - 1) p->pos++;
    return tok;
}

static int check_type(Parser *p, TokenType type) {
    return current(p)->type == type;
}

static int check_word(Parser *p, const char *word) {
    return current(p)->type == TOK_WORD && strcmp(current(p)->value, word) == 0;
}

static Token *expect_type(Parser *p, TokenType type, const char *msg) {
    if (!check_type(p, type)) {
        babel_error_at_token(current(p), "%s", msg);
    }
    return advance_parser(p);
}

static void expect_word(Parser *p, const char *word, const char *msg) {
    if (!check_word(p, word)) {
        babel_error_at_token(current(p), "%s", msg);
    }
    advance_parser(p);
}

/* Skip statement separators: newlines and commas between statements */
static void skip_separators(Parser *p) {
    while (check_type(p, TOK_NEWLINE) || check_type(p, TOK_COMMA)) {
        advance_parser(p);
    }
}

/* Check if current token is a statement separator (newline or comma) */
static int is_separator(Parser *p) {
    return check_type(p, TOK_NEWLINE) || check_type(p, TOK_COMMA);
}

/* Consume 'do' or a separator — makes 'do' optional */
static void expect_block_start(Parser *p) {
    if (check_word(p, "do")) {
        advance_parser(p);
    }
    skip_separators(p);
}

static int is_print_keyword(const char *word) {
    return strcmp(word, "display") == 0 ||
           strcmp(word, "print") == 0 ||
           strcmp(word, "show") == 0 ||
           strcmp(word, "say") == 0;
}

static int is_block_end(Parser *p) {
    if (check_type(p, TOK_EOF)) return 1;
    if (check_word(p, "end")) return 1;
    if (check_type(p, TOK_DOT)) return 1;  /* period terminates blocks */
    if (check_word(p, "else")) return 1;
    if (check_word(p, "until")) return 1;
    return 0;
}

/* Consume a block terminator: "end" or "." */
static void expect_block_end(Parser *p, const char *context) {
    if (check_word(p, "end")) {
        advance_parser(p);
    } else if (check_type(p, TOK_DOT)) {
        advance_parser(p);
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected 'end' or '.' to close %s", context);
        babel_error_at_token(current(p), "%s", msg);
    }
}

/* Forward declarations */
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_comparison(Parser *p);
static ASTNode *parse_statement(Parser *p);
static void parse_block(Parser *p, NodeList *list);

/* ---- Expression parsing ---- */

/* Check if current matches a word, and the next matches another */
static int check_two_words(Parser *p, const char *w1, const char *w2) {
    return check_word(p, w1) &&
           p->pos + 1 < p->count &&
           p->tokens[p->pos + 1].type == TOK_WORD &&
           strcmp(p->tokens[p->pos + 1].value, w2) == 0;
}


static ASTNode *parse_primary(Parser *p) {
    Token *tok = current(p);

    /* Named math operations: "the sum of X and Y" → X + Y
     * "the" is stripped by normalizer in mid-expression, so we check for
     * "sum of", "product of", "difference of", "quotient of" */
    if (check_two_words(p, "sum", "of")) {
        Token *op = advance_parser(p); advance_parser(p); /* consume "sum of" */
        ASTNode *left = parse_comparison(p);
        expect_word(p, "and", "expected 'and' after first operand in 'the sum of X and Y'");
        ASTNode *right = parse_comparison(p);
        return ast_binary(left, "+", right, op->line, op->col);
    }
    if (check_two_words(p, "difference", "of") || check_two_words(p, "difference", "between")) {
        Token *op = advance_parser(p); advance_parser(p);
        ASTNode *left = parse_comparison(p);
        if (check_word(p, "and") || check_word(p, "from")) advance_parser(p);
        else babel_error_at_token(current(p), "expected 'and' in 'the difference of X and Y'");
        ASTNode *right = parse_comparison(p);
        return ast_binary(left, "-", right, op->line, op->col);
    }
    if (check_two_words(p, "product", "of")) {
        Token *op = advance_parser(p); advance_parser(p);
        ASTNode *left = parse_comparison(p);
        expect_word(p, "and", "expected 'and' in 'the product of X and Y'");
        ASTNode *right = parse_comparison(p);
        return ast_binary(left, "*", right, op->line, op->col);
    }
    if (check_two_words(p, "quotient", "of")) {
        Token *op = advance_parser(p); advance_parser(p);
        ASTNode *left = parse_comparison(p);
        expect_word(p, "and", "expected 'and' in 'the quotient of X and Y'");
        ASTNode *right = parse_comparison(p);
        return ast_binary(left, "/", right, op->line, op->col);
    }
    if (check_two_words(p, "remainder", "of")) {
        Token *op = advance_parser(p); advance_parser(p);
        ASTNode *left = parse_comparison(p);
        if (check_word(p, "and")) advance_parser(p);
        else expect_word(p, "divided", "expected 'and' or 'divided by' in remainder expression");
        if (check_word(p, "by")) advance_parser(p); /* consume optional "by" after "divided" */
        ASTNode *right = parse_comparison(p);
        return ast_binary(left, "%", right, op->line, op->col);
    }

    /* "the result of calling NAME with ARGS" / "the result of NAME(ARGS)" */
    if (check_two_words(p, "result", "of")) {
        advance_parser(p); advance_parser(p); /* consume "result of" */
        /* "calling NAME with" → rewrite to "call NAME with" for the call parser */
        if (check_word(p, "calling")) {
            /* Replace "calling" with "call" conceptually — just skip it
             * and emit a call-style parse */
            advance_parser(p); /* skip "calling" */
            /* Inject "call" behavior: parse as call NAME with ARGS */
            Token *name = expect_type(p, TOK_WORD, "expected function name after 'result of calling'");
            int arg_cap = 8;
            ASTNode **args = malloc(sizeof(ASTNode *) * arg_cap);
            int arg_count = 0;
            if (check_word(p, "with")) {
                advance_parser(p);
                args[arg_count++] = parse_comparison(p);
                while (check_word(p, "and") || check_type(p, TOK_COMMA)) {
                    int was_comma = check_type(p, TOK_COMMA);
                    advance_parser(p);
                    if (was_comma && check_word(p, "and")) advance_parser(p);
                    if (arg_count >= arg_cap) {
                        arg_cap *= 2;
                        args = realloc(args, sizeof(ASTNode *) * arg_cap);
                    }
                    args[arg_count++] = parse_comparison(p);
                }
            }
            return ast_function_call(name->value, args, arg_count, tok->line, tok->col);
        }
        /* Otherwise parse whatever follows (e.g., name(args)) */
        return parse_primary(p);
    }

    if (tok->type == TOK_NUMBER) {
        advance_parser(p);
        return ast_number_lit(atof(tok->value), tok->line, tok->col);
    }

    if (tok->type == TOK_STRING) {
        advance_parser(p);
        return ast_string_lit(tok->value, tok->line, tok->col);
    }

    /* "call NAME with ARGS" in expression context.
     * Arguments are separated by "and" or "," or ", and" (Oxford comma).
     * We parse each argument with parse_comparison (not full parse_expression)
     * so that "and" acts as separator, not boolean AND. */
    if (tok->type == TOK_WORD && strcmp(tok->value, "call") == 0) {
        advance_parser(p); /* consume 'call' */
        Token *name = expect_type(p, TOK_WORD, "expected function name after 'call'");
        int arg_cap = 8;
        ASTNode **args = malloc(sizeof(ASTNode *) * arg_cap);
        int arg_count = 0;
        if (check_word(p, "with")) {
            advance_parser(p);
            args[arg_count++] = parse_comparison(p);
            while (check_word(p, "and") || check_type(p, TOK_COMMA)) {
                int was_comma = check_type(p, TOK_COMMA);
                advance_parser(p); /* consume "and" or "," */
                /* Skip "and" after "," for Oxford comma: "1, 2, and 3" */
                if (was_comma && check_word(p, "and")) advance_parser(p);
                if (arg_count >= arg_cap) {
                    arg_cap *= 2;
                    args = realloc(args, sizeof(ASTNode *) * arg_cap);
                }
                args[arg_count++] = parse_comparison(p);
            }
        }
        return ast_function_call(name->value, args, arg_count, tok->line, tok->col);
    }

    if (tok->type == TOK_WORD) {
        if (strcmp(tok->value, "true") == 0) {
            advance_parser(p);
            return ast_bool_lit(1, tok->line, tok->col);
        }
        if (strcmp(tok->value, "false") == 0) {
            advance_parser(p);
            return ast_bool_lit(0, tok->line, tok->col);
        }
        if (strcmp(tok->value, "nothing") == 0) {
            advance_parser(p);
            return ast_nothing_lit(tok->line, tok->col);
        }
        /* Identifier — check for function call with parens */
        advance_parser(p);
        if (check_type(p, TOK_LPAREN)) {
            /* Function call: name(arg1, arg2, ...) */
            advance_parser(p); /* consume '(' */
            int arg_cap = 8;
            ASTNode **args = malloc(sizeof(ASTNode *) * arg_cap);
            int arg_count = 0;
            if (!check_type(p, TOK_RPAREN)) {
                args[arg_count++] = parse_expression(p);
                while (check_type(p, TOK_COMMA)) {
                    advance_parser(p);
                    if (arg_count >= arg_cap) {
                        arg_cap *= 2;
                        args = realloc(args, sizeof(ASTNode *) * arg_cap);
                    }
                    args[arg_count++] = parse_expression(p);
                }
            }
            expect_type(p, TOK_RPAREN, "expected ')' after function arguments");
            return ast_function_call(tok->value, args, arg_count, tok->line, tok->col);
        }
        return ast_identifier(tok->value, tok->line, tok->col);
    }

    if (tok->type == TOK_LPAREN) {
        advance_parser(p);
        ASTNode *expr = parse_expression(p);
        expect_type(p, TOK_RPAREN, "expected ')'");
        return expr;
    }

    babel_error_at_token(tok, "expected an expression");
    return NULL;
}

/* Check if current token looks like a conjugated verb (past tense or third person).
 * Used for postfix function call: "x doubled" → double(x) */
static int looks_like_past_tense(Parser *p) {
    if (!check_type(p, TOK_WORD)) return 0;
    const char *w = current(p)->value;
    int len = strlen(w);
    if (len < 4) return 0;
    /* Don't match keywords */
    if (strcmp(w, "continued") == 0 || strcmp(w, "returned") == 0 ||
        strcmp(w, "called") == 0 || strcmp(w, "displayed") == 0) return 0;
    if (len > 3 && strcmp(w + len - 3, "ied") == 0) return 1;
    if (len > 2 && strcmp(w + len - 2, "ed") == 0) return 1;
    return 0;
}

/* Check if current token looks like a gerund (-ing) or third-person (-s/-es) form.
 * Used for prefix function call: "doubling x" → double(x), "doubles x" → double(x) */
static int looks_like_prefix_verb(Parser *p) {
    if (!check_type(p, TOK_WORD)) return 0;
    const char *w = current(p)->value;
    int len = strlen(w);
    if (len < 4) return 0;
    /* Don't match keywords */
    if (strcmp(w, "taking") == 0 || strcmp(w, "calling") == 0 ||
        strcmp(w, "equals") == 0 || strcmp(w, "exceeds") == 0 ||
        strcmp(w, "nothing") == 0 || strcmp(w, "this") == 0) return 0;
    if (len > 4 && strcmp(w + len - 4, "ying") == 0) return 1;
    if (len > 3 && strcmp(w + len - 3, "ing") == 0) return 1;
    if (len > 3 && strcmp(w + len - 3, "ies") == 0) return 1;
    if (len > 2 && strcmp(w + len - 2, "es") == 0 && w[len - 3] != 'l') return 1;
    if (w[len - 1] == 's' && w[len - 2] != 's' && w[len - 2] != 'i' &&
        !isdigit(w[len - 2])) return 1;
    return 0;
}

/* Parse postfix conjugation: after a primary expression, check for past-tense
 * verb and wrap as function call.
 * "x doubled"           → double(x)       (single-arg, no second operand)
 * "x multiplied by y"   → multiply(x, y)  (two-arg via "by") */
static ASTNode *parse_postfix(Parser *p) {
    ASTNode *expr = parse_primary(p);

    while (looks_like_past_tense(p)) {
        Token *verb = advance_parser(p);

        if (check_word(p, "by")) {
            /* "x multiplied by y" → multiply(x, y) */
            advance_parser(p); /* consume "by" */
            ASTNode *right = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = expr;
            args[1] = right;
            expr = ast_function_call(verb->value, args, 2, verb->line, verb->col);
        } else {
            /* "x doubled" → double(x) — single arg only */
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = expr;
            expr = ast_function_call(verb->value, args, 1, verb->line, verb->col);
        }
    }

    return expr;
}

/* Check if the next token (at pos+1) could start an expression */
static int next_is_expr_start(Parser *p) {
    int peek = p->pos + 1;
    if (peek >= p->count) return 0;
    TokenType t = p->tokens[peek].type;
    return t == TOK_NUMBER || t == TOK_STRING || t == TOK_LPAREN ||
           (t == TOK_WORD && strcmp(p->tokens[peek].value, "true") != 0 &&
            strcmp(p->tokens[peek].value, "false") != 0 &&
            strcmp(p->tokens[peek].value, "then") != 0 &&
            strcmp(p->tokens[peek].value, "do") != 0 &&
            strcmp(p->tokens[peek].value, "end") != 0);
}

static ASTNode *parse_unary(Parser *p) {
    /* Prefix verb: "doubling x" / "multiplying x and y" / "doubles x" */
    if (looks_like_prefix_verb(p) && next_is_expr_start(p)) {
        Token *verb = advance_parser(p);
        int arg_cap = 4;
        ASTNode **args = malloc(sizeof(ASTNode *) * arg_cap);
        int arg_count = 0;

        /* Parse first arg */
        args[arg_count++] = parse_comparison(p);

        /* Additional args separated by "and" or "," */
        while (check_word(p, "and") || check_type(p, TOK_COMMA)) {
            int was_comma = check_type(p, TOK_COMMA);
            advance_parser(p);
            if (was_comma && check_word(p, "and")) advance_parser(p);
            if (arg_count >= arg_cap) {
                arg_cap *= 2;
                args = realloc(args, sizeof(ASTNode *) * arg_cap);
            }
            args[arg_count++] = parse_comparison(p);
        }

        return ast_function_call(verb->value, args, arg_count, verb->line, verb->col);
    }
    if (check_word(p, "not")) {
        Token *op = advance_parser(p);
        ASTNode *operand = parse_unary(p);
        return ast_unary("not", operand, op->line, op->col);
    }
    if (check_type(p, TOK_MINUS)) {
        Token *op = advance_parser(p);
        ASTNode *operand = parse_unary(p);
        return ast_unary("-", operand, op->line, op->col);
    }
    return parse_postfix(p);
}

static ASTNode *parse_multiplication(Parser *p) {
    ASTNode *left = parse_unary(p);
    while (check_type(p, TOK_STAR) || check_type(p, TOK_SLASH) || check_type(p, TOK_PERCENT)) {
        Token *op = advance_parser(p);
        ASTNode *right = parse_unary(p);
        left = ast_binary(left, op->value, right, op->line, op->col);
    }
    return left;
}

static ASTNode *parse_addition(Parser *p) {
    ASTNode *left = parse_multiplication(p);
    while (check_type(p, TOK_PLUS) || check_type(p, TOK_MINUS)) {
        Token *op = advance_parser(p);
        ASTNode *right = parse_multiplication(p);
        left = ast_binary(left, op->value, right, op->line, op->col);
    }
    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_addition(p);
    if (check_type(p, TOK_EQ) || check_type(p, TOK_NEQ) ||
        check_type(p, TOK_GT) || check_type(p, TOK_LT) ||
        check_type(p, TOK_GTE) || check_type(p, TOK_LTE)) {
        Token *op = advance_parser(p);
        ASTNode *right = parse_addition(p);
        left = ast_binary(left, op->value, right, op->line, op->col);
    }
    return left;
}

static ASTNode *parse_logic_and(Parser *p) {
    ASTNode *left = parse_comparison(p);
    while (check_word(p, "and")) {
        Token *op = advance_parser(p);
        ASTNode *right = parse_comparison(p);
        left = ast_binary(left, "and", right, op->line, op->col);
    }
    return left;
}

static ASTNode *parse_logic_or(Parser *p) {
    ASTNode *left = parse_logic_and(p);
    while (check_word(p, "or")) {
        Token *op = advance_parser(p);
        ASTNode *right = parse_logic_and(p);
        left = ast_binary(left, "or", right, op->line, op->col);
    }
    return left;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_logic_or(p);
}

/* ---- Block parsing ---- */

static void parse_block(Parser *p, NodeList *list) {
    node_list_init(list);
    skip_separators(p);
    while (!is_block_end(p)) {
        ASTNode *stmt = parse_statement(p);
        node_list_push(list, stmt);
        if (!check_type(p, TOK_EOF) && !is_block_end(p)) {
            if (!is_separator(p)) {
                babel_error_at_token(current(p), "expected end of line or ',' after statement");
            }
            skip_separators(p);
        }
    }
}

/* ---- Statement parsing ---- */

static ASTNode *parse_print_stmt(Parser *p) {
    Token *keyword = advance_parser(p);
    ASTNode *expr = parse_expression(p);
    return ast_print(expr, keyword->line, keyword->col);
}

static ASTNode *parse_assignment(Parser *p) {
    Token *name_tok = advance_parser(p);
    advance_parser(p); /* consume 'is' */
    ASTNode *value = parse_expression(p);
    return ast_assignment(name_tok->value, value, name_tok->line, name_tok->col);
}

static ASTNode *parse_if_stmt(Parser *p) {
    Token *tok = advance_parser(p); /* consume 'if' */
    ASTNode *condition = parse_expression(p);
    expect_word(p, "then", "expected 'then' after condition");
    skip_separators(p);

    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = NODE_IF_STMT;
    node->line = tok->line;
    node->col = tok->col;
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.else_if = NULL;

    parse_block(p, &node->as.if_stmt.body);
    node_list_init(&node->as.if_stmt.else_body);

    if (check_word(p, "else")) {
        advance_parser(p);
        skip_separators(p);

        if (check_word(p, "if")) {
            node->as.if_stmt.else_if = parse_if_stmt(p);
            return node;
        } else {
            parse_block(p, &node->as.if_stmt.else_body);
        }
    }

    expect_block_end(p, "if statement");
    return node;
}

static ASTNode *parse_while_stmt(Parser *p) {
    Token *tok = advance_parser(p); /* consume 'while' */
    ASTNode *condition = parse_expression(p);
    expect_block_start(p); /* 'do' is optional */

    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = NODE_WHILE_STMT;
    node->line = tok->line;
    node->col = tok->col;
    node->as.while_stmt.condition = condition;
    parse_block(p, &node->as.while_stmt.body);

    expect_block_end(p, "while loop");
    return node;
}

static ASTNode *parse_until_stmt(Parser *p) {
    /* "until EXPR ... end" → same as "while not EXPR ... end" */
    Token *tok = advance_parser(p); /* consume 'until' */
    ASTNode *condition = parse_expression(p);
    expect_block_start(p);

    /* Wrap condition in "not" */
    ASTNode *negated = ast_unary("not", condition, tok->line, tok->col);

    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = NODE_WHILE_STMT;
    node->line = tok->line;
    node->col = tok->col;
    node->as.while_stmt.condition = negated;
    parse_block(p, &node->as.while_stmt.body);

    expect_block_end(p, "until loop");
    return node;
}

static char *str_dup_parser(const char *s) {
    char *d = malloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

static ASTNode *parse_for_stmt(Parser *p) {
    Token *tok = advance_parser(p); /* consume 'for' */

    Token *var_tok = expect_type(p, TOK_WORD, "expected variable name after 'for'");

    if (check_word(p, "from")) {
        /* for-range: for VAR from START to END [as ALIAS] [do] ... end */
        advance_parser(p);
        ASTNode *start = parse_expression(p);
        expect_word(p, "to", "expected 'to' in for-range loop");
        ASTNode *end_expr = parse_expression(p);

        /* Check for "as IDENT" — renames the loop variable.
         * This supports "count from 1 to 10 as i" where the normalizer
         * emits "for _counter from 1 to 10 as i do" */
        char *actual_var = str_dup_parser(var_tok->value);
        if (check_word(p, "as")) {
            advance_parser(p);
            Token *alias = expect_type(p, TOK_WORD, "expected variable name after 'as'");
            free(actual_var);
            actual_var = str_dup_parser(alias->value);
        }

        expect_block_start(p);

        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_FOR_RANGE_STMT;
        node->line = tok->line;
        node->col = tok->col;
        node->as.for_range.var_name = actual_var;
        node->as.for_range.start = start;
        node->as.for_range.end = end_expr;
        parse_block(p, &node->as.for_range.body);

        expect_block_end(p, "for loop");
        return node;
    } else if (check_word(p, "in")) {
        /* for-in: for ITEM in COLLECTION [do] ... end */
        advance_parser(p);
        ASTNode *collection = parse_expression(p);
        expect_block_start(p);

        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_FOR_IN_STMT;
        node->line = tok->line;
        node->col = tok->col;
        node->as.for_in.var_name = str_dup_parser(var_tok->value);
        node->as.for_in.collection = collection;
        parse_block(p, &node->as.for_in.body);

        expect_block_end(p, "for loop");
        return node;
    } else {
        babel_error_at_token(current(p), "expected 'from' or 'in' after for variable");
        return NULL;
    }
}

static ASTNode *parse_loop_stmt(Parser *p) {
    Token *tok = advance_parser(p); /* consume 'loop' */
    skip_separators(p);

    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = NODE_LOOP_STMT;
    node->line = tok->line;
    node->col = tok->col;
    parse_block(p, &node->as.loop_stmt.body);

    /* "loop ... end" OR "loop ... until EXPR" */
    if (check_word(p, "until")) {
        advance_parser(p);
        ASTNode *condition = parse_expression(p);
        /* Convert to while loop: while(not condition) { body }
         * But we need do-while semantics (body runs at least once).
         * Generate: body once + while(not condition) { body }
         * Actually, simpler to just use the loop node and let codegen
         * handle "until" — let's add an until_condition to the loop. */
        /* For simplicity, rewrite as: the block already ran, so we
         * wrap this as: while(1) { BODY; if(COND) break; } */
        /* Add a break-if at the end of the body */
        ASTNode *break_node = calloc(1, sizeof(ASTNode));
        break_node->type = NODE_BREAK_STMT;
        break_node->line = tok->line;
        break_node->col = tok->col;

        ASTNode *if_break = calloc(1, sizeof(ASTNode));
        if_break->type = NODE_IF_STMT;
        if_break->line = tok->line;
        if_break->col = tok->col;
        if_break->as.if_stmt.condition = condition;
        if_break->as.if_stmt.else_if = NULL;
        node_list_init(&if_break->as.if_stmt.body);
        node_list_push(&if_break->as.if_stmt.body, break_node);
        node_list_init(&if_break->as.if_stmt.else_body);

        node_list_push(&node->as.loop_stmt.body, if_break);
        return node;
    }

    expect_block_end(p, "loop");
    return node;
}

/* Parse a parameter list: "a and b and c" or "a, b, c" */
/* Check if the next token after separators looks like a parameter name
 * (not a statement keyword). Used to stop param list parsing before
 * commas that are statement separators. */
static int next_looks_like_param(Parser *p) {
    int peek = p->pos + 1;
    /* After comma, might have "and" (Oxford comma) */
    if (peek < p->count && p->tokens[peek].type == TOK_WORD &&
        strcmp(p->tokens[peek].value, "and") == 0) {
        peek++;
    }
    if (peek >= p->count) return 0;
    if (p->tokens[peek].type != TOK_WORD) return 0;
    const char *v = p->tokens[peek].value;
    /* If it's a statement keyword, it's not a param */
    static const char *stmt_kw[] = {
        "display", "print", "show", "say", "if", "when", "whenever",
        "suppose", "assuming", "given", "while", "for", "loop", "return",
        "break", "continue", "skip", "call", "end", "else", "otherwise",
        NULL
    };
    for (int i = 0; stmt_kw[i]; i++) {
        if (strcmp(v, stmt_kw[i]) == 0) return 0;
    }
    return 1;
}

static void parse_param_list(Parser *p, char ***out_params, int *out_count) {
    int cap = 8;
    char **params = malloc(sizeof(char *) * cap);
    int count = 0;

    Token *first = expect_type(p, TOK_WORD, "expected parameter name");
    params[count++] = str_dup_parser(first->value);

    while ((check_word(p, "and") || check_type(p, TOK_COMMA)) &&
           next_looks_like_param(p)) {
        int was_comma = check_type(p, TOK_COMMA);
        advance_parser(p); /* consume 'and' or ',' */
        if (was_comma && check_word(p, "and")) advance_parser(p); /* Oxford comma */
        if (count >= cap) { cap *= 2; params = realloc(params, sizeof(char *) * cap); }
        Token *param = expect_type(p, TOK_WORD, "expected parameter name");
        params[count++] = str_dup_parser(param->value);
    }

    *out_params = params;
    *out_count = count;
}

static ASTNode *parse_function_def(Parser *p) {
    Token *tok = advance_parser(p); /* consume 'function' */
    Token *name = expect_type(p, TOK_WORD, "expected function name");

    char **params = NULL;
    int param_count = 0;

    /* Optional parameter list: "taking a and b" or "taking a, b" */
    if (check_word(p, "taking") || check_word(p, "with")) {
        advance_parser(p);
        parse_param_list(p, &params, &param_count);
    }

    skip_separators(p);

    ASTNode *node = ast_function_def(name->value, params, param_count, tok->line, tok->col);
    parse_block(p, &node->as.func_def.body);

    expect_block_end(p, "function definition");
    return node;
}

static ASTNode *parse_return(Parser *p) {
    Token *tok = advance_parser(p); /* consume 'return' */
    ASTNode *value = NULL;
    /* Return has a value unless followed by newline/EOF/end */
    if (!check_type(p, TOK_NEWLINE) && !check_type(p, TOK_EOF) &&
        !check_word(p, "end")) {
        value = parse_expression(p);
    }
    return ast_return(value, tok->line, tok->col);
}

static ASTNode *parse_statement(Parser *p) {
    Token *tok = current(p);

    /* Print statement */
    if (tok->type == TOK_WORD && is_print_keyword(tok->value)) {
        return parse_print_stmt(p);
    }

    /* Function definition */
    if (check_word(p, "function")) {
        return parse_function_def(p);
    }

    /* Return statement */
    if (check_word(p, "return")) {
        return parse_return(p);
    }

    /* If statement */
    if (check_word(p, "if")) {
        return parse_if_stmt(p);
    }

    /* While loop */
    if (check_word(p, "while")) {
        return parse_while_stmt(p);
    }

    /* Until loop */
    if (check_word(p, "until")) {
        return parse_until_stmt(p);
    }

    /* For loop */
    if (check_word(p, "for")) {
        return parse_for_stmt(p);
    }

    /* Infinite loop */
    if (check_word(p, "loop")) {
        return parse_loop_stmt(p);
    }

    /* Break */
    if (check_word(p, "break")) {
        Token *brk = advance_parser(p);
        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_BREAK_STMT;
        node->line = brk->line;
        node->col = brk->col;
        return node;
    }

    /* Continue / skip */
    if (check_word(p, "continue") || check_word(p, "skip")) {
        Token *cont = advance_parser(p);
        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_CONTINUE_STMT;
        node->line = cont->line;
        node->col = cont->col;
        return node;
    }

    /* Assignment: IDENT is EXPR */
    if (tok->type == TOK_WORD) {
        if (p->pos + 1 < p->count &&
            p->tokens[p->pos + 1].type == TOK_WORD &&
            strcmp(p->tokens[p->pos + 1].value, "is") == 0) {
            return parse_assignment(p);
        }
    }

    /* Expression statement */
    ASTNode *expr = parse_expression(p);
    return expr;
}

ASTNode *parser_parse(Parser *p) {
    ASTNode *program = ast_program();

    skip_separators(p);
    while (!check_type(p, TOK_EOF)) {
        ASTNode *stmt = parse_statement(p);
        node_list_push(&program->as.program.stmts, stmt);

        /* After a statement, expect separator, period (top-level), or EOF */
        if (!check_type(p, TOK_EOF) && !is_separator(p) && !check_type(p, TOK_DOT)) {
            babel_error_at_token(current(p), "expected end of line or ',' after statement");
        }
        /* Consume period at top level (not inside a block) */
        if (check_type(p, TOK_DOT)) advance_parser(p);
        skip_separators(p);
    }

    return program;
}
