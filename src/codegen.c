#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void codegen_init(CodeGen *cg) {
    cg->output_cap = 4096;
    cg->output = malloc(cg->output_cap);
    cg->output[0] = '\0';
    cg->output_len = 0;
    cg->indent = 0;
    cg->declared_cap = 32;
    cg->declared_vars = malloc(sizeof(char *) * cg->declared_cap);
    cg->declared_count = 0;
}

static int is_declared(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->declared_count; i++) {
        if (strcmp(cg->declared_vars[i], name) == 0) return 1;
    }
    return 0;
}

static void mark_declared(CodeGen *cg, const char *name) {
    if (is_declared(cg, name)) return;
    if (cg->declared_count >= cg->declared_cap) {
        cg->declared_cap *= 2;
        cg->declared_vars = realloc(cg->declared_vars, sizeof(char *) * cg->declared_cap);
    }
    cg->declared_vars[cg->declared_count] = malloc(strlen(name) + 1);
    strcpy(cg->declared_vars[cg->declared_count], name);
    cg->declared_count++;
}

static void emit(CodeGen *cg, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    while (cg->output_len + len + 1 > cg->output_cap) {
        cg->output_cap *= 2;
        cg->output = realloc(cg->output, cg->output_cap);
    }

    memcpy(cg->output + cg->output_len, buf, len);
    cg->output_len += len;
    cg->output[cg->output_len] = '\0';
}

static void emit_indent(CodeGen *cg) {
    for (int i = 0; i < cg->indent; i++) {
        emit(cg, "    ");
    }
}

/* Escape a string for C string literal */
static void emit_c_string(CodeGen *cg, const char *s) {
    emit(cg, "\"");
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '\n': emit(cg, "\\n"); break;
            case '\t': emit(cg, "\\t"); break;
            case '\\': emit(cg, "\\\\"); break;
            case '"':  emit(cg, "\\\""); break;
            default:   emit(cg, "%c", *p); break;
        }
    }
    emit(cg, "\"");
}

/* Forward declaration */
static void gen_expression(CodeGen *cg, ASTNode *node);
static void gen_statement(CodeGen *cg, ASTNode *node);

static void gen_expression(CodeGen *cg, ASTNode *node) {
    switch (node->type) {
        case NODE_NUMBER_LIT:
            emit(cg, "babel_number(%g)", node->as.number_lit.value);
            break;

        case NODE_STRING_LIT:
            emit(cg, "babel_string(");
            emit_c_string(cg, node->as.string_lit.value);
            emit(cg, ")");
            break;

        case NODE_BOOL_LIT:
            emit(cg, "babel_bool(%d)", node->as.bool_lit.value);
            break;

        case NODE_NOTHING_LIT:
            emit(cg, "babel_nothing()");
            break;

        case NODE_IDENTIFIER:
            emit(cg, "var_%s", node->as.identifier.name);
            break;

        case NODE_BINARY_EXPR: {
            const char *op = node->as.binary.op;
            const char *func = NULL;
            if (strcmp(op, "+") == 0) func = "babel_add";
            else if (strcmp(op, "-") == 0) func = "babel_sub";
            else if (strcmp(op, "*") == 0) func = "babel_mul";
            else if (strcmp(op, "/") == 0) func = "babel_div";
            else if (strcmp(op, "%") == 0) func = "babel_mod";
            else if (strcmp(op, "==") == 0) func = "babel_eq";
            else if (strcmp(op, "!=") == 0) func = "babel_neq";
            else if (strcmp(op, ">") == 0) func = "babel_gt";
            else if (strcmp(op, "<") == 0) func = "babel_lt";
            else if (strcmp(op, ">=") == 0) func = "babel_gte";
            else if (strcmp(op, "<=") == 0) func = "babel_lte";
            else if (strcmp(op, "and") == 0) func = "babel_and";
            else if (strcmp(op, "or") == 0) func = "babel_or";

            if (func) {
                emit(cg, "%s(", func);
                gen_expression(cg, node->as.binary.left);
                emit(cg, ", ");
                gen_expression(cg, node->as.binary.right);
                emit(cg, ")");
            }
            break;
        }

        case NODE_UNARY_EXPR: {
            const char *op = node->as.unary.op;
            if (strcmp(op, "-") == 0) {
                emit(cg, "babel_neg(");
                gen_expression(cg, node->as.unary.operand);
                emit(cg, ")");
            } else if (strcmp(op, "not") == 0) {
                emit(cg, "babel_not(");
                gen_expression(cg, node->as.unary.operand);
                emit(cg, ")");
            }
            break;
        }

        default:
            emit(cg, "babel_nothing() /* unhandled expr */");
            break;
    }
}

static void gen_block(CodeGen *cg, NodeList *list) {
    for (int i = 0; i < list->count; i++) {
        gen_statement(cg, list->items[i]);
    }
}

