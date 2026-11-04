#ifndef APP_STATE_STORE_H
#define APP_STATE_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"
#include "app_installer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_STATE_OK 0
#define APP_STATE_NOT_FOUND 2
#define APP_STATE_INVALID_ARGUMENT (-1)
#define APP_STATE_BUFFER_TOO_SMALL (-2)
#define APP_STATE_UNAUTHORIZED (-3)
#define APP_STATE_QUOTA_EXCEEDED (-5)
#define APP_STATE_STORAGE_UNKNOWN (-6)
#define APP_STATE_BUSY (-7)
#define APP_STATE_REVISION_CONFLICT (-8)
#define APP_STATE_IO_ERROR (-9)

#define APP_STATE_SHA256_LEN APP_INSTALL_SHA256_LEN
#define APP_STATE_PUB_SLOT APP_INSTALL_PUBLISHER_SLOT
#define APP_STATE_APP_SLOT APP_INSTALL_APP_SLOT
#define APP_STATE_REVISION_NONE 0u

typedef struct {
    uint8_t spki_sha256[APP_STATE_SHA256_LEN];
    char publisher_id[APP_STATE_PUB_SLOT + 1];
    char app_id[APP_STATE_APP_SLOT + 1];
} app_state_identity_t;

typedef struct {
    const app_install_io_t *io;
    app_state_identity_t identity;
    uint32_t quota;
    aicam_bool_t authorized;
} app_state_session_t;

int32_t app_state_session_bind(app_state_session_t *session,
                               const app_install_io_t *io,
                               const app_state_identity_t *identity,
                               uint32_t quota);

void app_state_session_end(app_state_session_t *session);

int32_t app_state_read(app_state_session_t *session,
                       void *out, uint32_t cap,
                       uint32_t *actual_len, uint32_t *out_revision);

int32_t app_state_commit(app_state_session_t *session,
                         const void *blob, uint32_t len,
                         uint32_t expected_revision, uint32_t *new_revision);

int32_t app_state_store_recover(const app_install_io_t *io,
                                const app_state_identity_t *identity);

int32_t app_state_store_purge_locked(const app_install_io_t *io,
                                     const app_state_identity_t *identity);

const char *app_state_result_str(int32_t result);

#ifdef __cplusplus
}
#endif

#endif
