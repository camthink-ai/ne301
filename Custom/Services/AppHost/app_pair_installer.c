#include <string.h>
#include <stdio.h>

#include "mbedtls/sha256.h"

#include "app_pair_installer.h"

static int app_pair_version_cmp(const uint16_t left[3], const uint16_t right[3])
{
    size_t i;

    for (i = 0u; i < 3u; i++) {
        if (left[i] != right[i]) {
            return (left[i] < right[i]) ? -1 : 1;
        }
    }
    return 0;
}

app_pair_history_result_t app_pair_history_check(
    const app_pair_history_t *previous,
    const app_pair_history_t *candidate)
{
    int native_cmp;
    int ui_cmp;
    aicam_bool_t same_native;
    aicam_bool_t same_ui;
    aicam_bool_t same_pair;

    if (candidate == NULL || candidate->publisher_id[0] == '\0' ||
        candidate->app_id[0] == '\0' || candidate->release_seq == 0u) {
        return APP_PAIR_HISTORY_ERR_PARAM;
    }
    if (previous == NULL) {
        return APP_PAIR_HISTORY_FRESH;
    }
    if (previous->publisher_id[0] == '\0' || previous->app_id[0] == '\0' ||
        previous->release_seq == 0u) {
        return APP_PAIR_HISTORY_ERR_PARAM;
    }
    if (strcmp(previous->publisher_id, candidate->publisher_id) != 0 ||
        strcmp(previous->app_id, candidate->app_id) != 0 ||
        memcmp(previous->publisher_key_sha256, candidate->publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        return APP_PAIR_HISTORY_ERR_IDENTITY;
    }
    same_native = (memcmp(previous->native_tbs_sha256,
                          candidate->native_tbs_sha256,
                          APP_INSTALL_SHA256_LEN) == 0)
                      ? AICAM_TRUE : AICAM_FALSE;
    same_ui = (memcmp(previous->ui_tbs_sha256, candidate->ui_tbs_sha256,
                      APP_INSTALL_SHA256_LEN) == 0)
                  ? AICAM_TRUE : AICAM_FALSE;
    same_pair = (memcmp(previous->pair_tbs_sha256, candidate->pair_tbs_sha256,
                        APP_PAIR_TBS_SHA256_LEN) == 0)
                    ? AICAM_TRUE : AICAM_FALSE;
    if (candidate->release_seq < previous->release_seq) {
        return APP_PAIR_HISTORY_ERR_DOWNGRADE;
    }
    if (candidate->release_seq == previous->release_seq) {
        return (same_native == AICAM_TRUE && same_ui == AICAM_TRUE &&
                same_pair == AICAM_TRUE)
                   ? APP_PAIR_HISTORY_IDEMPOTENT
                   : APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT;
    }
    native_cmp = app_pair_version_cmp(candidate->native_version,
                                      previous->native_version);
    ui_cmp = app_pair_version_cmp(candidate->ui_version, previous->ui_version);
    if (native_cmp < 0 || ui_cmp < 0) {
        return APP_PAIR_HISTORY_ERR_DOWNGRADE;
    }
    if ((native_cmp == 0 && same_native != AICAM_TRUE) ||
        (same_native == AICAM_TRUE && ui_cmp == 0 && same_ui != AICAM_TRUE)) {
        return APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT;
    }
    return APP_PAIR_HISTORY_UPGRADE;
}

static uint32_t app_pair_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

app_pair_install_result_t app_pair_verify_package(
    const uint8_t *package,
    size_t package_len,
    app_install_verify_fn native_verify,
    void *native_verify_ctx,
    const app_sidecar_trust_t *trust,
    app_pair_candidate_t *out)
{
    static const uint8_t unsigned_magic[8] = {'N', 'E', 'A', 'P', 'P', 5u, 0u, 0u};
    static const uint8_t zero[APP_INSTALL_SHA256_LEN] = {0};
    app_pair_candidate_t candidate;
    app_admission_verdict_t native_verdict;
    app_sidecar_trust_t ui_trust;
    app_sidecar_verdict_t ui_verdict;
    app_admission_result_t native_rc;
    app_sidecar_result_t ui_rc;
    uint32_t native_len;
    uint32_t ui_len;
    uint64_t end;

    (void)trust;
    if (package == NULL || native_verify == NULL || out == NULL) {
        return APP_PAIR_INSTALL_ERR_PARAM;
    }
    if (package_len >= sizeof(unsigned_magic) &&
        memcmp(package, unsigned_magic, sizeof(unsigned_magic)) == 0) {
        if (package_len < 16u || package_len > APP_PAIR_MAX_PACKAGE_LEN) {
            return APP_PAIR_INSTALL_ERR_BAD_FORMAT;
        }
        native_len = app_pair_get_u32(package + 8u);
        ui_len = app_pair_get_u32(package + 12u);
        end = 16u + (uint64_t)native_len + (uint64_t)ui_len;
        if (native_len < APP_ADMISSION_IMAGE_OFFSET_V4 +
                             APP_ADMISSION_NATIVE_HEADER_SIZE ||
            native_len > APP_ADMISSION_MAX_PACKAGE_LEN_V5 ||
            ui_len < APP_SIDECAR_HEADER_SIZE +
                         APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE +
                         APP_SIDECAR_RECORD_FIXED_SIZE + 1u ||
            ui_len > APP_PAIR_MAX_UI_LEN || end != package_len) {
            return APP_PAIR_INSTALL_ERR_BAD_FORMAT;
        }
        memset(&candidate, 0, sizeof(candidate));
        candidate.native_offset = 16u;
        candidate.native_len = native_len;
        candidate.ui_offset = 16u + (size_t)native_len;
        candidate.ui_len = ui_len;
        candidate.pair_tbs_len = package_len;
        native_rc = native_verify(package + candidate.native_offset,
                                  candidate.native_len, &native_verdict,
                                  native_verify_ctx);
        if (native_rc != APP_ADMISSION_OK || native_verdict.container_major != 5u) {
            return APP_PAIR_INSTALL_ERR_NATIVE_VERIFY;
        }
        memset(&ui_trust, 0, sizeof(ui_trust));
        ui_trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
        ui_trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
        ui_trust.app_id = native_verdict.app_id;
        ui_trust.app_id_len = strlen(native_verdict.app_id);
        ui_trust.native_tbs_sha256 = native_verdict.tbs_sha256;
        ui_rc = app_sidecar_verify_bundle(package + candidate.ui_offset,
                                          candidate.ui_len, &ui_trust,
                                          NULL, NULL, &ui_verdict);
        if (ui_rc != APP_SIDECAR_OK) {
            return APP_PAIR_INSTALL_ERR_UI_VERIFY;
        }
        if (strcmp(native_verdict.app_id, ui_verdict.app_id) != 0 ||
            memcmp(native_verdict.publisher_key_sha256, zero, sizeof(zero)) != 0 ||
            native_verdict.version_major != ui_verdict.native_version[0] ||
            native_verdict.version_minor != ui_verdict.native_version[1] ||
            native_verdict.version_patch != ui_verdict.native_version[2]) {
            return APP_PAIR_INSTALL_ERR_IDENTITY;
        }
        (void)snprintf(candidate.history.publisher_id,
                       sizeof(candidate.history.publisher_id), "%s",
                       APP_ADMISSION_ADMIN_PUBLISHER_ID);
        (void)snprintf(candidate.history.app_id,
                       sizeof(candidate.history.app_id), "%s", native_verdict.app_id);
        memcpy(candidate.history.native_tbs_sha256, native_verdict.tbs_sha256,
               APP_INSTALL_SHA256_LEN);
        memcpy(candidate.history.ui_tbs_sha256, ui_verdict.ui_tbs_sha256,
               APP_INSTALL_SHA256_LEN);
        candidate.history.native_version[0] = native_verdict.version_major;
        candidate.history.native_version[1] = native_verdict.version_minor;
        candidate.history.native_version[2] = native_verdict.version_patch;
        candidate.history.ui_version[0] = ui_verdict.version_major;
        candidate.history.ui_version[1] = ui_verdict.version_minor;
        candidate.history.ui_version[2] = ui_verdict.version_patch;
        mbedtls_sha256(package, package_len, candidate.history.pair_tbs_sha256, 0);
        mbedtls_sha256(package, package_len, candidate.package_sha256, 0);
        memcpy(out, &candidate, sizeof(candidate));
        return APP_PAIR_INSTALL_OK;
    }
    if (package_len >= 5u && memcmp(package, "NEAPP", 5u) == 0) {
        if (package_len >= 6u && package[5] >= 1u && package[5] <= 4u) {
            return APP_PAIR_INSTALL_ERR_LEGACY_SIGNED_FORMAT;
        }
        return APP_PAIR_INSTALL_ERR_UNKNOWN_FORMAT;
    }
    if (package_len >= 4u && memcmp(package, "NEPK", 4u) == 0) {
        return APP_PAIR_INSTALL_ERR_LEGACY_SIGNED_FORMAT;
    }
    return APP_PAIR_INSTALL_ERR_UNKNOWN_FORMAT;
}

static app_pair_install_result_t app_pair_map_native_result(app_install_result_t result)
{
    switch (result) {
    case APP_INSTALL_OK:
        return APP_PAIR_INSTALL_OK;
    case APP_INSTALL_OK_ALREADY:
        return APP_PAIR_INSTALL_ALREADY;
    case APP_INSTALL_ERR_BUSY:
        return APP_PAIR_INSTALL_ERR_BUSY;
    case APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN:
        return APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN;
    case APP_INSTALL_ERR_STORAGE_FULL:
        return APP_PAIR_INSTALL_ERR_STORAGE_FULL;
    case APP_INSTALL_ERR_WRITE_FAILED:
        return APP_PAIR_INSTALL_ERR_WRITE_FAILED;
    case APP_INSTALL_ERR_COMMIT_FAILED:
        return APP_PAIR_INSTALL_ERR_COMMIT_FAILED;
    case APP_INSTALL_ERR_LEGACY_STATE:
        return APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED;
    case APP_INSTALL_ERR_DOWNGRADE_REFUSED:
        return APP_PAIR_INSTALL_ERR_DOWNGRADE;
    case APP_INSTALL_ERR_VERSION_CONFLICT:
        return APP_PAIR_INSTALL_ERR_CONTENT_CONFLICT;
    case APP_INSTALL_ERR_PUBLISHER_CONFLICT:
    case APP_INSTALL_ERR_IDENTITY_CONFLICT:
        return APP_PAIR_INSTALL_ERR_IDENTITY;
    default:
        return APP_PAIR_INSTALL_ERR_NATIVE_VERIFY;
    }
}

static app_pair_install_result_t app_pair_map_lifecycle_result(
    app_sidecar_lifecycle_result_t result)
{
    switch (result) {
    case APP_SIDECAR_LIFECYCLE_OK:
        return APP_PAIR_INSTALL_OK;
    case APP_SIDECAR_LIFECYCLE_ALREADY:
        return APP_PAIR_INSTALL_ALREADY;
    case APP_SIDECAR_LIFECYCLE_ERR_PARAM:
        return APP_PAIR_INSTALL_ERR_PARAM;
    case APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED:
        return APP_PAIR_INSTALL_ERR_IDENTITY;
    case APP_SIDECAR_LIFECYCLE_ERR_VERIFY:
        return APP_PAIR_INSTALL_ERR_UI_VERIFY;
    case APP_SIDECAR_LIFECYCLE_ERR_STORAGE_FULL:
        return APP_PAIR_INSTALL_ERR_STORAGE_FULL;
    case APP_SIDECAR_LIFECYCLE_ERR_VERSION_CONFLICT:
        return APP_PAIR_INSTALL_ERR_CONTENT_CONFLICT;
    case APP_SIDECAR_LIFECYCLE_ERR_DOWNGRADE:
        return APP_PAIR_INSTALL_ERR_DOWNGRADE;
    case APP_SIDECAR_LIFECYCLE_ERR_BUSY:
        return APP_PAIR_INSTALL_ERR_BUSY;
    default:
        return APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN;
    }
}

app_pair_install_result_t app_pair_install(
    const app_install_io_t *io,
    app_install_verify_fn native_verify,
    void *native_verify_ctx,
    const app_sidecar_trust_t *trust,
    const uint8_t *package,
    size_t package_len,
    app_install_record_t *out_native,
    app_sidecar_descriptor_t *out_ui,
    app_sidecar_lifecycle_state_t *out_state)
{
    app_pair_candidate_t candidate;
    app_sidecar_trust_t ui_trust;
    app_sidecar_lifecycle_result_t lifecycle_result;
    app_install_result_t native_result;
    app_pair_install_result_t pair_result;

    if (io == NULL || native_verify == NULL || trust == NULL || package == NULL ||
        out_native == NULL || out_ui == NULL || out_state == NULL) {
        return APP_PAIR_INSTALL_ERR_PARAM;
    }
    memset(out_native, 0, sizeof(*out_native));
    memset(out_ui, 0, sizeof(*out_ui));
    *out_state = APP_SIDECAR_LIFECYCLE_NONE;
    if (app_install_app_running() == AICAM_TRUE) {
        return APP_PAIR_INSTALL_ERR_BUSY;
    }
    pair_result = app_pair_verify_package(package, package_len, native_verify,
                                          native_verify_ctx, trust, &candidate);
    if (pair_result != APP_PAIR_INSTALL_OK) {
        return pair_result;
    }
    if (candidate.history.release_seq == 0u) {
        app_install_record_t current;
        aicam_bool_t present = AICAM_FALSE;
        app_install_result_t current_rc = app_install_query_committed(io, &current,
                                                                      &present);
        if (current_rc != APP_INSTALL_OK) {
            return APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN;
        }
        if (present == AICAM_TRUE &&
            (strcmp(current.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
             memcmp(current.publisher_key_sha256,
                    (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
                    APP_INSTALL_SHA256_LEN) != 0)) {
            return APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED;
        }
        lifecycle_result = app_sidecar_lifecycle_unsigned_preflight(
            io, (present == AICAM_TRUE) ? &current : NULL);
        if (lifecycle_result != APP_SIDECAR_LIFECYCLE_OK) {
            return (lifecycle_result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED)
                       ? APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED
                       : app_pair_map_lifecycle_result(lifecycle_result);
        }
    } else {
        lifecycle_result = app_sidecar_lifecycle_pair_history_check(
            io, &candidate.history);
        if (lifecycle_result != APP_SIDECAR_LIFECYCLE_OK) {
            return app_pair_map_lifecycle_result(lifecycle_result);
        }
    }
    native_result = app_install(io, native_verify, native_verify_ctx,
                                package + candidate.native_offset,
                                candidate.native_len, out_native);
    if (native_result != APP_INSTALL_OK && native_result != APP_INSTALL_OK_ALREADY) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return app_pair_map_native_result(native_result);
    }
    memset(&ui_trust, 0, sizeof(ui_trust));
    if (trust != NULL) {
        ui_trust = *trust;
    } else {
        ui_trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
        ui_trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    }
    ui_trust.app_id = out_native->app_id;
    ui_trust.app_id_len = strlen(out_native->app_id);
    ui_trust.native_tbs_sha256 = out_native->tbs_sha256;
    lifecycle_result = app_sidecar_lifecycle_install_pair(
        io, &ui_trust, out_native, package + candidate.ui_offset,
        candidate.ui_len,
        (candidate.history.release_seq == 0u) ? NULL : &candidate.history,
        out_ui, out_state);
    if (lifecycle_result != APP_SIDECAR_LIFECYCLE_OK &&
        lifecycle_result != APP_SIDECAR_LIFECYCLE_ALREADY) {
        return app_pair_map_lifecycle_result(lifecycle_result);
    }
    if (app_sidecar_lifecycle_pair_authorized(io, out_native, out_ui) != AICAM_TRUE) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN;
    }
    if (native_result == APP_INSTALL_OK_ALREADY &&
        lifecycle_result == APP_SIDECAR_LIFECYCLE_ALREADY) {
        return APP_PAIR_INSTALL_ALREADY;
    }
    return APP_PAIR_INSTALL_OK;
}

aicam_bool_t app_pair_is_authorized(
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    const app_sidecar_descriptor_t *descriptor)
{
    return app_sidecar_lifecycle_pair_authorized(io, native_record, descriptor);
}

const char *app_pair_install_result_str(app_pair_install_result_t result)
{
    switch (result) {
    case APP_PAIR_INSTALL_OK:
        return "INSTALLED";
    case APP_PAIR_INSTALL_ALREADY:
        return "ALREADY_INSTALLED";
    case APP_PAIR_INSTALL_ERR_PARAM:
        return "PARAM";
    case APP_PAIR_INSTALL_ERR_BAD_FORMAT:
        return "BAD_PACKAGE";
    case APP_PAIR_INSTALL_ERR_NATIVE_VERIFY:
        return "NATIVE_VERIFY_FAILED";
    case APP_PAIR_INSTALL_ERR_UI_VERIFY:
        return "UI_VERIFY_FAILED";
    case APP_PAIR_INSTALL_ERR_PAIR_SIGNATURE:
        return "LEGACY_SIGNED_FORMAT_REJECTED";
    case APP_PAIR_INSTALL_ERR_IDENTITY:
        return "IDENTITY_CONFLICT";
    case APP_PAIR_INSTALL_ERR_CONTENT_CONFLICT:
        return "CONTENT_CONFLICT";
    case APP_PAIR_INSTALL_ERR_DOWNGRADE:
        return "DOWNGRADE_REFUSED";
    case APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN:
        return "STORAGE_STATE_UNKNOWN";
    case APP_PAIR_INSTALL_ERR_STORAGE_FULL:
        return "STORAGE_FULL";
    case APP_PAIR_INSTALL_ERR_BUSY:
        return "BUSY";
    case APP_PAIR_INSTALL_ERR_WRITE_FAILED:
        return "WRITE_FAILED";
    case APP_PAIR_INSTALL_ERR_COMMIT_FAILED:
        return "COMMIT_FAILED";
    case APP_PAIR_INSTALL_ERR_UNAVAILABLE:
        return "UNAVAILABLE";
    case APP_PAIR_INSTALL_ERR_LEGACY_SIGNED_FORMAT:
        return "LEGACY_SIGNED_FORMAT_REJECTED";
    case APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED:
        return "LEGACY_SIGNED_STATE_PRESERVED";
    case APP_PAIR_INSTALL_ERR_UNKNOWN_FORMAT:
        return "UNKNOWN_FORMAT_REJECTED";
    default:
        return "UNKNOWN";
    }
}
