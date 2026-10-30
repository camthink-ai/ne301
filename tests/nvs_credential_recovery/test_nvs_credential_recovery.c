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

extern void e2e_storage_reset(void);
extern int e2e_storage_boot(void);
extern void e2e_arm_key_write_fail(const char *key, int nth_hit);
extern void e2e_arm_key_read_fail(const char *key, int nth_hit);
extern void e2e_disarm_key_fail(void);
extern void e2e_arm_key_read_corrupt(const char *key, int skip_hits, int corrupt_hits);
extern void e2e_arm_key_read_absent(const char *key, int skip_hits, int corrupt_hits);
extern void e2e_disarm_key_read_corrupt(void);

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

static void seed_legacy_password(const char *legacy)
{
    CHECK(storage_nvs_write(NVS_USER, NVS_KEY_DEVICE_INFO_PASSWORD, legacy, strlen(legacy)) > 0);
}

static void test_blank_first_boot_default_and_reboot(void)
{
    char password[128];

    fresh_boot_storage();
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_BLANK);
    json_config_mgr_init();
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
    shutdown_stack();
}

static void test_legacy_migration(void)
{
    char probe[8];
    aicam_result_t rc;

    fresh_boot_storage();
    seed_legacy_password("legacy-pw");
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    json_config_mgr_init();
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_TRUSTED_MIGRATED);
    CHECK(auth_mgr_verify_password("legacy-pw") == AICAM_TRUE);
    rc = storage_nvs_read(NVS_USER, NVS_KEY_DEVICE_INFO_PASSWORD, probe, sizeof(probe));
    CHECK(rc != AICAM_OK);
    shutdown_stack();
}

static void assert_deadlock_state(void)
{
    char credential[128];

    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_FAIL_CLOSED_COMMIT_MISSING_NONBLANK);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) != AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("whatever-else") == AICAM_FALSE);
    CHECK(auth_mgr_change_password("attacker-pw") != AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("attacker-pw") == AICAM_FALSE);
}

static void test_first_write_incomplete_rolls_back_all_old(void)
{
    fresh_boot_storage();
    e2e_arm_key_write_fail(NVS_KEY_AUTH_PASSWORD, 1);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    e2e_disarm_key_fail();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_credential_last_fault() == JSON_CONFIG_CRED_FAULT_WRITE_FAIL);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    shutdown_stack();

    CHECK(e2e_storage_boot() == 0);
    assert_deadlock_state();
    shutdown_stack();
}

static void test_read_back_mismatch_rolls_back_all_old(void)
{
    fresh_boot_storage();
    e2e_arm_key_read_corrupt(NVS_KEY_AUTH_PASSWORD, 0, 2);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    e2e_disarm_key_read_corrupt();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_credential_last_fault() == JSON_CONFIG_CRED_FAULT_VERIFY_MISMATCH);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_ABSENT);
    shutdown_stack();

    CHECK(e2e_storage_boot() == 0);
    assert_deadlock_state();
    shutdown_stack();
}

static void test_unstable_read_rolls_back_all_old(void)
{
    fresh_boot_storage();
    e2e_arm_key_read_corrupt(NVS_KEY_AUTH_PASSWORD, 0, 1);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    e2e_disarm_key_read_corrupt();
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_credential_last_fault() == JSON_CONFIG_CRED_FAULT_UNSTABLE_READ);
    shutdown_stack();

    CHECK(e2e_storage_boot() == 0);
    assert_deadlock_state();
    shutdown_stack();
}

static void test_runbook_recovery_restores_first_boot(void)
{
    char credential[128];

    fresh_boot_storage();
    e2e_arm_key_write_fail(NVS_KEY_AUTH_PASSWORD, 1);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    e2e_disarm_key_fail();
    shutdown_stack();
    CHECK(e2e_storage_boot() == 0);
    assert_deadlock_state();
    shutdown_stack();

    e2e_storage_reset();
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_BLANK);
    json_config_mgr_init();
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_TRUSTED_FIRST_BOOT_DEFAULT);
    CHECK(json_config_get_device_credential(credential, sizeof(credential)) == AICAM_OK);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    shutdown_stack();
}

static void test_gate_refused_classified(void)
{
    char record[AUTH_CREDENTIAL_RECORD_MAX];

    fresh_boot_storage();
    json_config_mgr_init();
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    storage_media_gate_set_state(NVS_USER, STORAGE_MEDIA_UNTRUSTED);
    CHECK(json_config_nvs_persist_credential_record("some-pw", record, sizeof(record)) == AICAM_ERROR);
    memset(record, 0, sizeof(record));
    CHECK(json_config_nvs_credential_last_fault() == JSON_CONFIG_CRED_FAULT_GATE_REFUSED);
    storage_media_gate_set_state(NVS_USER, STORAGE_MEDIA_HEALTHY);
    shutdown_stack();
}

