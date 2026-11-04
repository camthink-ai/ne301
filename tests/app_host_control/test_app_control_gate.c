#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "mongoose.h"
#include "web_server.h"
#include "web_api.h"
#include "api_app_control_module.h"
#include "aicam_types.h"
#include "app_host_control.h"

static int g_total;
static int g_fail;
static char g_last_response_kind[16];
static int g_last_response_code;
static char g_last_response_message[256];
static char g_last_response_data[8192];


static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d line=%d kind=[%s] code=%d msg=[%s] data=[%.400s]\n",
               g_total, line, g_last_response_kind, g_last_response_code,
               g_last_response_message, g_last_response_data);
    }
}

static void check_msg_impl(int cond, int line, const char *what)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d line=%d: %s\n", g_total, line, what);
    }
}

#define CHECK(cond) check_impl((cond), __LINE__)
#define CHECK_MSG(cond, what) check_msg_impl((cond), __LINE__, (what))

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx);
aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data,
                                    const char *message);
aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code,
                                  const char *message);
aicam_result_t http_server_register_route(const api_route_t *route);

aicam_bool_t web_server_high_privilege_allowed(http_handler_context_t *ctx)
{
    if (ctx == NULL) {
        return AICAM_FALSE;
    }
    return (aicam_bool_t)((ctx->authenticated == AICAM_TRUE &&
                           ctx->high_privilege == AICAM_TRUE)
                              ? AICAM_TRUE
                              : AICAM_FALSE);
}

aicam_result_t api_response_success(http_handler_context_t *ctx, const char *data,
                                    const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "success");
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    g_last_response_data[0] = '\0';
    if (data != NULL) {
        snprintf(g_last_response_data, sizeof(g_last_response_data), "%s", data);
        free((void *)data);
    }
    return AICAM_OK;
}

aicam_result_t api_response_error(http_handler_context_t *ctx, api_error_code_t error_code,
                                  const char *message)
{
    (void)ctx;
    snprintf(g_last_response_kind, sizeof(g_last_response_kind), "error");
    g_last_response_code = (int)error_code;
    snprintf(g_last_response_message, sizeof(g_last_response_message), "%s",
             message ? message : "");
    return AICAM_ERROR;
}

#define MAX_CAPTURED_ROUTES 8
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

static volatile uint32_t g_now_ms;

static uint32_t fake_now_ms(void *ctx)
{
    (void)ctx;
    return g_now_ms;
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

static uint32_t g_gate_session_gen;

static void ctl_init_fake(void)
{
    app_control_backend_t b;

    memset(&b, 0, sizeof(b));
    b.now_ms = fake_now_ms;
    b.mutex_create = fake_mutex_create;
    b.mutex_destroy = fake_mutex_destroy;
    b.mutex_lock = fake_mutex_lock;
    b.mutex_unlock = fake_mutex_unlock;
    app_host_control_init(&b);
}

static int32_t ctl_session_open_helper(uint32_t caps, uint32_t control_max,
                                       uint32_t snapshot_max, uint32_t *gen)
{
    app_control_session_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.caps = caps;
    cfg.control_max = control_max;
    cfg.snapshot_max = snapshot_max;
    strcpy(cfg.publisher_id, "gate-publisher");
    strcpy(cfg.app_id, "gate-app");
    return app_host_control_session_open(&cfg, gen);
}

static uint32_t gen_val(void)
{
    return g_gate_session_gen;
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

static char g_body_buf[16384];

static aicam_result_t invoke_route(const char *method, const char *path,
                                   const char *body, aicam_bool_t auth,
                                   aicam_bool_t high_priv)
{
    const api_route_t *route = find_route(method, path);
    http_handler_context_t ctx;

    if (route == NULL) {
        return AICAM_ERROR;
    }
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/json");
    if (body != NULL) {
        snprintf(g_body_buf, sizeof(g_body_buf), "%s", body);
        ctx.request.body = g_body_buf;
    } else {
        ctx.request.body = NULL;
    }
    ctx.authenticated = auth;
    ctx.high_privilege = high_priv;
    return route->handler(&ctx);
}

static aicam_result_t invoke_route_with_method(const char *wire_method,
                                               const char *route_method,
                                               const char *path, const char *body,
                                               aicam_bool_t auth, aicam_bool_t high_priv)
{
    const api_route_t *route = find_route(route_method, path);
    http_handler_context_t ctx;

    if (route == NULL) {
        return AICAM_ERROR;
    }
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", wire_method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s",
             "application/json");
    if (body != NULL) {
        snprintf(g_body_buf, sizeof(g_body_buf), "%s", body);
        ctx.request.body = g_body_buf;
    } else {
        ctx.request.body = NULL;
    }
    ctx.authenticated = auth;
    ctx.high_privilege = high_priv;
    return route->handler(&ctx);
}

static aicam_result_t post_config(const char *body)
{
    return invoke_route("POST", API_PATH_PREFIX "/app/control/config", body,
                        AICAM_TRUE, AICAM_TRUE);
}

static void reset_response(void)
{
    g_last_response_kind[0] = '\0';
    g_last_response_code = -1;
    g_last_response_message[0] = '\0';
    g_last_response_data[0] = '\0';
}

static size_t b64_encode_bytes(const uint8_t *src, size_t len, char *dst, size_t cap)
{
    return mg_base64_encode((const unsigned char *)src, len, dst, cap);
}

static void fill_seeded(uint8_t *buf, size_t len, uint8_t seed)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        buf[i] = (uint8_t)(seed + (uint8_t)i);
    }
}

