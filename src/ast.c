#include "ast.h"
#include <stdlib.h>
#include <string.h>

static ASTNode *ast_new(NodeType type, int line, int col) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = type;
    node->line = line;
    node->col = col;
    return node;
}

static char *str_dup(const char *s) {
    char *d = malloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

void node_list_init(NodeList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void node_list_push(NodeList *list, ASTNode *node) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = realloc(list->items, sizeof(ASTNode *) * list->capacity);
    }
    list->items[list->count++] = node;
}

ASTNode *ast_program(void) {
    ASTNode *node = ast_new(NODE_PROGRAM, 0, 0);
    node_list_init(&node->as.program.stmts);
    return node;
}

ASTNode *ast_print(ASTNode *expr, int line, int col) {
    ASTNode *node = ast_new(NODE_PRINT, line, col);
    node->as.print_stmt.expr = expr;
    return node;
}

ASTNode *ast_string_lit(const char *value, int line, int col) {
    ASTNode *node = ast_new(NODE_STRING_LIT, line, col);
    node->as.string_lit.value = str_dup(value);
    return node;
}

ASTNode *ast_number_lit(double value, int line, int col) {
    ASTNode *node = ast_new(NODE_NUMBER_LIT, line, col);
    node->as.number_lit.value = value;
    return node;
}

ASTNode *ast_bool_lit(int value, int line, int col) {
    ASTNode *node = ast_new(NODE_BOOL_LIT, line, col);
    node->as.bool_lit.value = value;
    return node;
}

ASTNode *ast_nothing_lit(int line, int col) {
    return ast_new(NODE_NOTHING_LIT, line, col);
}

ASTNode *ast_identifier(const char *name, int line, int col) {
    ASTNode *node = ast_new(NODE_IDENTIFIER, line, col);
    node->as.identifier.name = str_dup(name);
    return node;
}

ASTNode *ast_assignment(const char *name, ASTNode *value, int line, int col) {
    ASTNode *node = ast_new(NODE_ASSIGNMENT, line, col);
    node->as.assignment.name = str_dup(name);
    node->as.assignment.value = value;
    return node;
}

ASTNode *ast_binary(ASTNode *left, const char *op, ASTNode *right, int line, int col) {
    ASTNode *node = ast_new(NODE_BINARY_EXPR, line, col);
    node->as.binary.left = left;
    node->as.binary.op = str_dup(op);
    node->as.binary.right = right;
    return node;
}

ASTNode *ast_unary(const char *op, ASTNode *operand, int line, int col) {
    ASTNode *node = ast_new(NODE_UNARY_EXPR, line, col);
    node->as.unary.op = str_dup(op);
    node->as.unary.operand = operand;
    return node;
}

ASTNode *ast_function_def(const char *name, char **params, int param_count, int line, int col) {
    ASTNode *node = ast_new(NODE_FUNCTION_DEF, line, col);
    node->as.func_def.name = str_dup(name);
    node->as.func_def.params = params;
    node->as.func_def.param_count = param_count;
    node_list_init(&node->as.func_def.body);
    return node;
}

ASTNode *ast_function_call(const char *name, ASTNode **args, int arg_count, int line, int col) {
    ASTNode *node = ast_new(NODE_FUNCTION_CALL, line, col);
    node->as.func_call.name = str_dup(name);
    node->as.func_call.args = args;
    node->as.func_call.arg_count = arg_count;
    return node;
}

ASTNode *ast_return(ASTNode *value, int line, int col) {
    ASTNode *node = ast_new(NODE_RETURN_STMT, line, col);
    node->as.return_stmt.value = value;
    return node;
}

ASTNode *ast_list_lit(ASTNode **items, int count, int line, int col) {
    ASTNode *node = ast_new(NODE_LIST_LIT, line, col);
    node->as.list_lit.items = items;
    node->as.list_lit.count = count;
    return node;
}

ASTNode *ast_map_lit(char **keys, ASTNode **values, int count, int line, int col) {
    ASTNode *node = ast_new(NODE_MAP_LIT, line, col);
    node->as.map_lit.keys = keys;
    node->as.map_lit.values = values;
    node->as.map_lit.count = count;
    return node;
}

ASTNode *ast_index_expr(ASTNode *object, ASTNode *index, int line, int col) {
    ASTNode *node = ast_new(NODE_INDEX_EXPR, line, col);
    node->as.index_expr.object = object;
    node->as.index_expr.index = index;
    return node;
}

