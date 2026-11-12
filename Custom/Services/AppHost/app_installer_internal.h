#ifndef APP_INSTALLER_INTERNAL_H
#define APP_INSTALLER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "app_installer.h"
#include "app_sidecar_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal seam between app_installer.c and app_sidecar_lifecycle.c only.
 * These symbols are not part of the public AppHost API.
 */

/* Persisted install record / pending-intent blob layout (offsets in bytes). */
#define APP_INSTALL_REC_HDR_SIZE 8u
#define APP_INSTALL_REC_PUB_OFF 8u
#define APP_INSTALL_REC_APP_OFF 25u
#define APP_INSTALL_REC_VER_OFF 58u
#define APP_INSTALL_REC_LEN_OFF 64u
#define APP_INSTALL_REC_SHAS_OFF 68u
#define APP_INSTALL_REC_PATH_OFF 196u
#define APP_INSTALL_REC_PATH_SLOT APP_INSTALL_PATH_SLOT
#define APP_INSTALL_REC_CRC_OFF (APP_INSTALL_REC_PATH_OFF + APP_INSTALL_REC_PATH_SLOT)
#define APP_INSTALL_RECORD_BLOB_SIZE (APP_INSTALL_REC_CRC_OFF + 4u)
#define APP_INSTALL_PEND_ACT_OFF 4u
#define APP_INSTALL_PEND_HAS_PREV_OFF 8u
#define APP_INSTALL_PEND_PREV_OFF 12u
#define APP_INSTALL_PEND_CRC_OFF (APP_INSTALL_PEND_PREV_OFF + APP_INSTALL_RECORD_BLOB_SIZE)
#define APP_INSTALL_PENDING_BLOB_SIZE (APP_INSTALL_PEND_CRC_OFF + 4u)

#if APP_INSTALL_RECORD_BIN_SIZE != APP_INSTALL_RECORD_BLOB_SIZE
#error "record blob size mismatch"
#endif
#if APP_INSTALL_PENDING_BIN_SIZE != APP_INSTALL_PENDING_BLOB_SIZE
#error "pending blob size mismatch"
#endif

/* Proven storage helpers owned by app_installer.c. */
void inst_sha256(const uint8_t *data, size_t len, uint8_t out[APP_INSTALL_SHA256_LEN]);

aicam_bool_t inst_kv_delete_proven(const app_install_io_t *io, const char *key,
                                   size_t blob_size);

aicam_bool_t inst_record_delete_proven(const app_install_io_t *io);

aicam_result_t inst_record_load(const app_install_io_t *io,
                                app_install_record_t *record,
                                aicam_bool_t *present);

/* Boot-resolve gates owned by app_sidecar_lifecycle.c. */
app_install_result_t inst_sidecar_revoke_for_native(const app_install_io_t *io,
                                                    const app_install_record_t *native);

app_install_result_t inst_uninstall_intent_blocks_boot(const app_install_io_t *io);

app_sidecar_lifecycle_result_t app_sidecar_staged_identity(
    const app_install_io_t *io,
    aicam_bool_t *out_staged,
    uint8_t out_native_tbs_sha256[APP_SIDECAR_SHA256_LEN],
    uint8_t out_bundle_sha256[APP_SIDECAR_SHA256_LEN]);

app_sidecar_lifecycle_result_t app_sidecar_active_pair_peek(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native,
    aicam_bool_t *out_active);

/* Boot state flags owned by app_installer.c (uninstall finalize path). */
aicam_bool_t app_install_boot_state_resolved(void);

void app_install_boot_state_clear(void);

#ifdef __cplusplus
}
#endif

#endif
