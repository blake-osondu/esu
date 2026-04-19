#include "module.h"
#include "lexer.h"
#include "normalizer.h"
#include "parser.h"
#include "resolver.h"
#include "error.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_MODULES 128

typedef struct {
    char *resolved_path;    /* Absolute path to the .esu file */
    ASTNode *ast;           /* Parsed program AST */
    int loading;            /* 1 if currently being loaded (circular detect) */
} ModuleEntry;

static ModuleEntry modules[MAX_MODULES];
static int module_count = 0;
static char base_dir[4096] = ".";

void module_init(void) {
    module_count = 0;
    base_dir[0] = '.';
    base_dir[1] = '\0';
}

void module_set_base_dir(const char *dir) {
    strncpy(base_dir, dir, sizeof(base_dir) - 1);
    base_dir[sizeof(base_dir) - 1] = '\0';
}

static ModuleEntry *find_module(const char *resolved) {
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].resolved_path, resolved) == 0) {
            return &modules[i];
        }
    }
    return NULL;
}

/* Try to resolve a module path to an actual file. Returns malloc'd string or NULL. */
static char *resolve_path(const char *module_path) {
    char buf[4096];

    /* If it already has .esu extension, use as-is */
    int has_ext = (strlen(module_path) > 4 &&
                   strcmp(module_path + strlen(module_path) - 4, ".esu") == 0);

    /* 1. Relative to base_dir: ./NAME.esu */
    if (has_ext) {
        snprintf(buf, sizeof(buf), "%s/%s", base_dir, module_path);
    } else {
        snprintf(buf, sizeof(buf), "%s/%s.esu", base_dir, module_path);
    }
    if (access(buf, F_OK) == 0) {
        char *result = malloc(strlen(buf) + 1);
        strcpy(result, buf);
        return result;
    }

    /* 2. Directory: ./NAME/main.esu */
    if (!has_ext) {
        snprintf(buf, sizeof(buf), "%s/%s/main.esu", base_dir, module_path);
        if (access(buf, F_OK) == 0) {
            char *result = malloc(strlen(buf) + 1);
            strcpy(result, buf);
            return result;
        }
    }

    /* 3. ESU_HOME/stdlib/NAME.esu */
    const char *esu_home = getenv("ESU_HOME");
    if (esu_home) {
        if (has_ext) {
            snprintf(buf, sizeof(buf), "%s/stdlib/%s", esu_home, module_path);
        } else {
            snprintf(buf, sizeof(buf), "%s/stdlib/%s.esu", esu_home, module_path);
        }
        if (access(buf, F_OK) == 0) {
            char *result = malloc(strlen(buf) + 1);
            strcpy(result, buf);
            return result;
        }
    }

    /* 4. Try absolute path as given */
    if (module_path[0] == '/' || module_path[0] == '~') {
        if (access(module_path, F_OK) == 0) {
            char *result = malloc(strlen(module_path) + 1);
            strcpy(result, module_path);
            return result;
        }
    }

    return NULL;
}

/* Tokenize source into array (same as main.c's tokenize) */
static Token *tokenize_source(const char *source, int *out_count) {
    Lexer lex;
    lexer_init(&lex, source);
    int cap = 256;
    Token *tokens = malloc(sizeof(Token) * cap);
    int count = 0;
    for (;;) {
        if (count >= cap) { cap *= 2; tokens = realloc(tokens, sizeof(Token) * cap); }
        tokens[count] = lexer_next(&lex);
        if (tokens[count].type == TOK_EOF) { count++; break; }
        count++;
    }
    *out_count = count;
    return tokens;
}

ASTNode *module_load(const char *module_path) {
    /* Resolve the path */
    char *resolved = resolve_path(module_path);
    if (!resolved) {
        fprintf(stderr, "Error: cannot find module '%s'\n", module_path);
        fprintf(stderr, "  Searched: %s/%s.esu\n", base_dir, module_path);
        const char *esu_home = getenv("ESU_HOME");
        if (esu_home) {
            fprintf(stderr, "  Searched: %s/stdlib/%s.esu\n", esu_home, module_path);
        }
        return NULL;
    }

    /* Check cache */
    ModuleEntry *existing = find_module(resolved);
    if (existing) {
        if (existing->loading) {
            fprintf(stderr, "Error: circular import detected for '%s'\n", module_path);
            free(resolved);
            return NULL;
        }
        free(resolved);
        return existing->ast;
    }

    /* Register as loading (for circular detection) */
    if (module_count >= MAX_MODULES) {
        fprintf(stderr, "Error: too many modules loaded (max %d)\n", MAX_MODULES);
        free(resolved);
        return NULL;
    }
    int idx = module_count++;
    modules[idx].resolved_path = resolved;
    modules[idx].ast = NULL;
    modules[idx].loading = 1;

    /* Read file */
    char *source = read_file(resolved);

    /* Tokenize */
    int token_count;
    Token *tokens = tokenize_source(source, &token_count);

    /* Normalize */
    tokens = normalizer_run(tokens, token_count, &token_count);

    /* Parse */
    Parser parser;
    parser_init(&parser, tokens, token_count);
    ASTNode *program = parser_parse(&parser);

    /* Resolve (verb conjugation) */
    resolver_run(program);

    /* Cache the result */
    modules[idx].ast = program;
    modules[idx].loading = 0;

    /* Clean up tokens (parser doesn't own them) */
    for (int i = 0; i < token_count; i++) token_free(&tokens[i]);
    free(tokens);
    free(source);

    return program;
}

int module_is_loaded(const char *module_path) {
    char *resolved = resolve_path(module_path);
    if (!resolved) return 0;
    int found = (find_module(resolved) != NULL);
    free(resolved);
    return found;
}
