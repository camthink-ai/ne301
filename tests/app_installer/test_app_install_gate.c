#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "mongoose.h"
#include "web_server.h"
#include "api_app_install_module.h"
#include "aicam_types.h"
#include "app_installer.h"
#include "app_report_service_device.h"
#include "app_host_run.h"
#include "app_host_session.h"
#include "app_admission.h"
#include "app_trust_anchor.h"
#include "app_pair_installer.h"
#include "app_sidecar_lifecycle.h"
#include "installer_vectors.h"
#include "mem_io_backend.h"
#include "app_state_store.h"
#include "mbedtls/sha256.h"

#ifndef NEPK_CI_CANDIDATE_PATH
#define NEPK_CI_CANDIDATE_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_SEQ3_PATH
#define NEPK_TEST_PAIR_SEQ3_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_SEQ4_PATH
#define NEPK_TEST_PAIR_SEQ4_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_SEQ4_CONFLICT_PATH
#define NEPK_TEST_PAIR_SEQ4_CONFLICT_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_SEQ5_PATH
#define NEPK_TEST_PAIR_SEQ5_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_MIXED_PUBLISHER_PATH
#define NEPK_TEST_PAIR_MIXED_PUBLISHER_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_MIXED_SPKI_PATH
#define NEPK_TEST_PAIR_MIXED_SPKI_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_INBAND_ROOT_PATH
#define NEPK_TEST_PAIR_INBAND_ROOT_PATH ""
#endif
#ifndef NEPK_TEST_PAIR_MALFORMED_PATH
#define NEPK_TEST_PAIR_MALFORMED_PATH ""
#endif

static int g_total;
static int g_fail;

static char g_last_response_kind[16];
static int g_last_response_code;
static char g_last_response_message[256];
static char g_last_response_data[4096];

static void CHECK(int cond);
static void CHECK_MSG(int cond, const char *what);

typedef struct {
    uint8_t *data;
    size_t len;
} gate_pair_fixture_t;

static gate_pair_fixture_t g_pair_seq3;
static gate_pair_fixture_t g_pair_seq4;
static gate_pair_fixture_t g_pair_seq4_conflict;
static gate_pair_fixture_t g_pair_seq5;
static gate_pair_fixture_t g_pair_mixed_publisher;
static gate_pair_fixture_t g_pair_mixed_spki;
static gate_pair_fixture_t g_pair_inband_root;
static gate_pair_fixture_t g_pair_malformed;
static gate_pair_fixture_t g_unsigned_package;

static int gate_pair_fixture_load(const char *path, gate_pair_fixture_t *out)
{
    FILE *fp;
    long file_len = 0;

    if (path == NULL || path[0] == '\0' || out == NULL) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (file_len = ftell(fp)) <= 0 ||
        (unsigned long)file_len > APP_PAIR_MAX_PACKAGE_LEN ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    out->data = (uint8_t *)malloc((size_t)file_len);
    if (out->data == NULL) {
        fclose(fp);
        return 0;
    }
    out->len = (size_t)file_len;
    if (fread(out->data, 1u, out->len, fp) != out->len) {
        free(out->data);
        memset(out, 0, sizeof(*out));
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int gate_pair_fixtures_load(void)
{
    const char *p3 = NEPK_TEST_PAIR_SEQ3_PATH;
    const char *p4 = NEPK_TEST_PAIR_SEQ4_PATH;
    const char *p4c = NEPK_TEST_PAIR_SEQ4_CONFLICT_PATH;
    const char *p5 = NEPK_TEST_PAIR_SEQ5_PATH;

    if ((p3 == NULL || p3[0] == '\0') && (p4 == NULL || p4[0] == '\0') &&
        (p4c == NULL || p4c[0] == '\0') && (p5 == NULL || p5[0] == '\0')) {
        printf("signed_pair_fixtures: NOT_RUN\n");
        return 0;
    }
    CHECK_MSG(gate_pair_fixture_load(p3, &g_pair_seq3),
              "signed_fixture_seq3_loaded");
    CHECK_MSG(gate_pair_fixture_load(p4, &g_pair_seq4),
              "signed_fixture_seq4_loaded");
    CHECK_MSG(gate_pair_fixture_load(p4c, &g_pair_seq4_conflict),
              "signed_fixture_conflict_loaded");
    CHECK_MSG(gate_pair_fixture_load(p5, &g_pair_seq5),
              "signed_fixture_seq5_loaded");
#ifdef APP_TRUST_SYNTHETIC_GATE
    CHECK_MSG(gate_pair_fixture_load(NEPK_TEST_PAIR_MIXED_PUBLISHER_PATH,
                                    &g_pair_mixed_publisher),
              "signed_fixture_mixed_publisher_loaded");
    CHECK_MSG(gate_pair_fixture_load(NEPK_TEST_PAIR_MIXED_SPKI_PATH,
                                    &g_pair_mixed_spki),
              "signed_fixture_mixed_spki_loaded");
    CHECK_MSG(gate_pair_fixture_load(NEPK_TEST_PAIR_INBAND_ROOT_PATH,
                                    &g_pair_inband_root),
              "signed_fixture_inband_root_loaded");
    CHECK_MSG(gate_pair_fixture_load(NEPK_TEST_PAIR_MALFORMED_PATH,
                                    &g_pair_malformed),
              "signed_fixture_malformed_loaded");
    return (g_pair_seq3.data != NULL && g_pair_seq4.data != NULL &&
            g_pair_seq4_conflict.data != NULL && g_pair_seq5.data != NULL &&
            g_pair_mixed_publisher.data != NULL && g_pair_mixed_spki.data != NULL &&
            g_pair_inband_root.data != NULL && g_pair_malformed.data != NULL) ? 1 : -1;
#else
    return (g_pair_seq3.data != NULL && g_pair_seq4.data != NULL &&
            g_pair_seq4_conflict.data != NULL && g_pair_seq5.data != NULL) ? 1 : -1;
#endif
}

static void gate_pair_fixtures_free(void)
{
    free(g_pair_seq3.data);
    free(g_pair_seq4.data);
    free(g_pair_seq4_conflict.data);
    free(g_pair_seq5.data);
    free(g_pair_mixed_publisher.data);
    free(g_pair_mixed_spki.data);
    free(g_pair_inband_root.data);
    free(g_pair_malformed.data);
    memset(&g_pair_seq3, 0, sizeof(g_pair_seq3));
    memset(&g_pair_seq4, 0, sizeof(g_pair_seq4));
    memset(&g_pair_seq4_conflict, 0, sizeof(g_pair_seq4_conflict));
    memset(&g_pair_seq5, 0, sizeof(g_pair_seq5));
    memset(&g_pair_mixed_publisher, 0, sizeof(g_pair_mixed_publisher));
    memset(&g_pair_mixed_spki, 0, sizeof(g_pair_mixed_spki));
    memset(&g_pair_inband_root, 0, sizeof(g_pair_inband_root));
    memset(&g_pair_malformed, 0, sizeof(g_pair_malformed));
}

aicam_result_t app_report_service_device_uninstall_prepare(
    const app_install_io_t *io)
{
    (void)io;
    return AICAM_OK;
}

void app_report_service_device_uninstall_finish(const app_install_io_t *io)
{
    (void)io;
}

static app_uninstall_purge_result_t gate_purge_confirmed(
    void *ctx,
    const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_CONFIRMED;
}

const app_uninstall_cleanup_ops_t *app_report_service_device_cleanup_ops(void)
{
    static const app_uninstall_cleanup_ops_t ops = {NULL, gate_purge_confirmed};
    return &ops;
}

static void CHECK(int cond)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d\n", g_total);
    }
}

static void CHECK_MSG(int cond, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d: %s\n", g_total, what);
        printf("  kind=[%s] code=%d msg=[%s]\n", g_last_response_kind, g_last_response_code,
               g_last_response_message);
        printf("  data=[%.600s]\n", g_last_response_data);
    }
}

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_FALSE;
    }
    return (ctx->authenticated == AICAM_TRUE && ctx->high_privilege == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data, const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "success");
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    g_last_response_data[0] = '\0';
    if (data) {
        snprintf(g_last_response_data, sizeof(g_last_response_data), "%s", data);
        free((void *)data);
    }
    return AICAM_OK;
}

aicam_result_t api_response_success_static(http_handler_context_t *ctx, const char *data, const char *message)
{
    (void)data;
    return api_response_success(ctx, NULL, message);
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code, const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "error");
    g_last_response_code = (int)error_code;
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    return AICAM_ERROR;
}

aicam_result_t api_response_error_data(http_handler_context_t *ctx, api_error_code_t error_code,
                                       const char *message, const char *data)
{
    (void)data;
    return api_response_error(ctx, error_code, message);
}

aicam_bool_t app_host_exec_region(uint32_t *base, uint32_t *size)
{
    *base = 0x93E00000u;
    *size = 2u * 1024u * 1024u;
    return AICAM_TRUE;
}

static int g_routes_registered;
static char g_route_methods[8][8];
static char g_route_paths[8][128];

aicam_result_t http_server_register_route(const api_route_t *route)
{
    if (route != NULL && g_routes_registered < 8) {
        snprintf(g_route_methods[g_routes_registered],
                 sizeof(g_route_methods[g_routes_registered]), "%s", route->method);
        snprintf(g_route_paths[g_routes_registered],
                 sizeof(g_route_paths[g_routes_registered]), "%s", route->path);
    }
    g_routes_registered++;
    return AICAM_OK;
}

