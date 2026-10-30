#include <stdio.h>
#include <string.h>

#include "app_admission.h"
#include "app_installer.h"
#include "app_native_vnext_floor.h"
#include "v3_vectors.h"
#include "v4_vectors.h"
#include "mem_io_backend.h"

#define APP37_PACKAGE_CAP 512u
#define APP37_SPKI_CAP 128u

static int g_total;
static int g_fail;
static app_admission_policy_t g_policy;
static app_uninstall_purge_result_t g_purge_result;

typedef struct {
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
} app37_install_context_t;

#define CHECK(v) do { \
    g_total++; \
    if (!(v)) { \
        g_fail++; \
        printf("FAIL app_native_vnext:%d %s\n", g_total, #v); \
    } \
} while (0)

static void policy_fill(app_admission_policy_t *policy, uint32_t major,
                        uint32_t caps)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = (major == 4u) ? APP_ADMISSION_ABI_VERSION_V4 :
                                          APP_ADMISSION_ABI_VERSION_V3;
    policy->abi_table_size = (major == 4u) ? APP_ADMISSION_V4_ABI_TABLE_SIZE :
                                             APP_ADMISSION_V3_ABI_TABLE_SIZE;
    policy->supported_caps_mask = caps;
    policy->exec_region_base = 0x93E00000u;
    policy->exec_region_size = 2u * 1024u * 1024u;
    policy->event_capacity = 8192u;
    policy->report_capacity = 8192u;
    policy->state_quota_capacity = 8192u;
    policy->control_capacity = 1024u;
    policy->snapshot_capacity = 4096u;
    policy->live_publish_capacity = 9008u;
    policy->live_track_capacity = 64u;
    policy->max_package_len = (major == 4u) ? APP_ADMISSION_MAX_PACKAGE_LEN_V4 :
                                               APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    policy->v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
}

static app_admission_trust_view_t fixture_trust(void)
{
    app_admission_trust_view_t trust;
    trust.publisher_id = "ne301-exp-pub";
    trust.publisher_id_len = strlen(trust.publisher_id);
    trust.spki_der = V4_TEST_SPKI_DER;
    trust.spki_der_len = V4_TEST_SPKI_DER_LEN;
    return trust;
}

static app_admission_result_t fixture_verify(const uint8_t *data, size_t len,
                                             app_admission_verdict_t *out,
                                             void *ctx)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust = fixture_trust();
    uint32_t major = app_admission_container_version(data, len);
    uint32_t caps = (major == 4u) ? APP_ADMISSION_V4_KNOWN_CAPS_MASK :
                                    APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    (void)ctx;
    policy_fill(&policy, major, caps);
    return app_admission_verify_package(data, len, &policy, &trust, out);
}

static app_admission_result_t fixture_verify_alt_owner(
    const uint8_t *data, size_t len, app_admission_verdict_t *out, void *ctx)
{
    app_admission_trust_view_t trust = fixture_trust();
    app_admission_policy_t policy;
    uint32_t major = app_admission_container_version(data, len);
    (void)ctx;
    trust.publisher_id = "other-publisher";
    trust.publisher_id_len = strlen(trust.publisher_id);
    policy_fill(&policy, major, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    return app_admission_verify_package(data, len, &policy, &trust, out);
}

static app_admission_result_t verify(const uint8_t *package, size_t len,
                                     app_admission_policy_t *policy)
{
    app_admission_trust_view_t trust = fixture_trust();
    app_admission_verdict_t verdict;
    return app_admission_verify_package(package, len, policy, &trust, &verdict);
}

static size_t read_fixture(const char *path, uint8_t *buf, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    long file_len;
    size_t read_len;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 0u;
    }
    file_len = ftell(file);
    if (file_len <= 0L || (size_t)file_len > capacity || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 0u;
    }
    read_len = fread(buf, 1u, (size_t)file_len, file);
    fclose(file);
    return read_len == (size_t)file_len ? read_len : 0u;
}

static void print_digest(const uint8_t *digest)
{
    size_t i;
    for (i = 0u; i < APP_ADMISSION_SHA256_LEN; i++) {
        printf("%02x", digest[i]);
    }
}

