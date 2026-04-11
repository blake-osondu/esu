#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void lexer_init(Lexer *lex, const char *source) {
    lex->source = source;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->queue_count = 0;
    lex->queue_read = 0;
}

static void lexer_enqueue(Lexer *lex, Token tok) {
    if (lex->queue_count < LEXER_QUEUE_MAX) {
        lex->queue[lex->queue_count++] = tok;
    }
}

static char peek(Lexer *lex) {
    return lex->source[lex->pos];
}

static char peek_next(Lexer *lex) {
    if (lex->source[lex->pos] == '\0') return '\0';
    return lex->source[lex->pos + 1];
}

static char advance(Lexer *lex) {
    char c = lex->source[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static Token make_token(TokenType type, const char *value, int line, int col) {
    Token tok;
    tok.type = type;
    tok.value = malloc(strlen(value) + 1);
    strcpy(tok.value, value);
    tok.line = line;
    tok.col = col;
    return tok;
}

static Token make_token_char(TokenType type, char c, int line, int col) {
    char buf[2] = { c, '\0' };
    return make_token(type, buf, line, col);
}

/* Forward declaration for use in string interpolation */
Token lexer_next(Lexer *lex);

static void skip_whitespace(Lexer *lex) {
    while (peek(lex) == ' ' || peek(lex) == '\t' || peek(lex) == '\r') {
        advance(lex);
    }
}

static void skip_comment(Lexer *lex) {
    /* -- comment until end of line */
    while (peek(lex) != '\0' && peek(lex) != '\n') {
        advance(lex);
    }
}

static Token lex_word(Lexer *lex) {
    int start = lex->pos;
    int line = lex->line;
    int col = lex->col;

    while (isalpha(peek(lex)) || peek(lex) == '_' || isdigit(peek(lex)) ||
           (peek(lex) == '-' && isalpha(peek_next(lex)))) {
        advance(lex);
    }

    int len = lex->pos - start;
    char *buf = malloc(len + 1);
    memcpy(buf, lex->source + start, len);
    buf[len] = '\0';

    Token tok = make_token(TOK_WORD, buf, line, col);
    free(buf);
    return tok;
}

static Token lex_number(Lexer *lex) {
    int start = lex->pos;
    int line = lex->line;
    int col = lex->col;

    while (isdigit(peek(lex))) {
        advance(lex);
    }

    /* Check for ordinal suffix: 1st, 2nd, 3rd, 4th, etc. */
    char c1 = peek(lex);
    char c2 = peek_next(lex);
    if ((c1 == 's' && c2 == 't') ||
        (c1 == 'n' && c2 == 'd') ||
        (c1 == 'r' && c2 == 'd') ||
        (c1 == 't' && c2 == 'h')) {
        advance(lex); advance(lex); /* consume suffix */
        int len = lex->pos - start;
        char *buf = malloc(len + 1);
        memcpy(buf, lex->source + start, len);
        buf[len] = '\0';
        Token tok = make_token(TOK_WORD, buf, line, col);
        free(buf);
        return tok;
    }

    if (peek(lex) == '.' && isdigit(peek_next(lex))) {
        advance(lex); /* consume '.' */
        while (isdigit(peek(lex))) {
            advance(lex);
        }
    }

    int len = lex->pos - start;
    char *buf = malloc(len + 1);
    memcpy(buf, lex->source + start, len);
    buf[len] = '\0';

    Token tok = make_token(TOK_NUMBER, buf, line, col);
    free(buf);
    return tok;
}

/*
 * Lex a string literal with interpolation support.
 * "Hello, {name}!" becomes:
 *   ( TOK_STRING("Hello, ") TOK_PLUS IDENT(name) TOK_PLUS TOK_STRING("!") )
 * Wrapped in parens so it's a single expression.
 * Returns the first token; remaining tokens are enqueued.
 */
static Token lex_string(Lexer *lex) {
    int line = lex->line;
    int col = lex->col;

    advance(lex); /* consume opening " */

    /* First pass: scan to see if there's any interpolation */
    int save_pos = lex->pos;
    int has_interp = 0;
    while (lex->source[save_pos] != '"' && lex->source[save_pos] != '\0') {
        if (lex->source[save_pos] == '\\') { save_pos += 2; continue; }
        if (lex->source[save_pos] == '{') { has_interp = 1; break; }
        save_pos++;
    }

    if (!has_interp) {
        /* Simple string — no interpolation */
        int cap = 64;
        char *buf = malloc(cap);
        int len = 0;
        while (peek(lex) != '"' && peek(lex) != '\0') {
            if (peek(lex) == '\\') {
                advance(lex);
                char c = advance(lex);
                char escaped;
                switch (c) {
                    case 'n':  escaped = '\n'; break;
                    case 't':  escaped = '\t'; break;
                    case '\\': escaped = '\\'; break;
                    case '"':  escaped = '"';  break;
                    default:   escaped = c;    break;
                }
                if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = escaped;
            } else {
                if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = advance(lex);
            }
        }
        if (peek(lex) == '"') advance(lex);
        buf[len] = '\0';
        Token tok = make_token(TOK_STRING, buf, line, col);
        free(buf);
        return tok;
    }

    /* Has interpolation — collect all parts into the queue, then
     * return the first token. Parts are joined by +. Wrapped in ( ). */

    /* Temporary storage for all the tokens we'll produce */
    Token parts[LEXER_QUEUE_MAX];
    int part_count = 0;

    /* Helper macro to add a token to parts */
    #define ADD_PART(t) do { if (part_count < LEXER_QUEUE_MAX) parts[part_count++] = (t); } while(0)

    int cap = 64;
    char *buf = malloc(cap);
    int len = 0;

    while (peek(lex) != '"' && peek(lex) != '\0') {
        if (peek(lex) == '\\') {
            advance(lex);
            char c = advance(lex);
            char escaped;
            switch (c) {
                case 'n':  escaped = '\n'; break;
                case 't':  escaped = '\t'; break;
                case '\\': escaped = '\\'; break;
                case '"':  escaped = '"';  break;
                case '{':  escaped = '{';  break;
                default:   escaped = c;    break;
            }
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = escaped;
        } else if (peek(lex) == '{') {
            advance(lex); /* consume '{' */

            /* Emit the string segment so far */
            if (len > 0 || part_count == 0) {
                if (part_count > 0) ADD_PART(make_token_char(TOK_PLUS, '+', line, col));
                buf[len] = '\0';
                ADD_PART(make_token(TOK_STRING, buf, line, col));
            }

            /* Add + before the expression */
            if (part_count > 0) ADD_PART(make_token_char(TOK_PLUS, '+', line, col));

            /* Lex expression tokens inside {}, wrapped in parens for precedence */
            ADD_PART(make_token_char(TOK_LPAREN, '(', line, col));
            while (peek(lex) != '}' && peek(lex) != '\0') {
                skip_whitespace(lex);
                if (peek(lex) == '}') break;
                Token inner = lexer_next(lex);
                if (inner.type == TOK_EOF) break;
                ADD_PART(inner);
            }
            ADD_PART(make_token_char(TOK_RPAREN, ')', line, col));
            if (peek(lex) == '}') advance(lex);

            /* Reset string buffer */
            len = 0;
        } else {
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = advance(lex);
        }
    }

    if (peek(lex) == '"') advance(lex);

    /* Emit trailing string segment if any */
    if (len > 0) {
        if (part_count > 0) ADD_PART(make_token_char(TOK_PLUS, '+', line, col));
        buf[len] = '\0';
        ADD_PART(make_token(TOK_STRING, buf, line, col));
    }
    free(buf);

    #undef ADD_PART

    if (part_count == 0) {
        return make_token(TOK_STRING, "", line, col);
    }

    /* Wrap in parentheses to ensure correct precedence */
    Token open_paren = make_token_char(TOK_LPAREN, '(', line, col);

    /* Enqueue: all parts + close paren */
    for (int i = 0; i < part_count; i++) {
        lexer_enqueue(lex, parts[i]);
    }
    lexer_enqueue(lex, make_token_char(TOK_RPAREN, ')', line, col));

    return open_paren;
}

Token lexer_next(Lexer *lex) {
    /* Return queued tokens first (from string interpolation) */
    if (lex->queue_read < lex->queue_count) {
        return lex->queue[lex->queue_read++];
    }
    /* Reset queue when drained */
    if (lex->queue_count > 0) {
        lex->queue_count = 0;
        lex->queue_read = 0;
    }

    skip_whitespace(lex);

    int line = lex->line;
    int col = lex->col;
    char c = peek(lex);

    if (c == '\0') {
        return make_token(TOK_EOF, "", line, col);
    }

    /* Comments: -- */
    if (c == '-' && peek_next(lex) == '-') {
        skip_comment(lex);
        return lexer_next(lex);
    }

    if (c == '\n') {
        advance(lex);
        return make_token(TOK_NEWLINE, "\\n", line, col);
    }

    if (isalpha(c) || c == '_') {
        return lex_word(lex);
    }

    if (isdigit(c)) {
        return lex_number(lex);
    }

    if (c == '"') {
        return lex_string(lex);
    }

    /* Two-character operators */
    char next = peek_next(lex);
    if (c == '=' && next == '=') { advance(lex); advance(lex); return make_token(TOK_EQ, "==", line, col); }
    if (c == '!' && next == '=') { advance(lex); advance(lex); return make_token(TOK_NEQ, "!=", line, col); }
    if (c == '>' && next == '=') { advance(lex); advance(lex); return make_token(TOK_GTE, ">=", line, col); }
    if (c == '<' && next == '=') { advance(lex); advance(lex); return make_token(TOK_LTE, "<=", line, col); }

    /* Single-character operators */
    advance(lex);
    switch (c) {
        case '+': return make_token_char(TOK_PLUS, c, line, col);
        case '-': return make_token_char(TOK_MINUS, c, line, col);
        case '*': return make_token_char(TOK_STAR, c, line, col);
        case '/': return make_token_char(TOK_SLASH, c, line, col);
        case '%': return make_token_char(TOK_PERCENT, c, line, col);
        case '>': return make_token_char(TOK_GT, c, line, col);
        case '<': return make_token_char(TOK_LT, c, line, col);
        case '(': return make_token_char(TOK_LPAREN, c, line, col);
        case ')': return make_token_char(TOK_RPAREN, c, line, col);
        case '[': return make_token_char(TOK_LBRACKET, c, line, col);
        case ']': return make_token_char(TOK_RBRACKET, c, line, col);
        case '{': return make_token_char(TOK_LBRACE, c, line, col);
        case '}': return make_token_char(TOK_RBRACE, c, line, col);
        case ',': return make_token_char(TOK_COMMA, c, line, col);
        case '.': return make_token_char(TOK_DOT, c, line, col);
        case ':': return make_token_char(TOK_COLON, c, line, col);
        default: {
            fprintf(stderr, "Error: unexpected character '%c' at line %d, col %d\n", c, line, col);
            return lexer_next(lex);
        }
    }
}

void token_free(Token *tok) {
    if (tok->value) {
        free(tok->value);
        tok->value = NULL;
    }
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_WORD:     return "WORD";
        case TOK_NUMBER:   return "NUMBER";
        case TOK_STRING:   return "STRING";
        case TOK_PLUS:     return "PLUS";
        case TOK_MINUS:    return "MINUS";
        case TOK_STAR:     return "STAR";
        case TOK_SLASH:    return "SLASH";
        case TOK_PERCENT:  return "PERCENT";
        case TOK_EQ:       return "EQ";
        case TOK_NEQ:      return "NEQ";
        case TOK_GT:       return "GT";
        case TOK_LT:       return "LT";
        case TOK_GTE:      return "GTE";
        case TOK_LTE:      return "LTE";
        case TOK_LPAREN:   return "LPAREN";
        case TOK_RPAREN:   return "RPAREN";
        case TOK_LBRACKET: return "LBRACKET";
        case TOK_RBRACKET: return "RBRACKET";
        case TOK_LBRACE:   return "LBRACE";
        case TOK_RBRACE:   return "RBRACE";
        case TOK_COMMA:    return "COMMA";
        case TOK_DOT:      return "DOT";
        case TOK_COLON:    return "COLON";
        case TOK_NEWLINE:  return "NEWLINE";
        case TOK_EOF:      return "EOF";
    }
    return "UNKNOWN";
}
