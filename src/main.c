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
    fprintf(stderr, "  %s build <file.esu>          Compile to native binary\n", prog);
    fprintf(stderr, "  %s build <file.esu> -o <out>  Compile with custom output name\n", prog);
    fprintf(stderr, "  %s run <file.esu>             Compile and run, then clean up\n", prog);
    fprintf(stderr, "  %s emit <file.esu>            Print generated C to stdout\n", prog);
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
static int compile_esu_src(const char *source, const char *output_path,
                             int emit_only, int recoverable) {
    if (recoverable) {
        esu_recoverable_errors = 1;
        if (setjmp(esu_recovery_jmp) != 0) {
            esu_recoverable_errors = 0;
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
        esu_recoverable_errors = 0;
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

    const char *esu_dir = getenv("ESU_HOME");
    if (!esu_dir) esu_dir = ".";

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "cc -o %s %s -I%s/runtime -L%s/build -lesu_runtime -lm 2>&1",
        output_path, c_path, esu_dir, esu_dir);

    int result = system(cmd);
    if (result == 0) remove(c_path);

    codegen_free(&cg);
    ast_free(program);
    for (int i = 0; i < token_count; i++) token_free(&tokens[i]);
    free(tokens);
    esu_recoverable_errors = 0;
    return result;
}

static int compile_esu(const char *source_path, const char *output_path, int emit_only) {
    char *source = read_file(source_path);
    /* Use source_path as base name for intermediate C file */
    const char *out = emit_only ? "" : (output_path ? output_path : source_path);
    /* For emit_only with a file, the c_path uses source_path; otherwise output */
    int result;
    if (emit_only) {
        result = compile_esu_src(source, NULL, 1, 0);
    } else {
        result = compile_esu_src(source, out, 0, 0);
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

/* Print an explanation of a Esu topic. topic may be NULL for the
 * full guide, or a specific topic name. */
static void print_explain(const char *topic) {
    int all = (topic == NULL || *topic == '\0' ||
               strcmp(topic, "all") == 0 || strcmp(topic, "everything") == 0);

    if (all || strcmp(topic, "intro") == 0) {
        printf("\n=== Welcome to Esu ===\n\n");
        printf("Esu is a natural-language programming language. The same\n");
        printf("operation can be written many ways — whichever reads best.\n");
        printf("Lines end with a newline, a period (.), or a comma (,).\n");
        printf("Blocks close with 'end' or a period (.).\n");
        printf("Comments start with '--'.\n\n");
        printf("Try these commands for specific topics:\n");
        printf("  :explain basics       — variables, numbers, strings\n");
        printf("  :explain math         — arithmetic and math builtins\n");
        printf("  :explain logic        — comparisons and booleans\n");
        printf("  :explain control      — if/else, while, for, loops\n");
        printf("  :explain functions    — defining and calling functions\n");
        printf("  :explain strings      — string operations\n");
        printf("  :explain lists        — lists and iteration\n");
        printf("  :explain maps         — maps / dictionaries\n");
        printf("  :explain errors       — try/catch/raise\n");
        printf("  :explain io           — input and output\n");
        printf("  :explain sugar        — natural-language shortcuts\n");
        printf("  :explain all          — show everything\n\n");
        if (!all) return;
    }

    if (all || strcmp(topic, "basics") == 0) {
        printf("\n--- Basics: variables and values ---\n\n");
        printf("Esu has numbers, strings, booleans, lists, maps, and 'nothing'.\n");
        printf("Variables are created with 'is', 'let ... be', 'set ... to', etc.\n\n");
        printf("  x is 5\n");
        printf("  let name be \"Alice\"\n");
        printf("  set age to 30\n");
        printf("  is_ready is true\n");
        printf("  result is nothing\n\n");
        printf("Reassign with the same syntax:\n");
        printf("  x is 10\n");
        printf("  x is now 20\n");
        printf("  change x to 30\n\n");
        printf("Or in post-assignment style (action first):\n");
        printf("  5 + 10 then assign to total\n");
        printf("  the sum of 3 and 4 and store in answer\n\n");
    }

    if (all || strcmp(topic, "math") == 0) {
        printf("\n--- Math ---\n\n");
        printf("Arithmetic works with symbols or words:\n");
        printf("  display 5 + 3            (or 'plus')\n");
        printf("  display 10 - 2           (or 'minus')\n");
        printf("  display 4 * 5            (or 'times')\n");
        printf("  display 20 / 4           (or 'divided by')\n");
        printf("  display 17 modulo 5\n\n");
        printf("Named operations:\n");
        printf("  display the sum of 3 and 4\n");
        printf("  display the product of 6 and 7\n");
        printf("  display the difference of 10 and 3\n");
        printf("  display the quotient of 20 and 4\n\n");
        printf("Math builtins:\n");
        printf("  display the absolute value of -7\n");
        printf("  display the square root of 16\n");
        printf("  display 2 to the power of 10\n");
        printf("  display the floor of 3.7\n");
        printf("  display 3.5 rounded\n");
        printf("  display minimum of 5 and 10\n");
        printf("  display random number between 1 and 100\n\n");
    }

    if (all || strcmp(topic, "logic") == 0) {
        printf("\n--- Comparisons and booleans ---\n\n");
        printf("Comparisons produce true/false:\n");
        printf("  display x is greater than 5    (or x > 5)\n");
        printf("  display x is less than 5\n");
        printf("  display x equals 5             (or x == 5)\n");
        printf("  display x is not equal to 5\n");
        printf("  display x is at least 10\n");
        printf("  display x is at most 100\n\n");
        printf("Boolean logic:\n");
        printf("  display x > 0 and x < 100\n");
        printf("  display ready or finished\n");
        printf("  display not is_empty\n\n");
    }

    if (all || strcmp(topic, "control") == 0) {
        printf("\n--- Control flow ---\n\n");
        printf("If/else:\n");
        printf("  if x > 5 then\n");
        printf("      display \"big\"\n");
        printf("  otherwise\n");
        printf("      display \"small\".\n\n");
        printf("Alternative conditional words (all mean 'if'):\n");
        printf("  when, whenever, suppose, assuming, given, in case, should\n\n");
        printf("  when x is bigger than 5 then display \"big\".\n\n");
        printf("While loop:\n");
        printf("  while x < 10 do\n");
        printf("      x is x + 1\n");
        printf("  end\n\n");
        printf("Also: 'as long as', 'repeat while', 'until', 'keep doing ... until'.\n\n");
        printf("For loops:\n");
        printf("  for i from 1 to 10 do\n");
        printf("      display i\n");
        printf("  end\n");
        printf("  for item in list, display item.\n");
        printf("  repeat 5 times, display \"hi\".\n");
        printf("  count from 1 to 10 as i, display i.\n\n");
    }

    if (all || strcmp(topic, "functions") == 0) {
        printf("\n--- Functions ---\n\n");
        printf("Define functions many ways:\n");
        printf("  function add taking a and b\n");
        printf("      return a + b\n");
        printf("  end\n\n");
        printf("  to add with a and b\n");
        printf("      return a + b.\n\n");
        printf("  function square taking x, return x * x.\n\n");
        printf("Call functions:\n");
        printf("  display add(3, 4)\n");
        printf("  display call add with 3 and 4\n");
        printf("  display the result of calling add with 3 and 4\n\n");
        printf("Verb conjugation — define once, use many forms:\n");
        printf("  function double taking x, return x * 2.\n");
        printf("  display 5 doubled         (past tense)\n");
        printf("  display doubling 5        (gerund)\n");
        printf("  display doubles 5         (third person)\n");
        printf("  display 5 doubled tripled (chained, if triple defined)\n\n");
        printf("Return sugar: 'give back', 'send back', 'respond with'\n\n");
    }

    if (all || strcmp(topic, "strings") == 0) {
        printf("\n--- Strings ---\n\n");
        printf("Interpolation with {expr} inside double-quoted strings:\n");
        printf("  name is \"World\"\n");
        printf("  display \"Hello, {name}!\"\n");
        printf("  display \"2 + 3 = {2 + 3}\"\n\n");
        printf("Operations:\n");
        printf("  display length of \"hello\"\n");
        printf("  display \"hello\" in uppercase\n");
        printf("  display \"hello\" contains \"ell\"\n");
        printf("  display \"hello\" starts with \"hel\"\n");
        printf("  display first letter of \"hello\"\n");
        printf("  display \"hi\" replaced \"i\" with \"ello\"\n\n");
        printf("Possessive form:\n");
        printf("  display name's length    (same as length of name)\n\n");
    }

    if (all || strcmp(topic, "lists") == 0) {
        printf("\n--- Lists ---\n\n");
        printf("Create:\n");
        printf("  items is [1, 2, 3, 4, 5]\n");
        printf("  names is a list of \"Alice\", \"Bob\", and \"Charlie\"\n\n");
        printf("Access:\n");
        printf("  display items[0]\n");
        printf("  display the 1st item in items       (1-based)\n");
        printf("  display item at index 2 in items\n");
        printf("  display the first item of items\n");
        printf("  display the last item of items\n\n");
        printf("Modify:\n");
        printf("  add 6 to items\n");
        printf("  append 7 to items\n");
        printf("  remove item 0 from items\n\n");
        printf("Query:\n");
        printf("  display length of items\n");
        printf("  display the number of items in items\n");
        printf("  display items contains 3\n");
        printf("  display whether items contains 3\n");
        printf("  display items between 1 and 3 of items    (slice)\n\n");
        printf("Iterate:\n");
        printf("  for item in items, display item.\n");
        printf("  display every item in items\n\n");
    }

    if (all || strcmp(topic, "maps") == 0) {
        printf("\n--- Maps / Dictionaries ---\n\n");
        printf("Maps use string keys:\n");
        printf("  person is {\"name\": \"Alice\", \"age\": 30}\n");
        printf("  display person[\"name\"]\n");
        printf("  display length of person       (number of entries)\n\n");
    }

    if (all || strcmp(topic, "errors") == 0) {
        printf("\n--- Error handling ---\n\n");
        printf("Basic try/catch:\n");
        printf("  try\n");
        printf("      raise \"something went wrong\"\n");
        printf("  catch error\n");
        printf("      display \"Got: {error}\".\n\n");
        printf("Natural forms:\n");
        printf("  try to\n");
        printf("      DANGEROUS_THING\n");
        printf("  but if it fails then\n");
        printf("      HANDLE.\n\n");
        printf("  attempt to THING catch error, display \"oops: {error}\".\n\n");
        printf("Raise variants: 'raise', 'throw', 'fail with', 'report error'\n\n");
    }

    if (all || strcmp(topic, "io") == 0) {
        printf("\n--- Input and output ---\n\n");
        printf("Output:\n");
        printf("  display \"hello\"   (also: print, show, say)\n\n");
        printf("Input:\n");
        printf("  name is input    (read a line from stdin)\n");
        printf("  name is ask \"What's your name? \"\n");
        printf("  name is input(\"What's your name? \")\n\n");
    }

    if (all || strcmp(topic, "sugar") == 0) {
        printf("\n--- Natural-language sugar ---\n\n");
        printf("Esu has dozens of ways to say the same thing:\n\n");
        printf("  • Articles: 'the length of x' works the same as 'length of x'\n");
        printf("  • Possessive: x's length = length of x\n");
        printf("  • Types: type of x  =  the type of x  =  x's type\n");
        printf("  • One-liners: combine statements with commas, end with period:\n");
        printf("      to double with x, return x * 2.\n");
        printf("      when x > 5, display \"big\", otherwise display \"small\".\n\n");
        printf("  • Hyphenated names: function calculate-total taking items ...\n");
        printf("  • Ordinals: 1st, 2nd, 3rd, 5th... usable in list access\n\n");
        printf("There's no single \"right\" way — write what feels natural.\n\n");
    }
}

static int run_repl(void) {
    const char *esu_dir = getenv("ESU_HOME");
    if (!esu_dir) esu_dir = ".";

    /* Load history from ~/.esu_history if available */
    const char *home = getenv("HOME");
    char history_path[1024];
    if (home) {
        snprintf(history_path, sizeof(history_path), "%s/.esu_history", home);
        linenoiseHistoryLoad(history_path);
    } else {
        history_path[0] = '\0';
    }
    linenoiseHistorySetMaxLen(1000);
    /* Allow multi-line editing within a single linenoise call */
    linenoiseSetMultiLine(1);

    printf("Esu REPL — type :help for commands, :quit to exit\n");
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

    const char *tmp_bin = "/tmp/esu_repl_tmp";
    const char *tmp_out = "/tmp/esu_repl_output";

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
                printf("  :help, :h               Show this help\n");
                printf("  :quit, :q               Exit REPL\n");
                printf("  :clear, :c              Reset session (forget all definitions)\n");
                printf("  :show                   Print the full accumulated program\n");
                printf("  :explain [topic]        Esu language tutorial\n");
                printf("\n");
                printf("Multi-line input: start a block (if/while/function/try) and\n");
                printf("the prompt changes to '..' until the block is closed with 'end' or '.'\n");
                printf("\n");
                free(line);
                continue;
            }
            if (strncmp(line, ":explain", 8) == 0) {
                const char *topic = line + 8;
                while (*topic == ' ' || *topic == '\t') topic++;
                print_explain(*topic ? topic : NULL);
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
        int result = compile_esu_src(candidate, tmp_bin, 0, 1);

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
                int r = compile_esu_src(accumulated, tmp_bin, 0, 1);
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
        return compile_esu(source_path, NULL, 1);
    }

    if (strcmp(command, "build") == 0) {
        /* Determine output name */
        const char *output = "output";
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            output = argv[4];
        } else {
            /* Derive from source filename: hello.esu -> hello */
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
        return compile_esu(source_path, output, 0);
    }

    if (strcmp(command, "run") == 0) {
        const char *tmp_output = "/tmp/esu_run_tmp";
        int result = compile_esu(source_path, tmp_output, 0);
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
