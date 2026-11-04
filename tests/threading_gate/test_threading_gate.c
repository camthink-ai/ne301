#include <stdio.h>
#include <string.h>
#include "fake_flash.h"
#include "nvs.h"
#include "storage_media_gate.h"
#include "storage.h"
#include "json_config_internal.h"
#include "json_config_boot_gate.h"
#include "json_config_mgr.h"
#include "auth_mgr.h"
#include "mbedtls/sha256.h"
#include "mbedtls/threading.h"

extern void e2e_storage_reset(void);
extern int e2e_storage_boot(void);

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void fresh_boot_storage(void)
{
    e2e_storage_reset();
    if (e2e_storage_boot() != 0) {
        printf("FATAL storage boot failed\n");
    }
}

static void shutdown_stack(void)
{
    (void)json_config_mgr_deinit();
    (void)auth_mgr_deinit();
}

static void test_preregistration_sha256_fails(void)
{
    uint8_t in[32];
    uint8_t out[32];

    memset(in, 0x11, sizeof(in));
    CHECK(mbedtls_sha256(in, sizeof(in), out, 0) != 0);
}

static void test_preregistration_first_boot_fails_closed(void)
{
    char credential[128];

    fresh_boot_storage();
    json_config_mgr_init();
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_credential_last_disposition() != JSON_CONFIG_CRED_DISP_TRUSTED_FIRST_BOOT_DEFAULT);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) != AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_change_password("next-pw") != AUTH_RESULT_SUCCESS);
    shutdown_stack();
}

static void test_postregistration_hash_and_recovery(void)
{
    mbedtls_sha256_context poisoned;
    uint8_t in[32];
    uint8_t out[32];

    memset(in, 0x22, sizeof(in));
    mbedtls_sha256_init(&poisoned);
    mbedtls_threading_alt_init();
    mbedtls_threading_alt_init();
    CHECK(mbedtls_sha256(in, sizeof(in), out, 0) == 0);
    mbedtls_sha256_free(&poisoned);
    memset(in, 0x33, sizeof(in));
    CHECK(mbedtls_sha256(in, sizeof(in), out, 0) == 0);
}

static void test_postregistration_hash_error_fails_closed(void)
{
    char credential[128];

    fresh_boot_storage();
    hal_hash_shim_arm_fail();
    json_config_mgr_init();
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_credential_last_disposition() != JSON_CONFIG_CRED_DISP_TRUSTED_FIRST_BOOT_DEFAULT);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) != AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_change_password("err-pw") != AUTH_RESULT_SUCCESS);
    hal_hash_shim_disarm_fail();
    shutdown_stack();
}

static void test_hash_error_midrotation_preserves_old(void)
{
    fresh_boot_storage();
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    hal_hash_shim_arm_fail();
    CHECK(auth_mgr_change_password("another-pw") != AUTH_RESULT_SUCCESS);
    hal_hash_shim_disarm_fail();
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("another-pw") == AICAM_FALSE);
    shutdown_stack();
}

static void test_registration_first_boot_default_and_reboot(void)
{
    char password[128];

    fresh_boot_storage();
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_BLANK);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_TRUSTED_FIRST_BOOT_DEFAULT);
    CHECK(json_config_nvs_credential_last_fault() == JSON_CONFIG_CRED_FAULT_NONE);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(strncmp(password, "A1:", 3) == 0);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    shutdown_stack();

    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_mgr_init() == AICAM_OK);
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_TRUSTED_RECORD);
    memset(password, 0, sizeof(password));
    CHECK(json_config_get_device_credential(password, sizeof(password)) == AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_change_password("rotated-pw") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("rotated-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    shutdown_stack();
}

int main(void)
{
    test_preregistration_sha256_fails();
    test_preregistration_first_boot_fails_closed();
    test_postregistration_hash_and_recovery();
    test_postregistration_hash_error_fails_closed();
    test_hash_error_midrotation_preserves_old();
    test_registration_first_boot_default_and_reboot();
    printf("total=%d fail=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
