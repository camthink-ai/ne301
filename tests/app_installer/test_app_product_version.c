#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_admission.h"
#include "app_installer.h"
#include "app_pair_installer.h"
#include "app_sidecar_lifecycle.h"
#include "mem_io_backend.h"

static int g_checks;
static int g_failures;
static app_admission_policy_t g_policy;
static app_install_io_t g_io_iface;

static app_uninstall_purge_result_t purge_confirmed(
    void *ctx, const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_CONFIRMED;
}

typedef struct {
    uint8_t *data;
    size_t len;
} fixture_t;

static void check(int cond, const char *name)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL product-version: %s\n", name);
    }
}

static int fixture_load(const char *name, fixture_t *out)
{
    char path[256];
    FILE *fp;
    long len;

    memset(out, 0, sizeof(*out));
    snprintf(path, sizeof(path), "build/product-version/%s", name);
    fp = fopen(path, "rb");
    if (fp == NULL || fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) <= 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        if (fp != NULL) {
            fclose(fp);
        }
        return 0;
    }
    out->data = (uint8_t *)malloc((size_t)len);
    if (out->data == NULL || fread(out->data, 1u, (size_t)len, fp) != (size_t)len) {
        free(out->data);
        out->data = NULL;
        fclose(fp);
        return 0;
    }
    out->len = (size_t)len;
    fclose(fp);
    return 1;
}

static void fixture_free(fixture_t *fixture)
{
    free(fixture->data);
    memset(fixture, 0, sizeof(*fixture));
}

static app_admission_result_t verify_native(const uint8_t *data, size_t len,
                                             app_admission_verdict_t *out,
                                             void *ctx)
{
    return app_admission_verify_package(data, len,
                                        (const app_admission_policy_t *)ctx,
                                        NULL, out);
}

static void policy_init(void)
{
    memset(&g_policy, 0, sizeof(g_policy));
    g_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    g_policy.psram_mib = 64u;
    g_policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    g_policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    g_policy.supported_caps_mask = APP_ADMISSION_V5_KNOWN_CAPS_MASK;
    g_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    g_policy.event_capacity = 65536u;
    g_policy.report_capacity = 6144u;
    g_policy.state_quota_capacity = 8192u;
    g_policy.control_capacity = 65536u;
    g_policy.snapshot_capacity = 32768u;
    g_policy.exec_region_base = 0x93E00000u;
    g_policy.exec_region_size = 2u * 1024u * 1024u;
    g_policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    g_policy.live_publish_capacity = 9008u;
    g_policy.live_track_capacity = 64u;
}

static void runtime_reset(void)
{
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    app_install_set_app_running(AICAM_FALSE);
}

static void storage_reset(void)
{
    mem_reset();
    mem_io_init(&g_io_iface);
    runtime_reset();
}

static aicam_bool_t corrupt_stored_product_version(const char *native_path)
{
    static const uint8_t marker[] = "{\"product_version\":\"1.0.1\"}";
    static const uint8_t version_prefix[] = "{\"product_version\":\"1.0.";
    size_t file_index;

    for (file_index = 0u; file_index < MEM_MAX_FILES; file_index++) {
        mem_file_t *file = &g_io.files[file_index];
        size_t offset;

        if (file->present != AICAM_TRUE || file->data == NULL ||
            (native_path != NULL && strcmp(file->path, native_path) == 0) ||
            file->len < sizeof(marker) - 1u) {
            continue;
        }
        for (offset = 0u; offset <= file->len - (sizeof(marker) - 1u); offset++) {
            if (memcmp(file->data + offset, marker, sizeof(marker) - 1u) == 0) {
                file->data[offset + sizeof(version_prefix) - 1u] = (uint8_t)'0';
                return AICAM_TRUE;
            }
        }
    }
    return AICAM_FALSE;
}

static app_pair_install_result_t install_fixture(const fixture_t *fixture,
                                                 app_install_record_t *native,
                                                 app_sidecar_descriptor_t *ui,
                                                 app_sidecar_lifecycle_state_t *state)
{
    app_sidecar_trust_t trust;

    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    return app_pair_install(&g_io_iface, verify_native, &g_policy, &trust,
                            fixture->data, fixture->len, native, ui, state);
}

