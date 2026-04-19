#include "codegen.h"
#include "module.h"
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
    cg->function_names_cap = 16;
    cg->function_names = malloc(sizeof(char *) * cg->function_names_cap);
    cg->function_names_count = 0;
    cg->lambda_counter = 0;
    cg->hoisted_cap = 4096;
    cg->hoisted = malloc(cg->hoisted_cap);
    cg->hoisted[0] = '\0';
    cg->hoisted_len = 0;
}

static void register_function_name(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->function_names_count; i++) {
        if (strcmp(cg->function_names[i], name) == 0) return;
    }
    if (cg->function_names_count >= cg->function_names_cap) {
        cg->function_names_cap *= 2;
        cg->function_names = realloc(cg->function_names,
                                     sizeof(char *) * cg->function_names_cap);
    }
    cg->function_names[cg->function_names_count] = malloc(strlen(name) + 1);
    strcpy(cg->function_names[cg->function_names_count], name);
    cg->function_names_count++;
}

static int is_function_name(CodeGen *cg, const char *name) {
    for (int i = 0; i < cg->function_names_count; i++) {
        if (strcmp(cg->function_names[i], name) == 0) return 1;
    }
    return 0;
}

/* Append to hoisted buffer (for lambda function definitions) */
static void emit_hoisted(CodeGen *cg, const char *fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    while (cg->hoisted_len + len + 1 > cg->hoisted_cap) {
        cg->hoisted_cap *= 2;
        cg->hoisted = realloc(cg->hoisted, cg->hoisted_cap);
    }
    memcpy(cg->hoisted + cg->hoisted_len, buf, len);
    cg->hoisted_len += len;
    cg->hoisted[cg->hoisted_len] = '\0';
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

/* Emit a Esu identifier as a valid C identifier (hyphens → underscores) */
static void emit_c_ident(CodeGen *cg, const char *prefix, const char *name) {
    emit(cg, "%s", prefix);
    for (const char *p = name; *p; p++) {
        emit(cg, "%c", *p == '-' ? '_' : *p);
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
            emit(cg, "esu_number(%g)", node->as.number_lit.value);
            break;

        case NODE_STRING_LIT:
            emit(cg, "esu_string(");
            emit_c_string(cg, node->as.string_lit.value);
            emit(cg, ")");
            break;

        case NODE_BOOL_LIT:
            emit(cg, "esu_bool(%d)", node->as.bool_lit.value);
            break;

        case NODE_NOTHING_LIT:
            emit(cg, "esu_nothing()");
            break;

        case NODE_IDENTIFIER:
            emit_c_ident(cg, "var_", node->as.identifier.name);
            break;

        case NODE_BINARY_EXPR: {
            const char *op = node->as.binary.op;
            const char *func = NULL;
            if (strcmp(op, "+") == 0) func = "esu_add";
            else if (strcmp(op, "-") == 0) func = "esu_sub";
            else if (strcmp(op, "*") == 0) func = "esu_mul";
            else if (strcmp(op, "/") == 0) func = "esu_div";
            else if (strcmp(op, "%") == 0) func = "esu_mod";
            else if (strcmp(op, "==") == 0) func = "esu_eq";
            else if (strcmp(op, "!=") == 0) func = "esu_neq";
            else if (strcmp(op, ">") == 0) func = "esu_gt";
            else if (strcmp(op, "<") == 0) func = "esu_lt";
            else if (strcmp(op, ">=") == 0) func = "esu_gte";
            else if (strcmp(op, "<=") == 0) func = "esu_lte";
            else if (strcmp(op, "and") == 0) func = "esu_and";
            else if (strcmp(op, "or") == 0) func = "esu_or";

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
                emit(cg, "esu_neg(");
                gen_expression(cg, node->as.unary.operand);
                emit(cg, ")");
            } else if (strcmp(op, "not") == 0) {
                emit(cg, "esu_not(");
                gen_expression(cg, node->as.unary.operand);
                emit(cg, ")");
            }
            break;
        }

        case NODE_FUNCTION_CALL: {
            const char *name = node->as.func_call.name;
            int argc = node->as.func_call.arg_count;
            if (strncmp(name, "__", 2) == 0) {
                /* Builtin: __length → esu_length, __contains → esu_contains, etc. */
                emit(cg, "esu%s(", name + 1); /* __X → esu_X */
                for (int i = 0; i < argc; i++) {
                    if (i > 0) emit(cg, ", ");
                    gen_expression(cg, node->as.func_call.args[i]);
                }
                emit(cg, ")");
            } else if (is_function_name(cg, name)) {
                /* Direct call to a declared Esu function */
                emit_c_ident(cg, "esu_func_", name);
                emit(cg, "(");
                for (int i = 0; i < argc; i++) {
                    if (i > 0) emit(cg, ", ");
                    gen_expression(cg, node->as.func_call.args[i]);
                }
                emit(cg, ")");
            } else {
                /* Indirect call through a variable holding a function value */
                emit(cg, "({ EsuValue *_fn = ");
                emit_c_ident(cg, "var_", name);
                emit(cg, "; EsuValue *_args[%d] = {", argc > 0 ? argc : 1);
                if (argc == 0) {
                    emit(cg, "0");
                } else {
                    for (int i = 0; i < argc; i++) {
                        if (i > 0) emit(cg, ", ");
                        gen_expression(cg, node->as.func_call.args[i]);
                    }
                }
                emit(cg, "}; esu_call(_fn, _args, %d); })", argc);
            }
            break;
        }

        case NODE_LIST_LIT: {
            /* Generate: create list, append each item */
            emit(cg, "({ EsuValue *_l = esu_list_new(); ");
            for (int i = 0; i < node->as.list_lit.count; i++) {
                emit(cg, "esu_list_append(_l, ");
                gen_expression(cg, node->as.list_lit.items[i]);
                emit(cg, "); ");
            }
            emit(cg, "_l; })");
            break;
        }

        case NODE_MAP_LIT: {
            emit(cg, "({ EsuValue *_m = esu_map_new(); ");
            for (int i = 0; i < node->as.map_lit.count; i++) {
                emit(cg, "esu_map_set(_m, esu_string(");
                emit_c_string(cg, node->as.map_lit.keys[i]);
                emit(cg, "), ");
                gen_expression(cg, node->as.map_lit.values[i]);
                emit(cg, "); ");
            }
            emit(cg, "_m; })");
            break;
        }

        case NODE_INDEX_EXPR:
            emit(cg, "esu_index(");
            gen_expression(cg, node->as.index_expr.object);
            emit(cg, ", ");
            gen_expression(cg, node->as.index_expr.index);
            emit(cg, ")");
            break;

        case NODE_MEMBER_EXPR:
            /* x.field → esu_index(x, esu_string("field")) */
            emit(cg, "esu_index(");
            gen_expression(cg, node->as.member_expr.object);
            emit(cg, ", esu_string(");
            emit_c_string(cg, node->as.member_expr.field);
            emit(cg, "))");
            break;

        case NODE_METHOD_CALL: {
            /* x.method(args) → esu_call_method(x, "method", argv, argc) */
            int argc = node->as.method_call.arg_count;
            emit(cg, "({ EsuValue *_self = ");
            gen_expression(cg, node->as.method_call.object);
            emit(cg, "; EsuValue *_args[%d] = {", argc > 0 ? argc : 1);
            if (argc == 0) {
                emit(cg, "0");
            } else {
                for (int i = 0; i < argc; i++) {
                    if (i > 0) emit(cg, ", ");
                    gen_expression(cg, node->as.method_call.args[i]);
                }
            }
            emit(cg, "}; esu_call_method(_self, ");
            emit_c_string(cg, node->as.method_call.method_name);
            emit(cg, ", _args, %d); })", argc);
            break;
        }

        case NODE_LAMBDA: {
            /* Assign a unique ID and emit the function body to the hoisted
             * buffer. At this expression site, emit a closure creation. */
            int id = cg->lambda_counter++;
            int param_count = node->as.lambda.param_count;

            /* Emit the function to hoisted storage.
             * Note: closures don't currently capture enclosing variables;
             * the `env` arg is for future use. */
            emit_hoisted(cg,
                "static EsuValue *esu_lambda_%d(EsuValue **_args, int _argc, EsuValue *_env) {\n"
                "    (void)_argc; (void)_env;\n",
                id);
            for (int i = 0; i < param_count; i++) {
                emit_hoisted(cg,
                    "    EsuValue *var_%s = _argc > %d ? _args[%d] : esu_nothing();\n",
                    node->as.lambda.params[i], i, i);
            }
            /* Body: if single expression, return it */
            if (node->as.lambda.body_expr) {
                emit_hoisted(cg, "    return ");
                /* Temporarily redirect main output to hoisted to emit expression */
                char *saved_output = cg->output;
                int saved_len = cg->output_len;
                int saved_cap = cg->output_cap;
                cg->output = cg->hoisted;
                cg->output_len = cg->hoisted_len;
                cg->output_cap = cg->hoisted_cap;
                gen_expression(cg, node->as.lambda.body_expr);
                cg->hoisted = cg->output;
                cg->hoisted_len = cg->output_len;
                cg->hoisted_cap = cg->output_cap;
                cg->output = saved_output;
                cg->output_len = saved_len;
                cg->output_cap = saved_cap;
                emit_hoisted(cg, ";\n");
            } else {
                emit_hoisted(cg, "    return esu_nothing();\n");
            }
            emit_hoisted(cg, "}\n\n");

            /* At the call site: create a closure */
            emit(cg, "esu_closure_new(esu_lambda_%d, NULL, %d)",
                 id, param_count);
            break;
        }

        case NODE_CALL_VALUE: {
            int argc = node->as.call_value.arg_count;
            emit(cg, "({ EsuValue *_fn = ");
            gen_expression(cg, node->as.call_value.callee);
            emit(cg, "; EsuValue *_args[%d] = {", argc > 0 ? argc : 1);
            if (argc == 0) {
                emit(cg, "0");
            } else {
                for (int i = 0; i < argc; i++) {
                    if (i > 0) emit(cg, ", ");
                    gen_expression(cg, node->as.call_value.args[i]);
                }
            }
            emit(cg, "}; esu_call(_fn, _args, %d); })", argc);
            break;
        }

        case NODE_CONSTRUCTOR: {
            /* Create a new struct with fields set */
            emit(cg, "({ EsuValue *_s = esu_struct_new(");
            emit_c_string(cg, node->as.constructor.type_name);
            emit(cg, "); ");
            for (int i = 0; i < node->as.constructor.count; i++) {
                emit(cg, "esu_map_set(_s, esu_string(");
                emit_c_string(cg, node->as.constructor.field_names[i]);
                emit(cg, "), ");
                gen_expression(cg, node->as.constructor.values[i]);
                emit(cg, "); ");
            }
            emit(cg, "_s; })");
            break;
        }

        default:
            emit(cg, "esu_nothing() /* unhandled expr */");
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
            emit(cg, "esu_print(");
            gen_expression(cg, node->as.print_stmt.expr);
            emit(cg, ");\n");
            break;

        case NODE_ASSIGNMENT:
            emit_indent(cg);
            if (!is_declared(cg, node->as.assignment.name)) {
                emit(cg, "EsuValue *");
                emit_c_ident(cg, "var_", node->as.assignment.name);
                emit(cg, " = ");
                mark_declared(cg, node->as.assignment.name);
            } else {
                emit_c_ident(cg, "var_", node->as.assignment.name);
                emit(cg, " = ");
            }
            gen_expression(cg, node->as.assignment.value);
            emit(cg, ";\n");
            break;

        case NODE_IF_STMT:
            emit_indent(cg);
            emit(cg, "if (esu_is_truthy(");
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
                emit(cg, "if (esu_is_truthy(");
                gen_expression(cg, node->as.if_stmt.else_if->as.if_stmt.condition);
                emit(cg, ")) {\n");
                cg->indent++;
                gen_block(cg, &node->as.if_stmt.else_if->as.if_stmt.body);
                cg->indent--;
                /* Recurse for further else-if chains or final else */
                ASTNode *chain = node->as.if_stmt.else_if;
                while (chain->as.if_stmt.else_if) {
                    emit_indent(cg);
                    emit(cg, "} else if (esu_is_truthy(");
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
            emit(cg, "while (esu_is_truthy(");
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
            emit(cg, "EsuValue *");
            emit_c_ident(cg, "var_", var);
            emit(cg, " = esu_number(_i);\n");
            gen_block(cg, &node->as.for_range.body);
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;
        }

        case NODE_FOR_IN_STMT: {
            const char *var = node->as.for_in.var_name;
            emit_indent(cg);
            emit(cg, "{\n");
            cg->indent++;
            emit_indent(cg);
            emit(cg, "EsuValue *_collection = ");
            gen_expression(cg, node->as.for_in.collection);
            emit(cg, ";\n");
            emit_indent(cg);
            emit(cg, "if (_collection->type == %d) {\n", 4); /* VAL_LIST = 4 */
            cg->indent++;
            emit_indent(cg);
            emit(cg, "EsuList *_list = _collection->as.list;\n");
            emit_indent(cg);
            emit(cg, "for (int _idx = 0; _idx < _list->count; _idx++) {\n");
            cg->indent++;
            emit_indent(cg);
            emit(cg, "EsuValue *");
            emit_c_ident(cg, "var_", var);
            emit(cg, " = _list->items[_idx];\n");
            gen_block(cg, &node->as.for_in.body);
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;
        }

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

        case NODE_RETURN_STMT:
            emit_indent(cg);
            if (node->as.return_stmt.value) {
                emit(cg, "return ");
                gen_expression(cg, node->as.return_stmt.value);
                emit(cg, ";\n");
            } else {
                emit(cg, "return esu_nothing();\n");
            }
            break;

        case NODE_FUNCTION_CALL:
            /* Function call as a statement (discard return value) */
            emit_indent(cg);
            gen_expression(cg, node);
            emit(cg, ";\n");
            break;

        case NODE_FUNCTION_DEF:
            /* Function defs are handled in codegen_generate, not here */
            break;

        case NODE_TRY_STMT: {
            const char *err_var = node->as.try_stmt.error_var;
            emit_indent(cg);
            emit(cg, "esu_error_push();\n");
            emit_indent(cg);
            emit(cg, "if (setjmp(*esu_error_frame()) == 0) {\n");
            cg->indent++;
            gen_block(cg, &node->as.try_stmt.body);
            emit_indent(cg);
            emit(cg, "esu_error_pop();\n");
            cg->indent--;
            emit_indent(cg);
            emit(cg, "} else {\n");
            cg->indent++;
            emit_indent(cg);
            emit(cg, "EsuValue *");
            emit_c_ident(cg, "var_", err_var);
            emit(cg, " = esu_get_error();\n");
            mark_declared(cg, err_var);
            gen_block(cg, &node->as.try_stmt.catch_body);
            cg->indent--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;
        }

        case NODE_RAISE_STMT:
            emit_indent(cg);
            emit(cg, "esu_raise(");
            gen_expression(cg, node->as.raise_stmt.value);
            emit(cg, ");\n");
            break;

        case NODE_STRUCT_DECL:
            /* Handled at top level — no output here */
            break;
        case NODE_METHOD_DEF:
            /* Handled at top level — no output here */
            break;
        case NODE_IMPORT:
            /* Handled at top level — no output here */
            break;
        case NODE_METHOD_CALL:
            /* Method call as statement: discard return */
            emit_indent(cg);
            gen_expression(cg, node);
            emit(cg, ";\n");
            break;

        case NODE_FIELD_ASSIGN:
            /* x.field is V  →  esu_map_set(x, "field", V) (for struct/map)
             * x[k] is V     →  esu_index_set(x, k, V) (for lists) */
            emit_indent(cg);
            emit(cg, "{ EsuValue *_obj = ");
            gen_expression(cg, node->as.field_assign.object);
            emit(cg, "; EsuValue *_key = ");
            gen_expression(cg, node->as.field_assign.key);
            emit(cg, "; EsuValue *_val = ");
            gen_expression(cg, node->as.field_assign.value);
            emit(cg, "; if (_obj->type == VAL_MAP) esu_map_set(_obj, _key, _val); ");
            emit(cg, "else esu_index_set(_obj, _key, _val); }\n");
            break;

        default:
            /* Expression statement */
            emit_indent(cg);
            gen_expression(cg, node);
            emit(cg, ";\n");
            break;
    }
}

