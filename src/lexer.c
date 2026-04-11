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

static Token lex_string(Lexer *lex) {
    int line = lex->line;
    int col = lex->col;

    advance(lex); /* consume opening " */

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

    if (peek(lex) == '"') {
        advance(lex); /* consume closing " */
    } else {
        fprintf(stderr, "Error: unterminated string at line %d, col %d\n", line, col);
    }

    buf[len] = '\0';
    Token tok = make_token(TOK_STRING, buf, line, col);
    free(buf);
    return tok;
}

Token lexer_next(Lexer *lex) {
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
