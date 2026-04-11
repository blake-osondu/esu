#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "normalizer.h"
#include "parser.h"
#include "resolver.h"
#include "codegen.h"
#include "util.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s build <file.babel>          Compile to native binary\n", prog);
    fprintf(stderr, "  %s build <file.babel> -o <out>  Compile with custom output name\n", prog);
    fprintf(stderr, "  %s run <file.babel>             Compile and run, then clean up\n", prog);
    fprintf(stderr, "  %s emit <file.babel>            Print generated C to stdout\n", prog);
}

/* Tokenize the source into an array of tokens */
static Token *tokenize(const char *source, int *out_count) {
    Lexer lex;
    lexer_init(&lex, source);

    int cap = 256;
    Token *tokens = malloc(sizeof(Token) * cap);
    int count = 0;

    for (;;) {
        if (count >= cap) {
            cap *= 2;
            tokens = realloc(tokens, sizeof(Token) * cap);
        }
        tokens[count] = lexer_next(&lex);
        if (tokens[count].type == TOK_EOF) {
            count++;
            break;
        }
        count++;
    }

    *out_count = count;
    return tokens;
}

static int compile_babel(const char *source_path, const char *output_path, int emit_only) {
    char *source = read_file(source_path);

    /* Tokenize */
    int token_count;
    Token *tokens = tokenize(source, &token_count);

    /* Normalize: rewrite sugar patterns to canonical forms */
    tokens = normalizer_run(tokens, token_count, &token_count);

    /* Parse */
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode *program = parser_parse(&parser);

    /* Resolve verb conjugations to function calls */
    resolver_run(program);

    /* Generate C */
    CodeGen cg;
    codegen_init(&cg);
    codegen_generate(&cg, program);
    char *c_code = codegen_get_output(&cg);

    if (emit_only) {
        printf("%s", c_code);
        codegen_free(&cg);
        ast_free(program);
        for (int i = 0; i < token_count; i++) token_free(&tokens[i]);
        free(tokens);
        free(source);
        return 0;
    }

    /* Write C file */
    char c_path[512];
    snprintf(c_path, sizeof(c_path), "%s.c", source_path);
    FILE *f = fopen(c_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot write to '%s'\n", c_path);
        return 1;
    }
    fputs(c_code, f);
    fclose(f);

    /* Compile C to binary using cc */
    char cmd[2048];
    /* Get the directory where the babel compiler lives for finding runtime */
    /* For now, use a path relative to the source directory of the project */
    const char *babel_dir = getenv("BABEL_HOME");
    if (!babel_dir) {
        /* Fallback: assume runtime is in a known build location */
        babel_dir = ".";
    }

    snprintf(cmd, sizeof(cmd),
        "cc -o %s %s -I%s/runtime -L%s/build -lbabel_runtime -lm 2>&1",
        output_path, c_path, babel_dir, babel_dir);

    int result = system(cmd);

    /* Clean up intermediate C file on success */
    if (result == 0) {
        remove(c_path);
    }

    codegen_free(&cg);
    ast_free(program);
    for (int i = 0; i < token_count; i++) token_free(&tokens[i]);
    free(tokens);
    free(source);

    return result;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];
    const char *source_path = argv[2];

    if (strcmp(command, "emit") == 0) {
        return compile_babel(source_path, NULL, 1);
    }

    if (strcmp(command, "build") == 0) {
        /* Determine output name */
        const char *output = "output";
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            output = argv[4];
        } else {
            /* Derive from source filename: hello.babel -> hello */
            static char derived[256];
            strncpy(derived, source_path, sizeof(derived) - 1);
            char *dot = strrchr(derived, '.');
            if (dot) *dot = '\0';
            /* Strip any directory prefix */
            char *slash = strrchr(derived, '/');
            if (slash) {
                output = slash + 1;
            } else {
                output = derived;
            }
        }
        return compile_babel(source_path, output, 0);
    }

    if (strcmp(command, "run") == 0) {
        const char *tmp_output = "/tmp/babel_run_tmp";
        int result = compile_babel(source_path, tmp_output, 0);
        if (result != 0) {
            fprintf(stderr, "Compilation failed.\n");
            return 1;
        }
        result = system(tmp_output);
        remove(tmp_output);
        return result;
    }

    fprintf(stderr, "Unknown command: '%s'\n", command);
    usage(argv[0]);
    return 1;
}