static void gen_function_def(CodeGen *cg, ASTNode *node) {
    const char *name = node->as.func_def.name;
    int param_count = node->as.func_def.param_count;

    emit(cg, "EsuValue *");
    emit_c_ident(cg, "esu_func_", name);
    emit(cg, "(");
    for (int i = 0; i < param_count; i++) {
        if (i > 0) emit(cg, ", ");
        emit(cg, "EsuValue *");
        emit_c_ident(cg, "var_", node->as.func_def.params[i]);
    }
    emit(cg, ") {\n");

    /* Save and reset declared vars for function scope */
    char **saved_vars = cg->declared_vars;
    int saved_count = cg->declared_count;
    int saved_cap = cg->declared_cap;
    cg->declared_cap = 16;
    cg->declared_vars = malloc(sizeof(char *) * cg->declared_cap);
    cg->declared_count = 0;

    /* Mark parameters as declared */
    for (int i = 0; i < param_count; i++) {
        mark_declared(cg, node->as.func_def.params[i]);
    }

    cg->indent = 1;
    gen_block(cg, &node->as.func_def.body);

    /* Implicit return nothing at end */
    emit_indent(cg);
    emit(cg, "return esu_nothing();\n");

    cg->indent = 0;
    emit(cg, "}\n\n");

    /* Restore outer scope vars */
    for (int i = 0; i < cg->declared_count; i++) {
        free(cg->declared_vars[i]);
    }
    free(cg->declared_vars);
    cg->declared_vars = saved_vars;
    cg->declared_count = saved_count;
    cg->declared_cap = saved_cap;
}