static aicam_bool_t g_storage_initialized;

aicam_result_t app_installer_storage_init(void)
{
    g_storage_initialized = AICAM_TRUE;
    return AICAM_OK;
}

#ifndef APP_TRUST_SYNTHETIC_GATE
static const uint8_t g_gate_anchor_spki_der[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
    0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x7f, 0xcf, 0x75, 0xcd, 0x65, 0x69, 0xa5, 0x8f, 0x9c,
    0x2b, 0x26, 0xc6, 0xfa, 0x99, 0x16, 0x66, 0xaa, 0xc3, 0x9d, 0x44, 0x3f,
    0x7c, 0x8f, 0xeb, 0xb6, 0x14, 0xeb, 0x9d, 0x6d, 0x89, 0x23, 0xbc, 0xeb,
    0x89, 0xa3, 0xec, 0x42, 0x38, 0xa0, 0xdd, 0x9b, 0x38, 0x3c, 0x2b, 0xfd,
    0x94, 0x90, 0x45, 0x42, 0xa6, 0xa8, 0xb5, 0x70, 0x32, 0xcd, 0x4d, 0xfb,
    0x7b, 0x4c, 0xe5, 0xda, 0x86, 0xad, 0x3f
};

static const uint8_t g_gate_anchor_spki_sha256[] = {
    0x06, 0xe7, 0x32, 0xb1, 0xb9, 0x4d, 0xae, 0xee, 0xf4, 0x33, 0x29, 0x7f,
    0x7a, 0x8e, 0x79, 0x7b, 0xf7, 0x2b, 0x2f, 0xd3, 0xb8, 0x82, 0xa4, 0xec,
    0x6a, 0x54, 0x90, 0x35, 0x29, 0x31, 0xeb, 0xda
};

static const app_trust_anchor_t g_gate_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    "ne301-exp-pub",
    sizeof("ne301-exp-pub") - 1u,
    g_gate_anchor_spki_der,
    sizeof(g_gate_anchor_spki_der),
    g_gate_anchor_spki_sha256
};

static int g_gate_anchor_missing;

static void gate_anchor_set_missing(int missing)
{
    g_gate_anchor_missing = missing;
}

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    return &g_gate_anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    return (g_gate_anchor_missing != 0) ? APP_TRUST_ANCHOR_MISSING : APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    switch (status) {
    case APP_TRUST_ANCHOR_PROVISIONED:
        return "provisioned";
    case APP_TRUST_ANCHOR_MISSING:
        return "missing";
    case APP_TRUST_ANCHOR_MALFORMED:
        return "malformed";
    default:
        return "unknown";
    }
}
#else
static void gate_anchor_set_missing(int missing)
{
    (void)missing;
}
#endif

static app_install_io_t g_gate_io;

const app_install_io_t *app_installer_device_io(void)
{
    return &g_gate_io;
}

static uint8_t g_gate_stage_buf[APP_ADMISSION_MAX_PACKAGE_LEN_V4];
static size_t g_gate_stage_cap = sizeof(g_gate_stage_buf);
static int g_gate_stage_commit_fail;
static int g_gate_invoke_refuse;
static int g_gate_run_raw;
static const char *g_gate_run_log;
static int g_gate_stage_commit_count;
static int g_gate_invoke_count;
static int g_gate_scrub_count;
static int g_gate_session_mutexes[3];
static int g_gate_session_mutex_count;
static int g_gate_session_event;

static uint8_t *gate_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = g_gate_stage_cap;
    return g_gate_stage_buf;
}

static int gate_stage_commit(void *ctx, size_t stage_cap, const app_host_image_info_t *info)
{
    (void)ctx;
    (void)stage_cap;
    (void)info;
    g_gate_stage_commit_count++;
    return (g_gate_stage_commit_fail != 0) ? -1 : 0;
}

static int gate_invoke_entry(void *ctx, const app_host_api_table_t *api,
                             const app_host_image_info_t *info, int *raw)
{
    (void)ctx;
    (void)info;
    g_gate_invoke_count++;
    if (g_gate_invoke_refuse != 0) {
        return -1;
    }
    if (g_gate_run_log != NULL && api != NULL && api->log != NULL) {
        (void)api->log(g_gate_run_log);
    }
    *raw = g_gate_run_raw;
    return 0;
}

static void gate_scrub(void *ctx)
{
    (void)ctx;
    g_gate_scrub_count++;
    memset(g_gate_stage_buf, 0, sizeof(g_gate_stage_buf));
}

static void gate_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    (void)text;
}

static int gate_tick_ms(void *ctx)
{
    (void)ctx;
    return 5;
}

static const app_host_run_backend_t g_gate_run_backend = {
    NULL,
    gate_stage_base,
    gate_stage_commit,
    gate_invoke_entry,
    gate_scrub,
    gate_log_sink,
    gate_tick_ms
};

static uint8_t *gate_session_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = g_gate_stage_cap;
    return g_gate_stage_buf;
}

static int gate_session_stage_commit(void *ctx, size_t stage_cap,
                                     const app_host_image_info_t *info)
{
    (void)ctx;
    (void)stage_cap;
    (void)info;
    g_gate_stage_commit_count++;
    return (g_gate_stage_commit_fail != 0) ? -1 : 0;
}

static int gate_session_invoke_entry(void *ctx, const void *api,
                                     uint32_t abi_version,
                                     const app_host_image_info_t *info,
                                     int *raw)
{
    (void)ctx;
    (void)api;
    (void)abi_version;
    (void)info;
    g_gate_invoke_count++;
    if (raw != NULL) {
        *raw = g_gate_run_raw;
    }
    return (g_gate_invoke_refuse != 0) ? -1 : 0;
}

static void gate_session_scrub(void *ctx)
{
    (void)ctx;
    g_gate_scrub_count++;
    memset(g_gate_stage_buf, 0, sizeof(g_gate_stage_buf));
}

static int gate_session_entry_spawn(void *ctx, void (*fn)(void *arg), void *arg)
{
    (void)ctx;
    fn(arg);
    return 0;
}

static uint32_t gate_session_tick_ms(void *ctx)
{
    (void)ctx;
    return 5u;
}

static aicam_bool_t gate_session_entry_exited(void *ctx)
{
    (void)ctx;
    return AICAM_TRUE;
}

static void *gate_session_mutex_create(void *ctx)
{
    (void)ctx;
    return &g_gate_session_mutexes[g_gate_session_mutex_count++ % 3];
}

static void gate_session_mutex_destroy(void *ctx, void *mtx)
{
    (void)ctx;
    (void)mtx;
}

static int gate_session_mutex_lock(void *ctx, void *mtx)
{
    (void)ctx;
    (void)mtx;
    return 0;
}

static void gate_session_mutex_unlock(void *ctx, void *mtx)
{
    (void)ctx;
    (void)mtx;
}

static void *gate_session_event_create(void *ctx)
{
    (void)ctx;
    return &g_gate_session_event;
}

static void gate_session_event_destroy(void *ctx, void *evt)
{
    (void)ctx;
    (void)evt;
}

static uint32_t gate_session_event_wait(void *ctx, void *evt, uint32_t timeout_ms)
{
    (void)ctx;
    (void)evt;
    (void)timeout_ms;
    return 1u;
}

static void gate_session_event_set(void *ctx, void *evt)
{
    (void)ctx;
    (void)evt;
}

static aicam_bool_t gate_session_ai_attach(void *ctx)
{
    (void)ctx;
    return AICAM_TRUE;
}

static void gate_session_ai_detach(void *ctx)
{
    (void)ctx;
}

static int g_gate_model_od_fail;

static aicam_bool_t gate_session_model_view(void *ctx,
                                            app_session_model_view_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    snprintf(out->model_name, sizeof(out->model_name), "%s", "gate-model");
    snprintf(out->model_version, sizeof(out->model_version), "%s", "1");
    out->class_count = 1u;
    snprintf(out->class_names[0], sizeof(out->class_names[0]), "%s", "object");
    out->result_type_od = (g_gate_model_od_fail == 0) ? AICAM_TRUE : AICAM_FALSE;
    return AICAM_TRUE;
}

static app_session_result_t gate_session_pair_authorize(
    void *ctx,
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    char *reason,
    size_t reason_cap)
{
    (void)ctx;
    return app_host_session_pair_authorize(io, native_record, reason, reason_cap);
}

static const app_host_session_backend_t g_gate_session_backend = {
    NULL,
    gate_session_stage_base,
    gate_session_stage_commit,
    gate_session_invoke_entry,
    gate_session_scrub,
    gate_log_sink,
    gate_session_tick_ms,
    NULL,
    gate_session_entry_spawn,
    gate_session_entry_exited,
    gate_session_mutex_create,
    gate_session_mutex_destroy,
    gate_session_mutex_lock,
    gate_session_mutex_unlock,
    gate_session_event_create,
    gate_session_event_destroy,
    gate_session_event_wait,
    gate_session_event_set,
    gate_session_ai_attach,
    gate_session_ai_detach,
    gate_session_model_view,
    gate_session_pair_authorize
};

const app_host_run_backend_t *app_host_run_device_backend(void)
{
    return &g_gate_run_backend;
}

