#ifndef APP_INSTALLER_H
#define APP_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "app_admission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_INSTALL_PUBLISHER_SLOT APP_ADMISSION_PUBLISHER_ID_SLOT
#define APP_INSTALL_APP_SLOT APP_ADMISSION_APP_ID_SLOT
#define APP_INSTALL_SHA256_LEN APP_ADMISSION_SHA256_LEN
#define APP_INSTALL_PATH_MAX 96u
#define APP_INSTALL_PATH_SLOT 96u
#define APP_INSTALL_RECORD_BIN_SIZE 296u
#define APP_INSTALL_PENDING_BIN_SIZE 312u
#define APP_INSTALL_KEY_RECORD "app_install_record"
#define APP_INSTALL_KEY_PENDING "app_install_pending"
#define APP_INSTALL_KEY_UNINSTALL "app_uninstall_intent"
#define APP_INSTALL_UNINSTALL_BIN_SIZE 312u
#define APP_INSTALL_DIR "/apps"
#define APP_INSTALL_PKG_DIR "/apps/pkg"
#define APP_INSTALL_MIN_FREE_MULTIPLIER 2u

typedef enum {
    APP_INSTALL_OK = 0,
    APP_INSTALL_OK_ALREADY = 1,
    APP_INSTALL_ERR_PARAM = -1,
    APP_INSTALL_ERR_BUSY = -2,
    APP_INSTALL_ERR_BAD_PACKAGE = -3,
    APP_INSTALL_ERR_SIGNATURE_INVALID = -4,
    APP_INSTALL_ERR_PUBLISHER_UNTRUSTED = -5,
    APP_INSTALL_ERR_TARGET_INCOMPATIBLE = -6,
    APP_INSTALL_ERR_ABI_INCOMPATIBLE = -7,
    APP_INSTALL_ERR_RESOURCE_LIMIT = -8,
    APP_INSTALL_ERR_TRUST_UNPROVEN = -9,
    APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN = -10,
    APP_INSTALL_ERR_STORAGE_FULL = -11,
    APP_INSTALL_ERR_WRITE_FAILED = -12,
    APP_INSTALL_ERR_COMMIT_FAILED = -13,
    APP_INSTALL_ERR_VERSION_CONFLICT = -14,
    APP_INSTALL_ERR_DOWNGRADE_REFUSED = -15,
    APP_INSTALL_ERR_PUBLISHER_CONFLICT = -16,
    APP_INSTALL_ERR_IDENTITY_CONFLICT = -17,
    APP_INSTALL_ERR_NOT_FOUND = -18,
    APP_INSTALL_ERR_UNAVAILABLE = -19,
    APP_INSTALL_ERR_LEGACY_STATE = -20
} app_install_result_t;

typedef enum {
    APP_INSTALL_STATE_NONE = 0,
    APP_INSTALL_STATE_INSTALLED = 1,
    APP_INSTALL_STATE_UNAVAILABLE = 2
} app_install_state_t;

typedef struct {
    char publisher_id[APP_INSTALL_PUBLISHER_SLOT + 1];
    char app_id[APP_INSTALL_APP_SLOT + 1];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t package_len;
    uint8_t package_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t tbs_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t image_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t publisher_key_sha256[APP_INSTALL_SHA256_LEN];
    char package_path[APP_INSTALL_PATH_MAX];
} app_install_record_t;

typedef enum {
    APP_INSTALL_IO_OK = 0,
    APP_INSTALL_IO_NOT_FOUND = 1,
    APP_INSTALL_IO_ERROR = 2
} app_install_io_result_t;

