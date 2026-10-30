#include "app_trust_anchor.h"

#include <string.h>

#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#include "app_admission.h"

#if APP_TRUST_PROFILE == APP_TRUST_PROFILE_PRODUCTION && defined(APP_TRUST_PROVISION_HEADER)
#include APP_TRUST_PROVISION_HEADER
#if defined(APP_TRUST_FIRMWARE_BUILD) && \
    defined(APP_TRUST_PRODUCTION_PROVENANCE_CLASS) && \
    APP_TRUST_PRODUCTION_PROVENANCE_CLASS != APP_TRUST_PROVENANCE_CLASS_PRODUCTION
#error APP_TRUST_PRODUCTION_PROVENANCE_CLASS must be production in firmware
#endif
#if defined(APP_TRUST_FIRMWARE_BUILD) && defined(APP_TRUST_PRODUCTION_PUBLISHER_ID)
_Static_assert(__builtin_strcmp(APP_TRUST_PRODUCTION_PUBLISHER_ID,
                                "ne301-exp-pub") != 0,
               "production publisher must not be ne301-exp-pub");
#endif
#if defined(APP_TRUST_FIRMWARE_BUILD) && defined(APP_TRUST_PRODUCTION_PROVENANCE)
_Static_assert(__builtin_strncmp(APP_TRUST_PRODUCTION_PROVENANCE,
                                 APP_TRUST_SYNTHETIC_PROVENANCE_PREFIX,
                                 sizeof(APP_TRUST_SYNTHETIC_PROVENANCE_PREFIX) - 1u) != 0,
               "production provenance must not be synthetic");
#endif
#endif

#define APP_TRUST_TEST_PUBLISHER_ID "ne301-exp-pub"
#define APP_TRUST_TEST_SPKI_SHA256_HEX "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda"
#define APP_TRUST_PROVENANCE_MAX_LEN 160u

#if APP_TRUST_PROFILE == APP_TRUST_PROFILE_TEST
static const uint8_t g_anchor_spki_der[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x7f, 0xcf, 0x75, 0xcd, 0x65, 0x69, 0xa5, 0x8f, 0x9c,
    0x2b, 0x26, 0xc6, 0xfa, 0x99, 0x16, 0x66, 0xaa, 0xc3, 0x9d, 0x44, 0x3f,
    0x7c, 0x8f, 0xeb, 0xb6, 0x14, 0xeb, 0x9d, 0x6d, 0x89, 0x23, 0xbc, 0xeb,
    0x89, 0xa3, 0xec, 0x42, 0x38, 0xa0, 0xdd, 0x9b, 0x38, 0x3c, 0x2b, 0xfd,
    0x94, 0x90, 0x45, 0x42, 0xa6, 0xa8, 0xb5, 0x70, 0x32, 0xcd, 0x4d, 0xfb,
    0x7b, 0x4c, 0xe5, 0xda, 0x86, 0xad, 0x3f,
};

static const uint8_t g_anchor_spki_sha256[] = {
    0x06, 0xe7, 0x32, 0xb1, 0xb9, 0x4d, 0xae, 0xee, 0xf4, 0x33, 0x29, 0x7f,
    0x7a, 0x8e, 0x79, 0x7b, 0xf7, 0x2b, 0x2f, 0xd3, 0xb8, 0x82, 0xa4, 0xec,
    0x6a, 0x54, 0x90, 0x35, 0x29, 0x31, 0xeb, 0xda,
};

static const app_trust_anchor_t g_app_trust_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    APP_TRUST_TEST_PUBLISHER_ID,
    sizeof(APP_TRUST_TEST_PUBLISHER_ID) - 1u,
    g_anchor_spki_der,
    sizeof(g_anchor_spki_der),
    g_anchor_spki_sha256
};
#define APP_TRUST_PROFILE_NAME "test"
#define APP_TRUST_PROVENANCE_NAME "TEST_ONLY:compiled-ne301-exp-pub"
#define APP_TRUST_SPKI_SHA256_NAME APP_TRUST_TEST_SPKI_SHA256_HEX
#elif defined(APP_TRUST_PRODUCTION_PUBLISHER_ID) && \
      defined(APP_TRUST_PRODUCTION_SPKI_DER) && \
      defined(APP_TRUST_PRODUCTION_SPKI_SHA256) && \
      defined(APP_TRUST_PRODUCTION_SPKI_SHA256_HEX) && \
      defined(APP_TRUST_PRODUCTION_PROVENANCE) && \
      defined(APP_TRUST_PRODUCTION_PROVENANCE_CLASS)
static const uint8_t g_anchor_spki_der[] = APP_TRUST_PRODUCTION_SPKI_DER;
static const uint8_t g_anchor_spki_sha256[32u] = APP_TRUST_PRODUCTION_SPKI_SHA256;
static const app_trust_anchor_t g_app_trust_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    APP_TRUST_PRODUCTION_PUBLISHER_ID,
    sizeof(APP_TRUST_PRODUCTION_PUBLISHER_ID) - 1u,
    g_anchor_spki_der,
    sizeof(g_anchor_spki_der),
    g_anchor_spki_sha256
};
#define APP_TRUST_PROFILE_NAME "production"
#define APP_TRUST_PROVENANCE_NAME APP_TRUST_PRODUCTION_PROVENANCE
#define APP_TRUST_SPKI_SHA256_NAME APP_TRUST_PRODUCTION_SPKI_SHA256_HEX
#else
static const app_trust_anchor_t g_app_trust_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    "unprovisioned",
    sizeof("unprovisioned") - 1u,
    NULL,
    0u,
    NULL
};
#define APP_TRUST_PROFILE_NAME "production"
#define APP_TRUST_PROVENANCE_NAME "unprovisioned"
#define APP_TRUST_SPKI_SHA256_NAME "unprovisioned"
#endif

