#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "app_installer.h"
#include "app_trust_anchor.h"
#include "app_sidecar_lifecycle.h"
#include "app_sidecar_verify.h"
#include "app_pair_history.h"
#include "app_host_control.h"
#include "app_host_live.h"
#include "app_admission.h"
#include "app_report_policy.h"
#include "app_report_service.h"
#include "web_assets.h"
#include "web_api.h"
#include "web_server.h"
#include "websocket_stream_server.h"
#include "api_app_session_module.h"
#include "cJSON.h"
#include "sidecar_vectors.h"
#include "mbedtls/sha256.h"

#define SC_NATIVE_PACKAGE_LEN 4096u

static int g_total;
static int g_fail;

static void check_msg_impl(int cond, int line, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL bridge:%d line=%d: %s\n", g_total, line, what);
    }
}

#define CHECK(cond) check_msg_impl((cond), __LINE__, #cond)

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../app_sidecar_lifecycle/mem_sidecar_backend.h"

static char g_last_kind[16];
static char g_last_data[16384];
static char g_last_message[256];
static int g_last_error_code;

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data,
                                    const char *message)
{
    (void)ctx;
    snprintf(g_last_kind, sizeof(g_last_kind), "success");
    snprintf(g_last_message, sizeof(g_last_message), "%s", message ? message : "");
    g_last_data[0] = '\0';
    if (data != NULL) {
        snprintf(g_last_data, sizeof(g_last_data), "%s", data);
        cJSON_free((void *)data);
    }
    return AICAM_OK;
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code,
                                  const char *message)
{
    (void)ctx;
    snprintf(g_last_kind, sizeof(g_last_kind), "error");
    g_last_error_code = (int)error_code;
    snprintf(g_last_message, sizeof(g_last_message), "%s", message ? message : "");
    return AICAM_ERROR;
}

#define MAX_CAPTURED_ROUTES 16
static api_route_t g_captured_routes[MAX_CAPTURED_ROUTES];
static int g_captured_route_count;

aicam_result_t http_server_register_route(const api_route_t *route)
{
    if (g_captured_route_count >= MAX_CAPTURED_ROUTES) {
        return AICAM_ERROR;
    }
    g_captured_routes[g_captured_route_count] = *route;
    g_captured_route_count++;
    return AICAM_OK;
}

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx)
{
    if (ctx == NULL) {
        return AICAM_FALSE;
    }
    return (aicam_bool_t)((ctx->authenticated == AICAM_TRUE &&
                           ctx->high_privilege == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE);
}

static int g_ws_running = 1;
static int g_ws_stream_active = 1;
static int g_ws_start_calls;
static int g_ws_start_fail;
static uint32_t g_ws_start_last_id;

aicam_bool_t websocket_stream_server_is_running(void)
{
    return (g_ws_running != 0) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_result_t websocket_stream_server_get_stats(websocket_stream_stats_t *stats)
{
    if (stats == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    memset(stats, 0, sizeof(*stats));
    stats->stream_active = (g_ws_stream_active != 0) ? AICAM_TRUE : AICAM_FALSE;
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_start_stream(uint32_t stream_id)
{
    g_ws_start_calls++;
    g_ws_start_last_id = stream_id;
    if (g_ws_start_fail != 0) {
        return AICAM_ERROR;
    }
    g_ws_stream_active = 1;
    return AICAM_OK;
}

static const app_trust_anchor_t g_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    SC_VEC_PUBLISHER_ID,
    SC_VEC_PUBLISHER_ID_LEN,
    SC_VEC_SPKI_DER,
    SC_VEC_SPKI_LEN,
    NULL
};

static int g_anchor_missing;

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    return &g_anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    return (g_anchor_missing != 0) ? APP_TRUST_ANCHOR_MISSING : APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    (void)status;
    return "anchor";
}

static app_install_io_t g_test_io;

const app_install_io_t *app_installer_device_io(void)
{
    mem_io_init(&g_test_io);
    return &g_test_io;
}

void app_host_session_init(const void *backend)
{
    (void)backend;
}

const void *app_host_session_device_backend(void)
{
    return NULL;
}

const char *app_session_result_str(int result)
{
    (void)result;
    return "session-result";
}

const char *app_session_state_str(int state)
{
    (void)state;
    return "session-state";
}

static volatile uint32_t g_now_ms;
static volatile uint32_t g_now_step;
static uint64_t g_epoch_ms;
static void (*g_sleep_hook)(uint32_t ms);
static volatile uint64_t g_fake_live_epoch;

static uint32_t bridge_now(void)
{
    return g_now_ms;
}

static uint64_t bridge_epoch(void)
{
    return g_epoch_ms;
}

static void bridge_sleep(uint32_t ms)
{
    if (g_sleep_hook != NULL) {
        g_sleep_hook(ms);
    }
    g_now_ms += g_now_step;
}

static void *fake_mutex_create(void *ctx)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

    (void)ctx;
    pthread_mutex_init(m, NULL);
    return m;
}

static void fake_mutex_destroy(void *ctx, void *mtx)
{
    (void)ctx;
    if (mtx != NULL) {
        pthread_mutex_destroy((pthread_mutex_t *)mtx);
        free(mtx);
    }
}

static int fake_mutex_lock(void *ctx, void *mtx)
{
    (void)ctx;
    return pthread_mutex_lock((pthread_mutex_t *)mtx);
}

static void fake_mutex_unlock(void *ctx, void *mtx)
{
    (void)ctx;
    pthread_mutex_unlock((pthread_mutex_t *)mtx);
}

static uint32_t fake_zero_now(void *ctx)
{
    (void)ctx;
    return 0u;
}

static void ctl_init_fake(void)
{
    app_control_backend_t b;

    memset(&b, 0, sizeof(b));
    b.now_ms = fake_zero_now;
    b.mutex_create = fake_mutex_create;
    b.mutex_destroy = fake_mutex_destroy;
    b.mutex_lock = fake_mutex_lock;
    b.mutex_unlock = fake_mutex_unlock;
    app_host_control_init(&b);
}

static void build_cmd_header(uint8_t *buf, uint32_t type, uint32_t total,
                             uint32_t gen, uint32_t id, uint32_t cgen, uint32_t flags)
{
    memset(buf, 0, APP_CONTROL_HEADER_SIZE);
    buf[0] = 0x4Eu; buf[1] = 0x46u; buf[2] = 0x43u; buf[3] = 0x33u;
    buf[4] = 0x01u; buf[5] = 0x00u;
    buf[6] = (uint8_t)(type & 0xFFu);
    buf[7] = (uint8_t)((type >> 8) & 0xFFu);
    buf[8] = (uint8_t)(total & 0xFFu);
    buf[9] = (uint8_t)((total >> 8) & 0xFFu);
    buf[10] = (uint8_t)((total >> 16) & 0xFFu);
    buf[11] = (uint8_t)((total >> 24) & 0xFFu);
    buf[12] = (uint8_t)(gen & 0xFFu);
    buf[13] = (uint8_t)((gen >> 8) & 0xFFu);
    buf[14] = (uint8_t)((gen >> 16) & 0xFFu);
    buf[15] = (uint8_t)((gen >> 24) & 0xFFu);
    buf[16] = (uint8_t)(id & 0xFFu);
    buf[17] = (uint8_t)((id >> 8) & 0xFFu);
    buf[18] = (uint8_t)((id >> 16) & 0xFFu);
    buf[19] = (uint8_t)((id >> 24) & 0xFFu);
    buf[20] = (uint8_t)(cgen & 0xFFu);
    buf[21] = (uint8_t)((cgen >> 8) & 0xFFu);
    buf[22] = (uint8_t)((cgen >> 16) & 0xFFu);
    buf[23] = (uint8_t)((cgen >> 24) & 0xFFu);
    buf[28] = (uint8_t)(flags & 0xFFu);
    buf[29] = (uint8_t)((flags >> 8) & 0xFFu);
    buf[30] = (uint8_t)((flags >> 16) & 0xFFu);
    buf[31] = (uint8_t)((flags >> 24) & 0xFFu);
}

static void build_receipt(uint8_t *buf, uint32_t gen, uint32_t id, uint32_t new_gen,
                          uint32_t rev, uint32_t status)
{
    build_cmd_header(buf, APP_CONTROL_MSG_TYPE_RECEIPT, APP_CONTROL_HEADER_SIZE,
                     gen, id, new_gen, status);
    buf[24] = (uint8_t)(rev & 0xFFu);
    buf[25] = (uint8_t)((rev >> 8) & 0xFFu);
    buf[26] = (uint8_t)((rev >> 16) & 0xFFu);
    buf[27] = (uint8_t)((rev >> 24) & 0xFFu);
}

static void build_snapshot(uint8_t *buf, const char *json, uint32_t cgen, uint32_t rev,
                           uint32_t sg, uint32_t rid)
{
    uint32_t plen = (uint32_t)strlen(json);
    uint32_t total = APP_CONTROL_HEADER_SIZE + plen;

    memset(buf, 0, total);
    build_cmd_header(buf, APP_CONTROL_MSG_TYPE_SNAPSHOT, total, sg, rid, cgen, 0u);
    buf[24] = (uint8_t)(rev & 0xFFu);
    buf[25] = (uint8_t)((rev >> 8) & 0xFFu);
    buf[26] = (uint8_t)((rev >> 16) & 0xFFu);
    buf[27] = (uint8_t)((rev >> 24) & 0xFFu);
    memcpy(buf + APP_CONTROL_HEADER_SIZE, json, plen);
}

static uint32_t g_ctl_gen;

static uint32_t ctl_session_open(void)
{
    app_control_session_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.caps = 0xFFFFFFFFu;
    cfg.control_max = APP_CONTROL_MSG_RAM_MAX;
    cfg.snapshot_max = APP_CONTROL_SNAPSHOT_RAM_MAX;
    strcpy(cfg.publisher_id, SC_VEC_PUBLISHER_ID);
    strcpy(cfg.app_id, SC_VEC_APP_ID);
    CHECK(app_host_control_session_open(&cfg, &g_ctl_gen) == 0);
    return g_ctl_gen;
}

static void native_receipt_hook(uint32_t status)
{
    uint8_t rx[600];
    uint32_t rx_len = 0u;
    uint32_t id;

    if (app_host_control_deliver(rx, sizeof(rx), &rx_len) != APP_CONTROL_OK) {
        return;
    }
    id = (uint32_t)rx[16] | ((uint32_t)rx[17] << 8) | ((uint32_t)rx[18] << 16) |
         ((uint32_t)rx[19] << 24);
    build_receipt(rx, g_ctl_gen, id, 6u, 5u, status);
    app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE);
}

static void native_apply_hook(uint32_t ms)
{
    (void)ms;
    native_receipt_hook(APP_CONTROL_RESULT_APPLIED);
}

static void native_stale_hook(uint32_t ms)
{
    (void)ms;
    native_receipt_hook(APP_CONTROL_RESULT_REJECTED_STALE_CONFIG);
}

static void native_noop_hook(uint32_t ms)
{
    (void)ms;
}

static void set_package_path(app_install_record_t *record)
{
    size_t i;
    size_t pos = 0u;

    pos = (size_t)snprintf(record->package_path, sizeof(record->package_path), "%s/",
                           APP_INSTALL_PKG_DIR);
    for (i = 0u; i < APP_INSTALL_SHA256_LEN; i++) {
        pos += (size_t)snprintf(record->package_path + pos,
                                sizeof(record->package_path) - pos,
                                "%02x", record->package_sha256[i]);
    }
    (void)snprintf(record->package_path + pos, sizeof(record->package_path) - pos,
                   ".neapp");
}

static void make_native_record(app_install_record_t *record)
{
    uint8_t encoded[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t package[SC_NATIVE_PACKAGE_LEN];
    size_t i;
    const app_install_io_t *io = app_installer_device_io();

    memset(record, 0, sizeof(*record));
    snprintf(record->publisher_id, sizeof(record->publisher_id), "%s",
             APP_ADMISSION_ADMIN_PUBLISHER_ID);
    snprintf(record->app_id, sizeof(record->app_id), "%s", SC_VEC_APP_ID);
    record->version_major = 1u;
    record->version_minor = 0u;
    record->version_patch = 0u;
    for (i = 0u; i < sizeof(package); i++) {
        package[i] = (uint8_t)(0x41u + (i % 0x1Bu));
    }
    record->package_len = (uint32_t)sizeof(package);
    mbedtls_sha256(package, sizeof(package), record->package_sha256, 0);
    memcpy(record->tbs_sha256, SC_VEC_NATIVE_TBS, sizeof(record->tbs_sha256));
    memset(record->publisher_key_sha256, 0, sizeof(record->publisher_key_sha256));
    memset(record->image_sha256, 0x27, sizeof(record->image_sha256));
    set_package_path(record);
    CHECK(app_install_record_encode(record, encoded, sizeof(encoded)) == AICAM_TRUE);
    CHECK(io->kv_write(io->ctx, APP_INSTALL_KEY_RECORD, encoded, sizeof(encoded)) ==
          APP_INSTALL_IO_OK);
    CHECK(io->file_write(io->ctx, record->package_path, package, sizeof(package)) ==
          APP_INSTALL_IO_OK);
}

static uint8_t *load_bundle(const char *name, size_t *len)
{
    char path[256];
    FILE *stream;
    uint8_t *data;
    long size;

    (void)snprintf(path, sizeof(path), "build/%s", name);
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return NULL;
    }
    fseek(stream, 0, SEEK_END);
    size = ftell(stream);
    fseek(stream, 0, SEEK_SET);
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(stream);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, stream) != (size_t)size) {
        free(data);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *len = (size_t)size;
    return data;
}

static void install_active_sidecar(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t bundle_len;

    make_native_record(&native);
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = native.publisher_id;
    trust.publisher_id_len = strlen(native.publisher_id);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.native_tbs_sha256 = native.tbs_sha256;
    bundle = load_bundle("serving_small.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = app_sidecar_lifecycle_install_pair(io, &trust, &native, bundle,
                                                bundle_len, NULL,
                                                &descriptor, &state);
    free(bundle);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
}

static uint32_t g_ai_confidence_threshold = 50u;
static uint32_t g_ai_confidence_set_calls;
static uint32_t g_ai_confidence_last_written;
static aicam_bool_t g_ai_confidence_set_fail;

aicam_result_t ai_set_confidence_threshold(uint32_t threshold)
{
    g_ai_confidence_set_calls++;
    g_ai_confidence_last_written = threshold;
    if (g_ai_confidence_set_fail == AICAM_TRUE) {
        return AICAM_ERROR;
    }
    g_ai_confidence_threshold = threshold;
    return AICAM_OK;
}

uint32_t ai_get_confidence_threshold(void)
{
    return g_ai_confidence_threshold;
}

static uint8_t g_last_applied_blob[256];
static uint32_t g_last_applied_blob_len;

static uint8_t g_policy_blob[APP_REPORT_POLICY_STORAGE_BLOB_CAPACITY];
static uint32_t g_policy_blob_len;

static int32_t policy_storage_read(void *user, void *out, uint32_t cap, uint32_t *out_len)
{
    (void)user;
    if (out == NULL || out_len == NULL) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    if (g_policy_blob_len == 0u) {
        return APP_REPORT_POLICY_STORAGE_NOT_FOUND;
    }
    if (cap < g_policy_blob_len) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    memcpy(out, g_policy_blob, g_policy_blob_len);
    *out_len = g_policy_blob_len;
    return APP_REPORT_POLICY_STORAGE_OK;
}

static int32_t policy_storage_write(void *user, const void *data, uint32_t len)
{
    (void)user;
    if (data == NULL || len == 0u || len > sizeof(g_policy_blob)) {
        return APP_REPORT_POLICY_STORAGE_ERROR;
    }
    memcpy(g_policy_blob, data, len);
    g_policy_blob_len = len;
    return APP_REPORT_POLICY_STORAGE_OK;
}

static void policy_storage_attach(void)
{
    app_report_policy_storage_t storage;

    memset(&storage, 0, sizeof(storage));
    storage.user = NULL;
    storage.read = policy_storage_read;
    storage.write = policy_storage_write;
    app_report_policy_store_set_storage(&storage);
}

static void policy_storage_detach(void)
{
    app_report_policy_store_set_storage(NULL);
    g_policy_blob_len = 0u;
}

static void reset_all(void)
{
    mem_reset();
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    web_sidecar_projection_reset();
    g_anchor_missing = 0;
    g_now_ms = 1000u;
    g_now_step = 0u;
    g_epoch_ms = 1000000000u;
    g_sleep_hook = NULL;
    g_last_data[0] = '\0';
    g_last_kind[0] = '\0';
    g_ws_running = 1;
    g_ws_stream_active = 1;
    g_ws_start_calls = 0;
    g_ws_start_fail = 0;
    g_ws_start_last_id = 0u;
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_FALSE);
    app_bridge_video_lan_host_set(NULL);
    app_bridge_grants_reset();
    policy_storage_detach();
    g_ai_confidence_threshold = 50u;
    g_ai_confidence_set_calls = 0u;
    g_ai_confidence_last_written = 0u;
    g_ai_confidence_set_fail = AICAM_FALSE;
    g_last_applied_blob_len = 0u;
}

static const api_route_t *find_route(const char *method, const char *path)
{
    int i;

    for (i = 0; i < g_captured_route_count; i++) {
        if (strcmp(g_captured_routes[i].method, method) == 0 &&
            strcmp(g_captured_routes[i].path, path) == 0) {
            return &g_captured_routes[i];
        }
    }
    return NULL;
}

static cJSON *call_route_msg(const char *method, const char *path, const char *body,
                             size_t content_length, struct mg_http_message *msg)
{
    const api_route_t *route = find_route(method, path);
    http_handler_context_t ctx;
    char *body_copy;
    cJSON *parsed;

    CHECK(route != NULL);
    if (route == NULL) {
        return NULL;
    }
    memset(&ctx, 0, sizeof(ctx));
    body_copy = (char *)malloc(content_length + 1u);
    CHECK(body_copy != NULL);
    if (body_copy == NULL) {
        return NULL;
    }
    memcpy(body_copy, body, content_length);
    body_copy[content_length] = '\0';
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.uri, sizeof(ctx.request.uri), "%s", path);
    ctx.request.body = body_copy;
    ctx.request.content_length = content_length;
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    ctx.msg = msg;
    g_last_kind[0] = '\0';
    g_last_data[0] = '\0';
    (void)route->handler(&ctx);
    free(body_copy);
    if (strcmp(g_last_kind, "success") != 0) {
        return NULL;
    }
    parsed = cJSON_Parse(g_last_data);
    CHECK(parsed != NULL);
    return parsed;
}

static cJSON *call_route(const char *method, const char *path, const char *body,
                         size_t content_length)
{
    return call_route_msg(method, path, body, content_length, NULL);
}

static char g_grant_id[65];

static cJSON *request_grant(const char *capabilities_json)
{
    char body[512];
    cJSON *root;
    cJSON *grant;

    (void)snprintf(body, sizeof(body), "{\"capabilities\":%s}", capabilities_json);
    root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/grant", body,
                      strlen(body));
    CHECK(root != NULL);
    if (root == NULL) {
        return NULL;
    }
    grant = cJSON_GetObjectItem(root, "grant");
    if (grant != NULL && cJSON_IsObject(grant)) {
        cJSON *id = cJSON_GetObjectItem(grant, "grant_id");
        if (id != NULL && cJSON_IsString(id)) {
            snprintf(g_grant_id, sizeof(g_grant_id), "%s", id->valuestring);
        }
    }
    return root;
}

static cJSON *request_grant_raw(const char *capabilities_json)
{
    char body[512];

    (void)snprintf(body, sizeof(body), "{\"capabilities\":%s}", capabilities_json);
    return call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/grant", body,
                      strlen(body));
}

static cJSON *bridge_call(const char *op, uint32_t request_id, const char *extra)
{
    char body[2048];

    if (extra != NULL) {
        (void)snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"%s\",\"request_id\":%u,%s}",
                       g_grant_id, op, request_id, extra);
    } else {
        (void)snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"%s\",\"request_id\":%u}",
                       g_grant_id, op, request_id);
    }
    return call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge", body, strlen(body));
}

static int bridge_call_body(const char *body, size_t len, cJSON **out)
{
    *out = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge", body, len);
    return (*out != NULL);
}

static char g_fake_host_name[] = "Host";
static char g_fake_host_value[96];

struct bridge_fake_header {
    struct mg_str name;
    struct mg_str value;
};

struct bridge_fake_http_msg {
    struct mg_str request_line[4];
    struct bridge_fake_header headers[4];
};

static struct mg_http_message *fake_msg_with_host(const char *host_value)
{
    static struct bridge_fake_http_msg msg;

    memset(&msg, 0, sizeof(msg));
    (void)snprintf(g_fake_host_value, sizeof(g_fake_host_value), "%s", host_value);
    msg.headers[0].name.buf = g_fake_host_name;
    msg.headers[0].name.len = strlen(g_fake_host_name);
    msg.headers[0].value.buf = g_fake_host_value;
    msg.headers[0].value.len = strlen(g_fake_host_value);
    return (struct mg_http_message *)(void *)&msg;
}

static cJSON *bridge_call_via_msg(const char *op, uint32_t request_id, const char *extra,
                                  struct mg_http_message *msg)
{
    char body[2048];

    if (extra != NULL) {
        (void)snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"%s\",\"request_id\":%u,%s}",
                       g_grant_id, op, request_id, extra);
    } else {
        (void)snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"%s\",\"request_id\":%u}",
                       g_grant_id, op, request_id);
    }
    return call_route_msg("POST", API_PATH_PREFIX "/app/sidecar/bridge", body, strlen(body),
                          msg);
}

static int snprintf_bridge_write_body(char *body, size_t cap, const char *op, uint32_t request_id,
                                      uint32_t session_generation, const char *candidate_json,
                                      const char *confirm_token)
{
    int pos;

    if (candidate_json == NULL) {
        pos = snprintf(body, cap,
                       "{\"grant_id\":\"%s\",\"op\":\"%s\",\"request_id\":%u,"
                       "\"payload\":{\"session_generation\":%u}",
                       g_grant_id, op, request_id, session_generation);
    } else {
        pos = snprintf(body, cap,
                       "{\"grant_id\":\"%s\",\"op\":\"%s\",\"request_id\":%u,"
                       "\"payload\":{\"session_generation\":%u,\"candidate\":%s}",
                       g_grant_id, op, request_id, session_generation, candidate_json);
    }
    if (pos < 0 || (size_t)pos >= cap) {
        return -1;
    }
    if (confirm_token != NULL) {
        pos += snprintf(body + pos, cap - (size_t)pos, ",\"confirm_token\":\"%s\"",
                        confirm_token);
        if (pos < 0 || (size_t)pos >= cap) {
            return -1;
        }
    }
    pos += snprintf(body + pos, cap - (size_t)pos, "}");
    if (pos < 0 || (size_t)pos >= cap) {
        return -1;
    }
    return pos;
}

static const char *outcome_of(cJSON *root)
{
    cJSON *result;
    cJSON *outcome;

    if (root == NULL) {
        return "no-response";
    }
    result = cJSON_GetObjectItem(root, "result");
    if (result == NULL || !cJSON_IsObject(result)) {
        return "no-result";
    }
    outcome = cJSON_GetObjectItem(result, "outcome");
    if (outcome == NULL || !cJSON_IsString(outcome)) {
        return "no-outcome";
    }
    return outcome->valuestring;
}

static const char *reason_of(cJSON *root)
{
    cJSON *result;
    cJSON *reason;

    if (root == NULL) {
        return "no-response";
    }
    result = cJSON_GetObjectItem(root, "result");
    if (result == NULL) {
        return "no-result";
    }
    reason = cJSON_GetObjectItem(result, "reason");
    if (reason == NULL || !cJSON_IsString(reason)) {
        return "";
    }
    return reason->valuestring;
}

static void test_grant_requires_active_sidecar(void)
{
    cJSON *root;

    reset_all();
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    CHECK(strcmp(g_last_kind, "success") == 0);
    {
        cJSON *status = cJSON_GetObjectItem(root, "status");
        cJSON *reason = cJSON_GetObjectItem(root, "reason");
        CHECK(status != NULL && cJSON_IsString(status));
        CHECK(reason != NULL && cJSON_IsString(reason));
        if (status != NULL && reason != NULL) {
            CHECK(strcmp(status->valuestring, "refused") == 0);
            CHECK(strcmp(reason->valuestring, "SIDECAR_NOT_ACTIVE") == 0);
        }
    }
    cJSON_Delete(root);
}

static void test_grant_issues_bound_identity(void)
{
    cJSON *root;
    cJSON *grant;
    cJSON *identity;
    cJSON *caps;
    cJSON *item;
    const char *build_id;
    const char *expected_keys[10] = {
        "publisher_id", "app_id", "publisher_key_sha256", "native_tbs_sha256",
        "ui_tbs_sha256", "build_id", "host_boot_generation", "native_session_generation",
        "ui_generation", "sidecar_service_generation"
    };
    int i;

    reset_all();
    install_active_sidecar();
    root = request_grant("[\"snapshot.read\",\"config.read\",\"config.apply\",\"counters.reset\"]");
    CHECK(root != NULL);
    grant = cJSON_GetObjectItem(root, "grant");
    CHECK(grant != NULL && cJSON_IsObject(grant));
    if (grant == NULL) {
        cJSON_Delete(root);
        return;
    }
    CHECK(cJSON_GetObjectItem(grant, "expires_at_ms") != NULL);
    item = cJSON_GetObjectItem(grant, "expires_at_ms");
    CHECK(item != NULL && cJSON_IsNumber(item));
    if (item != NULL && cJSON_IsNumber(item)) {
        CHECK(item->valuedouble == (double)(g_epoch_ms + 300000u));
    }
    identity = cJSON_GetObjectItem(grant, "identity");
    CHECK(identity != NULL && cJSON_IsObject(identity));
    if (identity != NULL) {
        CHECK(cJSON_GetArraySize(identity) == 10);
        for (i = 0; i < 10; i++) {
            CHECK(cJSON_GetObjectItem(identity, expected_keys[i]) != NULL);
        }
        item = cJSON_GetObjectItem(identity, "app_id");
        CHECK(item != NULL && cJSON_IsString(item));
        if (item != NULL && cJSON_IsString(item)) {
            CHECK(strcmp(item->valuestring, SC_VEC_APP_ID) == 0);
        }
        item = cJSON_GetObjectItem(identity, "build_id");
        CHECK(item != NULL && cJSON_IsString(item));
        if (item != NULL && cJSON_IsString(item)) {
            build_id = item->valuestring;
            CHECK(strcmp(build_id, "1.4.2") == 0);
        }
        item = cJSON_GetObjectItem(identity, "sidecar_service_generation");
        CHECK(item != NULL && cJSON_IsNumber(item));
        if (item != NULL && cJSON_IsNumber(item)) {
            CHECK(item->valuedouble >= 1.0);
        }
        item = cJSON_GetObjectItem(identity, "host_boot_generation");
        CHECK(item != NULL && cJSON_IsNumber(item));
        if (item != NULL && cJSON_IsNumber(item)) {
            CHECK(item->valuedouble >= 1.0);
        }
        item = cJSON_GetObjectItem(identity, "version_major");
        CHECK(item == NULL);
        item = cJSON_GetObjectItem(identity, "host_ui_abi");
        CHECK(item == NULL);
    }
    caps = cJSON_GetObjectItem(root, "capabilities");
    CHECK(caps != NULL && cJSON_IsArray(caps));
    if (caps != NULL) {
        CHECK(cJSON_GetArraySize(caps) == 4);
    }
    CHECK(strlen(g_grant_id) == 64);
    cJSON_Delete(root);
}

static void test_grant_rejects_unknown_capability(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    {
        char body[128];
        (void)snprintf(body, sizeof(body), "{\"capabilities\":[\"admin.grant\"]}");
        root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/grant", body,
                          strlen(body));
        CHECK(root == NULL);
        CHECK(strcmp(g_last_kind, "error") == 0);
    }
    reset_all();
}

static void test_grant_full_slots_refuse_without_revoking_active(void)
{
    int i;
    char grant_ids[5][65];
    cJSON *root;
    cJSON *status;
    cJSON *grant;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    for (i = 0; i < 4; i++) {
        g_epoch_ms += 1000u;
        root = request_grant("[\"snapshot.read\"]");
        CHECK(root != NULL);
        snprintf(grant_ids[i], sizeof(grant_ids[i]), "%s", g_grant_id);
        cJSON_Delete(root);
    }
    g_epoch_ms += 1000u;
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    status = cJSON_GetObjectItem(root, "status");
    grant = cJSON_GetObjectItem(root, "grant");
    CHECK(status != NULL);
    if (status != NULL) {
        CHECK(strcmp(status->valuestring, "refused") == 0);
    }
    CHECK(grant == NULL);
    cJSON_Delete(root);
    for (i = 0; i < 4; i++) {
        snprintf(g_grant_id, sizeof(g_grant_id), "%.64s", grant_ids[i]);
        root = bridge_call("snapshot.read", (uint32_t)(80 + i), NULL);
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
    g_epoch_ms += 130000u;
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    status = cJSON_GetObjectItem(root, "status");
    CHECK(status != NULL);
    if (status != NULL) {
        CHECK(strcmp(status->valuestring, "granted") == 0);
    }
    snprintf(grant_ids[4], sizeof(grant_ids[4]), "%s", g_grant_id);
    cJSON_Delete(root);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[0]);
    root = bridge_call("snapshot.read", 90, NULL);
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[3]);
    root = bridge_call("snapshot.read", 91, NULL);
    CHECK(root != NULL);
    cJSON_Delete(root);
    reset_all();
}

static void test_grant_reclaims_inactive_before_active(void)
{
    int i;
    char grant_ids[5][65];
    cJSON *root;
    cJSON *status;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    for (i = 0; i < 4; i++) {
        g_epoch_ms += 1000u;
        root = request_grant("[\"snapshot.read\"]");
        CHECK(root != NULL);
        snprintf(grant_ids[i], sizeof(grant_ids[i]), "%s", g_grant_id);
        cJSON_Delete(root);
    }
    for (i = 1; i < 4; i++) {
        snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[i]);
        root = bridge_call("snapshot.read", (uint32_t)(90 + i), NULL);
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
    g_epoch_ms += 130000u;
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    status = cJSON_GetObjectItem(root, "status");
    CHECK(status != NULL);
    if (status != NULL) {
        CHECK(strcmp(status->valuestring, "granted") == 0);
    }
    snprintf(grant_ids[4], sizeof(grant_ids[4]), "%s", g_grant_id);
    cJSON_Delete(root);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[0]);
    root = bridge_call("snapshot.read", 100, NULL);
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    for (i = 1; i < 4; i++) {
        snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[i]);
        root = bridge_call("snapshot.read", (uint32_t)(100 + i), NULL);
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
    reset_all();
}

static void test_report_service_endpoint(void)
{
    cJSON *root;
    cJSON *data;
    cJSON *stats;
    cJSON *queue;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = call_route("GET", API_PATH_PREFIX "/app/report/service", "{}", 2);
    CHECK(root != NULL);
    data = root;
    if (data != NULL) {
        stats = cJSON_GetObjectItem(data, "stats");
        queue = cJSON_GetObjectItem(data, "queue");
        CHECK(stats != NULL && cJSON_IsObject(stats));
        CHECK(queue != NULL && cJSON_IsArray(queue));
        if (stats != NULL) {
            CHECK(cJSON_GetObjectItem(stats, "accepted") != NULL);
            CHECK(cJSON_GetObjectItem(stats, "dedup_hits") != NULL);
            CHECK(cJSON_GetObjectItem(stats, "rejected_schema") != NULL);
            CHECK(cJSON_GetObjectItem(stats, "rejected_identity") != NULL);
            CHECK(cJSON_GetObjectItem(stats, "rejected_quota") != NULL);
        }
        CHECK(cJSON_GetObjectItem(data, "running") != NULL);
        CHECK(cJSON_GetObjectItem(data, "boot_id") != NULL);
    }
    cJSON_Delete(root);
    reset_all();
}

static void test_grant_slides_expiry_on_activity(void)
{
    cJSON *root;
    uint8_t snap[1024];

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    g_epoch_ms += 1000u;
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    build_snapshot(snap, "{\"counters\":{\"in\":3}}", 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
        snap,
        APP_CONTROL_HEADER_SIZE +
            (uint32_t)strlen("{\"counters\":{\"in\":3}}")) == APP_CONTROL_OK);

    g_epoch_ms += 250000u;
    root = bridge_call("snapshot.read", 200, NULL);
    CHECK(root != NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    cJSON_Delete(root);

    g_epoch_ms += 250000u;
    root = bridge_call("snapshot.read", 201, NULL);
    CHECK(root != NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    cJSON_Delete(root);

    g_epoch_ms += 310000u;
    root = bridge_call("snapshot.read", 202, NULL);
    CHECK(root != NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "GRANT_EXPIRED") == 0);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(root, "revoked")) == 1);
    cJSON_Delete(root);
    reset_all();
}

static void test_grant_expired_slot_preferred_over_evict(void)
{
    int i;
    char grant_ids[5][65];
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    for (i = 0; i < 4; i++) {
        g_epoch_ms += 1000u;
        root = request_grant("[\"snapshot.read\"]");
        CHECK(root != NULL);
        snprintf(grant_ids[i], sizeof(grant_ids[i]), "%s", g_grant_id);
        cJSON_Delete(root);
    }
    g_epoch_ms = 1000301500u;
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    {
        cJSON *status = cJSON_GetObjectItem(root, "status");
        cJSON *grant = cJSON_GetObjectItem(root, "grant");
        CHECK(status != NULL && grant != NULL);
        if (status != NULL) {
            CHECK(strcmp(status->valuestring, "granted") == 0);
        }
    }
    snprintf(grant_ids[4], sizeof(grant_ids[4]), "%s", g_grant_id);
    cJSON_Delete(root);
    for (i = 1; i < 4; i++) {
        snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[i]);
        root = bridge_call("snapshot.read", (uint32_t)(100 + i), NULL);
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[0]);
    root = bridge_call("snapshot.read", 121, NULL);
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    reset_all();
}

static void fill_grant_slots(char grant_ids[4][65])
{
    int i;
    cJSON *root;

    for (i = 0; i < 4; i++) {
        g_epoch_ms += 1000u;
        root = request_grant("[\"snapshot.read\"]");
        CHECK(root != NULL);
        snprintf(grant_ids[i], sizeof(grant_ids[i]), "%s", g_grant_id);
        cJSON_Delete(root);
    }
}

static void test_failed_grant_keeps_evictable_grant_alive(void)
{
    char grant_ids[4][65];
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    fill_grant_slots(grant_ids);
    g_epoch_ms += 130000u;
    root = request_grant_raw("[\"admin.grant\"]");
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    CHECK(g_last_error_code == (int)API_ERROR_INVALID_REQUEST);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[0]);
    root = bridge_call("snapshot.read", 120, NULL);
    CHECK(root != NULL);
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    fill_grant_slots(grant_ids);
    g_epoch_ms += 130000u;
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_TRUE);
    root = request_grant_raw("[\"snapshot.read\"]");
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    CHECK(g_last_error_code == (int)API_ERROR_SERVICE_UNAVAILABLE);
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_FALSE);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[0]);
    root = bridge_call("snapshot.read", 121, NULL);
    CHECK(root != NULL);
    cJSON_Delete(root);
    reset_all();
}

static void test_grant_valid_request_reclaims_inactive_slot(void)
{
    int i;
    char grant_ids[4][65];
    cJSON *root;
    cJSON *status;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    fill_grant_slots(grant_ids);
    for (i = 1; i < 4; i++) {
        snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[i]);
        root = bridge_call("snapshot.read", (uint32_t)(210 + i), NULL);
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
    g_epoch_ms += 130000u;
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    status = cJSON_GetObjectItem(root, "status");
    CHECK(status != NULL);
    if (status != NULL) {
        CHECK(strcmp(status->valuestring, "granted") == 0);
    }
    cJSON_Delete(root);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[0]);
    root = bridge_call("snapshot.read", 220, NULL);
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    snprintf(g_grant_id, sizeof(g_grant_id), "%s", grant_ids[3]);
    root = bridge_call("snapshot.read", 221, NULL);
    CHECK(root != NULL);
    cJSON_Delete(root);
    reset_all();
}

static void test_call_rejects_unknown_grant_and_op(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    {
        char body[256];
        (void)snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"config.read\",\"request_id\":7}",
                       "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge", body, strlen(body));
        CHECK(root == NULL);
        CHECK(strcmp(g_last_kind, "error") == 0);
    }
    root = bridge_call("enable.apply", 8, NULL);
    CHECK(outcome_of(root) != NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "OP_NOT_IN_GRANT") == 0);
    cJSON_Delete(root);
    reset_all();
}

static void test_unavailable_ops(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    root = request_grant("[\"report-policy.read\",\"report-policy.apply\",\"video.open\",\"video.close\"]");
    cJSON_Delete(root);
    root = bridge_call("report-policy.read", 11, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "REPORT_POLICY_STORAGE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    root = bridge_call("report-policy.apply", 12, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    root = bridge_call("video.open", 13, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    root = bridge_call("video.close", 14, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    reset_all();
}

static int policy_config_bool(cJSON *owner, const char *name, int *out)
{
    cJSON *config;
    cJSON *item;

    if (owner == NULL || !cJSON_IsObject(owner)) {
        return 0;
    }
    config = cJSON_GetObjectItem(owner, "config");
    if (config == NULL || !cJSON_IsObject(config)) {
        return 0;
    }
    item = cJSON_GetObjectItem(config, name);
    if (item == NULL || !cJSON_IsBool(item)) {
        return 0;
    }
    *out = cJSON_IsTrue(item) ? 1 : 0;
    return 1;
}

static int policy_config_int(cJSON *owner, const char *name, int *out)
{
    cJSON *config;
    cJSON *item;

    if (owner == NULL || !cJSON_IsObject(owner)) {
        return 0;
    }
    config = cJSON_GetObjectItem(owner, "config");
    if (config == NULL || !cJSON_IsObject(config)) {
        return 0;
    }
    item = cJSON_GetObjectItem(config, name);
    if (item == NULL || !cJSON_IsNumber(item)) {
        return 0;
    }
    *out = item->valueint;
    return 1;
}

static void test_report_policy_bridge_paths(void)
{
    cJSON *root;
    cJSON *result;
    cJSON *payload;
    cJSON *snapshot;
    cJSON *field;
    char extra[512];
    int capacity;
    int flag;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    policy_storage_attach();
    root = request_grant("[\"report-policy.read\",\"report-policy.apply\"]");
    cJSON_Delete(root);

    root = bridge_call("report-policy.read", 201, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    result = cJSON_GetObjectItem(root, "result");
    payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
    CHECK(payload != NULL);
    if (payload != NULL && cJSON_IsObject(payload)) {
        field = cJSON_GetObjectItem(payload, "session_generation");
        CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == (int)g_ctl_gen);
        field = cJSON_GetObjectItem(payload, "generation");
        CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 0);
        CHECK(cJSON_GetObjectItem(payload, "snapshot") == NULL);
        CHECK(policy_config_bool(payload, "mqtt_enabled", &flag) == 1 && flag == 1);
        CHECK(policy_config_bool(payload, "webhook_enabled", &flag) == 1 && flag == 1);
        CHECK(policy_config_int(payload, "backlog_capacity", &capacity) == 1 &&
              capacity == (int)APP_REPORT_POLICY_DEFAULT_CAPACITY);
    }
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":0,"
                   "\"candidate\":{\"mqtt_enabled\":false,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":3}}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 202, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    result = cJSON_GetObjectItem(root, "result");
    payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
    CHECK(payload != NULL);
    if (payload != NULL && cJSON_IsObject(payload)) {
        field = cJSON_GetObjectItem(payload, "session_generation");
        CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == (int)g_ctl_gen);
        field = cJSON_GetObjectItem(payload, "generation");
        CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 1);
        snapshot = cJSON_GetObjectItem(payload, "snapshot");
        CHECK(snapshot != NULL && cJSON_IsObject(snapshot));
        if (snapshot != NULL) {
            field = cJSON_GetObjectItem(snapshot, "session_generation");
            CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == (int)g_ctl_gen);
            field = cJSON_GetObjectItem(snapshot, "generation");
            CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 1);
        }
        CHECK(policy_config_bool(snapshot, "mqtt_enabled", &flag) == 1 && flag == 0);
        CHECK(policy_config_bool(snapshot, "webhook_enabled", &flag) == 1 && flag == 1);
        CHECK(policy_config_int(snapshot, "backlog_capacity", &capacity) == 1 && capacity == 3);
    }
    cJSON_Delete(root);

    root = bridge_call("report-policy.read", 203, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    result = cJSON_GetObjectItem(root, "result");
    payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
    CHECK(payload != NULL);
    if (payload != NULL && cJSON_IsObject(payload)) {
        field = cJSON_GetObjectItem(payload, "generation");
        CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 1);
        CHECK(policy_config_bool(payload, "mqtt_enabled", &flag) == 1 && flag == 0);
        CHECK(policy_config_bool(payload, "webhook_enabled", &flag) == 1 && flag == 1);
        CHECK(policy_config_int(payload, "backlog_capacity", &capacity) == 1 && capacity == 3);
    }
    cJSON_Delete(root);

    root = bridge_call("report-policy.apply", 204, extra);
    CHECK(strcmp(outcome_of(root), "CONFLICT") == 0);
    CHECK(strcmp(reason_of(root), "POLICY_REVISION_MISMATCH") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1,"
                   "\"candidate\":{\"mqtt_enabled\":false,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":3}}",
                   (unsigned int)(g_ctl_gen + 7u));
    root = bridge_call("report-policy.apply", 205, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STALE_SESSION_GENERATION") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,"
                   "\"candidate\":{\"mqtt_enabled\":false,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":3}}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 206, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "POLICY_GENERATION_REQUIRED") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 207, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CANDIDATE_REQUIRED") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"expected_generation\":1,"
                   "\"candidate\":{\"mqtt_enabled\":false,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":3}}");
    root = bridge_call("report-policy.apply", 208, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "SESSION_GENERATION_REQUIRED") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1,"
                   "\"candidate\":{\"webhook_enabled\":true,\"backlog_capacity\":3}}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 209, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CANDIDATE_INVALID") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1,"
                   "\"candidate\":{\"mqtt_enabled\":1,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":3}}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 210, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CANDIDATE_INVALID") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1,"
                   "\"candidate\":{\"mqtt_enabled\":false,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":0}}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 211, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CANDIDATE_INVALID") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1,"
                   "\"candidate\":{\"mqtt_enabled\":false,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":%u}}",
                   (unsigned int)g_ctl_gen,
                   (unsigned int)(APP_REPORT_POLICY_MAX_CAPACITY + 1u));
    root = bridge_call("report-policy.apply", 212, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CANDIDATE_INVALID") == 0);
    cJSON_Delete(root);

    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":1,"
                   "\"candidate\":{\"mqtt_enabled\":true,\"webhook_enabled\":false,"
                   "\"backlog_capacity\":%u}}",
                   (unsigned int)g_ctl_gen, (unsigned int)APP_REPORT_POLICY_MAX_CAPACITY);
    root = bridge_call("report-policy.apply", 213, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    result = cJSON_GetObjectItem(root, "result");
    payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
    CHECK(payload != NULL);
    if (payload != NULL && cJSON_IsObject(payload)) {
        field = cJSON_GetObjectItem(payload, "generation");
        CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 2);
    }
    cJSON_Delete(root);

    root = bridge_call("report-policy.apply", 214, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);

    policy_storage_detach();
    root = bridge_call("report-policy.read", 215, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "REPORT_POLICY_STORAGE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"expected_generation\":2,"
                   "\"candidate\":{\"mqtt_enabled\":true,\"webhook_enabled\":true,"
                   "\"backlog_capacity\":3}}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("report-policy.apply", 216, extra);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "REPORT_POLICY_STORAGE_UNAVAILABLE") == 0);
    cJSON_Delete(root);

    policy_storage_attach();
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    root = bridge_call("report-policy.read", 217, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "OP_NOT_IN_GRANT") == 0);
    cJSON_Delete(root);
    reset_all();
}

static void test_video_open_close_paths(void)
{
    cJSON *root;
    cJSON *result;
    cJSON *payload;
    char extra[512];
    struct mg_http_message *fake_msg;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"video.open\",\"video.close\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    app_bridge_video_lan_host_set("10.0.0.42");

    root = bridge_call("video.open", 81, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)(g_ctl_gen + 7u));
    root = bridge_call("video.open", 82, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STALE_SESSION_GENERATION") == 0);
    cJSON_Delete(root);
    g_ws_running = 0;
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("video.open", 83, extra);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "VIDEO_SERVICE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    g_ws_running = 1;
    g_ws_stream_active = 0;
    root = bridge_call("video.open", 84, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(g_ws_start_calls == 1);
    CHECK(g_ws_start_last_id == 1u);
    cJSON_Delete(root);
    g_ws_stream_active = 1;
    app_bridge_video_lan_host_set(NULL);
    root = bridge_call("video.open", 85, extra);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "VIDEO_ENDPOINT_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    app_bridge_video_lan_host_set("bad host/with junk");
    root = bridge_call("video.open", 86, extra);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "VIDEO_ENDPOINT_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    app_bridge_video_lan_host_set("10.0.0.42");
    root = bridge_call("video.open", 87, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    result = cJSON_GetObjectItem(root, "result");
    payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
    CHECK(payload != NULL && cJSON_IsObject(payload));
    if (payload != NULL) {
        cJSON *url = cJSON_GetObjectItem(payload, "url");
        cJSON *session = cJSON_GetObjectItem(payload, "session_generation");
        cJSON *expires = cJSON_GetObjectItem(payload, "expires_at_ms");
        cJSON *grant_id = cJSON_GetObjectItem(payload, "grant_id");
        CHECK(cJSON_GetArraySize(payload) == 4);
        CHECK(url != NULL && cJSON_IsString(url));
        if (url != NULL && cJSON_IsString(url)) {
            CHECK(strcmp(url->valuestring, "ws://10.0.0.42:8081/stream") == 0);
        }
        CHECK(session != NULL && cJSON_IsNumber(session) && session->valueint == (int)g_ctl_gen);
        CHECK(expires != NULL && cJSON_IsNumber(expires));
        if (expires != NULL && cJSON_IsNumber(expires)) {
            CHECK(expires->valuedouble == (double)(g_epoch_ms + 300000u));
        }
        CHECK(grant_id != NULL && cJSON_IsString(grant_id));
        if (grant_id != NULL && cJSON_IsString(grant_id)) {
            CHECK(strcmp(grant_id->valuestring, g_grant_id) == 0);
        }
    }
    cJSON_Delete(root);
    root = bridge_call("video.close", 88, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("video.close", 89, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "GRANT_MISMATCH") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"grant_id\":\"%s\"}",
                   (unsigned int)g_ctl_gen, "0000000000000000000000000000000"
                   "00000000000000000000000000000000");
    root = bridge_call("video.close", 90, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "GRANT_MISMATCH") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("video.close", 91, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "GRANT_MISMATCH") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"grant_id\":\"%s\"}",
                   (unsigned int)g_ctl_gen, g_grant_id);
    root = bridge_call("video.close", 92, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *session;
        result = cJSON_GetObjectItem(root, "result");
        payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            CHECK(cJSON_GetArraySize(payload) == 1);
            session = cJSON_GetObjectItem(payload, "session_generation");
            CHECK(session != NULL && cJSON_IsNumber(session) &&
                  session->valueint == (int)g_ctl_gen);
        }
    }
    cJSON_Delete(root);
    root = bridge_call("video.close", 93, extra);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "VIDEO_SESSION_NOT_OPEN") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("video.open", 94, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    cJSON_Delete(root);
    (void)snprintf(extra, sizeof(extra),
                   "\"payload\":{\"session_generation\":%u,\"grant_id\":\"%s\"}",
                   (unsigned int)g_ctl_gen, g_grant_id);
    root = bridge_call("video.close", 95, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    cJSON_Delete(root);
    reset_all();

    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"video.open\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    fake_msg = fake_msg_with_host("192.168.7.55:80");
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call_via_msg("video.open", 96, extra, fake_msg);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *url;
        result = cJSON_GetObjectItem(root, "result");
        payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
        url = (payload != NULL) ? cJSON_GetObjectItem(payload, "url") : NULL;
        CHECK(url != NULL && cJSON_IsString(url));
        if (url != NULL && cJSON_IsString(url)) {
            CHECK(strcmp(url->valuestring, "ws://192.168.7.55:8081/stream") == 0);
        }
    }
    cJSON_Delete(root);
    fake_msg = fake_msg_with_host("[fd00::7]:8080");
    root = bridge_call("video.open", 97, extra);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "VIDEO_ENDPOINT_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    root = bridge_call_via_msg("video.open", 98, extra, fake_msg);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *url;
        result = cJSON_GetObjectItem(root, "result");
        payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
        url = (payload != NULL) ? cJSON_GetObjectItem(payload, "url") : NULL;
        CHECK(url != NULL && cJSON_IsString(url));
        if (url != NULL && cJSON_IsString(url)) {
            CHECK(strcmp(url->valuestring, "ws://[fd00::7]:8081/stream") == 0);
        }
    }
    cJSON_Delete(root);
    reset_all();
}

