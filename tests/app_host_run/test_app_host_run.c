#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_host_run.h"
#include "app_admission.h"
#include "app_host_control.h"
#include "app_host_live.h"
#include "app_pair_installer.h"
#include "generic_math.h"
#include "aicam_types.h"
#include "installer_vectors.h"
#include "mem_io_backend.h"

#define STAGE_BUF_CAP (64u * 1024u)

static int g_total;
static int g_fail;

static void CHECK(int cond)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL run:%d\n", g_total);
    }
}

static void CHECK_MSG(int cond, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL run:%d: %s\n", g_total, what);
    }
}

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    *base = 0x93E00000u;
    *size = 2u * 1024u * 1024u;
    return AICAM_TRUE;
}

static uint8_t g_stage_buf[STAGE_BUF_CAP];
static size_t g_stage_cap = STAGE_BUF_CAP;
static int g_stage_commit_fail;
static int g_invoke_refuse;
static int g_fake_raw;
static const char *g_fake_log_text;
static int g_stage_commit_count;
static int g_invoke_count;
static int g_scrub_count;
static int g_saw_running_inside_entry;
static uint32_t g_invoke_image_size;
static uint32_t g_invoke_entry_offset;
static uint8_t g_invoke_stage_head[32];
static int g_invoke_tail_zero;
static uint8_t g_invoke_entry_byte;
static int g_interleave_reserve;
static char g_sink_log[256];
static size_t g_sink_log_len;

static app_uninstall_purge_result_t suite_confirm_purge(
    void *ctx, const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_CONFIRMED;
}

static uint8_t *fake_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = g_stage_cap;
    return g_stage_buf;
}

static int fake_stage_commit(void *ctx, size_t stage_cap,
                             const app_host_image_info_t *info)
{
    (void)ctx;
    (void)stage_cap;
    (void)info;

    g_stage_commit_count++;
    if (g_stage_commit_fail != 0) {
        return -1;
    }
    return 0;
}

static int fake_invoke_entry(void *ctx, const app_host_api_table_t *api,
                             const app_host_image_info_t *info, int *raw)
{
    (void)ctx;

    g_invoke_count++;
    if (g_invoke_refuse != 0) {
        return -1;
    }
    g_invoke_image_size = info->image_size;
    g_invoke_entry_offset = info->entry_offset;
    memcpy(g_invoke_stage_head, g_stage_buf, sizeof(g_invoke_stage_head));
    g_invoke_tail_zero = 1;
    if (info->image_size <= g_stage_cap) {
        size_t i;

        for (i = info->image_size; i < g_stage_cap; i++) {
            if (g_stage_buf[i] != 0u) {
                g_invoke_tail_zero = 0;
                break;
            }
        }
    } else {
        g_invoke_tail_zero = 0;
    }
    g_invoke_entry_byte = (info->entry_offset < info->image_size)
                              ? g_stage_buf[info->entry_offset] : 0u;
    g_saw_running_inside_entry = (app_install_app_running() == AICAM_TRUE) ? 1 : 0;
    CHECK(api != NULL);
    CHECK(api->table_size == (uint32_t)sizeof(app_host_api_table_t));
    CHECK(api->abi_version == APP_HOST_ABI_VERSION);
    CHECK(api->log != NULL);
    CHECK(api->tick_ms != NULL);
    CHECK(api->tick_ms() == 123);
    if (g_fake_log_text != NULL) {
        CHECK(api->log(g_fake_log_text) == 0);
    }
    *raw = g_fake_raw;
    return 0;
}

static void fake_scrub(void *ctx)
{
    (void)ctx;
    g_scrub_count++;
    memset(g_stage_buf, 0, sizeof(g_stage_buf));
}

static void fake_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    if (text != NULL && g_sink_log_len + strlen(text) < sizeof(g_sink_log)) {
        memcpy(g_sink_log + g_sink_log_len, text, strlen(text));
        g_sink_log_len += strlen(text);
        g_sink_log[g_sink_log_len] = '\0';
    }
}

static int fake_tick_ms(void *ctx)
{
    (void)ctx;
    return 123;
}

static const app_host_run_backend_t g_fake_backend = {
    NULL,
    fake_stage_base,
    fake_stage_commit,
    fake_invoke_entry,
    fake_scrub,
    fake_log_sink,
    fake_tick_ms
};

static app_run_admission_ctx_t g_suite_ctx;
static app_sidecar_trust_t g_suite_trust;
static uint8_t *g_suite_package;
static size_t g_suite_package_len;

