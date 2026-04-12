#ifndef BABEL_RUNTIME_H
#define BABEL_RUNTIME_H

#include "babel_mem.h"
#include "babel_value.h"
#include "babel_io.h"
#include "babel_ops.h"
#include "babel_string.h"
#include "babel_list.h"
#include "babel_map.h"
#include "babel_error.h"
#include "babel_math.h"
#include "babel_file.h"

void babel_runtime_init(void);
void babel_runtime_cleanup(void);

#endif
