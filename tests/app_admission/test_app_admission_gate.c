#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "mongoose.h"
#include "web_server.h"
#include "api_app_admission_module.h"
#include "aicam_types.h"
#include "app_admission.h"
#include "app_trust_anchor.h"
#include "anchor_vectors.h"
#include "p2_vectors.h"

static int g_total;
static int g_fail;

static char g_last_response_kind[16];
static int g_last_response_code;
static char g_last_response_message[256];
static char g_last_response_data[4096];

static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL gate:%d line=%d code=%d kind=%s msg=\"%.60s\" data=\"%.60s\"\n",
               g_total, line, (int)g_last_response_code, g_last_response_kind,
               g_last_response_message, g_last_response_data);
    }
}
#define CHECK(cond) check_impl((cond), __LINE__)

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

static aicam_bool_t g_armed_state;

aicam_bool_t app_host_experiment_armed(void)
{
    return g_armed_state;
}

static int g_routes_registered;

aicam_result_t http_server_register_route(const api_route_t *route)
{
    (void)route;
    g_routes_registered++;
    return AICAM_OK;
}

static http_handler_context_t make_ctx(const char *method, const char *body)
{
    static char body_buf[8192];
    http_handler_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    memset(body_buf, 0, sizeof(body_buf));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    if (body) {
        snprintf(body_buf, sizeof(body_buf), "%s", body);
        ctx.request.body = body_buf;
    }
    return ctx;
}

static http_handler_context_t make_ctx_heap(const char *method, char *body)
{
    http_handler_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    ctx.request.body = body;
    return ctx;
}

static void reset_response(void)
{
    g_last_response_kind[0] = '\0';
    g_last_response_code = -1;
    g_last_response_message[0] = '\0';
    g_last_response_data[0] = '\0';
}

static size_t b64_encode(const uint8_t *src, size_t len, char *dst, size_t cap)
{
    return mg_base64_encode((const unsigned char *)src, len, dst, cap);
}

static void make_golden_b64(char *out, size_t cap)
{
    size_t n = b64_encode(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, out, cap);
    CHECK(n > 0);
    CHECK(n < cap);
}

static void test_verify_gate(void)
{
    http_handler_context_t ctx;
    char b64[8192];
    char body[8700];

    ctx = make_ctx("GET", NULL);
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_ctx("POST", "{\"package_base64\":\"AAAA\"}");
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(strstr(g_last_response_message, "High-privilege") != NULL);

    ctx = make_ctx("POST", "{\"package_base64\":\"AAAA\"}");
    ctx.authenticated = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"verdict\":\"refused\"") != NULL);

    make_golden_b64(b64, sizeof(b64));
    snprintf(body, sizeof(body), "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx("POST", body);
    ctx.authenticated = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"verdict\"") != NULL);

    ctx = make_ctx("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK(strstr(g_last_response_data, "\"verdict\":\"refused\"") != NULL);
    CHECK(strstr(g_last_response_data, "PUBLISHER_UNTRUSTED") != NULL);
    CHECK(strstr(g_last_response_data, "hello-app") == NULL);
    CHECK_MSG(strstr(g_last_response_data, "\"executable\":false") != NULL, "executable_false");
    CHECK_MSG(strstr(g_last_response_data, "\"installed\":false") != NULL, "installed_false");
}

static void test_verdict_json_trusted_shape(void)
{
    app_admission_verdict_t verdict;
    cJSON *json;
    char *text;

    memset(&verdict, 0, sizeof(verdict));
    snprintf(verdict.publisher_id, sizeof(verdict.publisher_id), "%s", "ne301-exp-pub");
    snprintf(verdict.app_id, sizeof(verdict.app_id), "%s", "demo-app");
    verdict.version_major = 1u;
    verdict.version_minor = 2u;
    verdict.version_patch = 3u;
    verdict.native_image_size = 600u;
    verdict.tbs_len = 824u;
    verdict.signature_len = 71u;
    verdict.tbs_sha256[0] = 0xABu;
    verdict.image_sha256[0] = 0xCDu;

    json = app_admission_verdict_json(&verdict, APP_ADMISSION_OK);
    CHECK(json != NULL);
    text = cJSON_PrintUnformatted(json);
    CHECK(text != NULL);
    CHECK(strstr(text, "\"verdict\":\"trusted_compatible\"") != NULL);
    CHECK(strstr(text, "\"publisher_id\":\"ne301-exp-pub\"") != NULL);
    CHECK(strstr(text, "\"app_id\":\"demo-app\"") != NULL);
    CHECK(strstr(text, "\"version_major\":1") != NULL);
    CHECK(strstr(text, "\"executable\":false") != NULL);
    CHECK(strstr(text, "\"installed\":false") != NULL);
    CHECK(strstr(text, "ab") != NULL);

    cJSON_free(text);
    cJSON_Delete(json);
}

