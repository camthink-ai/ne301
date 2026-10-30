#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_installer.h"
#include "app_trust_anchor.h"
#include "installer_vectors.h"
#include "mem_io_backend.h"

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_MSG(cond, what) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
    } \
} while (0)

static app_admission_policy_t g_policy;

static app_admission_result_t test_verify(const uint8_t *data, size_t len,
                                          app_admission_verdict_t *out, void *ctx)
{
    app_admission_policy_t *policy = (app_admission_policy_t *)ctx;
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_trust_view_t trust;

    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    return app_admission_verify_package(data, len, policy, &trust, out);
}

static void fill_policy(void)
{
    memset(&g_policy, 0, sizeof(g_policy));
    g_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    g_policy.psram_mib = 64u;
    g_policy.abi_version = 0x00010000u;
    g_policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    g_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    g_policy.exec_region_base = 0x93E00000u;
    g_policy.exec_region_size = 2u * 1024u * 1024u;
}

static app_install_io_t g_hio;

static void harness_reset(void)
{
    mem_reset();
    mem_io_init(&g_hio);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
}

static void simulate_reboot(void)
{
    app_install_boot_resolve_reset();
    g_io.tx_held = 0;
}

static void install_v1_ok(void)
{
    app_install_record_t rec;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
}

static void install_v2_ok(void)
{
    app_install_record_t rec;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
}

static void test_stage_write_failure_keeps_old(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    char v1_path[APP_INSTALL_PATH_MAX];

    harness_reset();
    install_v1_ok();
    g_hio.ctx = &g_io;
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    snprintf(v1_path, sizeof(v1_path), "%s", query.record.package_path);

    g_io.fail_next_file_writes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_WRITE_FAILED);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);
    CHECK(strcmp(query.record.package_path, v1_path) == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);
}

static void test_stage_readback_failure_keeps_old(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    g_io.fail_next_file_reads = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_WRITE_FAILED);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 1u);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);
}

static void test_storage_full_refused(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    g_io.free_bytes_value = (uint64_t)HELLO_V1_PACKAGE_LEN;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_FULL);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    g_io.free_bytes_value = (uint64_t)HELLO_V1_PACKAGE_LEN * 2u;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    g_io.free_bytes_value = (uint64_t)HELLO_V2_PACKAGE_LEN * 2u - 1u;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_FULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 1u);
    CHECK(count_files() == 1);
}

static void test_capacity_unknown_fail_closed(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    app_install_io_t nio;
    mem_file_t *f;
    uint8_t corrupted;

    harness_reset();
    g_io.free_bytes_enabled = AICAM_FALSE;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    g_io.free_bytes_enabled = AICAM_TRUE;
    install_v1_ok();
    g_io.free_bytes_enabled = AICAM_FALSE;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);

    memset(&nio, 0, sizeof(nio));
    nio.ctx = &g_io;
    nio.file_write = mem_file_write;
    nio.file_read = mem_file_read;
    nio.file_remove = mem_file_remove;
    nio.file_stat = mem_file_stat;
    nio.kv_write = mem_kv_write;
    nio.kv_read = mem_kv_read;
    nio.kv_delete = mem_kv_delete;
    nio.tx_begin = mem_tx_begin;
    nio.tx_end = mem_tx_end;
    CHECK(app_install(&nio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_PARAM);
    CHECK(count_files() == 1);

    g_io.free_bytes_enabled = AICAM_TRUE;
    install_v2_ok();
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    f = mem_find_file(query.record.package_path);
    CHECK(f != NULL);
    f->data[40] ^= 0xFFu;
    corrupted = f->data[40];
    g_io.free_bytes_enabled = AICAM_FALSE;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(f->data[40] == corrupted);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "content_mismatch") == 0);
    CHECK(strcmp(query.record.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(query.record.app_id, "hello-app") == 0);
    CHECK(query.record.version_major == 2u);

    g_io.free_bytes_enabled = AICAM_TRUE;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 2u);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
}

