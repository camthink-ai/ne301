/**
 * @file board_hw.c
 * @brief Board hardware revision band detection via the PE9 strap.
 *
 * PE9 is configured as a pull-down input and sampled exactly once, from
 * board_hw_init() in driver_core_init(): a debounced high level means the
 * board is v1.3 or newer (HaLow WAKE on PD9); low means v1.2 or older
 * (HaLow WAKE on PB2). Sampling delays use osDelay, so other threads (the
 * quick-capture pipeline in particular) keep running while it settles.
 * PE9 is released right after sampling because boards v1.3 or newer use it
 * for another module. The strap only separates these two bands — it does
 * not identify the exact revision. The result is cached; board_hw_version()
 * never touches the pin again.
 */

#include "board_hw.h"
#include "main.h"

#include "cmsis_os2.h"
#include "Log/debug.h"
#include "storage.h"

/* NVS key names for the factory-written hardware version. Keep in sync with
 * NVS_KEY_DEVICE_INFO_HW_VER ("dev_info_hw_ver") in json_config_internal.h
 * and NVS_KEY_HW_VERSION ("hw_version") in factory_test.c — defined locally
 * to avoid pulling Core/Services headers into the Hal layer. */
#define BOARD_USER_HW_VER_KEY      "dev_info_hw_ver"
#define BOARD_FACTORY_HW_VER_KEY   "hw_version"
#define BOARD_HW_VER_BUF_LEN       40U

#define BOARD_REV_SAMPLES      3U  /* total samples */
#define BOARD_REV_HIGH_MIN     3U  /* >= 3 high => board is v1.3 or newer */
#define BOARD_REV_SETTLE_MS    2U  /* strap settle after pull-down config */
#define BOARD_REV_SAMPLE_MS    1U  /* spacing between samples */

#define BOARD_REV_UNCACHED     0xFFU
#define BOARD_IR_UNCACHED      0xFFU

static uint8_t g_board_rev = BOARD_REV_UNCACHED;
static uint8_t g_board_ir = BOARD_IR_UNCACHED;

static board_hw_version_t board_hw_detect(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t high = 0;

    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Pin = MM_BOARD_REV_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(MM_BOARD_REV_GPIO_Port, &gpio);

    osDelay(BOARD_REV_SETTLE_MS);
    for (uint8_t i = 0; i < BOARD_REV_SAMPLES; i++) {
        if (HAL_GPIO_ReadPin(MM_BOARD_REV_GPIO_Port, MM_BOARD_REV_Pin) == GPIO_PIN_SET) {
            high++;
        }
        osDelay(BOARD_REV_SAMPLE_MS);
    }

    /* Release the strap pin: on boards v1.3 or newer PE9 is owned by another
     * module once boot progresses, so it must be sampled once and let go. */
    HAL_GPIO_DeInit(MM_BOARD_REV_GPIO_Port, MM_BOARD_REV_Pin);

    return (high >= BOARD_REV_HIGH_MIN) ? BOARD_HW_V1_3_OR_NEWER : BOARD_HW_V1_2_OR_OLDER;
}

/* NVS-only IR-variant detection; safe from any context that can call
 * storage_nvs_read (no osDelay / no strap sampling). */
static uint8_t board_ir_detect(void)
{
    char hw_ver[BOARD_HW_VER_BUF_LEN] = {0};
    int n = storage_nvs_read(NVS_USER, BOARD_USER_HW_VER_KEY,
                             hw_ver, sizeof(hw_ver) - 1);
    if (n <= 0 || hw_ver[0] == '\0') {
        memset(hw_ver, 0, sizeof(hw_ver));
        n = storage_nvs_read(NVS_FACTORY, BOARD_FACTORY_HW_VER_KEY,
                             hw_ver, sizeof(hw_ver) - 1);
    }
    if (n > 0 && hw_ver[0] != '\0') {
        for (const char *p = hw_ver; p[0] != '\0' && p[1] != '\0'; ++p) {
            if ((p[0] == 'i' || p[0] == 'I') &&
                (p[1] == 'r' || p[1] == 'R')) {
                LOG_DRV_INFO("Board variant: IR (hardware version \"%s\")", hw_ver);
                return 1U;
            }
        }
        LOG_DRV_INFO("Board variant: standard (hardware version \"%s\")", hw_ver);
        return 0U;
    }
    /* No factory-written hardware version: treat as standard board. */
    LOG_DRV_INFO("Board variant: standard (hardware version not set)");
    return 0U;
}

void board_hw_init(void)
{
    if (g_board_rev == BOARD_REV_UNCACHED) {
        g_board_rev = (uint8_t)board_hw_detect();
    }
    /* Prime the IR-variant cache (NVS-based, independent of the strap). */
    if (g_board_ir == BOARD_IR_UNCACHED) {
        g_board_ir = board_ir_detect();
    }
}

aicam_bool_t board_is_ir(void)
{
    if (g_board_ir == BOARD_IR_UNCACHED) {
        /* Defensive lazy fallback; board_hw_init() normally primes this
         * right after storage registration in driver_core_init(). Never
         * touches the PE9 strap, so it is safe without thread context. */
        g_board_ir = board_ir_detect();
    }
    return (g_board_ir == 1U) ? AICAM_TRUE : AICAM_FALSE;
}

board_hw_version_t board_hw_version(void)
{
    static uint8_t logged = 0;

    if (g_board_rev == BOARD_REV_UNCACHED) {
        /* board_hw_init() runs from driver_core_init(); this is only a
         * defensive fallback, not the normal path. */
        board_hw_init();
    }
    if (logged == 0) {
        logged = 1;
        LOG_DRV_INFO("Board hardware revision %s (HaLow WAKE on %s)",
                     board_hw_version_str(),
                     (g_board_rev == BOARD_HW_V1_3_OR_NEWER) ? "PD9" : "PB2");
    }
    return (board_hw_version_t)g_board_rev;
}

const char *board_hw_version_str(void)
{
    return (board_hw_version() == BOARD_HW_V1_3_OR_NEWER) ? "1.3.0" : "1.2.0";
}

GPIO_TypeDef *board_hw_halow_wake_port(void)
{
    return (board_hw_version() == BOARD_HW_V1_3_OR_NEWER) ? MM_HALOW_WAKE_V13_GPIO_Port
                                                          : MM_HALOW_WAKE_V12_GPIO_Port;
}

uint16_t board_hw_halow_wake_pin(void)
{
    return (board_hw_version() == BOARD_HW_V1_3_OR_NEWER) ? MM_HALOW_WAKE_V13_Pin
                                                          : MM_HALOW_WAKE_V12_Pin;
}