static int app_trust_anchor_hex_value(char value, uint8_t *out)
{
    if (value >= '0' && value <= '9') {
        *out = (uint8_t)(value - '0');
        return 1;
    }
    if (value >= 'a' && value <= 'f') {
        *out = (uint8_t)(value - 'a' + 10);
        return 1;
    }
    return 0;
}

static int app_trust_anchor_digest_hex_valid(const char *value,
                                              const uint8_t expected[32])
{
    size_t i;
    uint8_t high;
    uint8_t low;
    uint8_t diff = 0u;

    if (value == NULL || strlen(value) != 64u) {
        return 0;
    }
    for (i = 0u; i < 32u; i++) {
        if (!app_trust_anchor_hex_value(value[i * 2u], &high) ||
            !app_trust_anchor_hex_value(value[(i * 2u) + 1u], &low)) {
            return 0;
        }
        diff |= (uint8_t)(((high << 4) | low) ^ expected[i]);
    }
    return diff == 0u;
}

static int app_trust_anchor_provenance_valid(const char *value)
{
    size_t i;
    size_t len;

    if (value == NULL) {
        return 0;
    }
    len = strlen(value);
    if (len == 0u || len > APP_TRUST_PROVENANCE_MAX_LEN) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        if ((unsigned char)value[i] < 0x20u || (unsigned char)value[i] > 0x7Eu) {
            return 0;
        }
    }
    return 1;
}

static int app_trust_anchor_p256_spki_valid(const uint8_t *der, size_t der_len)
{
    static const uint8_t prefix[] = {
        0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48,
        0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48,
        0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04
    };
    mbedtls_pk_context key;
    int valid = 0;

    if (der == NULL || der_len != 91u ||
        memcmp(der, prefix, sizeof(prefix)) != 0) {
        return 0;
    }
    mbedtls_pk_init(&key);
    if (mbedtls_pk_parse_public_key(&key, der, der_len) == 0 &&
        mbedtls_pk_get_type(&key) == MBEDTLS_PK_ECKEY &&
        mbedtls_pk_get_bitlen(&key) == 256u) {
        valid = 1;
    }
    mbedtls_pk_free(&key);
    return valid;
}

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    return &g_app_trust_anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    const app_trust_anchor_t *anchor = &g_app_trust_anchor;
    uint8_t computed[32];
    volatile uint8_t diff = 0;
    size_t i;

    if (anchor->publisher_id == NULL || anchor->spki_der == NULL ||
        anchor->spki_sha256 == NULL) {
        return APP_TRUST_ANCHOR_UNPROVISIONED;
    }

    if (anchor->format_version != APP_TRUST_ANCHOR_FORMAT_VERSION ||
        anchor->publisher_id_len == 0u ||
        anchor->publisher_id_len > APP_ADMISSION_PUBLISHER_ID_SLOT) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    if (!app_admission_identifier_valid((const uint8_t *)anchor->publisher_id,
                                        anchor->publisher_id_len)) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

#if APP_TRUST_PROFILE == APP_TRUST_PROFILE_TEST
    if (anchor->publisher_id_len != sizeof(APP_TRUST_TEST_PUBLISHER_ID) - 1u ||
        memcmp(anchor->publisher_id, APP_TRUST_TEST_PUBLISHER_ID,
               anchor->publisher_id_len) != 0) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }
#else
    if (strcmp(anchor->publisher_id, APP_TRUST_TEST_PUBLISHER_ID) == 0) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }
#if defined(APP_TRUST_FIRMWARE_BUILD)
    if (strncmp(APP_TRUST_PROVENANCE_NAME,
                APP_TRUST_SYNTHETIC_PROVENANCE_PREFIX,
                sizeof(APP_TRUST_SYNTHETIC_PROVENANCE_PREFIX) - 1u) == 0) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }
#endif
#if defined(APP_TRUST_PRODUCTION_PROVENANCE_CLASS)
    if (APP_TRUST_PRODUCTION_PROVENANCE_CLASS !=
            APP_TRUST_PROVENANCE_CLASS_PRODUCTION &&
        APP_TRUST_PRODUCTION_PROVENANCE_CLASS !=
            APP_TRUST_PROVENANCE_CLASS_SYNTHETIC) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }
#endif
#endif

    if (!app_trust_anchor_provenance_valid(APP_TRUST_PROVENANCE_NAME) ||
        !app_trust_anchor_digest_hex_valid(APP_TRUST_SPKI_SHA256_NAME,
                                           anchor->spki_sha256) ||
        anchor->spki_der_len != 91u ||
        !app_trust_anchor_p256_spki_valid(anchor->spki_der,
                                          anchor->spki_der_len)) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    mbedtls_sha256(anchor->spki_der, anchor->spki_der_len, computed, 0);
    for (i = 0; i < 32u; i++) {
        diff |= (uint8_t)(computed[i] ^ anchor->spki_sha256[i]);
    }
    if (diff != 0) {
        return APP_TRUST_ANCHOR_MALFORMED;
    }

    return APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    switch (status) {
    case APP_TRUST_ANCHOR_PROVISIONED:
        return "provisioned";
    case APP_TRUST_ANCHOR_UNPROVISIONED:
        return "unprovisioned";
    case APP_TRUST_ANCHOR_MALFORMED:
        return "malformed";
    default:
        return "unknown";
    }
}

const char *app_trust_anchor_profile_str(void)
{
    return APP_TRUST_PROFILE_NAME;
}

const char *app_trust_anchor_provenance_str(void)
{
    return APP_TRUST_PROVENANCE_NAME;
}

const char *app_trust_anchor_spki_sha256_hex(void)
{
    return APP_TRUST_SPKI_SHA256_NAME;
}
