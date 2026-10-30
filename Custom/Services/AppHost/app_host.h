#ifndef APP_HOST_H
#define APP_HOST_H

#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_HOST_DEFAULT_IMAGE_PATH "/apps/apphost_test.bin"

int app_host_load_and_run(const char *path);

aicam_bool_t app_host_experiment_armed(void);

void app_host_experiment_set_armed(aicam_bool_t armed);

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size);

void app_host_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif
