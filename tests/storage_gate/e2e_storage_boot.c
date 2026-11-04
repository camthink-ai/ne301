#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include "fake_flash.h"
#include "nvs.h"
#include "storage_media_gate.h"
#include "json_config_boot_gate.h"
#include "json_config_mgr.h"
#include "json_config_internal.h"
#include <stdio.h>
#include "auth_mgr.h"
#include "web_server.h"
#include "api_device_module.h"
#include "aicam_types.h"

extern fake_flash_t g_e2e_user_ff;
extern fake_flash_t g_e2e_fact_ff;
extern bool g_e2e_fail_default_pw_write;
extern void e2e_storage_reset(void);
extern int e2e_storage_boot(void);
extern void e2e_reset_response(void);
extern char g_last_response_kind[16];
extern int g_last_response_code;
extern char g_last_response_message[512];
extern bool g_service_running;
extern int g_reload_calls;
extern bool g_e2e_lfs_prog_fail;
extern bool g_user_io_fail;

extern aicam_bool_t json_config_mgr_config_healthy(void);
extern aicam_bool_t json_config_mgr_credential_trusted(void);

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define E2E_USER_OFF 0x40000
#define E2E_USER_SIZE (8 * 4096)

static uint8_t g_snapshot[E2E_USER_SIZE];

static void snapshot_user(void)
{
    memcpy(g_snapshot, g_e2e_user_ff.mem, E2E_USER_SIZE);
}

static int user_bytes_changed(void)
{
    return memcmp(g_snapshot, g_e2e_user_ff.mem, E2E_USER_SIZE) != 0;
}

static http_handler_context_t make_ctx(const char *method, const char *body)
{
    http_handler_context_t ctx;
    static char body_buf[256];
    memset(&ctx, 0, sizeof(ctx));
    memset(body_buf, 0, sizeof(body_buf));
    snprintf(ctx.request.method, sizeof(ctx.request.method), "%s", method);
    if (body) {
        strncpy(body_buf, body, sizeof(body_buf) - 1);
        ctx.request.body = body_buf;
    }
    return ctx;
}

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

