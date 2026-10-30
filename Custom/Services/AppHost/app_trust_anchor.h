#ifndef APP_TRUST_ANCHOR_H
#define APP_TRUST_ANCHOR_H

#include <stddef.h>
#include <stdint.h>

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_TRUST_ANCHOR_FORMAT_VERSION 1u
#define APP_TRUST_PROFILE_TEST 1
#define APP_TRUST_PROFILE_PRODUCTION 2
#define APP_TRUST_PROVENANCE_CLASS_PRODUCTION 1
#define APP_TRUST_PROVENANCE_CLASS_SYNTHETIC 2
#define APP_TRUST_SYNTHETIC_PROVENANCE_PREFIX "SYNTHETIC_TEST_ONLY"

#ifndef APP_TRUST_PROFILE
#define APP_TRUST_PROFILE APP_TRUST_PROFILE_TEST
#endif

#if APP_TRUST_PROFILE != APP_TRUST_PROFILE_TEST && APP_TRUST_PROFILE != APP_TRUST_PROFILE_PRODUCTION
#error APP_TRUST_PROFILE must select test or production
#endif

typedef enum {
    APP_TRUST_ANCHOR_PROVISIONED = 0,
    APP_TRUST_ANCHOR_UNPROVISIONED = -1,
    APP_TRUST_ANCHOR_MISSING = APP_TRUST_ANCHOR_UNPROVISIONED,
    APP_TRUST_ANCHOR_MALFORMED = -2
} app_trust_anchor_status_t;

typedef struct {
    uint32_t format_version;
    const char *publisher_id;
    size_t publisher_id_len;
    const uint8_t *spki_der;
    size_t spki_der_len;
    const uint8_t *spki_sha256;
} app_trust_anchor_t;

const app_trust_anchor_t *app_trust_anchor_get(void);

app_trust_anchor_status_t app_trust_anchor_status(void);

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status);

const char *app_trust_anchor_profile_str(void);

const char *app_trust_anchor_provenance_str(void);

const char *app_trust_anchor_spki_sha256_hex(void);

#ifdef __cplusplus
}
#endif

#endif