static void test_verify_gate_predicate(void)
{
    http_handler_context_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    CHECK_MSG(web_server_high_privilege_allowed(NULL) == AICAM_FALSE, "pred_null");
    CHECK_MSG(web_server_high_privilege_allowed(&ctx) == AICAM_FALSE, "pred_zeroed");

    ctx.authenticated = AICAM_TRUE;
    CHECK_MSG(web_server_high_privilege_allowed(&ctx) == AICAM_FALSE, "pred_auth_only");

    ctx.authenticated = AICAM_FALSE;
    ctx.high_privilege = AICAM_TRUE;
    CHECK(web_server_high_privilege_allowed(&ctx) == AICAM_FALSE);

    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    CHECK(web_server_high_privilege_allowed(&ctx) == AICAM_TRUE);
}

static void test_verify_payload_negatives(void)
{
    http_handler_context_t ctx;

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "not-json");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "{\"other\":1}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "{\"package_base64\":\"\"}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_ctx("POST", "{\"package_base64\":\"!!!!not-base64!!!!\"}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    {
        char *huge;
        char *body;
        size_t i;
        huge = malloc(2900000u);
        body = malloc(2900100u);
        CHECK(huge != NULL && body != NULL);
        if (huge != NULL && body != NULL) {
            for (i = 0; i < 2899999u; i++) {
                huge[i] = 'A';
            }
            huge[2899999u] = '\0';
            snprintf(body, 2900100u, "{\"package_base64\":\"%s\"}", huge);
            ctx = make_ctx_heap("POST", body);
            ctx.authenticated = AICAM_TRUE;
            ctx.high_privilege = AICAM_TRUE;
            reset_response();
            CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
            CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
            free(huge);
            free(body);
        }
    }
}

static void test_verify_refusals_high_privilege(void)
{
    http_handler_context_t ctx;
    uint8_t tampered[320];
    char b64[8192];
    char body[8700];

    memcpy(tampered, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    tampered[70] ^= 0x01u;
    b64_encode(tampered, P2_GOLDEN_PACKAGE_LEN, b64, sizeof(b64));
    snprintf(body, sizeof(body), "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"verdict\":\"refused\"") != NULL);
    CHECK_MSG(strstr(g_last_response_data, "PUBLISHER_UNTRUSTED") != NULL, "tamper_untrusted_anchor");
    CHECK(strstr(g_last_response_data, "\"executable\":true") == NULL);

    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "PUBLISHER_UNTRUSTED") != NULL);
}

static void test_verify_gate_anchor_positive(void)
{
    http_handler_context_t ctx;
    char b64[1024];
    char body[1200];
    size_t n;

    n = b64_encode(ANCHOR_POSITIVE_PACKAGE, ANCHOR_POSITIVE_PACKAGE_LEN, b64, sizeof(b64));
    CHECK(n > 0);
    CHECK(n < sizeof(b64));
    snprintf(body, sizeof(body), "{\"package_base64\":\"%s\"}", b64);

    ctx = make_ctx("POST", body);
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    ctx = make_ctx("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK_MSG(strstr(g_last_response_data, "\"verdict\":\"trusted_compatible\"") != NULL,
              "gate_anchor_positive_verdict");
    CHECK_MSG(strstr(g_last_response_data, "\"publisher_id\":\"ne301-exp-pub\"") != NULL,
              "gate_anchor_positive_publisher");
    CHECK_MSG(strstr(g_last_response_data, "\"app_id\":\"anchor-fixture\"") != NULL,
              "gate_anchor_positive_app_id");
    CHECK_MSG(strstr(g_last_response_data, "\"executable\":false") != NULL,
              "gate_anchor_positive_not_executable");
    CHECK_MSG(strstr(g_last_response_data, "\"installed\":false") != NULL,
              "gate_anchor_positive_not_installed");

    n = b64_encode(ANCHOR_SUPERSEDED_PACKAGE, ANCHOR_SUPERSEDED_PACKAGE_LEN, b64, sizeof(b64));
    CHECK(n > 0);
    snprintf(body, sizeof(body), "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"verdict\":\"refused\"") != NULL,
              "gate_anchor_superseded_refused");
    CHECK_MSG(strstr(g_last_response_data, "PUBLISHER_UNTRUSTED") != NULL,
              "gate_anchor_superseded_reason");
}

