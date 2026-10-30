#ifndef SHIM_PP_H
#define SHIM_PP_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef enum {
    PP_OK = 0,
    PP_ERR = 1
} pp_result_t;
typedef struct pp_vtable {
    pp_result_t (*init)(void *params);
    pp_result_t (*deinit)(void *params);
    pp_result_t (*process)(void *params, void *in, void *out);
} pp_vtable_t;
#endif
