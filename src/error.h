#ifndef ERROR_H
#define ERROR_H

#include "token.h"

void babel_error(int line, int col, const char *fmt, ...);
void babel_error_at_token(Token *tok, const char *fmt, ...);

#endif
