#include "driver_core.h"
#include "debug.h"
#include "storage.h"
#include "drtc.h"
#include "camera.h"
#include "enc.h"
#include "draw.h"
// #include "ai_process.h"
#include "wifi.h"
#include "misc.h"
#include "pwr.h"
#include "codec.h"
#include "cat1.h"
#include "sd_file.h"
// #include "usb_host_video.h"
#include "netif_manager.h"
#include "wdg.h"
#include "jpegc.h"
#include "tls_test.h"
#include "driver_test.h"
#include "nn.h"
#include "mem.h"
#include "u0_module.h"
#include "quick_bootstrap.h"
#include "quick_snapshot.h"
#include "system_top.h"
#include "upgrade_manager.h"
#include "json_config_internal.h"
#include "SensorExt/i2c_tool/i2c_tool.h"
#include "SensorExt/tft_st7789v/tft_st7789vw.h"
#include "SensorExt/sensor_exemple/sensor_exemple.h"

#define QUICK_SNAPSHOT_CARE_WAKEUP_FLAG_MASK    (PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_RTC_ALARM_A | PWR_WAKEUP_FLAG_RTC_ALARM_B | PWR_WAKEUP_FLAG_CONFIG_KEY | PWR_WAKEUP_FLAG_PIR_HIGH | PWR_WAKEUP_FLAG_PIR_LOW | PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_FALLING)

extern void NPURam_enable();
extern void NPUCache_config();

bool driver_core_init(void)
{
#ifdef QUICK_SNAPSHOT_CARE_WAKEUP_FLAG_MASK
    // int ret = 0;
    // char capture_quick_mode[2] = {0};
    uint32_t wakeup_flag = 0;
#endif

    // printf("driver_core_init \r\n");
    hal_mem_register();
    storage_register();
#if ENABLE_U0_MODULE
    u0_module_register();
#endif
    wdg_register();
    pwr_register();
    sd_register();
    misc_register();
    rtc_register();
#if !defined(POWER_MODULE_TEST) || !POWER_MODULE_TEST
    camera_register();
    jpegc_register();
    draw_register();
    NPURam_enable();
    NPUCache_config();
    nn_register();
#if ENABLE_U0_MODULE && defined(QUICK_SNAPSHOT_CARE_WAKEUP_FLAG_MASK)
    if (u0_module_get_wakeup_flag(&wakeup_flag) == 0 && (wakeup_flag & QUICK_SNAPSHOT_CARE_WAKEUP_FLAG_MASK) && !(wakeup_flag & (PWR_WAKEUP_FLAG_KEY_LONG_PRESS | PWR_WAKEUP_FLAG_KEY_MAX_PRESS))) {
        // ret = storage_nvs_read(NVS_USER, NVS_KEY_CAPTURE_QUICK_MODE, capture_quick_mode, sizeof(capture_quick_mode));
        // if (ret > 0 && capture_quick_mode[0] == '1') {
        //     /* Quick_Bootstrap: after camera/jpegc are registered, run the fast snapshot pipeline based on U0 wakeup_flag. Register other modules on demand. */
        //     if (wakeup_flag & (PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_RTC_ALARM_A | PWR_WAKEUP_FLAG_RTC_ALARM_B)) {
        //         quick_bootstrap_run(QB_WAKEUP_SOURCE_TIMER);
        //     } else if ((wakeup_flag & PWR_WAKEUP_FLAG_CONFIG_KEY)) {
        //         quick_bootstrap_run(QB_WAKEUP_SOURCE_BUTTON);
        //     } else if (wakeup_flag & (PWR_WAKEUP_FLAG_PIR_HIGH | PWR_WAKEUP_FLAG_PIR_LOW | PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_FALLING)) {
        //         quick_bootstrap_run(QB_WAKEUP_SOURCE_PIR);
        //     }
        // } else {
            quick_snapshot_init();
        // }
    }
#endif
    enc_register();
#else
    netif_manager_register();
#endif
    netif_manager_register_commands();
    // wifi_register();
    // tls_test_register();
    // cat1_register();

    system_top_register();
#if !NETIF_WIFI_HALOW_IS_ENABLE
    i2c_tool_register();
    codec_register();
    tft_st7789vw_register_commands();
    sensor_exemple_register_commands();
#endif

    LOG_DRV_DEBUG("driver_core_init end \r\n");
    driver_test_main();
    set_slot_boot_success(FIRMWARE_APP, true);
    return true;
}

