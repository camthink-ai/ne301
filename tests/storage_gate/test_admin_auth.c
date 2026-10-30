#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <setjmp.h>
#include "fake_flash.h"
#include "storage.h"
#include "json_config_mgr.h"
#include "json_config_internal.h"
#include "json_config_boot_gate.h"
#include "auth_mgr.h"
#include "api_auth_module.h"
#include "api_device_module.h"
#include "web_api.h"
#include "web_server.h"
#include "aicam_types.h"

extern fake_flash_t g_e2e_user_ff;
extern bool g_user_io_fail;
extern void e2e_storage_reset(void);
extern int e2e_storage_boot(void);
extern void e2e_reset_response(void);
extern char g_last_response_kind[16];
extern int g_last_response_code;
extern char g_last_response_message[512];
extern bool g_service_running;
extern void e2e_arm_key_read_fail(const char *key, int nth_hit);
extern void e2e_arm_key_write_fail(const char *key, int nth_hit);
extern void e2e_disarm_key_fail(void);
extern jmp_buf g_e2e_powerloss_env;
extern void e2e_arm_powerloss(int nth_user_op, int drop_before_op);
extern void e2e_disarm_powerloss(void);

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define ADMIN_PASSWORD_MAX 64

static void boot_fresh(void)
{
    e2e_storage_reset();
    CHECK(e2e_storage_boot() == 0);
}

static void run_config_and_auth(void)
{
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
}

static uint8_t g_snapshot_user[8 * 4096];

static void snapshot_user(void)
{
    memcpy(g_snapshot_user, g_e2e_user_ff.mem, sizeof(g_snapshot_user));
}

static int user_bytes_changed(void)
{
    return memcmp(g_snapshot_user, g_e2e_user_ff.mem, sizeof(g_snapshot_user)) != 0;
}

static http_handler_context_t make_json_ctx(const char *method, const char *body)
{
    http_handler_context_t ctx;
    static char body_buf[512];
    memset(&ctx, 0, sizeof(ctx));
    memset(body_buf, 0, sizeof(body_buf));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    snprintf(ctx.request.content_type, sizeof(ctx.request.content_type), "%s", "application/json");
    if (body) {
        strncpy(body_buf, body, sizeof(body_buf) - 1);
        ctx.request.body = body_buf;
    }
    return ctx;
}

