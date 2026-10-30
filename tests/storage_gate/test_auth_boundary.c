#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "mongoose/mongoose.h"
#include "auth_mgr.h"
#include "json_config_mgr.h"
#include "aicam_types.h"

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void boot_fresh(void)
{
    extern void e2e_storage_reset(void);
    extern int e2e_storage_boot(void);
    e2e_storage_reset();
    CHECK(e2e_storage_boot() == 0);
}

static void make_basic_header(const char *user, const char *password, char *out, size_t out_size)
{
    char raw[256];
    snprintf(raw, sizeof(raw), "%s:%s", user, password);
    size_t encoded = mg_base64_encode((const unsigned char *)raw, strlen(raw), out, out_size);
    CHECK(encoded > 0);
}

static void decode_with(const char *b64, char *user, size_t user_size, char *pass, size_t pass_size)
{
    char req[640];
    struct mg_http_message hm;
    int parsed;
    memset(&hm, 0, sizeof(hm));
    snprintf(req, sizeof(req),
             "POST /api/v1/device/info HTTP/1.1\r\n"
             "Authorization: Basic %s\r\n"
             "Content-Length: 0\r\n"
             "\r\n",
             b64);
    parsed = mg_http_parse(req, strlen(req), &hm);
    CHECK(parsed > 0);
    mg_http_creds(&hm, user, user_size, pass, pass_size);
}

static void decode_production_sizes(const char *user, const char *password, char *dec_user, char *dec_pass)
{
    char b64[512];
    make_basic_header(user, password, b64, sizeof(b64));
    decode_with(b64, dec_user, AUTH_BASIC_DECODE_BUF_LEN, dec_pass, AUTH_BASIC_DECODE_BUF_LEN);
}

static void test_decode_boundary_holds_accepted_maxima(void)
{
    char dec_user[AUTH_BASIC_DECODE_BUF_LEN];
    char dec_pass[AUTH_BASIC_DECODE_BUF_LEN];
    const char *pw31 = "abcdefghij0123456789ijklmnopqrA";
    const char *pw32 = "abcdefghij0123456789ijklmnopqrAB";

    CHECK(AUTH_BASIC_DECODE_BUF_LEN == AUTH_MAX_PASSWORD_LEN + 1);
    CHECK(AUTH_BASIC_DECODE_BUF_LEN > 32 + 1);
    CHECK(strlen(pw31) == 31);
    CHECK(strlen(pw32) == 32);

    memset(dec_user, 0, sizeof(dec_user));
    memset(dec_pass, 0, sizeof(dec_pass));
    decode_production_sizes("admin", pw32, dec_user, dec_pass);
    CHECK(strcmp(dec_user, "admin") == 0);
    CHECK(strlen(dec_pass) == 32);
    CHECK(strcmp(dec_pass, pw32) == 0);
}

static void test_password_length_matrix_end_to_end(void)
{
    const char *lengths[] = {"abcd1234", "abcdefghij0123456789ijklmnopqrA", "abcdefghij0123456789ijklmnopqrAB"};
    char dec_user[AUTH_BASIC_DECODE_BUF_LEN];
    char dec_pass[AUTH_BASIC_DECODE_BUF_LEN];
    auth_verdict_t verdict;
    size_t i;

    boot_fresh();
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        CHECK(auth_mgr_change_password(lengths[i]) == AUTH_RESULT_SUCCESS);

        memset(dec_user, 0, sizeof(dec_user));
        memset(dec_pass, 0, sizeof(dec_pass));
        decode_production_sizes("admin", lengths[i], dec_user, dec_pass);
        CHECK(strcmp(dec_user, "admin") == 0);
        CHECK(strlen(dec_pass) == strlen(lengths[i]));

        verdict = auth_mgr_authorize(dec_user, dec_pass);
        CHECK(verdict.authenticated == AICAM_TRUE);
        CHECK(verdict.high_privilege_allowed == AICAM_TRUE);
        CHECK(verdict.default_password_active == AICAM_FALSE);

        verdict = auth_mgr_authorize("admin", "hicamthink");
        CHECK(verdict.authenticated == AICAM_FALSE);
        CHECK(verdict.high_privilege_allowed == AICAM_FALSE);
    }

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);

    {
        extern int e2e_storage_boot(void);
        CHECK(e2e_storage_boot() == 0);
        CHECK(json_config_mgr_init() == AICAM_OK);
        CHECK(auth_mgr_init() == AICAM_OK);
        memset(dec_user, 0, sizeof(dec_user));
        memset(dec_pass, 0, sizeof(dec_pass));
        decode_production_sizes("admin", lengths[2], dec_user, dec_pass);
        CHECK(auth_mgr_authorize(dec_user, dec_pass).authenticated == AICAM_TRUE);
        CHECK(auth_mgr_verify_password(lengths[2]) == AICAM_TRUE);
        CHECK(json_config_mgr_deinit() == AICAM_OK);
        CHECK(auth_mgr_deinit() == AICAM_OK);
    }
}