static void post_config_expect_invalid(const char *b64)
{
    char body[16384];

    snprintf(body, sizeof(body),
             "{\"request_id\":1,\"expected_config_generation\":1,\"payload_base64\":\"%s\"}",
             b64);
    reset_response();
    CHECK(post_config(body) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
}

static void test_route_table(void)
{
    g_captured_route_count = 0;
    CHECK(web_api_register_app_control_module() == AICAM_OK);
    CHECK(g_captured_route_count == 4);
    CHECK(find_route("POST", API_PATH_PREFIX "/app/control/config") != NULL);
    CHECK(find_route("POST", API_PATH_PREFIX "/app/control/reset") != NULL);
    CHECK(find_route("GET", API_PATH_PREFIX "/app/control/status") != NULL);
    CHECK(find_route("GET", API_PATH_PREFIX "/app/business/snapshot") != NULL);
}

static void test_gate_rejects_before_body(void)
{
    const api_route_t *cfg = find_route("POST", API_PATH_PREFIX "/app/control/config");
    const api_route_t *snap = find_route("GET", API_PATH_PREFIX "/app/business/snapshot");

    CHECK(cfg != NULL && snap != NULL);

    reset_response();
    CHECK(invoke_route_with_method("POST", "GET",
                                   API_PATH_PREFIX "/app/control/status", "{}",
                                   AICAM_TRUE, AICAM_TRUE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    reset_response();
    CHECK(invoke_route("POST", API_PATH_PREFIX "/app/control/config",
                       "{\"request_id\":1,\"expected_config_generation\":1}",
                       AICAM_FALSE, AICAM_FALSE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(strstr(g_last_response_message, "High-privilege") != NULL);

    reset_response();
    CHECK(invoke_route("POST", API_PATH_PREFIX "/app/control/config",
                       "{\"request_id\":1,\"expected_config_generation\":1}",
                       AICAM_TRUE, AICAM_FALSE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    reset_response();
    CHECK(invoke_route("GET", API_PATH_PREFIX "/app/business/snapshot", NULL,
                       AICAM_TRUE, AICAM_FALSE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    reset_response();
    CHECK(invoke_route("POST", API_PATH_PREFIX "/app/control/config", "not-json",
                       AICAM_TRUE, AICAM_TRUE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    reset_response();
    CHECK(invoke_route("POST", API_PATH_PREFIX "/app/control/config",
                       "{\"request_id\":1}", AICAM_TRUE, AICAM_TRUE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
    CHECK(strstr(g_last_response_message, "Invalid control request fields") != NULL);
}

static void test_config_base64_boundaries(void)
{
    uint32_t gen = 0u;
    char long_b64[768];
    size_t i;

    ctl_init_fake();
    g_now_ms = 0u;
    CHECK(ctl_session_open_helper(0xC0u, 512u, 2048u, &gen) == APP_CONTROL_OK);

    post_config_expect_invalid("!!!!");
    post_config_expect_invalid("QQ==QUJD");
    post_config_expect_invalid("====");
    post_config_expect_invalid("QUJD=");
    post_config_expect_invalid("");
    post_config_expect_invalid("QQ=");

    for (i = 0u; i < 641u; i++) {
        long_b64[i] = 'A';
    }
    long_b64[641] = '\0';
    post_config_expect_invalid(long_b64);

    for (i = 0u; i < 688u; i++) {
        long_b64[i] = 'A';
    }
    long_b64[688] = '\0';
    post_config_expect_invalid(long_b64);

    for (i = 0u; i < 684u; i++) {
        long_b64[i] = 'A';
    }
    long_b64[684] = '\0';
    post_config_expect_invalid(long_b64);

    app_host_control_session_close();
}

static void gate_deliver_and_receipt(uint32_t session_gen, uint32_t request_id,
                                     uint32_t config_gen)
{
    uint8_t rx[APP_CONTROL_MSG_RAM_MAX];
    uint32_t rx_len = 0u;
    uint8_t receipt[APP_CONTROL_HEADER_SIZE];

    CHECK(app_host_control_deliver(rx, sizeof(rx), &rx_len) == APP_CONTROL_OK);
    CHECK(rx_len >= APP_CONTROL_HEADER_SIZE);
    memset(receipt, 0, sizeof(receipt));
    receipt[0] = 0x4Eu;
    receipt[1] = 0x46u;
    receipt[2] = 0x43u;
    receipt[3] = 0x33u;
    receipt[4] = 0x01u;
    receipt[6] = (uint8_t)APP_CONTROL_MSG_TYPE_RECEIPT;
    receipt[8] = (uint8_t)(APP_CONTROL_HEADER_SIZE & 0xFFu);
    receipt[12] = (uint8_t)(session_gen & 0xFFu);
    receipt[13] = (uint8_t)((session_gen >> 8) & 0xFFu);
    receipt[14] = (uint8_t)((session_gen >> 16) & 0xFFu);
    receipt[15] = (uint8_t)((session_gen >> 24) & 0xFFu);
    receipt[16] = (uint8_t)(request_id & 0xFFu);
    receipt[17] = (uint8_t)((request_id >> 8) & 0xFFu);
    receipt[18] = (uint8_t)((request_id >> 16) & 0xFFu);
    receipt[19] = (uint8_t)((request_id >> 24) & 0xFFu);
    receipt[20] = (uint8_t)(config_gen & 0xFFu);
    receipt[21] = (uint8_t)((config_gen >> 8) & 0xFFu);
    receipt[22] = (uint8_t)((config_gen >> 16) & 0xFFu);
    receipt[23] = (uint8_t)((config_gen >> 24) & 0xFFu);
    receipt[25] = 0x01u;
    CHECK(app_host_control_receipt(receipt, APP_CONTROL_HEADER_SIZE) == APP_CONTROL_OK);
}

static void submit_b64_expect(const char *b64, uint32_t request_id,
                              const char *expect_status, const char *expect_reason,
                              int expect_payload_len)
{
    char body[16384];
    char expect[128];

    snprintf(body, sizeof(body),
             "{\"request_id\":%u,\"expected_config_generation\":1,"
             "\"payload_base64\":\"%s\"}",
             request_id, b64);
    reset_response();
    CHECK(post_config(body) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    snprintf(expect, sizeof(expect), "\"status\":\"%s\"", expect_status);
    CHECK_MSG(strstr(g_last_response_data, expect) != NULL, expect_status);
    if (expect_reason != NULL) {
        snprintf(expect, sizeof(expect), "\"reason\":\"%s\"", expect_reason);
        CHECK_MSG(strstr(g_last_response_data, expect) != NULL, expect_reason);
    }
    if (expect_payload_len >= 0) {
        snprintf(expect, sizeof(expect), "\"payload_len\":%d", expect_payload_len);
        CHECK_MSG(strstr(g_last_response_data, expect) != NULL, "payload_len_echo");
    }
}

static void test_config_registered_lifecycles(void)
{
    uint8_t payload480[480];
    uint8_t payload481[481];
    uint8_t payload512[512];
    static char b480[1024];
    static char b481[1024];
    static char b512[1024];
    static char bsmall[64];

    fill_seeded(payload480, sizeof(payload480), 1u);
    fill_seeded(payload481, sizeof(payload481), 2u);
    fill_seeded(payload512, sizeof(payload512), 3u);
    CHECK(b64_encode_bytes(payload480, sizeof(payload480), b480, sizeof(b480)) > 0u);
    CHECK(b64_encode_bytes(payload481, sizeof(payload481), b481, sizeof(b481)) > 0u);
    CHECK(b64_encode_bytes(payload512, sizeof(payload512), b512, sizeof(b512)) > 0u);
    CHECK(b64_encode_bytes((const uint8_t *)"{\"cfg\":1}", 9u, bsmall, sizeof(bsmall)) > 0u);

    submit_b64_expect(b512, 51u, "refused", "QUOTA_EXCEEDED", -1);
    submit_b64_expect(b481, 52u, "refused", "QUOTA_EXCEEDED", -1);
    submit_b64_expect(bsmall, 1u, "registered", NULL, 9);
    submit_b64_expect(b480, 2u, "refused", "BUSY", -1);
    gate_deliver_and_receipt(gen_val(), 1u, 1u);
    submit_b64_expect(b480, 2u, "registered", NULL, 480);
    submit_b64_expect(b480, 2u, "already_registered", NULL, 480);
    gate_deliver_and_receipt(gen_val(), 2u, 1u);
}

static void post_reset_expect(const char *body, const char *expect_status,
                              const char *expect_reason)
{
    reset_response();
    CHECK(invoke_route("POST", API_PATH_PREFIX "/app/control/reset", body,
                       AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK(strstr(g_last_response_data, expect_status) != NULL);
    if (expect_reason != NULL) {
        CHECK(strstr(g_last_response_data, expect_reason) != NULL);
    }
}

static void test_config_reset_path(void)
{
    char body[256];

    snprintf(body, sizeof(body),
             "{\"request_id\":9,\"expected_config_generation\":1,"
             "\"destructive_confirmed\":true}");
    post_reset_expect(body, "\"status\":\"registered\"", NULL);
    CHECK(strstr(g_last_response_data, "\"msg_type\":\"counters_reset\"") != NULL);
    gate_deliver_and_receipt(gen_val(), 9u, 1u);
    post_reset_expect(body, "\"status\":\"cached\"", NULL);

    snprintf(body, sizeof(body),
             "{\"request_id\":10,\"expected_config_generation\":1}");
    post_reset_expect(body, "\"status\":\"refused\"", "\"reason\":\"UNCONFIRMED\"");
}

static void test_status_and_snapshot_entries(void)
{
    uint8_t snap[APP_CONTROL_HEADER_SIZE + 32u];
    uint32_t total;

    reset_response();
    CHECK(invoke_route("GET", API_PATH_PREFIX "/app/control/status", NULL,
                       AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"session_active\":true") != NULL);
    CHECK(strstr(g_last_response_data, "\"session_generation\":") != NULL);
    CHECK(strstr(g_last_response_data, "\"known_config_generation\":1") != NULL);

    memset(snap, 0, sizeof(snap));
    snap[0] = 0x4Eu;
    snap[1] = 0x46u;
    snap[2] = 0x43u;
    snap[3] = 0x33u;
    snap[4] = 0x01u;
    snap[6] = (uint8_t)APP_CONTROL_MSG_TYPE_SNAPSHOT;
    total = APP_CONTROL_HEADER_SIZE + 7u;
    snap[8] = (uint8_t)(total & 0xFFu);
    snap[20] = 0x01u;
    snap[24] = 0x02u;
    memcpy(snap + APP_CONTROL_HEADER_SIZE, "{\"k\":1}", 7u);
    CHECK(app_host_control_snapshot_submit(snap, total) == APP_CONTROL_OK);

    reset_response();
    CHECK(invoke_route("GET", API_PATH_PREFIX "/app/business/snapshot", NULL,
                       AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"availability\":\"fresh\"") != NULL);
    CHECK(strstr(g_last_response_data, "\"payload\":{\"k\":1}") != NULL);
    CHECK(strstr(g_last_response_data, "\"payload_len\":7") != NULL);
    CHECK(strstr(g_last_response_data, "\"publisher_id\":\"gate-publisher\"") != NULL);
    CHECK(strstr(g_last_response_data, "\"app_id\":\"gate-app\"") != NULL);

    reset_response();
    CHECK(invoke_route("GET", API_PATH_PREFIX "/app/business/snapshot", NULL,
                       AICAM_TRUE, AICAM_FALSE) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    app_host_control_session_close();
    reset_response();
    CHECK(invoke_route("GET", API_PATH_PREFIX "/app/business/snapshot", NULL,
                       AICAM_TRUE, AICAM_TRUE) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"availability\":\"unavailable\"") != NULL);

    reset_response();
    CHECK(post_config("{\"request_id\":11,\"expected_config_generation\":1,"
                      "\"payload_base64\":\"QQ==\"}") == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"status\":\"refused\"") != NULL);
    CHECK(strstr(g_last_response_data, "\"reason\":\"UNAUTHORIZED\"") != NULL);
}

int main(void)
{
    test_route_table();
    test_gate_rejects_before_body();
    test_config_base64_boundaries();

    ctl_init_fake();
    g_now_ms = 0u;
    CHECK(ctl_session_open_helper(0xC0u, 512u, 2048u, &g_gate_session_gen) ==
          APP_CONTROL_OK);
    test_config_registered_lifecycles();
    test_config_reset_path();
    test_status_and_snapshot_entries();

    printf("app_control_gate: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
