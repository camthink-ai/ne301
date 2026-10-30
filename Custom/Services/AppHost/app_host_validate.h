#ifndef APP_HOST_VALIDATE_H
#define APP_HOST_VALIDATE_H

#include <stddef.h>
#include <stdint.h>

#include "app_host_abi.h"

typedef enum {
    APP_HOST_CHECK_OK = 0,
    APP_HOST_CHECK_ERR_PARAM = -1,
    APP_HOST_CHECK_ERR_TRUNCATED = -2,
    APP_HOST_CHECK_ERR_MAGIC = -3,
    APP_HOST_CHECK_ERR_HEADER = -4,
    APP_HOST_CHECK_ERR_FORMAT = -5,
    APP_HOST_CHECK_ERR_ABI = -6,
    APP_HOST_CHECK_ERR_TARGET = -7,
    APP_HOST_CHECK_ERR_IMAGE_SIZE = -8,
    APP_HOST_CHECK_ERR_ENTRY = -9,
    APP_HOST_CHECK_ERR_CRC = -10
} app_host_check_t;

typedef struct {
    uint32_t expected_target_addr;
    uint32_t region_size;
    uint32_t abi_version;
} app_host_load_policy_t;

typedef struct {
    const uint8_t *entry_addr;
    uint32_t image_size;
    uint32_t entry_offset;
} app_host_image_info_t;

app_host_check_t app_host_validate(const uint8_t *image, size_t file_size, const app_host_load_policy_t *policy, app_host_image_info_t *out);

const char *app_host_check_str(app_host_check_t result);

#endif
