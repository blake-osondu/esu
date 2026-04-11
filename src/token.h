#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
    TOK_WORD,       /* any alphabetic identifier/keyword */
    TOK_NUMBER,     /* integer or float literal */
    TOK_STRING,     /* double-quoted string literal */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_SLASH,      /* / */
    TOK_PERCENT,    /* % */
    TOK_EQ,         /* == */
    TOK_NEQ,        /* != */
    TOK_GT,         /* > */
    TOK_LT,         /* < */
    TOK_GTE,        /* >= */
    TOK_LTE,        /* <= */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACKET,   /* [ */
    TOK_RBRACKET,   /* ] */
    TOK_LBRACE,     /* { */
    TOK_RBRACE,     /* } */
    TOK_COMMA,      /* , */
    TOK_DOT,        /* . */
    TOK_COLON,      /* : */
    TOK_NEWLINE,    /* end of line */
    TOK_EOF,        /* end of file */
} TokenType;

typedef struct {
    TokenType type;
    char *value;    /* text content of the token */
    int line;
    int col;
} Token;

const char *token_type_name(TokenType type);

#endif
