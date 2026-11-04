#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "app_host_run.h"

#include "app_admission.h"
#include "app_host.h"
#include "app_host_control.h"
#include "app_host_live.h"
#include "app_pair_installer.h"
#include "app_sidecar_lifecycle.h"
#include "mbedtls/sha256.h"

static const app_host_run_backend_t *g_run_backend;
static char g_run_log[APP_RUN_LOG_CAP];
static size_t g_run_log_len;
static aicam_bool_t g_run_log_truncated;

void app_host_run_init(const app_host_run_backend_t *backend)
{
    g_run_backend = backend;
    g_run_log_len = 0;
    g_run_log_truncated = AICAM_FALSE;
}

static void run_log_reset(void)
{
    g_run_log_len = 0;
    g_run_log_truncated = AICAM_FALSE;
}

static void run_log_append(const char *text)
{
    size_t len;
    size_t room;

    if (text == NULL) {
        return;
    }
    len = strlen(text);
    if (len >= APP_RUN_LOG_CAP - g_run_log_len) {
        room = APP_RUN_LOG_CAP - g_run_log_len - 1u;
        if (g_run_log_len < APP_RUN_LOG_CAP) {
            memcpy(g_run_log + g_run_log_len, text, room);
            g_run_log_len = APP_RUN_LOG_CAP - 1u;
        }
        g_run_log_truncated = AICAM_TRUE;
        return;
    }
    memcpy(g_run_log + g_run_log_len, text, len);
    g_run_log_len += len;
}

size_t app_host_run_take_log(char *buf, size_t cap)
{
    size_t copied;

    if (buf == NULL || cap == 0u) {
        return 0u;
    }
    copied = g_run_log_len;
    if (copied > cap - 1u) {
        copied = cap - 1u;
    }
    memcpy(buf, g_run_log, copied);
    buf[copied] = '\0';
    g_run_log_len = 0;
    g_run_log_truncated = AICAM_FALSE;
    return copied;
}

aicam_bool_t app_host_run_log_truncated(void)
{
    return g_run_log_truncated;
}

static int run_api_log(const char *text)
{
    run_log_append(text);
    if (g_run_backend != NULL && g_run_backend->log_sink != NULL) {
        g_run_backend->log_sink(g_run_backend->ctx, text);
    }
    return 0;
}

static int run_api_tick_ms(void)
{
    if (g_run_backend != NULL && g_run_backend->tick_ms != NULL) {
        return g_run_backend->tick_ms(g_run_backend->ctx);
    }
    return 0;
}

static void run_scrub(void)
{
    if (g_run_backend != NULL && g_run_backend->scrub != NULL) {
        g_run_backend->scrub(g_run_backend->ctx);
    }
}

static app_run_result_t run_refuse_owned(app_run_outcome_t *out,
                                         app_run_result_t result,
                                         const char *reason)
{
    run_scrub();
    app_install_set_app_running(AICAM_FALSE);
    out->result = result;
    out->entry_returned = AICAM_FALSE;
    out->entry_result = 0;
    snprintf(out->reason, sizeof(out->reason), "%s", (reason != NULL) ? reason : "");
    return result;
}

static aicam_bool_t run_fill_ctx(app_run_admission_ctx_t *actx)
{
    uint32_t base = 0;
    uint32_t size = 0;

    memset(actx, 0, sizeof(*actx));
    actx->policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    actx->policy.psram_mib = 64u;
    actx->policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    actx->policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK |
                                       APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                       APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    actx->policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    actx->policy.event_capacity = 16384u;
    actx->policy.report_capacity = 16384u;
    actx->policy.state_quota_capacity = 8192u;
    actx->policy.control_capacity = APP_CONTROL_MSG_RAM_MAX;
    actx->policy.snapshot_capacity = APP_CONTROL_SNAPSHOT_RAM_MAX;
    actx->policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    actx->policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    actx->policy.live_publish_capacity = APP_HOST_LIVE_PUBLISH_CAPACITY;
    actx->policy.live_track_capacity = APP_HOST_LIVE_TRACK_CAPACITY;
    if (app_host_exec_region(&base, &size) == AICAM_TRUE) {
        actx->policy.exec_region_base = base;
        actx->policy.exec_region_size = size;
    }
    return AICAM_TRUE;
}