static void legacy_weak_hash(const char *password, char *hex_out)
{
    static const char *hex_chars = "0123456789abcdef";
    size_t len = strlen(password);
    uint32_t hash_val = 0x12345678u;
    uint8_t hash[32];
    memset(hash, 0, sizeof(hash));
    for (size_t i = 0; i < len; i++) {
        hash_val = hash_val * 33u + (uint8_t)password[i];
        hash_val ^= (hash_val >> 16);
    }
    for (int i = 0; i < 32; i += 4) {
        hash[i] = (uint8_t)(hash_val >> 24);
        hash[i + 1] = (uint8_t)(hash_val >> 16);
        hash[i + 2] = (uint8_t)(hash_val >> 8);
        hash[i + 3] = (uint8_t)(hash_val);
        hash_val = hash_val * 1664525u + 1013904223u;
    }
    for (int i = 0; i < 32; i++) {
        hex_out[i * 2] = hex_chars[hash[i] >> 4];
        hex_out[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    hex_out[64] = '\0';
}

static void test_credential_record_codec(void)
{
    char record[AUTH_CREDENTIAL_RECORD_MAX];
    char small[8];

    CHECK(auth_mgr_credential_record_encode("codec-pw-123", record, sizeof(record)) == AICAM_OK);
    CHECK(auth_mgr_credential_is_record(record) == AICAM_TRUE);
    CHECK(strlen(record) == 100);
    CHECK(strncmp(record, "A1:", 3) == 0);
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-124") == AICAM_FALSE);
    CHECK(auth_mgr_credential_record_verify(record, "") == AICAM_FALSE);

    record[10] ^= 0x01;
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-123") == AICAM_FALSE);
    record[10] ^= 0x01;
    CHECK(auth_mgr_credential_record_verify(record, "codec-pw-123") == AICAM_TRUE);

    CHECK(auth_mgr_credential_record_encode("codec-pw-123", small, sizeof(small)) == AICAM_ERROR_INVALID_PARAM);
    CHECK(auth_mgr_credential_record_encode("", record, sizeof(record)) == AICAM_ERROR_INVALID_PARAM);
    CHECK(auth_mgr_credential_is_record("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_credential_is_record("A1:short") == AICAM_FALSE);
    CHECK(auth_mgr_credential_is_record("") == AICAM_FALSE);
    CHECK(auth_mgr_credential_is_record(NULL) == AICAM_FALSE);
    CHECK(auth_mgr_credential_record_verify("hicamthink", "hicamthink") == AICAM_FALSE);
}

static void test_default_password_persisted_as_record(void)
{
    char credential[AUTH_CREDENTIAL_RECORD_MAX];
    char raw[128];
    int raw_len;

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_mgr_config_healthy() == AICAM_TRUE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) == AICAM_OK);
    CHECK(strncmp(credential, "A1:", 3) == 0);
    memset(raw, 0, sizeof(raw));
    raw_len = storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1);
    CHECK(raw_len == 101);
    CHECK(strncmp(raw, "A1:", 3) == 0);
    CHECK(strstr(raw, "hicamthink") == NULL);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_export_contains_no_admin_secret(void)
{
    aicam_global_config_t config;
    char exported[JSON_CONFIG_MAX_BUFFER_SIZE];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);

    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(json_config_serialize_to_string(&config, exported, sizeof(exported)) == AICAM_OK);
    CHECK(strstr(exported, "custom-secret-9") == NULL);
    CHECK(strstr(exported, "hicamthink") == NULL);
    CHECK(strstr(exported, "A1:") == NULL);
    CHECK(strstr(exported, "admin_password\"") == NULL);
    CHECK(strstr(exported, "admin_password_configured") != NULL);
    CHECK(strstr(exported, "session_timeout_ms") != NULL);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_verdict_default_gate_matrix(void)
{
    auth_verdict_t verdict;

    boot_fresh();
    run_config_and_auth();

    verdict = auth_mgr_authorize(NULL, "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", NULL);
    CHECK(verdict.authenticated == AICAM_FALSE);
    verdict = auth_mgr_authorize("", "");
    CHECK(verdict.authenticated == AICAM_FALSE);
    verdict = auth_mgr_authorize("root", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", "wrong-password");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.default_password_active == AICAM_TRUE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);

    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "custom-secret-9");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.default_password_active == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_TRUE);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_default_password_active() == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", "custom-secret-9");
    CHECK(verdict.high_privilege_allowed == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_untrusted_media_authorize_failclosed(void)
{
    uint8_t snapshot[sizeof(g_snapshot_user)];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("real-password") == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    snapshot_user();
    memcpy(snapshot, g_snapshot_user, sizeof(snapshot));
    g_user_io_fail = true;
    CHECK(e2e_storage_boot() != 0);

    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_get_device_credential((char *)snapshot, 16) != AICAM_OK);
    CHECK(auth_mgr_verify_password("real-password") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    auth_verdict_t verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);
    CHECK(auth_mgr_change_password("anything-else") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("anything-else") != AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    g_user_io_fail = false;
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("real-password") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_persist_failure_no_fake_success(void)
{
    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("first-pw-123") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);

    snapshot_user();
    g_e2e_user_ff.fail_next_writes = 1;
    CHECK(auth_mgr_change_password("second-pw-456") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(json_config_set_device_password("second-pw-456") != AICAM_OK);
    CHECK(user_bytes_changed() == 0);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_import_cannot_change_identity(void)
{
    aicam_global_config_t imported;
    aicam_global_config_t config;
    json_config_validation_options_t validation_opts = {
        .validate_json_syntax = AICAM_TRUE,
        .validate_data_types = AICAM_TRUE,
        .validate_value_ranges = AICAM_TRUE,
        .validate_checksum = AICAM_FALSE,
        .strict_mode = AICAM_FALSE
    };
    const char *attack_json =
        "{\"log_config\":{\"log_level\":3},"
        "\"auth_mgr\":{\"admin_password\":\"backdoor-pass\",\"session_timeout_ms\":7200000}}";
    char exported[JSON_CONFIG_MAX_BUFFER_SIZE];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);

    CHECK(json_config_parse_from_string(attack_json, &imported, &validation_opts) == AICAM_OK);
    CHECK(json_config_set_config(&imported) == AICAM_OK);

    CHECK(auth_mgr_verify_password("backdoor-pass") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(config.log_config.log_level == 3);
    CHECK(config.auth_mgr.session_timeout_ms == 7200000);
    CHECK(config.auth_mgr.admin_password[0] == '\0' ||
          strcmp(config.auth_mgr.admin_password, "custom-secret-9") == 0);

    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(json_config_serialize_to_string(&config, exported, sizeof(exported)) == AICAM_OK);
    CHECK(strstr(exported, "custom-secret-9") == NULL);
    CHECK(strstr(exported, "A1:") == NULL);

    aicam_global_config_t roundtrip;
    CHECK(json_config_parse_from_string(exported, &roundtrip, &validation_opts) == AICAM_OK);
    CHECK(json_config_set_config(&roundtrip) == AICAM_OK);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("backdoor-pass") == AICAM_FALSE);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_set_config_persist_failure_propagates(void)
{
    aicam_global_config_t config;
    uint32_t original_level;

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_get_config(&config) == AICAM_OK);
    original_level = config.log_config.log_level;

    g_e2e_user_ff.fail_next_writes = 1;
    config.log_config.log_level = 3;
    CHECK(json_config_set_config(&config) != AICAM_OK);
    g_e2e_user_ff.fail_next_writes = 0;

    CHECK(json_config_get_config(&config) == AICAM_OK);
    CHECK(config.log_config.log_level == original_level);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_change_password_handler_paths(void)
{
    http_handler_context_t ctx;

    boot_fresh();
    g_service_running = true;
    run_config_and_auth();

    ctx = make_json_ctx("POST", "{\"password\":\"handler-pw-1\"}");
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_UNAUTHORIZED);
    CHECK(auth_mgr_verify_password("handler-pw-1") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);

    ctx = make_json_ctx("POST", "{\"password\":\"short\"}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);

    ctx = make_json_ctx("POST", "{}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_json_ctx("POST", "{\"password\":\"handler-pw-1\"}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK(strcmp(g_last_response_message, "Password changed successfully") == 0);
    CHECK(auth_mgr_verify_password("handler-pw-1") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);

    ctx = make_json_ctx("POST", "{\"password\":\"handler-pw-1\"}");
    e2e_reset_response();
    CHECK(login_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);

    ctx = make_json_ctx("POST", "{\"password\":\"hicamthink\"}");
    e2e_reset_response();
    CHECK(login_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_BUSINESS_ERROR_INVALID_PASSWORD);

    ctx = make_json_ctx("GET", NULL);
    e2e_reset_response();
    CHECK(change_password_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_session_expiry_revocation_and_limits(void)
{
    uint32_t session_a;
    uint32_t session_b;
    uint32_t session_c;
    uint32_t session_d;
    uint32_t session_e;
    int iterations;
    auth_result_t rc;

    boot_fresh();
    run_config_and_auth();

    CHECK(auth_mgr_login("admin", "hicamthink", &session_a) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_validate_session(session_a, NULL) == AUTH_RESULT_SUCCESS);

    rc = AUTH_RESULT_SUCCESS;
    for (iterations = 0; iterations < 5000; iterations++) {
        rc = auth_mgr_validate_session(1, NULL);
        if (rc != AUTH_RESULT_SESSION_NOT_FOUND) {
            break;
        }
    }
    CHECK(rc == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(iterations >= 1500);
    CHECK(auth_mgr_validate_session(session_a, NULL) != AUTH_RESULT_SUCCESS);

    CHECK(auth_mgr_login("admin", "hicamthink", &session_a) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_b) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_c) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_d) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_login("admin", "hicamthink", &session_e) == AUTH_RESULT_MAX_SESSIONS_REACHED);

    CHECK(auth_mgr_logout(session_b) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_validate_session(session_b, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_logout(session_b) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_validate_session(session_a, NULL) == AUTH_RESULT_SUCCESS);

    CHECK(auth_mgr_change_password("revoked-pw-123") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_validate_session(session_a, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_validate_session(session_c, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);
    CHECK(auth_mgr_validate_session(session_d, NULL) == AUTH_RESULT_SESSION_NOT_FOUND);

    CHECK(auth_mgr_login("admin", "hicamthink", &session_a) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(auth_mgr_login("admin", "revoked-pw-123", &session_a) == AUTH_RESULT_SUCCESS);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_weak_hash_and_corrupt_record_rejected(void)
{
    char weak_hash_hex[65];
    aicam_global_config_t cfg;

    legacy_weak_hash("victim-pw", weak_hash_hex);

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("victim-pw") == AUTH_RESULT_SUCCESS);
    CHECK(storage_nvs_write(NVS_USER, "auth_password", weak_hash_hex, strlen(weak_hash_hex) + 1) > 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("victim-pw") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password(weak_hash_hex) == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "victim-pw").authenticated == AICAM_FALSE);
    CHECK(auth_mgr_change_password("recovered-pw-1") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("recovered-pw-1") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("trusted-pw-123") == AUTH_RESULT_SUCCESS);
    CHECK(storage_nvs_write(NVS_USER, "auth_password", "A1:00:01", 9) > 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&cfg) != AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_get_device_credential(weak_hash_hex, sizeof(weak_hash_hex)) != AICAM_OK);
    CHECK(auth_mgr_verify_password("trusted-pw-123") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("A1:00:01") == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "hicamthink").authenticated == AICAM_FALSE);
    CHECK(auth_mgr_change_password("repaired-pw-1") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("repaired-pw-1") != AICAM_OK);

    CHECK(json_config_reset_to_default(NULL) == AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);
    CHECK(auth_mgr_authorize("admin", "hicamthink").high_privilege_allowed == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_sensitive_key_predicate(void)
{
    CHECK(json_config_nvs_key_is_sensitive("auth_password") == 1);
    CHECK(json_config_nvs_key_is_sensitive("dev_info_password") == 1);
    CHECK(json_config_nvs_key_is_sensitive("AUTH_PASSWORD") == 0);
    CHECK(json_config_nvs_key_is_sensitive("auth_password_extra") == 0);
    CHECK(json_config_nvs_key_is_sensitive("log_level") == 0);
    CHECK(json_config_nvs_key_is_sensitive("") == 0);
    CHECK(json_config_nvs_key_is_sensitive(NULL) == 0);
}

static void test_legacy_upgrade_rejected_when_unpersistable(void)
{
    char credential[AUTH_CREDENTIAL_RECORD_MAX];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("legacy-keep-1") == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(storage_nvs_write(NVS_USER, "dev_info_password", "legacy-keep-1", 14) > 0);
    CHECK(storage_nvs_delete(NVS_USER, "auth_password") == 0);

    g_e2e_user_ff.fail_next_writes = 1000000;
    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&(aicam_global_config_t){0}) != AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_verify_password("legacy-keep-1") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&(aicam_global_config_t){0}) == AICAM_OK);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("legacy-keep-1") == AICAM_TRUE);
    memset(credential, 0, sizeof(credential));
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) == AICAM_OK);
    CHECK(strncmp(credential, "A1:", 3) == 0);
    CHECK(storage_nvs_read(NVS_USER, "dev_info_password", credential, sizeof(credential)) == -2);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_factory_reset_high_privilege_gate(void)
{
    http_handler_context_t ctx;
    auth_verdict_t verdict;

    boot_fresh();
    g_service_running = true;
    run_config_and_auth();

    ctx = make_json_ctx("GET", NULL);
    e2e_reset_response();
    CHECK(system_factory_reset_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);

    ctx = make_json_ctx("POST", "{\"delay_seconds\":5}");
    e2e_reset_response();
    CHECK(system_factory_reset_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(strstr(g_last_response_message, "High-privilege") != NULL);

    ctx = make_json_ctx("POST", "{\"delay_seconds\":5}");
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(system_factory_reset_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    CHECK(auth_mgr_change_password("reset-admin-1") == AUTH_RESULT_SUCCESS);
    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);
    verdict = auth_mgr_authorize("admin", "reset-admin-1");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.high_privilege_allowed == AICAM_TRUE);

    ctx = make_json_ctx("POST", "{\"delay_seconds\":61}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    e2e_reset_response();
    CHECK(system_factory_reset_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_INVALID_REQUEST);

    ctx = make_json_ctx("POST", "{\"delay_seconds\":5}");
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    e2e_reset_response();
    CHECK(system_factory_reset_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK(strcmp(g_last_response_message, "Factory reset initiated") == 0);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_factory_reset_refused_when_media_untrusted(void)
{
    http_handler_context_t ctx;
    auth_verdict_t verdict;

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("trusted-reset-1") == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    g_user_io_fail = true;
    CHECK(e2e_storage_boot() != 0);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "trusted-reset-1");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    ctx = make_json_ctx("POST", "{\"delay_seconds\":5}");
    e2e_reset_response();
    CHECK(system_factory_reset_handler(&ctx) == AICAM_ERROR);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    g_user_io_fail = false;
}

static void test_persist_readback_failure_rolls_back_all_old(void)
{
    char raw_before[128];
    char raw_after[128];
    int raw_len;
    char credential[AUTH_CREDENTIAL_RECORD_MAX];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("first-pw-123") == AUTH_RESULT_SUCCESS);
    memset(raw_before, 0, sizeof(raw_before));
    raw_len = storage_nvs_read(NVS_USER, "auth_password", raw_before, sizeof(raw_before) - 1);
    CHECK(raw_len == 101);

    e2e_arm_key_read_fail("auth_password", 2);
    CHECK(auth_mgr_change_password("second-pw-456") == AUTH_RESULT_INTERNAL_ERROR);
    e2e_disarm_key_fail();

    CHECK(json_config_nvs_credential_undecided() == AICAM_FALSE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "second-pw-456").authenticated == AICAM_FALSE);
    memset(raw_after, 0, sizeof(raw_after));
    raw_len = storage_nvs_read(NVS_USER, "auth_password", raw_after, sizeof(raw_after) - 1);
    CHECK(raw_len == 101);
    CHECK(memcmp(raw_before, raw_after, (size_t)raw_len) == 0);
    CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw_after, sizeof(raw_after) - 1) == -ENOENT);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) == AICAM_OK);
    CHECK(strcmp(credential, raw_before) == 0);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);

    CHECK(auth_mgr_change_password("second-pw-456") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_persist_rollback_unproven_failclosed_and_recovery(void)
{
    char raw[128];
    char old_record[128];
    char password[128];
    int raw_len;

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("first-pw-123") == AUTH_RESULT_SUCCESS);
    memset(old_record, 0, sizeof(old_record));
    CHECK(storage_nvs_read(NVS_USER, "auth_password", old_record, sizeof(old_record) - 1) == 101);

    e2e_arm_key_read_fail("auth_password", 3);
    e2e_arm_key_write_fail("auth_password", 2);
    CHECK(auth_mgr_change_password("second-pw-456") == AUTH_RESULT_INTERNAL_ERROR);
    e2e_disarm_key_fail();

    CHECK(json_config_nvs_credential_undecided() == AICAM_TRUE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "first-pw-123").authenticated == AICAM_FALSE);
    CHECK(auth_mgr_authorize("admin", "second-pw-456").authenticated == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "first-pw-123", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(auth_mgr_login("admin", "second-pw-456", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(auth_mgr_change_password("retry-pw-999") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("retry-pw-999") != AICAM_OK);

    memset(raw, 0, sizeof(raw));
    CHECK(storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1) == 101);
    CHECK(strncmp(raw, "A1:", 3) == 0);
    memset(raw, 0, sizeof(raw));
    raw_len = storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1);
    CHECK(raw_len == 113);
    CHECK(strncmp(raw, "P1:", 3) == 0);
    CHECK(strstr(raw, old_record) != NULL);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    memset(raw, 0, sizeof(raw));
    CHECK(storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1) == 101);
    CHECK(memcmp(raw, old_record, 101) == 0);
    memset(raw, 0, sizeof(raw));
    CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1) == -ENOENT);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strcmp(password, old_record) == 0);
    CHECK(auth_mgr_verify_password("first-pw-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("second-pw-456") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "first-pw-123", &(uint32_t){0}) == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_change_interruption_marker_failclosed(void)
{
    char raw[128];

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("interrupted-1") == AUTH_RESULT_SUCCESS);
    CHECK(storage_nvs_write(NVS_USER, "auth_password_pending", "1", 2) == 2);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("interrupted-1") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "interrupted-1", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_reset_to_default(NULL) == AICAM_OK);
    memset(raw, 0, sizeof(raw));
    CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1) == -ENOENT);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("interrupted-1") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_reset_to_default_credential_failure_ordering(void)
{
    char raw_before[128];
    char raw_after[128];
    int raw_len;

    boot_fresh();
    run_config_and_auth();
    CHECK(auth_mgr_change_password("custom-secret-9") == AUTH_RESULT_SUCCESS);
    memset(raw_before, 0, sizeof(raw_before));
    raw_len = storage_nvs_read(NVS_USER, "auth_password", raw_before, sizeof(raw_before) - 1);
    CHECK(raw_len == 101);

    e2e_arm_key_write_fail("auth_password", 1);
    CHECK(json_config_reset_to_default(NULL) != AICAM_OK);
    e2e_disarm_key_fail();
    CHECK(json_config_nvs_credential_undecided() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    memset(raw_after, 0, sizeof(raw_after));
    raw_len = storage_nvs_read(NVS_USER, "auth_password", raw_after, sizeof(raw_after) - 1);
    CHECK(raw_len == 101);
    CHECK(memcmp(raw_before, raw_after, (size_t)raw_len) == 0);
    memset(raw_after, 0, sizeof(raw_after));
    raw_len = storage_nvs_read(NVS_USER, "auth_password_pending", raw_after, sizeof(raw_after) - 1);
    CHECK(raw_len == 113);
    CHECK(strncmp(raw_after, "P1:", 3) == 0);
    CHECK(strstr(raw_after, raw_before) != NULL);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    memset(raw_after, 0, sizeof(raw_after));
    CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw_after, sizeof(raw_after) - 1) == -ENOENT);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    e2e_arm_key_write_fail("log_level", 1);
    CHECK(json_config_reset_to_default(NULL) != AICAM_OK);
    e2e_disarm_key_fail();
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    run_config_and_auth();
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("custom-secret-9") == AICAM_FALSE);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_commit_marker_clear_proof_failures(void)
{
    char raw[160];
    int mode;

    for (mode = 0; mode < 2; mode++) {
        boot_fresh();
        run_config_and_auth();
        CHECK(auth_mgr_change_password("commit-proof-old") == AUTH_RESULT_SUCCESS);
        CHECK(json_config_mgr_deinit() == AICAM_OK);
        CHECK(auth_mgr_deinit() == AICAM_OK);
        CHECK(e2e_storage_boot() == 0);
        run_config_and_auth();

        e2e_arm_key_read_fail("auth_password_pending", mode == 0 ? 5 : 3);
        CHECK(auth_mgr_change_password("commit-proof-new") == AUTH_RESULT_INTERNAL_ERROR);
        e2e_disarm_key_fail();

        CHECK(json_config_nvs_credential_undecided() == AICAM_TRUE);
        CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
        CHECK(auth_mgr_verify_password("commit-proof-old") == AICAM_FALSE);
        CHECK(auth_mgr_verify_password("commit-proof-new") == AICAM_FALSE);
        CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
        CHECK(auth_mgr_login("admin", "commit-proof-new", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);

        memset(raw, 0, sizeof(raw));
        CHECK(storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1) == 101);
        CHECK(strncmp(raw, "A1:", 3) == 0);
        memset(raw, 0, sizeof(raw));
        if (mode == 0)
        {
            CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1) == -ENOENT);
        }
        else
        {
            CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1) == 113);
            CHECK(strncmp(raw, "P1:", 3) == 0);
        }

        CHECK(json_config_mgr_deinit() == AICAM_OK);
        CHECK(auth_mgr_deinit() == AICAM_OK);
        CHECK(e2e_storage_boot() == 0);
        run_config_and_auth();
        CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
        if (mode == 0)
        {
            CHECK(auth_mgr_verify_password("commit-proof-new") == AICAM_TRUE);
            CHECK(auth_mgr_verify_password("commit-proof-old") == AICAM_FALSE);
            CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
        }
        else
        {
            CHECK(auth_mgr_verify_password("commit-proof-old") == AICAM_TRUE);
            CHECK(auth_mgr_verify_password("commit-proof-new") == AICAM_FALSE);
            CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
            memset(raw, 0, sizeof(raw));
            CHECK(storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1) == 101);
            CHECK(strncmp(raw, "A1:", 3) == 0);
        }
        memset(raw, 0, sizeof(raw));
        CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1) == -ENOENT);
        CHECK(json_config_mgr_deinit() == AICAM_OK);
        CHECK(auth_mgr_deinit() == AICAM_OK);
    }
}

