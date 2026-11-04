#ifndef HOST_SHIM_HAL_MEM_H
#define HOST_SHIM_HAL_MEM_H
#include <stdlib.h>
typedef enum { MEM_FAST = 0 } hal_mem_type_t;
static inline void *hal_mem_alloc_aligned(size_t sz, unsigned align, hal_mem_type_t t) { (void)align; (void)t; return malloc(sz); }
static inline void hal_mem_free(void *p) { free(p); }
#endif
