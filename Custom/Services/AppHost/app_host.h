#ifndef APP_HOST_H
#define APP_HOST_H

#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