static app_admission_result_t suite_verify(const uint8_t *data, size_t len,
                                           app_admission_verdict_t *out, void *ctx)
{
    (void)ctx;
    return app_admission_verify_package(data, len, &g_suite_ctx.policy, &g_suite_ctx.trust, out);
}

static app_install_io_t g_suite_io;

static int suite_load_package(void)
{
    FILE *file = fopen("../app_unsigned/single-file-v5.neapp", "rb");
    long size;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 0;
    }
    size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    g_suite_package = (uint8_t *)malloc((size_t)size);
    if (g_suite_package == NULL ||
        fread(g_suite_package, 1u, (size_t)size, file) != (size_t)size) {
        free(g_suite_package);
        g_suite_package = NULL;
        fclose(file);
        return 0;
    }
    fclose(file);
    g_suite_package_len = (size_t)size;
    return 1;
}

static aicam_bool_t interleave_tx_begin(void *ctx)
{
    mem_io_t *io = (mem_io_t *)ctx;

    if (io->tx_held != 0) {
        return AICAM_FALSE;
    }
    io->tx_held = 1;
    if (g_interleave_reserve != 0) {
        app_install_set_app_running(AICAM_TRUE);
    }
    return AICAM_TRUE;
}

static void suite_reset(void)
{
    mem_reset();
    mem_io_init(&g_suite_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    g_stage_cap = STAGE_BUF_CAP;
    g_stage_commit_fail = 0;
    g_invoke_refuse = 0;
    g_fake_raw = 0;
    g_fake_log_text = NULL;
    g_stage_commit_count = 0;
    g_invoke_count = 0;
    g_scrub_count = 0;
    g_saw_running_inside_entry = 0;
    g_invoke_image_size = 0u;
    g_invoke_entry_offset = 0xFFFFFFFFu;
    memset(g_invoke_stage_head, 0, sizeof(g_invoke_stage_head));
    g_invoke_tail_zero = 0;
    g_invoke_entry_byte = 0u;
    g_interleave_reserve = 0;
    g_sink_log_len = 0;
    g_sink_log[0] = '\0';

    memset(&g_suite_ctx, 0, sizeof(g_suite_ctx));
    memset(&g_suite_trust, 0, sizeof(g_suite_trust));
    g_suite_trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    g_suite_trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    g_suite_ctx.trust.publisher_id = g_suite_trust.publisher_id;
    g_suite_ctx.trust.publisher_id_len = g_suite_trust.publisher_id_len;
    g_suite_ctx.policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    g_suite_ctx.policy.psram_mib = 64u;
    g_suite_ctx.policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    g_suite_ctx.policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK |
                                             APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                             APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    g_suite_ctx.policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    g_suite_ctx.policy.event_capacity = 16384u;
    g_suite_ctx.policy.report_capacity = 16384u;
    g_suite_ctx.policy.state_quota_capacity = 8192u;
    g_suite_ctx.policy.control_capacity = APP_CONTROL_MSG_RAM_MAX;
    g_suite_ctx.policy.snapshot_capacity = APP_CONTROL_SNAPSHOT_RAM_MAX;
    g_suite_ctx.policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    g_suite_ctx.policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    g_suite_ctx.policy.live_publish_capacity = APP_HOST_LIVE_PUBLISH_CAPACITY;
    g_suite_ctx.policy.live_track_capacity = APP_HOST_LIVE_TRACK_CAPACITY;
    g_suite_ctx.policy.exec_region_base = 0x93E00000u;
    g_suite_ctx.policy.exec_region_size = 2u * 1024u * 1024u;
    app_host_run_init(&g_fake_backend);
}

static app_install_result_t suite_install_io(const app_install_io_t *io,
                                              app_install_record_t *out_record)
{
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_pair_install_result_t result;

    result = app_pair_install(io, suite_verify, NULL, &g_suite_trust,
                              g_suite_package, g_suite_package_len, out_record,
                              &descriptor, &state);
    if (result == APP_PAIR_INSTALL_OK) {
        return APP_INSTALL_OK;
    }
    if (result == APP_PAIR_INSTALL_ALREADY) {
        return APP_INSTALL_OK_ALREADY;
    }
    if (result == APP_PAIR_INSTALL_ERR_BUSY) {
        return APP_INSTALL_ERR_BUSY;
    }
    return APP_INSTALL_ERR_BAD_PACKAGE;
}

static app_install_result_t suite_install(const uint8_t *package, size_t len,
                                          app_install_record_t *out_record)
{
    (void)package;
    (void)len;
    return suite_install_io(&g_suite_io, out_record);
}

static void test_param_negatives(void)
{
    app_run_outcome_t outcome;

    suite_reset();
    app_host_run_init(NULL);
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_RUNTIME_UNAVAILABLE);
    CHECK(strcmp(outcome.reason, "RUNTIME_UNAVAILABLE") == 0);
    CHECK(g_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);

    CHECK(app_host_run_installed(NULL, &outcome) == APP_RUN_ERR_PARAM);
    CHECK(app_host_run_installed(&g_suite_io, NULL) == APP_RUN_ERR_PARAM);
    CHECK(g_invoke_count == 0);
}