static void test_stage_stat_io_error_fail_closed(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    char v2_path[APP_INSTALL_PATH_MAX];

    harness_reset();
    g_io.fail_next_file_stats = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    g_io.fail_next_file_stats = 0;
    install_v1_ok();
    install_v2_ok();
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    snprintf(v2_path, sizeof(v2_path), "%s", query.record.package_path);

    g_io.fail_next_file_stats = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(count_files() == 1);
    CHECK(mem_find_file(v2_path) != NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    g_io.fail_next_file_stats = 1;
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    g_io.fail_next_file_stats = 0;
    g_io.fail_next_file_reads = 1;
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    g_io.fail_next_file_stats = 1;
    CHECK(app_install_uninstall(&g_hio, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    g_io.fail_next_file_stats = 0;

    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(strcmp(query.record.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(query.record.app_id, "hello-app") == 0);
    CHECK(query.record.version_major == 2u);
    CHECK(query.record.package_len == HELLO_V2_PACKAGE_LEN);
}

static void test_marker_write_fail_proven_absent_rolls_back(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    g_io.fail_next_kv_writes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_WRITE_FAILED);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 1u);
    CHECK(count_files() == 1);
}

static void test_marker_write_unprovable_state_unknown_then_recover(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    g_io.fail_next_kv_writes = 1;
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s",
             APP_INSTALL_KEY_PENDING);
    g_io.fail_kv_read_key_skip = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    g_io.fail_kv_read_key[0] = '\0';
    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
}

static void test_record_write_fail_rollback_proven(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_INSTALL_KEY_RECORD);
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_COMMIT_FAILED);
    g_io.fail_kv_write_key[0] = '\0';
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 1u);
    CHECK(count_files() == 2);
}

static void test_commit_unproven_boot_resolve_restores_old(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    g_io.fail_next_kv_deletes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) != NULL);

    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 2);
}

static void test_first_install_crash_boot_resolve_clears(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_INSTALL_KEY_RECORD);
    g_io.fail_next_kv_deletes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    g_io.fail_kv_write_key[0] = '\0';
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) != NULL);

    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
}

static void test_torn_marker_fail_closed(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    mem_kv_t *kv;
    uint8_t blob[APP_INSTALL_PENDING_BIN_SIZE];

    harness_reset();
    install_v1_ok();

    g_io.fail_next_kv_deletes = 2;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    simulate_reboot();

    kv = mem_find_key(APP_INSTALL_KEY_PENDING);
    CHECK(kv != NULL);
    memcpy(blob, kv->data, sizeof(blob));
    blob[10] ^= 0x01u;
    memcpy(kv->data, blob, sizeof(blob));

    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_uninstall(&g_hio, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) != NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
}

static void test_marker_read_failure_fail_closed_then_recover(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    g_io.fail_next_kv_deletes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) != NULL);

    simulate_reboot();
    g_io.fail_next_kv_reads = 1;
    g_io.tx_held = 0;
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 1u);
}

static void test_record_read_failure_state_unknown(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    simulate_reboot();
    g_io.fail_next_kv_reads = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_uninstall(&g_hio, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.record.version_major == 1u);
}

static void test_torn_record_fail_closed(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    mem_kv_t *kv;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];

    harness_reset();
    install_v1_ok();

    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    memcpy(blob, kv->data, sizeof(blob));
    blob[30] ^= 0x80u;
    memcpy(kv->data, blob, sizeof(blob));

    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_uninstall(&g_hio, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
}

static void test_uninstall_fault_windows(void)
{
    app_install_record_t removed;
    app_install_query_t query;

    harness_reset();
    install_v1_ok();

    g_io.fail_next_kv_writes = 1;
    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_ERR_WRITE_FAILED);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    g_io.fail_next_kv_deletes = 1;
    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_ERR_COMMIT_FAILED);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    g_io.fail_next_kv_deletes = 2;
    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) != NULL);
    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);

    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);
}

