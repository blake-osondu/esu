#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "lexer.h"
#include "normalizer.h"
#include "parser.h"
#include "resolver.h"
#include "codegen.h"
#include "util.h"
#include "error.h"
#include "linenoise.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s build <file.babel>          Compile to native binary\n", prog);
    fprintf(stderr, "  %s build <file.babel> -o <out>  Compile with custom output name\n", prog);
    fprintf(stderr, "  %s run <file.babel>             Compile and run, then clean up\n", prog);
    fprintf(stderr, "  %s emit <file.babel>            Print generated C to stdout\n", prog);
    fprintf(stderr, "  %s repl                         Interactive REPL\n", prog);
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

/* Compile source code from a string to a binary. Returns 0 on success.
 * If emit_only is true, prints C to stdout instead of building a binary.
 * If recoverable is true, parse/semantic errors longjmp instead of exit. */
static int compile_babel_src(const char *source, const char *output_path,
                             int emit_only, int recoverable) {
    if (recoverable) {
        babel_recoverable_errors = 1;
        if (setjmp(babel_recovery_jmp) != 0) {
            babel_recoverable_errors = 0;
            return 1; /* Error recovered */
        }
    }
    /* Tokenize */
    int token_count;
    Token *tokens = tokenize(source, &token_count);

    /* Normalize */
    tokens = normalizer_run(tokens, token_count, &token_count);

    /* Parse */
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode *program = parser_parse(&parser);

    /* Resolve */
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
        babel_recoverable_errors = 0;
        return 0;
    }

    /* Write C file */
    char c_path[512];
    snprintf(c_path, sizeof(c_path), "%s.c", output_path);
    FILE *f = fopen(c_path, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot write to '%s'\n", c_path);
        codegen_free(&cg);
        ast_free(program);
        for (int i = 0; i < token_count; i++) token_free(&tokens[i]);
        free(tokens);
        return 1;
    }
    fputs(c_code, f);
    fclose(f);

    const char *babel_dir = getenv("BABEL_HOME");
    if (!babel_dir) babel_dir = ".";

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "cc -o %s %s -I%s/runtime -L%s/build -lbabel_runtime -lm 2>&1",
        output_path, c_path, babel_dir, babel_dir);

    int result = system(cmd);
    if (result == 0) remove(c_path);

    codegen_free(&cg);
    ast_free(program);
    for (int i = 0; i < token_count; i++) token_free(&tokens[i]);
    free(tokens);
    babel_recoverable_errors = 0;
    return result;
}

static int compile_babel(const char *source_path, const char *output_path, int emit_only) {
    char *source = read_file(source_path);
    /* Use source_path as base name for intermediate C file */
    const char *out = emit_only ? "" : (output_path ? output_path : source_path);
    /* For emit_only with a file, the c_path uses source_path; otherwise output */
    int result;
    if (emit_only) {
        result = compile_babel_src(source, NULL, 1, 0);
    } else {
        result = compile_babel_src(source, out, 0, 0);
    }
    free(source);
    return result;
}

/* Check if a source string has unbalanced block openers (needs more input).
 * Returns the net open-block count: positive means we need more input. */
static int needs_more_input(const char *source) {
    int depth = 0;
    int in_string = 0;
    int in_comment = 0;
    const char *p = source;
    while (*p) {
        if (in_comment) {
            if (*p == '\n') in_comment = 0;
            p++; continue;
        }
        if (in_string) {
            if (*p == '\\' && *(p+1)) { p += 2; continue; }
            if (*p == '"') in_string = 0;
            p++; continue;
        }
        if (*p == '"') { in_string = 1; p++; continue; }
        if (*p == '-' && *(p+1) == '-') { in_comment = 1; p += 2; continue; }
        /* Check for block keywords at word boundaries */
        if ((p == source || !isalnum((unsigned char)*(p-1))) && isalpha((unsigned char)*p)) {
            const char *start = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            int len = p - start;
            /* Openers */
            if ((len == 2 && strncmp(start, "if", 2) == 0) ||
                (len == 5 && strncmp(start, "while", 5) == 0) ||
                (len == 5 && strncmp(start, "until", 5) == 0) ||
                (len == 3 && strncmp(start, "for", 3) == 0) ||
                (len == 4 && strncmp(start, "loop", 4) == 0) ||
                (len == 8 && strncmp(start, "function", 8) == 0) ||
                (len == 3 && strncmp(start, "try", 3) == 0) ||
                (len == 2 && strncmp(start, "to", 2) == 0 && *p == ' ')) {
                /* "to NAME with" function form — only count if followed by name */
                if (len == 2 && strncmp(start, "to", 2) == 0) {
                    /* Skip if this is "to" in other contexts */
                    /* Simple heuristic: at start of line, it's a function def */
                    int at_line_start = (start == source);
                    if (!at_line_start) {
                        const char *q = start - 1;
                        while (q >= source && (*q == ' ' || *q == '\t')) q--;
                        if (q >= source && *q != '\n') {
                            continue; /* not at line start */
                        }
                    }
                    depth++;
                } else {
                    depth++;
                }
            } else if ((len == 3 && strncmp(start, "end", 3) == 0)) {
                depth--;
            }
            continue;
        }
        /* Period closes a block at top level */
        if (*p == '.' && (p == source || !isdigit((unsigned char)*(p-1))) &&
            !isdigit((unsigned char)*(p+1))) {
            if (depth > 0) depth--;
        }
        p++;
    }
    return depth;
}

