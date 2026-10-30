#ifndef APP_PAIR_HISTORY_H
#define APP_PAIR_HISTORY_H

#include <stdint.h>

#include "app_installer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PAIR_TBS_SHA256_LEN 32u

typedef struct {
    char publisher_id[APP_INSTALL_PUBLISHER_SLOT + 1u];
    char app_id[APP_INSTALL_APP_SLOT + 1u];
    uint8_t publisher_key_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t native_tbs_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t ui_tbs_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t pair_tbs_sha256[APP_PAIR_TBS_SHA256_LEN];
    uint16_t native_version[3];
    uint16_t ui_version[3];
    uint64_t release_seq;
} app_pair_history_t;

typedef enum {
    APP_PAIR_HISTORY_FRESH = 0,
    APP_PAIR_HISTORY_IDEMPOTENT = 1,
    APP_PAIR_HISTORY_UPGRADE = 2,
    APP_PAIR_HISTORY_ERR_PARAM = -1,
    APP_PAIR_HISTORY_ERR_IDENTITY = -2,
    APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT = -3,
    APP_PAIR_HISTORY_ERR_DOWNGRADE = -4
} app_pair_history_result_t;

app_pair_history_result_t app_pair_history_check(
    const app_pair_history_t *previous,
    const app_pair_history_t *candidate);

#ifdef __cplusplus
}
#endif

#endif
