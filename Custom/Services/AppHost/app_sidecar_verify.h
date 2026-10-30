#ifndef APP_SIDECAR_VERIFY_H
#define APP_SIDECAR_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SIDECAR_MAGIC_SIZE 8u
#define APP_SIDECAR_HEADER_SIZE 16u
#define APP_SIDECAR_MANIFEST_FIXED_SIZE 128u
#define APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE 136u
#define APP_SIDECAR_PUBLISHER_ID_SLOT 16u
#define APP_SIDECAR_APP_ID_SLOT 32u
#define APP_SIDECAR_SHA256_LEN 32u
#define APP_SIDECAR_RECORD_FIXED_SIZE 38u
#define APP_SIDECAR_PATH_MAX 160u
#define APP_SIDECAR_FILE_COUNT_MAX 64u
#define APP_SIDECAR_FILE_LEN_MAX 1048576u
#define APP_SIDECAR_MANIFEST_LEN_MAX 16384u
#define APP_SIDECAR_ASSET_PAYLOAD_MAX 2097152u
#define APP_SIDECAR_DER_LEN_MIN 8u
#define APP_SIDECAR_DER_LEN_MAX 72u
#define APP_SIDECAR_HOST_UI_ABI 1u
#define APP_SIDECAR_VERSION_MAJOR_MIN 1u
#define APP_SIDECAR_SPKI_P256_LEN 91u
#define APP_SIDECAR_VERDICT_ID_SLOT (APP_SIDECAR_PUBLISHER_ID_SLOT + 1u)
#define APP_SIDECAR_VERDICT_APP_SLOT (APP_SIDECAR_APP_ID_SLOT + 1u)

typedef enum {
    APP_SIDECAR_OK = 0,
    APP_SIDECAR_ERR_PARAM = -1,
    APP_SIDECAR_ERR_NO_TRUST = -2,
    APP_SIDECAR_ERR_BAD_FORMAT = -3,
    APP_SIDECAR_ERR_LIMIT = -4,
    APP_SIDECAR_ERR_ABI_INCOMPATIBLE = -5,
    APP_SIDECAR_ERR_IDENTITY_MISMATCH = -6,
    APP_SIDECAR_ERR_ASSET_INVALID = -7,
    APP_SIDECAR_ERR_SIGNATURE_INVALID = -8,
    APP_SIDECAR_ERR_LEGACY_SIGNED_FORMAT = -9,
    APP_SIDECAR_ERR_UNKNOWN_FORMAT = -10
} app_sidecar_result_t;

typedef struct {
    const char *publisher_id;
    size_t publisher_id_len;
    const char *app_id;
    size_t app_id_len;
    const uint8_t *spki_der;
    size_t spki_der_len;
    const uint8_t *native_tbs_sha256;
} app_sidecar_trust_t;

typedef struct {
    uint32_t file_len;
    uint8_t sha256[APP_SIDECAR_SHA256_LEN];
    char path[APP_SIDECAR_PATH_MAX + 1];
} app_sidecar_file_desc_t;

typedef void (*app_sidecar_file_fn)(void *user, const app_sidecar_file_desc_t *desc);

typedef struct {
    char publisher_id[APP_SIDECAR_VERDICT_ID_SLOT];
    char app_id[APP_SIDECAR_VERDICT_APP_SLOT];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t host_ui_abi;
    uint16_t file_count;
    uint32_t manifest_len;
    uint32_t asset_payload_len;
    uint8_t signature_len;
    uint16_t native_version[3];
    uint8_t ui_tbs_sha256[APP_SIDECAR_SHA256_LEN];
} app_sidecar_verdict_t;

app_sidecar_result_t app_sidecar_verify_bundle(const uint8_t *data,
                                               size_t len,
                                               const app_sidecar_trust_t *trust,
                                               app_sidecar_file_fn file_cb,
                                               void *user,
                                               app_sidecar_verdict_t *out);

const char *app_sidecar_result_str(app_sidecar_result_t result);

#ifdef __cplusplus
}
#endif

#endif
