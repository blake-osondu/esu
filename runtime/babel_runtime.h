#ifndef BABEL_RUNTIME_H
#define BABEL_RUNTIME_H

#include "babel_mem.h"
#include "babel_value.h"
#include "babel_io.h"
#include "babel_ops.h"
#include "babel_string.h"

void babel_runtime_init(void);
void babel_runtime_cleanup(void);

#endif
