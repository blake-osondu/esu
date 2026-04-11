#ifndef NORMALIZER_H
#define NORMALIZER_H

#include "token.h"

/*
 * The normalizer rewrites a token stream, collapsing sugar patterns
 * into canonical forms so the parser only handles one grammar.
 *
 * Example rewrites:
 *   "let" IDENT "be"       → IDENT "is"
 *   "set" IDENT "to"       → IDENT "is"
 *   "plus"                  → +
 *   "minus"                 → -
 *   "times"                 → *
 *   "divided" "by"          → /
 *   "modulo" / "mod"        → %
 *   "is" "greater" "than"   → >
 *   "is" "less" "than"      → <
 *   "is" "equal" "to"       → ==
 *   "equals"                → ==
 *   "is" "not" "equal" "to" → !=
 *   "is" "at" "least"       → >=
 *   "is" "at" "most"        → <=
 *   "is" "now"              → "is"
 *   "change" IDENT "to"     → IDENT "is"
 *   "update" IDENT "to"     → IDENT "is"
 *   "define" IDENT "as"     → IDENT "is"
 */

/* Takes ownership of input tokens array.
 * Returns a new normalized token array.
 * Sets *out_count to the number of tokens in the result. */
Token *normalizer_run(Token *tokens, int count, int *out_count);

#endif
