#include <stdio.h>
#include <string.h>

#include "app_admission.h"
#include "app_installer.h"
#include "app_native_floor.h"
#include "v3_vectors.h"
#include "v4_vectors.h"
#include "v5_vectors.h"
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
    policy->abi_version = (major >= 4u) ? APP_ADMISSION_ABI_VERSION_V4 :
                                          APP_ADMISSION_ABI_VERSION_V3;
    policy->abi_table_size = (major >= 4u) ? APP_ADMISSION_V4_ABI_TABLE_SIZE :
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
    policy->max_package_len = (major >= 4u) ? APP_ADMISSION_MAX_PACKAGE_LEN_V4 :
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
    uint32_t caps = (major >= 4u) ? APP_ADMISSION_V4_KNOWN_CAPS_MASK :
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





static app_uninstall_purge_result_t purge_cb(void *ctx,
                                             const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return g_purge_result;
}

static void test_legacy_containers_rejected(void)
{
    app_admission_verdict_t verdict;
    app_admission_policy_t policy;
    policy_fill(&policy, 4u, APP_ADMISSION_V4_KNOWN_CAPS_MASK);

    CHECK(app_admission_verify_package(V4_GENERIC_PACKAGE, V4_GENERIC_PACKAGE_LEN,
                                       &policy, NULL, &verdict) ==
          APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT);
    CHECK(app_admission_container_version(V4_GENERIC_PACKAGE,
                                          V4_GENERIC_PACKAGE_LEN) == 4u);
    CHECK(verify(V4_LIVE_ZERO_TRACK_PACKAGE,
                 V4_LIVE_ZERO_TRACK_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT);
    CHECK(verify(V4_WRONG_PUBLISHER_PACKAGE,
                 V4_WRONG_PUBLISHER_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT);
    CHECK(verify(V4_FOREIGN_SIGNATURE_PACKAGE,
                 V4_FOREIGN_SIGNATURE_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT);
    policy_fill(&policy, 3u, APP_ADMISSION_V3_KNOWN_CAPS_MASK);
    CHECK(verify(V3_GENERIC_MINIMAL_PACKAGE,
                 V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy) ==
          APP_ADMISSION_ERR_LEGACY_SIGNED_FORMAT);
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
                      V5_GENERIC_PACKAGE, V5_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &record, 1u, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_native_vnext_floor_check(&io, &record, 5u) == APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_verify(&io, &record, 1u, 17u, 9u, 1u, 0u) ==
          APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_verify(&io, &record, 1u, 17u, 10u, 1u, 0u) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V5_OTHER_APP_PACKAGE, V5_OTHER_APP_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_IDENTITY_CONFLICT);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify_alt_owner, NULL,
                      V5_OTHER_APP_PACKAGE,
                      V5_OTHER_APP_PACKAGE_LEN, &second) ==
          APP_INSTALL_ERR_IDENTITY_CONFLICT);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V5_DOWNGRADE_PACKAGE, V5_DOWNGRADE_PACKAGE_LEN,
                      &second) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V5_UPGRADE_PACKAGE, V5_UPGRADE_PACKAGE_LEN,
                      &second) == APP_INSTALL_OK);
    CHECK(memcmp(record.tbs_sha256, second.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0);
    CHECK(app_native_vnext_floor_check(&io, &second, 5u) == APP_INSTALL_OK);
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
                      V5_DOWNGRADE_PACKAGE, V5_DOWNGRADE_PACKAGE_LEN,
                      &record) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V5_GENERIC_PACKAGE, V5_GENERIC_PACKAGE_LEN,
                      &second) == APP_INSTALL_OK);
    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_native_vnext_floor_raise(&io, &second, 3u, 19u, 11u, 1u, 0u) ==
          APP_INSTALL_OK);
    app_install_set_app_running(AICAM_FALSE);
    CHECK(app_native_vnext_floor_retire_finish(&io, &retired) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_native_vnext_floor_validate(&io) == APP_INSTALL_OK);
    CHECK(app_native_vnext_floor_check(&io, &second, 5u) == APP_INSTALL_OK);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V5_DOWNGRADE_PACKAGE, V5_DOWNGRADE_PACKAGE_LEN,
                      &record) == APP_INSTALL_ERR_DOWNGRADE_REFUSED);

    mem_reset();
    mem_io_init(&io);
    CHECK(app_install(&io, fixture_verify, &g_policy,
                      V5_GENERIC_PACKAGE, V5_GENERIC_PACKAGE_LEN, &record) == APP_INSTALL_OK);
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
                      V5_DOWNGRADE_PACKAGE, V5_DOWNGRADE_PACKAGE_LEN,
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
                      V5_ENABLE_PACKAGE, V5_ENABLE_PACKAGE_LEN,
                      &record) == APP_INSTALL_OK);
    memset(&candidate, 0, sizeof(candidate));
    CHECK(fixture_verify(V5_DOWNGRADE_PACKAGE,
                         V5_DOWNGRADE_PACKAGE_LEN, &candidate, NULL) ==
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
    CHECK(app_native_vnext_floor_check(&io, &record, 5u) == APP_INSTALL_OK);
    app_install_boot_resolve_reset();
    v3_rc = app_install(&io, fixture_verify, &g_policy,
                        V5_DOWNGRADE_PACKAGE,
                        V5_DOWNGRADE_PACKAGE_LEN, &candidate_record);
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
    test_legacy_containers_rejected();
    test_install_floor_and_confirmed_retirement();
    test_failed_pause_fence_blocks_v3_from_committed_evidence();
    printf("app_native_vnext: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