static void test_video_open_starts_inactive_stream(void)
{
    cJSON *root;
    char extra[512];

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"video.open\",\"video.close\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    app_bridge_video_lan_host_set("10.0.0.42");
    (void)snprintf(extra, sizeof(extra), "\"payload\":{\"session_generation\":%u}",
                   (unsigned int)g_ctl_gen);
    g_ws_stream_active = 0;
    g_ws_start_fail = 1;
    root = bridge_call("video.open", 120, extra);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "VIDEO_SOURCE_UNAVAILABLE") == 0);
    CHECK(g_ws_start_calls == 1);
    cJSON_Delete(root);
    g_ws_start_fail = 0;
    root = bridge_call("video.open", 121, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(g_ws_start_calls == 2);
    CHECK(g_ws_start_last_id == 1u);
    {
        cJSON *url = NULL;
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
        if (payload != NULL) {
            url = cJSON_GetObjectItem(payload, "url");
        }
        CHECK(url != NULL && cJSON_IsString(url));
        if (url != NULL && cJSON_IsString(url)) {
            CHECK(strcmp(url->valuestring, "ws://10.0.0.42:8081/stream") == 0);
        }
    }
    cJSON_Delete(root);
    root = bridge_call("video.open", 122, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(g_ws_start_calls == 2);
    cJSON_Delete(root);
    reset_all();
}

static void test_snapshot_read_states(void)
{
    cJSON *root;
    uint8_t snap[1024];

    reset_all();
    install_active_sidecar();
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    root = bridge_call("snapshot.read", 21, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "SNAPSHOT_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    build_snapshot(snap, "{\"counters\":{\"in\":3}}", 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
        snap,
        APP_CONTROL_HEADER_SIZE +
            (uint32_t)strlen("{\"counters\":{\"in\":3}}")) == APP_CONTROL_OK);
    root = bridge_call("snapshot.read", 22, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *payload;
        cJSON *freshness;
        CHECK(result != NULL);
        payload = cJSON_GetObjectItem(result, "payload");
        freshness = cJSON_GetObjectItem(result, "freshness");
        CHECK(payload != NULL && freshness != NULL);
        if (payload != NULL && freshness != NULL) {
            CHECK(strcmp(freshness->valuestring, "fresh") == 0);
            CHECK(cJSON_GetObjectItem(payload, "counters") != NULL);
        }
    }
    cJSON_Delete(root);
    reset_all();
}

static void test_config_read_states(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    root = request_grant("[\"config.read\"]");
    cJSON_Delete(root);
    root = bridge_call("config.read", 31, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "SESSION_NOT_ACTIVE") == 0);
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.read\"]");
    cJSON_Delete(root);
    {
        uint8_t snap[512];
        build_snapshot(snap, "{\"line\":{\"y\":128}}", 6u, 5u, 0u, 0u);
        CHECK(app_host_control_snapshot_submit(snap, APP_CONTROL_HEADER_SIZE +
                                               (uint32_t)strlen("{\"line\":{\"y\":128}}")) ==
              APP_CONTROL_OK);
    }
    root = bridge_call("config.read", 32, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *gen;
        CHECK(result != NULL);
        gen = cJSON_GetObjectItem(result, "config_generation");
        CHECK(gen != NULL && cJSON_IsNumber(gen) && gen->valueint == 6);
    }
    cJSON_Delete(root);
    reset_all();
}

#define LC_CANDIDATE_FMT(y1) \
    "{\"schema\":1,\"counter_name\":\"Counter One\",\"target_class_name\":\"person\"," \
    "\"line\":{\"x1\":250,\"y1\":" #y1 ",\"x2\":750,\"y2\":500,\"outside_x\":500," \
    "\"outside_y\":350},\"conf_threshold_permille\":550,\"max_dist_permille\":350," \
    "\"track_history_k\":8,\"max_miss\":12,\"k_confirm\":2,\"tracks_report_enable\":true," \
    "\"heat_grid_enable\":false,\"window_minutes\":60}"

static void test_config_apply_paths(void)
{
    cJSON *root;
    char body[1024];
    int len;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    g_sleep_hook = native_apply_hook;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(128) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 41, extra);
    }
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *gen;
        cJSON *rev;
        CHECK(result != NULL);
        gen = cJSON_GetObjectItem(result, "config_generation");
        rev = cJSON_GetObjectItem(result, "state_revision");
        CHECK(gen != NULL && cJSON_IsNumber(gen) && gen->valueint == 6);
        CHECK(rev != NULL && cJSON_IsNumber(rev) && rev->valueint == 5);
    }
    cJSON_Delete(root);
    g_sleep_hook = native_stale_hook;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(130) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 42, extra);
    }
    CHECK(strcmp(outcome_of(root), "CONFLICT") == 0);
    CHECK(strcmp(reason_of(root), "STALE_CONFIG_GENERATION") == 0);
    cJSON_Delete(root);
    g_sleep_hook = native_noop_hook;
    g_now_step = 600u;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(131) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 43, extra);
    }
    CHECK(strcmp(outcome_of(root), "UNKNOWN") == 0);
    CHECK(strcmp(reason_of(root), "ACK_TIMEOUT") == 0);
    cJSON_Delete(root);
    g_now_step = 0u;
    g_sleep_hook = NULL;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"payload\":{\"session_generation\":%u,\"candidate\":" LC_CANDIDATE_FMT(132) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 44, extra);
    }
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CONFIG_GENERATION_REQUIRED") == 0);
    cJSON_Delete(root);
    {
        char extra[512];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 45, extra);
    }
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CANDIDATE_REQUIRED") == 0);
    cJSON_Delete(root);
    len = snprintf_bridge_write_body(body, sizeof(body), "config.apply", 46, 999u,
                                     "{\"line\":{\"y\":133}}", NULL);
    CHECK(len > 0);
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STALE_SESSION_GENERATION") == 0);
    cJSON_Delete(root);
    root = bridge_call("config.apply", 47, "\"expected_config_generation\":6");
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    reset_all();
}

static const char APPLY_SNAPSHOT_JSON[] =
    "{\"schema\":\"line-crossing/1\",\"config_generation\":6,"
    "\"enable_state\":\"enabled\",\"app_state\":\"running\","
    "\"active_config\":{\"counter_name\":\"Counter One\","
    "\"target_class_name\":\"person\"}}";

static void native_apply_snapshot_hook(uint32_t ms)
{
    uint8_t snap[512];

    (void)ms;
    native_receipt_hook(APP_CONTROL_RESULT_APPLIED);
    build_snapshot(snap, APPLY_SNAPSHOT_JSON, 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
              snap,
              APP_CONTROL_HEADER_SIZE +
                  (uint32_t)strlen(APPLY_SNAPSHOT_JSON)) == APP_CONTROL_OK);
}

static void test_config_apply_applied_result_echoes_reconciliation_payload(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    g_sleep_hook = native_apply_snapshot_hook;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(128) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 51, extra);
    }
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *payload;
        cJSON *field;
        cJSON *snapshot;
        CHECK(result != NULL);
        payload = cJSON_GetObjectItem(result, "payload");
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            field = cJSON_GetObjectItem(payload, "session_generation");
            CHECK(field != NULL && cJSON_IsNumber(field) &&
                  field->valueint == (int)g_ctl_gen);
            field = cJSON_GetObjectItem(payload, "config_generation");
            CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 6);
            snapshot = cJSON_GetObjectItem(payload, "snapshot");
            CHECK(snapshot != NULL && cJSON_IsObject(snapshot));
            if (snapshot != NULL) {
                field = cJSON_GetObjectItem(snapshot, "config_generation");
                CHECK(field != NULL && cJSON_IsNumber(field) && field->valueint == 6);
                field = cJSON_GetObjectItem(snapshot, "schema");
                CHECK(field != NULL && cJSON_IsString(field) &&
                      strcmp(field->valuestring, "line-crossing/1") == 0);
            }
        }
    }
    cJSON_Delete(root);
    g_sleep_hook = NULL;
    reset_all();
}

static void native_apply_capture_hook(uint32_t ms)
{
    uint8_t rx[600];
    uint32_t rx_len = 0u;
    uint32_t id;

    (void)ms;
    if (app_host_control_deliver(rx, sizeof(rx), &rx_len) != APP_CONTROL_OK) {
        return;
    }
    if (rx_len > APP_CONTROL_HEADER_SIZE) {
        g_last_applied_blob_len = rx_len - APP_CONTROL_HEADER_SIZE;
        memcpy(g_last_applied_blob, rx + APP_CONTROL_HEADER_SIZE,
               g_last_applied_blob_len);
    }
    id = (uint32_t)rx[16] | ((uint32_t)rx[17] << 8) | ((uint32_t)rx[18] << 16) |
         ((uint32_t)rx[19] << 24);
    build_receipt(rx, g_ctl_gen, id, 6u, 5u, APP_CONTROL_RESULT_APPLIED);
    app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE);
}

static void test_config_apply_writes_through_host_confidence(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    g_sleep_hook = native_apply_capture_hook;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(128) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 61, extra);
    }
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    cJSON_Delete(root);
    g_sleep_hook = NULL;
    CHECK(g_ai_confidence_set_calls == 1u);
    CHECK(g_ai_confidence_last_written == 55u);
    CHECK(g_last_applied_blob_len == 144u);
    CHECK((uint32_t)(g_last_applied_blob[126] |
                     ((uint32_t)g_last_applied_blob[127] << 8)) == 550u);
    reset_all();
}

static void test_config_apply_native_failures_keep_host_confidence(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    g_sleep_hook = native_stale_hook;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(140) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 63, extra);
    }
    CHECK(strcmp(outcome_of(root), "CONFLICT") == 0);
    CHECK(strcmp(reason_of(root), "STALE_CONFIG_GENERATION") == 0);
    cJSON_Delete(root);
    CHECK(g_ai_confidence_set_calls == 0u);
    CHECK(g_ai_confidence_threshold == 50u);
    g_sleep_hook = native_noop_hook;
    g_now_step = 600u;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(141) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 64, extra);
    }
    CHECK(strcmp(outcome_of(root), "UNKNOWN") == 0);
    CHECK(strcmp(reason_of(root), "ACK_TIMEOUT") == 0);
    cJSON_Delete(root);
    CHECK(g_ai_confidence_set_calls == 0u);
    CHECK(g_ai_confidence_threshold == 50u);
    g_now_step = 0u;
    g_sleep_hook = NULL;
    reset_all();
}

static void test_config_apply_cached_replay_does_not_rewrite_host_confidence(void)
{
    cJSON *root;
    char extra[640];

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    g_sleep_hook = native_apply_hook;
    (void)snprintf(extra, sizeof(extra),
                   "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                   "\"candidate\":" LC_CANDIDATE_FMT(128) "}",
                   (unsigned int)g_ctl_gen);
    root = bridge_call("config.apply", 65, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *synced = (result != NULL)
                            ? cJSON_GetObjectItem(result, "host_threshold_synced")
                            : NULL;
        CHECK(synced != NULL && cJSON_IsBool(synced) && cJSON_IsTrue(synced));
    }
    cJSON_Delete(root);
    CHECK(g_ai_confidence_set_calls == 1u);
    g_sleep_hook = NULL;
    root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/revoke",
                      "{\"all\":true}", 11);
    cJSON_Delete(root);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    root = bridge_call("config.apply", 65, extra);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        CHECK(result == NULL ||
              cJSON_GetObjectItem(result, "host_threshold_synced") == NULL);
    }
    cJSON_Delete(root);
    CHECK(g_ai_confidence_set_calls == 1u);
    CHECK(g_ai_confidence_last_written == 55u);
    reset_all();
}

static void test_config_apply_host_sync_failure_is_reported(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"config.apply\"]");
    cJSON_Delete(root);
    g_sleep_hook = native_apply_hook;
    g_ai_confidence_set_fail = AICAM_TRUE;
    {
        char extra[640];
        (void)snprintf(extra, sizeof(extra),
                       "\"expected_config_generation\":6,\"payload\":{\"session_generation\":%u,"
                       "\"candidate\":" LC_CANDIDATE_FMT(128) "}",
                       (unsigned int)g_ctl_gen);
        root = bridge_call("config.apply", 66, extra);
    }
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *synced = (result != NULL)
                            ? cJSON_GetObjectItem(result, "host_threshold_synced")
                            : NULL;
        CHECK(synced != NULL && cJSON_IsBool(synced) && !cJSON_IsTrue(synced));
    }
    cJSON_Delete(root);
    CHECK(g_ai_confidence_set_calls == 1u);
    CHECK(g_ai_confidence_threshold == 50u);
    g_sleep_hook = NULL;
    reset_all();
}

static const char LC_CONF_SNAPSHOT_JSON[] =
    "{\"schema\":\"line-crossing/1\",\"config_generation\":6,"
    "\"enable_state\":\"enabled\",\"app_state\":\"running\","
    "\"active_config\":{\"counter_name\":\"Counter One\","
    "\"target_class_name\":\"person\",\"line\":{\"x1_permille\":250,"
    "\"y1_permille\":128,\"x2_permille\":750,\"y2_permille\":500},"
    "\"outside\":{\"x_permille\":500,\"y_permille\":350},"
    "\"conf_threshold_permille\":550,\"max_dist_permille\":350,"
    "\"track_history_k\":8,\"max_miss\":12,\"k_confirm\":2,"
    "\"window_minutes\":60,\"tracks_report_enable\":true,"
    "\"heat_grid_enable\":false}}";

static uint32_t mirror_deliver_and_confirm(uint32_t *conf_out, uint32_t *rid_out,
                                           uint32_t *expected_gen_out)
{
    uint8_t rx[600];
    uint32_t rx_len = 0u;
    uint32_t id;
    uint32_t expected_gen;
    int32_t rc;

    rc = app_host_control_deliver(rx, sizeof(rx), &rx_len);
    if (rc != APP_CONTROL_OK) {
        return (uint32_t)rc;
    }
    id = (uint32_t)rx[16] | ((uint32_t)rx[17] << 8) | ((uint32_t)rx[18] << 16) |
         ((uint32_t)rx[19] << 24);
    expected_gen = (uint32_t)rx[20] | ((uint32_t)rx[21] << 8) |
                   ((uint32_t)rx[22] << 16) | ((uint32_t)rx[23] << 24);
    if (conf_out != NULL) {
        *conf_out = (uint32_t)(rx[APP_CONTROL_HEADER_SIZE + 126u] |
                               ((uint32_t)rx[APP_CONTROL_HEADER_SIZE + 127u] << 8));
    }
    if (rid_out != NULL) {
        *rid_out = id;
    }
    if (expected_gen_out != NULL) {
        *expected_gen_out = expected_gen;
    }
    build_receipt(rx, g_ctl_gen, id, expected_gen, 5u, APP_CONTROL_RESULT_APPLIED);
    app_host_control_receipt(rx, APP_CONTROL_HEADER_SIZE);
    return (uint32_t)APP_CONTROL_OK;
}

static uint32_t snapshot_conf_of(cJSON *root)
{
    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *payload;
    cJSON *active;
    cJSON *conf;

    if (result == NULL) {
        return 0xFFFFFFFFu;
    }
    payload = cJSON_GetObjectItem(result, "payload");
    if (payload == NULL) {
        return 0xFFFFFFFFu;
    }
    active = cJSON_GetObjectItem(payload, "active_config");
    if (active == NULL) {
        return 0xFFFFFFFFu;
    }
    conf = cJSON_GetObjectItem(active, "conf_threshold_permille");
    if (conf == NULL || !cJSON_IsNumber(conf)) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)conf->valueint;
}

static void test_snapshot_read_serves_host_confidence_and_mirrors_blob(void)
{
    cJSON *root;
    uint8_t snap[768];
    uint32_t mirror_conf = 0u;
    uint32_t mirror_rid = 0u;
    uint32_t mirror_expected_gen = 0u;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    build_snapshot(snap, LC_CONF_SNAPSHOT_JSON, 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
              snap, APP_CONTROL_HEADER_SIZE +
                        (uint32_t)strlen(LC_CONF_SNAPSHOT_JSON)) == APP_CONTROL_OK);
    g_ai_confidence_threshold = 40u;
    root = bridge_call("snapshot.read", 71, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(snapshot_conf_of(root) == 400u);
    cJSON_Delete(root);
    CHECK(mirror_deliver_and_confirm(&mirror_conf, &mirror_rid,
                                     &mirror_expected_gen) ==
          (uint32_t)APP_CONTROL_OK);
    CHECK(mirror_conf == 400u);
    CHECK(mirror_expected_gen == 6u);
    CHECK(mirror_rid >= 0x88000001u);
    reset_all();
}

static void test_snapshot_read_tracks_host_confidence_change(void)
{
    cJSON *root;
    uint8_t snap[768];
    uint32_t mirror_conf = 0u;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    build_snapshot(snap, LC_CONF_SNAPSHOT_JSON, 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
              snap, APP_CONTROL_HEADER_SIZE +
                        (uint32_t)strlen(LC_CONF_SNAPSHOT_JSON)) == APP_CONTROL_OK);
    g_ai_confidence_threshold = 55u;
    root = bridge_call("snapshot.read", 81, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(snapshot_conf_of(root) == 550u);
    cJSON_Delete(root);
    {
        uint8_t rx[600];
        uint32_t rx_len = 0u;

        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) != APP_CONTROL_OK);
    }
    g_ai_confidence_threshold = 33u;
    root = bridge_call("snapshot.read", 82, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(snapshot_conf_of(root) == 330u);
    cJSON_Delete(root);
    CHECK(mirror_deliver_and_confirm(&mirror_conf, NULL, NULL) ==
          (uint32_t)APP_CONTROL_OK);
    CHECK(mirror_conf == 330u);
    reset_all();
}

static const char LC_MIRROR_FIELDS_JSON[] =
    "{\"schema\":\"line-crossing/1\",\"config_generation\":6,"
    "\"enable_state\":\"enabled\",\"app_state\":\"running\","
    "\"active_config\":{\"counter_name\":\"South Gate Line\","
    "\"target_class_name\":\"car\","
    "\"line\":{\"x1_permille\":111,\"y1_permille\":222,\"x2_permille\":333,"
    "\"y2_permille\":444},\"outside\":{\"x_permille\":555,\"y_permille\":66},"
    "\"conf_threshold_permille\":770,\"max_dist_permille\":432,"
    "\"track_history_k\":9,\"max_miss\":34,\"k_confirm\":5,"
    "\"window_minutes\":777,\"tracks_report_enable\":false,"
    "\"heat_grid_enable\":true}}";

