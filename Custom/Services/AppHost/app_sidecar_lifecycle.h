#ifndef APP_SIDECAR_LIFECYCLE_H
#define APP_SIDECAR_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

#include "app_installer.h"
#include "app_pair_history.h"
#include "app_sidecar_verify.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A "app_sidecar_active_a"
#define APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B "app_sidecar_active_b"
#define APP_SIDECAR_LIFECYCLE_KEY_STAGING "app_sidecar_staging"
#define APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR "app_sidecar_pair_floor"
#define APP_SIDECAR_LIFECYCLE_RECORD_SIZE 272u
#define APP_SIDECAR_LIFECYCLE_STAGE_SIZE 560u
#define APP_SIDECAR_LIFECYCLE_BUNDLE_MAX 2113624u
#define APP_SIDECAR_LIFECYCLE_GC_RESERVE 65536u
#define APP_SIDECAR_LIFECYCLE_FREE_MULTIPLIER 2u
#define APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX 96u

typedef enum {
    APP_SIDECAR_LIFECYCLE_NONE = 0,
    APP_SIDECAR_LIFECYCLE_CANDIDATE_STAGED = 1,
    APP_SIDECAR_LIFECYCLE_VERIFIED = 2,
    APP_SIDECAR_LIFECYCLE_ACTIVE = 3,
    APP_SIDECAR_LIFECYCLE_UNAVAILABLE = 4,
    APP_SIDECAR_LIFECYCLE_UNKNOWN = 5
} app_sidecar_lifecycle_state_t;

typedef enum {
    APP_SIDECAR_LIFECYCLE_OK = 0,
    APP_SIDECAR_LIFECYCLE_ALREADY = 1,
    APP_SIDECAR_LIFECYCLE_CLEANUP_PENDING = 2,
    APP_SIDECAR_LIFECYCLE_ERR_PARAM = -1,
    APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED = -2,
    APP_SIDECAR_LIFECYCLE_ERR_VERIFY = -3,
    APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN = -4,
    APP_SIDECAR_LIFECYCLE_ERR_STORAGE_FULL = -5,
    APP_SIDECAR_LIFECYCLE_ERR_VERSION_CONFLICT = -6,
    APP_SIDECAR_LIFECYCLE_ERR_DOWNGRADE = -7,
    APP_SIDECAR_LIFECYCLE_ERR_BUSY = -8,
    APP_SIDECAR_LIFECYCLE_ERR_IO = -9
} app_sidecar_lifecycle_result_t;

typedef struct {
    char path[APP_SIDECAR_PATH_MAX + 1u];
    char storage_path[APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX];
    uint32_t file_len;
    uint8_t sha256[APP_SIDECAR_SHA256_LEN];
} app_sidecar_asset_t;

typedef struct {
    app_sidecar_lifecycle_state_t state;
    char publisher_id[APP_SIDECAR_VERDICT_ID_SLOT];
    char app_id[APP_SIDECAR_VERDICT_APP_SLOT];
    uint8_t publisher_key_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t native_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t ui_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t pair_tbs_sha256[APP_PAIR_TBS_SHA256_LEN];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t host_ui_abi;
    uint16_t native_version[3];
    uint64_t release_seq;
    uint64_t service_generation;
    uint32_t bundle_len;
    uint32_t manifest_len;
    uint32_t asset_payload_len;
    uint16_t file_count;
    app_sidecar_asset_t files[APP_SIDECAR_FILE_COUNT_MAX];
} app_sidecar_descriptor_t;

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_install(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    const uint8_t *bundle,
    size_t bundle_len,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state);

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_install_pair(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    const uint8_t *bundle,
    size_t bundle_len,
    const app_pair_history_t *pair_history,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state);

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_pair_history_check(
    const app_install_io_t *io,
    const app_pair_history_t *candidate);

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_pair_history_query(
    const app_install_io_t *io,
    app_pair_history_t *out,
    aicam_bool_t *present);

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_unsigned_preflight(
    const app_install_io_t *io,
    const app_install_record_t *native_record);

aicam_bool_t app_sidecar_lifecycle_pair_authorized(
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    const app_sidecar_descriptor_t *descriptor);

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_query(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state);

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_boot_resolve(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state);

void app_sidecar_lifecycle_reset(void);

const char *app_sidecar_lifecycle_result_str(app_sidecar_lifecycle_result_t result);

const char *app_sidecar_lifecycle_state_str(app_sidecar_lifecycle_state_t state);

#ifdef __cplusplus
}
#endif

#endif