static void test_powerloss_slices_change_reboot_consistency(void)
{
    char old_record[128];
    char call_record[AUTH_CREDENTIAL_RECORD_MAX];
    char marker[160];
    char raw[160];
    int slice;
    int mode;
    int marker_len;
    int record_len;
    int load_ok;
    int old_ok;
    int new_ok;

    for (mode = 0; mode < 2; mode++) {
        for (slice = 1; slice <= 14; slice++) {
            boot_fresh();
            run_config_and_auth();
            CHECK(auth_mgr_change_password("slice-old-pw") == AUTH_RESULT_SUCCESS);
            CHECK(auth_mgr_verify_password("slice-old-pw") == AICAM_TRUE);
            CHECK(auth_mgr_deinit() == AICAM_OK);
            CHECK(json_config_mgr_deinit() == AICAM_OK);
            CHECK(e2e_storage_boot() == 0);
            memset(old_record, 0, sizeof(old_record));
            CHECK(storage_nvs_read(NVS_USER, "auth_password", old_record, sizeof(old_record) - 1) == 101);

            e2e_arm_powerloss(slice, mode);
            if (setjmp(g_e2e_powerloss_env) == 0)
            {
                (void)json_config_nvs_persist_credential_record("slice-new-pw", call_record, sizeof(call_record));
            }
            e2e_disarm_powerloss();
            CHECK(e2e_storage_boot() == 0);

            memset(marker, 0, sizeof(marker));
            marker_len = storage_nvs_read(NVS_USER, "auth_password_pending", marker, sizeof(marker) - 1);
            memset(raw, 0, sizeof(raw));
            record_len = storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1);

            load_ok = (json_config_mgr_init() == AICAM_OK);
            CHECK(auth_mgr_init() == AICAM_OK);
            old_ok = (auth_mgr_verify_password("slice-old-pw") == AICAM_TRUE);
            new_ok = (auth_mgr_verify_password("slice-new-pw") == AICAM_TRUE);

            if (marker_len >= 0)
            {
                CHECK(marker_len == 113);
                CHECK(strncmp(marker, "P1:", 3) == 0);
                CHECK(strstr(marker, old_record) != NULL);
                CHECK(load_ok == 1);
                CHECK(old_ok == 1);
                CHECK(new_ok == 0);
                memset(raw, 0, sizeof(raw));
                CHECK(storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1) == 101);
                CHECK(memcmp(raw, old_record, 101) == 0);
                memset(marker, 0, sizeof(marker));
                CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", marker, sizeof(marker) - 1) == -ENOENT);
            }
            else if (record_len == 101 && memcmp(raw, old_record, 101) == 0)
            {
                CHECK(load_ok == 1);
                CHECK(old_ok == 1);
                CHECK(new_ok == 0);
            }
            else if (record_len == 101 && raw[0] == 'A' && raw[1] == '1' && raw[2] == ':')
            {
                CHECK(load_ok == 1);
                CHECK(old_ok == 0);
                CHECK(new_ok == 1);
                CHECK(auth_mgr_login("admin", "slice-new-pw", &(uint32_t){0}) == AUTH_RESULT_SUCCESS);
                CHECK(auth_mgr_login("admin", "slice-old-pw", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
            }
            else
            {
                CHECK(load_ok == 0);
                CHECK(old_ok == 0);
                CHECK(new_ok == 0);
            }
            CHECK(old_ok + new_ok <= 1);
            CHECK(auth_mgr_deinit() == AICAM_OK);
            (void)json_config_mgr_deinit();
        }
    }
}