/* Emit a method definition as a C function accepting (EsuValue **args, int argc).
 * args[0] is self. */
static void gen_method_def(CodeGen *cg, ASTNode *node) {
    const char *type_name = node->as.method_def.type_name;
    const char *mname = node->as.method_def.name;
    int param_count = node->as.method_def.param_count;

    emit(cg, "static EsuValue *esu_method_");
    /* Emit type_name with hyphens -> underscores */
    for (const char *p = type_name; *p; p++) emit(cg, "%c", *p == '-' ? '_' : *p);
    emit(cg, "_");
    for (const char *p = mname; *p; p++) emit(cg, "%c", *p == '-' ? '_' : *p);
    emit(cg, "(EsuValue **_args, int _argc) {\n");

    cg->indent = 1;
    emit_indent(cg);
    emit(cg, "(void)_argc;\n");
    emit_indent(cg);
    emit(cg, "EsuValue *var_self = _args[0];\n");
    /* Also bind the type name as a self-ish alias, e.g. "var_person = self" */
    emit_indent(cg);
    emit(cg, "EsuValue *");
    emit_c_ident(cg, "var_", type_name);
    emit(cg, " = _args[0];\n");

    /* Save/reset declared vars */
    char **saved_vars = cg->declared_vars;
    int saved_count = cg->declared_count;
    int saved_cap = cg->declared_cap;
    cg->declared_cap = 16;
    cg->declared_vars = malloc(sizeof(char *) * cg->declared_cap);
    cg->declared_count = 0;
    mark_declared(cg, "self");
    mark_declared(cg, type_name);

    /* Parameters */
    for (int i = 0; i < param_count; i++) {
        emit_indent(cg);
        emit(cg, "EsuValue *");
        emit_c_ident(cg, "var_", node->as.method_def.params[i]);
        emit(cg, " = _argc > %d ? _args[%d] : esu_nothing();\n", i + 1, i + 1);
        mark_declared(cg, node->as.method_def.params[i]);
    }

    gen_block(cg, &node->as.method_def.body);

    emit_indent(cg);
    emit(cg, "return esu_nothing();\n");

    cg->indent = 0;
    emit(cg, "}\n\n");

    /* Restore scope */
    for (int i = 0; i < cg->declared_count; i++) free(cg->declared_vars[i]);
    free(cg->declared_vars);
    cg->declared_vars = saved_vars;
    cg->declared_count = saved_count;
    cg->declared_cap = saved_cap;
}

