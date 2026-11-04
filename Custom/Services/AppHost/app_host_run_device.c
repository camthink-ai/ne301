#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_host_run.h"

#include "app_host_abi.h"
#include "cmsis_os2.h"
#include "stm32n6xx_hal.h"
#include "debug.h"

extern uint8_t __app_host_ram_base__[];
extern uint8_t __app_host_ram_end__[];

static uint8_t *dev_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = (size_t)(__app_host_ram_end__ - __app_host_ram_base__);
    return __app_host_ram_base__;
}

static int dev_stage_commit(void *ctx, size_t stage_cap,
                            const app_host_image_info_t *info)
{
    (void)ctx;
    (void)info;

    __DSB();
    SCB_CleanInvalidateDCache_by_Addr(__app_host_ram_base__, (int32_t)stage_cap);
    SCB_InvalidateICache_by_Addr(__app_host_ram_base__, (int32_t)stage_cap);
    __ISB();
    return 0;
}

static int dev_invoke_entry(void *ctx, const app_host_api_table_t *api,
                            const app_host_image_info_t *info, int *raw)
{
    uintptr_t entry_addr;
    app_host_entry_fn_t entry;
    (void)ctx;

    if (raw == NULL) {
        return -1;
    }
    entry_addr = (uintptr_t)__app_host_ram_base__ + info->entry_offset;
    entry = (app_host_entry_fn_t)(entry_addr | APP_HOST_ENTRY_THUMB_MASK);
    *raw = entry(api, APP_HOST_ABI_VERSION);
    return 0;
}

static void dev_scrub(void *ctx)
{
    int32_t size = (int32_t)(__app_host_ram_end__ - __app_host_ram_base__);
    (void)ctx;
    memset(__app_host_ram_base__, 0, (size_t)size);
    SCB_CleanInvalidateDCache_by_Addr(__app_host_ram_base__, size);
}

static void dev_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    LOG_SIMPLE("[APP] %s\r\n", (text != NULL) ? text : "");
}

static int dev_tick_ms(void *ctx)
{
    (void)ctx;
    return (int)osKernelGetTickCount();
}

static const app_host_run_backend_t g_app_host_run_device_backend = {
    NULL,
    dev_stage_base,
    dev_stage_commit,
    dev_invoke_entry,
    dev_scrub,
    dev_log_sink,
    dev_tick_ms
};

const app_host_run_backend_t *app_host_run_device_backend(void)
{
    return &g_app_host_run_device_backend;
}