static app_run_result_t run_pair_gate(const app_install_io_t *io,
                                      const app_install_record_t *native,
                                      app_sidecar_descriptor_t *out_descriptor,
                                      app_run_outcome_t *outcome)
{
    app_sidecar_trust_t trust;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_NONE;
    app_sidecar_lifecycle_result_t result;

    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);
    trust.app_id = native->app_id;
    trust.app_id_len = strlen(native->app_id);
    trust.native_tbs_sha256 = native->tbs_sha256;
    result = app_sidecar_lifecycle_query(io, &trust, native, out_descriptor, &state);
    if (result == APP_SIDECAR_LIFECYCLE_ERR_BUSY) {
        return run_refuse_owned(outcome, APP_RUN_ERR_BUSY, "BUSY");
    }
    if (result != APP_SIDECAR_LIFECYCLE_OK ||
        state != APP_SIDECAR_LIFECYCLE_ACTIVE) {
        if (result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN ||
            state == APP_SIDECAR_LIFECYCLE_UNKNOWN) {
            return run_refuse_owned(outcome, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                    "PAIR_STORAGE_UNKNOWN");
        }
        return run_refuse_owned(outcome, APP_RUN_ERR_UNAVAILABLE,
                                "PAIR_NOT_SERVABLE");
    }
    if (app_pair_is_authorized(io, native, out_descriptor) != AICAM_TRUE) {
        return run_refuse_owned(outcome, APP_RUN_ERR_UNAVAILABLE,
                                "PAIR_AUTHORIZATION_MISMATCH");
    }
    return APP_RUN_OK;
}

app_host_check_t app_host_run_stage_load(uint8_t *stage, size_t stage_cap,
                                         size_t native_file_len,
                                         const app_host_load_policy_t *policy,
                                         app_host_image_info_t *out)
{
    app_host_check_t check;
    const app_host_image_header_t *header;
    uint8_t *native;
    size_t header_size;

    if (stage == NULL || policy == NULL || out == NULL) {
        return APP_HOST_CHECK_ERR_PARAM;
    }
    native = stage + APP_ADMISSION_IMAGE_OFFSET_V5;
    check = app_host_validate(native, native_file_len, policy, out);
    if (check != APP_HOST_CHECK_OK) {
        return check;
    }
    header = (const app_host_image_header_t *)(const void *)native;
    header_size = (size_t)header->header_size;
    memmove(stage, native + header_size, (size_t)out->image_size);
    if ((size_t)out->image_size < stage_cap) {
        memset(stage + (size_t)out->image_size, 0,
               stage_cap - (size_t)out->image_size);
    }
    return APP_HOST_CHECK_OK;
}

