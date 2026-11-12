/*
 * NULL out-param semantics for fixes 3bbd11845 (power_mode) and 07647bf31
 * (web_server network config), exercised against the REAL production
 * json_config_mgr.c on host (same shim/fake-flash harness as
 * tests/storage_gate/test_storage_e2e).
 *
 * Property matrix under test:
 *   1. Refusal path (ctx not initialized): both getters return
 *      AICAM_ERROR_NOT_INITIALIZED and never dereference the out-param —
 *      NULL or not. This path is byte-identical before/after the fixes.
 *   2. OK path with a real caller struct (the fixed caller pattern):
 *      the getter whole-struct-copies the loaded configuration.
 *   3. OK path with NULL (the pre-fix caller pattern): the production
 *      callee has NO NULL guard and writes *config — undefined behavior.
 *      On host address 0 is unmapped, so the child dies with SIGSEGV.
 *      On the target at -O0 address 0 aliases flash: the write is
 *      dropped and the subsequent read returns garbage (campaign
 *      evidence, e1-193/brick-rootcause.md); at -Os/-O2 GCC folds the
 *      provable NULL deref into udf (deterministic HardFault).
 *   4. Negative, power_mode: an illegal raw NVS value
 *      (current_mode >= POWER_MODE_MAX) propagates unvalidated through
 *      load -> getter -> (post-fix) system_service loader. The validated
 *      setter rejects it, so only external NVS corruption can produce it.
 *   5. Negative, web_server: the AP sleep timeout the OK path now feeds
 *      web_server_ap_sleep_timer_init comes from configuration (default
 *      0 = "no sleep") instead of a garbage read from the flash alias.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

#include "json_config_mgr.h"
#include "json_config_internal.h"
#include "aicam_types.h"
#include "fake_flash.h"

extern void e2e_storage_reset(void);
extern int e2e_storage_boot(void);

static int g_total = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/* Reality anchors for the consumer policy argued in the review:
 * system_service.c treats any current_mode that is not POWER_MODE_LOW_POWER
 * as "active" (switch default denies wakeup support, should_enter_sleep
 * returns false). */
_Static_assert(POWER_MODE_LOW_POWER == 0, "power mode enum drifted");
_Static_assert(POWER_MODE_MAX == 2, "power mode enum drifted");

static void child_power_getter_null(void)
{
    fflush(NULL);
    (void)json_config_get_power_mode_config(NULL);
    _exit(0); /* unreachable if the write through NULL faults */
}

static void child_network_getter_null(void)
{
    fflush(NULL);
    (void)json_config_get_network_service_config(NULL);
    _exit(0); /* unreachable if the write through NULL faults */
}

static int child_dies_with_sigsegv(void (*fn)(void))
{
    pid_t pid = fork();
    if (pid == 0) {
        fn();
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        return 0;
    }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}