static http_handler_context_t make_ctx(const char *method, const char *body)
{
    static char body_buf[12000];
    http_handler_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    memset(body_buf, 0, sizeof(body_buf));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    if (body) {
        snprintf(body_buf, sizeof(body_buf), "%s", body);
        ctx.request.body = body_buf;
        ctx.request.content_length = strlen(body_buf);
    }
    return ctx;
}

static void reset_response(void)
{
    g_last_response_kind[0] = '\0';
    g_last_response_code = -1;
    g_last_response_message[0] = '\0';
    g_last_response_data[0] = '\0';
}

static void reset_storage(void)
{
    mem_reset();
    mem_io_init(&g_gate_io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    gate_anchor_set_missing(0);
    g_gate_stage_cap = sizeof(g_gate_stage_buf);
    g_gate_stage_commit_fail = 0;
    g_gate_invoke_refuse = 0;
    g_gate_run_raw = 0;
    g_gate_run_log = NULL;
    g_gate_stage_commit_count = 0;
    g_gate_invoke_count = 0;
    g_gate_scrub_count = 0;
    g_gate_session_mutex_count = 0;
    app_host_session_init(&g_gate_session_backend);
}

static aicam_result_t post_install(const uint8_t *package_bytes, size_t package_len,
                                   int authenticated, int high_privilege)
{
    http_handler_context_t ctx = make_ctx("POST", NULL);

    ctx.request.body = (char *)package_bytes;
    ctx.request.content_length = package_len;
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/octet-stream");
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    return app_install_package_handler(&ctx);
}

static app_admission_result_t gate_native_verify(const uint8_t *data, size_t len,
                                                 app_admission_verdict_t *out,
                                                 void *ctx)
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

static app_install_result_t seed_native_for_run_test(const uint8_t *package,
                                                     size_t package_len)
{
    app_admission_policy_t policy;
    app_install_record_t record;

    memset(&policy, 0, sizeof(policy));
    policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy.psram_mib = 64u;
    policy.abi_version = 0x00010000u;
    policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    policy.exec_region_base = 0x93E00000u;
    policy.exec_region_size = 2u * 1024u * 1024u;
    return app_install(&g_gate_io, gate_native_verify, &policy,
                       package, package_len, &record);
}

static aicam_result_t get_state(int authenticated, int high_privilege)
{
    http_handler_context_t ctx = make_ctx("GET", NULL);
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    return app_install_state_handler(&ctx);
}

static aicam_result_t delete_installed_confirm(int authenticated, int high_privilege,
                                              int confirm)
{
    char body[64];
    http_handler_context_t ctx;

    snprintf(body, sizeof(body), "{\"confirm_delete_data\":%s}",
             (confirm != 0) ? "true" : "false");
    ctx = make_ctx("DELETE", body);
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    return app_install_remove_handler(&ctx);
}

static aicam_result_t delete_installed(int authenticated, int high_privilege)
{
    return delete_installed_confirm(authenticated, high_privilege, 1);
}

static aicam_result_t post_run(int authenticated, int high_privilege)
{
    http_handler_context_t ctx = make_ctx("POST", NULL);
    ctx.authenticated = (authenticated != 0) ? AICAM_TRUE : AICAM_FALSE;
    ctx.high_privilege = (high_privilege != 0) ? AICAM_TRUE : AICAM_FALSE;
    reset_response();
    return app_install_run_handler(&ctx);
}

static void gate_reap_session(void)
{
    app_host_session_status_t status;

    memset(&status, 0, sizeof(status));
    app_host_session_status(&status);
    CHECK(status.app_running == AICAM_FALSE);
}

static void test_route_table_registered(void)
{
    int i;
    reset_storage();
    g_routes_registered = 0;
    memset(g_route_methods, 0, sizeof(g_route_methods));
    memset(g_route_paths, 0, sizeof(g_route_paths));
    CHECK(web_api_register_app_install_module() == AICAM_OK);
    CHECK(g_routes_registered == 4);
    CHECK(g_storage_initialized == AICAM_TRUE);
    for (i = 0; i < g_routes_registered; i++) {
        CHECK(strstr(g_route_paths[i], "/app/sidecar/install") == NULL);
        CHECK(strstr(g_route_paths[i], "/app/sidecar/installed") == NULL);
    }
}

static void test_nepk_ci_candidate_api(void)
{
    const char *path = NEPK_CI_CANDIDATE_PATH;
    FILE *fp;
    long file_len;
    uint8_t *package;
    http_handler_context_t ctx;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    fp = fopen(path, "rb");
    CHECK_MSG(fp != NULL, "nepk_candidate_file_open");
    if (fp == NULL) {
        return;
    }
    CHECK(fseek(fp, 0, SEEK_END) == 0);
    file_len = ftell(fp);
    CHECK(file_len > 0);
    CHECK(fseek(fp, 0, SEEK_SET) == 0);
    if (file_len <= 0) {
        fclose(fp);
        return;
    }
    package = (uint8_t *)malloc((size_t)file_len);
    CHECK(package != NULL);
    if (package == NULL) {
        fclose(fp);
        return;
    }
    CHECK(fread(package, 1u, (size_t)file_len, fp) == (size_t)file_len);
    fclose(fp);

    reset_storage();
    ctx = make_ctx("POST", NULL);
    ctx.request.body = (char *)package;
    ctx.request.content_length = (size_t)file_len;
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/octet-stream");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "nepk_api_install_status");
    CHECK_MSG(strstr(g_last_response_data, "\"release_seq\":\"3\"") != NULL,
              "nepk_api_release_seq");
    CHECK_MSG(strstr(g_last_response_data, "\"ui\":{\"status\":\"active\"") != NULL,
              "nepk_api_ui_identity");

    CHECK(get_state(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"installed\"") != NULL,
              "nepk_api_state_installed");
    CHECK_MSG(strstr(g_last_response_data, "\"ui\":{\"status\":\"active\"") != NULL,
              "nepk_api_state_ui_identity");

    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data,
                     "\"status\":\"already_installed\"") != NULL,
              "nepk_api_idempotent");
    CHECK(get_state(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"installed\"") != NULL,
              "nepk_api_state_still_installed");
    free(package);
    reset_storage();
}

static void test_install_auth_gate(void)
{
    reset_storage();

    CHECK(post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 0, 0) == AICAM_ERROR);
    CHECK(g_last_response_kind[0] == 'e');
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(g_last_response_data[0] == '\0');
    CHECK(count_files() == 0);

    CHECK(post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 0) == AICAM_OK);
    CHECK(get_state(1, 0) == AICAM_OK);
    CHECK(delete_installed(1, 0) == AICAM_OK);
    CHECK(count_files() == 0);
}

static void test_install_method_gate(void)
{
    http_handler_context_t ctx;

    reset_storage();

    ctx = make_ctx("GET", "NEPK");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_state_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_remove_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);
}

static void test_install_payload_negatives(void)
{
    http_handler_context_t ctx;
    static const uint8_t legacy_json[] = "{\"package_base64\":\"not-base64!!\"}";

    reset_storage();

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    CHECK(post_install((const uint8_t *)"not-json", strlen("not-json"), 1, 1) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"reason\":\"BAD_PACKAGE\"") != NULL);
    CHECK(post_install(legacy_json, sizeof(legacy_json) - 1u, 1, 1) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"reason\":\"BAD_PACKAGE\"") != NULL);

    {
        size_t huge_len = (size_t)APP_PAIR_MAX_PACKAGE_LEN + 1u;
        uint8_t *huge = calloc(huge_len, 1u);
        CHECK(huge != NULL);
        if (huge != NULL) {
            CHECK(post_install(huge, huge_len, 1, 1) == AICAM_ERROR);
            CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
            free(huge);
        }
    }
    CHECK(count_files() == 0);
}

static void test_install_lifecycle_end_to_end(void)
{
    static const uint8_t legacy_json[] = "{\"package_base64\":\"AA==\"}";

    reset_storage();

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"none\"") != NULL, "query_none");

    CHECK(post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "native_only_install_refused");
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"BAD_PACKAGE\"") != NULL,
              "native_only_is_not_nepk");
    CHECK(post_install(legacy_json, sizeof(legacy_json) - 1u, 1, 1) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"reason\":\"BAD_PACKAGE\"") != NULL);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_PENDING) == NULL);

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"none\"") != NULL,
              "native_only_query_not_installed");
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
}

static void test_state_query_trust_unproven_paths(void)
{
    mem_kv_t *kv;
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];

    reset_storage();

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"none\"") != NULL, "unproven_none");

    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);

    gate_anchor_set_missing(1);
    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"unavailable\"") != NULL,
              "unproven_unavailable");
    CHECK_MSG(strstr(g_last_response_data, "\"publisher_id\":\"ne301-exp-pub\"") != NULL,
              "unproven_identity_publisher");
    CHECK_MSG(strstr(g_last_response_data, "\"app_id\":\"hello-app\"") != NULL,
              "unproven_identity_app");
    CHECK_MSG(strstr(g_last_response_data, "\"version_major\":1") != NULL,
              "unproven_identity_version");
    CHECK_MSG(strstr(g_last_response_data,
                     "\"package_sha256\":\"f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8\"") !=
                  NULL,
              "unproven_identity_hash");
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"TRUST_UNPROVEN\"") != NULL,
              "unproven_reason");
    CHECK_MSG(strstr(g_last_response_data, "\"executable\":false") != NULL,
              "unproven_not_executable");

    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "unproven_install_refused");
    CHECK_MSG(strstr(g_last_response_data, "TRUST_UNPROVEN") != NULL,
              "unproven_install_reason");
    CHECK(count_files() == 1);

    kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK(kv != NULL);
    gate_anchor_set_missing(0);
    if (kv != NULL) {
        memcpy(blob, kv->data, sizeof(blob));
        blob[30] ^= 0x80u;
        memcpy(kv->data, blob, sizeof(blob));
    }

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"unknown\"") != NULL,
              "corrupt_unknown");
    CHECK_MSG(strstr(g_last_response_data, "STORAGE_STATE_UNKNOWN") != NULL,
              "corrupt_unknown_reason");

    reset_storage();
}