app_run_result_t app_host_run_installed(const app_install_io_t *io,
                                        app_run_outcome_t *out)
{
    app_run_admission_ctx_t actx;
    app_install_record_t rec;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t qrc;
    uint8_t *stage;
    size_t stage_cap = 0;
    size_t staged_len = 0;
    app_install_io_result_t src;
    app_install_io_result_t rrc;
    size_t stat_len = 0;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    app_admission_verdict_t verdict;
    app_admission_result_t arc;
    app_sidecar_descriptor_t pair_descriptor;
    app_host_load_policy_t load_policy;
    app_host_image_info_t info;
    app_host_api_table_t api;
    app_host_check_t check;
    int raw = 0;

    if (out == NULL) {
        return APP_RUN_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    out->result = APP_RUN_ERR_PARAM;
    if (io == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        io->file_stat == NULL || io->file_read == NULL) {
        out->result = APP_RUN_ERR_PARAM;
        return APP_RUN_ERR_PARAM;
    }

    if (g_run_backend == NULL || g_run_backend->stage_base == NULL ||
        g_run_backend->stage_commit == NULL || g_run_backend->invoke_entry == NULL) {
        out->result = APP_RUN_ERR_RUNTIME_UNAVAILABLE;
        snprintf(out->reason, sizeof(out->reason), "%s", "RUNTIME_UNAVAILABLE");
        return APP_RUN_ERR_RUNTIME_UNAVAILABLE;
    }

    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        out->result = APP_RUN_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "BUSY");
        return APP_RUN_ERR_BUSY;
    }
    if (app_install_app_running() == AICAM_TRUE) {
        io->tx_end(io->ctx);
        out->result = APP_RUN_ERR_BUSY;
        snprintf(out->reason, sizeof(out->reason), "%s", "BUSY");
        return APP_RUN_ERR_BUSY;
    }
    app_install_set_app_running(AICAM_TRUE);
    io->tx_end(io->ctx);
    run_log_reset();

    qrc = app_install_query_committed(io, &rec, &present);
    if (qrc != APP_INSTALL_OK) {
        return run_refuse_owned(out, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                app_install_result_str(qrc));
    }
    if (present != AICAM_TRUE) {
        return run_refuse_owned(out, APP_RUN_ERR_NOT_INSTALLED, "NOT_INSTALLED");
    }
    if (strcmp(rec.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
        memcmp(rec.publisher_key_sha256,
               (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
               APP_INSTALL_SHA256_LEN) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE,
                                "LEGACY_SIGNED_STATE_PRESERVED");
    }
    out->record = rec;

    (void)run_fill_ctx(&actx);

    stage = g_run_backend->stage_base(g_run_backend->ctx, &stage_cap);
    if (stage == NULL || stage_cap == 0u ||
        (size_t)rec.package_len > stage_cap ||
        (uint64_t)rec.package_len > (uint64_t)APP_ADMISSION_MAX_PACKAGE_LEN_V5) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "RESOURCE_LIMIT");
    }

    src = io->file_stat(io->ctx, rec.package_path, &stat_len);
    if (src == APP_INSTALL_IO_ERROR) {
        return run_refuse_owned(out, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                "STORAGE_STATE_UNKNOWN");
    }
    if (src == APP_INSTALL_IO_NOT_FOUND) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "file_missing");
    }
    if (stat_len != (size_t)rec.package_len) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "content_mismatch");
    }

    rrc = io->file_read(io->ctx, rec.package_path, stage, (size_t)rec.package_len,
                        &staged_len);
    if (rrc == APP_INSTALL_IO_ERROR) {
        return run_refuse_owned(out, APP_RUN_ERR_STORAGE_STATE_UNKNOWN,
                                "STORAGE_STATE_UNKNOWN");
    }
    if (rrc != APP_INSTALL_IO_OK || staged_len != (size_t)rec.package_len) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "content_unreadable");
    }

    mbedtls_sha256(stage, staged_len, sha, 0);
    if (memcmp(sha, rec.package_sha256, APP_INSTALL_SHA256_LEN) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "content_mismatch");
    }

    arc = app_admission_verify_package(stage, staged_len, &actx.policy,
                                       &actx.trust, &verdict);
    if (arc != APP_ADMISSION_OK) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE,
                                app_admission_result_str(arc));
    }

    if (strcmp(rec.publisher_id, verdict.publisher_id) != 0 ||
        strcmp(rec.app_id, verdict.app_id) != 0 ||
        rec.version_major != verdict.version_major ||
        rec.version_minor != verdict.version_minor ||
        rec.version_patch != verdict.version_patch ||
        memcmp(rec.tbs_sha256, verdict.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(rec.publisher_key_sha256, verdict.publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE, "identity_mismatch");
    }

    if (verdict.container_major == 5u &&
        run_pair_gate(io, &rec, &pair_descriptor, out) != APP_RUN_OK) {
        return out->result;
    }

    load_policy.expected_target_addr = verdict.native_target_addr;
    load_policy.region_size = stage_cap;
    load_policy.abi_version = verdict.required_host_abi;
    check = app_host_run_stage_load(stage, stage_cap,
                                    (size_t)APP_ADMISSION_NATIVE_HEADER_SIZE +
                                        (size_t)verdict.native_image_size,
                                    &load_policy, &info);
    if (check != APP_HOST_CHECK_OK) {
        return run_refuse_owned(out, APP_RUN_ERR_UNAVAILABLE,
                                app_host_check_str(check));
    }

    if (g_run_backend->stage_commit(g_run_backend->ctx, stage_cap, &info) != 0) {
        return run_refuse_owned(out, APP_RUN_ERR_LOAD_FAILED, "stage_commit_failed");
    }

    api.table_size = (uint32_t)sizeof(api);
    api.abi_version = APP_HOST_ABI_VERSION;
    api.log = run_api_log;
    api.tick_ms = run_api_tick_ms;

    if (g_run_backend->invoke_entry(g_run_backend->ctx, &api, &info, &raw) != 0) {
        app_install_set_app_running(AICAM_FALSE);
        run_scrub();
        out->result = APP_RUN_ERR_RUNTIME_UNAVAILABLE;
        out->entry_returned = AICAM_FALSE;
        out->entry_result = 0;
        snprintf(out->reason, sizeof(out->reason), "%s", "RUNTIME_UNAVAILABLE");
        return APP_RUN_ERR_RUNTIME_UNAVAILABLE;
    }

    app_install_set_app_running(AICAM_FALSE);
    run_scrub();

    out->result = APP_RUN_OK;
    out->entry_returned = AICAM_TRUE;
    out->entry_result = raw;
    out->reason[0] = '\0';
    return APP_RUN_OK;
}

const char *app_host_run_result_str(app_run_result_t result)
{
    switch (result) {
    case APP_RUN_OK:
        return "OK";
    case APP_RUN_ERR_PARAM:
        return "PARAM";
    case APP_RUN_ERR_NOT_INSTALLED:
        return "NOT_INSTALLED";
    case APP_RUN_ERR_UNAVAILABLE:
        return "UNAVAILABLE";
    case APP_RUN_ERR_STORAGE_STATE_UNKNOWN:
        return "STORAGE_STATE_UNKNOWN";
    case APP_RUN_ERR_BUSY:
        return "BUSY";
    case APP_RUN_ERR_LOAD_FAILED:
        return "LOAD_FAILED";
    case APP_RUN_ERR_RUNTIME_UNAVAILABLE:
        return "RUNTIME_UNAVAILABLE";
    default:
        return "UNKNOWN";
    }
}
