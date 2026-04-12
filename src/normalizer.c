#include "normalizer.h"
#include "lexer.h"
#include <stdlib.h>
#include <string.h>

/* Dynamic token list for building the output */
typedef struct {
    Token *items;
    int count;
    int capacity;
} TokenList;

static void tlist_init(TokenList *tl) {
    tl->capacity = 256;
    tl->items = malloc(sizeof(Token) * tl->capacity);
    tl->count = 0;
}

static void tlist_push(TokenList *tl, Token tok) {
    if (tl->count >= tl->capacity) {
        tl->capacity *= 2;
        tl->items = realloc(tl->items, sizeof(Token) * tl->capacity);
    }
    tl->items[tl->count++] = tok;
}

/* Create a synthetic token (for rewritten output) */
static Token syn_word(const char *value, int line, int col) {
    Token t;
    t.type = TOK_WORD;
    t.value = malloc(strlen(value) + 1);
    strcpy(t.value, value);
    t.line = line;
    t.col = col;
    return t;
}

static Token syn_op(TokenType type, const char *value, int line, int col) {
    Token t;
    t.type = type;
    t.value = malloc(strlen(value) + 1);
    strcpy(t.value, value);
    t.line = line;
    t.col = col;
    return t;
}

static Token clone_token(Token *src) {
    Token t;
    t.type = src->type;
    t.value = malloc(strlen(src->value) + 1);
    strcpy(t.value, src->value);
    t.line = src->line;
    t.col = src->col;
    return t;
}

/* Helpers to peek at tokens in the stream */
static int is_word(Token *tokens, int pos, int count, const char *word) {
    if (pos >= count) return 0;
    return tokens[pos].type == TOK_WORD && strcmp(tokens[pos].value, word) == 0;
}

static int is_type(Token *tokens, int pos, int count, TokenType type) {
    if (pos >= count) return 0;
    return tokens[pos].type == type;
}

static int is_ident(Token *tokens, int pos, int count) {
    /* An identifier is a WORD that is not a known hard keyword */
    if (pos >= count) return 0;
    if (tokens[pos].type != TOK_WORD) return 0;
    const char *v = tokens[pos].value;
    /* Hard keywords cannot be identifiers */
    static const char *hard_kw[] = {
        "if", "then", "else", "end", "while", "do", "for", "function",
        "return", "true", "false", "nothing", "and", "or", "not",
        "try", "catch", "finally", "from", "to", "in", "taking",
        "break", "continue", "loop", "is",
        NULL
    };
    for (int i = 0; hard_kw[i]; i++) {
        if (strcmp(v, hard_kw[i]) == 0) return 0;
    }
    return 1;
}

/* Check if the last token in the output stream looks like the end of an expression.
 * Used to determine if an ambiguous word like "add" should be an operator vs identifier. */
static int last_output_is_expr_end(TokenList *tl) {
    if (tl->count == 0) return 0;
    Token *last = &tl->items[tl->count - 1];
    switch (last->type) {
        case TOK_NUMBER:
        case TOK_STRING:
        case TOK_RPAREN:
        case TOK_RBRACKET:
            return 1;
        case TOK_WORD:
            /* An identifier or literal value keyword is expression-like */
            if (strcmp(last->value, "true") == 0 ||
                strcmp(last->value, "false") == 0 ||
                strcmp(last->value, "nothing") == 0)
                return 1;
            /* Any other word could be an identifier (expression end) */
            /* But not keywords like "display", "if", "then", etc. */
            {
                static const char *non_expr[] = {
                    "if", "then", "else", "end", "while", "do", "for",
                    "function", "return", "display", "print", "show", "say",
                    "taking", "with", "call", "from", "to", "in", "is",
                    "and", "or", "not", "loop", "break", "continue", "skip",
                    NULL
                };
                for (int k = 0; non_expr[k]; k++) {
                    if (strcmp(last->value, non_expr[k]) == 0) return 0;
                }
            }
            return 1;
        default:
            return 0;
    }
}