static void gen_statement(CodeGen *cg, ASTNode *node) {
    switch (node->type) {
        case NODE_PRINT:
            emit_indent(cg);
            emit(cg, "babel_print(");
            gen_expression(cg, node->as.print_stmt.expr);
            emit(cg, ");\n");
            break;

        case NODE_ASSIGNMENT:
            emit_indent(cg);
            if (!is_declared(cg, node->as.assignment.name)) {
                emit(cg, "BabelValue *var_%s = ", node->as.assignment.name);
                mark_declared(cg, node->as.assignment.name);
            } else {
                emit(cg, "var_%s = ", node->as.assignment.name);
            }
            gen_expression(cg, node->as.assignment.value);
            emit(cg, ";\n");
            break;

        case NODE_IF_STMT:
            emit_indent(cg);
            emit(cg, "if (babel_is_truthy(");
            gen_expression(cg, node->as.if_stmt.condition);
            emit(cg, ")) {\n");
            cg->indent++;
            gen_block(cg, &node->as.if_stmt.body);
            cg->indent--;
            if (node->as.if_stmt.else_if) {
                /* else if chain */
                emit_indent(cg);
                emit(cg, "} else ");
                /* Generate the nested if without extra indent wrapper */
                emit(cg, "if (babel_is_truthy(");
                gen_expression(cg, node->as.if_stmt.else_if->as.if_stmt.condition);
                emit(cg, ")) {\n");
                cg->indent++;
                gen_block(cg, &node->as.if_stmt.else_if->as.if_stmt.body);
                cg->indent--;
                /* Recurse for further else-if chains or final else */
                ASTNode *chain = node->as.if_stmt.else_if;
                while (chain->as.if_stmt.else_if) {
                    emit_indent(cg);
                    emit(cg, "} else if (babel_is_truthy(");
                    gen_expression(cg, chain->as.if_stmt.else_if->as.if_stmt.condition);
                    emit(cg, ")) {\n");
                    cg->indent++;
                    gen_block(cg, &chain->as.if_stmt.else_if->as.if_stmt.body);
                    cg->indent--;
                    chain = chain->as.if_stmt.else_if;
                }
                if (chain->as.if_stmt.else_body.count > 0) {
                    emit_indent(cg);
                    emit(cg, "} else {\n");
                    cg->indent++;
                    gen_block(cg, &chain->as.if_stmt.else_body);
                    cg->indent--;
                }
                emit_indent(cg);
                emit(cg, "}\n");
            } else if (node->as.if_stmt.else_body.count > 0) {
                emit_indent(cg);
                emit(cg, "} else {\n");
                cg->indent++;
                gen_block(cg, &node->as.if_stmt.else_body);
                cg->indent--;
                emit_indent(cg);
                emit(cg, "}\n");
            } else {
                emit_indent(cg);
                emit(cg, "}\n");
            }
            break;

        case NODE_WHILE_STMT:
            emit_indent(cg);
            emit(cg, "while (babel_is_truthy(");
            gen_expression(cg, node->as.while_stmt.condition);
            emit(cg, ")) {\n");
            cg->indent++;
            gen_block(cg, &node->as.while_stmt.body);
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;

        case NODE_FOR_RANGE_STMT: {
            /* For-range loop: always declares loop var in its own C scope */
            const char *var = node->as.for_range.var_name;
            emit_indent(cg);
            emit(cg, "{\n");
            cg->indent++;
            emit_indent(cg);
            emit(cg, "double _start = (");
            gen_expression(cg, node->as.for_range.start);
            emit(cg, ")->as.number;\n");
            emit_indent(cg);
            emit(cg, "double _end = (");
            gen_expression(cg, node->as.for_range.end);
            emit(cg, ")->as.number;\n");
            emit_indent(cg);
            emit(cg, "for (double _i = _start; _i <= _end; _i++) {\n");
            cg->indent++;
            emit_indent(cg);
            emit(cg, "BabelValue *var_%s = babel_number(_i);\n", var);
            gen_block(cg, &node->as.for_range.body);
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;
        }

        case NODE_FOR_IN_STMT:
            /* Will be fully implemented in Phase 6 (lists) */
            emit_indent(cg);
            emit(cg, "/* TODO: for-in loop */\n");
            break;

        case NODE_LOOP_STMT:
            emit_indent(cg);
            emit(cg, "while (1) {\n");
            cg->indent++;
            gen_block(cg, &node->as.loop_stmt.body);
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;

        case NODE_BREAK_STMT:
            emit_indent(cg);
            emit(cg, "break;\n");
            break;

        case NODE_CONTINUE_STMT:
            emit_indent(cg);
            emit(cg, "continue;\n");
            break;

        default:
            /* Expression statement */
            emit_indent(cg);
            gen_expression(cg, node);
            emit(cg, ";\n");
            break;
    }
}

void codegen_generate(CodeGen *cg, ASTNode *program) {
    /* Preamble */
    emit(cg, "#include \"babel_runtime.h\"\n\n");
    emit(cg, "int main(int argc, char **argv) {\n");
    cg->indent = 1;
    emit_indent(cg);
    emit(cg, "babel_runtime_init();\n\n");

    /* Statements */
    NodeList *stmts = &program->as.program.stmts;
    for (int i = 0; i < stmts->count; i++) {
        gen_statement(cg, stmts->items[i]);
    }

    /* Epilogue */
    emit(cg, "\n");
    emit_indent(cg);
    emit(cg, "babel_runtime_cleanup();\n");
    emit_indent(cg);
    emit(cg, "return 0;\n");
    cg->indent = 0;
    emit(cg, "}\n");
}

char *codegen_get_output(CodeGen *cg) {
    return cg->output;
}

void codegen_free(CodeGen *cg) {
    free(cg->output);
    cg->output = NULL;
    for (int i = 0; i < cg->declared_count; i++) {
        free(cg->declared_vars[i]);
    }
    free(cg->declared_vars);
    cg->declared_vars = NULL;
}