static uint8_t g_mirror_rx[600];
static uint32_t g_mirror_rx_len;

static void mirror_capture_and_confirm(void)
{
    uint32_t id;
    uint32_t expected_gen;

    CHECK(app_host_control_deliver(g_mirror_rx, sizeof(g_mirror_rx),
                                   &g_mirror_rx_len) == APP_CONTROL_OK);
    id = (uint32_t)g_mirror_rx[16] | ((uint32_t)g_mirror_rx[17] << 8) |
         ((uint32_t)g_mirror_rx[18] << 16) | ((uint32_t)g_mirror_rx[19] << 24);
    expected_gen = (uint32_t)g_mirror_rx[20] | ((uint32_t)g_mirror_rx[21] << 8) |
                   ((uint32_t)g_mirror_rx[22] << 16) |
                   ((uint32_t)g_mirror_rx[23] << 24);
    build_receipt(g_mirror_rx, g_ctl_gen, id, expected_gen, 5u,
                  APP_CONTROL_RESULT_APPLIED);
    app_host_control_receipt(g_mirror_rx, APP_CONTROL_HEADER_SIZE);
}

static void test_snapshot_mirror_preserves_active_config_bytes(void)
{
    cJSON *root;
    uint8_t snap[768];
    uint8_t expect[144];
    const uint8_t *blob;
    uint32_t user_id;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    build_snapshot(snap, LC_MIRROR_FIELDS_JSON, 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
              snap, APP_CONTROL_HEADER_SIZE +
                        (uint32_t)strlen(LC_MIRROR_FIELDS_JSON)) == APP_CONTROL_OK);
    g_ai_confidence_threshold = 40u;
    root = bridge_call("snapshot.read", 72, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    CHECK(snapshot_conf_of(root) == 400u);
    cJSON_Delete(root);
    g_mirror_rx_len = 0u;
    mirror_capture_and_confirm();
    CHECK(g_mirror_rx_len == APP_CONTROL_HEADER_SIZE + 144u);
    blob = g_mirror_rx + APP_CONTROL_HEADER_SIZE;
    memset(expect, 0, sizeof(expect));
    expect[0] = 0x4Cu; expect[1] = 0x43u; expect[2] = 0x43u; expect[3] = 0x33u;
    expect[4] = 1u;
    expect[8] = 144u;
    expect[16] = 3u;
    memcpy(expect + 17u, "car", 3u);
    expect[49] = 15u;
    memcpy(expect + 50u, "South Gate Line", 15u);
    expect[114] = 111u; expect[115] = 0u;
    expect[116] = 222u; expect[117] = 0u;
    expect[118] = 0x4Du; expect[119] = 1u;
    expect[120] = 188u; expect[121] = 1u;
    expect[122] = 43u; expect[123] = 2u;
    expect[124] = 66u; expect[125] = 0u;
    expect[126] = 144u; expect[127] = 1u;
    expect[128] = 176u; expect[129] = 1u;
    expect[130] = 9u;
    expect[131] = 34u;
    expect[132] = 5u;
    expect[133] = 0x02u;
    expect[134] = 9u; expect[135] = 3u;
    CHECK(memcmp(blob, expect, sizeof(expect)) == 0);
    {
        app_control_request_t ureq;
        uint8_t ublob[144];
        uint8_t rx[600];
        uint32_t rx_len = 0u;

        memcpy(ublob, blob, sizeof(ublob));
        memset(&ureq, 0, sizeof(ureq));
        ureq.request_id = 4242u;
        ureq.msg_type = APP_CONTROL_MSG_TYPE_CONFIG_APPLY;
        ureq.payload = ublob;
        ureq.payload_len = sizeof(ublob);
        ureq.expected_config_generation = 6u;
        CHECK(app_host_control_register(&ureq) == APP_CONTROL_REG_OK);
        build_snapshot(snap, LC_MIRROR_FIELDS_JSON, 6u, 5u, 0u, 0u);
        CHECK(app_host_control_snapshot_submit(
                  snap, APP_CONTROL_HEADER_SIZE +
                            (uint32_t)strlen(LC_MIRROR_FIELDS_JSON)) ==
              APP_CONTROL_OK);
        root = bridge_call("snapshot.read", 73, NULL);
        CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
        cJSON_Delete(root);
        CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
        user_id = (uint32_t)rx[16] | ((uint32_t)rx[17] << 8) |
                  ((uint32_t)rx[18] << 16) | ((uint32_t)rx[19] << 24);
        CHECK(user_id == 4242u);
        CHECK(rx_len == APP_CONTROL_HEADER_SIZE + 144u);
    }
    reset_all();
}

static long g_hook_live;
static unsigned long g_hook_allocs;

static void live_backend_init(void);
static void live_bind_grant_identity(uint32_t generation, uint32_t track_max);

static void *hook_track_malloc(size_t size)
{
    void *p = malloc(size);

    if (p != NULL) {
        g_hook_live++;
        g_hook_allocs++;
    }
    return p;
}

static void hook_track_free(void *ptr)
{
    if (ptr != NULL) {
        g_hook_live--;
        free(ptr);
    }
}

static const char LC_CANDIDATE_CONF300_JSON[] =
    "{\"schema\":1,\"counter_name\":\"Counter One\",\"target_class_name\":\"person\","
    "\"line\":{\"x1\":250,\"y1\":128,\"x2\":750,\"y2\":500,\"outside_x\":500,"
    "\"outside_y\":350},\"conf_threshold_permille\":300,\"max_dist_permille\":350,"
    "\"track_history_k\":8,\"max_miss\":12,\"k_confirm\":2,\"tracks_report_enable\":true,"
    "\"heat_grid_enable\":false,\"window_minutes\":60}";

static void test_bridge_readthrough_ownership_stress(void)
{
    static const uint32_t host_percents[4] = {40u, 33u, 70u, 61u};
    cJSON *root;
    uint8_t snap[768];
    cJSON_Hooks hooks;
    long baseline;
    uint32_t mirror_conf = 0u;
    uint32_t i;
    uint32_t rid = 7000u;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    live_backend_init();
    live_bind_grant_identity(g_ctl_gen, 64u);
    root = request_grant("[\"snapshot.read\",\"config.apply\",\"live.subscribe\","
                         "\"live.unsubscribe\"]");
    cJSON_Delete(root);
    build_snapshot(snap, LC_CONF_SNAPSHOT_JSON, 6u, 5u, 0u, 0u);
    CHECK(app_host_control_snapshot_submit(
              snap, APP_CONTROL_HEADER_SIZE +
                        (uint32_t)strlen(LC_CONF_SNAPSHOT_JSON)) == APP_CONTROL_OK);
    hooks.malloc_fn = hook_track_malloc;
    hooks.free_fn = hook_track_free;
    g_hook_live = 0;
    g_now_step = 50u;
    cJSON_InitHooks(&hooks);
    baseline = g_hook_live;
    for (i = 0u; i < 240u; i++) {
        uint32_t percent = host_percents[i & 3u];
        char extra[640];

        g_ai_confidence_threshold = percent;
        root = bridge_call("snapshot.read", rid++, NULL);
        CHECK(root != NULL);
        if (root != NULL) {
            CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
            CHECK(snapshot_conf_of(root) == percent * 10u);
            cJSON_Delete(root);
        }
        CHECK(mirror_deliver_and_confirm(&mirror_conf, NULL, NULL) ==
              (uint32_t)APP_CONTROL_OK);
        CHECK(mirror_conf == percent * 10u);
        if ((i % 6u) == 0u) {
            root = bridge_call("live.subscribe", rid++,
                               "\"payload\":{\"stream\":\"live\"}");
            CHECK(root != NULL);
            if (root != NULL) {
                CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
                cJSON_Delete(root);
            }
        }
        if ((i % 12u) == 0u) {
            g_sleep_hook = native_apply_hook;
            (void)snprintf(extra, sizeof(extra),
                           "\"expected_config_generation\":6,"
                           "\"payload\":{\"session_generation\":%u,"
                           "\"candidate\":%s}",
                           (unsigned int)g_ctl_gen, LC_CANDIDATE_CONF300_JSON);
            root = bridge_call("config.apply", rid++, extra);
            g_sleep_hook = NULL;
            CHECK(root != NULL);
            if (root != NULL) {
                CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
                cJSON_Delete(root);
            }
            CHECK(mirror_deliver_and_confirm(&mirror_conf, NULL, NULL) ==
                  (uint32_t)APP_CONTROL_OK);
            CHECK(mirror_conf == 300u);
        }
        CHECK(g_hook_live == baseline);
    }
    CHECK(g_hook_allocs > 240u);
    CHECK(g_hook_live == baseline);
    cJSON_InitHooks(NULL);
    live_backend_init();
    reset_all();
}

static void assert_broker_reset_body_shape(const char *body)
{
    cJSON *parsed = cJSON_Parse(body);
    cJSON *payload;

    CHECK(parsed != NULL);
    if (parsed == NULL) {
        return;
    }
    CHECK(cJSON_GetArraySize(parsed) == 5);
    CHECK(cJSON_GetObjectItem(parsed, "grant_id") != NULL);
    CHECK(cJSON_GetObjectItem(parsed, "op") != NULL);
    CHECK(cJSON_GetObjectItem(parsed, "request_id") != NULL);
    CHECK(cJSON_GetObjectItem(parsed, "confirm_token") != NULL);
    payload = cJSON_GetObjectItem(parsed, "payload");
    CHECK(cJSON_IsObject(payload));
    if (payload != NULL) {
        CHECK(cJSON_GetArraySize(payload) == 1);
        CHECK(cJSON_GetObjectItem(payload, "session_generation") != NULL);
        CHECK(cJSON_GetObjectItem(payload, "confirm_token") == NULL);
    }
    cJSON_Delete(parsed);
}

static void test_counters_reset_requires_admin_confirm(void)
{
    cJSON *root;
    cJSON *confirm;
    char body[1024];
    int len;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"counters.reset\"]");
    cJSON_Delete(root);
    len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 51, g_ctl_gen,
                                     NULL, NULL);
    CHECK(len > 0);
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
    cJSON_Delete(root);
    root = bridge_call("counters.reset", 52, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    {
        char extra[512];
        (void)snprintf(extra, sizeof(extra),
                       "\"payload\":{\"session_generation\":%u}", (unsigned int)(g_ctl_gen + 1u));
        root = bridge_call("counters.reset", 53, extra);
    }
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STALE_SESSION_GENERATION") == 0);
    cJSON_Delete(root);
    {
        char confirm_body[192];
        (void)snprintf(confirm_body, sizeof(confirm_body),
                       "{\"grant_id\":\"%s\",\"op\":\"counters.reset\"}", g_grant_id);
        confirm = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                             strlen(confirm_body));
    }
    CHECK(confirm != NULL);
    if (confirm != NULL) {
        cJSON *token = cJSON_GetObjectItem(confirm, "confirm_token");
        CHECK(token != NULL && cJSON_IsString(token));
        if (token != NULL) {
            char embedded[1280];
            int elen;

            len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 54, g_ctl_gen,
                                             NULL, token->valuestring);
            CHECK(len > 0);
            assert_broker_reset_body_shape(body);
            elen = snprintf(embedded, sizeof(embedded),
                            "{\"grant_id\":\"%s\",\"op\":\"counters.reset\",\"request_id\":56,"
                            "\"payload\":{\"session_generation\":%u,\"confirm_token\":\"%s\"}}",
                            g_grant_id, (unsigned int)g_ctl_gen, token->valuestring);
            CHECK(elen > 0);
            CHECK(bridge_call_body(embedded, (size_t)elen, &root));
            CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
            CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
            cJSON_Delete(root);
            g_sleep_hook = native_apply_hook;
            CHECK(bridge_call_body(body, (size_t)len, &root));
            CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
            CHECK(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "result"), "confirm_token") == NULL);
            cJSON_Delete(root);
            CHECK(bridge_call_body(body, (size_t)len, &root));
            CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
            CHECK(strcmp(reason_of(root), "REPLAY") == 0);
            cJSON_Delete(root);
            {
                char extra[512];
                (void)snprintf(extra, sizeof(extra),
                               "\"expected_config_generation\":6,"
                               "\"payload\":{\"session_generation\":%u}",
                               (unsigned int)g_ctl_gen);
                root = bridge_call("counters.reset", 55, extra);
            }
            CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
            CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
            cJSON_Delete(root);
            g_sleep_hook = NULL;
        }
        cJSON_Delete(confirm);
    }
    reset_all();
}

static void test_confirm_token_expiry_and_grant_binding(void)
{
    cJSON *root;
    cJSON *confirm;
    char body[1024];
    char confirm_body[192];
    char grant_a[65];
    char token_expired[128];
    char token_live[128];
    int len;

    memset(grant_a, 0, sizeof(grant_a));
    memset(token_expired, 0, sizeof(token_expired));
    memset(token_live, 0, sizeof(token_live));
    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"counters.reset\"]");
    CHECK(root != NULL);
    snprintf(grant_a, sizeof(grant_a), "%s", g_grant_id);
    cJSON_Delete(root);
    (void)snprintf(confirm_body, sizeof(confirm_body),
                   "{\"grant_id\":\"%s\",\"op\":\"counters.reset\"}", grant_a);
    confirm = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                         strlen(confirm_body));
    CHECK(confirm != NULL);
    if (confirm != NULL) {
        cJSON *token = cJSON_GetObjectItem(confirm, "confirm_token");
        CHECK(token != NULL && cJSON_IsString(token));
        if (token != NULL) {
            snprintf(token_expired, sizeof(token_expired), "%s", token->valuestring);
        }
        cJSON_Delete(confirm);
    }
    CHECK(token_expired[0] != '\0');
    g_epoch_ms += 121000u;
    len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 61, g_ctl_gen,
                                     NULL, token_expired);
    CHECK(len > 0);
    g_sleep_hook = native_apply_hook;
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
    cJSON_Delete(root);
    (void)snprintf(confirm_body, sizeof(confirm_body),
                   "{\"grant_id\":\"%s\",\"op\":\"counters.reset\"}", grant_a);
    confirm = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                         strlen(confirm_body));
    CHECK(confirm != NULL);
    if (confirm != NULL) {
        cJSON *token = cJSON_GetObjectItem(confirm, "confirm_token");
        CHECK(token != NULL && cJSON_IsString(token));
        if (token != NULL) {
            snprintf(token_live, sizeof(token_live), "%s", token->valuestring);
            CHECK(strcmp(token_live, token_expired) != 0);
        }
        cJSON_Delete(confirm);
    }
    CHECK(token_live[0] != '\0');
    len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 62, g_ctl_gen,
                                     NULL, token_live);
    CHECK(len > 0);
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    cJSON_Delete(root);
    root = request_grant("[\"counters.reset\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    CHECK(strcmp(g_grant_id, grant_a) != 0);
    len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 63, g_ctl_gen,
                                     NULL, token_live);
    CHECK(len > 0);
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
    cJSON_Delete(root);
    (void)snprintf(confirm_body, sizeof(confirm_body),
                   "{\"grant_id\":\"%s\",\"op\":\"counters.reset\"}", g_grant_id);
    confirm = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                         strlen(confirm_body));
    CHECK(confirm != NULL);
    if (confirm != NULL) {
        cJSON *token = cJSON_GetObjectItem(confirm, "confirm_token");
        CHECK(token != NULL && cJSON_IsString(token));
        if (token != NULL) {
            CHECK(strcmp(token->valuestring, token_live) != 0);
            len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 64, g_ctl_gen,
                                             NULL, token->valuestring);
            CHECK(len > 0);
            CHECK(bridge_call_body(body, (size_t)len, &root));
            CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
            cJSON_Delete(root);
        }
        cJSON_Delete(confirm);
    }
    {
        const app_install_io_t *io = app_installer_device_io();
        CHECK(io->kv_delete(io->ctx, APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A) == APP_INSTALL_IO_OK ||
              io->kv_delete(io->ctx, APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B) == APP_INSTALL_IO_OK);
    }
    len = snprintf(body, sizeof(body),
                   "{\"grant_id\":\"%s\",\"op\":\"counters.reset\",\"request_id\":65}", grant_a);
    CHECK(len > 0);
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "SIDECAR_NOT_ACTIVE") == 0);
    {
        cJSON *revoked = cJSON_GetObjectItem(root, "revoked");
        CHECK(revoked != NULL && cJSON_IsBool(revoked) && cJSON_IsTrue(revoked));
    }
    cJSON_Delete(root);
    install_active_sidecar();
    root = request_grant("[\"counters.reset\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    CHECK(strcmp(g_grant_id, grant_a) != 0);
    len = snprintf_bridge_write_body(body, sizeof(body), "counters.reset", 66, g_ctl_gen,
                                     NULL, token_live);
    CHECK(len > 0);
    CHECK(bridge_call_body(body, (size_t)len, &root));
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
    cJSON_Delete(root);
    g_sleep_hook = NULL;
    reset_all();
}

