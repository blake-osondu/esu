#include "parser.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void add_method_name(Parser *p, const char *name) {
    /* Avoid duplicates */
    for (int i = 0; i < p->method_name_count; i++) {
        if (strcmp(p->method_names[i], name) == 0) return;
    }
    if (p->method_name_count >= p->method_name_capacity) {
        p->method_name_capacity = p->method_name_capacity == 0 ? 8 : p->method_name_capacity * 2;
        p->method_names = realloc(p->method_names,
                                  sizeof(char *) * p->method_name_capacity);
    }
    p->method_names[p->method_name_count] = malloc(strlen(name) + 1);
    strcpy(p->method_names[p->method_name_count], name);
    p->method_name_count++;
}

static int is_known_method(Parser *p, const char *name) {
    for (int i = 0; i < p->method_name_count; i++) {
        if (strcmp(p->method_names[i], name) == 0) return 1;
    }
    return 0;
}

/* Pre-scan token stream to collect all method names defined in the program.
 * We look for the pattern: "function METHOD a/an TYPE" (after normalizer
 * converts "to" → "function"). */
static void prescan_methods(Parser *p) {
    for (int i = 0; i + 3 < p->count; i++) {
        if (p->tokens[i].type == TOK_WORD &&
            strcmp(p->tokens[i].value, "function") == 0 &&
            p->tokens[i+1].type == TOK_WORD &&
            p->tokens[i+2].type == TOK_WORD &&
            (strcmp(p->tokens[i+2].value, "a") == 0 ||
             strcmp(p->tokens[i+2].value, "an") == 0) &&
            p->tokens[i+3].type == TOK_WORD) {
            add_method_name(p, p->tokens[i+1].value);
        }
    }
}

void parser_init(Parser *p, Token *tokens, int count) {
    p->tokens = tokens;
    p->count = count;
    p->pos = 0;
    p->method_names = NULL;
    p->method_name_count = 0;
    p->method_name_capacity = 0;
    prescan_methods(p);
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
        esu_error_at_token(current(p), "%s", msg);
    }
    return advance_parser(p);
}

static void expect_word(Parser *p, const char *word, const char *msg) {
    if (!check_word(p, word)) {
        esu_error_at_token(current(p), "%s", msg);
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
    if (check_word(p, "catch")) return 1;
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
        esu_error_at_token(current(p), "%s", msg);
    }
}

/* Forward declarations */
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_comparison(Parser *p);
static ASTNode *parse_primary(Parser *p);
static ASTNode *parse_postfix(Parser *p);
static ASTNode *parse_unary(Parser *p);
static ASTNode *parse_file_path(Parser *p);
static int looks_like_past_tense(Parser *p);
static char *str_dup_parser(const char *s);
static void parse_param_list(Parser *p, char ***out_params, int *out_count);
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


/* Create a builtin function call node. Builtin names start with "__" to
 * distinguish from user functions in codegen. */
static ASTNode *make_builtin_call(const char *name, ASTNode **args, int argc, int line, int col) {
    return ast_function_call(name, args, argc, line, col);
}