static void test_not_installed(void)
{
    app_run_outcome_t outcome;

    suite_reset();
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_NOT_INSTALLED);
    CHECK_MSG(strcmp(outcome.reason, "NOT_INSTALLED") == 0, "not_installed_reason");
    CHECK(g_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(g_scrub_count > 0);
}

static void test_happy_path_raw_results(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    char log_buf[APP_RUN_LOG_CAP + 1u];
    size_t log_len;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(count_files() >= 1);

    g_fake_raw = 7;
    g_fake_log_text = "hello from app";
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK_MSG(outcome.result == APP_RUN_OK, "v1_run_ok");
    CHECK_MSG(outcome.entry_returned == AICAM_TRUE, "v1_entry_returned");
    CHECK_MSG(outcome.entry_result == 7, "v1_raw_result_7");
    CHECK_MSG(strcmp(outcome.record.publisher_id,
                     APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0, "unsigned_publisher");
    CHECK_MSG(strcmp(outcome.record.app_id, "line-crossing") == 0, "unsigned_app_id");
    CHECK_MSG(outcome.record.version_major == 1u, "unsigned_version");
    CHECK(g_invoke_count == 1);
    CHECK(g_stage_commit_count == 1);
    CHECK_MSG(g_invoke_image_size == 64u, "unsigned_layout_image_size_64");
    CHECK_MSG(g_invoke_entry_offset == 0u, "unsigned_layout_entry_offset_zero");
    CHECK_MSG(g_invoke_stage_head[0] == 0x03u && g_invoke_stage_head[1] == 0x14u &&
              g_invoke_stage_head[2] == 0x25u && g_invoke_stage_head[3] == 0x36u &&
              g_invoke_stage_head[4] == 0x47u, "unsigned_exec_region_starts_payload");
    CHECK_MSG(g_invoke_stage_head[0] != 0x4Eu, "unsigned_exec_region_no_header_magic");
    CHECK_MSG(g_invoke_tail_zero != 0, "unsigned_exec_region_tail_zeroed");
    CHECK_MSG(g_invoke_entry_offset < g_invoke_image_size &&
              g_invoke_entry_byte == 0x03u, "unsigned_entry_byte_pointer_payload");
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(g_saw_running_inside_entry == 1);
    CHECK_MSG(strstr(g_sink_log, "hello from app") != NULL, "unsigned_log_sink");
    log_len = app_host_run_take_log(log_buf, sizeof(log_buf));
    CHECK_MSG(log_len == strlen("hello from app"), "unsigned_log_captured");
    CHECK_MSG(strcmp(log_buf, "hello from app") == 0, "unsigned_log_text");
    CHECK(app_host_run_take_log(log_buf, sizeof(log_buf)) == 0);

    g_fake_raw = -3;
    g_fake_log_text = NULL;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK_MSG(outcome.entry_result == -3, "v1_raw_result_negative");
    CHECK(outcome.entry_returned == AICAM_TRUE);

    g_fake_raw = 0;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK_MSG(outcome.entry_result == 0, "v1_raw_result_zero");
    CHECK(strcmp(outcome.reason, "") == 0);
}

static void test_busy_paths(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);

    app_install_set_app_running(AICAM_TRUE);
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_BUSY);
    CHECK_MSG(strcmp(outcome.reason, "BUSY") == 0, "busy_reason");
    CHECK_MSG(app_install_app_running() == AICAM_TRUE, "busy_foreign_flag_untouched");
    CHECK(g_invoke_count == 0);
    app_install_set_app_running(AICAM_FALSE);

    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK(g_invoke_count == 1);

    ((mem_io_t *)g_suite_io.ctx)->tx_held = 1;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_BUSY);
    CHECK_MSG(strcmp(outcome.reason, "BUSY") == 0, "busy_tx_held");
    CHECK(app_install_app_running() == AICAM_FALSE);
    ((mem_io_t *)g_suite_io.ctx)->tx_held = 0;
    CHECK(g_invoke_count == 1);
}

static void test_unavailable_paths(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    mem_file_t *file;
    mem_kv_t *kv;
    app_install_record_t mutated;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);

    file = mem_find_file(record.package_path);
    CHECK(file != NULL);
    file->data[40] ^= 0x40u;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "content_mismatch") == 0, "corrupt_content_mismatch");
    CHECK(g_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(mem_find_file(record.package_path) != NULL);
    mem_find_file(record.package_path)->present = AICAM_FALSE;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "file_missing") == 0, "missing_file_reason");
    CHECK(g_invoke_count == 0);

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    file = mem_find_file(record.package_path);
    CHECK(file != NULL);
    file->len = 100u;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "content_mismatch") == 0, "short_file_mismatch");

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    g_stage_cap = 100u;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "RESOURCE_LIMIT") == 0, "resource_limit_reason");
    CHECK(g_invoke_count == 0);

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    CHECK(app_install_record_decode(kv->data, APP_INSTALL_RECORD_BIN_SIZE, &mutated) == AICAM_TRUE);
    mutated.version_major = 9u;
    CHECK(app_install_record_encode(&mutated, kv->data, APP_INSTALL_RECORD_BIN_SIZE) == AICAM_TRUE);
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "identity_mismatch") == 0, "identity_mismatch_reason");
    CHECK(g_invoke_count == 0);
}

static void test_legacy_signed_state_refusal(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    app_install_record_t legacy;
    mem_kv_t *kv;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    if (kv != NULL) {
        CHECK(app_install_record_decode(kv->data, APP_INSTALL_RECORD_BIN_SIZE,
                                        &legacy) == AICAM_TRUE);
        memset(legacy.publisher_key_sha256, 0x91,
               sizeof(legacy.publisher_key_sha256));
        CHECK(app_install_record_encode(&legacy, kv->data,
                                        APP_INSTALL_RECORD_BIN_SIZE) == AICAM_TRUE);
    }

    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "LEGACY_SIGNED_STATE_PRESERVED") == 0,
              "legacy_signed_state_preserved");
    CHECK(g_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);
}

static void test_storage_unknown_paths(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    mem_kv_t *kv;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    kv->data[30] ^= 0x80u;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_STORAGE_STATE_UNKNOWN);
    CHECK_MSG(strcmp(outcome.reason, "STORAGE_STATE_UNKNOWN") == 0, "record_corrupt_unknown");
    CHECK(g_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    ((mem_io_t *)g_suite_io.ctx)->fail_next_file_stats = 1;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_STORAGE_STATE_UNKNOWN);
    CHECK_MSG(strcmp(outcome.reason, "STORAGE_STATE_UNKNOWN") == 0, "stat_fail_unknown");

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    ((mem_io_t *)g_suite_io.ctx)->fail_next_file_reads = 1;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_STORAGE_STATE_UNKNOWN);
    CHECK_MSG(strcmp(outcome.reason, "STORAGE_STATE_UNKNOWN") == 0, "read_fail_unknown");
    CHECK(g_invoke_count == 0);
}

static void test_uninstall_then_run(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    app_install_record_t removed;
    app_uninstall_cleanup_ops_t cleanup;
    app_uninstall_state_t uninstall_state;
    app_uninstall_result_t uninstall_result;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    CHECK(count_files() >= 1);
    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.purge = suite_confirm_purge;
    uninstall_result = app_install_uninstall_confirmed(&g_suite_io, AICAM_TRUE,
                                                        &cleanup, &removed,
                                                        &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_OK);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_NONE);
    CHECK(count_files() == 0);
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_NOT_INSTALLED);
    CHECK_MSG(strcmp(outcome.reason, "NOT_INSTALLED") == 0, "uninstalled_not_runnable");
    CHECK(g_invoke_count == 0);
}

static void test_backend_failure_paths(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    g_stage_commit_fail = 1;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_LOAD_FAILED);
    CHECK_MSG(strcmp(outcome.reason, "stage_commit_failed") == 0, "stage_commit_failed_reason");
    CHECK(g_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(g_scrub_count > 0);

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    g_invoke_refuse = 1;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_ERR_RUNTIME_UNAVAILABLE);
    CHECK_MSG(strcmp(outcome.reason, "RUNTIME_UNAVAILABLE") == 0, "invoke_refuse_reason");
    CHECK(outcome.entry_returned == AICAM_FALSE);
    CHECK(app_install_app_running() == AICAM_FALSE);
    CHECK(g_scrub_count > 0);
}