static void test_rng_failures_issue_zero_grants(void)
{
    cJSON *root;
    cJSON *grant;
    char grant_ids[2][65];

    reset_all();
    install_active_sidecar();
    app_bridge_rng_fault_set(AICAM_TRUE, AICAM_FALSE);
    root = request_grant_raw("[\"snapshot.read\"]");
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    CHECK(g_last_error_code == (int)API_ERROR_SERVICE_UNAVAILABLE);
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_FALSE);
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    grant = cJSON_GetObjectItem(root, "grant");
    CHECK(grant != NULL);
    snprintf(grant_ids[0], sizeof(grant_ids[0]), "%s", g_grant_id);
    cJSON_Delete(root);
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    grant = cJSON_GetObjectItem(root, "grant");
    CHECK(grant != NULL);
    snprintf(grant_ids[1], sizeof(grant_ids[1]), "%s", g_grant_id);
    cJSON_Delete(root);
    CHECK(strcmp(grant_ids[0], grant_ids[1]) != 0);
    reset_all();
    install_active_sidecar();
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_TRUE);
    root = request_grant_raw("[\"snapshot.read\"]");
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    CHECK(g_last_error_code == (int)API_ERROR_SERVICE_UNAVAILABLE);
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_FALSE);
    {
        int i;
        for (i = 0; i < 4; i++) {
            root = request_grant("[\"snapshot.read\"]");
            CHECK(root != NULL);
            grant = cJSON_GetObjectItem(root, "grant");
            CHECK(grant != NULL);
            cJSON_Delete(root);
        }
    }
    reset_all();
    install_active_sidecar();
    root = request_grant("[\"counters.reset\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_TRUE);
    {
        char confirm_body[192];
        (void)snprintf(confirm_body, sizeof(confirm_body),
                       "{\"grant_id\":\"%s\",\"op\":\"counters.reset\"}", g_grant_id);
        root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                          strlen(confirm_body));
    }
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    CHECK(g_last_error_code == (int)API_ERROR_SERVICE_UNAVAILABLE);
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_FALSE);
    root = request_grant("[\"counters.reset\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    {
        char confirm_body[192];
        (void)snprintf(confirm_body, sizeof(confirm_body),
                       "{\"grant_id\":\"%s\",\"op\":\"counters.reset\"}", g_grant_id);
        root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                          strlen(confirm_body));
    }
    CHECK(root != NULL);
    if (root != NULL) {
        cJSON *token = cJSON_GetObjectItem(root, "confirm_token");
        CHECK(token != NULL && cJSON_IsString(token));
        if (token != NULL) {
            CHECK(strlen(token->valuestring) == 64);
        }
    }
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    app_bridge_rng_fault_set(AICAM_TRUE, AICAM_FALSE);
    {
        int i;
        for (i = 0; i < 3; i++) {
            root = request_grant_raw("[\"snapshot.read\"]");
            CHECK(root == NULL);
            CHECK(strcmp(g_last_kind, "error") == 0);
        }
        root = request_grant_raw("[\"snapshot.read\"]");
        CHECK(root == NULL);
    }
    app_bridge_rng_fault_set(AICAM_FALSE, AICAM_FALSE);
    root = request_grant_raw("[\"snapshot.read\"]");
    CHECK(root == NULL);
    app_bridge_grants_reset();
    root = request_grant("[\"snapshot.read\"]");
    CHECK(root != NULL);
    cJSON_Delete(root);
    reset_all();
}

static void test_replay_and_generation_change(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open() == g_ctl_gen);
    root = request_grant("[\"snapshot.read\",\"config.read\"]");
    cJSON_Delete(root);
    root = bridge_call("snapshot.read", 61, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    cJSON_Delete(root);
    root = bridge_call("snapshot.read", 61, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "REPLAY") == 0);
    cJSON_Delete(root);
    {
        const app_install_io_t *io = app_installer_device_io();
        CHECK(io->kv_delete(io->ctx, APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A) == APP_INSTALL_IO_OK ||
              io->kv_delete(io->ctx, APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B) == APP_INSTALL_IO_OK);
    }
    root = bridge_call("snapshot.read", 62, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "SIDECAR_NOT_ACTIVE") == 0);
    {
        cJSON *revoked = cJSON_GetObjectItem(root, "revoked");
        CHECK(revoked != NULL && cJSON_IsBool(revoked) && cJSON_IsTrue(revoked));
    }
    cJSON_Delete(root);
    root = bridge_call("snapshot.read", 63, NULL);
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    reset_all();
}

static void test_revoke_endpoint(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    root = request_grant("[\"snapshot.read\"]");
    cJSON_Delete(root);
    root = call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/revoke", "{\"all\":true}", 11);
    CHECK(root != NULL);
    {
        cJSON *count = cJSON_GetObjectItem(root, "count");
        CHECK(count != NULL && cJSON_IsNumber(count) && count->valueint == 1);
    }
    cJSON_Delete(root);
    root = bridge_call("snapshot.read", 71, NULL);
    CHECK(root == NULL);
    CHECK(strcmp(g_last_kind, "error") == 0);
    reset_all();
}

static void test_sidecar_ui_projection(void)
{
    cJSON *root;

    reset_all();
    root = call_route("GET", API_PATH_PREFIX "/app/sidecar/ui", "{}", 2);
    CHECK(root != NULL);
    {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        CHECK(state != NULL && cJSON_IsString(state));
        if (state != NULL) {
            CHECK(strcmp(state->valuestring, "unavailable") == 0);
        }
    }
    cJSON_Delete(root);
    install_active_sidecar();
    app_bridge_app_site_origin_set("http://127.0.0.1:48080");
    root = call_route("GET", API_PATH_PREFIX "/app/sidecar/ui", "{}", 2);
    CHECK(root != NULL);
    {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        cJSON *origin = cJSON_GetObjectItem(root, "app_site_origin");
        cJSON *base = cJSON_GetObjectItem(root, "ui_base_path");
        cJSON *abi = cJSON_GetObjectItem(root, "host_ui_abi");
        CHECK(state != NULL && origin != NULL && base != NULL);
        if (state != NULL && origin != NULL && base != NULL) {
            CHECK(strcmp(state->valuestring, "active") == 0);
            CHECK(cJSON_IsNull(origin));
            CHECK(strncmp(base->valuestring, "/appsite/g", 10) == 0);
        }
        CHECK(abi != NULL && cJSON_IsNumber(abi) && abi->valueint == 1);
    }
    cJSON_Delete(root);
    app_bridge_app_site_origin_set("http://evil.example.com/admin/path");
    root = call_route("GET", API_PATH_PREFIX "/app/sidecar/ui", "{}", 2);
    CHECK(root != NULL);
    {
        cJSON *origin = cJSON_GetObjectItem(root, "app_site_origin");
        CHECK(origin != NULL && cJSON_IsNull(origin));
    }
    cJSON_Delete(root);
    app_bridge_app_site_origin_set(NULL);
    reset_all();
}


static void test_sidecar_projection_refresh_retention(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
    app_sidecar_lifecycle_result_t result;
    uint64_t generation;
    uint8_t *bundle;
    size_t bundle_len;

    reset_all();
    install_active_sidecar();
    CHECK(web_sidecar_projection_valid() == AICAM_TRUE);
    CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_TRUE);
    generation = web_sidecar_projection_generation();
    CHECK(generation != 0u);

    g_io.tx_held = 1;
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    CHECK(web_sidecar_projection_valid() == AICAM_TRUE);
    CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_TRUE);
    CHECK(web_sidecar_projection_generation() == generation);
    g_io.tx_held = 0;

    make_native_record(&native);
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = native.publisher_id;
    trust.publisher_id_len = strlen(native.publisher_id);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.native_tbs_sha256 = native.tbs_sha256;
    bundle = load_bundle("serving_upgrade.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle != NULL) {
        memset(&descriptor, 0, sizeof(descriptor));
        result = app_sidecar_lifecycle_install_pair(io, &trust, &native, bundle,
                                                    bundle_len, NULL,
                                                    &descriptor, &state);
        free(bundle);
        CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
        CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
        CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
        CHECK(web_sidecar_projection_generation() != generation);
        CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_TRUE);
        CHECK(web_sidecar_projection_valid() == AICAM_TRUE);
    }

    CHECK(io->kv_delete(io->ctx, APP_INSTALL_KEY_RECORD) == APP_INSTALL_IO_OK);
    CHECK(web_sidecar_projection_refresh() == AICAM_FALSE);
    CHECK(web_sidecar_projection_valid() == AICAM_FALSE);
    CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_FALSE);
    CHECK(web_sidecar_projection_generation() == 0u);

    g_io.tx_held = 1;
    CHECK(web_sidecar_projection_refresh() == AICAM_FALSE);
    g_io.tx_held = 0;

    reset_all();
}

static void test_sidecar_gate_rearm_after_invalidation(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t native;
    uint64_t generation;

    reset_all();
    install_active_sidecar();
    CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_TRUE);
    generation = web_sidecar_projection_generation();

    CHECK(io->kv_delete(io->ctx, APP_INSTALL_KEY_RECORD) == APP_INSTALL_IO_OK);
    CHECK(web_sidecar_projection_refresh() == AICAM_FALSE);
    CHECK(web_sidecar_projection_valid() == AICAM_FALSE);
    CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_FALSE);

    make_native_record(&native);
    CHECK(web_sidecar_projection_valid() == AICAM_FALSE);
    CHECK(web_sidecar_projection_same_origin_allowed() == AICAM_TRUE);
    CHECK(web_sidecar_projection_valid() == AICAM_TRUE);
    CHECK(web_sidecar_projection_generation() == generation);

    reset_all();
}

static uint32_t g_fake_live_now;
static int g_precommit_mode;
static int g_receipt_check_mode;

static int32_t fake_enable_precommit(void *ctx, uint32_t session_generation,
                                     uint32_t request_id, uint32_t config_generation,
                                     uint32_t expected_state_revision, uint32_t desired_state)
{
    (void)ctx;
    (void)session_generation;
    (void)request_id;
    (void)config_generation;
    (void)expected_state_revision;
    (void)desired_state;
    if (g_precommit_mode == 1) {
        return APP_CONTROL_ERR_CONFLICT;
    }
    if (g_precommit_mode == 2) {
        return APP_CONTROL_ERR_STORAGE_GUARD;
    }
    return APP_CONTROL_OK;
}

static int32_t fake_enable_receipt_check(void *ctx, uint32_t session_generation,
                                         uint32_t request_id, uint32_t config_generation,
                                         uint32_t state_revision, uint32_t desired_state)
{
    (void)ctx;
    (void)session_generation;
    (void)request_id;
    (void)config_generation;
    (void)state_revision;
    (void)desired_state;
    return (g_receipt_check_mode == 0) ? APP_CONTROL_OK : APP_CONTROL_ERR_STORAGE_GUARD;
}

static uint32_t fake_live_now(void *ctx)
{
    (void)ctx;
    return g_fake_live_now;
}

static uint64_t fake_live_epoch(void *ctx)
{
    (void)ctx;
    return g_fake_live_epoch;
}

static void ctl_init_v4_fake(void)
{
    app_control_backend_t b;

    memset(&b, 0, sizeof(b));
    b.now_ms = fake_zero_now;
    b.mutex_create = fake_mutex_create;
    b.mutex_destroy = fake_mutex_destroy;
    b.mutex_lock = fake_mutex_lock;
    b.mutex_unlock = fake_mutex_unlock;
    b.enable_precommit = fake_enable_precommit;
    b.enable_receipt_check = fake_enable_receipt_check;
    g_precommit_mode = 0;
    g_receipt_check_mode = 0;
    app_host_control_init(&b);
}

static uint32_t ctl_session_open_caps(uint32_t caps)
{
    app_control_session_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.caps = caps;
    cfg.control_max = APP_CONTROL_MSG_RAM_MAX;
    cfg.snapshot_max = APP_CONTROL_SNAPSHOT_RAM_MAX;
    strcpy(cfg.publisher_id, SC_VEC_PUBLISHER_ID);
    strcpy(cfg.app_id, SC_VEC_APP_ID);
    CHECK(app_host_control_session_open(&cfg, &g_ctl_gen) == 0);
    return g_ctl_gen;
}

static uint32_t ctl_session_open_v4(uint32_t caps)
{
    app_control_session_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_major = 4u;
    cfg.caps = caps;
    cfg.control_max = APP_CONTROL_MSG_RAM_MAX;
    cfg.snapshot_max = APP_CONTROL_SNAPSHOT_RAM_MAX;
    strcpy(cfg.publisher_id, SC_VEC_PUBLISHER_ID);
    strcpy(cfg.app_id, SC_VEC_APP_ID);
    CHECK(app_host_control_session_open(&cfg, &g_ctl_gen) == 0);
    return g_ctl_gen;
}