static void test_admission_fixtures(void)
{
    fixture_t valid;
    fixture_t legacy;
    fixture_t tampered;
    fixture_t missing;
    fixture_t malformed;
    fixture_t stray;
    fixture_t malformed_stray;
    fixture_t unsupported_caps = {0};
    app_pair_candidate_t candidate;
    app_admission_policy_t old_host;

    check(fixture_load("valid-product.neapp", &valid) &&
              fixture_load("unsupported-capability.neapp", &unsupported_caps),
          "load_product_and_unsupported_capability_fixtures");
    check(fixture_load("legacy.neapp", &legacy), "load_real_legacy_fixture");
    check(fixture_load("tampered-carrier.neapp", &tampered), "load_tampered_carrier");
    check(fixture_load("missing-carrier.neapp", &missing), "load_missing_carrier");
    check(fixture_load("bad-encoding.neapp", &malformed), "load_bad_encoding");
    check(fixture_load("legacy-with-stray-carrier.neapp", &stray), "load_legacy_stray_carrier");
    check(fixture_load("legacy-with-malformed-stray.neapp", &malformed_stray),
          "load_legacy_malformed_stray");

    check(app_pair_verify_package(valid.data, valid.len, verify_native, &g_policy,
                                  NULL, &candidate) == APP_PAIR_INSTALL_OK,
          "accept_0x7ff_real_fixture");
    check(candidate.product_version_required == AICAM_TRUE &&
              strcmp(candidate.product_version, "1.0.1") == 0,
          "carrier_verified_and_bound_to_capability");
    check(app_pair_verify_package(legacy.data, legacy.len, verify_native, &g_policy,
                                  NULL, &candidate) == APP_PAIR_INSTALL_OK,
          "accept_real_legacy_fixture");
    check(app_pair_verify_package(stray.data, stray.len, verify_native, &g_policy,
                                  NULL, &candidate) == APP_PAIR_INSTALL_OK,
          "legacy_stray_carrier_does_not_break_admission");
    check(candidate.product_version_required == AICAM_FALSE &&
              candidate.product_version[0] == '\0',
          "stray_carrier_does_not_confer_product_version");
    check(app_pair_verify_package(malformed_stray.data, malformed_stray.len,
                                  verify_native, &g_policy, NULL, &candidate) ==
              APP_PAIR_INSTALL_OK,
          "legacy_malformed_stray_carrier_is_ignored");

    check(app_pair_verify_package(tampered.data, tampered.len, verify_native, &g_policy,
                                  NULL, &candidate) == APP_PAIR_INSTALL_ERR_UI_VERIFY,
          "reject_tampered_carrier_hash");
    check(app_pair_verify_package(missing.data, missing.len, verify_native, &g_policy,
                                  NULL, &candidate) == APP_PAIR_INSTALL_ERR_UI_VERIFY,
          "reject_missing_required_carrier");
    check(app_pair_verify_package(malformed.data, malformed.len, verify_native, &g_policy,
                                  NULL, &candidate) == APP_PAIR_INSTALL_ERR_UI_VERIFY,
          "reject_noncanonical_product_version");

    old_host = g_policy;
    old_host.supported_caps_mask = APP_ADMISSION_V4_KNOWN_CAPS_MASK;
    check(app_pair_verify_package(valid.data, valid.len, verify_native, &old_host,
                                  NULL, &candidate) == APP_PAIR_INSTALL_ERR_NATIVE_VERIFY &&
              app_pair_verify_package(unsupported_caps.data, unsupported_caps.len,
                                      verify_native, &g_policy, NULL, &candidate) ==
                  APP_PAIR_INSTALL_ERR_NATIVE_VERIFY,
          "reject_old_host_product_bit_and_new_host_unknown_capability");

    fixture_free(&valid);
    fixture_free(&legacy);
    fixture_free(&tampered);
    fixture_free(&missing);
    fixture_free(&malformed);
    fixture_free(&stray);
    fixture_free(&malformed_stray);
    fixture_free(&unsupported_caps);
}

static void test_canonical_parser(void)
{
    static const uint8_t valid[] = "{\"product_version\":\"1.0.1\"}";
    static const uint8_t leading_zero[] = "{\"product_version\":\"1.0.01\"}";
    static const uint8_t whitespace[] = "{ \"product_version\":\"1.0.1\"}";
    static const uint8_t extra_key[] = "{\"product_version\":\"1.0.1\",\"x\":0}";
    char out[APP_SIDECAR_PRODUCT_VERSION_MAX + 1u];

    memset(out, 0, sizeof(out));
    check(app_sidecar_product_version_parse(valid, sizeof(valid) - 1u, out) == AICAM_TRUE &&
              strcmp(out, "1.0.1") == 0,
          "canonical_carrier_parses");
    check(app_sidecar_product_version_parse(leading_zero,
                                             sizeof(leading_zero) - 1u, out) == AICAM_FALSE,
          "reject_leading_zero_component");
    check(app_sidecar_product_version_parse(whitespace,
                                             sizeof(whitespace) - 1u, out) == AICAM_FALSE,
          "reject_noncanonical_whitespace");
    check(app_sidecar_product_version_parse(extra_key,
                                             sizeof(extra_key) - 1u, out) == AICAM_FALSE,
          "reject_extra_json_key");
}