static void test_reboot_keeps_committed_identity(void)
{
    app_install_query_t query;

    harness_reset();
    install_v1_ok();
    install_v2_ok();

    simulate_reboot();
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 2u);
    CHECK(strcmp(query.record.publisher_id, "ne301-exp-pub") == 0);
    CHECK(strcmp(query.record.app_id, "hello-app") == 0);
    CHECK(query.record.package_len == HELLO_V2_PACKAGE_LEN);
    CHECK(count_files() == 1);
}

static void test_repair_corrupted_installed_same_content(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    mem_file_t *f;

    harness_reset();
    install_v2_ok();

    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    f = mem_find_file(query.record.package_path);
    CHECK(f != NULL);
    f->data[40] ^= 0xFFu;

    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "content_mismatch") == 0);
    CHECK(query.record.version_major == 2u);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(memcmp(f->data, HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN) == 0);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 2u);
    CHECK(query.record.package_len == HELLO_V2_PACKAGE_LEN);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
}

static void test_repair_missing_file_alt_signature(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    char old_path[APP_INSTALL_PATH_MAX];
    mem_file_t *f;

    harness_reset();
    install_v2_ok();
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    snprintf(old_path, sizeof(old_path), "%s", query.record.package_path);
    CHECK(mem_file_remove(&g_io, old_path) == APP_INSTALL_IO_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "file_missing") == 0);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_HISIG_PACKAGE,
                      HELLO_V2_HISIG_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(rec.package_len == HELLO_V2_HISIG_PACKAGE_LEN);
    CHECK(strcmp(rec.package_path, old_path) != 0);
    f = mem_find_file(rec.package_path);
    CHECK(f != NULL);
    CHECK(f->len == HELLO_V2_HISIG_PACKAGE_LEN);
    CHECK(memcmp(f->data, HELLO_V2_HISIG_PACKAGE, HELLO_V2_HISIG_PACKAGE_LEN) == 0);
    CHECK(mem_find_file(old_path) == NULL);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.package_len == HELLO_V2_HISIG_PACKAGE_LEN);
    CHECK(strcmp(query.record.package_path, rec.package_path) == 0);
    CHECK(memcmp(query.record.package_sha256, rec.package_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK_ALREADY);
    CHECK(count_files() == 1);
    CHECK(mem_find_file(rec.package_path) != NULL);
}

static void test_repair_corrupt_alt_signature_write_fault_keeps_old(void)
{
    app_install_record_t rec;
    app_install_query_t query;
    char old_path[APP_INSTALL_PATH_MAX];
    mem_file_t *f;
    mem_kv_t *kv;
    uint8_t old_blob[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t corrupt_byte;

    harness_reset();
    install_v2_ok();
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    snprintf(old_path, sizeof(old_path), "%s", query.record.package_path);
    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    memcpy(old_blob, kv->data, sizeof(old_blob));
    f = mem_find_file(old_path);
    CHECK(f != NULL);
    f->data[40] ^= 0xFFu;
    corrupt_byte = f->data[40];

    g_io.fail_next_file_writes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_HISIG_PACKAGE,
                      HELLO_V2_HISIG_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_WRITE_FAILED);
    g_io.fail_next_file_writes = 0;
    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    CHECK(memcmp(kv->data, old_blob, sizeof(old_blob)) == 0);
    f = mem_find_file(old_path);
    CHECK(f != NULL);
    CHECK(f->len == HELLO_V2_PACKAGE_LEN);
    CHECK(f->data[40] == corrupt_byte);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_UNAVAILABLE);
    CHECK(strcmp(query.reason, "content_mismatch") == 0);
    CHECK(query.record.package_len == HELLO_V2_PACKAGE_LEN);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_HISIG_PACKAGE,
                      HELLO_V2_HISIG_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(strcmp(rec.package_path, old_path) != 0);
    f = mem_find_file(rec.package_path);
    CHECK(f != NULL);
    CHECK(f->len == HELLO_V2_HISIG_PACKAGE_LEN);
    CHECK(memcmp(f->data, HELLO_V2_HISIG_PACKAGE, HELLO_V2_HISIG_PACKAGE_LEN) == 0);
    CHECK(mem_find_file(old_path) == NULL);
    CHECK(count_files() == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.package_len == HELLO_V2_HISIG_PACKAGE_LEN);
    CHECK(strcmp(query.record.package_path, rec.package_path) == 0);
}