void ast_free(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->as.program.stmts.count; i++)
                ast_free(node->as.program.stmts.items[i]);
            free(node->as.program.stmts.items);
            break;
        case NODE_PRINT:
            ast_free(node->as.print_stmt.expr);
            break;
        case NODE_STRING_LIT:
            free(node->as.string_lit.value);
            break;
        case NODE_NUMBER_LIT:
        case NODE_BOOL_LIT:
        case NODE_NOTHING_LIT:
        case NODE_BREAK_STMT:
        case NODE_CONTINUE_STMT:
            break;
        case NODE_IDENTIFIER:
            free(node->as.identifier.name);
            break;
        case NODE_ASSIGNMENT:
            free(node->as.assignment.name);
            ast_free(node->as.assignment.value);
            break;
        case NODE_BINARY_EXPR:
            ast_free(node->as.binary.left);
            free(node->as.binary.op);
            ast_free(node->as.binary.right);
            break;
        case NODE_UNARY_EXPR:
            free(node->as.unary.op);
            ast_free(node->as.unary.operand);
            break;
        case NODE_IF_STMT:
            ast_free(node->as.if_stmt.condition);
            for (int i = 0; i < node->as.if_stmt.body.count; i++)
                ast_free(node->as.if_stmt.body.items[i]);
            free(node->as.if_stmt.body.items);
            for (int i = 0; i < node->as.if_stmt.else_body.count; i++)
                ast_free(node->as.if_stmt.else_body.items[i]);
            free(node->as.if_stmt.else_body.items);
            ast_free(node->as.if_stmt.else_if);
            break;
        case NODE_WHILE_STMT:
            ast_free(node->as.while_stmt.condition);
            for (int i = 0; i < node->as.while_stmt.body.count; i++)
                ast_free(node->as.while_stmt.body.items[i]);
            free(node->as.while_stmt.body.items);
            break;
        case NODE_FOR_RANGE_STMT:
            free(node->as.for_range.var_name);
            ast_free(node->as.for_range.start);
            ast_free(node->as.for_range.end);
            for (int i = 0; i < node->as.for_range.body.count; i++)
                ast_free(node->as.for_range.body.items[i]);
            free(node->as.for_range.body.items);
            break;
        case NODE_FOR_IN_STMT:
            free(node->as.for_in.var_name);
            ast_free(node->as.for_in.collection);
            for (int i = 0; i < node->as.for_in.body.count; i++)
                ast_free(node->as.for_in.body.items[i]);
            free(node->as.for_in.body.items);
            break;
        case NODE_LOOP_STMT:
            for (int i = 0; i < node->as.loop_stmt.body.count; i++)
                ast_free(node->as.loop_stmt.body.items[i]);
            free(node->as.loop_stmt.body.items);
            break;
        case NODE_FUNCTION_DEF:
            free(node->as.func_def.name);
            for (int i = 0; i < node->as.func_def.param_count; i++)
                free(node->as.func_def.params[i]);
            free(node->as.func_def.params);
            for (int i = 0; i < node->as.func_def.body.count; i++)
                ast_free(node->as.func_def.body.items[i]);
            free(node->as.func_def.body.items);
            break;
        case NODE_FUNCTION_CALL:
            free(node->as.func_call.name);
            for (int i = 0; i < node->as.func_call.arg_count; i++)
                ast_free(node->as.func_call.args[i]);
            free(node->as.func_call.args);
            break;
        case NODE_RETURN_STMT:
            ast_free(node->as.return_stmt.value);
            break;
        case NODE_LIST_LIT:
            for (int i = 0; i < node->as.list_lit.count; i++)
                ast_free(node->as.list_lit.items[i]);
            free(node->as.list_lit.items);
            break;
        case NODE_MAP_LIT:
            for (int i = 0; i < node->as.map_lit.count; i++) {
                free(node->as.map_lit.keys[i]);
                ast_free(node->as.map_lit.values[i]);
            }
            free(node->as.map_lit.keys);
            free(node->as.map_lit.values);
            break;
        case NODE_INDEX_EXPR:
            ast_free(node->as.index_expr.object);
            ast_free(node->as.index_expr.index);
            break;
    }

    free(node);
}