static void test_state_gate(void)
{
    http_handler_context_t ctx;

    ctx = make_ctx("POST", "{}");
    reset_response();
    CHECK(app_admission_state_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_ctx("GET", NULL);
    reset_response();
    CHECK(app_admission_state_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    ctx = make_ctx("GET", NULL);
    ctx.authenticated = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_state_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);

    g_armed_state = AICAM_FALSE;
    ctx = make_ctx("GET", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_state_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK_MSG(strstr(g_last_response_data, "\"install\":\"not_supported\"") != NULL, "install_not_supported");
    CHECK(strstr(g_last_response_data, "\"execute\":\"not_supported_by_admission\"") != NULL);
    CHECK_MSG(strstr(g_last_response_data, "\"uart_loader_armed\":false") != NULL, "armed_false");
    CHECK(strstr(g_last_response_data, "\"status\":\"provisioned\"") != NULL);
    CHECK(strstr(g_last_response_data, "ne301-exp-pub") != NULL);
    CHECK(strstr(g_last_response_data, "\"profile\":\"test\"") != NULL);
    CHECK(strstr(g_last_response_data,
                 "\"spki_sha256\":\"06e732b1b94daeeef433297f7a8e797bf72b2fd3b882a4ec6a5490352931ebda\"") != NULL);
    CHECK(strstr(g_last_response_data,
                 "\"provenance\":\"TEST_ONLY:compiled-ne301-exp-pub\"") != NULL);

    g_armed_state = AICAM_TRUE;
    ctx = make_ctx("GET", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_state_handler(&ctx) == AICAM_OK);
    CHECK(strstr(g_last_response_data, "\"uart_loader_armed\":true") != NULL);
    g_armed_state = AICAM_FALSE;
}

static void test_route_table_registered(void)
{
    g_routes_registered = 0;
    CHECK(web_api_register_app_admission_module() == AICAM_OK);
    CHECK(g_routes_registered == 2);
}

static void fill_length_probe(uint8_t *pkg, size_t total_len, uint8_t ver_byte)
{
    uint32_t image_len = APP_ADMISSION_MAX_NATIVE_FILE_LEN;

    memset(pkg, 0, total_len);
    pkg[0] = 0x4Eu;
    pkg[1] = 0x45u;
    pkg[2] = 0x41u;
    pkg[3] = 0x50u;
    pkg[4] = 0x50u;
    pkg[5] = ver_byte;
    pkg[8] = (ver_byte == 0x02u) ? 192u : 160u;
    pkg[12] = (uint8_t)(image_len & 0xFFu);
    pkg[13] = (uint8_t)((image_len >> 8) & 0xFFu);
    pkg[14] = (uint8_t)((image_len >> 16) & 0xFFu);
    pkg[15] = (uint8_t)((image_len >> 24) & 0xFFu);
}

static void test_v2_protocol_length_gate(void)
{
    http_handler_context_t ctx;
    uint8_t *pkg;
    uint8_t *pkg_over;
    char *b64;
    char *body;
    size_t total = (size_t)APP_ADMISSION_MAX_PACKAGE_LEN_V2;
    size_t b64_cap = ((total + 1u) / 3u + 1u) * 4u + 32u;

    pkg = malloc(total);
    pkg_over = malloc(total + 1u);
    b64 = malloc(b64_cap);
    body = malloc(b64_cap + 64u);
    CHECK(pkg != NULL && pkg_over != NULL && b64 != NULL && body != NULL);
    if (pkg == NULL || pkg_over == NULL || b64 == NULL || body == NULL) {
        free(pkg);
        free(pkg_over);
        free(b64);
        free(body);
        return;
    }

    fill_length_probe(pkg, total, 0x02u);
    CHECK(b64_encode(pkg, total, b64, b64_cap) > 0);
    snprintf(body, b64_cap + 64u, "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx_heap("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK_MSG(strcmp(g_last_response_kind, "success") == 0, "v2_max_len_passes_entrance");
    CHECK_MSG(strstr(g_last_response_data, "\"verdict\":\"refused\"") != NULL,
              "v2_max_len_refused_by_admission");

    fill_length_probe(pkg, total, 0x01u);
    CHECK(b64_encode(pkg, total, b64, b64_cap) > 0);
    snprintf(body, b64_cap + 64u, "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx_heap("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_OK);
    CHECK_MSG(strstr(g_last_response_data, "\"reason\":\"BAD_PACKAGE\"") != NULL,
              "v1_magic_over_v1_limit_refused_by_v1_parser");

    fill_length_probe(pkg_over, total + 1u, 0x02u);
    CHECK(b64_encode(pkg_over, total + 1u, b64, b64_cap) > 0);
    snprintf(body, b64_cap + 64u, "{\"package_base64\":\"%s\"}", b64);
    ctx = make_ctx_heap("POST", body);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    reset_response();
    CHECK(app_admission_verify_handler(&ctx) == AICAM_ERROR);
    CHECK_MSG(g_last_response_code == API_ERROR_INVALID_REQUEST, "v2_over_limit_fail_closed");
    CHECK(strstr(g_last_response_message, "protocol size limit") != NULL);

    free(pkg);
    free(pkg_over);
    free(b64);
    free(body);
}

int main(void)
{
    test_verify_gate();
    test_verdict_json_trusted_shape();
    test_verify_gate_predicate();
    test_verify_payload_negatives();
    test_verify_refusals_high_privilege();
    test_verify_gate_anchor_positive();
    test_state_gate();
    test_route_table_registered();
    test_v2_protocol_length_gate();

    printf("app_admission_gate: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