/* Expand imports: load each imported module's AST and merge its
 * function defs, method defs, and struct decls into the program's
 * statement list (prepended so they're available to later code). */
void codegen_expand_imports(ASTNode *program) {
    NodeList *stmts = &program->as.program.stmts;
    /* Collect imported AST nodes to prepend */
    NodeList imported;
    node_list_init(&imported);

    /* Track which modules we've already imported to avoid duplicates */
    char *loaded_modules[128];
    int loaded_count = 0;

    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type != NODE_IMPORT) continue;
        const char *path = stmts->items[i]->as.import_stmt.module_path;

        /* Skip if already imported */
        int already = 0;
        for (int k = 0; k < loaded_count; k++) {
            if (strcmp(loaded_modules[k], path) == 0) { already = 1; break; }
        }
        if (already) continue;
        if (loaded_count < 128) {
            loaded_modules[loaded_count] = malloc(strlen(path) + 1);
            strcpy(loaded_modules[loaded_count], path);
            loaded_count++;
        }

        ASTNode *mod = module_load(path);
        if (!mod) continue;
        /* Merge the module's definitions (not its executable statements) */
        NodeList *mod_stmts = &mod->as.program.stmts;
        for (int j = 0; j < mod_stmts->count; j++) {
            NodeType t = mod_stmts->items[j]->type;
            if (t == NODE_FUNCTION_DEF || t == NODE_METHOD_DEF ||
                t == NODE_STRUCT_DECL || t == NODE_IMPORT) {
                node_list_push(&imported, mod_stmts->items[j]);
            }
        }
    }

    /* Free tracking */
    for (int k = 0; k < loaded_count; k++) free(loaded_modules[k]);

    if (imported.count > 0) {
        /* Prepend imported definitions before existing statements */
        int new_count = imported.count + stmts->count;
        ASTNode **new_items = malloc(sizeof(ASTNode *) * new_count);
        memcpy(new_items, imported.items, sizeof(ASTNode *) * imported.count);
        memcpy(new_items + imported.count, stmts->items,
               sizeof(ASTNode *) * stmts->count);
        free(stmts->items);
        stmts->items = new_items;
        stmts->count = new_count;
        stmts->capacity = new_count;
    }
    free(imported.items);
}