/* Read a single line using linenoise (arrow keys, history, line editing).
 * Falls back to getchar when input is not a TTY (piped input for testing).
 * Returns malloc'd string, NULL on EOF. */
static char *read_line(const char *prompt) {
    /* Use linenoise when stdin is a terminal */
    if (isatty(fileno(stdin))) {
        char *ln = linenoise(prompt);
        if (!ln) return NULL;
        if (*ln) linenoiseHistoryAdd(ln);
        /* Copy to malloc memory so caller can free() with stdlib free */
        int len = strlen(ln);
        char *buf = malloc(len + 1);
        memcpy(buf, ln, len + 1);
        linenoiseFree(ln);
        return buf;
    }
    /* Non-interactive fallback: simple getchar loop */
    printf("%s", prompt);
    fflush(stdout);
    int cap = 256;
    char *buf = malloc(cap);
    int len = 0;
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = c;
    }
    if (c == EOF && len == 0) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static int run_repl(void) {
    const char *babel_dir = getenv("BABEL_HOME");
    if (!babel_dir) babel_dir = ".";

    /* Load history from ~/.babel_history if available */
    const char *home = getenv("HOME");
    char history_path[1024];
    if (home) {
        snprintf(history_path, sizeof(history_path), "%s/.babel_history", home);
        linenoiseHistoryLoad(history_path);
    } else {
        history_path[0] = '\0';
    }
    linenoiseHistorySetMaxLen(1000);
    /* Allow multi-line editing within a single linenoise call */
    linenoiseSetMultiLine(1);

    printf("Babel REPL — type :help for commands, :quit to exit\n");
    printf("\n");

    /* Accumulated source code across the session */
    int src_cap = 4096;
    char *accumulated = malloc(src_cap);
    accumulated[0] = '\0';
    int src_len = 0;

    /* Previous run's output (for diffing to show only new output) */
    char *prev_output = malloc(1);
    prev_output[0] = '\0';
    int prev_output_len = 0;

    /* Buffer for multi-line input */
    int buf_cap = 1024;
    char *input_buf = malloc(buf_cap);
    input_buf[0] = '\0';
    int input_len = 0;

    const char *tmp_bin = "/tmp/babel_repl_tmp";
    const char *tmp_out = "/tmp/babel_repl_output";

    for (;;) {
        int more = needs_more_input(input_buf);
        const char *prompt = more > 0 ? "..  " : ">>  ";
        char *line = read_line(prompt);
        if (!line) {
            printf("\n");
            break;
        }

        /* Meta-commands at top-level (no pending multi-line input) */
        if (input_len == 0 && line[0] == ':') {
            if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) {
                free(line);
                break;
            }
            if (strcmp(line, ":help") == 0 || strcmp(line, ":h") == 0) {
                printf("REPL commands:\n");
                printf("  :help, :h       Show this help\n");
                printf("  :quit, :q       Exit REPL\n");
                printf("  :clear, :c      Reset session (forget all definitions)\n");
                printf("  :show           Print the full accumulated program\n");
                printf("\n");
                printf("Multi-line input: start a block (if/while/function/try) and\n");
                printf("the prompt changes to '..' until the block is closed with 'end' or '.'\n");
                printf("\n");
                free(line);
                continue;
            }
            if (strcmp(line, ":clear") == 0 || strcmp(line, ":c") == 0) {
                accumulated[0] = '\0';
                src_len = 0;
                prev_output[0] = '\0';
                prev_output_len = 0;
                printf("Session cleared.\n");
                free(line);
                continue;
            }
            if (strcmp(line, ":show") == 0) {
                printf("--- Session source ---\n%s\n--- End ---\n", accumulated);
                free(line);
                continue;
            }
            printf("Unknown command: %s (try :help)\n", line);
            free(line);
            continue;
        }

        /* Append line to input buffer */
        int line_len = strlen(line);
        while (input_len + line_len + 2 > buf_cap) {
            buf_cap *= 2;
            input_buf = realloc(input_buf, buf_cap);
        }
        memcpy(input_buf + input_len, line, line_len);
        input_len += line_len;
        input_buf[input_len++] = '\n';
        input_buf[input_len] = '\0';
        free(line);

        /* If still need more input, continue with '..' prompt */
        if (needs_more_input(input_buf) > 0) continue;

        /* Build a candidate source: accumulated + new input */
        int candidate_len = src_len + input_len + 1;
        char *candidate = malloc(candidate_len + 1);
        memcpy(candidate, accumulated, src_len);
        memcpy(candidate + src_len, input_buf, input_len);
        candidate[src_len + input_len] = '\0';

        /* Compile the candidate */
        int result = compile_babel_src(candidate, tmp_bin, 0, 1);

        if (result != 0) {
            /* Compilation failed — discard this input, keep old accumulated */
            free(candidate);
            input_buf[0] = '\0';
            input_len = 0;
            continue;
        }

        /* Run the binary, capture output */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", tmp_bin, tmp_out);
        int run_result = system(cmd);

        /* Read the output file */
        FILE *f = fopen(tmp_out, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *output = malloc(size + 1);
            fread(output, 1, size, f);
            output[size] = '\0';
            fclose(f);

            /* Show only the new output (suffix after prev_output) */
            if (size >= prev_output_len &&
                memcmp(output, prev_output, prev_output_len) == 0) {
                printf("%s", output + prev_output_len);
            } else {
                /* Output doesn't match prefix — just show whole thing */
                printf("%s", output);
            }

            /* Update prev_output */
            free(prev_output);
            prev_output = output;
            prev_output_len = size;
        }

        remove(tmp_bin);
        remove(tmp_out);

        if (run_result == 0) {
            /* Commit the input to accumulated source */
            while (src_len + input_len + 1 > src_cap) {
                src_cap *= 2;
                accumulated = realloc(accumulated, src_cap);
            }
            memcpy(accumulated + src_len, input_buf, input_len);
            src_len += input_len;
            accumulated[src_len] = '\0';
        } else {
            /* Runtime error — don't commit, reset prev_output so next run
             * shows any output from the fresh accumulated code */
            /* Actually we already updated prev_output above. If the error
             * left state in a weird spot, we need to re-run the old
             * accumulated to reset prev_output. Simpler: just clear it. */
            free(prev_output);
            prev_output = malloc(1);
            prev_output[0] = '\0';
            prev_output_len = 0;
            /* Re-run the committed accumulated to re-establish baseline */
            if (src_len > 0) {
                int r = compile_babel_src(accumulated, tmp_bin, 0, 1);
                if (r == 0) {
                    snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", tmp_bin, tmp_out);
                    system(cmd);
                    FILE *f2 = fopen(tmp_out, "rb");
                    if (f2) {
                        fseek(f2, 0, SEEK_END);
                        long size = ftell(f2);
                        fseek(f2, 0, SEEK_SET);
                        char *out = malloc(size + 1);
                        fread(out, 1, size, f2);
                        out[size] = '\0';
                        fclose(f2);
                        free(prev_output);
                        prev_output = out;
                        prev_output_len = size;
                    }
                    remove(tmp_bin);
                    remove(tmp_out);
                }
            }
        }

        free(candidate);
        input_buf[0] = '\0';
        input_len = 0;
    }

    if (history_path[0]) linenoiseHistorySave(history_path);

    free(accumulated);
    free(prev_output);
    free(input_buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    /* REPL takes no additional arguments */
    if (strcmp(command, "repl") == 0) {
        return run_repl();
    }

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

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
