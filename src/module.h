#ifndef MODULE_H
#define MODULE_H

#include "ast.h"

/*
 * Module resolver: loads and parses .esu files for import.
 *
 * Resolution order for "use NAME":
 *   1. ./NAME.esu (relative to importing file)
 *   2. ./NAME/main.esu (directory with main file)
 *   3. $ESU_HOME/stdlib/NAME.esu (standard library)
 *
 * Returns a parsed AST (program node) for the module, or NULL on error.
 * Caches results to avoid double-loading. Detects circular imports.
 */

/* Initialize the module system (call once at startup) */
void module_init(void);

/* Set the base directory for resolving relative imports */
void module_set_base_dir(const char *dir);

/* Resolve and parse a module. Returns its AST program node.
 * The caller should NOT free the returned AST (it's cached). */
ASTNode *module_load(const char *module_path);

/* Check if a module has already been loaded */
int module_is_loaded(const char *module_path);

#endif