static void test_put_u32(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
    buf[2] = (uint8_t)((v >> 16) & 0xFFu);
    buf[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t test_get_u32(const uint8_t *buf)
{
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

static void native_enable_receipt_hook(uint32_t status)
{
    uint8_t rx[128];
    uint32_t rx_len = 0u;
    uint32_t id;
    uint32_t desired;
    uint32_t rev = 6u;

    if (app_host_control_deliver(rx, sizeof(rx), &rx_len) != APP_CONTROL_OK) {
        return;
    }
    id = test_get_u32(rx + 16);
    desired = (rx_len >= APP_CONTROL_ENABLE_SET_SIZE)
                  ? test_get_u32(rx + APP_CONTROL_HEADER_SIZE + 4u)
                  : 0u;
    build_cmd_header(rx, APP_CONTROL_MSG_TYPE_RECEIPT, APP_CONTROL_ENABLE_RECEIPT_SIZE,
                     g_ctl_gen, id, 6u, status);
    rx[3] = 0x34u;
    test_put_u32(rx + 24, rev);
    rx[32] = 0x45u; rx[33] = 0x4Eu; rx[34] = 0x52u; rx[35] = 0x31u;
    test_put_u32(rx + 36, (status == APP_CONTROL_RESULT_APPLIED)
                              ? desired : APP_CONTROL_ENABLE_STATE_UNKNOWN);
    test_put_u32(rx + 40, rev);
    test_put_u32(rx + 44, 0u);
    app_host_control_receipt(rx, APP_CONTROL_ENABLE_RECEIPT_SIZE);
}

static void native_enable_apply_hook(uint32_t ms)
{
    (void)ms;
    native_enable_receipt_hook(APP_CONTROL_RESULT_APPLIED);
}

static void native_enable_stale_hook(uint32_t ms)
{
    (void)ms;
    native_enable_receipt_hook(APP_CONTROL_RESULT_REJECTED_STALE_CONFIG);
}

static void native_enable_storage_hook(uint32_t ms)
{
    (void)ms;
    native_enable_receipt_hook(APP_CONTROL_RESULT_FAILED_STORAGE_UNKNOWN);
}

static void live_backend_init(void)
{
    app_host_live_backend_t backend;

    memset(&backend, 0, sizeof(backend));
    backend.ctx = NULL;
    backend.now_ms = fake_live_now;
    backend.epoch_ms = fake_live_epoch;
    backend.mutex_create = fake_mutex_create;
    backend.mutex_destroy = fake_mutex_destroy;
    backend.mutex_lock = fake_mutex_lock;
    backend.mutex_unlock = fake_mutex_unlock;
    g_fake_live_now = 1000u;
    g_fake_live_epoch = 1760000001000ull;
    app_host_live_init(&backend);
}

static void live_bind_grant_identity(uint32_t generation, uint32_t track_max)
{
    app_host_live_identity_t ident;

    memset(&ident, 0, sizeof(ident));
    ident.session_generation = generation;
    snprintf(ident.publisher_id, sizeof(ident.publisher_id), "%s",
             APP_ADMISSION_ADMIN_PUBLISHER_ID);
    snprintf(ident.app_id, sizeof(ident.app_id), "%s", SC_VEC_APP_ID);
    memcpy(ident.tbs_sha256, SC_VEC_NATIVE_TBS, sizeof(ident.tbs_sha256));
    memset(ident.package_sha256, 0x11, sizeof(ident.package_sha256));
    ident.live_publish_max = 9008u;
    ident.live_track_max = track_max;
    CHECK(app_host_live_bind(&ident) == APP_HOST_LIVE_OK);
}

static uint32_t live_header(uint8_t *buf, uint32_t kind, uint32_t total_len,
                            uint32_t packet_seq, uint32_t through_seq,
                            uint32_t source_total, uint32_t source_gap,
                            uint32_t record_count, uint32_t flags)
{
    memset(buf, 0, APP_HOST_LIVE_HEADER_SIZE);
    memcpy(buf, "LVN4", 4u);
    test_put_u32(buf + 8, total_len);
    test_put_u32(buf + 12, packet_seq);
    test_put_u32(buf + 16, through_seq);
    test_put_u32(buf + 20, source_total);
    test_put_u32(buf + 24, source_gap);
    test_put_u32(buf + 32, 6u);
    test_put_u32(buf + 36, record_count);
    test_put_u32(buf + 40, flags);
    test_put_u32(buf + 44, 0u);
    buf[4] = 0x01u;
    buf[5] = 0x00u;
    buf[6] = (uint8_t)(kind & 0xFFu);
    buf[7] = (uint8_t)((kind >> 8) & 0xFFu);
    return APP_HOST_LIVE_HEADER_SIZE;
}

static void live_event_record(uint8_t *rec, uint32_t event_seq, uint32_t track_id,
                              uint32_t direction)
{
    memset(rec, 0, APP_HOST_LIVE_EVENT_RECORD_SIZE);
    test_put_u32(rec + 0, event_seq);
    test_put_u32(rec + 4, track_id);
    test_put_u32(rec + 8, 1234u);
    rec[12] = (uint8_t)direction;
    rec[14] = 0x40u;
    rec[15] = 0x80u;
}

static uint32_t live_track_record(uint8_t *buf, uint32_t track_id, uint32_t point_count)
{
    uint32_t i;

    test_put_u32(buf + 0, track_id);
    buf[4] = (uint8_t)(point_count & 0xFFu);
    buf[5] = (uint8_t)((point_count >> 8) & 0xFFu);
    buf[6] = 0x00u;
    buf[7] = 0x00u;
    buf[8] = 0x01u;
    buf[9] = 0x00u;
    test_put_u32(buf + 10, 60000u);
    for (i = 0u; i < point_count; i++) {
        uint8_t *pt = buf + APP_HOST_LIVE_TRACK_HEADER_SIZE + i * APP_HOST_LIVE_TRACK_POINT_SIZE;

        test_put_u32(pt + 0, 1000u + i);
        test_put_u32(pt + 4, (uint32_t)(i * 4096u));
        test_put_u32(pt + 6, (uint32_t)(65535u - i * 4096u));
    }
    return APP_HOST_LIVE_TRACK_HEADER_SIZE + point_count * APP_HOST_LIVE_TRACK_POINT_SIZE;
}

static cJSON *enable_apply_call(uint32_t request_id, const char *value_json,
                                const char *confirm_token)
{
    char body[1024];
    int len;

    if (confirm_token != NULL) {
        len = snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"enable.apply\",\"request_id\":%u,"
                       "\"payload\":{\"session_generation\":%u,\"value\":%s},"
                       "\"confirm_token\":\"%s\"}",
                       g_grant_id, (unsigned int)request_id, (unsigned int)g_ctl_gen,
                       value_json, confirm_token);
    } else {
        len = snprintf(body, sizeof(body),
                       "{\"grant_id\":\"%s\",\"op\":\"enable.apply\",\"request_id\":%u,"
                       "\"payload\":{\"session_generation\":%u,\"value\":%s}}",
                       g_grant_id, (unsigned int)request_id, (unsigned int)g_ctl_gen,
                       value_json);
    }
    CHECK(len > 0);
    {
        cJSON *root = NULL;

        bridge_call_body(body, (size_t)len, &root);
        return root;
    }
}

static cJSON *request_enable_confirm(void)
{
    char confirm_body[192];

    (void)snprintf(confirm_body, sizeof(confirm_body),
                   "{\"grant_id\":\"%s\",\"op\":\"enable.apply\"}", g_grant_id);
    return call_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm", confirm_body,
                      strlen(confirm_body));
}

static void submit_enable_snapshot(const char *state_json, uint32_t session_gen)
{
    char json[160];
    uint8_t snap[256];

    (void)snprintf(json, sizeof(json), "{\"status\":{\"state\":%s}}", state_json);
    build_snapshot(snap, json, 6u, 5u, session_gen, 0u);
    snap[3] = 0x34u;
    CHECK(app_host_control_snapshot_submit(
        snap, APP_CONTROL_HEADER_SIZE + (uint32_t)strlen(json)) == APP_CONTROL_OK);
}

static void test_enable_read_states(void)
{
    cJSON *root;
    cJSON *payload;

    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"enable.read\"]");
    cJSON_Delete(root);
    root = bridge_call("enable.read", 121, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "ENABLE_STATE_UNKNOWN") == 0);
    cJSON_Delete(root);
    submit_enable_snapshot("\"running\"", 0u);
    root = bridge_call("enable.read", 122, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");

        CHECK(result != NULL);
        payload = cJSON_GetObjectItem(result, "payload");
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            cJSON *sg = cJSON_GetObjectItem(payload, "session_generation");
            cJSON *value = cJSON_GetObjectItem(payload, "value");
            CHECK(cJSON_GetArraySize(payload) == 2);
            CHECK(sg != NULL && cJSON_IsNumber(sg) && sg->valueint == (int)g_ctl_gen);
            CHECK(value != NULL && cJSON_IsBool(value) && cJSON_IsTrue(value));
        }
    }
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"enable.read\"]");
    cJSON_Delete(root);
    submit_enable_snapshot("\"stopped\"", 0u);
    root = bridge_call("enable.read", 123, NULL);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");

        payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            cJSON *value = cJSON_GetObjectItem(payload, "value");
            CHECK(value != NULL && cJSON_IsBool(value) && cJSON_IsFalse(value));
        }
    }
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"enable.read\"]");
    cJSON_Delete(root);
    submit_enable_snapshot("\"nonsense\"", 0u);
    root = bridge_call("enable.read", 124, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "ENABLE_STATE_UNKNOWN") == 0);
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_fake();
    CHECK(ctl_session_open_caps(0x000000FFu) == g_ctl_gen);
    root = request_grant("[\"enable.read\",\"enable.apply\"]");
    cJSON_Delete(root);
    root = bridge_call("enable.read", 125, NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "ENABLE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    root = enable_apply_call(126, "true", NULL);
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "ENABLE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    reset_all();
}

static void test_enable_apply_gates(void)
{
    cJSON *root;

    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"enable.apply\"]");
    cJSON_Delete(root);
    root = enable_apply_call(131, "\"not-bool\"", NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "ENABLE_VALUE_INVALID") == 0);
    cJSON_Delete(root);
    {
        char body[512];
        int len = snprintf(body, sizeof(body),
                           "{\"grant_id\":\"%s\",\"op\":\"enable.apply\",\"request_id\":132,"
                           "\"payload\":{\"session_generation\":%u,\"value\":true}}",
                           g_grant_id, (unsigned int)(g_ctl_gen + 3u));
        CHECK(len > 0);
        CHECK(bridge_call_body(body, (size_t)len, &root));
    }
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STALE_SESSION_GENERATION") == 0);
    cJSON_Delete(root);
    root = enable_apply_call(133, "true", NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CONFIG_GENERATION_UNKNOWN") == 0);
    cJSON_Delete(root);
    submit_enable_snapshot("\"running\"", 0u);
    root = enable_apply_call(134, "true", NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "ADMIN_CONFIRM_REQUIRED") == 0);
    cJSON_Delete(root);
    reset_all();
}

static void test_enable_apply_native_paths(void)
{
    cJSON *root;
    cJSON *confirm;
    cJSON *token_item;
    cJSON *payload;
    char token[80];
    uint8_t rx[128];
    uint32_t rx_len = 0u;

    token[0] = '\0';
    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"enable.apply\"]");
    cJSON_Delete(root);
    submit_enable_snapshot("\"running\"", 0u);
    confirm = request_enable_confirm();
    CHECK(confirm != NULL);
    token_item = (confirm != NULL) ? cJSON_GetObjectItem(confirm, "confirm_token") : NULL;
    CHECK(token_item != NULL && cJSON_IsString(token_item));
    if (token_item != NULL) {
        snprintf(token, sizeof(token), "%s", token_item->valuestring);
    }
    cJSON_Delete(confirm);
    g_precommit_mode = 1;
    root = enable_apply_call(141, "false", token);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "CONFLICT") == 0);
    cJSON_Delete(root);
    g_precommit_mode = 0;
    confirm = request_enable_confirm();
    CHECK(confirm != NULL);
    token_item = (confirm != NULL) ? cJSON_GetObjectItem(confirm, "confirm_token") : NULL;
    CHECK(token_item != NULL && cJSON_IsString(token_item));
    if (token_item != NULL) {
        snprintf(token, sizeof(token), "%s", token_item->valuestring);
    }
    cJSON_Delete(confirm);
    g_sleep_hook = native_noop_hook;
    g_now_step = 600u;
    root = enable_apply_call(142, "false", token);
    CHECK(strcmp(outcome_of(root), "UNKNOWN") == 0);
    CHECK(strcmp(reason_of(root), "ACK_TIMEOUT") == 0);
    cJSON_Delete(root);
    g_now_step = 0u;
    g_sleep_hook = NULL;
    CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
    CHECK(rx_len == APP_CONTROL_ENABLE_SET_SIZE);
    CHECK(test_get_u32(rx + 0) == APP_CONTROL_PROTO_MAGIC_V4);
    CHECK(test_get_u32(rx + 6) == 0u ||
          ((uint32_t)rx[6] | ((uint32_t)rx[7] << 8)) == APP_CONTROL_MSG_TYPE_ENABLE_SET);
    CHECK(rx_len == test_get_u32(rx + 8));
    CHECK(test_get_u32(rx + 12) == g_ctl_gen);
    CHECK(test_get_u32(rx + 16) == 142u);
    CHECK(test_get_u32(rx + 20) == 6u);
    CHECK(test_get_u32(rx + 24) == 0u);
    CHECK(test_get_u32(rx + 28) == 0u);
    CHECK(rx[32] == 0x45u && rx[33] == 0x4Eu && rx[34] == 0x42u && rx[35] == 0x31u);
    CHECK(test_get_u32(rx + 36) == APP_CONTROL_ENABLE_STATE_PAUSED);
    CHECK(test_get_u32(rx + 40) == 5u);
    CHECK(test_get_u32(rx + 44) == 0u);
    {
        uint32_t desired_seen = test_get_u32(rx + 36);

        build_cmd_header(rx, APP_CONTROL_MSG_TYPE_RECEIPT, APP_CONTROL_ENABLE_RECEIPT_SIZE,
                         g_ctl_gen, 142u, 6u, APP_CONTROL_RESULT_APPLIED);
        rx[3] = 0x34u;
        test_put_u32(rx + 24, 6u);
        rx[32] = 0x45u; rx[33] = 0x4Eu; rx[34] = 0x52u; rx[35] = 0x31u;
        test_put_u32(rx + 36, desired_seen);
        test_put_u32(rx + 40, 6u);
        test_put_u32(rx + 44, 0u);
        CHECK(app_host_control_receipt(rx, APP_CONTROL_ENABLE_RECEIPT_SIZE) == APP_CONTROL_OK);
    }
    confirm = request_enable_confirm();
    CHECK(confirm != NULL);
    token_item = (confirm != NULL) ? cJSON_GetObjectItem(confirm, "confirm_token") : NULL;
    CHECK(token_item != NULL && cJSON_IsString(token_item));
    if (token_item != NULL) {
        snprintf(token, sizeof(token), "%s", token_item->valuestring);
    }
    cJSON_Delete(confirm);
    root = enable_apply_call(100, "false", token);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "DUPLICATE_ID_CONFLICT") == 0);
    cJSON_Delete(root);
    confirm = request_enable_confirm();
    CHECK(confirm != NULL);
    token_item = (confirm != NULL) ? cJSON_GetObjectItem(confirm, "confirm_token") : NULL;
    CHECK(token_item != NULL && cJSON_IsString(token_item));
    if (token_item != NULL) {
        snprintf(token, sizeof(token), "%s", token_item->valuestring);
    }
    cJSON_Delete(confirm);
    g_sleep_hook = native_enable_apply_hook;
    root = enable_apply_call(143, "false", token);
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *rev;

        CHECK(result != NULL);
        rev = cJSON_GetObjectItem(result, "state_revision");
        CHECK(rev != NULL && cJSON_IsNumber(rev) && rev->valueint == 6);
        payload = cJSON_GetObjectItem(result, "payload");
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            cJSON *sg = cJSON_GetObjectItem(payload, "session_generation");
            cJSON *value = cJSON_GetObjectItem(payload, "value");
            CHECK(cJSON_GetArraySize(payload) == 2);
            CHECK(sg != NULL && cJSON_IsNumber(sg) && sg->valueint == (int)g_ctl_gen);
            CHECK(value != NULL && cJSON_IsBool(value) && cJSON_IsFalse(value));
        }
    }
    cJSON_Delete(root);
    root = enable_apply_call(143, "false", NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "REPLAY") == 0);
    cJSON_Delete(root);
    confirm = request_enable_confirm();
    CHECK(confirm != NULL);
    token_item = (confirm != NULL) ? cJSON_GetObjectItem(confirm, "confirm_token") : NULL;
    CHECK(token_item != NULL && cJSON_IsString(token_item));
    if (token_item != NULL) {
        snprintf(token, sizeof(token), "%s", token_item->valuestring);
    }
    cJSON_Delete(confirm);
    g_sleep_hook = native_enable_stale_hook;
    root = enable_apply_call(144, "true", token);
    CHECK(strcmp(outcome_of(root), "CONFLICT") == 0);
    CHECK(strcmp(reason_of(root), "STALE_CONFIG_GENERATION") == 0);
    cJSON_Delete(root);
    confirm = request_enable_confirm();
    CHECK(confirm != NULL);
    token_item = (confirm != NULL) ? cJSON_GetObjectItem(confirm, "confirm_token") : NULL;
    CHECK(token_item != NULL && cJSON_IsString(token_item));
    if (token_item != NULL) {
        snprintf(token, sizeof(token), "%s", token_item->valuestring);
    }
    cJSON_Delete(confirm);
    g_sleep_hook = native_enable_storage_hook;
    root = enable_apply_call(145, "true", token);
    CHECK(strcmp(outcome_of(root), "UNKNOWN") == 0);
    CHECK(strcmp(reason_of(root), "failed_storage_unknown") == 0);
    cJSON_Delete(root);
    g_sleep_hook = NULL;
    reset_all();
}

