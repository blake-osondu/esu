#ifndef AST_H
#define AST_H

typedef enum {
    NODE_PROGRAM,
    NODE_PRINT,
    NODE_STRING_LIT,
    NODE_NUMBER_LIT,
    NODE_BOOL_LIT,
    NODE_NOTHING_LIT,
    NODE_IDENTIFIER,
    NODE_ASSIGNMENT,
    NODE_BINARY_EXPR,
    NODE_UNARY_EXPR,
    NODE_IF_STMT,
    NODE_WHILE_STMT,
    NODE_FOR_RANGE_STMT,
    NODE_FOR_IN_STMT,
    NODE_LOOP_STMT,
    NODE_BREAK_STMT,
    NODE_CONTINUE_STMT,
    NODE_FUNCTION_DEF,
    NODE_FUNCTION_CALL,
    NODE_RETURN_STMT,
} NodeType;

typedef struct ASTNode ASTNode;

/* A dynamically-sized list of AST nodes */
typedef struct {
    ASTNode **items;
    int count;
    int capacity;
} NodeList;

struct ASTNode {
    NodeType type;
    int line;
    int col;

    union {
        /* NODE_PROGRAM */
        struct { NodeList stmts; } program;

        /* NODE_PRINT */
        struct { ASTNode *expr; } print_stmt;

        /* NODE_STRING_LIT */
        struct { char *value; } string_lit;

        /* NODE_NUMBER_LIT */
        struct { double value; } number_lit;

        /* NODE_BOOL_LIT */
        struct { int value; } bool_lit;

        /* NODE_IDENTIFIER */
        struct { char *name; } identifier;

        /* NODE_ASSIGNMENT */
        struct { char *name; ASTNode *value; } assignment;

        /* NODE_BINARY_EXPR */
        struct { ASTNode *left; char *op; ASTNode *right; } binary;

        /* NODE_UNARY_EXPR */
        struct { char *op; ASTNode *operand; } unary;

        /* NODE_IF_STMT */
        struct {
            ASTNode *condition;
            NodeList body;
            NodeList else_body;     /* empty if no else */
            ASTNode *else_if;       /* chained else-if, or NULL */
        } if_stmt;

        /* NODE_WHILE_STMT */
        struct { ASTNode *condition; NodeList body; } while_stmt;

        /* NODE_FOR_RANGE_STMT: for i from start to end */
        struct {
            char *var_name;
            ASTNode *start;
            ASTNode *end;
            NodeList body;
        } for_range;

        /* NODE_FOR_IN_STMT: for item in collection */
        struct {
            char *var_name;
            ASTNode *collection;
            NodeList body;
        } for_in;

        /* NODE_LOOP_STMT */
        struct { NodeList body; } loop_stmt;

        /* NODE_FUNCTION_DEF */
        struct {
            char *name;
            char **params;
            int param_count;
            NodeList body;
        } func_def;

        /* NODE_FUNCTION_CALL */
        struct {
            char *name;
            ASTNode **args;
            int arg_count;
        } func_call;

        /* NODE_RETURN_STMT */
        struct { ASTNode *value; /* NULL for bare return */ } return_stmt;
    } as;
};

/* Constructors */
ASTNode *ast_program(void);
ASTNode *ast_print(ASTNode *expr, int line, int col);
ASTNode *ast_string_lit(const char *value, int line, int col);
ASTNode *ast_number_lit(double value, int line, int col);
ASTNode *ast_bool_lit(int value, int line, int col);
ASTNode *ast_nothing_lit(int line, int col);
ASTNode *ast_identifier(const char *name, int line, int col);
ASTNode *ast_assignment(const char *name, ASTNode *value, int line, int col);
ASTNode *ast_binary(ASTNode *left, const char *op, ASTNode *right, int line, int col);
ASTNode *ast_unary(const char *op, ASTNode *operand, int line, int col);

/* NodeList helpers */
void node_list_init(NodeList *list);
void node_list_push(NodeList *list, ASTNode *node);

/* Cleanup */
void ast_free(ASTNode *node);

#endif
