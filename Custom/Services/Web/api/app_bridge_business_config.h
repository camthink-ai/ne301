#ifndef APP_BRIDGE_BUSINESS_CONFIG_H
#define APP_BRIDGE_BUSINESS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_BRIDGE_LC_CFG_SIZE 144u

aicam_bool_t app_bridge_business_config_encode(const cJSON *candidate,
                                               uint8_t *out, size_t out_cap);

uint32_t app_bridge_business_config_conf_permille(const uint8_t *blob,
                                                  size_t len);

char *app_bridge_business_snapshot_readthrough(uint8_t *payload_buf,
                                               uint32_t payload_len,
                                               uint32_t config_generation);

void app_bridge_business_snapshot_target_class(const char *json, char *out,
                                               size_t cap, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif
