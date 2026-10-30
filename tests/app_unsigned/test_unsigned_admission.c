#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_admission.h"
#include "app_installer.h"
#include "app_native_vnext_floor.h"
#include "app_pair_installer.h"
#include "app_sidecar_lifecycle.h"
#include "app_sidecar_verify.h"
#include "generic_math.h"
#include "installer_vectors.h"
#include "mem_io_backend.h"

static int failures;

static void check(int value, const char *name)
{
    if (!value) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *file = fopen(path, "rb");
    long size;
    unsigned char *data;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)size);
    if (data == NULL || fread(data, 1u, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len = (size_t)size;
    return data;
}

static app_admission_result_t verify_native(const uint8_t *data, size_t len,
                                            app_admission_verdict_t *out, void *ctx)
{
    return app_admission_verify_package(data, len,
                                        (const app_admission_policy_t *)ctx,
                                        NULL, out);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u64(uint8_t *data, uint64_t value)
{
    size_t index;
    for (index = 0u; index < 8u; index++) {
        data[index] = (uint8_t)(value >> (index * 8u));
    }
}

static app_uninstall_purge_result_t confirm_purge(void *ctx,
                                                   const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_CONFIRMED;
}

static void test_unsigned_install_lifecycle(const app_admission_policy_t *policy)
{
    app_install_io_t io;
    app_sidecar_trust_t trust;
    app_install_record_t native;
    app_install_record_t committed;
    app_install_record_t removed;
    app_pair_history_t history;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_uninstall_cleanup_ops_t cleanup;
    app_uninstall_state_t uninstall_state;
    app_uninstall_result_t uninstall_result;
    app_pair_install_result_t pair_result;
    aicam_bool_t present;
    uint8_t *package;
    uint8_t *bad;
    size_t package_len = 0u;
    unsigned char *upgrade;
    size_t upgrade_len = 0u;
    uint8_t floor[192];
    uint8_t floor_before[192];
    uint8_t pair_floor[216];
    uint8_t pair_floor_before[216];
    uint8_t install_record[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t install_record_before[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t active_package_sha[APP_INSTALL_SHA256_LEN];
    char package_path[APP_INSTALL_PATH_MAX];
    size_t index;
    mem_kv_t *stored;

    package = read_file("single-file-v5.neapp", &package_len);
    upgrade = read_file("single-file-v5-upgrade.neapp", &upgrade_len);
    check(package != NULL && upgrade != NULL, "install_fixture_read");
    if (package == NULL || upgrade == NULL) {
        free(package);
        free(upgrade);
        return;
    }
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    app_install_set_app_running(AICAM_FALSE);
    pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                   package, package_len, &native, &descriptor, &state);
    if (pair_result != APP_PAIR_INSTALL_OK) {
        printf("unsigned_pair_install_result=%s\n",
               app_pair_install_result_str(pair_result));
    }
    check(pair_result == APP_PAIR_INSTALL_OK, "unsigned_pair_install");
    check(state == APP_SIDECAR_LIFECYCLE_ACTIVE, "unsigned_pair_active");
    check(app_pair_is_authorized(&io, &native, &descriptor) == AICAM_TRUE,
          "unsigned_pair_authorized");
    check(app_sidecar_lifecycle_pair_history_query(&io, &history, &present) ==
              APP_SIDECAR_LIFECYCLE_OK && present == AICAM_FALSE,
          "unsigned_install_has_no_release_sequence");
    check(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK &&
              present == AICAM_TRUE &&
              strcmp(committed.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0,
          "unsigned_install_record_committed");
    check(memcmp(committed.publisher_key_sha256,
                 (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
                 APP_INSTALL_SHA256_LEN) == 0,
          "unsigned_install_key_digest_zero");
    memcpy(active_package_sha, committed.package_sha256, sizeof(active_package_sha));
    pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                   package, package_len, &native, &descriptor, &state);
    check(pair_result == APP_PAIR_INSTALL_ALREADY, "unsigned_pair_retry_idempotent");

    bad = (uint8_t *)malloc(package_len);
    check(bad != NULL, "unsigned_failure_clone_allocated");
    if (bad != NULL) {
        memcpy(bad, package, package_len);
        bad[package_len - 1u] ^= 1u;
        pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                       bad, package_len, &native, &descriptor, &state);
        check(pair_result == APP_PAIR_INSTALL_ERR_UI_VERIFY,
              "unsigned_failed_update_rejected_before_mutation");
        check(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK &&
                  present == AICAM_TRUE &&
                  memcmp(committed.package_sha256, active_package_sha,
                         APP_INSTALL_SHA256_LEN) == 0 && committed.version_minor == 0u,
              "unsigned_failed_update_keeps_native_commit");
        trust.app_id = committed.app_id;
        trust.app_id_len = strlen(committed.app_id);
        trust.native_tbs_sha256 = committed.tbs_sha256;
        check(app_sidecar_lifecycle_query(&io, &trust, &committed, &descriptor,
                                          &state) == APP_SIDECAR_LIFECYCLE_OK &&
                  state == APP_SIDECAR_LIFECYCLE_ACTIVE &&
                  app_pair_is_authorized(&io, &committed, &descriptor) == AICAM_TRUE,
              "unsigned_failed_update_keeps_active_ui");
        free(bad);
    }

    pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                   upgrade, upgrade_len, &native, &descriptor, &state);
    if (pair_result != APP_PAIR_INSTALL_OK) {
        printf("unsigned_pair_upgrade_result=%s\n",
               app_pair_install_result_str(pair_result));
    }
    check(pair_result == APP_PAIR_INSTALL_OK, "unsigned_pair_upgrade");
    check(native.version_minor == 1u && descriptor.version_patch == 4u,
          "unsigned_pair_upgrade_versions");
    check(app_pair_is_authorized(&io, &native, &descriptor) == AICAM_TRUE,
          "unsigned_pair_upgrade_authorized");
    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.purge = confirm_purge;
    uninstall_result = app_install_uninstall_confirmed(
        &io, AICAM_TRUE, &cleanup, &removed, &uninstall_state);
    check(uninstall_result == APP_UNINSTALL_OK &&
              uninstall_state == APP_UNINSTALL_STATE_NONE,
          "unsigned_pair_explicit_uninstall");
    check(app_pair_is_authorized(&io, &native, &descriptor) == AICAM_FALSE,
          "unsigned_pair_uninstall_revokes_ui_authorization");
    check(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK &&
              present == AICAM_FALSE,
          "unsigned_pair_uninstall_clears_native");
    check(app_sidecar_lifecycle_pair_history_query(&io, &history, &present) ==
              APP_SIDECAR_LIFECYCLE_OK && present == AICAM_FALSE,
          "unsigned_pair_uninstall_no_release_sequence");
    free(package);
    free(upgrade);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    memset(&committed, 0, sizeof(committed));
    snprintf(committed.publisher_id, sizeof(committed.publisher_id), "%s", "legacy-signed");
    snprintf(committed.app_id, sizeof(committed.app_id), "%s", "line-crossing");
    committed.version_major = 4u;
    committed.package_len = 128u;
    memset(committed.package_sha256, 0x31, sizeof(committed.package_sha256));
    memset(committed.tbs_sha256, 0x32, sizeof(committed.tbs_sha256));
    memset(committed.image_sha256, 0x33, sizeof(committed.image_sha256));
    memset(committed.publisher_key_sha256, 0x34,
           sizeof(committed.publisher_key_sha256));
    index = (size_t)snprintf(package_path, sizeof(package_path), "%s/",
                             APP_INSTALL_PKG_DIR);
    for (size_t byte = 0u; byte < APP_INSTALL_SHA256_LEN; byte++) {
        index += (size_t)snprintf(package_path + index, sizeof(package_path) - index,
                                  "%02x", committed.package_sha256[byte]);
    }
    (void)snprintf(package_path + index, sizeof(package_path) - index, ".neapp");
    snprintf(committed.package_path, sizeof(committed.package_path), "%s", package_path);
    check(app_install_record_encode(&committed, install_record,
                                    sizeof(install_record)) == AICAM_TRUE,
          "legacy_record_fixture_encode");
    memcpy(install_record_before, install_record, sizeof(install_record));
    check(io.kv_write(io.ctx, APP_INSTALL_KEY_RECORD, install_record,
                      sizeof(install_record)) == APP_INSTALL_IO_OK,
          "legacy_record_fixture_store");
    package = read_file("single-file-v5.neapp", &package_len);
    pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                   package, package_len, &native, &descriptor, &state);
    check(pair_result == APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED,
          "legacy_signed_record_blocks_unsigned_install");
    stored = mem_find_key(APP_INSTALL_KEY_RECORD);
    check(stored != NULL && stored->len == sizeof(install_record_before) &&
              memcmp(stored->data, install_record_before,
                     sizeof(install_record_before)) == 0,
          "legacy_signed_record_bytes_preserved");
    free(package);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    memset(pair_floor, 0, sizeof(pair_floor));
    put_u32(pair_floor, 0x31465041u);
    put_u32(pair_floor + 4u, 1u);
    put_u64(pair_floor + 8u, 1u);
    memcpy(pair_floor + 16u, "legacy-signed", 13u);
    memcpy(pair_floor + 32u, "line-crossing", 13u);
    memset(pair_floor + 64u, 0x44, APP_INSTALL_SHA256_LEN);
    memset(pair_floor + 96u, 0x55, APP_INSTALL_SHA256_LEN);
    memset(pair_floor + 128u, 0x66, APP_INSTALL_SHA256_LEN);
    memset(pair_floor + 160u, 0x77, APP_INSTALL_SHA256_LEN);
    put_u64(pair_floor + 192u, 3u);
    put_u16(pair_floor + 200u, 4u);
    put_u16(pair_floor + 206u, 1u);
    put_u32(pair_floor + 212u, generic_crc32(pair_floor, 212u));
    memcpy(pair_floor_before, pair_floor, sizeof(pair_floor));
    check(io.kv_write(io.ctx, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                      pair_floor, sizeof(pair_floor)) == APP_INSTALL_IO_OK,
          "legacy_pair_floor_fixture_store");
    package = read_file("single-file-v5.neapp", &package_len);
    pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                   package, package_len, &native, &descriptor, &state);
    check(pair_result == APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED,
          "legacy_pair_floor_blocks_unsigned_install");
    stored = mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR);
    check(stored != NULL && stored->len == sizeof(pair_floor_before) &&
              memcmp(stored->data, pair_floor_before, sizeof(pair_floor_before)) == 0,
          "legacy_pair_floor_bytes_preserved");
    free(package);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    memset(floor, 0, sizeof(floor));
    put_u32(floor, 0x3446564Eu);
    put_u32(floor + 4u, 1u);
    put_u32(floor + 8u, 1u);
    put_u32(floor + 12u, APP_NATIVE_VNEXT_MAJOR);
    put_u64(floor + 16u, 1u);
    memcpy(floor + 44u, "legacy-signed", 13u);
    memcpy(floor + 60u, "line-crossing", 13u);
    memset(floor + 92u, 0x44, APP_INSTALL_SHA256_LEN);
    memset(floor + 124u, 0x55, APP_INSTALL_SHA256_LEN);
    memset(floor + 156u, 0x66, APP_INSTALL_SHA256_LEN);
    put_u32(floor + 188u, generic_crc32(floor, 188u));
    memcpy(floor_before, floor, sizeof(floor));
    check(io.kv_write(io.ctx, APP_NATIVE_VNEXT_FLOOR_KEY, floor,
                      sizeof(floor)) == APP_INSTALL_IO_OK,
          "legacy_floor_fixture_store");
    package = read_file("single-file-v5.neapp", &package_len);
    pair_result = app_pair_install(&io, verify_native, (void *)policy, &trust,
                                   package, package_len, &native, &descriptor, &state);
    check(pair_result == APP_PAIR_INSTALL_ERR_LEGACY_STATE_PRESERVED,
          "legacy_vnext_floor_blocks_unsigned_install");
    stored = mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY);
    check(stored != NULL && stored->len == sizeof(floor_before) &&
              memcmp(stored->data, floor_before, sizeof(floor_before)) == 0,
          "legacy_vnext_floor_bytes_preserved");
    free(package);
}

static void test_version_boundary_rejections(const app_admission_policy_t *policy)
{
    static const struct {
        const char *path;
        const char *name;
    } ui_cases[] = {
        {"single-file-v5-ui-version-1.0.0.neapp", "ui_version_1_0_0_rejected"},
        {"single-file-v5-ui-version-1.1.0.neapp", "ui_version_1_1_0_rejected"}
    };
    size_t index;

    for (index = 0u; index < sizeof(ui_cases) / sizeof(ui_cases[0]); index++) {
        uint8_t *package;
        size_t package_len = 0u;
        uint32_t native_len;
        uint32_t ui_len;
        app_admission_verdict_t native_verdict;
        app_admission_result_t native_result;
        app_sidecar_trust_t trust;
        app_sidecar_verdict_t ui_verdict;
        app_sidecar_result_t ui_result;
        app_pair_candidate_t pair_candidate;

        package = read_file(ui_cases[index].path, &package_len);
        check(package != NULL, "ui_version_fixture_read");
        if (package == NULL) {
            continue;
        }
        native_len = read_u32_le(package + 8u);
        ui_len = read_u32_le(package + 12u);
        if (package_len < 16u || (size_t)native_len > package_len - 16u ||
            (size_t)ui_len != package_len - 16u - (size_t)native_len) {
            check(0, "ui_version_fixture_bounds");
            free(package);
            continue;
        }
        native_result = verify_native(package + 16u, native_len, &native_verdict,
                                      (void *)policy);
        check(native_result == APP_ADMISSION_OK,
              "ui_version_fixture_native_valid");
        if (native_result != APP_ADMISSION_OK) {
            free(package);
            continue;
        }
        memset(&trust, 0, sizeof(trust));
        trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
        trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
        trust.app_id = native_verdict.app_id;
        trust.app_id_len = strlen(native_verdict.app_id);
        trust.native_tbs_sha256 = native_verdict.tbs_sha256;
        ui_result = app_sidecar_verify_bundle(package + 16u + native_len,
                                              ui_len, &trust, NULL, NULL,
                                              &ui_verdict);
        check(ui_result == APP_SIDECAR_ERR_BAD_FORMAT, ui_cases[index].name);
        check(app_pair_verify_package(package, package_len, verify_native,
                                      (void *)policy, NULL, &pair_candidate) ==
                  APP_PAIR_INSTALL_ERR_UI_VERIFY,
              "ui_version_pair_rejected");
        free(package);
    }

    {
        uint8_t *package;
        size_t package_len = 0u;
        uint32_t native_len;
        app_admission_verdict_t native_verdict;
        app_pair_candidate_t pair_candidate;

        package = read_file("single-file-v5-native-major-0.neapp",
                            &package_len);
        check(package != NULL, "native_version_fixture_read");
        if (package != NULL) {
            native_len = read_u32_le(package + 8u);
            if (package_len < 16u || (size_t)native_len > package_len - 16u) {
                check(0, "native_version_fixture_bounds");
                free(package);
                package = NULL;
            }
        }
        if (package != NULL) {
            check(verify_native(package + 16u, native_len, &native_verdict,
                                (void *)policy) == APP_ADMISSION_ERR_BAD_PACKAGE,
                  "native_version_major_zero_rejected");
            check(app_pair_verify_package(package, package_len, verify_native,
                                          (void *)policy, NULL, &pair_candidate) ==
                      APP_PAIR_INSTALL_ERR_NATIVE_VERIFY,
                  "native_version_pair_rejected");
            free(package);
        }
    }
}

int main(void)
{
    app_admission_policy_t policy;
    app_admission_verdict_t verdict;
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t ui_verdict;
    app_sidecar_result_t ui_result;
    app_pair_candidate_t pair_candidate;
    app_admission_trust_view_t legacy_trust;
    unsigned char *data;
    unsigned char *ui;
    unsigned char *bad;
    size_t len = 0u;
    size_t ui_len = 0u;
    app_admission_result_t result;

    data = read_file("native-v5.neapp", &len);
    check(data != NULL, "fixture_read");
    if (data == NULL) {
        return 1;
    }
    memset(&policy, 0, sizeof(policy));
    policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy.psram_mib = 64u;
    policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    policy.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    policy.exec_region_base = 0x93E00000u;
    policy.exec_region_size = 2u * 1024u * 1024u;
    policy.event_capacity = 16384u;
    policy.report_capacity = 16384u;
    policy.state_quota_capacity = 8192u;
    policy.control_capacity = 4096u;
    policy.snapshot_capacity = 4096u;
    policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    policy.live_publish_capacity = APP_ADMISSION_V4_LIVE_MIN_PUBLISH;
    policy.live_track_capacity = APP_ADMISSION_V4_LIVE_TRACK_MAX;
    policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V4;
    memset(&legacy_trust, 0, sizeof(legacy_trust));
    {
        uint8_t old_magic[APP_ADMISSION_CONTAINER_HEADER_SIZE] = {
            'N', 'E', 'A', 'P', 'P', 0u, 0u, 0u
        };
        uint8_t version;
        for (version = 1u; version <= 4u; version++) {
            old_magic[5] = version;
            check(app_admission_verify_package(old_magic, sizeof(old_magic),
                                               &policy, &legacy_trust, &verdict) ==
                      APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT,
                  "legacy_signed_native_version_rejected");
        }
    }
    result = app_admission_verify_package(data, len, &policy, NULL, &verdict);
    check(result == APP_ADMISSION_OK, "unsigned_native_accept");
    check(verdict.container_major == 5u, "unsigned_native_version");
    check(verdict.signature_len == 0u, "unsigned_signature_absent");
    check(strcmp(verdict.app_id, "line-crossing") == 0, "unsigned_app_id");
    check(strcmp(verdict.publisher_id, "admin-installed") == 0, "unsigned_local_identity");
    check(app_admission_container_version(data, len) == 5u, "unsigned_magic_version");
    bad = (unsigned char *)malloc(len);
    check(bad != NULL, "native_fixture_clone_allocated");
    if (bad != NULL) {
        memcpy(bad, data, len);
        bad[0] ^= 1u;
        check(app_admission_verify_package(bad, len, &policy, NULL, &verdict) ==
                  APP_ADMISSION_ERR_UNKNOWN_FORMAT,
              "native_unknown_format_rejected");
        memcpy(bad, data, len);
        memset(bad + 8u, 0xFF, 8u);
        check(app_admission_verify_package(bad, len, &policy, NULL, &verdict) ==
                  APP_ADMISSION_ERR_BAD_PACKAGE,
              "native_length_overflow_rejected");
        free(bad);
    }
    free(data);
    data = read_file("single-file-v5.neapp", &len);
    check(data != NULL, "pair_fixture_read");
    if (data == NULL) {
        return 1;
    }
    check(app_pair_verify_package(data, len, verify_native, &policy, NULL,
                                  &pair_candidate) == APP_PAIR_INSTALL_OK,
          "unsigned_pair_accept");
    check(pair_candidate.native_offset == 16u &&
              pair_candidate.history.release_seq == 0u,
          "unsigned_pair_no_trusted_sequence");
    bad = (unsigned char *)malloc(len);
    check(bad != NULL, "pair_fixture_clone_allocated");
    if (bad != NULL) {
        memcpy(bad, data, len);
        bad[5] = 4u;
        check(app_pair_verify_package(bad, len, verify_native, &policy, NULL,
                                      &pair_candidate) ==
                  APP_PAIR_INSTALL_ERR_LEGACY_SIGNED_FORMAT,
              "legacy_signed_pair_rejected");
        memcpy(bad, data, len);
        bad[5] = 6u;
        check(app_pair_verify_package(bad, len, verify_native, &policy, NULL,
                                      &pair_candidate) == APP_PAIR_INSTALL_ERR_UNKNOWN_FORMAT,
              "unknown_pair_rejected");
        memcpy(bad, data, len);
        memset(bad + 8u, 0xFF, 8u);
        check(app_pair_verify_package(bad, len, verify_native, &policy, NULL,
                                      &pair_candidate) == APP_PAIR_INSTALL_ERR_BAD_FORMAT,
              "pair_length_overflow_rejected");
        free(bad);
    }
    free(data);
    data = read_file("native-v5.neapp", &len);
    check(data != NULL, "native_fixture_read_again");
    if (data == NULL) {
        return 1;
    }
    ui = read_file("ui-v2.neui", &ui_len);
    check(ui != NULL, "ui_fixture_read");
    if (ui == NULL) {
        free(data);
        return 1;
    }
    check(app_admission_verify_package(data, len, &policy, NULL, &verdict) ==
              APP_ADMISSION_OK,
          "unsigned_native_reverify_for_ui_binding");
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    trust.app_id = verdict.app_id;
    trust.app_id_len = strlen(verdict.app_id);
    trust.native_tbs_sha256 = verdict.tbs_sha256;
    ui_result = app_sidecar_verify_bundle(ui, ui_len, &trust, NULL, NULL, &ui_verdict);
    if (ui_result != APP_SIDECAR_OK) {
        printf("unsigned_ui_result=%s\n", app_sidecar_result_str(ui_result));
    }
    check(ui_result == APP_SIDECAR_OK,
          "unsigned_ui_accept");
    check(ui_verdict.signature_len == 0u, "unsigned_ui_signature_absent");
    check(ui_verdict.file_count == 1u, "unsigned_ui_member_count");
    check(ui_verdict.version_major == 1u && ui_verdict.version_minor == 2u &&
              ui_verdict.version_patch == 3u,
          "unsigned_ui_version");
    check(ui_verdict.native_version[0] == 1u && ui_verdict.native_version[1] == 0u &&
              ui_verdict.native_version[2] == 0u,
          "unsigned_ui_native_binding_version");
    bad = (unsigned char *)malloc(ui_len);
    check(bad != NULL, "ui_fixture_clone_allocated");
    if (bad != NULL) {
        memcpy(bad, ui, ui_len);
        memcpy(bad + 16u + APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE + 38u,
               "../evil.tx", 10u);
        check(app_sidecar_verify_bundle(bad, ui_len, &trust, NULL, NULL, &ui_verdict) ==
                  APP_SIDECAR_ERR_BAD_FORMAT,
              "unsigned_ui_path_traversal_rejected");
        memcpy(bad, ui, ui_len);
        bad[4] = 1u;
        check(app_sidecar_verify_bundle(bad, ui_len, &trust, NULL, NULL, &ui_verdict) ==
                  APP_SIDECAR_ERR_LEGACY_SIGNED_FORMAT,
              "legacy_signed_ui_rejected");
        memcpy(bad, ui, ui_len);
        bad[ui_len - 1u] ^= 1u;
        check(app_sidecar_verify_bundle(bad, ui_len, &trust, NULL, NULL, &ui_verdict) ==
                  APP_SIDECAR_ERR_ASSET_INVALID,
              "unsigned_ui_hash_mismatch_rejected");
        memcpy(bad, ui, ui_len);
        memset(bad + 8u, 0xFF, 4u);
        check(app_sidecar_verify_bundle(bad, ui_len, &trust, NULL, NULL, &ui_verdict) ==
                  APP_SIDECAR_ERR_LIMIT,
              "unsigned_ui_length_overflow_rejected");
        free(bad);
    }
    free(ui);
    check(app_admission_verify_package(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN,
                                       &policy, &legacy_trust, &verdict) ==
              APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT,
          "legacy_signed_native_explicitly_rejected");
    check(app_admission_verify_package(data, len - 1u, &policy, NULL, &verdict) ==
              APP_ADMISSION_ERR_BAD_PACKAGE,
          "unsigned_truncated_rejected");
    bad = (unsigned char *)malloc(len);
    check(bad != NULL, "fixture_clone_allocated");
    if (bad != NULL) {
        memcpy(bad, data, len);
        bad[16u + 96u] ^= 1u;
        check(app_admission_verify_package(bad, len, &policy, NULL, &verdict) ==
                  APP_ADMISSION_ERR_BAD_PACKAGE,
              "unsigned_hash_mismatch_rejected");
        free(bad);
    }
    free(data);
    test_version_boundary_rejections(&policy);
    test_unsigned_install_lifecycle(&policy);
    printf("test_unsigned_admission: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