static void test_install_busy_running(void)
{
    reset_storage();
    app_install_set_app_running(AICAM_TRUE);
    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "BUSY") != NULL, "install_busy_running");
    delete_installed(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "BUSY") != NULL, "uninstall_busy_running");
    app_install_set_app_running(AICAM_FALSE);
}

static void test_install_protocol_length_gate(void)
{
    http_handler_context_t ctx;
    uint8_t *pkg;
    size_t total = (size_t)APP_PAIR_MAX_PACKAGE_LEN;

    reset_storage();
    pkg = malloc(total + 1u);
    CHECK(pkg != NULL);
    if (pkg == NULL) {
        free(pkg);
        return;
    }

    memset(pkg, 0, total + 1u);
    ctx = make_ctx("POST", NULL);
    ctx.request.body = (char *)pkg;
    ctx.request.content_length = total;
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/octet-stream");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "install_pair_max_len_passes_entrance");
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"BAD_PACKAGE\"") != NULL,
              "install_pair_max_len_refused_bad_package");
    CHECK(count_files() == 0);

    ctx = make_ctx("POST", NULL);
    ctx.request.body = (char *)pkg;
    ctx.request.content_length = total + 1u;
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/octet-stream");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_package_handler(&ctx) == AICAM_ERROR);
    CHECK_MSG(g_last_response_code == API_ERROR_INVALID_REQUEST, "install_over_limit_fail_closed");
    CHECK(strstr(g_last_response_message, "length out of bounds") != NULL);
    CHECK(count_files() == 0);

    free(pkg);
}

static void test_run_auth_and_method_gate(void)
{
    http_handler_context_t ctx;

    reset_storage();

    CHECK(post_run(0, 0) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(g_last_response_data[0] == '\0');
    CHECK(g_gate_invoke_count == 0);

    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);
    g_gate_run_raw = 7;
    g_gate_run_log = "gate app log line";
    CHECK(post_run(1, 0) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"status\":\"completed\"") != NULL);
    CHECK(g_gate_invoke_count == 1);

    g_gate_invoke_count = 0;
    ctx = make_ctx("GET", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_install_run_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);
    CHECK(g_gate_invoke_count == 0);
}

static void test_run_lifecycle_end_to_end(void)
{
    reset_storage();

    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL, "run_none_refused");
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"NOT_INSTALLED\"") != NULL,
              "run_none_reason");
    CHECK_MSG(strstr(g_last_response_data, "\"executable\":false") != NULL, "run_none_not_exec");
    CHECK(g_gate_invoke_count == 0);

    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);
    g_gate_run_raw = 7;
    g_gate_run_log = "gate app log line";
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"completed\"") != NULL,
              "legacy_v1_run_completed");
    CHECK_MSG(strstr(g_last_response_data, "\"entry_returned\":true") != NULL,
              "legacy_v1_entry_returned");
    CHECK_MSG(strstr(g_last_response_data, "\"entry_result\":7") != NULL,
              "legacy_v1_run_result");
    CHECK(g_gate_invoke_count == 1);
    CHECK(g_gate_stage_commit_count == 1);
    CHECK(app_install_app_running() == AICAM_FALSE);
}

static void gate_expect_pair_floor(uint64_t expected_seq,
                                   app_pair_history_t *out_history)
{
    app_pair_history_t history;
    aicam_bool_t present = AICAM_FALSE;

    memset(&history, 0, sizeof(history));
    CHECK_MSG(app_sidecar_lifecycle_pair_history_query(&g_gate_io, &history, &present) ==
                  APP_SIDECAR_LIFECYCLE_OK,
              "pair_floor_query_ok");
    CHECK_MSG(present == AICAM_TRUE, "pair_floor_present");
    CHECK_MSG(history.release_seq == expected_seq, "pair_floor_sequence");
    if (out_history != NULL) {
        *out_history = history;
    }
}

static void gate_install_pair_fixture(const gate_pair_fixture_t *fixture,
                                      const char *status,
                                      const char *label)
{
    CHECK(fixture != NULL && fixture->data != NULL && fixture->len > 0u);
    if (fixture == NULL || fixture->data == NULL || fixture->len == 0u) {
        return;
    }
    CHECK(post_install(fixture->data, fixture->len, 1, 1) == AICAM_OK);
    if (status != NULL) {
        CHECK_MSG(strstr(g_last_response_data, status) != NULL, label);
    }
}

static int gate_pair_descriptor(app_install_record_t *native,
                                app_sidecar_descriptor_t *descriptor,
                                app_sidecar_lifecycle_state_t *state)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_sidecar_trust_t trust;
    aicam_bool_t present = AICAM_FALSE;

    memset(native, 0, sizeof(*native));
    memset(descriptor, 0, sizeof(*descriptor));
    memset(&trust, 0, sizeof(trust));
    if (app_install_query_committed(&g_gate_io, native, &present) != APP_INSTALL_OK ||
        present != AICAM_TRUE) {
        return 0;
    }
    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    trust.app_id = native->app_id;
    trust.app_id_len = strlen(native->app_id);
    trust.native_tbs_sha256 = native->tbs_sha256;
    return app_sidecar_lifecycle_query(&g_gate_io, &trust, native,
                                      descriptor, state) == APP_SIDECAR_LIFECYCLE_OK &&
           *state == APP_SIDECAR_LIFECYCLE_ACTIVE;
}

static int gate_sidecar_boot_resolve(app_install_record_t *native,
                                     app_sidecar_descriptor_t *descriptor,
                                     app_sidecar_lifecycle_state_t *state)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_sidecar_trust_t trust;
    aicam_bool_t present = AICAM_FALSE;

    memset(native, 0, sizeof(*native));
    memset(descriptor, 0, sizeof(*descriptor));
    memset(&trust, 0, sizeof(trust));
    if (app_install_query_committed(&g_gate_io, native, &present) != APP_INSTALL_OK ||
        present != AICAM_TRUE) {
        return 0;
    }
    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    trust.app_id = native->app_id;
    trust.app_id_len = strlen(native->app_id);
    trust.native_tbs_sha256 = native->tbs_sha256;
    return app_sidecar_lifecycle_boot_resolve(&g_gate_io, &trust, native,
                                              descriptor, state) ==
           APP_SIDECAR_LIFECYCLE_OK;
}

#ifdef APP_TRUST_SYNTHETIC_GATE
static app_pair_install_result_t gate_verify_synthetic_pair(
    const gate_pair_fixture_t *fixture)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_policy_t policy;
    app_sidecar_trust_t trust;
    app_pair_candidate_t candidate;

    memset(&policy, 0, sizeof(policy));
    policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy.psram_mib = 64u;
    policy.abi_version = 0x00010000u;
    policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    policy.exec_region_base = 0x93E00000u;
    policy.exec_region_size = 2u * 1024u * 1024u;
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    return app_pair_verify_package(fixture->data, fixture->len,
                                   gate_native_verify, &policy, &trust,
                                   &candidate);
}

static void test_synthetic_production_trust_negatives(void)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();

    CHECK(app_trust_anchor_status() == APP_TRUST_ANCHOR_PROVISIONED);
    CHECK_MSG(strcmp(app_trust_anchor_profile_str(), "production") == 0,
              "synthetic_production_profile_selected");
    CHECK_MSG(strcmp(anchor->publisher_id, "synthetic-pub") == 0,
              "synthetic_publisher_selected");
    printf("profile=%s publisher=%s spki_sha256=%s provenance=%s status=%s\n",
           app_trust_anchor_profile_str(), anchor->publisher_id,
           app_trust_anchor_spki_sha256_hex(),
           app_trust_anchor_provenance_str(),
           app_trust_anchor_status_str(app_trust_anchor_status()));

    reset_storage();
    post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "test_exp_issuer_package_refused_by_production_profile");
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);

    CHECK(gate_verify_synthetic_pair(&g_pair_mixed_publisher) !=
              APP_PAIR_INSTALL_OK);
    CHECK(gate_verify_synthetic_pair(&g_pair_mixed_spki) !=
              APP_PAIR_INSTALL_OK);
    CHECK(gate_verify_synthetic_pair(&g_pair_inband_root) !=
              APP_PAIR_INSTALL_OK);
    CHECK(gate_verify_synthetic_pair(&g_pair_malformed) ==
          APP_PAIR_INSTALL_ERR_BAD_FORMAT);
}
#endif

static void test_signed_pair_run_and_monotonic_floor(void)
{
    app_pair_history_t history3;
    app_pair_history_t history4;
    app_pair_history_t history5;
    app_install_record_t native;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    mem_file_t *asset;
    mem_kv_t *native_record;
    mem_kv_t *active_a;
    mem_kv_t *active_b;
    mem_kv_t saved_active_a;
    mem_kv_t saved_active_b;
    int asset_byte;
    int prior_invoke_count;

    reset_storage();
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"refused\"",
                              "pair_native_committed_ui_write_failed");
    native_record = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK_MSG(native_record != NULL, "pair_native_commit_survives_ui_failure");
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "run_after_ui_write_failure_refused");
    CHECK(g_gate_invoke_count == 0);
    CHECK(g_gate_stage_commit_count == 0);
    gate_expect_pair_floor(3u, NULL);

    reset_storage();
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"installed\"",
                              "pair_unknown_setup_install");
    app_install_boot_resolve_reset();
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR);
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data,
                     "\"reason\":\"STORAGE_STATE_UNKNOWN\"") != NULL,
              "run_pair_floor_unknown_refused");
    CHECK(g_gate_invoke_count == 0);

    reset_storage();
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"installed\"",
                              "pair_commit_cut_setup_install");
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B);
    gate_install_pair_fixture(&g_pair_seq4, "\"status\":\"refused\"",
                              "signed_upgrade_active_record_cut");
    app_install_boot_resolve_reset();
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "run_refused_after_floor_committed_slot_failed");
    CHECK(g_gate_invoke_count == 0);
    gate_expect_pair_floor(4u, NULL);
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"refused\"",
                              "seq3_rejected_after_interrupted_seq4");
    gate_expect_pair_floor(4u, NULL);

    reset_storage();
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"installed\"",
                              "pair_seq3_install");
    CHECK_MSG(strstr(g_last_response_data, "\"release_seq\":\"3\"") != NULL,
              "pair_seq3_api_install_identity");
    gate_expect_pair_floor(3u, &history3);
    CHECK(get_state(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data,
                     "\"ui\":{\"status\":\"active\"") != NULL,
              "pair_seq3_api_query_active");
    prior_invoke_count = g_gate_invoke_count;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "complete_pair_explicit_run_started");
    CHECK(g_gate_invoke_count == prior_invoke_count + 1);
    CHECK(g_gate_stage_commit_count == 1);
    gate_reap_session();

    g_io.fail_kv_write_key[0] = '\0';
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR);
    gate_install_pair_fixture(&g_pair_seq4, "\"status\":\"refused\"",
                              "signed_upgrade_floor_write_interrupted");
    app_install_boot_resolve_reset();
    CHECK(gate_sidecar_boot_resolve(&native, &descriptor, &state));
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(descriptor.release_seq == 3u);
    CHECK(get_state(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data,
                     "\"ui\":{\"status\":\"active\"") != NULL,
              "signed_upgrade_interruption_keeps_old_ui");
    gate_expect_pair_floor(3u, NULL);
    prior_invoke_count = g_gate_invoke_count;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "old_pair_explicit_run_started_after_interrupted_upgrade");
    CHECK(g_gate_invoke_count == prior_invoke_count + 1);
    gate_reap_session();

    gate_install_pair_fixture(&g_pair_seq4, "\"status\":\"installed\"",
                              "signed_ui_only_upgrade");
    CHECK_MSG(strstr(g_last_response_data, "\"release_seq\":\"4\"") != NULL,
              "signed_upgrade_api_release_seq");
    gate_expect_pair_floor(4u, &history4);
    CHECK(memcmp(history3.native_tbs_sha256, history4.native_tbs_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
    CHECK(get_state(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data,
                     "\"ui\":{\"status\":\"active\"") != NULL,
              "signed_upgrade_query_active");
    prior_invoke_count = g_gate_invoke_count;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "signed_upgrade_pair_explicit_run_started");
    CHECK(g_gate_invoke_count == prior_invoke_count + 1);
    gate_reap_session();

    app_install_boot_resolve_reset();
    prior_invoke_count = g_gate_invoke_count;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "run_signed_pair_after_restart_started");
    CHECK(g_gate_invoke_count == prior_invoke_count + 1);
    gate_reap_session();

    prior_invoke_count = g_gate_invoke_count;
    gate_install_pair_fixture(&g_pair_seq4_conflict, "\"status\":\"refused\"",
                              "same_seq_different_pair_rejected");
    gate_expect_pair_floor(4u, NULL);
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"refused\"",
                              "signed_seq3_downgrade_rejected");
    gate_expect_pair_floor(4u, NULL);
    prior_invoke_count = g_gate_invoke_count;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "pair_unchanged_after_negative_upgrades_still_runs");
    CHECK(g_gate_invoke_count == prior_invoke_count + 1);
    gate_reap_session();

    CHECK(gate_pair_descriptor(&native, &descriptor, &state));
    CHECK(descriptor.release_seq == 4u);
    CHECK(descriptor.file_count > 0u);
    asset = mem_find_file(descriptor.files[0].storage_path);
    CHECK(asset != NULL && asset->len > 0u);
    if (asset != NULL && asset->len > 0u) {
        asset_byte = asset->data[0];
        asset->data[0] ^= 0x40u;
        app_install_boot_resolve_reset();
        CHECK(!gate_pair_descriptor(&native, &descriptor, &state));
        gate_expect_pair_floor(4u, &history4);
        prior_invoke_count = g_gate_invoke_count;
        post_run(1, 1);
        CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
                  "run_corrupt_pair_ui_refused");
        CHECK(g_gate_invoke_count == prior_invoke_count);
        asset->data[0] = (uint8_t)asset_byte;
        app_install_boot_resolve_reset();
        CHECK(get_state(1, 1) == AICAM_OK);
        CHECK_MSG(strstr(g_last_response_data,
                         "\"ui\":{\"status\":\"active\"") != NULL,
                  "seq4_pair_recovers_after_asset_restore");
    }
    gate_install_pair_fixture(&g_pair_seq3, "\"status\":\"refused\"",
                              "seq3_reinstall_rejected_after_recovery");
    gate_install_pair_fixture(&g_pair_seq4_conflict, "\"status\":\"refused\"",
                              "same_seq_conflict_rejected_after_recovery");
    gate_expect_pair_floor(4u, &history4);

    gate_install_pair_fixture(&g_pair_seq5, "\"status\":\"installed\"",
                              "signed_native_rebind_install");
    gate_expect_pair_floor(5u, &history5);
    CHECK(memcmp(history4.native_tbs_sha256, history5.native_tbs_sha256,
                 APP_INSTALL_SHA256_LEN) != 0);
    CHECK(get_state(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data,
                     "\"ui\":{\"status\":\"active\"") != NULL,
              "signed_native_rebind_query_active");
    prior_invoke_count = g_gate_invoke_count;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "signed_native_rebind_explicit_run_started");
    CHECK(g_gate_invoke_count == prior_invoke_count + 1);
    gate_reap_session();

    active_a = mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
    active_b = mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B);
    CHECK(active_a != NULL && active_b != NULL);
    if (active_a != NULL && active_b != NULL) {
        saved_active_a = *active_a;
        saved_active_b = *active_b;
        active_a->present = AICAM_FALSE;
        active_b->present = AICAM_FALSE;
        app_install_boot_resolve_reset();
        prior_invoke_count = g_gate_invoke_count;
        post_run(1, 1);
        CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
                  "run_revoked_pair_native_only_residual_refused");
        CHECK(g_gate_invoke_count == prior_invoke_count);
        gate_expect_pair_floor(5u, NULL);
        *active_a = saved_active_a;
        *active_b = saved_active_b;
        app_install_boot_resolve_reset();
    }

    CHECK(delete_installed(1, 1) == AICAM_OK);
    gate_expect_pair_floor(5u, NULL);
    gate_install_pair_fixture(&g_pair_seq4, "\"status\":\"refused\"",
                              "signed_old_pair_rejected_after_uninstall");
    gate_expect_pair_floor(5u, NULL);
}

static void test_run_busy_and_conflicts(void)
{
    reset_storage();
    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);

    app_install_set_app_running(AICAM_TRUE);
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"BUSY\"") != NULL, "run_busy_reason");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL, "run_busy_refused");
    CHECK_MSG(app_install_app_running() == AICAM_TRUE, "run_busy_flag_preserved");
    CHECK(g_gate_invoke_count == 0);

    post_install(HELLO_V2_PACKAGE, HELLO_V2_PACKAGE_LEN, 1, 1);
    CHECK_MSG(strstr(g_last_response_data, "BUSY") != NULL, "run_install_busy");
    delete_installed(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "BUSY") != NULL, "run_uninstall_busy");
    app_install_set_app_running(AICAM_FALSE);
}

static void test_run_refusal_paths(void)
{
    reset_storage();
    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);

    {
        app_install_record_t committed;
        aicam_bool_t present = AICAM_FALSE;
        mem_file_t *file;
        CHECK(app_install_query_committed(&g_gate_io, &committed, &present) == APP_INSTALL_OK);
        CHECK(present == AICAM_TRUE);
        file = mem_find_file(committed.package_path);
        CHECK(file != NULL);
        file->data[40] ^= 0x40u;
    }
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"UNAVAILABLE\"") != NULL,
              "run_corrupt_unavailable");
    CHECK_MSG(strstr(g_last_response_data, "content_mismatch") != NULL,
              "run_corrupt_native_content_mismatch");
    CHECK(g_gate_invoke_count == 0);

    reset_storage();
    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);
    gate_anchor_set_missing(1);
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"detail\":\"TRUST_UNPROVEN\"") != NULL,
              "run_unproven_detail");
    CHECK(g_gate_invoke_count == 0);

    reset_storage();
    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);
    {
        mem_kv_t *kv = mem_find_key(APP_INSTALL_KEY_RECORD);
        CHECK(kv != NULL);
        kv->data[30] ^= 0x80u;
    }
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"STORAGE_STATE_UNKNOWN\"") != NULL,
              "run_record_corrupt_unknown");
    CHECK_MSG(strstr(g_last_response_data, "\"state\"") == NULL, "run_unknown_no_state_key");
    CHECK(g_gate_invoke_count == 0);
    CHECK(app_install_app_running() == AICAM_FALSE);

    get_state(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"unknown\"") != NULL,
              "run_unknown_query_state");
    CHECK_MSG(strstr(g_last_response_data, "\"app_running\":false") != NULL,
              "run_unknown_query_running");

}

static void test_run_restart_semantics(void)
{
    reset_storage();
    CHECK(seed_native_for_run_test(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN) ==
          APP_INSTALL_OK);

    g_gate_run_raw = 4;
    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"completed\"") != NULL,
              "gate_restart_legacy_run_completed");
    CHECK(g_gate_invoke_count == 1);

    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    CHECK_MSG(app_install_app_running() == AICAM_FALSE, "gate_restart_not_running");
    CHECK_MSG(g_gate_invoke_count == 1, "gate_restart_no_autostart");

    post_run(1, 1);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"completed\"") != NULL,
              "gate_restart_legacy_explicit_run");
    CHECK(g_gate_invoke_count == 2);
}

static void test_unsigned_admin_install_gate(void)
{
    app_install_record_t installed;
    aicam_bool_t present = AICAM_FALSE;

    reset_storage();
    CHECK(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 0) ==
          AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(count_files() == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);

    CHECK(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
          AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "unsigned_api_install_status");
    CHECK(strstr(g_last_response_data, "release_seq") == NULL);
    CHECK(get_state(1, 0) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"state\":\"installed\"") != NULL,
              "unsigned_api_state");
    CHECK(g_gate_invoke_count == 0);

    CHECK(post_run(1, 0) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(g_gate_invoke_count == 0);
    CHECK_MSG(post_run(1, 1) == AICAM_OK, "unsigned_api_high_privilege_run");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL,
              "unsigned_api_explicit_run_started");
    gate_reap_session();

    CHECK(delete_installed(1, 0) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(app_install_query_committed(&g_gate_io, &installed, &present) ==
          APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);

    CHECK(delete_installed(1, 1) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "unsigned_api_high_privilege_uninstall");
    CHECK(app_install_query_committed(&g_gate_io, &installed, &present) ==
          APP_INSTALL_OK);
    CHECK(present == AICAM_FALSE);
    CHECK(count_files() == 0);
    CHECK(post_install(HELLO_V1_PACKAGE, HELLO_V1_PACKAGE_LEN, 1, 1) == AICAM_ERROR);
    CHECK(strstr(g_last_response_message, "LEGACY_SIGNED_FORMAT_REJECTED") != NULL);
}

#define LT_REC_SIZE 272u
#define LT_FLOOR_SIZE 216u
#define LT_REC_STATE_OFF 8u
#define LT_REC_GEN_OFF 12u
#define LT_REC_PUB_OFF 20u
#define LT_REC_APP_OFF 36u
#define LT_REC_KEY_OFF 68u
#define LT_REC_NATIVE_OFF 100u
#define LT_REC_UI_OFF 132u
#define LT_REC_VERSION_OFF 164u
#define LT_REC_ABI_OFF 170u
#define LT_REC_COUNT_OFF 174u
#define LT_REC_MANIFEST_OFF 176u
#define LT_REC_PAYLOAD_OFF 180u
#define LT_REC_BUNDLE_LEN_OFF 184u
#define LT_REC_BUNDLE_SHA_OFF 188u
#define LT_REC_PAIR_TBS_OFF 220u
#define LT_REC_NATIVE_VERSION_OFF 252u
#define LT_REC_RELEASE_SEQ_OFF 258u
#define LT_REC_CRC_OFF 268u
#define LT_FLOOR_PUB_OFF 16u
#define LT_FLOOR_APP_OFF 32u
#define LT_FLOOR_KEY_OFF 64u
#define LT_FLOOR_NATIVE_OFF 96u
#define LT_FLOOR_UI_OFF 128u
#define LT_FLOOR_PAIR_OFF 160u
#define LT_FLOOR_SEQ_OFF 192u
#define LT_FLOOR_NATIVE_VERSION_OFF 200u
#define LT_FLOOR_UI_VERSION_OFF 206u
#define LT_FLOOR_CRC_OFF 212u

static uint32_t lt_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int j;
    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void lt_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void lt_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void lt_put_u64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    }
}

static void lt_fill_tag(uint8_t *blob, size_t off)
{
    size_t i;
    for (i = 0u; i < 32u; i++) {
        blob[off + i] = (uint8_t)(0x80u + ((off + i) & 0x3Fu));
    }
}

static void lt_seed_record(const char *key, uint8_t state, int keep_payload)
{
    uint8_t blob[LT_REC_SIZE];
    memset(blob, 0, sizeof(blob));
    lt_put_u32(blob, 0x31534341u);
    lt_put_u32(blob + 4u, 1u);
    blob[LT_REC_STATE_OFF] = state;
    lt_put_u64(blob + LT_REC_GEN_OFF, 7u);
    memcpy(blob + LT_REC_PUB_OFF, "ne301-exp-pub", 13u);
    memcpy(blob + LT_REC_APP_OFF, "line-crossing", 13u);
    lt_fill_tag(blob, LT_REC_KEY_OFF);
    lt_fill_tag(blob, LT_REC_NATIVE_OFF);
    lt_fill_tag(blob, LT_REC_UI_OFF);
    lt_put_u16(blob + LT_REC_VERSION_OFF, 4u);
    lt_put_u16(blob + LT_REC_VERSION_OFF + 2u, 0u);
    lt_put_u16(blob + LT_REC_VERSION_OFF + 4u, 4u);
    lt_put_u32(blob + LT_REC_ABI_OFF, keep_payload ? APP_SIDECAR_HOST_UI_ABI : 0u);
    lt_put_u16(blob + LT_REC_COUNT_OFF, keep_payload ? 2u : 0u);
    lt_put_u32(blob + LT_REC_MANIFEST_OFF, keep_payload ? 1024u : 0u);
    lt_put_u32(blob + LT_REC_PAYLOAD_OFF, keep_payload ? 2048u : 0u);
    lt_put_u32(blob + LT_REC_BUNDLE_LEN_OFF, keep_payload ? 4096u : 0u);
    if (keep_payload) {
        lt_fill_tag(blob, LT_REC_BUNDLE_SHA_OFF);
    }
    lt_fill_tag(blob, LT_REC_PAIR_TBS_OFF);
    lt_put_u16(blob + LT_REC_NATIVE_VERSION_OFF, 4u);
    lt_put_u16(blob + LT_REC_NATIVE_VERSION_OFF + 2u, 0u);
    lt_put_u16(blob + LT_REC_NATIVE_VERSION_OFF + 4u, 4u);
    lt_put_u64(blob + LT_REC_RELEASE_SEQ_OFF, 5u);
    lt_put_u32(blob + LT_REC_CRC_OFF, lt_crc32(blob, LT_REC_CRC_OFF));
    CHECK(g_gate_io.kv_write(g_gate_io.ctx, key, blob, sizeof(blob)) ==
          APP_INSTALL_IO_OK);
}

static void lt_seed_floor_matching(void)
{
    uint8_t tomb[LT_REC_SIZE];
    uint8_t blob[LT_FLOOR_SIZE];
    mem_kv_t *kv = mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
    CHECK(kv != NULL);
    memcpy(tomb, kv->data, sizeof(tomb));
    memset(blob, 0, sizeof(blob));
    lt_put_u32(blob, 0x31465041u);
    lt_put_u32(blob + 4u, 1u);
    lt_put_u64(blob + 8u, 3u);
    memcpy(blob + LT_FLOOR_PUB_OFF, tomb + LT_REC_PUB_OFF, 16u);
    memcpy(blob + LT_FLOOR_APP_OFF, tomb + LT_REC_APP_OFF, 32u);
    memcpy(blob + LT_FLOOR_KEY_OFF, tomb + LT_REC_KEY_OFF, 32u);
    memcpy(blob + LT_FLOOR_NATIVE_OFF, tomb + LT_REC_NATIVE_OFF, 32u);
    memcpy(blob + LT_FLOOR_UI_OFF, tomb + LT_REC_UI_OFF, 32u);
    memcpy(blob + LT_FLOOR_PAIR_OFF, tomb + LT_REC_PAIR_TBS_OFF, 32u);
    memcpy(blob + LT_FLOOR_SEQ_OFF, tomb + LT_REC_RELEASE_SEQ_OFF, 8u);
    memcpy(blob + LT_FLOOR_NATIVE_VERSION_OFF, tomb + LT_REC_NATIVE_VERSION_OFF, 6u);
    memcpy(blob + LT_FLOOR_UI_VERSION_OFF, tomb + LT_REC_VERSION_OFF, 6u);
    lt_put_u32(blob + LT_FLOOR_CRC_OFF, lt_crc32(blob, LT_FLOOR_CRC_OFF));
    CHECK(g_gate_io.kv_write(g_gate_io.ctx, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                             blob, sizeof(blob)) == APP_INSTALL_IO_OK);
}

