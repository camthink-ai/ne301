#ifndef APP_PAIR_INSTALLER_H
#define APP_PAIR_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "app_installer.h"
#include "app_sidecar_lifecycle.h"
#include "app_pair_history.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PAIR_MAX_NATIVE_LEN 2097448u
#define APP_PAIR_MAX_UI_LEN 2113552u
#define APP_PAIR_MAX_PACKAGE_LEN 4210944u
typedef struct {
    app_pair_history_t history;
    size_t native_offset;
    size_t native_len;
    size_t ui_offset;
    size_t ui_len;
    size_t pair_tbs_len;
    uint8_t package_sha256[APP_INSTALL_SHA256_LEN];
} app_pair_candidate_t;

typedef enum {
    APP_PAIR_INSTALL_OK = 0,
    APP_PAIR_INSTALL_ALREADY = 1,
    APP_PAIR_INSTALL_ERR_PARAM = -1,
    APP_PAIR_INSTALL_ERR_BAD_FORMAT = -2,
    APP_PAIR_INSTALL_ERR_NATIVE_VERIFY = -3,
    APP_PAIR_INSTALL_ERR_UI_VERIFY = -4,
    APP_PAIR_INSTALL_ERR_PAIR_SIGNATURE = -5,
    APP_PAIR_INSTALL_ERR_IDENTITY = -6,
    APP_PAIR_INSTALL_ERR_CONTENT_CONFLICT = -7,
    APP_PAIR_INSTALL_ERR_DOWNGRADE = -8,
    APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN = -9,
    APP_PAIR_INSTALL_ERR_STORAGE_FULL = -10,
    APP_PAIR_INSTALL_ERR_BUSY = -11,
    APP_PAIR_INSTALL_ERR_WRITE_FAILED = -12,
    APP_PAIR_INSTALL_ERR_COMMIT_FAILED = -13,
    APP_PAIR_INSTALL_ERR_UNAVAILABLE = -14,
    APP_PAIR_INSTALL_ERR_LEGACY_SIGNED_FORMAT = -15,
    APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED = -16,
    APP_PAIR_INSTALL_ERR_UNKNOWN_FORMAT = -17
} app_pair_install_result_t;

app_pair_history_result_t app_pair_history_check(
    const app_pair_history_t *previous,
    const app_pair_history_t *candidate);

app_pair_install_result_t app_pair_verify_package(
    const uint8_t *package,
    size_t package_len,
    app_install_verify_fn native_verify,
    void *native_verify_ctx,
    const app_sidecar_trust_t *trust,
    app_pair_candidate_t *out);

app_pair_install_result_t app_pair_install(
    const app_install_io_t *io,
    app_install_verify_fn native_verify,
    void *native_verify_ctx,
    const app_sidecar_trust_t *trust,
    const uint8_t *package,
    size_t package_len,
    app_install_record_t *out_native,
    app_sidecar_descriptor_t *out_ui,
    app_sidecar_lifecycle_state_t *out_state);

aicam_bool_t app_pair_is_authorized(
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    const app_sidecar_descriptor_t *descriptor);

const char *app_pair_install_result_str(app_pair_install_result_t result);

#ifdef __cplusplus
}
#endif

#endif