static void test_truncated_and_illegal_credentials_refused(void)
{
    const char *pw32 = "abcdefghij0123456789ijklmnopqrAB";
    char dec_user[AUTH_BASIC_DECODE_BUF_LEN];
    char dec_pass[AUTH_BASIC_DECODE_BUF_LEN];
    char legacy_user[32];
    char legacy_pass[32];
    char b64[512];
    char oversized[71];
    auth_verdict_t verdict;

    boot_fresh();
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_change_password(pw32) == AUTH_RESULT_SUCCESS);

    memset(legacy_user, 0, sizeof(legacy_user));
    memset(legacy_pass, 0, sizeof(legacy_pass));
    make_basic_header("admin", pw32, b64, sizeof(b64));
    decode_with(b64, legacy_user, sizeof(legacy_user), legacy_pass, sizeof(legacy_pass));
    CHECK(strlen(legacy_pass) == 31);
    CHECK(auth_mgr_verify_password(legacy_pass) == AICAM_FALSE);
    verdict = auth_mgr_authorize(legacy_user, legacy_pass);
    CHECK(verdict.authenticated == AICAM_FALSE);

    memset(dec_user, 0, sizeof(dec_user));
    memset(dec_pass, 0, sizeof(dec_pass));
    decode_with(b64, dec_user, sizeof(dec_user), dec_pass, sizeof(dec_pass));
    CHECK(strlen(dec_pass) == 32);
    verdict = auth_mgr_authorize(dec_user, dec_pass);
    CHECK(verdict.authenticated == AICAM_TRUE);

    memset(oversized, 'A', sizeof(oversized));
    oversized[0] = 'x';
    oversized[sizeof(oversized) - 1] = '\0';
    memset(dec_user, 0, sizeof(dec_user));
    memset(dec_pass, 0, sizeof(dec_pass));
    decode_production_sizes("admin", oversized, dec_user, dec_pass);
    CHECK(strlen(dec_pass) <= AUTH_BASIC_DECODE_MAX_LEN);
    CHECK(auth_mgr_verify_password(dec_pass) == AICAM_FALSE);
    verdict = auth_mgr_authorize(dec_user, dec_pass);
    CHECK(verdict.authenticated == AICAM_FALSE);

    memset(dec_user, 0, sizeof(dec_user));
    memset(dec_pass, 0, sizeof(dec_pass));
    make_basic_header("root", pw32, b64, sizeof(b64));
    decode_with(b64, dec_user, sizeof(dec_user), dec_pass, sizeof(dec_pass));
    verdict = auth_mgr_authorize(dec_user, dec_pass);
    CHECK(verdict.authenticated == AICAM_FALSE);

    memset(dec_user, 0, sizeof(dec_user));
    memset(dec_pass, 0, sizeof(dec_pass));
    make_basic_header("admin", "", b64, sizeof(b64));
    decode_with(b64, dec_user, sizeof(dec_user), dec_pass, sizeof(dec_pass));
    CHECK(dec_pass[0] == '\0');
    verdict = auth_mgr_authorize(dec_user, dec_pass);
    CHECK(verdict.authenticated == AICAM_FALSE);

    memset(dec_user, 0, sizeof(dec_user));
    memset(dec_pass, 0, sizeof(dec_pass));
    decode_with("not-valid-base64-!!!", dec_user, sizeof(dec_user), dec_pass, sizeof(dec_pass));
    CHECK(dec_user[0] == '\0' && dec_pass[0] == '\0');
    CHECK(auth_mgr_authorize(dec_user, dec_pass).authenticated == AICAM_FALSE);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

static void test_epoch_snapshot_denies_straddled_password_change(void)
{
    uint32_t epoch_before;
    uint32_t epoch_after;
    auth_verdict_t verdict;

    boot_fresh();
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);

    epoch_before = auth_mgr_credential_epoch();
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_default_password_active() == AICAM_TRUE);
    CHECK(auth_mgr_credential_epoch() == epoch_before);

    CHECK(auth_mgr_change_password("rotated-race-pw") == AUTH_RESULT_SUCCESS);
    epoch_after = auth_mgr_credential_epoch();
    CHECK(epoch_after != epoch_before);
    CHECK(auth_mgr_default_password_active() == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "hicamthink");
    CHECK(verdict.authenticated == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_FALSE);

    verdict = auth_mgr_authorize("admin", "rotated-race-pw");
    CHECK(verdict.authenticated == AICAM_TRUE);
    CHECK(verdict.default_password_active == AICAM_FALSE);
    CHECK(verdict.high_privilege_allowed == AICAM_TRUE);
    CHECK(auth_mgr_credential_epoch() == epoch_after);

    CHECK(json_config_mgr_deinit() == AICAM_OK);
    CHECK(auth_mgr_deinit() == AICAM_OK);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_decode_boundary_holds_accepted_maxima();
    test_password_length_matrix_end_to_end();
    test_truncated_and_illegal_credentials_refused();
    test_epoch_snapshot_denies_straddled_password_change();
    printf("auth boundary checks=%d failures=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