static void lt_seed_floor_foreign(void)
{
    uint8_t blob[LT_FLOOR_SIZE];
    memset(blob, 0, sizeof(blob));
    lt_put_u32(blob, 0x31465041u);
    lt_put_u32(blob + 4u, 1u);
    lt_put_u64(blob + 8u, 3u);
    memcpy(blob + LT_FLOOR_PUB_OFF, "another-pub", 11u);
    memcpy(blob + LT_FLOOR_APP_OFF, "another-app", 11u);
    lt_fill_tag(blob, LT_FLOOR_KEY_OFF);
    lt_fill_tag(blob, LT_FLOOR_NATIVE_OFF);
    lt_fill_tag(blob, LT_FLOOR_UI_OFF);
    lt_fill_tag(blob, LT_FLOOR_PAIR_OFF);
    lt_put_u64(blob + LT_FLOOR_SEQ_OFF, 9u);
    lt_put_u16(blob + LT_FLOOR_NATIVE_VERSION_OFF, 1u);
    lt_put_u16(blob + LT_FLOOR_UI_VERSION_OFF, 1u);
    lt_put_u32(blob + LT_FLOOR_CRC_OFF, lt_crc32(blob, LT_FLOOR_CRC_OFF));
    CHECK(g_gate_io.kv_write(g_gate_io.ctx, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                             blob, sizeof(blob)) == APP_INSTALL_IO_OK);
}

static void lt_seed_intent_from_record(void)
{
    uint8_t blob[312];
    mem_kv_t *kv = mem_find_key(APP_INSTALL_KEY_RECORD);
    CHECK_MSG(kv != NULL, "lt_intent_source_record");

    memset(blob, 0, sizeof(blob));
    lt_put_u32(blob, 0x31554941u);
    lt_put_u32(blob + 4u, 1u);
    blob[8] = 1u;
    memcpy(blob + 12u, kv->data, 296u);
    lt_put_u32(blob + 308u, lt_crc32(blob, 308u));
    CHECK(g_gate_io.kv_delete(g_gate_io.ctx, APP_INSTALL_KEY_RECORD) ==
          APP_INSTALL_IO_OK);
    CHECK(g_gate_io.kv_write(g_gate_io.ctx, APP_INSTALL_KEY_UNINSTALL,
                             blob, sizeof(blob)) == APP_INSTALL_IO_OK);
}

static void lt_assert_installed_authentic(const char *what)
{
    app_install_record_t installed;
    aicam_bool_t present = AICAM_FALSE;

    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL, what);
    CHECK_MSG(app_install_query_committed(&g_gate_io, &installed, &present) ==
              APP_INSTALL_OK && present == AICAM_TRUE, "lt_committed_present");
    CHECK_MSG(strcmp(installed.publisher_id, "admin-installed") == 0 &&
              installed.publisher_key_sha256[0] == 0u &&
              installed.publisher_key_sha256[31] == 0u, "lt_committed_admin_unsigned");
    CHECK_MSG(strstr(g_last_response_data, "release_seq") == NULL &&
              strstr(g_last_response_data, "ne301-exp-pub") == NULL,
              "lt_no_legacy_identity_leak");
}

static void test_legacy_migration_full_lifecycle(void)
{
    app_install_record_t installed;
    aicam_bool_t present = AICAM_FALSE;
    int cycle;

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    for (cycle = 1; cycle <= 3; cycle++) {
        CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
                  AICAM_OK, "lt_cycle_install_invoked");
        CHECK_MSG(strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
                  "lt_cycle_install_installed");
        CHECK_MSG(app_install_query_committed(&g_gate_io, &installed, &present) ==
                      APP_INSTALL_OK && present == AICAM_TRUE &&
                      strcmp(installed.publisher_id, "admin-installed") == 0,
                  "lt_cycle_committed");
        CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_cycle_delete_invoked");
        CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
                  "lt_cycle_delete_uninstalled");
        CHECK_MSG(app_install_query_committed(&g_gate_io, &installed, &present) ==
                      APP_INSTALL_OK && present == AICAM_FALSE,
                  "lt_cycle_committed_cleared");
        CHECK_MSG(mem_find_key(APP_INSTALL_KEY_UNINSTALL) == NULL,
                  "lt_cycle_intent_absent");
    }
}

static void lt_seed_signed_committed_record(void)
{
    app_install_record_t signed_rec;
    uint8_t blob[296];
    uint8_t payload[64];
    uint8_t key[32];
    uint8_t tbs[32];
    uint8_t pkg[32];
    size_t i;
    memset(&signed_rec, 0, sizeof(signed_rec));
    for (i = 0; i < sizeof(key); i++) {
        key[i] = (uint8_t)(0xa0u + i);
        tbs[i] = (uint8_t)(0x30u + i);
        pkg[i] = (uint8_t)(0x50u + i);
        payload[i] = (uint8_t)(i & 0xffu);
    }
    snprintf(signed_rec.publisher_id, sizeof(signed_rec.publisher_id), "%s",
             "ne301-exp-pub");
    snprintf(signed_rec.app_id, sizeof(signed_rec.app_id), "%s", "line-crossing");
    signed_rec.version_major = 4u;
    signed_rec.version_patch = 4u;
    signed_rec.package_len = sizeof(payload);
    memcpy(signed_rec.publisher_key_sha256, key, sizeof(key));
    memcpy(signed_rec.tbs_sha256, tbs, sizeof(tbs));
    memcpy(signed_rec.package_sha256, pkg, sizeof(pkg));
    {
        char hex_path[APP_INSTALL_PATH_MAX];
        size_t pos;
        size_t si;
        pos = (size_t)snprintf(hex_path, sizeof(hex_path), "%s/", APP_INSTALL_PKG_DIR);
        for (si = 0; si < sizeof(pkg); si++) {
            pos += (size_t)snprintf(hex_path + pos, sizeof(hex_path) - pos, "%02x",
                                    pkg[si]);
        }
        (void)snprintf(hex_path + pos, sizeof(hex_path) - pos, "%s", ".neapp");
        snprintf(signed_rec.package_path, sizeof(signed_rec.package_path), "%s",
                 hex_path);
    }
    CHECK_MSG(app_install_record_encode(&signed_rec, blob, sizeof(blob)) ==
              AICAM_TRUE, "lt_signed_record_encoded");
    CHECK(g_gate_io.kv_write(g_gate_io.ctx, APP_INSTALL_KEY_RECORD, blob,
                             sizeof(blob)) == APP_INSTALL_IO_OK);
    CHECK(g_gate_io.file_write(g_gate_io.ctx, signed_rec.package_path, payload,
                               sizeof(payload)) == APP_INSTALL_IO_OK);
}

static void test_device_kv_strict_semantics_lifecycle(void)
{
    reset_storage();
    g_io.strict_exact_size = 1;
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_strict_install_admitted");
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_strict_delete_completes_first_try");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL &&
              mem_find_key(APP_INSTALL_KEY_UNINSTALL) == NULL,
              "lt_strict_record_and_intent_cleared");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) == NULL,
              "lt_strict_floor_consumed");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_strict_reinstall_admitted");
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_strict_second_uninstall_completes");

    reset_storage();
    lt_seed_signed_committed_record();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 1u, 1);
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B, 2u, 0);
    lt_seed_floor_matching();
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_strict_signed_delete_completes");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) != NULL,
              "lt_strict_signed_delete_preserves_floor");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_strict_unsigned_admitted_after_signed_delete");
    g_io.strict_exact_size = 0;
}

static void test_device_kv_strict_state_rollback(void)
{
    app_state_session_t session;
    app_state_identity_t ident;
    uint8_t seed[APP_STATE_SHA256_LEN + APP_STATE_PUB_SLOT + APP_STATE_APP_SLOT];
    uint8_t hash[32];
    uint8_t pub_pad[APP_STATE_PUB_SLOT];
    uint8_t app_pad[APP_STATE_APP_SLOT];
    uint8_t stale[134];
    uint8_t blob[64];
    char meta_key[24];
    uint32_t new_rev = 0;
    size_t i;

    reset_storage();
    g_io.strict_exact_size = 1;
    lt_seed_signed_committed_record();
    memset(&ident, 0, sizeof(ident));
    for (i = 0; i < sizeof(ident.spki_sha256); i++) {
        ident.spki_sha256[i] = (uint8_t)(0xa0u + i);
    }
    snprintf(ident.publisher_id, sizeof(ident.publisher_id), "%s", "ne301-exp-pub");
    snprintf(ident.app_id, sizeof(ident.app_id), "%s", "line-crossing");
    memset(pub_pad, 0, sizeof(pub_pad));
    memset(app_pad, 0, sizeof(app_pad));
    memcpy(pub_pad, ident.publisher_id, strlen(ident.publisher_id));
    memcpy(app_pad, ident.app_id, strlen(ident.app_id));
    memcpy(seed, ident.spki_sha256, APP_STATE_SHA256_LEN);
    memcpy(seed + APP_STATE_SHA256_LEN, pub_pad, APP_STATE_PUB_SLOT);
    memcpy(seed + APP_STATE_SHA256_LEN + APP_STATE_PUB_SLOT, app_pad,
           APP_STATE_APP_SLOT);
    mbedtls_sha256(seed, sizeof(seed), hash, 0);
    snprintf(meta_key, sizeof(meta_key), "app_state_%02x%02x%02x%02x%02x",
             hash[0], hash[1], hash[2], hash[3], hash[4]);

    for (i = 0; i < sizeof(stale); i++) {
        stale[i] = (uint8_t)i;
        blob[i % sizeof(blob)] = (uint8_t)(0xa5u ^ i);
    }
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s",
             meta_key);
    g_io.fail_kv_read_key_skip = 1;
    CHECK(app_state_session_bind(&session, &g_gate_io, &ident, 4096) ==
          APP_STATE_OK);
    CHECK_MSG(app_state_commit(&session, blob, sizeof(blob),
                               APP_STATE_REVISION_NONE, &new_rev) ==
              APP_STATE_IO_ERROR,
              "lt_strict_meta_rollback_clean_io_error");
    CHECK_MSG(mem_find_key(meta_key) == NULL,
              "lt_strict_meta_rollback_key_deleted");
    g_io.fail_kv_read_key[0] = '\0';
    g_io.strict_exact_size = 0;
    app_state_session_end(&session);
}