static void test_invalid_marker_fails_closed_unknown_key(void)
{
    const char *garbage = "P1:zzzz";

    fresh_boot_storage();
    seed_legacy_password("legacy-pw");
    CHECK(storage_nvs_write(NVS_USER, NVS_KEY_AUTH_PENDING, garbage, strlen(garbage)) > 0);
    CHECK(e2e_storage_boot() == 0);
    CHECK(storage_nvs_media_state(NVS_USER) == STORAGE_MEDIA_HEALTHY);
    json_config_mgr_init();
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_INVALID);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_FAIL_CLOSED_UNKNOWN_KEY);
    CHECK(auth_mgr_verify_password("legacy-pw") == AICAM_FALSE);
    shutdown_stack();
}

static void test_false_absent_marker_boot_not_exposed(void)
{
    fresh_boot_storage();
    json_config_mgr_init();
    (void)auth_mgr_init();
    CHECK(auth_mgr_change_password("uncommitted-check") == AUTH_RESULT_SUCCESS);
    e2e_arm_key_read_fail(NVS_KEY_AUTH_PENDING, 3);
    CHECK(auth_mgr_change_password("pending-glitch-new") != AUTH_RESULT_SUCCESS);
    e2e_disarm_key_fail();
    CHECK(json_config_nvs_credential_undecided() == AICAM_TRUE);
    shutdown_stack();

    CHECK(e2e_storage_boot() == 0);
    e2e_arm_key_read_absent(NVS_KEY_AUTH_PENDING, 0, 1);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_config_healthy() == AICAM_FALSE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_FALSE);
    CHECK(json_config_nvs_boot_marker_status() == JSON_CONFIG_CRED_MARKER_UNREADABLE);
    CHECK(json_config_nvs_credential_last_disposition() == JSON_CONFIG_CRED_DISP_FAIL_CLOSED_UNKNOWN_KEY);
    CHECK(auth_mgr_verify_password("pending-glitch-new") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("uncommitted-check") == AICAM_FALSE);
    e2e_disarm_key_read_corrupt();
    shutdown_stack();

    CHECK(e2e_storage_boot() == 0);
    json_config_mgr_init();
    (void)auth_mgr_init();
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("uncommitted-check") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("pending-glitch-new") == AICAM_FALSE);
    shutdown_stack();
}

static void test_prior_capture_glitch_keeps_old_intact(void)
{
    char probe[8];

    fresh_boot_storage();
    json_config_mgr_init();
    (void)auth_mgr_init();
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    e2e_arm_key_read_absent(NVS_KEY_AUTH_PASSWORD, 0, 1);
    CHECK(auth_mgr_change_password("glitch-pw") != AUTH_RESULT_SUCCESS);
    e2e_disarm_key_read_corrupt();
    CHECK(json_config_nvs_credential_undecided() == AICAM_FALSE);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("glitch-pw") == AICAM_FALSE);
    memset(probe, 0, sizeof(probe));
    CHECK(storage_nvs_read(NVS_USER, NVS_KEY_AUTH_PENDING, probe, sizeof(probe)) < 0);
    shutdown_stack();
}

static void test_verify_cache_lifecycle(void)
{
    fresh_boot_storage();
    CHECK(e2e_storage_boot() == 0);
    CHECK(json_config_mgr_init() == AICAM_OK);
    CHECK(auth_mgr_init() == AICAM_OK);
    CHECK(json_config_mgr_credential_trusted() == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_TRUE);
    CHECK(auth_mgr_change_password("first-record-pw") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("first-record-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("first-record-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("first-record-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("first-record-wrong") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("hicamthink") == AICAM_FALSE);
    CHECK(auth_mgr_change_password("rotated-pw") == AUTH_RESULT_SUCCESS);
    CHECK(auth_mgr_verify_password("first-record-pw") == AICAM_FALSE);
    CHECK(auth_mgr_verify_password("rotated-pw") == AICAM_TRUE);
    CHECK(auth_mgr_verify_password("rotated-pw") == AICAM_TRUE);
    shutdown_stack();
}

int main(void)
{
    test_blank_first_boot_default_and_reboot();
    test_legacy_migration();
    test_first_write_incomplete_rolls_back_all_old();
    test_read_back_mismatch_rolls_back_all_old();
    test_unstable_read_rolls_back_all_old();
    test_runbook_recovery_restores_first_boot();
    test_gate_refused_classified();
    test_invalid_marker_fails_closed_unknown_key();
    test_false_absent_marker_boot_not_exposed();
    test_prior_capture_glitch_keeps_old_intact();
    test_verify_cache_lifecycle();
    printf("nvs_credential_recovery: total=%d fail=%d\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