static void test_refusal_path_identical_for_null_and_real(void)
{
    power_mode_config_t pc;
    network_service_config_t nc;

    e2e_storage_reset();
    CHECK(e2e_storage_boot() == 0);

    /* ctx deliberately NOT initialized: getters must refuse without
     * touching the out-param, for NULL and for a real struct alike. */
    CHECK(json_config_get_power_mode_config(NULL) == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(json_config_get_power_mode_config(&pc) == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(json_config_get_network_service_config(NULL) == AICAM_ERROR_NOT_INITIALIZED);
    CHECK(json_config_get_network_service_config(&nc) == AICAM_ERROR_NOT_INITIALIZED);
}

static void test_ok_path_copies_loaded_config(void)
{
    power_mode_config_t pc;
    network_service_config_t nc;
    power_mode_config_t pc2;

    CHECK(json_config_mgr_init() == AICAM_OK);

    /* Fixed power_mode caller pattern (system_service.c:186-189). */
    memset(&pc, 0x55, sizeof(pc));
    CHECK(json_config_get_power_mode_config(&pc) == AICAM_OK);
    CHECK(pc.current_mode == POWER_MODE_LOW_POWER);
    CHECK(pc.default_mode == POWER_MODE_LOW_POWER);
    CHECK(pc.low_power_timeout_ms == 60000);

    /* Whole-struct copy is deterministic. */
    memset(&pc2, 0xAA, sizeof(pc2));
    CHECK(json_config_get_power_mode_config(&pc2) == AICAM_OK);
    CHECK(memcmp(&pc, &pc2, sizeof(pc)) == 0);

    /* Fixed web_server caller pattern (web_server.c:451). */
    memset(&nc, 0x55, sizeof(nc));
    CHECK(json_config_get_network_service_config(&nc) == AICAM_OK);
    /* json_config_mgr.c:162 — default is 0 = "no AP sleep timeout". */
    CHECK(nc.ap_sleep_time == 0);
}

static void test_setter_rejects_illegal_power_modes(void)
{
    power_mode_config_t bad;

    memset(&bad, 0, sizeof(bad));
    bad.current_mode = POWER_MODE_MAX;      /* == 2, out of range */
    bad.default_mode = POWER_MODE_LOW_POWER;
    CHECK(json_config_set_power_mode_config(&bad) == AICAM_ERROR_INVALID_PARAM);

    bad.current_mode = 5;                   /* raw corruption value used below */
    CHECK(json_config_set_power_mode_config(&bad) == AICAM_ERROR_INVALID_PARAM);
}

static void test_configured_ap_sleep_survives_reload(void)
{
    network_service_config_t nc;
    power_mode_config_t pc;

    /* Legal API path: configure 1800 (one of the UI-allowed values,
     * api_network_module.c:793-794). */
    CHECK(json_config_get_network_service_config(&nc) == AICAM_OK);
    nc.ap_sleep_time = 1800;
    CHECK(json_config_set_network_service_config(&nc) == AICAM_OK);

    /* deinit performs a full save of the in-memory config, so the raw
     * injection must happen AFTER it — this also mirrors reality, where
     * corruption lands between sessions, not during one. */
    CHECK(json_config_mgr_deinit() == AICAM_OK);

    /* Raw NVS injection of an illegal power mode, bypassing the setter
     * validation (only external NVS corruption can do this in the field). */
    CHECK(json_config_nvs_write_uint32(NVS_KEY_POWER_CURRENT_MODE, 5) == AICAM_OK);

    /* Full reload from NVS, as a reboot would do. */
    CHECK(json_config_mgr_init() == AICAM_OK);

    /* web_server OK path now feeds the configured value to the timer. */
    CHECK(json_config_get_network_service_config(&nc) == AICAM_OK);
    CHECK(nc.ap_sleep_time == 1800);

    /* Negative: illegal power mode propagates unvalidated through the
     * load path. Consumers treat it as "not low power" (fail-active). */
    CHECK(json_config_get_power_mode_config(&pc) == AICAM_OK);
    CHECK(pc.current_mode == 5);
    CHECK(pc.current_mode >= POWER_MODE_MAX);
    CHECK(pc.current_mode != POWER_MODE_LOW_POWER);
}

static void test_null_out_param_is_undefined_behavior(void)
{
    /* Production callee performs *config = ... with no NULL guard
     * (json_config_mgr.c:970 and :1547). Pre-fix callers passed NULL. */
    CHECK(child_dies_with_sigsegv(child_power_getter_null));
    CHECK(child_dies_with_sigsegv(child_network_getter_null));
}

int main(void)
{
    test_refusal_path_identical_for_null_and_real();
    test_ok_path_copies_loaded_config();
    test_setter_rejects_illegal_power_modes();
    test_configured_ap_sleep_survives_reload();
    test_null_out_param_is_undefined_behavior();

    printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_total, g_fail);
    return g_fail ? 1 : 0;
}
