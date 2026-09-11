#include "wdg.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"

#include "common_utils.h"
#include "debug.h"
#include "iwdg.h"
#include "wwdg.h"

extern IWDG_HandleTypeDef hiwdg;
extern WWDG_HandleTypeDef hwwdg;
static wdg_t g_wdg = {0};

static int wdg_flag = 0;
const osThreadAttr_t wdgTask_attributes = {
    .name = "wdgTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_size = 4096,
};

static int wdg_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: wdg <start|stop>\r\n");
        return -1;
    }
    if (strcmp(argv[1], "start") == 0) {
        wdg_flag = 1;
    } else if (strcmp(argv[1], "stop") == 0) {
        wdg_flag = 0;
    } else {
        LOG_SIMPLE("Unknown command: %s\r\n", argv[1]);
        return -1;
    }
    return 0;
}

debug_cmd_reg_t wdg_cmd_table[] = {
    {"wdg", "wdg", wdg_cmd},
};

static void wdg_cmd_register(void)
{
    debug_cmdline_register(wdg_cmd_table, sizeof(wdg_cmd_table) / sizeof(wdg_cmd_table[0]));
}

static void wdgProcess(void *argument)
{
    wdg_t *wdg = (wdg_t *)argument;
    wdg->is_init = true;
    wdg_flag = 1;
#if WDG_IS_USE_IWDG
    MX_IWDG_Init();
#else
    MX_WWDG_Init();
#endif
    while (wdg->is_init) {
        if (wdg_flag == 1) {
        #if WDG_IS_USE_IWDG
            HAL_IWDG_Refresh(&hiwdg);
        #else
            HAL_WWDG_Refresh(&hwwdg);
        #endif
        }
        /* osDelay stays outside the flag check: without it `wdg stop` spun
         * this RT7-priority loop with no yield and starved the whole system
         * for the remaining IWDG window. */
        osDelay(1000);
    }
    wdg->wdg_processId = NULL;  // Thread cleans up its own ID
    osThreadExit();
}

static int wdg_init(void *priv)
{
    LOG_DRV_DEBUG("wdg_init \r\n");
    wdg_t *wdg = (wdg_t *)priv;
    wdg->wdg_processId = osThreadNew(wdgProcess, wdg, &wdgTask_attributes);
    return 0;
}

static int wdg_deinit(void *priv)
{
    wdg_t *wdg = (wdg_t *)priv;

    wdg->is_init = false;
    osDelay(100);
    if (wdg->wdg_processId != NULL && osThreadGetId() != wdg->wdg_processId) {
        osThreadTerminate(wdg->wdg_processId);
        wdg->wdg_processId = NULL;
    }

    return 0;
}

void wdg_register(void)
{
    static dev_ops_t wdg_ops = {
        .init = wdg_init,
        .deinit = wdg_deinit, 
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_wdg.dev = dev;
    strcpy(dev->name, WDG_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &wdg_ops;
    dev->priv_data = &g_wdg;

    device_register(g_wdg.dev);

    driver_cmd_register_callback(WDG_DEVICE_NAME, wdg_cmd_register);
}

void wdg_task_change_priority(osPriority_t priority)
{
    if (g_wdg.wdg_processId == NULL) return;
    osThreadSetPriority(g_wdg.wdg_processId, priority);
}

void wdg_unregister(void)
{
    device_unregister(g_wdg.dev);
    hal_mem_free(g_wdg.dev);
}