static void test_powerloss_slices_reset_reboot_consistency(void)
{
    char old_record[128];
    char marker[160];
    char raw[160];
    int slice;
    int mode;
    int marker_len;
    int record_len;
    int load_ok;
    int old_ok;
    int default_ok;

    for (mode = 0; mode < 2; mode++) {
        for (slice = 1; slice <= 20; slice++) {
            boot_fresh();
            run_config_and_auth();
            CHECK(auth_mgr_change_password("reset-old-pw") == AUTH_RESULT_SUCCESS);
            memset(old_record, 0, sizeof(old_record));
            CHECK(storage_nvs_read(NVS_USER, "auth_password", old_record, sizeof(old_record) - 1) == 101);
            CHECK(json_config_mgr_deinit() == AICAM_OK);
            CHECK(auth_mgr_deinit() == AICAM_OK);
            CHECK(e2e_storage_boot() == 0);

            e2e_arm_powerloss(slice, mode);
            if (setjmp(g_e2e_powerloss_env) == 0)
            {
                (void)json_config_reset_to_default(NULL);
            }
            e2e_disarm_powerloss();
            CHECK(e2e_storage_boot() == 0);

            memset(marker, 0, sizeof(marker));
            marker_len = storage_nvs_read(NVS_USER, "auth_password_pending", marker, sizeof(marker) - 1);
            memset(raw, 0, sizeof(raw));
            record_len = storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1);

            load_ok = (json_config_mgr_init() == AICAM_OK);
            CHECK(auth_mgr_init() == AICAM_OK);
            old_ok = (auth_mgr_verify_password("reset-old-pw") == AICAM_TRUE);
            default_ok = (auth_mgr_verify_password("hicamthink") == AICAM_TRUE);

            if (marker_len >= 0)
            {
                CHECK(strncmp(marker, "P1:", 3) == 0);
                CHECK(strstr(marker, old_record) != NULL);
                CHECK(load_ok == 1);
                CHECK(old_ok == 1);
                CHECK(default_ok == 0);
                memset(marker, 0, sizeof(marker));
                CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", marker, sizeof(marker) - 1) == -ENOENT);
            }
            else if (record_len == 101 && memcmp(raw, old_record, 101) == 0)
            {
                CHECK(load_ok == 1);
                CHECK(old_ok == 1);
                CHECK(default_ok == 0);
            }
            else if (record_len == 101 && raw[0] == 'A' && raw[1] == '1' && raw[2] == ':')
            {
                CHECK(load_ok == 1);
                CHECK(old_ok == 0);
                CHECK(default_ok == 1);
            }
            else
            {
                CHECK(load_ok == 0);
                CHECK(old_ok == 0);
                CHECK(default_ok == 0);
            }
            CHECK(old_ok + default_ok <= 1);
            CHECK(auth_mgr_deinit() == AICAM_OK);
            (void)json_config_mgr_deinit();
        }
    }
}