static void test_restart_semantics(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    g_fake_raw = 5;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK(g_invoke_count == 1);

    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    g_invoke_count = 0;
    CHECK_MSG(app_install_app_running() == AICAM_FALSE, "restart_running_not_restored");
    CHECK_MSG(g_invoke_count == 0, "restart_no_autostart");
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK_MSG(g_invoke_count == 1, "restart_explicit_run_ok");
    CHECK(outcome.entry_result == 5);
}

static void test_log_truncation(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    static char big[1200];
    char log_buf[APP_RUN_LOG_CAP + 1u];
    size_t i;
    size_t log_len;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    for (i = 0; i < sizeof(big) - 1u; i++) {
        big[i] = 'a';
    }
    big[sizeof(big) - 1u] = '\0';
    g_fake_log_text = big;
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK(g_invoke_count == 1);
    CHECK_MSG(app_host_run_log_truncated() == AICAM_TRUE, "log_truncated_flag");
    log_len = app_host_run_take_log(log_buf, sizeof(log_buf));
    CHECK_MSG(log_len == APP_RUN_LOG_CAP - 1u, "log_capped_length");
    CHECK(app_host_run_log_truncated() == AICAM_FALSE);
    CHECK(app_host_run_take_log(log_buf, sizeof(log_buf)) == 0u);
}

static void test_stage_load_layout_nonzero_entry_offset(void)
{
    static const uint8_t payload[16] = {
        0x60, 0x47, 0x00, 0x20, 0xA5, 0x5A, 0x70, 0x47,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    app_host_load_policy_t policy;
    app_host_image_info_t info;
    app_host_image_header_t header;
    uint8_t buf[4096];
    size_t i;
    int region_match;
    int tail_zero;

    memset(buf, 0xFF, sizeof(buf));
    header.magic = APP_HOST_IMAGE_MAGIC;
    header.header_size = (uint16_t)APP_HOST_IMAGE_HEADER_SIZE;
    header.format_version = APP_HOST_IMAGE_FORMAT_VERSION;
    header.abi_version = APP_HOST_ABI_VERSION;
    header.target_addr = 0x93E00000u;
    header.image_size = (uint32_t)sizeof(payload);
    header.entry_offset = 6u;
    header.reserved0 = 0u;
    header.crc32 = generic_crc32(payload, sizeof(payload));
    memcpy(buf + APP_ADMISSION_IMAGE_OFFSET_V5, &header, sizeof(header));
    memcpy(buf + APP_ADMISSION_IMAGE_OFFSET_V5 + APP_HOST_IMAGE_HEADER_SIZE,
           payload, sizeof(payload));

    policy.expected_target_addr = 0x93E00000u;
    policy.region_size = (uint32_t)sizeof(buf);
    policy.abi_version = APP_HOST_ABI_VERSION;

    CHECK(app_host_run_stage_load(buf, sizeof(buf),
                                  (size_t)APP_HOST_IMAGE_HEADER_SIZE + sizeof(payload),
                                  &policy, &info) == APP_HOST_CHECK_OK);
    CHECK_MSG(info.image_size == 16u, "stage_layout_image_size_16");
    CHECK_MSG(info.entry_offset == 6u, "stage_layout_entry_offset_6");
    CHECK_MSG((uintptr_t)info.entry_addr == 0x93E00000u + 6u, "stage_layout_entry_addr_math");
    region_match = 1;
    for (i = 0; i < sizeof(payload); i++) {
        if (buf[i] != payload[i]) {
            region_match = 0;
            break;
        }
    }
    CHECK_MSG(region_match != 0, "stage_layout_payload_only_at_base");
    CHECK_MSG(buf[0] == 0x60u && buf[1] == 0x47u && buf[2] == 0x00u,
              "stage_layout_first_bytes_payload");
    tail_zero = 1;
    for (i = sizeof(payload); i < sizeof(buf); i++) {
        if (buf[i] != 0u) {
            tail_zero = 0;
            break;
        }
    }
    CHECK_MSG(tail_zero != 0, "stage_layout_tail_zeroed_to_cap");
    CHECK_MSG((const uint8_t *)buf + info.entry_offset == &buf[6] &&
              buf[6] == payload[6] && buf[6] == 0x70u, "stage_layout_entry_byte_pointer");

    memset(buf, 0xFF, sizeof(buf));
    header.entry_offset = 7u;
    memcpy(buf + APP_ADMISSION_IMAGE_OFFSET_V5, &header, sizeof(header));
    memcpy(buf + APP_ADMISSION_IMAGE_OFFSET_V5 + APP_HOST_IMAGE_HEADER_SIZE,
           payload, sizeof(payload));
    CHECK(app_host_run_stage_load(buf, sizeof(buf),
                                  (size_t)APP_HOST_IMAGE_HEADER_SIZE + sizeof(payload),
                                  &policy, &info) == APP_HOST_CHECK_ERR_ENTRY);
    CHECK_MSG(buf[0] == 0xFFu && buf[1] == 0xFFu, "stage_layout_no_strip_on_reject");
}

static void test_install_interleave_run_reserve_busy(void)
{
    app_run_outcome_t outcome;
    app_install_record_t record;
    app_install_io_t hooked_io;

    suite_reset();
    hooked_io = g_suite_io;
    hooked_io.tx_begin = interleave_tx_begin;
    g_interleave_reserve = 1;
    CHECK(suite_install_io(&hooked_io, &record) == APP_INSTALL_ERR_BUSY);
    CHECK_MSG(((mem_io_t *)hooked_io.ctx)->tx_held == 0, "install_interleave_tx_released");
    CHECK_MSG(count_files() == 0, "install_interleave_zero_files");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL, "install_interleave_no_pending");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL, "install_interleave_no_record");
    CHECK_MSG(app_install_app_running() == AICAM_TRUE, "install_interleave_flag_preserved");
    g_interleave_reserve = 0;
    app_install_set_app_running(AICAM_FALSE);
    CHECK(suite_install_io(&hooked_io, &record) == APP_INSTALL_OK);
    CHECK(app_host_run_installed(&g_suite_io, &outcome) == APP_RUN_OK);
    CHECK(g_invoke_count == 1);
}