typedef struct {
    void *ctx;
    app_install_io_result_t (*file_write)(void *ctx, const char *path,
                                          const uint8_t *data, size_t len);
    app_install_io_result_t (*file_read)(void *ctx, const char *path,
                                         uint8_t *buf, size_t cap, size_t *len);
    app_install_io_result_t (*file_remove)(void *ctx, const char *path);
    app_install_io_result_t (*file_stat)(void *ctx, const char *path, size_t *len);
    app_install_io_result_t (*kv_write)(void *ctx, const char *key,
                                        const uint8_t *data, size_t len);
    app_install_io_result_t (*kv_read)(void *ctx, const char *key,
                                       uint8_t *buf, size_t cap, size_t *len);
    app_install_io_result_t (*kv_delete)(void *ctx, const char *key);
    app_install_io_result_t (*free_bytes)(void *ctx, uint64_t *out);
    aicam_bool_t (*tx_begin)(void *ctx);
    void (*tx_end)(void *ctx);
    aicam_bool_t (*tx_begin_read)(void *ctx);
    void *(*big_alloc)(void *ctx, size_t len);
    void (*big_free)(void *ctx, void *ptr);
} app_install_io_t;

typedef app_admission_result_t (*app_install_verify_fn)(const uint8_t *data,
                                                        size_t len,
                                                        app_admission_verdict_t *out,
                                                        void *ctx);

typedef struct {
    app_install_state_t state;
    app_install_record_t record;
    char reason[48];
} app_install_query_t;

typedef enum {
    APP_UNINSTALL_STATE_NONE = 0,
    APP_UNINSTALL_STATE_CLEANUP_PENDING = 1,
    APP_UNINSTALL_STATE_UNKNOWN = 2
} app_uninstall_state_t;

typedef enum {
    APP_UNINSTALL_PURGE_CONFIRMED = 0,
    APP_UNINSTALL_PURGE_UNKNOWN = 1
} app_uninstall_purge_result_t;

typedef enum {
    APP_UNINSTALL_OK = 0,
    APP_UNINSTALL_CANCELLED = 1,
    APP_UNINSTALL_CLEANUP_PENDING = 2,
    APP_UNINSTALL_ERR_PARAM = -1,
    APP_UNINSTALL_ERR_BUSY = -2,
    APP_UNINSTALL_ERR_NOT_FOUND = -3
} app_uninstall_result_t;

typedef app_uninstall_purge_result_t (*app_uninstall_external_purge_fn)(
    void *ctx,
    const app_install_record_t *record);

typedef struct {
    void *ctx;
    app_uninstall_external_purge_fn purge;
} app_uninstall_cleanup_ops_t;

aicam_bool_t app_install_record_encode(const app_install_record_t *record,
                                       uint8_t *out, size_t cap);

aicam_bool_t app_install_record_decode(const uint8_t *data, size_t len,
                                       app_install_record_t *out);

app_install_result_t app_install(const app_install_io_t *io,
                                 app_install_verify_fn verify, void *verify_ctx,
                                 const uint8_t *package, size_t len,
                                 app_install_record_t *out_record);

app_install_result_t app_install_uninstall(const app_install_io_t *io,
                                           app_install_record_t *out_removed);

app_uninstall_result_t app_install_uninstall_confirmed(
    const app_install_io_t *io,
    aicam_bool_t confirmed,
    const app_uninstall_cleanup_ops_t *cleanup_ops,
    app_install_record_t *out_removed,
    app_uninstall_state_t *out_state);

app_install_result_t app_install_uninstall_status(
    const app_install_io_t *io,
    app_uninstall_state_t *out_state,
    app_install_record_t *out_record);

const app_uninstall_cleanup_ops_t *app_installer_device_cleanup_ops(void);

app_install_result_t app_install_query(const app_install_io_t *io,
                                       app_install_verify_fn verify, void *verify_ctx,
                                       app_install_query_t *out);

app_install_result_t app_install_query_committed(const app_install_io_t *io,
                                                 app_install_record_t *out,
                                                 aicam_bool_t *present);

app_install_result_t app_install_boot_resolve(const app_install_io_t *io);

void app_install_boot_resolve_reset(void);

void app_install_set_app_running(aicam_bool_t running);

aicam_bool_t app_install_app_running(void);

const app_install_io_t *app_installer_device_io(void);

aicam_result_t app_installer_storage_init(void);

const char *app_install_result_str(app_install_result_t result);

const char *app_install_state_str(app_install_state_t state);

#ifdef __cplusplus
}
#endif

#endif
