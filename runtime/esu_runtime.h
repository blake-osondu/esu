#ifndef ESU_RUNTIME_H
#define ESU_RUNTIME_H

#include "esu_mem.h"
#include "esu_value.h"
#include "esu_io.h"
#include "esu_ops.h"
#include "esu_string.h"
#include "esu_list.h"
#include "esu_map.h"
#include "esu_error.h"
#include "esu_math.h"
#include "esu_file.h"
#include "esu_method.h"
#include "esu_json.h"
#include "esu_http.h"

void esu_runtime_init(void);
void esu_runtime_cleanup(void);

#endif
