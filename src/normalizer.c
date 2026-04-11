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

        /* "remainder of" → % (e.g., "the remainder of x divided by y"
         * but also usable as infix: "x remainder of y") */
        if (is_word(tokens, i, count, "remainder") &&
            is_word(tokens, i+1, count, "of")) {
            tlist_push(&out, syn_op(TOK_PERCENT, "%", line, col));
            i += 2;
            continue;
        }

        /*
         * Single-token arithmetic rewrites
         */

        /* Addition: "plus" / "add" / "adding" → + */
        if (is_word(tokens, i, count, "plus") ||
            is_word(tokens, i, count, "add") ||
            is_word(tokens, i, count, "adding")) {
            tlist_push(&out, syn_op(TOK_PLUS, "+", line, col));
            i++;
            continue;
        }

        /* Subtraction: "minus" / "subtract" / "subtracting" → - */
        if (is_word(tokens, i, count, "minus") ||
            is_word(tokens, i, count, "subtract") ||
            is_word(tokens, i, count, "subtracting")) {
            tlist_push(&out, syn_op(TOK_MINUS, "-", line, col));
            i++;
            continue;
        }

        /* Multiplication: "times" / "multiply" / "multiplying" → * */
        if (is_word(tokens, i, count, "times") ||
            is_word(tokens, i, count, "multiply") ||
            is_word(tokens, i, count, "multiplying")) {
            tlist_push(&out, syn_op(TOK_STAR, "*", line, col));
            i++;
            continue;
        }

        /* Division: "divide" / "dividing" / "over" → / */
        if (is_word(tokens, i, count, "divide") ||
            is_word(tokens, i, count, "dividing") ||
            is_word(tokens, i, count, "over")) {
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

        /* Strip noise word "the" when followed by a word or expression start,
         * but NOT when it could be meaningful (e.g., at start of a statement
         * where it could be part of "the X of Y" patterns — we'll handle
         * those later in Phase 6). For now, strip if between known tokens. */
        if (is_word(tokens, i, count, "the") &&
            i + 1 < count &&
            (is_type(tokens, i+1, count, TOK_WORD) ||
             is_type(tokens, i+1, count, TOK_NUMBER) ||
             is_type(tokens, i+1, count, TOK_STRING))) {
            /* Check context: only strip "the" when it's mid-expression,
             * i.e., there's something before it on this line */
            if (i > 0 && !is_type(tokens, i-1, count, TOK_NEWLINE)) {
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

    *out_count = out.count;
    return out.items;
}