static void test_run_model_gate_reports_refused_not_started(void)
{
    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_ghost_install_ok");
    g_gate_model_od_fail = 1;
    CHECK_MSG(post_run(1, 1) == AICAM_OK, "lt_ghost_run_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_ghost_model_fail_refused");
    CHECK_MSG(strstr(g_last_response_data, "\"publisher_id\":\"\"") == NULL,
              "lt_ghost_no_empty_started");
    g_gate_model_od_fail = 0;
    CHECK_MSG(post_run(1, 1) == AICAM_OK, "lt_ghost_run_ok_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"started\"") != NULL &&
              strstr(g_last_response_data, "\"publisher_id\":\"admin-installed\"") != NULL,
              "lt_ghost_run_started_real_identity");
    (void)app_host_session_stop(0, NULL);
    app_install_set_app_running(AICAM_FALSE);
}

static void test_big_alloc_mechanism_and_wedge_recovery(void)
{
    unsigned calls_before;

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    g_io.big_alloc_calls = 0;
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_alloc_install_ok");
    calls_before = g_io.big_alloc_calls;
    CHECK_MSG(calls_before > 0u, "lt_alloc_vtable_used");
    g_io.big_alloc_calls = 0;
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_alloc_uninstall_ok");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    g_io.big_alloc_calls = 0;
    g_io.fail_big_alloc_after = calls_before - 1u;
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL &&
              strstr(g_last_response_data, "STORAGE_STATE_UNKNOWN") != NULL,
              "lt_alloc_wedge_install_refused");
    g_io.fail_big_alloc_above = 0u;
    g_io.fail_big_alloc_after = 0u;
    g_io.big_alloc_calls = 0;
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) != NULL,
              "lt_alloc_wedge_stage_left");
    CHECK_MSG(post_run(1, 1) == AICAM_OK, "lt_alloc_wedge_run_invoked");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) == NULL,
              "lt_alloc_recovery_stage_cleared");
    CHECK_MSG(app_install_app_running() == AICAM_TRUE,
              "lt_alloc_recovery_app_running");
    app_install_set_app_running(AICAM_FALSE);
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_alloc_final_uninstall");
}

static void test_legacy_signed_delete_preserves_floor(void)
{
    reset_storage();
    lt_seed_signed_committed_record();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 1u, 1);
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B, 2u, 0);
    lt_seed_floor_matching();

    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_signed_delete_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_signed_delete_completes");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) != NULL,
              "lt_signed_delete_preserves_monotonic_floor");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_signed_delete_record_cleared");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_UNINSTALL) == NULL,
              "lt_signed_delete_intent_cleared");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_signed_retired_lineage_admits_unsigned_after_delete");
}

static void test_legacy_floor_persistence_cutpoints(void)
{
    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_cut_a_install");

    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_cut_a_delete_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"cleanup_pending\"") != NULL,
              "lt_cut_a_slot_write_fault_pending");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) != NULL,
              "lt_cut_a_floor_intact_before_persistence");
    g_io.fail_kv_write_key[0] = '\0';
    app_install_boot_resolve_reset();
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_cut_a_retry_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_cut_a_retry_completes");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) == NULL,
              "lt_cut_a_retry_consumes_floor");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"installed\"") != NULL,
              "lt_cut_a_reinstall_after_recovery");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_cut_b_install");
    snprintf(g_io.fail_kv_delete_key, sizeof(g_io.fail_kv_delete_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR);
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_cut_b_delete_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"cleanup_pending\"") != NULL,
              "lt_cut_b_floor_delete_fault_pending");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) != NULL,
              "lt_cut_b_floor_preserved_on_fault");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_cut_b_record_already_cleaned");
    g_io.fail_kv_delete_key[0] = '\0';
    app_install_boot_resolve_reset();
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_cut_b_retry_invoked");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_cut_b_retry_completes_idempotent");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) != NULL,
              "lt_cut_b_orphan_floor_documented_retained");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_cut_b_orphan_floor_keeps_fail_closed");
}

static void test_legacy_unsigned_transition(void)
{
    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_retired_tombstone_admits_unsigned");
    lt_assert_installed_authentic("lt_tomb_install_authentic");
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_tomb_delete_ok");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_matching();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_retired_lineage_floor_admits_unsigned");
    lt_assert_installed_authentic("lt_lineage_floor_install_authentic");
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_lineage_floor_delete_ok");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) == NULL,
              "lt_retired_lineage_floor_consumed_at_rotation");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 1u, 1);
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_active_signed_refused");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_active_signed_norecord");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 1);
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_partial_uninstall_refused");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_partial_uninstall_norecord");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_floor_foreign();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_foreign_floor_refused");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_foreign_floor_norecord");

    reset_storage();
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    {
        mem_kv_t *kv = mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
        CHECK_MSG(kv != NULL, "lt_corrupt_kv_found");
        kv->data[LT_REC_PUB_OFF] ^= 0xFFu;
    }
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_corrupt_record_refused");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_corrupt_record_norecord");

    reset_storage();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_interrupted_install_source");
    lt_seed_record(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A, 2u, 0);
    lt_seed_intent_from_record();
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_UNINSTALL) != NULL,
              "lt_interrupted_state_intent_alive");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_interrupted_uninstall_refuses_install");
    CHECK_MSG(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL,
              "lt_interrupted_uninstall_norecord");
    app_install_boot_resolve_reset();
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_interrupted_recovery_delete");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_interrupted_recovery_completes");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_completed_uninstall_admits_reinstall");
    lt_assert_installed_authentic("lt_recovery_install_authentic");

    reset_storage();
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK, "lt_foreign_floor_install_source");
    lt_seed_floor_foreign();
    CHECK_MSG(delete_installed(1, 1) == AICAM_OK, "lt_foreign_floor_delete_ok");
    CHECK_MSG(strstr(g_last_response_data, "\"status\":\"uninstalled\"") != NULL,
              "lt_foreign_floor_delete_completes");
    CHECK_MSG(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR) != NULL,
              "lt_foreign_floor_preserved");
    CHECK_MSG(post_install(g_unsigned_package.data, g_unsigned_package.len, 1, 1) ==
              AICAM_OK &&
              strstr(g_last_response_data, "\"status\":\"refused\"") != NULL,
              "lt_foreign_floor_keeps_fail_closed");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "unsigned") == 0) {
        if (!gate_pair_fixture_load("../app_unsigned/single-file-v5.neapp",
                                    &g_unsigned_package)) {
            printf("FAIL unsigned_api_fixture_load\n");
            return 1;
        }
        test_legacy_migration_full_lifecycle();
        test_device_kv_strict_semantics_lifecycle();
        test_device_kv_strict_state_rollback();
        test_run_model_gate_reports_refused_not_started();
        test_big_alloc_mechanism_and_wedge_recovery();
        test_legacy_signed_delete_preserves_floor();
        test_legacy_floor_persistence_cutpoints();
        test_unsigned_admin_install_gate();
        test_legacy_unsigned_transition();
        free(g_unsigned_package.data);
        printf("app_install_gate_unsigned: %d checks, %d failures\n",
               g_total, g_fail);
        return g_fail == 0 ? 0 : 1;
    }
    int have_signed_pair_fixtures = gate_pair_fixtures_load();

#ifdef APP_TRUST_SYNTHETIC_GATE
    if (have_signed_pair_fixtures > 0) {
        test_synthetic_production_trust_negatives();
        test_signed_pair_run_and_monotonic_floor();
    } else {
        CHECK_MSG(0, "synthetic_signed_pair_fixtures_required");
    }
#else
    test_route_table_registered();
    test_nepk_ci_candidate_api();
    test_install_auth_gate();
    test_install_method_gate();
    test_install_payload_negatives();
    test_install_lifecycle_end_to_end();
    test_state_query_trust_unproven_paths();
    test_install_busy_running();
    test_install_protocol_length_gate();
    test_run_auth_and_method_gate();
    test_run_lifecycle_end_to_end();
    test_run_busy_and_conflicts();
    test_run_refusal_paths();
    test_run_restart_semantics();
    if (have_signed_pair_fixtures > 0) {
        test_signed_pair_run_and_monotonic_floor();
    }
#endif
    gate_pair_fixtures_free();

    printf("app_install_gate: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