static ASTNode *parse_primary(Parser *p) {
    Token *tok = current(p);

    /* ===== BUILTIN PREFIX EXPRESSIONS ===== */

    /* --- Higher-order list operations --- */
    /* "map FN over LIST" → __map_list(LIST, FN) */
    if (check_word(p, "map") && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p); /* consume "map" */
        ASTNode *fn = parse_comparison(p);
        if (check_word(p, "over") || check_word(p, "across")) {
            advance_parser(p);
            ASTNode *list = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = list; args[1] = fn;
            return make_builtin_call("__map_list", args, 2, t->line, t->col);
        }
        /* Not a map expression — restore */
        ast_free(fn);
        p->pos = saved;
    }

    /* "transform every item in LIST using FN"
     * "transform every item in LIST with FN" */
    if (check_word(p, "transform") && p->pos + 3 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "every") == 0) {
        Token *t = advance_parser(p); /* transform */
        advance_parser(p); /* every */
        if (check_type(p, TOK_WORD)) advance_parser(p); /* noun e.g. "item" */
        if (check_word(p, "in") || check_word(p, "of")) advance_parser(p);
        ASTNode *list = parse_comparison(p);
        if (check_word(p, "using") || check_word(p, "with") || check_word(p, "by")) {
            advance_parser(p);
        }
        ASTNode *fn = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = list; args[1] = fn;
        return make_builtin_call("__map_list", args, 2, t->line, t->col);
    }

    /* Filter LIST — supports many forms:
     *   filter LIST with FN              (predicate-first)
     *   filter LIST by FN
     *   filter LIST using FN
     *   filter LIST where every ITEM FN  ("every item is_positive")
     *   filter LIST where each ITEM FN
     *   filter LIST where ITEM FN        ("where x is_positive")
     * The noun after "where every/each" is a noise word; the actual predicate
     * is the function that follows. */
    if (check_word(p, "filter") && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p);
        ASTNode *list = parse_comparison(p);
        if (check_word(p, "with") || check_word(p, "by") || check_word(p, "using")) {
            advance_parser(p);
            ASTNode *fn = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = list; args[1] = fn;
            return make_builtin_call("__filter_list", args, 2, t->line, t->col);
        }
        if (check_word(p, "where")) {
            advance_parser(p); /* consume "where" */
            int had_every = 0;
            /* Optional "every" / "each" */
            if (check_word(p, "every") || check_word(p, "each")) {
                advance_parser(p);
                had_every = 1;
            }
            /* If we saw "every/each", the next word is the noun placeholder
             * (e.g., "item") — skip it. Otherwise, leave the word for parse. */
            if (had_every && check_type(p, TOK_WORD)) {
                advance_parser(p); /* skip noun like "item", "number", etc. */
            }
            /* Optional "is" / "matches" connector */
            if (check_word(p, "is") || check_word(p, "matches")) advance_parser(p);
            ASTNode *fn = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = list; args[1] = fn;
            return make_builtin_call("__filter_list", args, 2, t->line, t->col);
        }
        ast_free(list);
        p->pos = saved;
    }
    /* "keep [only] items [from/in/of] LIST where [every/each ITEM [is]] FN"
     * Examples:
     *   keep items from numbers where is-even
     *   keep only items in numbers where every item is-positive
     *   keep items in numbers where each number matches is-big */
    if (check_word(p, "keep") && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p);
        /* optional "only" */
        if (check_word(p, "only")) advance_parser(p);
        /* optional "items" or similar noun */
        if (check_type(p, TOK_WORD) &&
            (strcmp(current(p)->value, "items") == 0 ||
             strcmp(current(p)->value, "elements") == 0 ||
             strcmp(current(p)->value, "values") == 0)) {
            advance_parser(p);
        }
        if (check_word(p, "from") || check_word(p, "in") || check_word(p, "of")) {
            advance_parser(p);
            ASTNode *list = parse_comparison(p);
            if (check_word(p, "where") || check_word(p, "matching") ||
                check_word(p, "that")) {
                advance_parser(p);
                /* optional "match/matches" after "that" */
                if (check_word(p, "match") || check_word(p, "matches")) advance_parser(p);
                int had_every = 0;
                if (check_word(p, "every") || check_word(p, "each")) {
                    advance_parser(p);
                    had_every = 1;
                }
                /* Skip noun placeholder if "every/each" was used */
                if (had_every && check_type(p, TOK_WORD)) advance_parser(p);
                if (check_word(p, "is") || check_word(p, "matches")) advance_parser(p);
                ASTNode *fn = parse_postfix(p);
                ASTNode **args = malloc(sizeof(ASTNode *) * 2);
                args[0] = list; args[1] = fn;
                return make_builtin_call("__filter_list", args, 2, t->line, t->col);
            }
            ast_free(list);
        }
        p->pos = saved;
    }

    /* "reduce LIST using FN starting at INIT"
     * "fold LIST using FN starting with INIT"
     * "reduce LIST with FN starting at INIT" */
    if ((check_word(p, "reduce") || check_word(p, "fold")) && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p);
        ASTNode *list = parse_comparison(p);
        if (check_word(p, "using") || check_word(p, "with") || check_word(p, "by")) {
            advance_parser(p);
            ASTNode *fn = parse_comparison(p);
            ASTNode *init = NULL;
            if (check_word(p, "starting")) {
                advance_parser(p);
                if (check_word(p, "at") || check_word(p, "with") ||
                    check_word(p, "from")) advance_parser(p);
                init = parse_postfix(p);
            } else if (check_word(p, "from")) {
                advance_parser(p);
                init = parse_postfix(p);
            }
            if (!init) init = ast_number_lit(0, t->line, t->col);
            ASTNode **args = malloc(sizeof(ASTNode *) * 3);
            args[0] = list; args[1] = fn; args[2] = init;
            return make_builtin_call("__reduce_list", args, 3, t->line, t->col);
        }
        ast_free(list);
        p->pos = saved;
    }

    /* --- Length --- */
    /* "length of X" / "size of X" → __length(X) */
    if (check_two_words(p, "length", "of") || check_two_words(p, "size", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        ASTNode *arg = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = arg;
        return make_builtin_call("__length", args, 1, t->line, t->col);
    }
    /* "number of letters in X" / "number of characters in X" → __length(X) */
    if (check_word(p, "number") && p->pos + 3 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "of") == 0 &&
        p->tokens[p->pos + 2].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 2].value, "letters") == 0 ||
         strcmp(p->tokens[p->pos + 2].value, "characters") == 0) &&
        p->tokens[p->pos + 3].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 3].value, "in") == 0) {
        Token *t = advance_parser(p);
        advance_parser(p); advance_parser(p); advance_parser(p); /* skip "of letters/characters in" */
        ASTNode *arg = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = arg;
        return make_builtin_call("__length", args, 1, t->line, t->col);
    }

    /* "number of items in X" → __length(X) */
    if (check_word(p, "number") && p->pos + 3 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "of") == 0 &&
        p->tokens[p->pos + 2].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 2].value, "items") == 0 ||
         strcmp(p->tokens[p->pos + 2].value, "elements") == 0 ||
         strcmp(p->tokens[p->pos + 2].value, "entries") == 0) &&
        p->tokens[p->pos + 3].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 3].value, "in") == 0) {
        Token *t = advance_parser(p);
        advance_parser(p); advance_parser(p); advance_parser(p);
        ASTNode *arg = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = arg;
        return make_builtin_call("__length", args, 1, t->line, t->col);
    }

    /* "items between X and Y in/of LIST" → slice(LIST, X, Y+1)
     * Also handles "items between Xth and Yth index of LIST" */
    if ((check_word(p, "items") || check_word(p, "elements")) &&
        p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "between") == 0) {
        Token *t = advance_parser(p); /* consume "items" */
        advance_parser(p); /* consume "between" */
        ASTNode *start = parse_primary(p);
        /* Skip "index"/"indices"/"position" if present */
        if (check_word(p, "index") || check_word(p, "indices") ||
            check_word(p, "position")) advance_parser(p);
        expect_word(p, "and", "expected 'and' in 'items between X and Y'");
        ASTNode *end_node = parse_primary(p);
        if (check_word(p, "index") || check_word(p, "indices") ||
            check_word(p, "position") || check_word(p, "positions")) advance_parser(p);
        if (check_word(p, "in") || check_word(p, "of")) advance_parser(p);
        ASTNode *list = parse_postfix(p);
        /* Slice is exclusive on end, but "between X and Y" is inclusive in English.
         * We add 1 to the end index: slice(list, start, end+1) */
        ASTNode *end_plus_one = ast_binary(end_node, "+",
            ast_number_lit(1, t->line, t->col), t->line, t->col);
        ASTNode **args = malloc(sizeof(ASTNode *) * 3);
        args[0] = list; args[1] = start; args[2] = end_plus_one;
        return make_builtin_call("__list_slice", args, 3, t->line, t->col);
    }

    /* "item at index N in X" / "element at index N in X" → __index(X, N) */
    if ((check_word(p, "item") || check_word(p, "element")) &&
        p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "at") == 0) {
        Token *t = advance_parser(p);
        advance_parser(p); /* consume "at" */
        if (check_word(p, "index")) advance_parser(p); /* optional "index" */
        if (check_word(p, "position")) advance_parser(p); /* optional "position" */
        ASTNode *idx = parse_primary(p);
        if (check_word(p, "in") || check_word(p, "of")) advance_parser(p);
        ASTNode *collection = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = collection; args[1] = idx;
        return make_builtin_call("__index", args, 2, t->line, t->col);
    }

    /* "item N in/of X" → __index(X, N-1) for 1-based indexing
     * Actually let's keep 0-based but support "Nth item in X" below */

    /* --- Ordinal indexing: "the 1st/2nd/3rd/4th/5th/Nth item in X" --- */
    /* Ordinals are handled by checking if the current token is a number-like
     * word or a number followed by "st"/"nd"/"rd"/"th" in the token.
     * Since the lexer treats "5th" as a word, we parse the number from it. */
    if (check_type(p, TOK_WORD) && p->pos + 2 < p->count) {
        const char *w = current(p)->value;
        int wlen = strlen(w);
        /* Check if word ends in st/nd/rd/th and starts with digits */
        if (wlen >= 3 && isdigit(w[0]) &&
            (strcmp(w + wlen - 2, "st") == 0 ||
             strcmp(w + wlen - 2, "nd") == 0 ||
             strcmp(w + wlen - 2, "rd") == 0 ||
             strcmp(w + wlen - 2, "th") == 0)) {
            /* Extract the number */
            int ordinal = atoi(w);
            /* Check if followed by "item/element in/of" */
            if (p->pos + 2 < p->count &&
                p->tokens[p->pos + 1].type == TOK_WORD &&
                (strcmp(p->tokens[p->pos + 1].value, "item") == 0 ||
                 strcmp(p->tokens[p->pos + 1].value, "element") == 0) &&
                p->tokens[p->pos + 2].type == TOK_WORD &&
                (strcmp(p->tokens[p->pos + 2].value, "in") == 0 ||
                 strcmp(p->tokens[p->pos + 2].value, "of") == 0)) {
                Token *t = advance_parser(p); /* consume ordinal */
                advance_parser(p); /* consume "item"/"element" */
                advance_parser(p); /* consume "in"/"of" */
                ASTNode *collection = parse_postfix(p);
                /* 1-based to 0-based: "1st item" = index 0 */
                ASTNode **args = malloc(sizeof(ASTNode *) * 2);
                args[0] = collection;
                args[1] = ast_number_lit(ordinal - 1, t->line, t->col);
                return make_builtin_call("__index", args, 2, t->line, t->col);
            }
        }
    }

    /* --- Character access --- */
    /* "first letter/character/item/element of X" → index 0 */
    if (check_word(p, "first") && p->pos + 2 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        p->tokens[p->pos + 2].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 2].value, "of") == 0) {
        const char *noun = p->tokens[p->pos + 1].value;
        if (strcmp(noun, "letter") == 0 || strcmp(noun, "character") == 0) {
            Token *t = advance_parser(p);
            advance_parser(p); advance_parser(p);
            ASTNode *str = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = str;
            args[1] = ast_number_lit(0, t->line, t->col);
            return make_builtin_call("__char_at", args, 2, t->line, t->col);
        }
        if (strcmp(noun, "item") == 0 || strcmp(noun, "element") == 0 ||
            strcmp(noun, "entry") == 0 || strcmp(noun, "value") == 0) {
            Token *t = advance_parser(p);
            advance_parser(p); advance_parser(p);
            ASTNode *collection = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = collection;
            args[1] = ast_number_lit(0, t->line, t->col);
            return make_builtin_call("__index", args, 2, t->line, t->col);
        }
    }
    /* "last letter/character/item/element of X" */
    if (check_word(p, "last") && p->pos + 2 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        p->tokens[p->pos + 2].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 2].value, "of") == 0) {
        const char *noun = p->tokens[p->pos + 1].value;
        if (strcmp(noun, "letter") == 0 || strcmp(noun, "character") == 0) {
            Token *t = advance_parser(p);
            advance_parser(p); advance_parser(p);
            ASTNode *str = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = str;
            return make_builtin_call("__last_char", args, 1, t->line, t->col);
        }
        if (strcmp(noun, "item") == 0 || strcmp(noun, "element") == 0 ||
            strcmp(noun, "entry") == 0 || strcmp(noun, "value") == 0) {
            Token *t = advance_parser(p);
            advance_parser(p); advance_parser(p);
            ASTNode *collection = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = collection;
            return make_builtin_call("__list_last", args, 1, t->line, t->col);
        }
    }
    /* "character/letter N of X" → __char_at(X, N) */
    if ((check_word(p, "character") || check_word(p, "letter")) &&
        p->pos + 1 < p->count && p->tokens[p->pos + 1].type != TOK_NEWLINE) {
        Token *t = advance_parser(p);
        /* Check for "at first/last index of X" */
        if (check_word(p, "at")) {
            advance_parser(p); /* consume "at" */
            if (check_word(p, "first")) {
                advance_parser(p); /* consume "first" */
                if (check_word(p, "index")) advance_parser(p); /* optional "index" */
                expect_word(p, "of", "expected 'of'");
                ASTNode *str = parse_postfix(p);
                ASTNode **args = malloc(sizeof(ASTNode *) * 2);
                args[0] = str;
                args[1] = ast_number_lit(0, t->line, t->col);
                return make_builtin_call("__char_at", args, 2, t->line, t->col);
            } else if (check_word(p, "last")) {
                advance_parser(p); /* consume "last" */
                if (check_word(p, "index")) advance_parser(p);
                expect_word(p, "of", "expected 'of'");
                ASTNode *str = parse_postfix(p);
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = str;
                return make_builtin_call("__last_char", args, 1, t->line, t->col);
            }
        }
        /* "character N of X" */
        ASTNode *idx = parse_primary(p);
        expect_word(p, "of", "expected 'of' in 'character N of X'");
        ASTNode *str = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = str;
        args[1] = idx;
        return make_builtin_call("__char_at", args, 2, t->line, t->col);
    }

    /* --- Replace (prefix form) --- */
    /* "replace X with Y in Z" → __replace(Z, X, Y) */
    if (check_word(p, "replace") && !looks_like_past_tense(p)) {
        Token *t = advance_parser(p);
        ASTNode *old_s = parse_primary(p);
        expect_word(p, "with", "expected 'with' in 'replace X with Y in Z'");
        ASTNode *new_s = parse_primary(p);
        expect_word(p, "in", "expected 'in' in 'replace X with Y in Z'");
        ASTNode *str = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 3);
        args[0] = str; args[1] = old_s; args[2] = new_s;
        return make_builtin_call("__replace", args, 3, t->line, t->col);
    }

    /* --- Input --- */
    /* "input" / "input(prompt)" / "input prompt" → __input */
    if (check_word(p, "input")) {
        Token *t = advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        if (check_type(p, TOK_LPAREN)) {
            advance_parser(p);
            args[0] = parse_expression(p);
            expect_type(p, TOK_RPAREN, "expected ')' after input prompt");
            return make_builtin_call("__input", args, 1, t->line, t->col);
        } else if (check_type(p, TOK_STRING)) {
            args[0] = parse_primary(p);
            return make_builtin_call("__input", args, 1, t->line, t->col);
        }
        free(args);
        return make_builtin_call("__input_no_prompt", NULL, 0, t->line, t->col);
    }
    /* "ask EXPR" → __input(EXPR) */
    if (check_word(p, "ask")) {
        Token *t = advance_parser(p);
        ASTNode *prompt = parse_primary(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = prompt;
        return make_builtin_call("__input", args, 1, t->line, t->col);
    }

    /* --- Directory builtins (no args) --- */
    /* "home directory" / "home folder" → __home_dir() */
    if (check_word(p, "home") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "directory") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "folder") == 0)) {
        Token *t = advance_parser(p); advance_parser(p);
        return make_builtin_call("__home_dir", NULL, 0, t->line, t->col);
    }
    /* "current directory" / "current folder" / "working directory" */
    if ((check_word(p, "current") || check_word(p, "working")) &&
        p->pos + 1 < p->count && p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "directory") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "folder") == 0)) {
        Token *t = advance_parser(p); advance_parser(p);
        return make_builtin_call("__current_dir", NULL, 0, t->line, t->col);
    }
    /* "temp directory" / "temp folder" / "temporary directory" */
    if ((check_word(p, "temp") || check_word(p, "temporary")) &&
        p->pos + 1 < p->count && p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "directory") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "folder") == 0)) {
        Token *t = advance_parser(p); advance_parser(p);
        return make_builtin_call("__temp_dir", NULL, 0, t->line, t->col);
    }
    /* "desktop folder" / "desktop directory" / "desktop" (if not variable) */
    if (check_word(p, "desktop") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "directory") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "folder") == 0)) {
        Token *t = advance_parser(p); advance_parser(p);
        return make_builtin_call("__desktop_dir", NULL, 0, t->line, t->col);
    }
    /* "downloads folder" */
    if (check_word(p, "downloads") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "directory") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "folder") == 0)) {
        Token *t = advance_parser(p); advance_parser(p);
        return make_builtin_call("__downloads_dir", NULL, 0, t->line, t->col);
    }
    /* "documents folder" */
    if (check_word(p, "documents") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "directory") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "folder") == 0)) {
        Token *t = advance_parser(p); advance_parser(p);
        return make_builtin_call("__documents_dir", NULL, 0, t->line, t->col);
    }

    /* --- File operations (prefix) ---
     * Supports many natural phrasings:
     *   read file X           read from file X       read the file X
     *   read lines from X     read all lines from X  read all lines in file X
     *   contents of file X    contents of the file X
     *   lines of file X       lines in file X        lines from file X
     */

    /* "read [all] lines [from/of/in] [file] EXPR" — must check BEFORE "read file" */
    if (check_word(p, "read") && p->pos + 1 < p->count) {
        int saved = p->pos;
        int matched_lines = 0;
        advance_parser(p); /* consume "read" */
        /* optional "all" */
        if (check_word(p, "all")) advance_parser(p);
        if (check_word(p, "lines")) {
            advance_parser(p);
            matched_lines = 1;
            /* optional "from"/"of"/"in" */
            if (check_word(p, "from") || check_word(p, "of") || check_word(p, "in")) {
                advance_parser(p);
            }
            /* optional "the" */
            if (check_word(p, "the")) advance_parser(p);
            /* optional "file" */
            if (check_word(p, "file")) advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = parse_file_path(p);
            Token t_tok = p->tokens[saved];
            return make_builtin_call("__file_lines", args, 1, t_tok.line, t_tok.col);
        }
        if (!matched_lines) p->pos = saved;
    }

    /* "read [from] [the] file EXPR" / "read [from] EXPR" */
    if (check_word(p, "read") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        (strcmp(p->tokens[p->pos + 1].value, "file") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "from") == 0 ||
         strcmp(p->tokens[p->pos + 1].value, "the") == 0)) {
        Token *t = advance_parser(p); /* consume "read" */
        /* Skip optional "from" */
        if (check_word(p, "from")) advance_parser(p);
        /* Skip optional "the" */
        if (check_word(p, "the")) advance_parser(p);
        /* Skip optional "file" */
        if (check_word(p, "file")) advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_file_path(p);
        return make_builtin_call("__file_read", args, 1, t->line, t->col);
    }

    /* "contents of [the] [file] EXPR" → __file_read(EXPR) */
    if (check_two_words(p, "contents", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        if (check_word(p, "the")) advance_parser(p);
        if (check_word(p, "file")) advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_file_path(p);
        return make_builtin_call("__file_read", args, 1, t->line, t->col);
    }

    /* "[all] lines of/from/in [the] [file] EXPR" */
    if ((check_word(p, "lines") || check_two_words(p, "all", "lines")) &&
        p->pos + 1 < p->count) {
        int saved = p->pos;
        Token t_tok = p->tokens[saved];
        if (check_word(p, "all")) advance_parser(p);
        advance_parser(p); /* consume "lines" */
        if (check_word(p, "of") || check_word(p, "from") || check_word(p, "in")) {
            advance_parser(p);
            if (check_word(p, "the")) advance_parser(p);
            if (check_word(p, "file")) advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = parse_file_path(p);
            return make_builtin_call("__file_lines", args, 1, t_tok.line, t_tok.col);
        }
        /* Not a lines-of-file pattern, restore */
        p->pos = saved;
    }

    /* "file EXPR exists" — needs to be an expression. We'll rearrange here:
     * Actually, we want "X exists" as postfix since "file EXPR" can be a
     * compound term. Let's handle "does file EXPR exist" as prefix: */
    if (check_word(p, "does") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "file") == 0) {
        Token *t = advance_parser(p); advance_parser(p); /* does file */
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        ASTNode *path = parse_file_path(p);
        /* optional "exist" */
        if (check_word(p, "exist") || check_word(p, "exists")) advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = path;
        return make_builtin_call("__file_exists", args, 1, t->line, t->col);
    }

    /* "file EXPR exists" — prefix form that reads natural */
    if (check_word(p, "file") && p->pos + 1 < p->count) {
        /* Peek for the pattern: file STR_OR_IDENT ... exists */
        int saved = p->pos;
        advance_parser(p);
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        if (check_type(p, TOK_STRING) || check_type(p, TOK_WORD)) {
            ASTNode *path = parse_file_path(p);
            if (check_word(p, "exists")) {
                Token *t = advance_parser(p);
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = path;
                return make_builtin_call("__file_exists", args, 1, t->line, t->col);
            }
            /* Not "file X exists" — restore */
            ast_free(path);
        }
        p->pos = saved;
    }

    /* --- Math builtins --- */
    /* Single-arg: "absolute value of X", "square root of X", "floor of X", etc. */
    if (check_word(p, "absolute") && p->pos + 2 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "value") == 0 &&
        p->tokens[p->pos + 2].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 2].value, "of") == 0) {
        Token *t = advance_parser(p); advance_parser(p); advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_unary(p);
        return make_builtin_call("__abs", args, 1, t->line, t->col);
    }
    if (check_word(p, "square") && check_two_words(p, "square", "root")) {
        Token *t = advance_parser(p); advance_parser(p); /* consume "square root" */
        if (check_word(p, "of")) advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_postfix(p);
        return make_builtin_call("__sqrt_val", args, 1, t->line, t->col);
    }
    /* "floor of X" / "ceiling of X" / "round X" */
    if (check_two_words(p, "floor", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_postfix(p);
        return make_builtin_call("__floor_val", args, 1, t->line, t->col);
    }
    if (check_two_words(p, "ceiling", "of") || check_two_words(p, "ceil", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_postfix(p);
        return make_builtin_call("__ceil_val", args, 1, t->line, t->col);
    }
    /* "X raised to the power of Y" is handled in postfix */
    /* "X to the power of Y" — also postfix */

    /* "random number" / "random number between X and Y" */
    if (check_word(p, "random")) {
        Token *t = advance_parser(p);
        if (check_word(p, "number")) advance_parser(p); /* optional "number" */
        if (check_word(p, "between")) {
            advance_parser(p);
            ASTNode *lo = parse_comparison(p);
            expect_word(p, "and", "expected 'and' in 'random between X and Y'");
            ASTNode *hi = parse_comparison(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = lo; args[1] = hi;
            return make_builtin_call("__random_between", args, 2, t->line, t->col);
        }
        return make_builtin_call("__random_val", NULL, 0, t->line, t->col);
    }

    /* "minimum of X and Y" / "maximum of X and Y" */
    if (check_two_words(p, "minimum", "of") || check_two_words(p, "min", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        ASTNode *a = parse_comparison(p);
        expect_word(p, "and", "expected 'and' in 'minimum of X and Y'");
        ASTNode *b = parse_comparison(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = a; args[1] = b;
        return make_builtin_call("__min_val", args, 2, t->line, t->col);
    }
    if (check_two_words(p, "maximum", "of") || check_two_words(p, "max", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        ASTNode *a = parse_comparison(p);
        expect_word(p, "and", "expected 'and' in 'maximum of X and Y'");
        ASTNode *b = parse_comparison(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = a; args[1] = b;
        return make_builtin_call("__max_val", args, 2, t->line, t->col);
    }

    /* --- Type operations --- */
    /* "type of X" → __type_of(X) */
    if (check_two_words(p, "type", "of")) {
        Token *t = advance_parser(p); advance_parser(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = parse_postfix(p);
        return make_builtin_call("__type_of", args, 1, t->line, t->col);
    }
    /* "convert X to string/number" or "X as a string/number" (postfix handled below) */
    /* "convert X to TYPE" / "conversion of X to/as a TYPE" */
    if (check_word(p, "convert") || check_two_words(p, "conversion", "of")) {
        Token *t = advance_parser(p);
        if (check_word(p, "of")) advance_parser(p); /* "conversion of" */
        /* Use parse_primary (not parse_postfix) so "as a number" stays
         * for the conversion keyword to consume */
        ASTNode *val = parse_primary(p);
        /* Accept "to" or "as" (with optional "a"/"an") */
        if (check_word(p, "to") || check_word(p, "as")) {
            advance_parser(p);
        } else {
            esu_error_at_token(current(p), "expected 'to' or 'as' in conversion");
        }
        if (check_word(p, "a") || check_word(p, "an")) advance_parser(p);
        if (check_word(p, "string") || check_word(p, "text")) {
            advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = val;
            return make_builtin_call("__to_string_val", args, 1, t->line, t->col);
        }
        if (check_word(p, "number")) {
            advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = val;
            return make_builtin_call("__to_number_val", args, 1, t->line, t->col);
        }
        esu_error_at_token(current(p), "expected 'string' or 'number' after conversion");
    }

    /* Lambda / anonymous function:
     *   "a function of X that returns EXPR"
     *   "a function of X and Y that returns EXPR"
     *   "a function taking X, return EXPR"
     * Must check before constructor (also starts with "a"). */
    if ((check_word(p, "a") || check_word(p, "an")) && p->pos + 2 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "function") == 0) {
        int saved = p->pos;
        Token *a_tok = advance_parser(p); /* consume a/an */
        advance_parser(p); /* consume 'function' */

        /* Optional "of X and Y" or "taking X and Y" */
        char **params = NULL;
        int param_count = 0;
        if (check_word(p, "of") || check_word(p, "taking") || check_word(p, "with")) {
            advance_parser(p);
            parse_param_list(p, &params, &param_count);
        }

        /* Body: "that returns EXPR" (single expression form) */
        if (check_word(p, "that")) {
            advance_parser(p);
            if (check_word(p, "returns") || check_word(p, "return")) {
                advance_parser(p);
                ASTNode *body = parse_expression(p);
                return ast_lambda(params, param_count, body, a_tok->line, a_tok->col);
            }
        }
        /* Simpler "returning EXPR" / "returns EXPR" */
        if (check_word(p, "returning") || check_word(p, "returns")) {
            advance_parser(p);
            ASTNode *body = parse_expression(p);
            return ast_lambda(params, param_count, body, a_tok->line, a_tok->col);
        }

        /* Not a recognized lambda form — restore */
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        p->pos = saved;
    }

    /* Constructor: "a [new] TYPE with FIELD X, FIELD Y, ..."
     *           or "a [new] TYPE named NAME with FIELD X, ..."
     *           or "a new TYPE" (no fields)
     * Must check before other uses of "a". */
    if ((check_word(p, "a") || check_word(p, "an")) && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *a_tok = advance_parser(p);
        int had_new = 0;
        if (check_word(p, "new")) { advance_parser(p); had_new = 1; }

        /* Next must be a type name (a plain word, not a reserved word) */
        int ok = 0;
        if (check_type(p, TOK_WORD)) {
            const char *w = current(p)->value;
            /* Reject known non-type words (articles, builtins, etc.) */
            if (strcmp(w, "list") != 0 && strcmp(w, "new") != 0 &&
                strcmp(w, "number") != 0 && strcmp(w, "string") != 0 &&
                strcmp(w, "map") != 0) {
                ok = 1;
            }
        }

        if (!ok) { p->pos = saved; }
        else {
            Token *type_tok = advance_parser(p);
            int has_with = check_word(p, "with");
            int has_named = check_word(p, "named") || check_word(p, "called");

            if (!has_with && !has_named && !had_new) {
                /* Not a constructor — backtrack */
                p->pos = saved;
            } else {
                int cap = 4;
                char **field_names = malloc(sizeof(char *) * cap);
                ASTNode **values = malloc(sizeof(ASTNode *) * cap);
                int count = 0;

                if (has_named) {
                    advance_parser(p);
                    ASTNode *val = parse_comparison(p);
                    field_names[count] = str_dup_parser("name");
                    values[count] = val;
                    count++;
                    has_with = check_word(p, "with");
                }

                if (has_with) {
                    advance_parser(p);
                    while (check_type(p, TOK_WORD)) {
                        Token *fname = advance_parser(p);
                        ASTNode *val = parse_comparison(p);
                        if (count >= cap) {
                            cap *= 2;
                            field_names = realloc(field_names, sizeof(char *) * cap);
                            values = realloc(values, sizeof(ASTNode *) * cap);
                        }
                        field_names[count] = str_dup_parser(fname->value);
                        values[count] = val;
                        count++;
                        if (check_word(p, "and") || check_type(p, TOK_COMMA)) {
                            int was_comma = check_type(p, TOK_COMMA);
                            advance_parser(p);
                            if (was_comma && check_word(p, "and")) advance_parser(p);
                        } else {
                            break;
                        }
                    }
                }

                return ast_constructor(type_tok->value, field_names, values, count,
                                       a_tok->line, a_tok->col);
            }
        }
    }

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
        else esu_error_at_token(current(p), "expected 'and' in 'the difference of X and Y'");
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

    /* List literal: [expr, expr, ...] */
    if (tok->type == TOK_LBRACKET) {
        Token *t = advance_parser(p);
        int cap = 8;
        ASTNode **items = malloc(sizeof(ASTNode *) * cap);
        int count = 0;
        if (!check_type(p, TOK_RBRACKET)) {
            items[count++] = parse_expression(p);
            while (check_type(p, TOK_COMMA)) {
                advance_parser(p);
                if (check_type(p, TOK_RBRACKET)) break; /* trailing comma */
                if (count >= cap) { cap *= 2; items = realloc(items, sizeof(ASTNode *) * cap); }
                items[count++] = parse_expression(p);
            }
        }
        expect_type(p, TOK_RBRACKET, "expected ']' to close list");
        return ast_list_lit(items, count, t->line, t->col);
    }

    /* Map literal: {"key": value, ...} */
    if (tok->type == TOK_LBRACE) {
        Token *t = advance_parser(p);
        int cap = 8;
        char **keys = malloc(sizeof(char *) * cap);
        ASTNode **values = malloc(sizeof(ASTNode *) * cap);
        int count = 0;
        if (!check_type(p, TOK_RBRACE)) {
            Token *key = expect_type(p, TOK_STRING, "expected string key in map");
            expect_type(p, TOK_COLON, "expected ':' after map key");
            ASTNode *val = parse_expression(p);
            keys[count] = malloc(strlen(key->value) + 1);
            strcpy(keys[count], key->value);
            values[count] = val;
            count++;
            while (check_type(p, TOK_COMMA)) {
                advance_parser(p);
                if (check_type(p, TOK_RBRACE)) break;
                if (count >= cap) {
                    cap *= 2;
                    keys = realloc(keys, sizeof(char *) * cap);
                    values = realloc(values, sizeof(ASTNode *) * cap);
                }
                key = expect_type(p, TOK_STRING, "expected string key in map");
                expect_type(p, TOK_COLON, "expected ':' after map key");
                val = parse_expression(p);
                keys[count] = malloc(strlen(key->value) + 1);
                strcpy(keys[count], key->value);
                values[count] = val;
                count++;
            }
        }
        expect_type(p, TOK_RBRACE, "expected '}' to close map");
        return ast_map_lit(keys, values, count, t->line, t->col);
    }

    if (tok->type == TOK_NUMBER) {
        advance_parser(p);
        return ast_number_lit(atof(tok->value), tok->line, tok->col);
    }

    /* Bare ordinal word: "2nd", "5th" → number (1-based, but used directly
     * as 0-based when in slice context handled by caller) */
    if (tok->type == TOK_WORD) {
        const char *w = tok->value;
        int wlen = strlen(w);
        if (wlen >= 3 && isdigit(w[0]) &&
            (strcmp(w + wlen - 2, "st") == 0 ||
             strcmp(w + wlen - 2, "nd") == 0 ||
             strcmp(w + wlen - 2, "rd") == 0 ||
             strcmp(w + wlen - 2, "th") == 0)) {
            /* Return N-1 for 0-based indexing compatibility */
            advance_parser(p);
            return ast_number_lit(atoi(w) - 1, tok->line, tok->col);
        }
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

    esu_error_at_token(tok, "expected an expression");
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
        strcmp(w, "called") == 0 || strcmp(w, "displayed") == 0 ||
        strcmp(w, "replaced") == 0 || strcmp(w, "uppercased") == 0 ||
        strcmp(w, "lowercased") == 0 || strcmp(w, "trimmed") == 0 ||
        strcmp(w, "rounded") == 0) return 0;
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
        strcmp(w, "nothing") == 0 || strcmp(w, "this") == 0 ||
        strcmp(w, "replacing") == 0 || strcmp(w, "begins") == 0 ||
        strcmp(w, "starts") == 0 || strcmp(w, "ends") == 0 ||
        strcmp(w, "contains") == 0 || strcmp(w, "has") == 0 ||
        strcmp(w, "ceiling") == 0 || strcmp(w, "nothing") == 0 ||
        strcmp(w, "string") == 0 || strcmp(w, "minimum") == 0 ||
        strcmp(w, "maximum") == 0) return 0;
    if (len > 4 && strcmp(w + len - 4, "ying") == 0) return 1;
    if (len > 3 && strcmp(w + len - 3, "ing") == 0) return 1;
    /* -s/-es/-ies only when next token is a literal value (number, string, paren)
     * to avoid ambiguity with plural nouns like "items", "results" */
    return 0;
}

/* Stricter check: looks like a third-person verb AND next is a literal */
static int looks_like_third_person_call(Parser *p) {
    if (!check_type(p, TOK_WORD)) return 0;
    const char *w = current(p)->value;
    int len = strlen(w);
    if (len < 4) return 0;
    /* Exclude builtin keywords */
    if (strcmp(w, "equals") == 0 || strcmp(w, "exceeds") == 0 ||
        strcmp(w, "contains") == 0 || strcmp(w, "has") == 0 ||
        strcmp(w, "starts") == 0 || strcmp(w, "ends") == 0 ||
        strcmp(w, "begins") == 0 || strcmp(w, "items") == 0 ||
        strcmp(w, "takes") == 0) return 0;
    /* Must end in -s and next must be a number, string, or ( */
    if (w[len - 1] != 's') return 0;
    int peek = p->pos + 1;
    if (peek >= p->count) return 0;
    TokenType next = p->tokens[peek].type;
    return next == TOK_NUMBER || next == TOK_STRING || next == TOK_LPAREN;
}

/* Parse postfix conjugation: after a primary expression, check for past-tense
 * verb and wrap as function call.
 * "x doubled"           → double(x)       (single-arg, no second operand)
 * "x multiplied by y"   → multiply(x, y)  (two-arg via "by") */
static ASTNode *parse_postfix(Parser *p) {
    ASTNode *expr = parse_primary(p);

    /* Index and dot access: x[0], x["key"], x.field, x.method(args) */
    for (;;) {
        if (check_type(p, TOK_LBRACKET)) {
            advance_parser(p);
            ASTNode *index = parse_expression(p);
            expect_type(p, TOK_RBRACKET, "expected ']' after index");
            expr = ast_index_expr(expr, index, expr->line, expr->col);
            continue;
        }
        if (check_type(p, TOK_DOT) && p->pos + 1 < p->count &&
            p->tokens[p->pos + 1].type == TOK_WORD) {
            Token *dot = advance_parser(p); /* consume '.' */
            Token *field = advance_parser(p); /* the field/method name */
            /* If followed by '(' it's a method call */
            if (check_type(p, TOK_LPAREN)) {
                advance_parser(p);
                int arg_cap = 4;
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
                expect_type(p, TOK_RPAREN, "expected ')' after method arguments");
                expr = ast_method_call(expr, field->value, args, arg_count,
                                       dot->line, dot->col);
            } else {
                /* Field access — but could also be a zero-arg method call.
                 * Heuristic: if it's followed by something that looks like
                 * a method-call continuation (end of expr), just emit member. */
                expr = ast_member_expr(expr, field->value, dot->line, dot->col);
            }
            continue;
        }
        break;
    }

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

    /* Postfix builtin string operations */
    for (;;) {
        /* "x is a TYPE" / "x is a kind of TYPE" / "x is an instance of TYPE"
         * → __is_instance_of(x, "TYPE")
         * Only triggers when followed by a/an + [kind/type/instance of] + TYPE
         * and the token after TYPE is a separator (end of expression). */
        if (check_word(p, "is") && p->pos + 2 < p->count) {
            int peek = p->pos + 1;
            /* Optional "not" */
            int negated = 0;
            if (peek < p->count && p->tokens[peek].type == TOK_WORD &&
                strcmp(p->tokens[peek].value, "not") == 0) {
                negated = 1;
                peek++;
            }
            if (peek < p->count && p->tokens[peek].type == TOK_WORD &&
                (strcmp(p->tokens[peek].value, "a") == 0 ||
                 strcmp(p->tokens[peek].value, "an") == 0)) {
                /* Maybe "is a/an TYPE" — but only if TYPE isn't followed
                 * by something that would indicate it's an expression */
                int type_pos = peek + 1;
                /* Optional "kind/type/instance of" */
                if (type_pos + 1 < p->count &&
                    p->tokens[type_pos].type == TOK_WORD &&
                    (strcmp(p->tokens[type_pos].value, "kind") == 0 ||
                     strcmp(p->tokens[type_pos].value, "type") == 0 ||
                     strcmp(p->tokens[type_pos].value, "instance") == 0) &&
                    p->tokens[type_pos + 1].type == TOK_WORD &&
                    strcmp(p->tokens[type_pos + 1].value, "of") == 0) {
                    type_pos += 2;
                }
                if (type_pos < p->count && p->tokens[type_pos].type == TOK_WORD) {
                    /* Check that what follows TYPE is a separator
                     * (not a binary op / method call etc.) */
                    int after = type_pos + 1;
                    int looks_terminal = (after >= p->count ||
                        p->tokens[after].type == TOK_NEWLINE ||
                        p->tokens[after].type == TOK_COMMA ||
                        p->tokens[after].type == TOK_DOT ||
                        p->tokens[after].type == TOK_EOF ||
                        p->tokens[after].type == TOK_RPAREN ||
                        p->tokens[after].type == TOK_RBRACKET ||
                        (p->tokens[after].type == TOK_WORD &&
                         (strcmp(p->tokens[after].value, "then") == 0 ||
                          strcmp(p->tokens[after].value, "do") == 0 ||
                          strcmp(p->tokens[after].value, "end") == 0 ||
                          strcmp(p->tokens[after].value, "and") == 0 ||
                          strcmp(p->tokens[after].value, "or") == 0 ||
                          strcmp(p->tokens[after].value, "else") == 0)));
                    if (looks_terminal) {
                        Token *is_tok = advance_parser(p); /* is */
                        if (negated) advance_parser(p); /* not */
                        advance_parser(p); /* a/an */
                        if (check_word(p, "kind") || check_word(p, "type") ||
                            check_word(p, "instance")) {
                            advance_parser(p);
                            if (check_word(p, "of")) advance_parser(p);
                        }
                        Token *type_tok = advance_parser(p);
                        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
                        args[0] = expr;
                        args[1] = ast_string_lit(type_tok->value,
                                                 type_tok->line, type_tok->col);
                        ASTNode *call = make_builtin_call("__is_instance_of",
                                                         args, 2,
                                                         is_tok->line, is_tok->col);
                        if (negated) {
                            ASTNode **not_args = malloc(sizeof(ASTNode *));
                            not_args[0] = call;
                            expr = make_builtin_call("__not", not_args, 1,
                                                     is_tok->line, is_tok->col);
                        } else {
                            expr = call;
                        }
                        continue;
                    }
                }
            }
        }

        /* --- Contains --- */
        /* "x contains Y" → __contains(x, Y) */
        if (check_word(p, "contains")) {
            Token *t = advance_parser(p);
            ASTNode *arg = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = expr; args[1] = arg;
            expr = make_builtin_call("__contains", args, 2, t->line, t->col);
            continue;
        }
        /* "x has Y in it" → __contains(x, Y) */
        if (check_word(p, "has") && !check_two_words(p, "has", "key")) {
            Token *t = advance_parser(p);
            ASTNode *arg = parse_primary(p);
            /* consume "in it" if present */
            if (check_word(p, "in")) { advance_parser(p); }
            if (check_word(p, "it")) { advance_parser(p); }
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = expr; args[1] = arg;
            expr = make_builtin_call("__contains", args, 2, t->line, t->col);
            continue;
        }

        /* --- Starts/ends with --- */
        /* "x starts with Y" / "x begins with Y" → __starts_with(x, Y) */
        if (check_two_words(p, "starts", "with") || check_two_words(p, "begins", "with")) {
            Token *t = advance_parser(p); advance_parser(p);
            ASTNode *arg = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = expr; args[1] = arg;
            expr = make_builtin_call("__starts_with", args, 2, t->line, t->col);
            continue;
        }
        /* "x ends with Y" → __ends_with(x, Y) */
        if (check_two_words(p, "ends", "with")) {
            Token *t = advance_parser(p); advance_parser(p);
            ASTNode *arg = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = expr; args[1] = arg;
            expr = make_builtin_call("__ends_with", args, 2, t->line, t->col);
            continue;
        }
        /* "x is at the beginning/start of Y" → __starts_with(Y, x) (note: reversed) */
        if (check_word(p, "is") && p->pos + 3 < p->count &&
            p->tokens[p->pos + 1].type == TOK_WORD &&
            strcmp(p->tokens[p->pos + 1].value, "at") == 0) {
            /* Look for "is at the beginning/start of" or "is at the end of" */
            int peek = p->pos + 2;
            if (peek < p->count && p->tokens[peek].type == TOK_WORD &&
                (strcmp(p->tokens[peek].value, "beginning") == 0 ||
                 strcmp(p->tokens[peek].value, "start") == 0)) {
                Token *t = advance_parser(p); /* is */
                advance_parser(p); /* at */
                advance_parser(p); /* beginning/start */
                if (check_word(p, "of")) advance_parser(p);
                ASTNode *str = parse_primary(p);
                ASTNode **args = malloc(sizeof(ASTNode *) * 2);
                args[0] = str; args[1] = expr; /* reversed: Y starts with x */
                expr = make_builtin_call("__starts_with", args, 2, t->line, t->col);
                continue;
            }
            if (peek < p->count && p->tokens[peek].type == TOK_WORD &&
                strcmp(p->tokens[peek].value, "end") == 0) {
                Token *t = advance_parser(p); /* is */
                advance_parser(p); /* at */
                advance_parser(p); /* end */
                if (check_word(p, "of")) advance_parser(p);
                ASTNode *str = parse_primary(p);
                ASTNode **args = malloc(sizeof(ASTNode *) * 2);
                args[0] = str; args[1] = expr; /* reversed: Y ends with x */
                expr = make_builtin_call("__ends_with", args, 2, t->line, t->col);
                continue;
            }
        }

        /* --- Case conversion --- */
        /* "x in uppercase" / "x in caps" / "x in all caps" */
        if (check_word(p, "in") && p->pos + 1 < p->count) {
            const char *next_val = p->tokens[p->pos + 1].value;
            if (p->tokens[p->pos + 1].type == TOK_WORD &&
                (strcmp(next_val, "uppercase") == 0 || strcmp(next_val, "caps") == 0)) {
                Token *t = advance_parser(p); advance_parser(p);
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = expr;
                expr = make_builtin_call("__to_upper", args, 1, t->line, t->col);
                continue;
            }
            if (p->tokens[p->pos + 1].type == TOK_WORD &&
                strcmp(next_val, "all") == 0 &&
                p->pos + 2 < p->count &&
                p->tokens[p->pos + 2].type == TOK_WORD &&
                strcmp(p->tokens[p->pos + 2].value, "caps") == 0) {
                Token *t = advance_parser(p); advance_parser(p); advance_parser(p);
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = expr;
                expr = make_builtin_call("__to_upper", args, 1, t->line, t->col);
                continue;
            }
            if (p->tokens[p->pos + 1].type == TOK_WORD &&
                strcmp(next_val, "lowercase") == 0) {
                Token *t = advance_parser(p); advance_parser(p);
                ASTNode **args = malloc(sizeof(ASTNode *));
                args[0] = expr;
                expr = make_builtin_call("__to_lower", args, 1, t->line, t->col);
                continue;
            }
        }
        /* "x uppercased" / "x lowercased" */
        if (check_word(p, "uppercased")) {
            Token *t = advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = expr;
            expr = make_builtin_call("__to_upper", args, 1, t->line, t->col);
            continue;
        }
        if (check_word(p, "lowercased")) {
            Token *t = advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = expr;
            expr = make_builtin_call("__to_lower", args, 1, t->line, t->col);
            continue;
        }

        /* --- Replace (postfix) --- */
        /* "x replaced OLD with NEW" → __replace(x, OLD, NEW) */
        if (check_word(p, "replaced") && !check_two_words(p, "replaced", "by")) {
            Token *t = advance_parser(p);
            ASTNode *old_s = parse_primary(p);
            expect_word(p, "with", "expected 'with' in 'replaced X with Y'");
            ASTNode *new_s = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 3);
            args[0] = expr; args[1] = old_s; args[2] = new_s;
            expr = make_builtin_call("__replace", args, 3, t->line, t->col);
            continue;
        }
        /* "x replaced by NEW in STR" → __replace(STR, x, NEW) — note: x is the old value */
        if (check_two_words(p, "replaced", "by")) {
            Token *t = advance_parser(p); advance_parser(p); /* consume "replaced by" */
            ASTNode *new_s = parse_primary(p);
            expect_word(p, "in", "expected 'in' in 'X replaced by Y in Z'");
            ASTNode *str = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 3);
            args[0] = str; args[1] = expr; args[2] = new_s;
            expr = make_builtin_call("__replace", args, 3, t->line, t->col);
            continue;
        }
        /* "x replacing OLD with NEW" → __replace(x, OLD, NEW) */
        if (check_word(p, "replacing")) {
            Token *t = advance_parser(p);
            ASTNode *old_s = parse_primary(p);
            expect_word(p, "with", "expected 'with' in 'replacing X with Y'");
            ASTNode *new_s = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 3);
            args[0] = expr; args[1] = old_s; args[2] = new_s;
            expr = make_builtin_call("__replace", args, 3, t->line, t->col);
            continue;
        }
        /* "x trimmed" → __trim(x) */
        if (check_word(p, "trimmed")) {
            Token *t = advance_parser(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = expr;
            expr = make_builtin_call("__trim", args, 1, t->line, t->col);
            continue;
        }
        /* "x rounded" / "x rounded down" / "x rounded up" */
        if (check_word(p, "rounded")) {
            Token *t = advance_parser(p);
            const char *func = "__round_val";
            if (check_word(p, "down")) { advance_parser(p); func = "__floor_val"; }
            else if (check_word(p, "up")) { advance_parser(p); func = "__ceil_val"; }
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = expr;
            expr = make_builtin_call(func, args, 1, t->line, t->col);
            continue;
        }
        /* "x as a string" / "x as a number" / "x as text" */
        if (check_word(p, "as")) {
            int peek = p->pos + 1;
            /* skip optional "a"/"an" */
            if (peek < p->count && p->tokens[peek].type == TOK_WORD &&
                (strcmp(p->tokens[peek].value, "a") == 0 ||
                 strcmp(p->tokens[peek].value, "an") == 0)) peek++;
            if (peek < p->count && p->tokens[peek].type == TOK_WORD) {
                const char *type_name = p->tokens[peek].value;
                if (strcmp(type_name, "string") == 0 || strcmp(type_name, "text") == 0) {
                    Token *t = advance_parser(p); /* consume "as" */
                    if (check_word(p, "a") || check_word(p, "an")) advance_parser(p);
                    advance_parser(p); /* consume "string"/"text" */
                    ASTNode **args = malloc(sizeof(ASTNode *));
                    args[0] = expr;
                    expr = make_builtin_call("__to_string_val", args, 1, t->line, t->col);
                    continue;
                }
                if (strcmp(type_name, "number") == 0) {
                    Token *t = advance_parser(p);
                    if (check_word(p, "a") || check_word(p, "an")) advance_parser(p);
                    advance_parser(p);
                    ASTNode **args = malloc(sizeof(ASTNode *));
                    args[0] = expr;
                    expr = make_builtin_call("__to_number_val", args, 1, t->line, t->col);
                    continue;
                }
            }
        }
        /* "x to the power of y" / "x raised to y" */
        if (check_word(p, "to") && p->pos + 2 < p->count &&
            p->tokens[p->pos + 1].type == TOK_WORD &&
            (strcmp(p->tokens[p->pos + 1].value, "power") == 0 ||
             (strcmp(p->tokens[p->pos + 1].value, "the") == 0 &&
              p->pos + 3 < p->count &&
              p->tokens[p->pos + 2].type == TOK_WORD &&
              strcmp(p->tokens[p->pos + 2].value, "power") == 0))) {
            Token *t = advance_parser(p); /* consume "to" */
            if (check_word(p, "the")) advance_parser(p);
            advance_parser(p); /* consume "power" */
            if (check_word(p, "of")) advance_parser(p);
            ASTNode *exp = parse_primary(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = expr; args[1] = exp;
            expr = make_builtin_call("__power", args, 2, t->line, t->col);
            continue;
        }
        break;
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
    /* Prefix verb: "doubling x" / "multiplying x and y" / "doubles 3" */
    if ((looks_like_prefix_verb(p) || looks_like_third_person_call(p)) && next_is_expr_start(p)) {
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
            if (!is_separator(p) && !check_type(p, TOK_WORD)) {
                esu_error_at_token(current(p), "expected end of line or ',' after statement");
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

/* Parse a file path expression, with optional directory suffix:
 *   EXPR                              → EXPR
 *   EXPR at DIR                       → DIR + "/" + EXPR
 *   EXPR in [the] directory DIR       → DIR + "/" + EXPR
 *   EXPR in [the] folder DIR          → DIR + "/" + EXPR
 *   EXPR in DIR                       → DIR + "/" + EXPR (if DIR starts with /, ./, or ~)
 * Returns a path expression (possibly a binary concat expression). */
static ASTNode *parse_file_path(Parser *p) {
    ASTNode *filename = parse_comparison(p);

    /* Check for directory suffix */
    if (check_word(p, "at")) {
        Token *at_tok = advance_parser(p);
        /* optional "the" */
        if (check_word(p, "the")) advance_parser(p);
        /* optional "directory"/"folder"/"path" */
        if (check_word(p, "directory") || check_word(p, "folder") ||
            check_word(p, "path")) advance_parser(p);
        ASTNode *dir = parse_comparison(p);
        /* Build: dir + "/" + filename */
        ASTNode *slash = ast_string_lit("/", at_tok->line, at_tok->col);
        ASTNode *dir_slash = ast_binary(dir, "+", slash, at_tok->line, at_tok->col);
        return ast_binary(dir_slash, "+", filename, at_tok->line, at_tok->col);
    }

    if (check_word(p, "in") && p->pos + 1 < p->count) {
        /* Only consume "in" if followed by "the directory/folder" or just
         * "directory/folder" — otherwise "in" could mean something else */
        int peek = p->pos + 1;
        int is_dir_phrase = 0;
        if (peek < p->count && p->tokens[peek].type == TOK_WORD) {
            const char *w1 = p->tokens[peek].value;
            if (strcmp(w1, "directory") == 0 || strcmp(w1, "folder") == 0) {
                is_dir_phrase = 1;
            } else if (strcmp(w1, "the") == 0 && peek + 1 < p->count &&
                       p->tokens[peek + 1].type == TOK_WORD) {
                const char *w2 = p->tokens[peek + 1].value;
                if (strcmp(w2, "directory") == 0 || strcmp(w2, "folder") == 0) {
                    is_dir_phrase = 1;
                }
            }
        }
        if (is_dir_phrase) {
            Token *in_tok = advance_parser(p); /* consume "in" */
            if (check_word(p, "the")) advance_parser(p);
            advance_parser(p); /* consume directory/folder */
            ASTNode *dir = parse_comparison(p);
            ASTNode *slash = ast_string_lit("/", in_tok->line, in_tok->col);
            ASTNode *dir_slash = ast_binary(dir, "+", slash, in_tok->line, in_tok->col);
            return ast_binary(dir_slash, "+", filename, in_tok->line, in_tok->col);
        }
    }

    return filename;
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
        esu_error_at_token(current(p), "expected 'from' or 'in' after for variable");
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
        "that", "returning", "returns",
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
    Token *tok = advance_parser(p); /* consume 'function' (was 'to'/'method'/etc) */
    Token *name = expect_type(p, TOK_WORD, "expected function/method name");

    /* Check for method form: "function NAME a TYPE [taking params] ..." */
    if (check_word(p, "a") || check_word(p, "an")) {
        int saved = p->pos;
        advance_parser(p); /* consume a/an */
        if (check_type(p, TOK_WORD)) {
            Token *type_tok = advance_parser(p);
            /* Optional extra params */
            char **params = NULL;
            int param_count = 0;
            if (check_word(p, "taking") || check_word(p, "with") ||
                check_word(p, "and")) {
                advance_parser(p);
                parse_param_list(p, &params, &param_count);
            }
            skip_separators(p);

            ASTNode *node = ast_method_def(type_tok->value, name->value,
                                           params, param_count,
                                           tok->line, tok->col);
            parse_block(p, &node->as.method_def.body);
            expect_block_end(p, "method definition");
            return node;
        }
        /* Not a method — restore */
        p->pos = saved;
    }

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

    /* "display every ITEM in COLLECTION" → for-in loop printing each */
    if (tok->type == TOK_WORD && is_print_keyword(tok->value) &&
        p->pos + 3 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "every") == 0) {
        Token *t = advance_parser(p); /* consume display */
        advance_parser(p); /* consume "every" */
        Token *var_tok = expect_type(p, TOK_WORD, "expected variable name after 'every'");
        expect_word(p, "in", "expected 'in' after 'every ITEM'");
        ASTNode *collection = parse_expression(p);

        /* Build: for var in collection do, display var. end */
        ASTNode *var_ref = ast_identifier(var_tok->value, var_tok->line, var_tok->col);
        ASTNode *print_node = ast_print(var_ref, t->line, t->col);

        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_FOR_IN_STMT;
        node->line = t->line;
        node->col = t->col;
        node->as.for_in.var_name = str_dup_parser(var_tok->value);
        node->as.for_in.collection = collection;
        node_list_init(&node->as.for_in.body);
        node_list_push(&node->as.for_in.body, print_node);
        return node;
    }

    /* Import: "use STRING" / "import STRING" / "load STRING"
     * Also: "use X from STRING" — for now, just imports the whole file */
    if (check_word(p, "use") || check_word(p, "import") || check_word(p, "load")) {
        Token *t = advance_parser(p); /* consume use/import/load */
        /* Check for "X [and Y] from STRING" — skip the names, import whole file */
        if (check_type(p, TOK_WORD) && !check_type(p, TOK_STRING)) {
            /* Could be "use X and Y from FILE" or just "use identifier" */
            /* Scan forward to see if there's a "from" */
            int saved = p->pos;
            while (check_type(p, TOK_WORD) || check_type(p, TOK_COMMA)) {
                advance_parser(p);
                if (check_word(p, "and")) advance_parser(p);
            }
            if (check_word(p, "from")) {
                advance_parser(p); /* consume "from" */
            } else {
                p->pos = saved;
            }
        }
        /* Now expect a string path */
        Token *path = expect_type(p, TOK_STRING, "expected module path string after use/import");
        return ast_import(path->value, t->line, t->col);
    }
    /* "bring in [X from] STRING" */
    if (check_word(p, "bring") && p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        strcmp(p->tokens[p->pos + 1].value, "in") == 0) {
        Token *t = advance_parser(p); advance_parser(p); /* consume "bring in" */
        /* Skip optional "X [and Y] from" */
        if (check_type(p, TOK_WORD) && !check_type(p, TOK_STRING)) {
            int saved = p->pos;
            while (check_type(p, TOK_WORD) || check_type(p, TOK_COMMA)) {
                advance_parser(p);
                if (check_word(p, "and")) advance_parser(p);
            }
            if (check_word(p, "from")) {
                advance_parser(p);
            } else {
                p->pos = saved;
            }
        }
        Token *path = expect_type(p, TOK_STRING, "expected module path after 'bring in'");
        return ast_import(path->value, t->line, t->col);
    }

    /* Struct declarations and extensions:
     *   "a TYPE has FIELD, FIELD, and FIELD"           — basic declaration
     *   "a TYPE also has FIELD, FIELD"                 — extend existing type
     *   "a TYPE is a kind of PARENT"                   — inheritance
     *   "a TYPE is a kind of PARENT and has FIELD..."  — inheritance + fields
     */
    if ((check_word(p, "a") || check_word(p, "an")) && p->pos + 2 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        p->tokens[p->pos + 2].type == TOK_WORD) {
        const char *after_type = p->tokens[p->pos + 2].value;
        int is_has = strcmp(after_type, "has") == 0;
        int is_is = strcmp(after_type, "is") == 0;
        int is_also = strcmp(after_type, "also") == 0;
        if (!is_has && !is_is && !is_also) {
            /* Not a struct declaration — fall through */
        } else {
            Token *a_tok = advance_parser(p); /* consume a/an */
            Token *type_tok = advance_parser(p); /* type name */

            char *parent = NULL;
            int additive = 0;

            if (is_also) {
                /* "a X also has FIELD..." */
                advance_parser(p); /* consume "also" */
                if (check_word(p, "has")) {
                    advance_parser(p);
                    additive = 1;
                } else {
                    esu_error_at_token(current(p), "expected 'has' after 'also'");
                }
            } else if (is_is) {
                /* "a X is a kind of PARENT [and has FIELD...]" */
                advance_parser(p); /* consume "is" */
                /* Expect "a kind of" or "a type of" */
                if (check_word(p, "a") || check_word(p, "an")) advance_parser(p);
                if (check_word(p, "kind") || check_word(p, "type") ||
                    check_word(p, "subclass")) {
                    advance_parser(p);
                }
                if (check_word(p, "of")) advance_parser(p);
                /* Now expect parent type name */
                if (!check_type(p, TOK_WORD)) {
                    esu_error_at_token(current(p), "expected parent type name");
                }
                Token *parent_tok = advance_parser(p);
                parent = str_dup_parser(parent_tok->value);
                /* Optional "and has FIELD..." */
                if (check_word(p, "and")) {
                    advance_parser(p);
                    if (check_word(p, "has")) advance_parser(p);
                } else if (check_word(p, "with")) {
                    advance_parser(p);
                } else if (check_word(p, "has")) {
                    advance_parser(p);
                }
            } else if (is_has) {
                advance_parser(p); /* consume "has" */
            }

            int cap = 4;
            char **fields = malloc(sizeof(char *) * cap);
            int count = 0;

            /* Parse field list (may be empty for pure inheritance) */
            while (check_type(p, TOK_WORD) && !is_separator(p) &&
                   !check_type(p, TOK_DOT) && !check_type(p, TOK_EOF)) {
                /* Skip optional article */
                if (check_word(p, "a") || check_word(p, "an")) {
                    advance_parser(p);
                    if (!check_type(p, TOK_WORD)) break;
                }
                /* If we hit a reserved word, stop */
                if (check_word(p, "and") || check_word(p, "has") ||
                    check_word(p, "is")) break;
                Token *fname = advance_parser(p);
                if (count >= cap) { cap *= 2; fields = realloc(fields, sizeof(char *) * cap); }
                fields[count++] = str_dup_parser(fname->value);

                if (check_word(p, "and") || check_type(p, TOK_COMMA)) {
                    int was_comma = check_type(p, TOK_COMMA);
                    advance_parser(p);
                    if (was_comma && check_word(p, "and")) advance_parser(p);
                } else {
                    break;
                }
            }

            ASTNode *node = ast_struct_decl(type_tok->value, fields, count,
                                            a_tok->line, a_tok->col);
            node->as.struct_decl.parent_type = parent;
            node->as.struct_decl.additive = additive;
            return node;
        }
    }

    /* Print statement */
    if (tok->type == TOK_WORD && is_print_keyword(tok->value)) {
        return parse_print_stmt(p);
    }

    /* "add EXPR to EXPR" — list append (as statement) */
    if (check_word(p, "add") && !looks_like_prefix_verb(p)) {
        /* Peek: is this "add X to Y" (list append) or just an identifier? */
        /* We detect by looking for "to" after the expression */
        int saved = p->pos;
        advance_parser(p); /* consume "add" */
        /* Try to parse expression, then check for "to" */
        ASTNode *item = parse_comparison(p);
        if (check_word(p, "to")) {
            advance_parser(p); /* consume "to" */
            ASTNode *list = parse_postfix(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = list; args[1] = item;
            return make_builtin_call("__list_append", args, 2, tok->line, tok->col);
        }
        /* Not "add X to Y" — restore and fall through */
        /* Can't easily restore, so just treat as expression */
        /* This is "add" used as a function name, which the expression parser handles */
        p->pos = saved; /* restore */
    }

    /* "append EXPR to [file] EXPR" */
    if (check_word(p, "append")) {
        Token *t = advance_parser(p);
        ASTNode *item = parse_comparison(p);
        expect_word(p, "to", "expected 'to' in 'append X to Y'");
        /* If followed by "file", use file append; otherwise list append */
        if (check_word(p, "file")) {
            advance_parser(p); /* consume "file" */
            ASTNode *path = parse_file_path(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = path; args[1] = item;
            return make_builtin_call("__file_append", args, 2, t->line, t->col);
        }
        ASTNode *list = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = list; args[1] = item;
        return make_builtin_call("__list_append", args, 2, t->line, t->col);
    }

    /* "remove item N from LIST" / "remove EXPR from LIST" */
    if (check_word(p, "remove")) {
        Token *t = advance_parser(p);
        if (check_word(p, "item")) advance_parser(p); /* optional "item" */
        ASTNode *idx = parse_comparison(p);
        expect_word(p, "from", "expected 'from' in 'remove X from Y'");
        ASTNode *list = parse_postfix(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = list; args[1] = idx;
        return make_builtin_call("__list_remove", args, 2, t->line, t->col);
    }

    /* --- File operations (statement-level actions) --- */
    /* "write EXPR to [the] [file] EXPR [at/in DIR]" */
    if (check_word(p, "write")) {
        Token *t = advance_parser(p);
        ASTNode *content = parse_comparison(p);
        expect_word(p, "to", "expected 'to' in 'write X to Y'");
        if (check_word(p, "the")) advance_parser(p);
        if (check_word(p, "file")) advance_parser(p);
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        ASTNode *path = parse_file_path(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = path; args[1] = content;
        return make_builtin_call("__file_write", args, 2, t->line, t->col);
    }

    /* "save EXPR to [the] [file] EXPR [at/in DIR]" (alias for write) */
    if (check_word(p, "save") && p->pos + 1 < p->count) {
        Token *t = advance_parser(p);
        ASTNode *content = parse_comparison(p);
        expect_word(p, "to", "expected 'to' in 'save X to Y'");
        if (check_word(p, "the")) advance_parser(p);
        if (check_word(p, "file")) advance_parser(p);
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        ASTNode *path = parse_file_path(p);
        ASTNode **args = malloc(sizeof(ASTNode *) * 2);
        args[0] = path; args[1] = content;
        return make_builtin_call("__file_write", args, 2, t->line, t->col);
    }

    /* "delete [the] file [named] EXPR [at/in DIR]" / "remove [the] file [named] EXPR" */
    if ((check_word(p, "delete") || check_two_words(p, "remove", "file")) &&
        p->pos + 1 < p->count) {
        Token *t = advance_parser(p);
        if (check_word(p, "the")) advance_parser(p);
        if (check_word(p, "file")) advance_parser(p);
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        ASTNode *path = parse_file_path(p);
        ASTNode **args = malloc(sizeof(ASTNode *));
        args[0] = path;
        return make_builtin_call("__file_delete", args, 1, t->line, t->col);
    }

    /* "create [a] [new] file [named] EXPR" / "make [a] [new] file [named] EXPR"
     * / "create [a] [new] file EXPR" — creates an empty file.
     * Also handles "touch file EXPR" for idempotent create. */
    if ((check_word(p, "create") || check_word(p, "make") || check_word(p, "touch")) &&
        p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p); /* consume create/make/touch */
        /* optional "a" / "an" / "the" */
        if (check_word(p, "a") || check_word(p, "an") || check_word(p, "the")) advance_parser(p);
        /* optional "new" */
        if (check_word(p, "new")) advance_parser(p);
        /* Require "file" keyword for disambiguation */
        if (!check_word(p, "file")) {
            /* Not a file creation — restore and fall through */
            p->pos = saved;
        } else {
            advance_parser(p); /* consume "file" */
            /* optional "named" / "called" */
            if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
            ASTNode *path = parse_file_path(p);
            ASTNode **args = malloc(sizeof(ASTNode *));
            args[0] = path;
            /* "touch" uses __file_touch (no error if exists); create/make uses __file_create */
            const char *fn = (strcmp(t->value, "touch") == 0) ?
                             "__file_touch" : "__file_create";
            return make_builtin_call(fn, args, 1, t->line, t->col);
        }
    }

    /* "move [the] file [named] EXPR [at/in DIR] to [the] [file] Y [at/in DIR]"
     * Also supports "move file from X to Y" form */
    if ((check_word(p, "move") || check_word(p, "rename")) && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p); /* consume move/rename */
        if (check_word(p, "the")) advance_parser(p);
        if (check_word(p, "file")) advance_parser(p);
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        ASTNode *from = parse_file_path(p);
        /* Support "move file from X to Y" — if we see "from", discard and reparse */
        if (check_word(p, "from")) {
            ast_free(from);
            advance_parser(p); /* consume "from" */
            from = parse_file_path(p);
        }
        if (!check_word(p, "to")) {
            ast_free(from);
            p->pos = saved;
        } else {
            advance_parser(p); /* consume "to" */
            if (check_word(p, "the")) advance_parser(p);
            if (check_word(p, "file")) advance_parser(p);
            if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
            ASTNode *to = parse_file_path(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = from; args[1] = to;
            return make_builtin_call("__file_move", args, 2, t->line, t->col);
        }
    }

    /* "copy [the] file [named] X [at/in DIR] to Y [at/in DIR]" */
    if (check_word(p, "copy") && p->pos + 1 < p->count) {
        int saved = p->pos;
        Token *t = advance_parser(p);
        if (check_word(p, "the")) advance_parser(p);
        if (check_word(p, "file")) advance_parser(p);
        if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
        ASTNode *from = parse_file_path(p);
        if (check_word(p, "from")) {
            ast_free(from);
            advance_parser(p);
            from = parse_file_path(p);
        }
        if (!check_word(p, "to")) {
            ast_free(from);
            p->pos = saved;
        } else {
            advance_parser(p); /* consume "to" */
            if (check_word(p, "the")) advance_parser(p);
            if (check_word(p, "file")) advance_parser(p);
            if (check_word(p, "named") || check_word(p, "called")) advance_parser(p);
            ASTNode *to = parse_file_path(p);
            ASTNode **args = malloc(sizeof(ASTNode *) * 2);
            args[0] = from; args[1] = to;
            return make_builtin_call("__file_copy", args, 2, t->line, t->col);
        }
    }

    /* Try/catch: try ... catch ERROR ... end */
    if (check_word(p, "try")) {
        Token *t = advance_parser(p);
        skip_separators(p);
        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_TRY_STMT;
        node->line = t->line;
        node->col = t->col;
        parse_block(p, &node->as.try_stmt.body);
        expect_word(p, "catch", "expected 'catch' after try block");
        /* Optional error variable name */
        if (check_type(p, TOK_WORD) && !check_word(p, "end") &&
            !is_separator(p) && !check_type(p, TOK_DOT)) {
            Token *err_var = advance_parser(p);
            node->as.try_stmt.error_var = str_dup_parser(err_var->value);
        } else {
            node->as.try_stmt.error_var = str_dup_parser("error");
        }
        skip_separators(p);
        parse_block(p, &node->as.try_stmt.catch_body);
        expect_block_end(p, "try/catch");
        return node;
    }

    /* Raise/throw: raise EXPR */
    if (check_word(p, "raise") || check_word(p, "throw")) {
        Token *t = advance_parser(p);
        /* optional "error" noise word */
        if (check_word(p, "error")) advance_parser(p);
        ASTNode *value = parse_expression(p);
        ASTNode *node = calloc(1, sizeof(ASTNode));
        node->type = NODE_RAISE_STMT;
        node->line = t->line;
        node->col = t->col;
        node->as.raise_stmt.value = value;
        return node;
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

    /* Field/index assignment: "X.field is V" or "X[key] is V"
     * We detect by parsing an expression and checking for "is" after. */
    if (tok->type == TOK_WORD) {
        /* Peek: is this IDENT.IDENT is ... ? or IDENT[...] is ...? */
        int saved = p->pos;
        if (p->pos + 1 < p->count &&
            (p->tokens[p->pos + 1].type == TOK_DOT ||
             p->tokens[p->pos + 1].type == TOK_LBRACKET)) {
            /* Try parsing an lvalue expression, then check for "is" */
            ASTNode *lhs = parse_postfix(p);
            if (check_word(p, "is") &&
                (lhs->type == NODE_MEMBER_EXPR || lhs->type == NODE_INDEX_EXPR)) {
                advance_parser(p); /* consume "is" */
                ASTNode *value = parse_expression(p);
                ASTNode *obj, *key;
                if (lhs->type == NODE_MEMBER_EXPR) {
                    obj = lhs->as.member_expr.object;
                    key = ast_string_lit(lhs->as.member_expr.field,
                                         lhs->line, lhs->col);
                    /* Prevent double-free: detach object from lhs */
                    lhs->as.member_expr.object = NULL;
                    free(lhs->as.member_expr.field);
                    lhs->as.member_expr.field = NULL;
                } else {
                    obj = lhs->as.index_expr.object;
                    key = lhs->as.index_expr.index;
                    lhs->as.index_expr.object = NULL;
                    lhs->as.index_expr.index = NULL;
                }
                ast_free(lhs);
                return ast_field_assign(obj, key, value, tok->line, tok->col);
            }
            /* Not a field assignment — backtrack */
            ast_free(lhs);
            p->pos = saved;
        }
    }

    /* Assignment: IDENT is EXPR */
    if (tok->type == TOK_WORD) {
        if (p->pos + 1 < p->count &&
            p->tokens[p->pos + 1].type == TOK_WORD &&
            strcmp(p->tokens[p->pos + 1].value, "is") == 0) {
            return parse_assignment(p);
        }
    }

    /* Bare method call: "IDENT METHOD_NAME [args]"
     * where METHOD_NAME is a known method name from the pre-scan.
     * Args are separated by "and", ",", or natural prepositions like "to". */
    if (tok->type == TOK_WORD &&
        p->pos + 1 < p->count &&
        p->tokens[p->pos + 1].type == TOK_WORD &&
        is_known_method(p, p->tokens[p->pos + 1].value)) {
        Token *obj_tok = advance_parser(p);
        Token *method_tok = advance_parser(p);
        ASTNode *obj = ast_identifier(obj_tok->value, obj_tok->line, obj_tok->col);

        /* Parse args until end of line/statement */
        int arg_cap = 4;
        ASTNode **args = malloc(sizeof(ASTNode *) * arg_cap);
        int arg_count = 0;

        /* Check if there's an arg: not newline/comma/period/EOF */
        while (!is_separator(p) && !check_type(p, TOK_EOF) &&
               !check_type(p, TOK_DOT) && !is_block_end(p)) {
            /* Optional preposition noise words: "to", "with", "at" */
            if (check_word(p, "to") || check_word(p, "with") ||
                check_word(p, "at") || check_word(p, "on") ||
                check_word(p, "by") || check_word(p, "for")) {
                advance_parser(p);
                continue;
            }
            if (arg_count >= arg_cap) {
                arg_cap *= 2;
                args = realloc(args, sizeof(ASTNode *) * arg_cap);
            }
            args[arg_count++] = parse_comparison(p);
            if (check_word(p, "and") || check_type(p, TOK_COMMA)) {
                int was_comma = check_type(p, TOK_COMMA);
                advance_parser(p);
                if (was_comma && check_word(p, "and")) advance_parser(p);
            }
        }

        return ast_method_call(obj, method_tok->value, args, arg_count,
                               obj_tok->line, obj_tok->col);
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

        /* After a statement, expect separator, period, EOF, or another word
         * (lenient: allows "alice greet" on one line — the resolver
         * can merge adjacent identifiers into method calls later) */
        if (!check_type(p, TOK_EOF) && !is_separator(p) &&
            !check_type(p, TOK_DOT) && !check_type(p, TOK_WORD)) {
            esu_error_at_token(current(p), "expected end of line or ',' after statement");
        }
        /* Consume period at top level (not inside a block) */
        if (check_type(p, TOK_DOT)) advance_parser(p);
        skip_separators(p);
    }

    return program;
}