static void test_powerloss_slices_legacy_migration(void)
{
    char raw[160];
    int slice;
    int mode;
    int load_ok;

    for (mode = 0; mode < 2; mode++) {
        for (slice = 1; slice <= 10; slice++) {
            boot_fresh();
            run_config_and_auth();
            CHECK(auth_mgr_change_password("legacy-keep-1") == AUTH_RESULT_SUCCESS);
            CHECK(json_config_mgr_deinit() == AICAM_OK);
            CHECK(auth_mgr_deinit() == AICAM_OK);
            CHECK(storage_nvs_write(NVS_USER, "dev_info_password", "legacy-keep-1", 14) > 0);
            CHECK(storage_nvs_delete(NVS_USER, "auth_password") == 0);
            CHECK(e2e_storage_boot() == 0);

            e2e_arm_powerloss(slice, mode);
            if (setjmp(g_e2e_powerloss_env) == 0)
            {
                (void)json_config_mgr_init();
            }
            e2e_disarm_powerloss();
            CHECK(e2e_storage_boot() == 0);

            memset(raw, 0, sizeof(raw));
            CHECK(storage_nvs_read(NVS_USER, "auth_password_pending", raw, sizeof(raw) - 1) == -ENOENT);

            load_ok = (json_config_mgr_init() == AICAM_OK);
            CHECK(load_ok == 1);
            CHECK(auth_mgr_init() == AICAM_OK);
            CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
            CHECK(auth_mgr_verify_password("legacy-keep-1") == AICAM_TRUE);
            CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
            memset(raw, 0, sizeof(raw));
            CHECK(storage_nvs_read(NVS_USER, "auth_password", raw, sizeof(raw) - 1) == 101);
            CHECK(strncmp(raw, "A1:", 3) == 0);
            CHECK(auth_mgr_deinit() == AICAM_OK);
            (void)json_config_mgr_deinit();
        }
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_credential_record_codec();
    test_default_password_persisted_as_record();
    test_export_contains_no_admin_secret();
    test_verdict_default_gate_matrix();
    test_untrusted_media_authorize_failclosed();
    test_persist_failure_no_fake_success();
    test_persist_readback_failure_rolls_back_all_old();
    test_persist_rollback_unproven_failclosed_and_recovery();
    test_commit_marker_clear_proof_failures();
    test_powerloss_slices_change_reboot_consistency();
    test_powerloss_slices_reset_reboot_consistency();
    test_powerloss_slices_legacy_migration();
    test_change_interruption_marker_failclosed();
    test_reset_to_default_credential_failure_ordering();
    test_factory_reset_high_privilege_gate();
    test_factory_reset_refused_when_media_untrusted();
    test_import_cannot_change_identity();
    test_set_config_persist_failure_propagates();
    test_change_password_handler_paths();
    test_session_expiry_revocation_and_limits();
    test_weak_hash_and_corrupt_record_rejected();
    test_sensitive_key_predicate();
    test_legacy_upgrade_rejected_when_unpersistable();
    printf("admin auth checks=%d failures=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