static void test_product_line(void)
{
    fixture_t product;
    fixture_t legacy;
    fixture_t lower;
    fixture_t same_content_conflict;
    fixture_t lower_after_uninstall;
    app_install_record_t native;
    app_sidecar_descriptor_t ui;
    app_sidecar_lifecycle_state_t state;
    app_install_query_t query;
    app_sidecar_trust_t trust;

    check(fixture_load("valid-product.neapp", &product), "load_product_for_line_tests");
    check(fixture_load("legacy.neapp", &legacy), "load_legacy_for_line_tests");
    check(fixture_load("lower-product.neapp", &lower), "load_lower_product");
    check(fixture_load("same-version-different-content.neapp", &same_content_conflict),
          "load_same_version_content_conflict");
    check(fixture_load("lower-product-new-components.neapp", &lower_after_uninstall),
          "load_lower_product_after_uninstall");

    storage_reset();
    check(install_fixture(&legacy, &native, &ui, &state) == APP_PAIR_INSTALL_OK,
          "install_legacy_package");
    check(install_fixture(&product, &native, &ui, &state) == APP_PAIR_INSTALL_OK,
          "allow_one_way_legacy_to_product_transition");
    check(install_fixture(&product, &native, &ui, &state) == APP_PAIR_INSTALL_ALREADY,
          "identical_product_package_is_idempotent");
    check(install_fixture(&lower, &native, &ui, &state) == APP_PAIR_INSTALL_ERR_DOWNGRADE &&
              install_fixture(&lower_after_uninstall, &native, &ui, &state) ==
                  APP_PAIR_INSTALL_ERR_DOWNGRADE,
          "reject_lower_product_even_with_higher_components");
    check(install_fixture(&same_content_conflict, &native, &ui, &state) ==
              APP_PAIR_INSTALL_ERR_CONTENT_CONFLICT,
          "reject_same_product_version_different_bytes");
    check(install_fixture(&legacy, &native, &ui, &state) == APP_PAIR_INSTALL_ERR_DOWNGRADE,
          "reject_product_to_legacy_transition");

    runtime_reset();
    memset(&query, 0, sizeof(query));
    check(app_install_query(&g_io_iface, verify_native, &g_policy, &query) ==
              APP_INSTALL_OK && query.state == APP_INSTALL_STATE_INSTALLED,
          "requery_native_record_after_restart");
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    trust.app_id = query.record.app_id;
    trust.app_id_len = strlen(query.record.app_id);
    trust.native_tbs_sha256 = query.record.tbs_sha256;
    memset(&ui, 0, sizeof(ui));
    state = APP_SIDECAR_LIFECYCLE_NONE;
    check(app_sidecar_lifecycle_query(&g_io_iface, &trust, &query.record,
                                     &ui, &state) == APP_SIDECAR_LIFECYCLE_OK &&
              state == APP_SIDECAR_LIFECYCLE_ACTIVE &&
              ui.product_version_trusted == AICAM_TRUE &&
              strcmp(ui.product_version, "1.0.1") == 0,
          "requery_trusted_version_from_stored_asset_after_restart");
    check(install_fixture(&product, &native, &ui, &state) == APP_PAIR_INSTALL_ALREADY &&
              corrupt_stored_product_version(native.package_path) == AICAM_TRUE &&
              install_fixture(&product, &native, &ui, &state) ==
                  APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN,
          "restart_requery_and_corrupt_stored_product_version_fail_closed");

    storage_reset();
    check(install_fixture(&product, &native, &ui, &state) == APP_PAIR_INSTALL_OK,
          "install_product_before_uninstall");
    check(count_files() > 0, "product_package_and_assets_are_persisted");
    {
        app_uninstall_cleanup_ops_t cleanup;
        app_uninstall_state_t uninstall_state;
        app_uninstall_result_t uninstall_result;
        memset(&cleanup, 0, sizeof(cleanup));
        cleanup.purge = purge_confirmed;
        uninstall_result = app_install_uninstall_confirmed(
            &g_io_iface, AICAM_TRUE, &cleanup, &native, &uninstall_state);
        check(uninstall_result == APP_UNINSTALL_OK,
              "uninstall_product_package_cleans_version_line");
        check(count_files() == 0, "uninstall_removes_product_assets_and_package");
    }
    runtime_reset();
    check(install_fixture(&lower_after_uninstall, &native, &ui, &state) ==
              APP_PAIR_INSTALL_OK,
          "no_product_version_floor_after_uninstall");

    fixture_free(&product);
    fixture_free(&legacy);
    fixture_free(&lower);
    fixture_free(&same_content_conflict);
    fixture_free(&lower_after_uninstall);
}

int main(void)
{
    policy_init();
    test_canonical_parser();
    test_admission_fixtures();
    test_product_line();
    printf("app_product_version: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