static void print_app37_result(const char *name, size_t package_len,
                               app_admission_result_t rc,
                               const app_admission_verdict_t *verdict)
{
    printf("APP37_C case=%s package_len=%zu result=%s publisher=%s caps=0x%08x tbs_sha256=",
           name, package_len, app_admission_result_str(rc), verdict->publisher_id,
           (unsigned int)verdict->required_host_caps);
    print_digest(verdict->tbs_sha256);
    printf(" image_sha256=");
    print_digest(verdict->image_sha256);
    printf(" spki_sha256=");
    print_digest(verdict->publisher_key_sha256);
    printf("\n");
}

static app_admission_result_t app37_install_verify(const uint8_t *data, size_t len,
                                                   app_admission_verdict_t *out,
                                                   void *ctx)
{
    const app37_install_context_t *install_ctx = (const app37_install_context_t *)ctx;
    return app_admission_verify_package(data, len, &install_ctx->policy,
                                        &install_ctx->trust, out);
}

static app_uninstall_purge_result_t purge_cb(void *ctx,
                                             const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return g_purge_result;
}

static void test_signed_admission(void)
{
    app_admission_verdict_t verdict;
    app_admission_trust_view_t trust = fixture_trust();
    app_admission_policy_t policy;
    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    CHECK(app_admission_verify_package(V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN,
                                       &policy, &trust, &verdict) == APP_ADMISSION_OK);
    CHECK(verdict.container_major == 4u && verdict.required_host_abi == 0x00040000u);
    CHECK(verdict.required_host_caps == 0x63u && verdict.run_profile == 3u);
    CHECK(app_admission_container_version(V4_GENERIC_PACKAGE,
                                         V4_GENERIC_PACKAGE_LEN) == 4u);
    CHECK(verify(V4_GENERIC_NO_CONTROL_PACKAGE,
                 V4_GENERIC_NO_CONTROL_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_ENABLE_PACKAGE, V4_ENABLE_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_LIVE_PACKAGE, V4_LIVE_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_LIVE_64_PACKAGE, V4_LIVE_64_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
    CHECK(verify(V4_LIVE_SMALL_CLOSURE_PACKAGE,
                 V4_LIVE_SMALL_CLOSURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_RESOURCE_LIMIT);
    CHECK(verify(V4_LIVE_ZERO_TRACK_PACKAGE,
                 V4_LIVE_ZERO_TRACK_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_ENABLE_MISSING_DEPS_PACKAGE,
                 V4_ENABLE_MISSING_DEPS_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_ENABLE_SMALL_CONTROL_PACKAGE,
                 V4_ENABLE_SMALL_CONTROL_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_RESOURCE_LIMIT);
    CHECK(verify(V4_UNKNOWN_CAP_PACKAGE, V4_UNKNOWN_CAP_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_MAGIC_PACKAGE, V4_BAD_MAGIC_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_MANIFEST_LENGTH_PACKAGE,
                 V4_BAD_MANIFEST_LENGTH_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_RESERVED_PACKAGE, V4_BAD_RESERVED_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_TRAILING_BYTE_PACKAGE, V4_TRAILING_BYTE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_MANIFEST_ABI_PACKAGE,
                 V4_BAD_MANIFEST_ABI_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_BAD_NATIVE_ABI_PACKAGE,
                 V4_BAD_NATIVE_ABI_PACKAGE_LEN, &policy) == APP_ADMISSION_ERR_BAD_PACKAGE);
    CHECK(verify(V4_WRONG_PUBLISHER_PACKAGE,
                 V4_WRONG_PUBLISHER_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED);
    CHECK(verify(V4_WRONG_TARGET_PACKAGE, V4_WRONG_TARGET_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_TARGET_INCOMPATIBLE);
    CHECK(verify(V4_FOREIGN_SIGNATURE_PACKAGE,
                 V4_FOREIGN_SIGNATURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_SIGNATURE_INVALID);
    CHECK(verify(V4_ROTATED_KEY_PACKAGE,
                 V4_ROTATED_KEY_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED);
    CHECK(verify(V4_TAMPERED_TBS_PACKAGE,
                 V4_TAMPERED_TBS_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_SIGNATURE_INVALID);
    CHECK(verify(V4_TAMPERED_SIGNATURE_PACKAGE,
                 V4_TAMPERED_SIGNATURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_SIGNATURE_INVALID);
    policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    CHECK(verify(V4_ENABLE_PACKAGE, V4_ENABLE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_ABI_INCOMPATIBLE);
    CHECK(verify(V4_LIVE_PACKAGE, V4_LIVE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_ABI_INCOMPATIBLE);
    policy_fill(&policy, 3u, APP_ADMISSION_V3_KNOWN_CAPS_MASK);
    CHECK(verify(V3_GENERIC_MINIMAL_PACKAGE,
                 V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy) == APP_ADMISSION_OK);
}

static void test_app37_signed_package_interop(void)
{
    uint8_t golden[APP37_PACKAGE_CAP];
    uint8_t live[APP37_PACKAGE_CAP];
    uint8_t enable[APP37_PACKAGE_CAP];
    uint8_t test_spki[APP37_SPKI_CAP];
    uint8_t dev_spki[APP37_SPKI_CAP];
    size_t golden_len;
    size_t live_len;
    size_t enable_len;
    size_t test_spki_len;
    size_t dev_spki_len;
    app_admission_verdict_t verdict;
    app_admission_policy_t policy;
    app_admission_policy_t live_denied_policy;
    app_admission_policy_t cross_major_baseline_policy;
    app_admission_trust_view_t trust;
    app_admission_trust_view_t cross_major_trust;
    app37_install_context_t install_ctx;
    app_install_io_t io;
    app_install_record_t record;
    app_install_result_t install_rc;
    app_admission_result_t rc;

    golden_len = read_fixture("fixtures/app-pr37/golden.neapp.v4", golden,
                              sizeof(golden));
    live_len = read_fixture("fixtures/app-pr37/live-only.neapp.v4", live,
                            sizeof(live));
    enable_len = read_fixture("fixtures/app-pr37/enable.neapp.v4", enable,
                              sizeof(enable));
    test_spki_len = read_fixture("fixtures/app-pr37/test-publisher.spki.der",
                                 test_spki, sizeof(test_spki));
    dev_spki_len = read_fixture("fixtures/app-pr37/dev-publisher.spki.der",
                                dev_spki, sizeof(dev_spki));
    CHECK(golden_len != 0u && live_len != 0u && enable_len != 0u);
    CHECK(test_spki_len != 0u && dev_spki_len != 0u);

    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    trust.publisher_id = "test-publisher";
    trust.publisher_id_len = strlen(trust.publisher_id);
    trust.spki_der = test_spki;
    trust.spki_der_len = test_spki_len;
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(golden, golden_len, &policy, &trust, &verdict);
    CHECK(rc == APP_ADMISSION_OK);
    print_app37_result("tracked-golden", golden_len, rc, &verdict);

    trust.publisher_id = "dev-publisher";
    trust.publisher_id_len = strlen(trust.publisher_id);
    trust.spki_der = dev_spki;
    trust.spki_der_len = dev_spki_len;
    cross_major_trust.publisher_id = "ne301-exp-pub";
    cross_major_trust.publisher_id_len = strlen(cross_major_trust.publisher_id);
    cross_major_trust.spki_der = V4_TEST_SPKI_DER;
    cross_major_trust.spki_der_len = V4_TEST_SPKI_DER_LEN;
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(live, live_len, &policy, &trust, &verdict);
    CHECK(rc == APP_ADMISSION_OK);
    CHECK(verdict.required_host_caps == 0x223u && verdict.control_max == 0u);
    CHECK(verdict.live_publish_max == 9008u && verdict.live_track_max == 64u);
    CHECK(verdict.signature_len == 70u);
    print_app37_result("live-only", live_len, rc, &verdict);

    {
        uint8_t altered[APP37_PACKAGE_CAP];
        app_admission_trust_view_t wrong_spki = trust;

        memcpy(altered, live, live_len);
        altered[224u + 32u] ^= 1u;
        rc = app_admission_verify_package(altered, live_len, &policy, &trust,
                                          &verdict);
        CHECK(rc == APP_ADMISSION_ERR_SIGNATURE_INVALID);
        printf("APP37_C negative=tbs-tamper result=%s\n",
               app_admission_result_str(rc));
        memcpy(altered, live, live_len);
        altered[live_len - 1u] ^= 1u;
        rc = app_admission_verify_package(altered, live_len, &policy, &trust,
                                          &verdict);
        CHECK(rc == APP_ADMISSION_ERR_SIGNATURE_INVALID);
        printf("APP37_C negative=signature-tamper result=%s\n",
               app_admission_result_str(rc));
        rc = app_admission_verify_package(live, live_len - 70u, &policy, &trust,
                                          &verdict);
        CHECK(rc == APP_ADMISSION_ERR_BAD_PACKAGE);
        printf("APP37_C negative=unsigned result=%s\n",
               app_admission_result_str(rc));
        wrong_spki.spki_der = test_spki;
        wrong_spki.spki_der_len = test_spki_len;
        rc = app_admission_verify_package(live, live_len, &policy, &wrong_spki,
                                          &verdict);
        CHECK(rc == APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED);
        printf("APP37_C negative=wrong-spki result=%s\n",
               app_admission_result_str(rc));
        policy_fill(&cross_major_baseline_policy, 3u,
                    APP_ADMISSION_V3_KNOWN_CAPS_MASK);
        rc = app_admission_verify_package(V3_GENERIC_MINIMAL_PACKAGE,
                                          V3_GENERIC_MINIMAL_PACKAGE_LEN,
                                          &cross_major_baseline_policy,
                                          &cross_major_trust, &verdict);
        CHECK(rc == APP_ADMISSION_OK);
        printf("APP37_C cross-major-baseline=accepted_v3 result=%s\n",
               app_admission_result_str(rc));
        rc = app_admission_verify_package(V3_GENERIC_MINIMAL_PACKAGE,
                                          V3_GENERIC_MINIMAL_PACKAGE_LEN,
                                          &policy, &cross_major_trust, &verdict);
        CHECK(rc != APP_ADMISSION_OK);
        printf("APP37_C negative=cross-major result=%s\n",
               app_admission_result_str(rc));
    }

    install_ctx.policy = policy;
    install_ctx.trust = trust;
    mem_reset();
    mem_io_init(&io);
    install_rc = app_install(&io, app37_install_verify, &install_ctx, live,
                             live_len, &record);
    CHECK(install_rc == APP_INSTALL_OK);
    printf("APP37_C install=live-only result=%s\n",
           install_rc == APP_INSTALL_OK ? "APP_INSTALL_OK" : "APP_INSTALL_REJECTED");

    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(enable, enable_len, &policy, &trust, &verdict);
    CHECK(rc == APP_ADMISSION_OK);
    CHECK(verdict.required_host_caps == 0x173u && verdict.control_max == 256u);
    print_app37_result("enable", enable_len, rc, &verdict);

    install_ctx.policy = policy;
    install_ctx.trust = trust;
    mem_reset();
    mem_io_init(&io);
    install_rc = app_install(&io, app37_install_verify, &install_ctx, enable,
                             enable_len, &record);
    CHECK(install_rc == APP_INSTALL_OK);
    printf("APP37_C install=enable result=%s\n",
           install_rc == APP_INSTALL_OK ? "APP_INSTALL_OK" : "APP_INSTALL_REJECTED");

    live_denied_policy = policy;
    live_denied_policy.supported_caps_mask &= ~APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(live, live_len, &live_denied_policy,
                                      &trust, &verdict);
    CHECK(rc == APP_ADMISSION_ERR_ABI_INCOMPATIBLE);
    printf("APP37_C authorization=live-only supported_caps=0x%08x result=%s\n",
           (unsigned int)live_denied_policy.supported_caps_mask,
           app_admission_result_str(rc));
}

static void test_install_floor_and_confirmed_retirement(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_install_record_t second;
    app_install_record_t retired;
    app_uninstall_cleanup_ops_t ops;
    app_uninstall_state_t uninstall_state;
    app_admission_policy_t policy;
    mem_kv_t *floor_kv;

    mem_reset();
    mem_io_init(&io);
    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    g_policy = policy;
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &record, 1u, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_native_vnext_floor_check(&io, &record, 4u) == APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_verify(&io, &record, 1u, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_verify(&io, &record, 1u, 17u, 10u, 1u, 0u) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_ROTATED_KEY_PACKAGE, V4_ROTATED_KEY_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_PUBLISHER_UNTRUSTED);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify_alt_owner, NULL,
                      V4_WRONG_PUBLISHER_PACKAGE,
                      V4_WRONG_PUBLISHER_PACKAGE_LEN, &second) ==
          APP_INSTALL_ERR_IDENTITY_CONFLICT);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_NEXT_GENERATION_PACKAGE, V4_NEXT_GENERATION_PACKAGE_LEN,
                      &second) == APP_INSTALL_OK);
    CHECK(memcmp(record.tbs_sha256, second.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0);
    CHECK(app_native_vnext_floor_check(&io, &second, 4u) == APP_INSTALL_OK);
    ops.ctx = NULL;
    ops.purge = purge_cb;
    g_purge_result = APP_UNINSTALL_PURGE_UNKNOWN;
    CHECK(app_install_uninstall_confirmed(&io, AICAM_TRUE, &ops, &record,
                                          &uninstall_state) ==
          APP_UNINSTALL_CLEANUP_PENDING);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_install_boot_resolve(&io) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    g_purge_result = APP_UNINSTALL_PURGE_CONFIRMED;
    CHECK(app_install_uninstall_confirmed(&io, AICAM_TRUE, &ops, &record,
                                          &uninstall_state) == APP_UNINSTALL_OK);
    memcpy(&retired, &record, sizeof(retired));
    CHECK(uninstall_state == APP_UNINSTALL_STATE_NONE);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) == NULL);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &record) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN,
                      &second) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &second, 3u, 19u, 11u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_native_vnext_floor_retire_finish(&io, &retired) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_check(&io, &second, 4u) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &record) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);

    mem_reset();
    mem_io_init(&io);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &record, 2u, 18u, 10u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    floor_kv = mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY);
    CHECK(floor_kv != NULL);
    if (floor_kv != NULL) {
        floor_kv->data[floor_kv->len - 1u] ^= 1u;
    }
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
}

static void test_failed_pause_fence_blocks_v3_from_committed_evidence(void)
{
    app_install_io_t io;
    app_install_record_t record;
    app_install_record_t candidate_record;
    app_admission_policy_t policy;
    app_admission_verdict_t candidate;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t v3_rc;
    uint32_t epoch = 0u;
    uint32_t epoch_next = 0u;
    mem_kv_t *epoch_kv;

    mem_reset();
    mem_io_init(&io);
    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);
    g_policy = policy;
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V4_ENABLE_PACKAGE, V4_ENABLE_PACKAGE_LEN,
                      &record) == APP_INSTALL_OK);
    memset(&candidate, 0, sizeof(candidate));
    CHECK(fixture_verify(V3_GENERIC_MINIMAL_PACKAGE,
                         V3_GENERIC_MINIMAL_PACKAGE_LEN, &candidate, NULL) ==
          APP_ADMISSION_OK);
    CHECK(strcmp(record.publisher_id, candidate.publisher_id) == 0);
    CHECK(strcmp(record.app_id, candidate.app_id) == 0);
    app_install_set_app_running(AICAM_TRUE);
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_NATIVE_VNEXT_FLOOR_KEY);
    CHECK(app_native_vnext_floor_raise(&io, &record, 21u, 31u, 41u, 1u, 0u) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(mem_find_key(APP_NATIVE_VNEXT_FLOOR_KEY) == NULL);
    CHECK(app_native_vnext_floor_check(&io, &record, 3u) == APP_INSTALL_OK);
    app_install_boot_resolve_reset();
    v3_rc = app_install(&io, fixture_verify, &g_policy,
                        V3_GENERIC_MINIMAL_PACKAGE,
                        V3_GENERIC_MINIMAL_PACKAGE_LEN, &candidate_record);
    CHECK(v3_rc == APP_INSTALL_ERR_DOWNGRADE_REFUSED);
    printf("HOST_DOWNGRADE_AFTER_FAILED_FENCE=PASS floor_present=0 v3_install=%s\n",
           app_install_result_str(v3_rc));
    CHECK(app_install_query_committed(&io, &candidate_record, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    CHECK(memcmp(candidate_record.package_sha256, record.package_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);

    CHECK(app_native_vnext_floor_epoch_advance(&io, &epoch) == APP_INSTALL_OK);
    CHECK(epoch == 1u);
    CHECK(app_native_vnext_floor_epoch_advance(&io, &epoch_next) == APP_INSTALL_OK);
    CHECK(epoch_next == 2u);
    epoch_kv = mem_find_key(APP_NATIVE_VNEXT_EPOCH_KEY);
    CHECK(epoch_kv != NULL);
    if (epoch_kv != NULL) {
        epoch_kv->data[epoch_kv->len - 1u] ^= 1u;
    }
    CHECK(app_native_vnext_floor_epoch_advance(&io, &epoch_next) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
}

int main(void)
{
    test_signed_admission();
    test_app37_signed_package_interop();
    test_install_floor_and_confirmed_retirement();
    test_failed_pause_fence_blocks_v3_from_committed_evidence();
    printf("app_native_vnext: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