static void test_committed_rollback_leaves_orphan_reused(void)
{
    app_install_record_t rec;
    app_install_query_t query;

    harness_reset();
    g_io.fail_next_kv_deletes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) != NULL);
    CHECK(count_files() == 1);

    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(count_files() == 1);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
}

static void test_boot_resolve_idempotent(void)
{
    app_install_query_t query;

    harness_reset();
    install_v1_ok();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
}

static void test_uninstall_then_reinstall_same_version(void)
{
    app_install_record_t rec;
    app_install_record_t removed;
    app_install_query_t query;

    harness_reset();
    install_v2_ok();
    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_OK);
    CHECK(count_files() == 0);
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V2_PACKAGE,
                      HELLO_V2_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 2u);
    CHECK(count_files() == 1);
}

static void test_user_data_never_touched_by_faults(void)
{
    app_install_record_t rec;
    app_install_record_t removed;
    const uint8_t user_data[] = {0xCA, 0xFE, 0xBA, 0xBE};
    uint8_t buf[16];
    size_t len = 0;
    app_install_query_t query;

    harness_reset();
    CHECK(mem_file_write(&g_io, "/captures/user.jpg", user_data,
                         sizeof(user_data)) == APP_INSTALL_IO_OK);

    g_io.fail_next_kv_deletes = 1;
    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    simulate_reboot();
    CHECK(app_install_boot_resolve(&g_hio) == APP_INSTALL_OK);
    CHECK(app_install_query(&g_hio, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_NONE);
    CHECK(count_files() == 2);

    CHECK(app_install(&g_hio, test_verify, &g_policy, HELLO_V1_PACKAGE,
                      HELLO_V1_PACKAGE_LEN, &rec) == APP_INSTALL_OK);
    CHECK(app_install_uninstall(&g_hio, &removed) == APP_INSTALL_OK);
    CHECK(count_files() == 1);

    CHECK(mem_file_read(&g_io, "/captures/user.jpg", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_OK);
    CHECK(len == sizeof(user_data));
    CHECK(memcmp(buf, user_data, sizeof(user_data)) == 0);
    CHECK(count_files() == 1);
}

int main(void)
{
    fill_policy();
    test_stage_write_failure_keeps_old();
    test_stage_readback_failure_keeps_old();
    test_storage_full_refused();
    test_capacity_unknown_fail_closed();
    test_stage_stat_io_error_fail_closed();
    test_marker_write_fail_proven_absent_rolls_back();
    test_marker_write_unprovable_state_unknown_then_recover();
    test_record_write_fail_rollback_proven();
    test_commit_unproven_boot_resolve_restores_old();
    test_first_install_crash_boot_resolve_clears();
    test_torn_marker_fail_closed();
    test_marker_read_failure_fail_closed_then_recover();
    test_record_read_failure_state_unknown();
    test_torn_record_fail_closed();
    test_uninstall_fault_windows();
    test_reboot_keeps_committed_identity();
    test_repair_corrupted_installed_same_content();
    test_repair_missing_file_alt_signature();
    test_repair_corrupt_alt_signature_write_fault_keeps_old();
    test_committed_rollback_leaves_orphan_reused();
    test_boot_resolve_idempotent();
    test_uninstall_then_reinstall_same_version();
    test_user_data_never_touched_by_faults();

    printf("test_app_installer_faults: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