static void test_uninstall_interleave_run_reserve_busy(void)
{
    app_install_record_t record;
    app_install_record_t removed;
    app_install_io_t hooked_io;
    size_t installed_files;
    app_uninstall_cleanup_ops_t cleanup;
    app_uninstall_state_t uninstall_state;
    app_uninstall_result_t uninstall_result;

    suite_reset();
    CHECK(suite_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, &record) == APP_INSTALL_OK);
    installed_files = count_files();
    CHECK(installed_files >= 1u);
    hooked_io = g_suite_io;
    hooked_io.tx_begin = interleave_tx_begin;
    g_interleave_reserve = 1;
    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.purge = suite_confirm_purge;
    uninstall_result = app_install_uninstall_confirmed(&hooked_io, AICAM_TRUE,
                                                        &cleanup, &removed,
                                                        &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_ERR_BUSY);
    CHECK_MSG(((mem_io_t *)hooked_io.ctx)->tx_held == 0, "uninstall_interleave_tx_released");
    CHECK_MSG((size_t)count_files() == installed_files, "uninstall_interleave_file_kept");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL, "uninstall_interleave_record_kept");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL, "uninstall_interleave_no_pending");
    g_interleave_reserve = 0;
    app_install_set_app_running(AICAM_FALSE);
    uninstall_result = app_install_uninstall_confirmed(&hooked_io, AICAM_TRUE,
                                                        &cleanup, &removed,
                                                        &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_OK);
    CHECK_MSG(count_files() == 0, "uninstall_interleave_clears_after_reserve_gone");
}

int main(void)
{
    if (suite_load_package() == 0) {
        printf("FAIL unsigned_fixture_read\n");
        return 1;
    }
    test_param_negatives();
    test_not_installed();
    test_happy_path_raw_results();
    test_stage_load_layout_nonzero_entry_offset();
    test_install_interleave_run_reserve_busy();
    test_uninstall_interleave_run_reserve_busy();
    test_busy_paths();
    test_unavailable_paths();
    test_legacy_signed_state_refusal();
    test_storage_unknown_paths();
    test_uninstall_then_run();
    test_backend_failure_paths();
    test_restart_semantics();
    test_log_truncation();

    printf("app_host_run: %d checks, %d failures\n", g_total, g_fail);
    free(g_suite_package);
    return g_fail == 0 ? 0 : 1;
}
