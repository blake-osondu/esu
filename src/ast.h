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
    NODE_LIST_LIT,
    NODE_MAP_LIT,
    NODE_INDEX_EXPR,
    NODE_TRY_STMT,
    NODE_RAISE_STMT,
    NODE_STRUCT_DECL,       /* "a person has a name and an age" */
    NODE_METHOD_DEF,        /* "to greet a person ..." */
    NODE_CONSTRUCTOR,       /* "a new person with name ..., age ..." */
    NODE_MEMBER_EXPR,       /* x.field — read-only */
    NODE_METHOD_CALL,       /* x.method(args) */
    NODE_FIELD_ASSIGN,      /* x.field is V  or  x[k] is V */
    NODE_LAMBDA,            /* anonymous function */
    NODE_CALL_VALUE,        /* call a function value (not by name) */
    NODE_IMPORT,            /* use "file" / import X from "file" */
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

        /* NODE_LIST_LIT */
        struct { ASTNode **items; int count; } list_lit;

        /* NODE_MAP_LIT */
        struct { char **keys; ASTNode **values; int count; } map_lit;

        /* NODE_INDEX_EXPR: collection[index] */
        struct { ASTNode *object; ASTNode *index; } index_expr;

        /* NODE_TRY_STMT */
        struct {
            NodeList body;
            char *error_var;    /* variable name for caught error */
            NodeList catch_body;
        } try_stmt;

        /* NODE_RAISE_STMT */
        struct { ASTNode *value; } raise_stmt;

        /* NODE_STRUCT_DECL: "a person has a name and an age"
         *                or "a student is a kind of person"
         *                or "a student also has a school" */
        struct {
            char *type_name;
            char **field_names;
            int field_count;
            char *parent_type;      /* NULL for root types */
            int additive;           /* 1 if "also has" — extends existing */
        } struct_decl;

        /* NODE_METHOD_DEF: "to greet a person ... end" */
        struct {
            char *type_name;        /* "person" */
            char *name;             /* "greet" */
            char **params;          /* extra parameters beyond self */
            int param_count;
            NodeList body;
        } method_def;

        /* NODE_CONSTRUCTOR: "a new person with name X and age Y" */
        struct {
            char *type_name;
            char **field_names;     /* NULL for positional args */
            ASTNode **values;
            int count;
        } constructor;

        /* NODE_MEMBER_EXPR: x.field */
        struct {
            ASTNode *object;
            char *field;
        } member_expr;

        /* NODE_METHOD_CALL: x.method(args) or "have x method" */
        struct {
            ASTNode *object;
            char *method_name;
            ASTNode **args;
            int arg_count;
        } method_call;

        /* NODE_FIELD_ASSIGN: x.field is V  or  x[k] is V */
        struct {
            ASTNode *object;
            ASTNode *key;       /* string expr for .field, any expr for [k] */
            ASTNode *value;
        } field_assign;

        /* NODE_LAMBDA: anonymous function */
        struct {
            char **params;
            int param_count;
            NodeList body;        /* block of statements (if multi) */
            ASTNode *body_expr;   /* single-expression body (if present) */
        } lambda;

        /* NODE_CALL_VALUE: call a function value (not by name) */
        struct {
            ASTNode *callee;
            ASTNode **args;
            int arg_count;
        } call_value;

        /* NODE_IMPORT: use "file" / use X from "file" */
        struct {
            char *module_path;      /* the file path string */
        } import_stmt;
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
ASTNode *ast_function_def(const char *name, char **params, int param_count, int line, int col);
ASTNode *ast_function_call(const char *name, ASTNode **args, int arg_count, int line, int col);
ASTNode *ast_return(ASTNode *value, int line, int col);
ASTNode *ast_list_lit(ASTNode **items, int count, int line, int col);
ASTNode *ast_map_lit(char **keys, ASTNode **values, int count, int line, int col);
ASTNode *ast_index_expr(ASTNode *object, ASTNode *index, int line, int col);
ASTNode *ast_struct_decl(const char *type_name, char **field_names, int field_count, int line, int col);
ASTNode *ast_method_def(const char *type_name, const char *name, char **params, int param_count, int line, int col);
ASTNode *ast_constructor(const char *type_name, char **field_names, ASTNode **values, int count, int line, int col);
ASTNode *ast_member_expr(ASTNode *object, const char *field, int line, int col);
ASTNode *ast_method_call(ASTNode *object, const char *method_name, ASTNode **args, int arg_count, int line, int col);
ASTNode *ast_field_assign(ASTNode *object, ASTNode *key, ASTNode *value, int line, int col);
ASTNode *ast_lambda(char **params, int param_count, ASTNode *body_expr, int line, int col);
ASTNode *ast_call_value(ASTNode *callee, ASTNode **args, int arg_count, int line, int col);
ASTNode *ast_import(const char *module_path, int line, int col);

/* NodeList helpers */
void node_list_init(NodeList *list);
void node_list_push(NodeList *list, ASTNode *node);

/* Cleanup */
void ast_free(ASTNode *node);

#endif