static void test_first_boot_blank_default_flow_kept(void)
{
    aicam_global_config_t cfg;
    char password[128];

    boot_fresh();
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_BLANK);
    run_config_and_auth();
    CHECK(json_config_mgr_config_healthy() == AICAM_TRUE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strncmp(password, "A1:", 3) == 0);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("wrong-guess-1") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "hicamthink", &(uint32_t){0}) == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_change_password("new-secret-123") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("new-secret-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strncmp(password, "A1:", 3) == 0);
    CHECK(auth_mgr_verify_password("new-secret-123") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(json_config_load_from_nvs(&cfg) == AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_untrusted_media_login_failclosed_bytes_preserved(void)
{
    char password[128];

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_set_device_password("real-password") == AICAM_OK);
    CHECK(auth_mgr_change_password("real-password") == AUTH_RESULT_SUCCESS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    snapshot_user();
    g_user_io_fail = true;
    CHECK(e2e_storage_boot() != 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_nvs_write(NVS_USER, "sneaky", "x", 2) == -1);
    CHECK(storage_nvs_delete(NVS_USER, "auth_password") == -1);
    CHECK(storage_nvs_clear(NVS_USER) == -1);
    g_user_io_fail = false;

    run_config_and_auth();
    CHECK(json_config_load_from_nvs(&(aicam_global_config_t){0}) != AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_get_device_credential(password, sizeof(password)) != AICAM_OK);
    CHECK(json_config_set_device_password("anything") != AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("real-password") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "hicamthink", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(auth_mgr_change_password("whatever") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strncmp(password, "A1:", 3) == 0);
    CHECK(auth_mgr_verify_password("real-password") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static const uint8_t e2e_crc8_table[16] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d
};

static uint8_t e2e_crc8(uint8_t val, const void *buf, size_t cnt)
{
    size_t i;
    const uint8_t *p = (const uint8_t *)buf;
    for (i = 0; i < cnt; i++) {
        val ^= p[i];
        val = (uint8_t)((val << 4) ^ e2e_crc8_table[val >> 4]);
        val = (uint8_t)((val << 4) ^ e2e_crc8_table[val >> 4]);
    }
    return val;
}

static void e2e_craft_ate(uint8_t *slot, const char *key, uint16_t off, uint16_t len)
{
    struct nvs_ate ate;
    memset(&ate, 0xFF, sizeof(ate));
    memset(ate.key, 0, NVS_KEY_SIZE);
    strncpy(ate.key, key, NVS_KEY_SIZE);
    ate.offset = off;
    ate.len = len;
    ate.part = 0xFF;
    ate.crc8 = e2e_crc8(0xFF, &ate, offsetof(struct nvs_ate, crc8));
    memcpy(slot, &ate, sizeof(ate));
}

static void e2e_craft_close(uint8_t *slot, uint16_t last_ate_off)
{
    struct nvs_ate ate;
    memset(&ate, 0xFF, sizeof(ate));
    ate.len = 0;
    ate.offset = last_ate_off;
    ate.crc8 = e2e_crc8(0xFF, &ate, offsetof(struct nvs_ate, crc8));
    memcpy(slot, &ate, sizeof(ate));
}

static void craft_degraded_user_image(const char *password)
{
    size_t pw_len = strlen(password) + 1;
    uint8_t *mem = g_e2e_user_ff.mem;
    uint32_t s0 = 4 * 4096;
    uint32_t s1 = 5 * 4096;
    uint32_t s2 = 6 * 4096;

    memcpy(mem + s0, password, pw_len);
    e2e_craft_ate(mem + s0 + 4096 - 64, "auth_password", 0, (uint16_t)pw_len);
    e2e_craft_close(mem + s0 + 4096 - 32, (uint16_t)(4096 - 64));

    memcpy(mem + s1, "BBBBBBBB", 8);
    e2e_craft_ate(mem + s1 + 4096 - 64, "k2", 0, 8);
    memset(mem + s1 + 4096 - 96, 0x33, 10);

    memcpy(mem + s2, "AAAAAAAA", 8);
    e2e_craft_ate(mem + s2 + 4096 - 64, "k1", 0, 8);
    e2e_craft_close(mem + s2 + 4096 - 32, (uint16_t)(4096 - 64));
}

static void craft_reclaim_bad_ate_user_image(const char *password)
{
    size_t pw_len = strlen(password) + 1;
    uint8_t *mem = g_e2e_user_ff.mem;
    uint32_t s0 = 4 * 4096;
    uint32_t s2 = 6 * 4096;

    memcpy(mem + s0, password, pw_len);
    e2e_craft_ate(mem + s0 + 4096 - 64, "auth_password", 0, (uint16_t)pw_len);
    e2e_craft_close(mem + s0 + 4096 - 32, (uint16_t)(4096 - 64));

    memcpy(mem + s2, "CCCCCCCC", 8);
    e2e_craft_ate(mem + s2 + 4096 - 64, "k3", 0, 8);
    memset(mem + s2 + 4096 - 96, 0x33, 10);
    e2e_craft_close(mem + s2 + 4096 - 32, (uint16_t)(4096 - 96));
}

static void craft_interrupted_gc_user_image(const char *password)
{
    size_t pw_len = strlen(password) + 1;
    uint8_t *mem = g_e2e_user_ff.mem;
    uint32_t s = 4 * 4096;
    int i;

    memcpy(mem + s, password, pw_len);
    e2e_craft_ate(mem + s + 4096 - 64, "auth_password", 0, (uint16_t)pw_len);
    e2e_craft_close(mem + s + 4096 - 32, (uint16_t)(4096 - 64));

    memcpy(mem + s + 4096, "CCCCCCCC", 8);
    e2e_craft_ate(mem + s + 4096 + 4096 - 64, "k3", 0, 8);

    memcpy(mem + s + 2 * 4096, "CCCCCCCC", 8);
    e2e_craft_ate(mem + s + 2 * 4096 + 4096 - 64, "k3", 0, 8);
    e2e_craft_close(mem + s + 2 * 4096 + 4096 - 32, (uint16_t)(4096 - 64));

    for (i = 0; i < 8; i++) {
        if (i == 4 || i == 5 || i == 6) {
            continue;
        }
        e2e_craft_close(mem + i * 4096 + 4096 - 32, (uint16_t)(4096 - 32));
    }
}

static void test_degraded_media_write_refused_real_password_login(void)
{
    boot_fresh();
    craft_degraded_user_image("stored-real-pw");
    snapshot_user();
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(storage_nvs_write(NVS_USER, "sneaky", "x", 2) == -1);
    CHECK(storage_nvs_delete(NVS_USER, "log_level") == -1);
    CHECK(storage_nvs_clear(NVS_USER) == -1);

    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("stored-real-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_change_password("nope") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("nope") == AICAM_ERROR);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
}

static void test_degraded_media_without_credential_failclosed(void)
{
    boot_fresh();
    craft_degraded_user_image("stored-real-pw");
    memset(g_e2e_user_ff.mem + 4 * 4096 + 4096 - 64, 0x33, 10);
    snapshot_user();
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_DEGRADED);

    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("stored-real-pw") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "hicamthink", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_save_first_write_failure_survives_aggregation(void)
{
    aicam_global_config_t cfg;
    aicam_result_t rc;

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_mgr_config_healthy() == AICAM_TRUE);
    CHECK(json_config_get_config(&cfg) == AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    rc = json_config_load_from_nvs(&cfg);
    CHECK(rc == AICAM_OK);
    CHECK(storage_nvs_delete(NVS_USER, "log_level") == 0);
    CHECK(storage_nvs_delete(NVS_USER, "log_size") == 0);
    CHECK(storage_nvs_delete(NVS_USER, "log_count") == 0);

    g_e2e_user_ff.fail_next_writes = 1;
    rc = json_config_load_from_nvs(&cfg);
    CHECK(rc != AICAM_OK);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_nvs_write(NVS_USER, "sneaky", "x", 2) == -1);
    g_e2e_user_ff.fail_next_writes = 0;
    rc = json_config_load_from_nvs(&cfg);
    CHECK(rc != AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    rc = json_config_load_from_nvs(&cfg);
    CHECK(rc == AICAM_OK);

    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_get_config(&cfg) == AICAM_OK);
    g_e2e_user_ff.fail_next_writes = 1;
    CHECK(json_config_save_to_file("unused", &cfg) != AICAM_OK);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_UNTRUSTED);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_save_to_file("unused", &cfg) != AICAM_OK);
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(json_config_save_to_file("unused", &cfg) == AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
}

static void test_migration_write_failure_visible(void)
{
    aicam_global_config_t cfg;
    char password[128];

    boot_fresh();
    run_config_and_auth();
    CHECK(json_config_set_device_password("legacy-pass") == AICAM_OK);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
    CHECK(storage_nvs_write(NVS_USER, "dev_info_password", "legacy-pass", 12) > 0);
    CHECK(storage_nvs_delete(NVS_USER, "auth_password") == 0);

    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    g_e2e_user_ff.fail_next_writes = 1;
    CHECK(json_config_load_from_nvs(&cfg) != AICAM_OK);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_UNTRUSTED);
    CHECK(storage_nvs_write(NVS_USER, "sneaky", "x", 2) == -1);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_load_from_nvs(&cfg) != AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_verify_password("legacy-pass") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(json_config_load_from_nvs(&cfg) == AICAM_OK);
    g_e2e_user_ff.fail_next_writes = 1000000;
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strncmp(password, "A1:", 3) == 0);
    CHECK(auth_mgr_verify_password("legacy-pass") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_change_password("x-not-persisted") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(auth_mgr_verify_password("x-not-persisted") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("legacy-pass") == AICAM_TRUE);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_UNTRUSTED);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_load_from_nvs(&cfg) == AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strncmp(password, "A1:", 3) == 0);
    CHECK(auth_mgr_verify_password("legacy-pass") == AICAM_TRUE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_format_handler_success_and_real_failure(void)
{
    http_handler_context_t ctx;

    boot_fresh();
    g_service_running = true;
    run_config_and_auth();

    ctx = make_ctx("GET", NULL);
    e2e_reset_response();
    CHECK(device_storage_format_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_METHOD_NOT_ALLOWED);
    CHECK(g_reload_calls == 0);

    ctx = make_ctx("POST", NULL);
    e2e_reset_response();
    CHECK(device_storage_format_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(strstr(g_last_response_message, "High-privilege") != NULL);
    CHECK(g_reload_calls == 0);

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    e2e_reset_response();
    CHECK(device_storage_format_handler(&ctx) == AICAM_ERROR);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_FORBIDDEN);
    CHECK(g_reload_calls == 0);

    CHECK(auth_mgr_change_password("format-admin-1") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_default_password_active() == AICAM_FALSE);

    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    e2e_reset_response();
    CHECK(device_storage_format_handler(&ctx) == AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "success") == 0);
    CHECK(strstr(g_last_response_message, "formatted") != NULL);
    CHECK(g_reload_calls == 1);

    g_e2e_lfs_prog_fail = true;
    ctx = make_ctx("POST", NULL);
    ctx.authenticated = AICAM_TRUE;
    ctx.high_privilege = AICAM_TRUE;
    e2e_reset_response();
    CHECK(device_storage_format_handler(&ctx) != AICAM_OK);
    CHECK(strcmp(g_last_response_kind, "error") == 0);
    CHECK(g_last_response_code == API_ERROR_INTERNAL_ERROR);
    CHECK(strstr(g_last_response_message, "failed") != NULL);
    CHECK(g_reload_calls == 1);
    g_e2e_lfs_prog_fail = false;
}

static void test_blank_boot_credential_write_failure_failclosed(void)
{
    aicam_global_config_t cfg;
    char password[128];

    boot_fresh();
    g_e2e_fail_default_pw_write = true;
    CHECK(json_config_load_from_nvs(&cfg) != AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_init() == AICAM_OK);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) != AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "hicamthink", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(auth_mgr_change_password("whatever") == AUTH_RESULT_INTERNAL_ERROR);
    CHECK(json_config_set_device_password("anything") != AICAM_OK);
    CHECK(storage_nvs_read(NVS_USER, "auth_password", password, sizeof(password)) == -ENOENT);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    snapshot_user();
    g_e2e_fail_default_pw_write = false;
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_load_from_nvs(&cfg) != AICAM_OK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_login("admin", "hicamthink", &(uint32_t){0}) == AUTH_RESULT_INVALID_CREDENTIALS);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_reclaim_bad_ate_degraded_preserved(void)
{
    boot_fresh();
    craft_reclaim_bad_ate_user_image("stored-real-pw");
    snapshot_user();
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(g_e2e_user_ff.erase_calls == 0);
    CHECK(storage_nvs_write(NVS_USER, "sneaky", "x", 2) == -1);
    CHECK(storage_nvs_clear(NVS_USER) == -1);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("stored-real-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_DEGRADED);
    CHECK(user_bytes_changed() == 0);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
}

static void test_interrupted_gc_recovery_healthy(void)
{
    char buf[16];

    boot_fresh();
    craft_interrupted_gc_user_image("stored-real-pw");
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(g_e2e_user_ff.erase_calls >= 2);
    memset(buf, 0, sizeof(buf));
    CHECK(storage_nvs_read(NVS_USER, "k3", buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "CCCCCCCC", 8) == 0);
    CHECK(storage_nvs_read(NVS_USER, "auth_password", buf, sizeof(buf)) == 15);
    run_config_and_auth();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("stored-real-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_media_fault_downgrade_and_benign_errors(void)
{
    char big[64];

    boot_fresh();
    run_config_and_auth();
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_BLANK);
    CHECK(storage_nvs_write(NVS_USER, "ok", "v", 2) == 2);
    memset(big, 'x', sizeof(big));
    CHECK(storage_nvs_write(NVS_USER, "big", big, 4500) == -EINVAL);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_BLANK);
    CHECK(storage_nvs_write(NVS_USER, "ok2", "v", 2) == 2);
    g_e2e_user_ff.fail_next_writes = 1;
    CHECK(storage_nvs_write(NVS_USER, "bad", "v", 2) == -5);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_UNTRUSTED);
    g_e2e_user_ff.fail_next_writes = 0;
    CHECK(storage_nvs_write(NVS_USER, "after", "v", 2) == -1);
    CHECK(storage_nvs_delete(NVS_USER, "ok") == -1);
    CHECK(storage_nvs_clear(NVS_USER) == -1);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    CHECK(storage_nvs_write(NVS_USER, "post", "v", 2) == 2);
    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_first_boot_blank_default_flow_kept();
    test_untrusted_media_login_failclosed_bytes_preserved();
    test_degraded_media_write_refused_real_password_login();
    test_degraded_media_without_credential_failclosed();
    test_blank_boot_credential_write_failure_failclosed();
    test_reclaim_bad_ate_degraded_preserved();
    test_interrupted_gc_recovery_healthy();
    test_media_fault_downgrade_and_benign_errors();
    test_save_first_write_failure_survives_aggregation();
    test_migration_write_failure_visible();
    test_format_handler_success_and_real_failure();
    printf("e2e checks=%d failures=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