Token *normalizer_run(Token *tokens, int count, int *out_count) {
    TokenList out;
    tlist_init(&out);

    int i = 0;
    while (i < count) {
        int line = tokens[i].line;
        int col = tokens[i].col;

        /*
         * Multi-token patterns, checked longest-first.
         */

        /* "is not equal to" → != */
        if (is_word(tokens, i, count, "is") &&
            is_word(tokens, i+1, count, "not") &&
            is_word(tokens, i+2, count, "equal") &&
            is_word(tokens, i+3, count, "to")) {
            tlist_push(&out, syn_op(TOK_NEQ, "!=", line, col));
            i += 4;
            continue;
        }

        /* "is greater than" / "is more than" / "is bigger than" → > */
        if (is_word(tokens, i, count, "is") &&
            (is_word(tokens, i+1, count, "greater") ||
             is_word(tokens, i+1, count, "more") ||
             is_word(tokens, i+1, count, "bigger")) &&
            is_word(tokens, i+2, count, "than")) {
            tlist_push(&out, syn_op(TOK_GT, ">", line, col));
            i += 3;
            continue;
        }

        /* "is less than" / "is smaller than" → < */
        if (is_word(tokens, i, count, "is") &&
            (is_word(tokens, i+1, count, "less") ||
             is_word(tokens, i+1, count, "smaller")) &&
            is_word(tokens, i+2, count, "than")) {
            tlist_push(&out, syn_op(TOK_LT, "<", line, col));
            i += 3;
            continue;
        }

        /* "is equal to" / "equal to" → == */
        if (is_word(tokens, i, count, "is") &&
            is_word(tokens, i+1, count, "equal") &&
            is_word(tokens, i+2, count, "to")) {
            tlist_push(&out, syn_op(TOK_EQ, "==", line, col));
            i += 3;
            continue;
        }
        if (is_word(tokens, i, count, "equal") &&
            is_word(tokens, i+1, count, "to")) {
            tlist_push(&out, syn_op(TOK_EQ, "==", line, col));
            i += 2;
            continue;
        }

        /* "is at least" / "at least" → >= */
        if (is_word(tokens, i, count, "is") &&
            is_word(tokens, i+1, count, "at") &&
            is_word(tokens, i+2, count, "least")) {
            tlist_push(&out, syn_op(TOK_GTE, ">=", line, col));
            i += 3;
            continue;
        }
        if (is_word(tokens, i, count, "at") &&
            is_word(tokens, i+1, count, "least")) {
            tlist_push(&out, syn_op(TOK_GTE, ">=", line, col));
            i += 2;
            continue;
        }

        /* "is at most" / "at most" → <= */
        if (is_word(tokens, i, count, "is") &&
            is_word(tokens, i+1, count, "at") &&
            is_word(tokens, i+2, count, "most")) {
            tlist_push(&out, syn_op(TOK_LTE, "<=", line, col));
            i += 3;
            continue;
        }
        if (is_word(tokens, i, count, "at") &&
            is_word(tokens, i+1, count, "most")) {
            tlist_push(&out, syn_op(TOK_LTE, "<=", line, col));
            i += 2;
            continue;
        }

        /* Bare 2-word comparisons (without "is" prefix):
         * "greater than" / "more than" / "bigger than" → >
         * "less than" / "smaller than" → <
         * These fire after "should x be" strips "be", leaving "bigger than" */
        if ((is_word(tokens, i, count, "greater") ||
             is_word(tokens, i, count, "more") ||
             is_word(tokens, i, count, "bigger")) &&
            is_word(tokens, i+1, count, "than")) {
            tlist_push(&out, syn_op(TOK_GT, ">", line, col));
            i += 2;
            continue;
        }
        if ((is_word(tokens, i, count, "less") ||
             is_word(tokens, i, count, "smaller")) &&
            is_word(tokens, i+1, count, "than")) {
            tlist_push(&out, syn_op(TOK_LT, "<", line, col));
            i += 2;
            continue;
        }

        /* "divided by" → / */
        if (is_word(tokens, i, count, "divided") &&
            is_word(tokens, i+1, count, "by")) {
            tlist_push(&out, syn_op(TOK_SLASH, "/", line, col));
            i += 2;
            continue;
        }

        /* "is now" → "is" (reassignment sugar, drop "now") */
        if (is_word(tokens, i, count, "is") &&
            is_word(tokens, i+1, count, "now")) {
            tlist_push(&out, syn_word("is", line, col));
            i += 2;
            continue;
        }

        /*
         * Assignment sugar: "let IDENT be" → IDENT "is"
         */
        if (is_word(tokens, i, count, "let") &&
            is_ident(tokens, i+1, count) &&
            is_word(tokens, i+2, count, "be")) {
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("is", line, col));
            i += 3;
            continue;
        }

        /* "set the value of IDENT to" → IDENT "is" */
        if (is_word(tokens, i, count, "set") &&
            is_word(tokens, i+1, count, "the") &&
            is_word(tokens, i+2, count, "value") &&
            is_word(tokens, i+3, count, "of") &&
            is_ident(tokens, i+4, count) &&
            is_word(tokens, i+5, count, "to")) {
            tlist_push(&out, clone_token(&tokens[i+4]));
            tlist_push(&out, syn_word("is", line, col));
            i += 6;
            continue;
        }

        /* "set the variable IDENT to" → IDENT "is" */
        if (is_word(tokens, i, count, "set") &&
            is_word(tokens, i+1, count, "the") &&
            is_word(tokens, i+2, count, "variable") &&
            is_ident(tokens, i+3, count) &&
            is_word(tokens, i+4, count, "to")) {
            tlist_push(&out, clone_token(&tokens[i+3]));
            tlist_push(&out, syn_word("is", line, col));
            i += 5;
            continue;
        }

        /* "set IDENT to" → IDENT "is" */
        if (is_word(tokens, i, count, "set") &&
            is_ident(tokens, i+1, count) &&
            is_word(tokens, i+2, count, "to")) {
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("is", line, col));
            i += 3;
            continue;
        }

        /* "define IDENT as" → IDENT "is" */
        if (is_word(tokens, i, count, "define") &&
            is_ident(tokens, i+1, count) &&
            is_word(tokens, i+2, count, "as")) {
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("is", line, col));
            i += 3;
            continue;
        }

        /* "change IDENT to" → IDENT "is" */
        if (is_word(tokens, i, count, "change") &&
            is_ident(tokens, i+1, count) &&
            is_word(tokens, i+2, count, "to")) {
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("is", line, col));
            i += 3;
            continue;
        }

        /* "update IDENT to" → IDENT "is" */
        if (is_word(tokens, i, count, "update") &&
            is_ident(tokens, i+1, count) &&
            is_word(tokens, i+2, count, "to")) {
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("is", line, col));
            i += 3;
            continue;
        }

        /*
         * Multi-token arithmetic rewrites (before single-token)
         */

        /* "multiplied by" → * */
        if (is_word(tokens, i, count, "multiplied") &&
            is_word(tokens, i+1, count, "by")) {
            tlist_push(&out, syn_op(TOK_STAR, "*", line, col));
            i += 2;
            continue;
        }

        /* "divided by" is already handled above */

        /* "added to" → + */
        if (is_word(tokens, i, count, "added") &&
            is_word(tokens, i+1, count, "to")) {
            tlist_push(&out, syn_op(TOK_PLUS, "+", line, col));
            i += 2;
            continue;
        }

        /* "increased by" → + */
        if (is_word(tokens, i, count, "increased") &&
            is_word(tokens, i+1, count, "by")) {
            tlist_push(&out, syn_op(TOK_PLUS, "+", line, col));
            i += 2;
            continue;
        }

        /* "subtracted from" → - */
        if (is_word(tokens, i, count, "subtracted") &&
            is_word(tokens, i+1, count, "from")) {
            tlist_push(&out, syn_op(TOK_MINUS, "-", line, col));
            i += 2;
            continue;
        }

        /* "decreased by" → - */
        if (is_word(tokens, i, count, "decreased") &&
            is_word(tokens, i+1, count, "by")) {
            tlist_push(&out, syn_op(TOK_MINUS, "-", line, col));
            i += 2;
            continue;
        }

        /* "take away" → - (spoken math: "10 take away 3") */
        if (is_word(tokens, i, count, "take") &&
            is_word(tokens, i+1, count, "away")) {
            tlist_push(&out, syn_op(TOK_MINUS, "-", line, col));
            i += 2;
            continue;
        }

        /* "remainder of" — handled by the parser as a named operation
         * ("the remainder of X divided by Y"), so only rewrite when
         * preceded by an expression (infix usage: "x remainder of y") */
        if (is_word(tokens, i, count, "remainder") &&
            is_word(tokens, i+1, count, "of") &&
            last_output_is_expr_end(&out)) {
            tlist_push(&out, syn_op(TOK_PERCENT, "%", line, col));
            i += 2;
            continue;
        }

        /*
         * Single-token arithmetic rewrites
         */

        /* Addition: "plus" → + (always), "add" / "adding" → + (only after expression) */
        if (is_word(tokens, i, count, "plus")) {
            tlist_push(&out, syn_op(TOK_PLUS, "+", line, col));
            i++;
            continue;
        }
        if ((is_word(tokens, i, count, "add") ||
             is_word(tokens, i, count, "adding")) &&
            last_output_is_expr_end(&out)) {
            tlist_push(&out, syn_op(TOK_PLUS, "+", line, col));
            i++;
            continue;
        }

        /* Subtraction: "minus" → - (always), "subtract" / "subtracting" → - (after expr) */
        if (is_word(tokens, i, count, "minus")) {
            tlist_push(&out, syn_op(TOK_MINUS, "-", line, col));
            i++;
            continue;
        }
        if ((is_word(tokens, i, count, "subtract") ||
             is_word(tokens, i, count, "subtracting")) &&
            last_output_is_expr_end(&out)) {
            tlist_push(&out, syn_op(TOK_MINUS, "-", line, col));
            i++;
            continue;
        }

        /* Multiplication: "times" → * (always), "multiply" / "multiplying" → * (after expr) */
        if (is_word(tokens, i, count, "times")) {
            tlist_push(&out, syn_op(TOK_STAR, "*", line, col));
            i++;
            continue;
        }
        if ((is_word(tokens, i, count, "multiply") ||
             is_word(tokens, i, count, "multiplying")) &&
            last_output_is_expr_end(&out)) {
            tlist_push(&out, syn_op(TOK_STAR, "*", line, col));
            i++;
            continue;
        }

        /* Division: "divide" / "dividing" / "over" → / (after expr) */
        if ((is_word(tokens, i, count, "divide") ||
             is_word(tokens, i, count, "dividing") ||
             is_word(tokens, i, count, "over")) &&
            last_output_is_expr_end(&out)) {
            tlist_push(&out, syn_op(TOK_SLASH, "/", line, col));
            i++;
            continue;
        }

        /* Modulo: "modulo" / "mod" / "remainder" → % */
        if (is_word(tokens, i, count, "modulo") ||
            is_word(tokens, i, count, "mod") ||
            is_word(tokens, i, count, "remainder")) {
            tlist_push(&out, syn_op(TOK_PERCENT, "%", line, col));
            i++;
            continue;
        }

        /* Comparison: "exceeds" → > */
        if (is_word(tokens, i, count, "exceeds")) {
            tlist_push(&out, syn_op(TOK_GT, ">", line, col));
            i++;
            continue;
        }

        /* "equals" → == */
        if (is_word(tokens, i, count, "equals")) {
            tlist_push(&out, syn_op(TOK_EQ, "==", line, col));
            i++;
            continue;
        }

        /* Possessive form: "X's Y" → "Y of X"
         * The lexer emits X, __poss, Y tokens. We rewrite to Y, of, X. */
        if (is_word(tokens, i, count, "__poss")) {
            /* We need to reorder: the previous output token (X) should come
             * after Y. Pull the previous token from the output. */
            if (out.count > 0 && i + 1 < count &&
                is_type(tokens, i+1, count, TOK_WORD)) {
                Token subject = out.items[out.count - 1];
                out.count--; /* pop X from output */
                /* Emit: Y of X */
                tlist_push(&out, clone_token(&tokens[i+1]));
                tlist_push(&out, syn_word("of", line, col));
                tlist_push(&out, subject); /* reuse the popped token */
                i += 2; /* skip __poss and Y */
                continue;
            }
            /* Fallback: just drop the marker */
            i++;
            continue;
        }

        /*
         * ===== ERROR HANDLING SUGAR =====
         */

        /* "try to" / "attempt to" → "try" (strip "to") */
        if ((is_word(tokens, i, count, "try") || is_word(tokens, i, count, "attempt")) &&
            is_word(tokens, i+1, count, "to") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("try", line, col));
            i += 2;
            continue;
        }

        /* "attempt" → "try" (at line start) */
        if (is_word(tokens, i, count, "attempt") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("try", line, col));
            i++;
            continue;
        }

        /* "but if it fails" / "but if it errors" → "catch error"
         * Also "if it fails", "if it errors", "if that fails" */
        if (is_word(tokens, i, count, "but") &&
            is_word(tokens, i+1, count, "if") &&
            (is_word(tokens, i+2, count, "it") || is_word(tokens, i+2, count, "that")) &&
            (is_word(tokens, i+3, count, "fails") ||
             is_word(tokens, i+3, count, "errors"))) {
            tlist_push(&out, syn_word("catch", line, col));
            tlist_push(&out, syn_word("error", line, col));
            i += 4;
            /* Skip optional "then" */
            if (is_word(tokens, i, count, "then")) i++;
            continue;
        }
        if (is_word(tokens, i, count, "if") &&
            (is_word(tokens, i+1, count, "it") || is_word(tokens, i+1, count, "that")) &&
            (is_word(tokens, i+2, count, "fails") ||
             is_word(tokens, i+2, count, "errors"))) {
            tlist_push(&out, syn_word("catch", line, col));
            tlist_push(&out, syn_word("error", line, col));
            i += 3;
            if (is_word(tokens, i, count, "then")) i++;
            continue;
        }

        /* "catch the error" → "catch error" (strip "the") */
        if (is_word(tokens, i, count, "catch") &&
            is_word(tokens, i+1, count, "the") &&
            is_word(tokens, i+2, count, "error")) {
            tlist_push(&out, syn_word("catch", line, col));
            tlist_push(&out, syn_word("error", line, col));
            i += 3;
            /* Skip optional "then" */
            if (is_word(tokens, i, count, "then")) i++;
            continue;
        }

        /* "catch X then" → "catch X" (strip trailing "then") */
        if (is_word(tokens, i, count, "catch")) {
            tlist_push(&out, syn_word("catch", line, col));
            i++;
            /* Optional identifier + optional "then" */
            if (i < count && tokens[i].type == TOK_WORD &&
                !is_word(tokens, i, count, "then") &&
                !is_word(tokens, i, count, "end")) {
                tlist_push(&out, clone_token(&tokens[i]));
                i++;
            }
            if (is_word(tokens, i, count, "then")) i++;
            continue;
        }

        /* "if something goes wrong" / "if an error occurs" / "on error" → "catch" */
        if (is_word(tokens, i, count, "if") &&
            is_word(tokens, i+1, count, "something") &&
            is_word(tokens, i+2, count, "goes") &&
            is_word(tokens, i+3, count, "wrong")) {
            tlist_push(&out, syn_word("catch", line, col));
            i += 4;
            continue;
        }
        if (is_word(tokens, i, count, "if") &&
            (is_word(tokens, i+1, count, "an") || is_word(tokens, i+1, count, "a")) &&
            is_word(tokens, i+2, count, "error") &&
            is_word(tokens, i+3, count, "occurs")) {
            tlist_push(&out, syn_word("catch", line, col));
            i += 4;
            continue;
        }
        if (is_word(tokens, i, count, "on") &&
            is_word(tokens, i+1, count, "error")) {
            tlist_push(&out, syn_word("catch", line, col));
            i += 2;
            continue;
        }
        /* "handle error" / "handle the error" → "catch error" */
        if (is_word(tokens, i, count, "handle") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("catch", line, col));
            i++;
            continue;
        }

        /* "throw" → "raise" (already handled in parser but also normalize) */

        /* "fail with" → "raise" */
        if (is_word(tokens, i, count, "fail") &&
            is_word(tokens, i+1, count, "with")) {
            tlist_push(&out, syn_word("raise", line, col));
            i += 2;
            continue;
        }

        /* "report error" → "raise" */
        if (is_word(tokens, i, count, "report") &&
            is_word(tokens, i+1, count, "error")) {
            tlist_push(&out, syn_word("raise", line, col));
            i += 2;
            continue;
        }

        /*
         * ===== FUNCTION DEFINITION SUGAR =====
         * All map to: function NAME [taking PARAMS] ... end
         */

        /* "to NAME with PARAMS" → "function NAME taking PARAMS" */
        if (is_word(tokens, i, count, "to") &&
            is_ident(tokens, i+1, count) &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("function", line, col));
            tlist_push(&out, clone_token(&tokens[i+1]));
            i += 2;
            /* "with" → "taking" */
            if (is_word(tokens, i, count, "with")) {
                tlist_push(&out, syn_word("taking", line, col));
                i++;
            }
            continue;
        }

        /* "define function" / "create function" / "define method" / "create method" → "function" */
        if ((is_word(tokens, i, count, "define") ||
             is_word(tokens, i, count, "create")) &&
            (is_word(tokens, i+1, count, "function") ||
             is_word(tokens, i+1, count, "method"))) {
            tlist_push(&out, syn_word("function", line, col));
            i += 2;
            continue;
        }

        /* bare "method" at line start → "function" */
        if (is_word(tokens, i, count, "method") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("function", line, col));
            i++;
            continue;
        }

        /* "define NAME taking/with" → "function NAME taking" (without "function" keyword) */
        if (is_word(tokens, i, count, "define") &&
            is_ident(tokens, i+1, count) &&
            (is_word(tokens, i+2, count, "taking") || is_word(tokens, i+2, count, "with") ||
             is_word(tokens, i+2, count, "as")) &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("function", line, col));
            /* leave the name and rest for next iteration */
            i++;
            continue;
        }

        /* "that takes" → "taking" */
        if (is_word(tokens, i, count, "that") &&
            is_word(tokens, i+1, count, "takes")) {
            tlist_push(&out, syn_word("taking", line, col));
            i += 2;
            continue;
        }

        /* "which takes" → "taking" */
        if (is_word(tokens, i, count, "which") &&
            is_word(tokens, i+1, count, "takes")) {
            tlist_push(&out, syn_word("taking", line, col));
            i += 2;
            continue;
        }

        /* "called" → noise word before function name (strip it) */
        if (is_word(tokens, i, count, "called") &&
            is_ident(tokens, i+1, count)) {
            i++;
            continue;
        }

        /* "give back" / "send back" → "return" */
        if ((is_word(tokens, i, count, "give") || is_word(tokens, i, count, "send")) &&
            is_word(tokens, i+1, count, "back")) {
            tlist_push(&out, syn_word("return", line, col));
            i += 2;
            continue;
        }

        /* "respond with" / "result is" as return sugar */
        if (is_word(tokens, i, count, "respond") &&
            is_word(tokens, i+1, count, "with")) {
            tlist_push(&out, syn_word("return", line, col));
            i += 2;
            continue;
        }

        /*
         * ===== CONDITIONAL SUGAR =====
         * All these map to: if EXPR then ... else ... end
         */

        /* "in the event that" → "if" (4 words) */
        if (is_word(tokens, i, count, "in") &&
            is_word(tokens, i+1, count, "the") &&
            is_word(tokens, i+2, count, "event") &&
            is_word(tokens, i+3, count, "that")) {
            tlist_push(&out, syn_word("if", line, col));
            i += 4;
            continue;
        }

        /* "given that" → "if" */
        if (is_word(tokens, i, count, "given") &&
            is_word(tokens, i+1, count, "that")) {
            tlist_push(&out, syn_word("if", line, col));
            i += 2;
            continue;
        }

        /* "in case" → "if" */
        if (is_word(tokens, i, count, "in") &&
            is_word(tokens, i+1, count, "case")) {
            tlist_push(&out, syn_word("if", line, col));
            i += 2;
            continue;
        }

        /* "should EXPR be" — tricky, "should" → "if" and we leave the rest.
         * The user writes: "should x be greater than 5 then"
         * We transform to: "if x is greater than 5 then"
         * So "should" → "if", and we need "be" to become comparison context.
         * Actually simplest: "should" → "if" at statement start */
        if (is_word(tokens, i, count, "should") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("if", line, col));
            i++;
            continue;
        }

        /* "suppose" / "assuming" / "when" / "whenever" / "given" → "if" */
        if ((is_word(tokens, i, count, "suppose") ||
             is_word(tokens, i, count, "assuming") ||
             is_word(tokens, i, count, "when") ||
             is_word(tokens, i, count, "whenever") ||
             is_word(tokens, i, count, "given")) &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("if", line, col));
            i++;
            continue;
        }

        /* "check if" → "if" (drop "check") */
        if (is_word(tokens, i, count, "check") &&
            is_word(tokens, i+1, count, "if")) {
            i++;
            continue;
        }

        /*
         * ===== ELSE BRANCH SUGAR =====
         */

        /* "but not when" / "but not if" / "and not when" / "and not if"
         * → "else if not" (negate the following condition) */
        if ((is_word(tokens, i, count, "but") || is_word(tokens, i, count, "and")) &&
            is_word(tokens, i+1, count, "not") &&
            (is_word(tokens, i+2, count, "when") || is_word(tokens, i+2, count, "if"))) {
            tlist_push(&out, syn_word("else", line, col));
            tlist_push(&out, syn_word("if", line, col));
            tlist_push(&out, syn_word("not", line, col));
            i += 3;
            continue;
        }

        /* "or else" → "else" */
        if (is_word(tokens, i, count, "or") &&
            is_word(tokens, i+1, count, "else")) {
            tlist_push(&out, syn_word("else", line, col));
            i += 2;
            continue;
        }

        /* "if not then" → "else" (+ skip "then" since else doesn't need it) */
        if (is_word(tokens, i, count, "if") &&
            is_word(tokens, i+1, count, "not") &&
            is_word(tokens, i+2, count, "then")) {
            tlist_push(&out, syn_word("else", line, col));
            i += 3;
            continue;
        }

        /* "if not" (without "then") → "else" */
        if (is_word(tokens, i, count, "if") &&
            is_word(tokens, i+1, count, "not") &&
            !is_word(tokens, i+2, count, "equal")) {
            /* Make sure we don't eat "if not equal to" */
            tlist_push(&out, syn_word("else", line, col));
            i += 2;
            continue;
        }

        /* "but if" / "except when" / "except if" → "else if" */
        if (is_word(tokens, i, count, "but") &&
            is_word(tokens, i+1, count, "if")) {
            tlist_push(&out, syn_word("else", line, col));
            tlist_push(&out, syn_word("if", line, col));
            i += 2;
            continue;
        }
        if (is_word(tokens, i, count, "except") &&
            (is_word(tokens, i+1, count, "when") || is_word(tokens, i+1, count, "if"))) {
            tlist_push(&out, syn_word("else", line, col));
            tlist_push(&out, syn_word("if", line, col));
            i += 2;
            continue;
        }

        /* "otherwise" → "else" */
        if (is_word(tokens, i, count, "otherwise")) {
            tlist_push(&out, syn_word("else", line, col));
            i++;
            continue;
        }

        /*
         * ===== LOOP SUGAR =====
         */

        /* "as long as" → "while" */
        if (is_word(tokens, i, count, "as") &&
            is_word(tokens, i+1, count, "long") &&
            is_word(tokens, i+2, count, "as")) {
            tlist_push(&out, syn_word("while", line, col));
            i += 3;
            continue;
        }

        /* "keep doing" → "loop" (infinite loop, use with "until" later) */
        if (is_word(tokens, i, count, "keep") &&
            is_word(tokens, i+1, count, "doing")) {
            tlist_push(&out, syn_word("loop", line, col));
            i += 2;
            continue;
        }

        /* "repeat while" → "while" */
        if (is_word(tokens, i, count, "repeat") &&
            is_word(tokens, i+1, count, "while")) {
            tlist_push(&out, syn_word("while", line, col));
            i += 2;
            continue;
        }

        /* "repeat until" → "until" (special loop form handled by parser) */
        if (is_word(tokens, i, count, "repeat") &&
            is_word(tokens, i+1, count, "until")) {
            tlist_push(&out, syn_word("until", line, col));
            i += 2;
            continue;
        }

        /* "repeat N times" → "for _i from 1 to N do" */
        if (is_word(tokens, i, count, "repeat") &&
            is_type(tokens, i+1, count, TOK_NUMBER) &&
            is_word(tokens, i+2, count, "times")) {
            tlist_push(&out, syn_word("for", line, col));
            tlist_push(&out, syn_word("_iter", line, col));
            tlist_push(&out, syn_word("from", line, col));
            tlist_push(&out, syn_op(TOK_NUMBER, "1", line, col));
            tlist_push(&out, syn_word("to", line, col));
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("do", line, col));
            i += 3;
            continue;
        }

        /* "look at each" / "look through" / "loop through" / "iterate through"
         * / "go through" / "go through each" / "cycle through" → "for" + mark for-in */

        /* "go through each" / "look at each" (3 words → "for")
         * These are for-in style: "go through each item in list" */
        if ((is_word(tokens, i, count, "go") ||
             is_word(tokens, i, count, "look")) &&
            (is_word(tokens, i+1, count, "through") || is_word(tokens, i+1, count, "at")) &&
            is_word(tokens, i+2, count, "each")) {
            tlist_push(&out, syn_word("for", line, col));
            i += 3;
            continue;
        }

        /* "go through" / "loop through" / "iterate through" / "cycle through"
         * / "look through" / "map through" (2 words)
         * → "for _iter from" (insert placeholder var + from keyword) */
        if ((is_word(tokens, i, count, "go") ||
             is_word(tokens, i, count, "loop") ||
             is_word(tokens, i, count, "iterate") ||
             is_word(tokens, i, count, "cycle") ||
             is_word(tokens, i, count, "look") ||
             is_word(tokens, i, count, "map")) &&
            is_word(tokens, i+1, count, "through")) {
            tlist_push(&out, syn_word("for", line, col));
            tlist_push(&out, syn_word("_iter", line, col));
            tlist_push(&out, syn_word("from", line, col));
            i += 2;
            continue;
        }

        /* "iterating" / "looping" / "mapping" (single word at line start)
         * → "for _iter from" */
        if ((is_word(tokens, i, count, "iterating") ||
             is_word(tokens, i, count, "looping") ||
             is_word(tokens, i, count, "mapping")) &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("for", line, col));
            tlist_push(&out, syn_word("_iter", line, col));
            tlist_push(&out, syn_word("from", line, col));
            i++;
            continue;
        }

        /* "count from" → "for _i from" (the "count" itself becomes the loop keyword) */
        if (is_word(tokens, i, count, "count") &&
            is_word(tokens, i+1, count, "from")) {
            tlist_push(&out, syn_word("for", line, col));
            tlist_push(&out, syn_word("_counter", line, col));
            tlist_push(&out, syn_word("from", line, col));
            i += 2;
            continue;
        }

        /* "for each" → "for" */
        if (is_word(tokens, i, count, "for") &&
            is_word(tokens, i+1, count, "each")) {
            tlist_push(&out, clone_token(&tokens[i]));
            i += 2;
            continue;
        }

        /* "as" in loop context → "do"
         * This handles "count from 1 to 10 as i" and "go through 1 to 10 as i"
         * where "as IDENT" gives the loop variable name.
         * We rewrite: ... as IDENT (newline) → ... do (and prepend IDENT before from)
         * Actually this is tricky — "as" maps to a variable binding.
         * Simpler approach: the parser handles "as" as an alternative to "do"
         * after for-range, where "as IDENT" reorders to set the loop var. */

        /* "until" as block closer for loops: handled by parser directly */

        /* "stop" → "break" */
        if (is_word(tokens, i, count, "stop") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("break", line, col));
            i++;
            continue;
        }

        /* "skip this step" → "continue" (drop "this" and "step") */
        if (is_word(tokens, i, count, "skip") &&
            is_word(tokens, i+1, count, "this") &&
            is_word(tokens, i+2, count, "step")) {
            tlist_push(&out, syn_word("continue", line, col));
            i += 3;
            continue;
        }

        /* "next" → "continue" (at line start) */
        if (is_word(tokens, i, count, "next") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            tlist_push(&out, syn_word("continue", line, col));
            i++;
            continue;
        }

        /* Strip "be" as a noise word mid-expression.
         * Handles: "should x be greater than 5" → "if x greater than 5"
         * Only strip when preceded by an identifier (not at line start). */
        if (is_word(tokens, i, count, "be") &&
            i > 0 && !is_type(tokens, i-1, count, TOK_NEWLINE)) {
            i++;
            continue;
        }

        /* "whether or not" → strip all three (noise before boolean) */
        if (is_word(tokens, i, count, "whether") &&
            is_word(tokens, i+1, count, "or") &&
            is_word(tokens, i+2, count, "not")) {
            i += 3;
            continue;
        }
        /* "whether" → strip (noise before boolean) */
        if (is_word(tokens, i, count, "whether")) {
            i++;
            continue;
        }

        /* "does X contain Y" → "X contains Y" (question form) */
        if (is_word(tokens, i, count, "does") &&
            is_ident(tokens, i+1, count) &&
            is_word(tokens, i+2, count, "contain")) {
            tlist_push(&out, clone_token(&tokens[i+1]));
            tlist_push(&out, syn_word("contains", tokens[i].line, tokens[i].col));
            i += 3;
            continue;
        }

        /* "a list of" → marks start of inline list.
         * We emit a '[' and then later match the end. */
        if (is_word(tokens, i, count, "a") &&
            is_word(tokens, i+1, count, "list") &&
            is_word(tokens, i+2, count, "of")) {
            tlist_push(&out, syn_op(TOK_LBRACKET, "[", line, col));
            i += 3;
            /* Scan forward to find end of list items (next newline, period, or 'then') */
            /* Items are separated by commas/and — we just emit them and add ] before the line end */
            while (i < count &&
                   !is_type(tokens, i, count, TOK_NEWLINE) &&
                   !is_type(tokens, i, count, TOK_EOF) &&
                   !(tokens[i].type == TOK_DOT)) {
                /* "and" between items → comma (skip if after existing comma) */
                if (is_word(tokens, i, count, "and")) {
                    /* Only emit comma if last output isn't already a comma */
                    if (out.count > 0 && out.items[out.count - 1].type != TOK_COMMA) {
                        tlist_push(&out, syn_op(TOK_COMMA, ",", line, col));
                    }
                    i++;
                    continue;
                }
                tlist_push(&out, clone_token(&tokens[i]));
                i++;
            }
            tlist_push(&out, syn_op(TOK_RBRACKET, "]", line, col));
            /* Don't advance past the newline/period — let the outer loop handle it */
            continue;
        }

        /* "every" before a noun → "for" (iteration keyword) */
        if (is_word(tokens, i, count, "every") &&
            (i == 0 || is_type(tokens, i-1, count, TOK_NEWLINE))) {
            /* "every item in X" or "every X in Y" */
            tlist_push(&out, syn_word("for", line, col));
            i++;
            continue;
        }

        /* Strip noise word "the" — now also strip at line start when followed
         * by known builtin prefix words */
        if (is_word(tokens, i, count, "the") && i + 1 < count) {
            int next_is_word = is_type(tokens, i+1, count, TOK_WORD);
            if (next_is_word) {
                const char *nv = tokens[i+1].value;
                /* Always strip "the" before these (even at line start) */
                if (strcmp(nv, "length") == 0 || strcmp(nv, "size") == 0 ||
                    strcmp(nv, "number") == 0 || strcmp(nv, "first") == 0 ||
                    strcmp(nv, "last") == 0 || strcmp(nv, "sum") == 0 ||
                    strcmp(nv, "product") == 0 || strcmp(nv, "difference") == 0 ||
                    strcmp(nv, "quotient") == 0 || strcmp(nv, "remainder") == 0 ||
                    strcmp(nv, "result") == 0) {
                    i++;
                    continue;
                }
            }
            /* Strip mid-expression "the" */
            if (i > 0 && !is_type(tokens, i-1, count, TOK_NEWLINE) &&
                (next_is_word || is_type(tokens, i+1, count, TOK_NUMBER) ||
                 is_type(tokens, i+1, count, TOK_STRING))) {
                i++;
                continue;
            }
        }

        /* Default: pass through unchanged */
        tlist_push(&out, clone_token(&tokens[i]));
        i++;
    }

    /* Free original tokens */
    for (int j = 0; j < count; j++) {
        token_free(&tokens[j]);
    }
    free(tokens);

    /*
     * Post-processing pass: detect post-assignment patterns.
     * Scan for lines ending with:
     *   ... (and|then) (assign|store|save|put) (to|in) IDENT NEWLINE
     * Rearrange to: IDENT is ... NEWLINE
     */
    TokenList final;
    tlist_init(&final);

    int li = 0; /* line start index in out */
    for (int k = 0; k <= out.count; k++) {
        int is_end = (k == out.count) ||
                     (out.items[k].type == TOK_NEWLINE) ||
                     (out.items[k].type == TOK_EOF);
        if (!is_end) continue;

        /* We have a line from li to k-1 (inclusive).
         * Check if it ends with: (and|then) (assign|store|save|put) (to|in) IDENT */
        int line_len = k - li;
        if (line_len >= 4) {
            int e = k - 1; /* last token index */
            Token *t_ident = &out.items[e];
            Token *t_prep  = (e >= li + 1) ? &out.items[e - 1] : NULL;
            Token *t_verb  = (e >= li + 2) ? &out.items[e - 2] : NULL;
            Token *t_conj  = (e >= li + 3) ? &out.items[e - 3] : NULL;

            int matched = 0;
            if (t_ident && t_prep && t_verb && t_conj &&
                t_ident->type == TOK_WORD &&
                t_prep->type == TOK_WORD &&
                t_verb->type == TOK_WORD &&
                t_conj->type == TOK_WORD &&
                (strcmp(t_conj->value, "and") == 0 ||
                 strcmp(t_conj->value, "then") == 0) &&
                (strcmp(t_verb->value, "assign") == 0 ||
                 strcmp(t_verb->value, "store") == 0 ||
                 strcmp(t_verb->value, "save") == 0 ||
                 strcmp(t_verb->value, "put") == 0) &&
                (strcmp(t_prep->value, "to") == 0 ||
                 strcmp(t_prep->value, "in") == 0 ||
                 strcmp(t_prep->value, "as") == 0)) {
                matched = 1;
            }

            if (matched) {
                /* Rearrange: IDENT is [tokens from li to e-4] NEWLINE */
                int expr_end = e - 3; /* exclusive: tokens li..expr_end-1 are the expression */
                tlist_push(&final, clone_token(t_ident));
                tlist_push(&final, syn_word("is", t_ident->line, t_ident->col));
                for (int m = li; m < expr_end; m++) {
                    /* Strip leading "the" since it's now mid-expression */
                    if (m == li && out.items[m].type == TOK_WORD &&
                        strcmp(out.items[m].value, "the") == 0) continue;
                    tlist_push(&final, clone_token(&out.items[m]));
                }
                if (k < out.count) {
                    tlist_push(&final, clone_token(&out.items[k])); /* NEWLINE/EOF */
                }
                li = k + 1;
                continue;
            }
        }

        /* No post-assignment match — emit line as-is */
        for (int m = li; m <= k && m < out.count; m++) {
            tlist_push(&final, clone_token(&out.items[m]));
        }
        li = k + 1;
    }

    /* Free intermediate output */
    for (int j = 0; j < out.count; j++) {
        token_free(&out.items[j]);
    }
    free(out.items);

    *out_count = final.count;
    return final.items;
}