void codegen_generate(CodeGen *cg, ASTNode *program) {
    /* Imports should already be expanded by compile_esu_src before
     * the resolver runs. Don't re-expand here. */

    NodeList *stmts = &program->as.program.stmts;

    /* Preamble */
    emit(cg, "#include \"esu_runtime.h\"\n\n");

    /* Register all function names so NODE_FUNCTION_CALL can distinguish
     * direct calls from calls-through-variable. */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_FUNCTION_DEF) {
            register_function_name(cg, stmts->items[i]->as.func_def.name);
        }
    }

    /* Forward declarations for all functions */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_FUNCTION_DEF) {
            ASTNode *fn = stmts->items[i];
            emit(cg, "EsuValue *");
            emit_c_ident(cg, "esu_func_", fn->as.func_def.name);
            emit(cg, "(");
            for (int j = 0; j < fn->as.func_def.param_count; j++) {
                if (j > 0) emit(cg, ", ");
                emit(cg, "EsuValue *");
                emit_c_ident(cg, "var_", fn->as.func_def.params[j]);
            }
            emit(cg, ");\n");
        }
    }
    emit(cg, "\n");

    /* Emit function definitions */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_FUNCTION_DEF) {
            gen_function_def(cg, stmts->items[i]);
        }
    }

    /* Emit method definitions (same level as functions) */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_METHOD_DEF) {
            gen_method_def(cg, stmts->items[i]);
        }
    }

    /* Emit hoisted lambda definitions (accumulated during expression
     * generation). We do a first pass over statements to trigger lambda
     * registration — but since lambdas are expressions generated when
     * we encounter them, we need to do the main pass first, collecting
     * hoisted output, then splice it in. We handle this by generating
     * main into a temporary and hoisted lambdas into cg->hoisted. */

    /* Placeholder — we'll emit hoisted lambdas between method defs and main
     * by pre-generating main's body into a separate buffer. Simpler: we
     * generate main as usual, then at the end, we splice the hoisted
     * content right before "int main". */

    /* Emit wrapper closures so declared functions can be passed as values.
     * esu_func_wrap_NAME wraps esu_func_NAME in the closure calling
     * convention (EsuValue** args, int argc, EsuValue* env). */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_FUNCTION_DEF) {
            ASTNode *fn = stmts->items[i];
            const char *name = fn->as.func_def.name;
            int pc = fn->as.func_def.param_count;
            emit(cg, "static EsuValue *esu_func_wrap_");
            for (const char *pp = name; *pp; pp++) emit(cg, "%c", *pp == '-' ? '_' : *pp);
            emit(cg, "(EsuValue **_a, int _ac, EsuValue *_e) {\n");
            emit(cg, "    (void)_ac; (void)_e;\n");
            emit(cg, "    return ");
            emit_c_ident(cg, "esu_func_", name);
            emit(cg, "(");
            for (int j = 0; j < pc; j++) {
                if (j > 0) emit(cg, ", ");
                emit(cg, "_a[%d]", j);
            }
            emit(cg, ");\n}\n\n");
        }
    }

    /* Main function with non-function/method statements */
    /* Record position for later splicing of hoisted content */
    int pre_main_pos = cg->output_len;

    emit(cg, "int main(int argc, char **argv) {\n");
    cg->indent = 1;
    emit_indent(cg);
    emit(cg, "esu_runtime_init();\n");

    /* Create EsuValue* wrappers for all declared functions so they can
     * be passed as values: var_triple = closure_new(esu_func_wrap_triple) */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_FUNCTION_DEF) {
            ASTNode *fn = stmts->items[i];
            const char *name = fn->as.func_def.name;
            emit_indent(cg);
            emit(cg, "EsuValue *");
            emit_c_ident(cg, "var_", name);
            emit(cg, " = esu_closure_new(esu_func_wrap_");
            for (const char *pp = name; *pp; pp++) emit(cg, "%c", *pp == '-' ? '_' : *pp);
            emit(cg, ", NULL, %d);\n", fn->as.func_def.param_count);
            mark_declared(cg, name);
        }
    }

    /* Register methods at startup */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_METHOD_DEF) {
            ASTNode *m = stmts->items[i];
            emit_indent(cg);
            emit(cg, "esu_register_method(");
            emit_c_string(cg, m->as.method_def.type_name);
            emit(cg, ", ");
            emit_c_string(cg, m->as.method_def.name);
            emit(cg, ", esu_method_");
            for (const char *p = m->as.method_def.type_name; *p; p++)
                emit(cg, "%c", *p == '-' ? '_' : *p);
            emit(cg, "_");
            for (const char *p = m->as.method_def.name; *p; p++)
                emit(cg, "%c", *p == '-' ? '_' : *p);
            emit(cg, ");\n");
        }
    }

    /* Register parent relationships for inheritance */
    for (int i = 0; i < stmts->count; i++) {
        if (stmts->items[i]->type == NODE_STRUCT_DECL) {
            ASTNode *sd = stmts->items[i];
            if (sd->as.struct_decl.parent_type) {
                emit_indent(cg);
                emit(cg, "esu_register_parent(");
                emit_c_string(cg, sd->as.struct_decl.type_name);
                emit(cg, ", ");
                emit_c_string(cg, sd->as.struct_decl.parent_type);
                emit(cg, ");\n");
            }
        }
    }
    emit(cg, "\n");

    for (int i = 0; i < stmts->count; i++) {
        NodeType t = stmts->items[i]->type;
        if (t != NODE_FUNCTION_DEF && t != NODE_METHOD_DEF &&
            t != NODE_STRUCT_DECL) {
            gen_statement(cg, stmts->items[i]);
        }
    }

    emit(cg, "\n");
    emit_indent(cg);
    emit(cg, "esu_runtime_cleanup();\n");
    emit_indent(cg);
    emit(cg, "return 0;\n");
    cg->indent = 0;
    emit(cg, "}\n");

    /* Splice hoisted lambda definitions in before main() */
    if (cg->hoisted_len > 0) {
        int new_len = cg->output_len + cg->hoisted_len;
        while (new_len + 1 > cg->output_cap) {
            cg->output_cap *= 2;
            cg->output = realloc(cg->output, cg->output_cap);
        }
        /* Shift content from pre_main_pos onward to make room for hoisted */
        int tail_len = cg->output_len - pre_main_pos;
        memmove(cg->output + pre_main_pos + cg->hoisted_len,
                cg->output + pre_main_pos,
                tail_len);
        memcpy(cg->output + pre_main_pos, cg->hoisted, cg->hoisted_len);
        cg->output_len = new_len;
        cg->output[cg->output_len] = '\0';
    }
}

char *codegen_get_output(CodeGen *cg) {
    return cg->output;
}

void codegen_free(CodeGen *cg) {
    free(cg->output);
    cg->output = NULL;
    free(cg->hoisted);
    cg->hoisted = NULL;
    for (int i = 0; i < cg->function_names_count; i++) {
        free(cg->function_names[i]);
    }
    free(cg->function_names);
    cg->function_names = NULL;
    for (int i = 0; i < cg->declared_count; i++) {
        free(cg->declared_vars[i]);
    }
    free(cg->declared_vars);
    cg->declared_vars = NULL;
}
