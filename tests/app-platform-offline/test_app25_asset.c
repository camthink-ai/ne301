#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_sidecar_verify.h"
#include "app_trust_anchor.h"
#include "mbedtls/sha256.h"

typedef struct {
    size_t count;
    size_t bytes;
    int invalid;
} file_summary_t;

static int fail(const char *name)
{
    fprintf(stderr, "FAIL %s\n", name);
    return 1;
}

static int decode_hex(const char *hex, uint8_t *out, size_t len)
{
    size_t i;

    if (strlen(hex) != len * 2u) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        unsigned value;
        if (sscanf(hex + (i * 2u), "%2x", &value) != 1) {
            return 0;
        }
        out[i] = (uint8_t)value;
    }
    return 1;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *data;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len = (size_t)size;
    return data;
}

static void note_file(void *user, const app_sidecar_file_desc_t *desc)
{
    file_summary_t *summary = (file_summary_t *)user;

    if (summary == NULL || desc == NULL || desc->file_len == 0u || desc->path[0] == '\0') {
        if (summary != NULL) {
            summary->invalid = 1;
        }
        return;
    }
    summary->count++;
    summary->bytes += desc->file_len;
}

int main(int argc, char **argv)
{
    static const char *const expected_bundle_sha = "3cd01519af881eb114c2831d67b4f0542815c98140e06deeba14e597d947771c";
    static const char *const expected_spki_sha = "5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3";
    static const char *const expected_host_anchor_sha = "06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda";
    static const char *const expected_native_tbs = "d793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e";
    static const char *const expected_ui_tbs = "59f695608f3caa2a06ffae486e6288b3e3052c277a87c5a8a0a3cfb3a049e7a6";
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;
    file_summary_t files = {0u, 0u, 0};
    uint8_t expected_bundle[APP_SIDECAR_SHA256_LEN];
    uint8_t expected_spki[APP_SIDECAR_SHA256_LEN];
    uint8_t expected_host_anchor[APP_SIDECAR_SHA256_LEN];
    uint8_t expected_tbs[APP_SIDECAR_SHA256_LEN];
    uint8_t expected_ui[APP_SIDECAR_SHA256_LEN];
    uint8_t actual_sha[APP_SIDECAR_SHA256_LEN];
    uint8_t *bundle;
    uint8_t *spki;
    size_t bundle_len = 0u;
    size_t spki_len = 0u;
    app_sidecar_result_t result;
    const app_trust_anchor_t *native_anchor;

    if (argc != 3) {
        return fail("arguments");
    }
    if (!decode_hex(expected_bundle_sha, expected_bundle, sizeof(expected_bundle)) ||
        !decode_hex(expected_spki_sha, expected_spki, sizeof(expected_spki)) ||
        !decode_hex(expected_host_anchor_sha, expected_host_anchor, sizeof(expected_host_anchor)) ||
        !decode_hex(expected_native_tbs, expected_tbs, sizeof(expected_tbs)) ||
        !decode_hex(expected_ui_tbs, expected_ui, sizeof(expected_ui))) {
        return fail("expected_digest_encoding");
    }
    bundle = read_file(argv[1], &bundle_len);
    spki = read_file(argv[2], &spki_len);
    if (bundle == NULL || spki == NULL) {
        free(bundle);
        free(spki);
        return fail("input_read");
    }
    if (bundle_len != 422963u || spki_len != APP_SIDECAR_SPKI_P256_LEN) {
        free(bundle);
        free(spki);
        return fail("input_lengths");
    }
    if (mbedtls_sha256(bundle, bundle_len, actual_sha, 0) != 0 ||
        memcmp(actual_sha, expected_bundle, sizeof(actual_sha)) != 0) {
        free(bundle);
        free(spki);
        return fail("bundle_sha256");
    }
    if (mbedtls_sha256(spki, spki_len, actual_sha, 0) != 0 ||
        memcmp(actual_sha, expected_spki, sizeof(actual_sha)) != 0) {
        free(bundle);
        free(spki);
        return fail("signer_spki_sha256");
    }
    native_anchor = app_trust_anchor_get();
    if (native_anchor == NULL || app_trust_anchor_status() != APP_TRUST_ANCHOR_PROVISIONED ||
        native_anchor->publisher_id == NULL || native_anchor->spki_der == NULL ||
        native_anchor->spki_der_len != APP_SIDECAR_SPKI_P256_LEN ||
        mbedtls_sha256(native_anchor->spki_der, native_anchor->spki_der_len, actual_sha, 0) != 0 ||
        memcmp(actual_sha, expected_host_anchor, sizeof(actual_sha)) != 0 ||
        strcmp(native_anchor->publisher_id, "test-publisher") == 0 ||
        (native_anchor->spki_der_len == spki_len &&
         memcmp(native_anchor->spki_der, spki, spki_len) == 0)) {
        free(bundle);
        free(spki);
        return fail("host_native_anchor_identity");
    }

    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = "test-publisher";
    trust.publisher_id_len = strlen(trust.publisher_id);
    trust.app_id = "line-crossing";
    trust.app_id_len = strlen(trust.app_id);
    trust.spki_der = spki;
    trust.spki_der_len = spki_len;
    trust.native_tbs_sha256 = expected_tbs;
    result = app_sidecar_verify_bundle(bundle, bundle_len, &trust, note_file, &files, &verdict);
    if (result != APP_SIDECAR_OK) {
        fprintf(stderr, "FAIL app_sidecar_verify_bundle: %s\n", app_sidecar_result_str(result));
        free(bundle);
        free(spki);
        return 1;
    }
    if (strcmp(verdict.publisher_id, "test-publisher") != 0 ||
        strcmp(verdict.app_id, "line-crossing") != 0 ||
        verdict.version_major != 1u || verdict.version_minor != 0u || verdict.version_patch != 0u ||
        verdict.host_ui_abi != 1u || verdict.file_count != 7u || verdict.manifest_len != 568u ||
        verdict.asset_payload_len != 422308u || verdict.signature_len != 71u ||
        memcmp(verdict.ui_tbs_sha256, expected_ui, sizeof(expected_ui)) != 0 ||
        files.count != 7u || files.bytes != 422308u || files.invalid != 0) {
        free(bundle);
        free(spki);
        return fail("verified_metadata");
    }

    printf("PASS app25 signed NEUI: bundle_sha256=%s bytes=%lu native_tbs_sha256=%s ui_tbs_sha256=%s tbs_bytes=%lu spki_sha256=%s abi=%lu version=1.0.0 files=%u\n",
           expected_bundle_sha, (unsigned long)bundle_len, expected_native_tbs, expected_ui_tbs,
           (unsigned long)(bundle_len - verdict.signature_len), expected_spki_sha,
           (unsigned long)verdict.host_ui_abi, (unsigned)verdict.file_count);
    printf("CROSS-ROOT UNKNOWN: app_test_publisher=%s/%s host_native_publisher=%s/%s\n",
           trust.publisher_id, expected_spki_sha, native_anchor->publisher_id,
           expected_host_anchor_sha);
    free(bundle);
    free(spki);
    return 0;
}