static void test_live_bridge_paths(void)
{
    cJSON *root;
    cJSON *payload;
    cJSON *events;
    cJSON *tracks;
    uint8_t packet[512];
    uint32_t len;

    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"live.subscribe\",\"live.unsubscribe\"]");
    cJSON_Delete(root);
    root = bridge_call("live.subscribe", 151, NULL);
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "PAYLOAD_REQUIRED") == 0);
    cJSON_Delete(root);
    root = bridge_call("live.subscribe", 152, "\"payload\":{\"stream\":\"other\"}");
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STREAM_UNKNOWN") == 0);
    cJSON_Delete(root);
    root = bridge_call("live.subscribe", 153, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "LIVE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu & ~APP_ADMISSION_V4_CAP_LIVE_PUBLISH) == g_ctl_gen);
    root = request_grant("[\"live.subscribe\"]");
    cJSON_Delete(root);
    live_backend_init();
    live_bind_grant_identity(g_ctl_gen, 64u);
    root = bridge_call("live.subscribe", 154, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "LIVE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"live.subscribe\",\"live.unsubscribe\"]");
    cJSON_Delete(root);
    live_backend_init();
    live_bind_grant_identity(g_ctl_gen, 64u);
    root = bridge_call("live.subscribe", 155, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result_item = cJSON_GetObjectItem(root, "result");

        CHECK(result_item != NULL);
        payload = cJSON_GetObjectItem(result_item, "payload");
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            cJSON *freshness = cJSON_GetObjectItem(payload, "freshness");
            cJSON *sg = cJSON_GetObjectItem(payload, "session_generation");
            cJSON *through = cJSON_GetObjectItem(payload, "through_seq");
            cJSON *view = cJSON_GetObjectItem(payload, "view_epoch");
            cJSON *now = cJSON_GetObjectItem(payload, "server_now_ms");
            cJSON *target = cJSON_GetObjectItem(payload, "target_class");
            events = cJSON_GetObjectItem(payload, "events");
            tracks = cJSON_GetObjectItem(payload, "tracks");
            CHECK(freshness != NULL && cJSON_IsString(freshness));
            if (freshness != NULL) {
                CHECK(strcmp(freshness->valuestring, "none") == 0);
            }
            CHECK(sg != NULL && cJSON_IsNumber(sg) && sg->valueint == (int)g_ctl_gen);
            CHECK(through != NULL && cJSON_IsNumber(through) && through->valueint == 0);
            CHECK(view != NULL && cJSON_IsNumber(view) && view->valueint >= 1);
            CHECK(now != NULL && cJSON_IsNumber(now) &&
                  now->valuedouble == (double)g_fake_live_epoch);
            CHECK(target != NULL && cJSON_IsString(target) &&
                  strcmp(target->valuestring, "unknown") == 0);
            CHECK(events != NULL && cJSON_IsArray(events) && cJSON_GetArraySize(events) == 0);
            CHECK(tracks != NULL && cJSON_IsArray(tracks) && cJSON_GetArraySize(tracks) == 0);
        }
    }
    cJSON_Delete(root);
    len = live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH,
                      APP_HOST_LIVE_HEADER_SIZE + APP_HOST_LIVE_EVENT_RECORD_SIZE,
                      1u, 1u, 1u, 0u, 1u, 0u);
    live_event_record(packet + len, 1u, 77u, 1u);
    CHECK(app_host_live_ingest(packet, len + APP_HOST_LIVE_EVENT_RECORD_SIZE,
                               g_ctl_gen, 2u) == APP_HOST_LIVE_OK);
    g_fake_live_now += 50u;
    root = bridge_call("live.subscribe", 156, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result_item = cJSON_GetObjectItem(root, "result");

        CHECK(result_item != NULL);
        payload = cJSON_GetObjectItem(result_item, "payload");
        CHECK(payload != NULL);
        if (payload != NULL) {
            cJSON *freshness = cJSON_GetObjectItem(payload, "freshness");
            cJSON *through = cJSON_GetObjectItem(payload, "through_seq");
            cJSON *source_total = cJSON_GetObjectItem(payload, "source_total");
            cJSON *target = cJSON_GetObjectItem(payload, "target_class");

            events = cJSON_GetObjectItem(payload, "events");
            CHECK(events != NULL && cJSON_IsArray(events) && cJSON_GetArraySize(events) == 1);
            if (events != NULL && cJSON_GetArraySize(events) == 1) {
                cJSON *event = cJSON_GetArrayItem(events, 0);
                cJSON *seq = cJSON_GetObjectItem(event, "event_seq");
                cJSON *track = cJSON_GetObjectItem(event, "track_id");
                cJSON *dir = cJSON_GetObjectItem(event, "direction");
                cJSON *ts = cJSON_GetObjectItem(event, "event_ts_ms");
                CHECK(seq != NULL && seq->valueint == 1);
                CHECK(track != NULL && track->valueint == 77);
                CHECK(dir != NULL && dir->valueint == 1);
                CHECK(ts != NULL && cJSON_IsNumber(ts) &&
                      ts->valuedouble == (double)g_fake_live_epoch &&
                      ts->valuedouble != 1234.0);
            }
            CHECK(freshness != NULL && cJSON_IsString(freshness) &&
                  strcmp(freshness->valuestring, "fresh") == 0);
            CHECK(through != NULL && cJSON_IsNumber(through) && through->valueint == 1);
            CHECK(source_total != NULL && cJSON_IsNumber(source_total) &&
                  source_total->valueint == 1);
            CHECK(target != NULL && cJSON_IsString(target) &&
                  strcmp(target->valuestring, "unknown") == 0);
        }
    }
    cJSON_Delete(root);
    root = bridge_call("live.subscribe", 157, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result_item = cJSON_GetObjectItem(root, "result");

        payload = (result_item != NULL) ? cJSON_GetObjectItem(result_item, "payload") : NULL;
        CHECK(payload != NULL);
        if (payload != NULL) {
            events = cJSON_GetObjectItem(payload, "events");
            CHECK(events != NULL && cJSON_IsArray(events) && cJSON_GetArraySize(events) == 0);
        }
    }
    cJSON_Delete(root);
    len = live_header(packet, APP_HOST_LIVE_KIND_EVENT_BATCH,
                      APP_HOST_LIVE_HEADER_SIZE + APP_HOST_LIVE_EVENT_RECORD_SIZE,
                      2u, 2u, 3u, 2u, 1u, APP_HOST_LIVE_FLAG_SOURCE_GAP_DELTA);
    live_event_record(packet + len, 2u, 78u, 2u);
    CHECK(app_host_live_ingest(packet, len + APP_HOST_LIVE_EVENT_RECORD_SIZE,
                               g_ctl_gen, 2u) == APP_HOST_LIVE_OK);
    g_fake_live_now += 50u;
    root = bridge_call("live.subscribe", 158, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result_item = cJSON_GetObjectItem(root, "result");

        payload = (result_item != NULL) ? cJSON_GetObjectItem(result_item, "payload") : NULL;
        CHECK(payload != NULL);
        if (payload != NULL) {
            cJSON *gap = cJSON_GetObjectItem(payload, "source_gap");
            cJSON *state = cJSON_GetObjectItem(payload, "state");
            cJSON *state_str = cJSON_GetObjectItem(payload, "freshness");

            events = cJSON_GetObjectItem(payload, "events");
            CHECK(events != NULL && cJSON_GetArraySize(events) == 1);
            CHECK(gap != NULL && cJSON_IsNumber(gap) && gap->valueint == 2);
            CHECK(state != NULL && cJSON_IsString(state) &&
                  strcmp(state->valuestring, "gapped") == 0);
            CHECK(state_str != NULL && cJSON_IsString(state_str) &&
                  strcmp(state_str->valuestring, "fresh") == 0);
        }
    }
    cJSON_Delete(root);
    g_fake_live_now += 20000u;
    root = bridge_call("live.subscribe", 159, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result_item = cJSON_GetObjectItem(root, "result");

        payload = (result_item != NULL) ? cJSON_GetObjectItem(result_item, "payload") : NULL;
        CHECK(payload != NULL);
        if (payload != NULL) {
            cJSON *freshness = cJSON_GetObjectItem(payload, "freshness");
            CHECK(freshness != NULL && cJSON_IsString(freshness) &&
                  strcmp(freshness->valuestring, "stale") == 0);
        }
    }
    cJSON_Delete(root);
    g_fake_live_now += 50u;
    root = bridge_call("live.unsubscribe", 160, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        cJSON *result_item = cJSON_GetObjectItem(root, "result");

        payload = (result_item != NULL) ? cJSON_GetObjectItem(result_item, "payload") : NULL;
        CHECK(payload != NULL && cJSON_IsObject(payload));
        if (payload != NULL) {
            cJSON *active = cJSON_GetObjectItem(payload, "active");
            CHECK(active != NULL && cJSON_IsBool(active) && cJSON_IsFalse(active));
        }
    }
    cJSON_Delete(root);
    root = bridge_call("live.unsubscribe", 161, "\"payload\":{\"stream\":\"other\"}");
    CHECK(strcmp(outcome_of(root), "REJECTED") == 0);
    CHECK(strcmp(reason_of(root), "STREAM_UNKNOWN") == 0);
    cJSON_Delete(root);
    app_host_live_end(g_ctl_gen);
    root = bridge_call("live.subscribe", 162, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "UNAVAILABLE") == 0);
    CHECK(strcmp(reason_of(root), "LIVE_UNAVAILABLE") == 0);
    cJSON_Delete(root);
    reset_all();
}

static void test_live_target_class_paths(void)
{
    static const char active_json[] =
        "{\"active_config\":{\"target_class_name\":\"person\"}}";
    static const char legacy_json[] =
        "{\"config\":{\"target_class_name\":\"vehicle\"}}";
    static const char both_json[] =
        "{\"active_config\":{\"target_class_name\":\"person\"},"
        "\"config\":{\"target_class_name\":\"vehicle\"}}";
    const char *cases[3] = { active_json, legacy_json, both_json };
    const char *expect[3] = { "person", "vehicle", "person" };
    cJSON *root;
    cJSON *result;
    cJSON *payload;
    cJSON *target;
    uint8_t snap[256];
    uint32_t i;

    for (i = 0u; i < 3u; i++) {
        reset_all();
        install_active_sidecar();
        ctl_init_v4_fake();
        CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
        root = request_grant("[\"live.subscribe\"]");
        cJSON_Delete(root);
        live_backend_init();
        live_bind_grant_identity(g_ctl_gen, 64u);
        build_snapshot(snap, cases[i], 6u, 5u, 0u, 0u);
        snap[3] = 0x34u;
        CHECK(app_host_control_snapshot_submit(
                  snap, APP_CONTROL_HEADER_SIZE + (uint32_t)strlen(cases[i])) ==
              APP_CONTROL_OK);
        root = bridge_call("live.subscribe", 181u + i,
                           "\"payload\":{\"stream\":\"live\"}");
        CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
        result = cJSON_GetObjectItem(root, "result");
        CHECK(result != NULL);
        payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
        CHECK(payload != NULL);
        target = (payload != NULL) ? cJSON_GetObjectItem(payload, "target_class") : NULL;
        CHECK(target != NULL && cJSON_IsString(target) &&
              strcmp(target->valuestring, expect[i]) == 0);
        cJSON_Delete(root);
    }
    reset_all();
}

static void test_live_tracks_projection(void)
{
    cJSON *root;
    cJSON *result;
    cJSON *payload;
    cJSON *tracks;
    uint8_t packet[2048];
    uint32_t len;
    uint32_t offset;
    uint32_t i;

    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"live.subscribe\"]");
    cJSON_Delete(root);
    live_backend_init();
    live_bind_grant_identity(g_ctl_gen, 64u);
    len = live_header(packet, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u,
                      1u, 0u, 0u, 0u, 3u, 0u);
    offset = len;
    for (i = 0u; i < 3u; i++) {
        offset += live_track_record(packet + offset, 100u + i, 2u);
    }
    test_put_u32(packet + 8, offset);
    CHECK(app_host_live_ingest(packet, offset, g_ctl_gen, 2u) == APP_HOST_LIVE_OK);
    g_fake_live_now += 50u;
    root = bridge_call("live.subscribe", 171, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        result = cJSON_GetObjectItem(root, "result");
        CHECK(result != NULL);
        payload = cJSON_GetObjectItem(result, "payload");
        CHECK(payload != NULL);
        if (payload != NULL) {
            cJSON *truncated = cJSON_GetObjectItem(payload, "tracks_truncated");

            tracks = cJSON_GetObjectItem(payload, "tracks");
            CHECK(tracks != NULL && cJSON_IsArray(tracks) && cJSON_GetArraySize(tracks) == 3);
            if (tracks != NULL && cJSON_GetArraySize(tracks) == 3) {
                cJSON *track = cJSON_GetArrayItem(tracks, 0);
                cJSON *track_id = cJSON_GetObjectItem(track, "track_id");
                cJSON *points = cJSON_GetObjectItem(track, "points");
                cJSON *confidence = cJSON_GetObjectItem(track, "confidence_q16");
                CHECK(track_id != NULL && track_id->valueint == 100);
                CHECK(points != NULL && cJSON_IsArray(points) &&
                      cJSON_GetArraySize(points) == 2);
                if (points != NULL && cJSON_GetArraySize(points) == 2) {
                    cJSON *point = cJSON_GetArrayItem(points, 0);
                    cJSON *x = cJSON_GetObjectItem(point, "x_q16");
                    cJSON *y = cJSON_GetObjectItem(point, "y_q16");
                    CHECK(x != NULL && x->valueint == 0);
                    CHECK(y != NULL && y->valueint == 65535);
                }
                CHECK(confidence != NULL && confidence->valueint == 60000);
            }
            CHECK(truncated != NULL && cJSON_IsBool(truncated) && cJSON_IsFalse(truncated));
        }
    }
    cJSON_Delete(root);
    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"live.subscribe\"]");
    cJSON_Delete(root);
    live_backend_init();
    live_bind_grant_identity(g_ctl_gen, 64u);
    len = live_header(packet, APP_HOST_LIVE_KIND_TRACKS_SNAPSHOT, 0u,
                      1u, 0u, 0u, 0u, 20u, 0u);
    offset = len;
    for (i = 0u; i < 20u; i++) {
        offset += live_track_record(packet + offset, 200u + i, 1u);
    }
    test_put_u32(packet + 8, offset);
    CHECK(app_host_live_ingest(packet, offset, g_ctl_gen, 2u) == APP_HOST_LIVE_OK);
    g_fake_live_now += 50u;
    root = bridge_call("live.subscribe", 172, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    {
        result = cJSON_GetObjectItem(root, "result");
        CHECK(result != NULL);
        payload = cJSON_GetObjectItem(result, "payload");
        CHECK(payload != NULL);
        if (payload != NULL) {
            cJSON *truncated = cJSON_GetObjectItem(payload, "tracks_truncated");

            tracks = cJSON_GetObjectItem(payload, "tracks");
            CHECK(tracks != NULL && cJSON_IsArray(tracks) &&
                  cJSON_GetArraySize(tracks) == 16);
            CHECK(truncated != NULL && cJSON_IsBool(truncated) && cJSON_IsTrue(truncated));
        }
    }
    cJSON_Delete(root);
    reset_all();
}

static void submit_live_target_snapshot(const char *class_name)
{
    char json[160];
    uint8_t snap[256];

    (void)snprintf(json, sizeof(json),
                   "{\"active_config\":{\"target_class_name\":\"%s\"}}", class_name);
    build_snapshot(snap, json, 6u, 5u, 0u, 0u);
    snap[3] = 0x34u;
    CHECK(app_host_control_snapshot_submit(
        snap, APP_CONTROL_HEADER_SIZE + (uint32_t)strlen(json)) == APP_CONTROL_OK);
}

static void test_live_target_class_from_snapshot(void)
{
    cJSON *root;
    cJSON *result;
    cJSON *payload;
    cJSON *target;

    reset_all();
    install_active_sidecar();
    ctl_init_v4_fake();
    CHECK(ctl_session_open_v4(0xFFFFFFFFu) == g_ctl_gen);
    root = request_grant("[\"live.subscribe\"]");
    cJSON_Delete(root);
    live_backend_init();
    live_bind_grant_identity(g_ctl_gen, 64u);
    submit_live_target_snapshot("person");
    root = bridge_call("live.subscribe", 173, "\"payload\":{\"stream\":\"live\"}");
    CHECK(strcmp(outcome_of(root), "APPLIED") == 0);
    result = cJSON_GetObjectItem(root, "result");
    CHECK(result != NULL);
    payload = (result != NULL) ? cJSON_GetObjectItem(result, "payload") : NULL;
    CHECK(payload != NULL);
    target = (payload != NULL) ? cJSON_GetObjectItem(payload, "target_class") : NULL;
    CHECK(target != NULL && cJSON_IsString(target) &&
          strcmp(target->valuestring, "person") == 0);
    cJSON_Delete(root);
    reset_all();
}

int main(void)
{
    app_bridge_backend_t backend;

    setvbuf(stdout, NULL, _IONBF, 0);
    backend.now_ms = bridge_now;
    backend.sleep_ms = bridge_sleep;
    backend.epoch_ms = bridge_epoch;
    app_bridge_backend_set(&backend);
    CHECK(web_api_register_app_session_module() == AICAM_OK);
    CHECK(g_captured_route_count == 9);
    CHECK(find_route("GET", API_PATH_PREFIX "/app/sidecar/ui") != NULL);
    CHECK(find_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/grant") != NULL);
    CHECK(find_route("POST", API_PATH_PREFIX "/app/sidecar/bridge") != NULL);
    CHECK(find_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/confirm") != NULL);
    CHECK(find_route("POST", API_PATH_PREFIX "/app/sidecar/bridge/revoke") != NULL);
    CHECK(find_route("GET", API_PATH_PREFIX "/app/report/service") != NULL);
    test_grant_requires_active_sidecar();
    test_grant_issues_bound_identity();
    test_grant_rejects_unknown_capability();
    test_grant_full_slots_refuse_without_revoking_active();
    test_grant_reclaims_inactive_before_active();
    test_report_service_endpoint();
    test_grant_slides_expiry_on_activity();
    test_grant_expired_slot_preferred_over_evict();
    test_failed_grant_keeps_evictable_grant_alive();
    test_grant_valid_request_reclaims_inactive_slot();
    test_rng_failures_issue_zero_grants();
    test_call_rejects_unknown_grant_and_op();
    test_unavailable_ops();
    test_report_policy_bridge_paths();
    test_video_open_close_paths();
    test_video_open_starts_inactive_stream();
    test_snapshot_read_states();
    test_config_read_states();
    test_config_apply_paths();
    test_config_apply_writes_through_host_confidence();
    test_config_apply_native_failures_keep_host_confidence();
    test_config_apply_cached_replay_does_not_rewrite_host_confidence();
    test_config_apply_host_sync_failure_is_reported();
    test_config_apply_applied_result_echoes_reconciliation_payload();
    test_snapshot_read_serves_host_confidence_and_mirrors_blob();
    test_snapshot_read_tracks_host_confidence_change();
    test_snapshot_mirror_preserves_active_config_bytes();
    test_bridge_readthrough_ownership_stress();
    test_counters_reset_requires_admin_confirm();
    test_confirm_token_expiry_and_grant_binding();
    test_replay_and_generation_change();
    test_revoke_endpoint();
    test_sidecar_ui_projection();
    test_sidecar_projection_refresh_retention();
    test_sidecar_gate_rearm_after_invalidation();
    test_enable_read_states();
    test_enable_apply_gates();
    test_enable_apply_native_paths();
    test_live_bridge_paths();
    test_live_target_class_paths();
    test_live_tracks_projection();
    test_live_target_class_from_snapshot();
    printf("web_sidecar_bridge: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
