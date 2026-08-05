#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "tx_port.h"
#include "Hal/mem.h"
#include "rtc.h"
#include "Log/debug.h"
#include "common_utils.h"
#include "version.h"
#include "u0_module.h"

static uint32_t g_key_value = 1, g_pir_value = 0, g_power_status = PWR_DEFAULT_SWITCH_BITS, g_wakeup_flag = 0;
static uint8_t pir_is_inited = 0;
typedef struct {
    uint32_t type;
    uint32_t value;
    uint32_t timestamp;
} value_change_event_t;
#define VALUE_CHANGE_TYPE_KEY 0
#define VALUE_CHANGE_TYPE_PIR 1
#define VALUE_CHANGE_QUEUE_SIZE 10
static osMessageQueueId_t value_change_event_queue = NULL;

#define U9_MAX_RECV_LEN    256
// static uint8_t u9_rx_res = 0;
static uint8_t u9_rx_buf[U9_MAX_RECV_LEN] = {0};
static HAL_StatusTypeDef u9_rx_state = HAL_OK;
static ms_bridging_handler_t *u0_handler = NULL;
static osMutexId_t u0_tx_mutex = NULL;
static uint8_t ms_bd_thread_stack[1024 * 4] ALIGN_32 IN_PSRAM;
static const osThreadAttr_t ms_bd_task_attributes = {
    .name = "ms_bd_Task",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_mem = ms_bd_thread_stack,
    .stack_size = sizeof(ms_bd_thread_stack),
    .cb_mem     = 0,
    .cb_size    = 0,
    .attr_bits  = 0u,
    .tz_module  = 0u,
};

// void u0_module_IRQHandler(UART_HandleTypeDef *huart)
// {
//     if (huart == &huart9) {
//         ms_bridging_recv(u0_handler, &u9_rx_res, 1);
//         HAL_UART_Receive_IT(&huart9, &u9_rx_res, 1);
//     }
// }

void HAL_UART9_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) 
{
    ms_bridging_recv(u0_handler, u9_rx_buf, Size);
    u9_rx_state = HAL_UARTEx_ReceiveToIdle_IT(&huart9, u9_rx_buf, U9_MAX_RECV_LEN);
}

void HAL_UART9_ErrorCallback(UART_HandleTypeDef *huart)
{
    u9_rx_state = HAL_UARTEx_ReceiveToIdle_IT(&huart9, u9_rx_buf, U9_MAX_RECV_LEN);
}

int u0_module_send_func(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    int ret = 0;
    uint32_t time_ms = 0;
    // TX_INTERRUPT_SAVE_AREA

    if (osMutexAcquire(u0_tx_mutex, timeout_ms) != osOK) return HAL_TIMEOUT;
    // TX_DISABLE
    ret = HAL_UART_Transmit_IT(&huart9, buf, len);
    if (ret != HAL_OK) HAL_UART_AbortTransmit_IT(&huart9);
    else {
        // Wait for transmission to complete or timeout
        do {
            osDelay(1);
            time_ms++;
        } while ((huart9.gState != HAL_UART_STATE_READY) && (time_ms < timeout_ms));
        if (huart9.gState != HAL_UART_STATE_READY) {
            HAL_UART_AbortTransmit_IT(&huart9);
            ret = HAL_TIMEOUT; // Timeout error
        }
    }
    osMutexRelease(u0_tx_mutex);
    // TX_RESTORE

    return ret;
}

void u0_module_notify_cb(void *handler, ms_bridging_frame_t *frame)
{
    ms_bridging_version_t version = {0};
    value_change_event_t value_change_event = {0};
    // LOG_SIMPLE("u0 module notify: %d", frame->header.cmd);

    if (frame->header.type == MS_BR_FRAME_TYPE_REQUEST) {
        switch (frame->header.cmd) {
            case MS_BR_FRAME_CMD_KEEPLIVE:
                ms_bridging_response(handler, frame, NULL, 0);
                break;
            case MS_BR_FRAME_CMD_GET_VERSION:
                ms_bridging_get_version_from_str(FW_VERSION_STRING, &version);
                ms_bridging_response(handler, frame, &version, sizeof(ms_bridging_version_t));
                break;
            default:
                break;
        }
    } else if (frame->header.type == MS_BR_FRAME_TYPE_EVENT) {
        switch (frame->header.cmd) {
            case MS_BR_FRAME_CMD_KEY_VALUE:
                g_key_value = *(uint32_t *)frame->data;
                if (value_change_event_queue != NULL) {
                    value_change_event.type = VALUE_CHANGE_TYPE_KEY;
                    value_change_event.value = g_key_value;
                    value_change_event.timestamp = osKernelGetTickCount();
                    osMessageQueuePut(value_change_event_queue, &value_change_event, 0, 0);
                }
                ms_bridging_event_ack(handler, frame);
                // LOG_SIMPLE("key value: %d", g_key_value);
                break;
            case MS_BR_FRAME_CMD_PIR_VALUE:
                g_pir_value = *(uint32_t *)frame->data;
                if (value_change_event_queue != NULL) {
                    value_change_event.type = VALUE_CHANGE_TYPE_PIR;
                    value_change_event.value = g_pir_value;
                    value_change_event.timestamp = osKernelGetTickCount();
                    osMessageQueuePut(value_change_event_queue, &value_change_event, 0, 0);
                }
                ms_bridging_event_ack(handler, frame);
                // LOG_SIMPLE("pir value: %d", g_pir_value);
                break;
            default:
                break;
        }
    }
}

void ms_bridging_polling_task(void *argument) 
{
    for (;;) {
        if (u9_rx_state != HAL_OK) {
            LOG_SIMPLE("u0 rx state: %d, error: %lx", u9_rx_state, huart9.ErrorCode);
            HAL_UART_AbortReceive_IT(&huart9);
            u9_rx_state = HAL_UARTEx_ReceiveToIdle_IT(&huart9, u9_rx_buf, U9_MAX_RECV_LEN);
        }
        ms_bridging_polling(u0_handler);
    }
}

int u0_module_update_rtc_time(void)
{
    int ret = 0;
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    ms_bridging_time_t ms_time = {0};

    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);
    ms_time.year = date.Year;
    ms_time.month = date.Month;
    ms_time.day = date.Date;
    ms_time.week = date.WeekDay;
    ms_time.hour = time.Hours;
    ms_time.minute = time.Minutes;
    ms_time.second = time.Seconds;
    ret = ms_bridging_request_set_time(u0_handler, &ms_time);

    return ret;
}

int u0_module_sync_rtc_time(void)
{
    int ret = 0;
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    ms_bridging_time_t ms_time = {0};

    ret = ms_bridging_request_get_time(u0_handler, &ms_time);
    if (ret != MS_BR_OK) return ret;

    time.Hours = ms_time.hour;
    time.Minutes = ms_time.minute;
    time.Seconds = ms_time.second;
    date.Year = ms_time.year;
    date.Month = ms_time.month;
    date.Date = ms_time.day;
    date.WeekDay = ms_time.week;

    ret = HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
    if (ret != HAL_OK) return ret;

    ret = HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);
    if (ret != HAL_OK) return ret;

    return ret;
}

int u0_module_get_power_status(uint32_t *switch_bits)
{
    int ret = 0;

    ret = ms_bridging_request_power_status(u0_handler, switch_bits);
    if (ret != MS_BR_OK) return ret;

    g_power_status = *switch_bits;

    return ret;
}

uint32_t u0_module_get_power_status_ex(void)
{
    return g_power_status;
}

int u0_module_get_wakeup_flag(uint32_t *wakeup_flag)
{
    int ret = 0;

    ret = ms_bridging_request_wakeup_flag(u0_handler, wakeup_flag);
    if (ret != MS_BR_OK) return ret;

    g_wakeup_flag = *wakeup_flag;
    if (g_wakeup_flag & (PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_FALLING)) pir_is_inited = 1;

    return ret;
}

uint32_t u0_module_get_wakeup_flag_ex(void)
{
    return g_wakeup_flag;
}

int u0_module_clear_wakeup_flag(void)
{
    int ret = 0;

    ret = ms_bridging_request_clear_flag(u0_handler);
    if (ret != MS_BR_OK) return ret;

    g_wakeup_flag = 0;

    return ret;
}

int u0_module_reset_chip_n6(void)
{
    int ret = 0;

    ret = ms_bridging_request_reset_n6(u0_handler);
    if (ret != MS_BR_OK) return ret;
    
    return ret;
}

int u0_module_get_key_value(uint32_t *key_value)
{
    int ret = 0;

    ret = ms_bridging_request_key_value(u0_handler, key_value);
    if (ret != MS_BR_OK) return ret;

    g_key_value = *key_value;

    return ret;
}

uint32_t u0_module_get_key_value_ex(void)
{
    return g_key_value;
}

int u0_module_get_pir_value(uint32_t *pir_value)
{
    int ret = 0;

    ret = ms_bridging_request_pir_value(u0_handler, pir_value);
    if (ret != MS_BR_OK) return ret;

    g_pir_value = *pir_value;

    return ret;
}

uint32_t u0_module_get_pir_value_ex(void)
{
    return g_pir_value;
}

int u0_module_get_usbin_value(uint32_t *usbin_value)
{
    int ret = 0;

    ret = ms_bridging_request_usbin_value(u0_handler, usbin_value);
    if (ret != MS_BR_OK) return ret;

    return ret;
}

int u0_module_get_version(ms_bridging_version_t *version)
{
    int ret = 0;

    if (version == NULL) return -1;

    ret = ms_bridging_request_version(u0_handler, version);
    if (ret != MS_BR_OK) return ret;

    return ret;
}

int u0_module_cfg_pir(ms_bridging_pir_cfg_t *pir_cfg)
{
    int ret = 0, retry_times = 0;

    do {
        if (ret != MS_BR_OK) {
            LOG_SIMPLE("pir config retry %d times...", retry_times);
            osDelay(PIR_CONFIG_RETRY_DELAY_MS);
        }
        ret = ms_bridging_request_pir_cfg(u0_handler, pir_cfg);
        if (ret < 0) break;
    } while (++retry_times < PIR_CONFIG_RETRY_TIMES && ret != MS_BR_OK);

    return ret;
}

int u0_module_power_control(uint32_t switch_bits)
{
    int ret = 0;
    ms_bridging_power_ctrl_t power_ctrl = {0};

    power_ctrl.power_mode = MS_BR_PWR_MODE_NORMAL;
    power_ctrl.switch_bits = switch_bits;
    power_ctrl.wakeup_flags = 0;
    ret = ms_bridging_request_power_control(u0_handler, &power_ctrl);
    if (ret != MS_BR_OK) return ret;

    g_power_status = switch_bits;

    return ret;
}

int u0_module_enter_sleep_mode(uint32_t wakeup_flag, uint32_t switch_bits, uint32_t sleep_second)
{
    int ret = 0;
    ms_bridging_power_ctrl_t power_ctrl = {0};
    
    if (switch_bits || sleep_second > 0xFFFFU || (wakeup_flag & (PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_FALLING))) power_ctrl.power_mode = MS_BR_PWR_MODE_STOP2;
    else power_ctrl.power_mode = MS_BR_PWR_MODE_STANDBY;

    power_ctrl.switch_bits = switch_bits;
    power_ctrl.wakeup_flags = wakeup_flag;
    power_ctrl.sleep_second = sleep_second;
    ret = ms_bridging_request_power_control(u0_handler, &power_ctrl);
    if (ret != MS_BR_OK) return ret;

    return ret;
}

int u0_module_enter_sleep_mode_ex(uint32_t wakeup_flag, uint32_t switch_bits, uint32_t sleep_second, ms_bridging_alarm_t *rtc_alarm_a, ms_bridging_alarm_t *rtc_alarm_b)
{
    int ret = 0;
    ms_bridging_power_ctrl_t power_ctrl = {0};
    
    if (switch_bits || sleep_second > 0xFFFFU || (wakeup_flag & (PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_FALLING))) power_ctrl.power_mode = MS_BR_PWR_MODE_STOP2;
    else power_ctrl.power_mode = MS_BR_PWR_MODE_STANDBY;

    power_ctrl.switch_bits = switch_bits;
    power_ctrl.wakeup_flags = wakeup_flag;
    power_ctrl.sleep_second = sleep_second;
    if (rtc_alarm_a != NULL) {
        power_ctrl.alarm_a.is_valid = rtc_alarm_a->is_valid;
        power_ctrl.alarm_a.week_day = rtc_alarm_a->week_day;
        power_ctrl.alarm_a.date = rtc_alarm_a->date;
        power_ctrl.alarm_a.hour = rtc_alarm_a->hour;
        power_ctrl.alarm_a.minute = rtc_alarm_a->minute;
        power_ctrl.alarm_a.second = rtc_alarm_a->second;
    }
    if (rtc_alarm_b != NULL) {
        power_ctrl.alarm_b.is_valid = rtc_alarm_b->is_valid;
        power_ctrl.alarm_b.week_day = rtc_alarm_b->week_day;
        power_ctrl.alarm_b.date = rtc_alarm_b->date;
        power_ctrl.alarm_b.hour = rtc_alarm_b->hour;
        power_ctrl.alarm_b.minute = rtc_alarm_b->minute;
        power_ctrl.alarm_b.second = rtc_alarm_b->second;
    }
    ret = ms_bridging_request_power_control(u0_handler, &power_ctrl);
    if (ret != MS_BR_OK) return ret;

    return ret;
}

static u0_module_value_change_cb_t g_pir_value_change_cb = NULL;
static u0_module_value_change_cb_t g_key_value_change_cb = NULL;

int u0_module_pir_value_change_cb_register(u0_module_value_change_cb_t pir_value_change_cb)
{
    g_pir_value_change_cb = pir_value_change_cb;
    return 0;
}

int u0_module_pir_value_change_cb_unregister(void)
{
    g_pir_value_change_cb = NULL;
    return 0;
}

int u0_module_key_value_change_cb_register(u0_module_value_change_cb_t key_value_change_cb)
{
    g_key_value_change_cb = key_value_change_cb;
    return 0;
}

int u0_module_key_value_change_cb_unregister(void)
{
    g_key_value_change_cb = NULL;
    return 0;
}

void u0_module_callback_process(void)
{
    uint32_t current_timestamp = 0, diff_timestamp = 0;
    value_change_event_t value_change_event = {0};

    while (osMessageQueueGet(value_change_event_queue, &value_change_event, NULL, 0) == osOK) {
        current_timestamp = osKernelGetTickCount();
        if (current_timestamp < value_change_event.timestamp) diff_timestamp = UINT32_MAX - value_change_event.timestamp + current_timestamp;
        else diff_timestamp = current_timestamp - value_change_event.timestamp;
        if (diff_timestamp > 1000) continue; // 1s timeout
        if (value_change_event.type == VALUE_CHANGE_TYPE_PIR) {
            if (g_pir_value_change_cb != NULL) g_pir_value_change_cb(value_change_event.value);
        } else if (value_change_event.type == VALUE_CHANGE_TYPE_KEY) {
            if (g_key_value_change_cb != NULL) g_key_value_change_cb(value_change_event.value);
        }
    }
}

void u0_module_test_pir_value_change_cb(uint32_t pir_value)
{
    LOG_SIMPLE("pir value change: %d", pir_value);
}

void u0_module_test_key_value_change_cb(uint32_t key_value)
{
    LOG_SIMPLE("key value change: %d", key_value);
}

int u0_module_cmd_deal(int argc, char* argv[])
{
    int ret = 0;
    uint32_t value = 0;
    uint32_t wakeup_flags = 0, switch_bits = 0;
    uint32_t sleep_second = 0;
    ms_bridging_alarm_t rtc_alarm_a = {0};
    ms_bridging_pir_cfg_t pir_cfg = {0};
    // RTC_TimeTypeDef time = {0};
    // RTC_DateTypeDef date = {0};

    if (argc < 2) {
        LOG_SIMPLE("Usage:");
        LOG_SIMPLE("  u0 <cmd>");
        LOG_SIMPLE("  u0 key");
        LOG_SIMPLE("  u0 pir");
        LOG_SIMPLE("  u0 cfg_pir [sensitivity_level] [ignore_time_s] [pulse_count] [window_time_s]");
        LOG_SIMPLE("  u0 pwr");
        LOG_SIMPLE("  u0 pwr_on <name1> <name2> ... <nameN>");
        LOG_SIMPLE("  u0 pwr_off <name1> <name2> ... <nameN>");
        LOG_SIMPLE("  u0 wakeup_flag");
        LOG_SIMPLE("  u0 version");
        LOG_SIMPLE("  u0 rtc_update");
        LOG_SIMPLE("  u0 rtc_sync");
        LOG_SIMPLE("  u0 sleep <sleep_second> [name1] [name2] ... [nameN]");
        LOG_SIMPLE("  u0 sleep_ex <date> <week_day> <hour> <minute> <second> [name1] [name2] ... [nameN]");
        LOG_SIMPLE("  u0 sleep_pir [sleep_second]");
        return -1;
    }

    if (g_key_value_change_cb == NULL) u0_module_key_value_change_cb_register(u0_module_test_key_value_change_cb);
    if (strcmp(argv[1], "key") == 0) {
        ret = u0_module_get_key_value(&value);
        if (ret != 0) {
            LOG_SIMPLE("get key value failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("key value: %d", value);
    } else if (strcmp(argv[1], "pir") == 0) {
        ret = u0_module_get_pir_value(&value);
        if (ret != 0) {
            LOG_SIMPLE("get pir value failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("pir value: %d", value);
    } else if (strcmp(argv[1], "pwr") == 0) {
        ret = u0_module_get_power_status(&switch_bits);
        if (ret != 0) {
            LOG_SIMPLE("get power status failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("power status: %08X", switch_bits);

    } else if (strcmp(argv[1], "pwr_on") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("Usage:");
            LOG_SIMPLE("  u0 pwr_on <name1> <name2> ... <nameN>");
            return -1;
        }

        ret = u0_module_get_power_status(&switch_bits);
        if (ret != 0) {
            LOG_SIMPLE("get power status failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("before power on, status: %08X", switch_bits);

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "3v3") == 0) {
                switch_bits |= PWR_3V3_SWITCH_BIT;
            } else if (strcmp(argv[i], "wifi") == 0) {
                switch_bits |= PWR_WIFI_SWITCH_BIT;
            } else if (strcmp(argv[i], "aon") == 0) {
                switch_bits |= PWR_AON_SWITCH_BIT;
            } else if (strcmp(argv[i], "n6") == 0) {
                switch_bits |= PWR_N6_SWITCH_BIT;
            } else if (strcmp(argv[i], "ext") == 0) {
                switch_bits |= PWR_EXT_SWITCH_BIT;
            } else if (strcmp(argv[i], "all") == 0) {
                switch_bits |= PWR_ALL_SWITCH_BIT;
            } else {
                LOG_SIMPLE("Unknown power name: %s", argv[i]);
                return -1;
            }
        }

        ret = u0_module_power_control(switch_bits);
        if (ret != 0) {
            LOG_SIMPLE("power on failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("after power on, status: %08X", switch_bits);
    } else if (strcmp(argv[1], "pwr_off") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("Usage:");
            LOG_SIMPLE("  u0 pwr_off <name1> <name2> ... <nameN>");
            return -1;
        }
        ret = u0_module_get_power_status(&switch_bits);
        if (ret != 0) {
            LOG_SIMPLE("get power status failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("before power off, status: %08X", switch_bits);
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "3v3") == 0) {
                switch_bits &= ~PWR_3V3_SWITCH_BIT;
            } else if (strcmp(argv[i], "wifi") == 0) {
                switch_bits &= ~PWR_WIFI_SWITCH_BIT;
            } else if (strcmp(argv[i], "aon") == 0) {
                switch_bits &= ~PWR_AON_SWITCH_BIT;
            } else if (strcmp(argv[i], "n6") == 0) {
                switch_bits &= ~PWR_N6_SWITCH_BIT;
            } else if (strcmp(argv[i], "ext") == 0) {
                switch_bits &= ~PWR_EXT_SWITCH_BIT;
            } else if (strcmp(argv[i], "all") == 0) {
                switch_bits &= ~PWR_ALL_SWITCH_BIT;
            } else {
                LOG_SIMPLE("Unknown power name: %s", argv[i]);
                return -1;
            }
        }
        ret = u0_module_power_control(switch_bits);
        if (ret != 0) {
            LOG_SIMPLE("power off failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("after power off, status: %08X", switch_bits);
    } else if (strcmp(argv[1], "wakeup_flag") == 0) {
        ret = u0_module_get_wakeup_flag(&wakeup_flags);
        if (ret != 0) {
            LOG_SIMPLE("get wakeup flag failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("wakeup flag: %08X", wakeup_flags);
    } else if (strcmp(argv[1], "version") == 0) {
        ms_bridging_version_t version = {0};
        ret = u0_module_get_version(&version);
        if (ret != 0) {
            LOG_SIMPLE("get version failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("U0 version: %d.%d.%d.%d", version.major, version.minor, version.patch, version.build);
    } else if (strcmp(argv[1], "rtc_update") == 0) {
        ret = u0_module_update_rtc_time();
        if (ret != 0) {
            LOG_SIMPLE("update rtc time failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("update rtc time success");
    } else if (strcmp(argv[1], "rtc_sync") == 0) {
        ret = u0_module_sync_rtc_time();
        if (ret != 0) {
            LOG_SIMPLE("sync rtc time failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("sync rtc time success");
    } else if (strcmp(argv[1], "sleep") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("Usage:");
            LOG_SIMPLE("  u0 sleep <sleep_second> [name1] [name2] ... [nameN]");
            return -1;
        }
        sleep_second = atoi(argv[2]);
        wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY;
        if (argc > 3) {
            for (int i = 3; i < argc; i++) {
                if (strcmp(argv[i], "3v3") == 0) {
                    switch_bits |= PWR_3V3_SWITCH_BIT;
                } else if (strcmp(argv[i], "wifi") == 0) {
                    switch_bits |= PWR_WIFI_SWITCH_BIT;
                } else if (strcmp(argv[i], "aon") == 0) {
                    switch_bits |= PWR_AON_SWITCH_BIT;
                } else if (strcmp(argv[i], "n6") == 0) {
                    switch_bits |= PWR_N6_SWITCH_BIT;
                } else if (strcmp(argv[i], "ext") == 0) {
                    switch_bits |= PWR_EXT_SWITCH_BIT;
                } else if (strcmp(argv[i], "all") == 0) {
                    switch_bits |= PWR_ALL_SWITCH_BIT;
                }
            }
            if ((switch_bits & (PWR_WIFI_SWITCH_BIT | PWR_3V3_SWITCH_BIT)) == (PWR_WIFI_SWITCH_BIT | PWR_3V3_SWITCH_BIT)) wakeup_flags |= PWR_WAKEUP_FLAG_SI91X;
            if ((switch_bits & (PWR_EXT_SWITCH_BIT | PWR_3V3_SWITCH_BIT)) == (PWR_EXT_SWITCH_BIT | PWR_3V3_SWITCH_BIT)) wakeup_flags |= PWR_WAKEUP_FLAG_NET;
        }
        ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, sleep_second);
        if (ret != 0) {
            LOG_SIMPLE("enter sleep mode failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("enter sleep mode success");
    } else if (strcmp(argv[1], "sleep_ex") == 0) {
        if (argc < 7) {
            LOG_SIMPLE("Usage:");
            LOG_SIMPLE("  u0 sleep_ex <date> <week_day> <hour> <minute> <second> [name1] [name2] ... [nameN]");
            return -1;
        }
        rtc_alarm_a.is_valid = 1;
        rtc_alarm_a.date = atoi(argv[2]);
        rtc_alarm_a.week_day = atoi(argv[3]);
        rtc_alarm_a.hour = atoi(argv[4]);
        rtc_alarm_a.minute = atoi(argv[5]);
        rtc_alarm_a.second = atoi(argv[6]);
        wakeup_flags = PWR_WAKEUP_FLAG_RTC_ALARM_A | PWR_WAKEUP_FLAG_CONFIG_KEY;
        if (argc > 7) {
            for (int i = 7; i < argc; i++) {
                if (strcmp(argv[i], "3v3") == 0) {
                    switch_bits |= PWR_3V3_SWITCH_BIT;
                }
                else if (strcmp(argv[i], "wifi") == 0) {
                    switch_bits |= PWR_WIFI_SWITCH_BIT;
                }
                else if (strcmp(argv[i], "aon") == 0) {
                    switch_bits |= PWR_AON_SWITCH_BIT;
                }
                else if (strcmp(argv[i], "n6") == 0) {
                    switch_bits |= PWR_N6_SWITCH_BIT;
                }
                else if (strcmp(argv[i], "ext") == 0) {
                    switch_bits |= PWR_EXT_SWITCH_BIT;
                }
                else if (strcmp(argv[i], "all") == 0) {
                    switch_bits |= PWR_ALL_SWITCH_BIT;
                } else {
                    LOG_SIMPLE("Unknown power name: %s", argv[i]);
                    return -1;
                }
            }
            if (switch_bits & (PWR_WIFI_SWITCH_BIT | PWR_3V3_SWITCH_BIT)) wakeup_flags |= PWR_WAKEUP_FLAG_SI91X;
            if (switch_bits & (PWR_EXT_SWITCH_BIT | PWR_3V3_SWITCH_BIT)) wakeup_flags |= PWR_WAKEUP_FLAG_NET;
        }
        ret = u0_module_enter_sleep_mode_ex(wakeup_flags, switch_bits, 0, &rtc_alarm_a, NULL);
        if (ret != 0) {
            LOG_SIMPLE("enter sleep mode failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("enter sleep mode success");
    } else if (strcmp(argv[1], "cfg_pir") == 0) {
        if (argc >= 6) {
            pir_cfg.sensitivity_level = atoi(argv[2]);
            pir_cfg.ignore_time_s = atoi(argv[3]);
            pir_cfg.pulse_count = atoi(argv[4]);
            pir_cfg.window_time_s = atoi(argv[5]);
            pir_cfg.motion_enable = 1;
            pir_cfg.interrupt_src = 0;
            pir_cfg.volt_select = 0;
            pir_cfg.reserved1 = 0;
            pir_cfg.reserved2 = 0;
            ret = u0_module_cfg_pir(&pir_cfg);
        } else {
            ret = u0_module_cfg_pir(NULL);
        }
        if (ret != 0) {
            LOG_SIMPLE("configure pir failed: %d", ret);
            return ret;
        }
        pir_is_inited = 1;
        u0_module_pir_value_change_cb_register(u0_module_test_pir_value_change_cb);
        LOG_SIMPLE("configure pir success");
    } else if (strcmp(argv[1], "sleep_pir") == 0) {
        if (!pir_is_inited) {
            LOG_SIMPLE("pir is not initialized");
            return -1;
        }
        if (argc > 2) sleep_second = atoi(argv[2]);
        wakeup_flags = PWR_WAKEUP_FLAG_RTC_TIMING | PWR_WAKEUP_FLAG_CONFIG_KEY | PWR_WAKEUP_FLAG_PIR_RISING;
        // switch_bits = PWR_3V3_SWITCH_BIT;
        ret = u0_module_enter_sleep_mode(wakeup_flags, switch_bits, sleep_second);
        if (ret != 0) {
            LOG_SIMPLE("enter sleep pir mode failed: %d", ret);
            return ret;
        }
        LOG_SIMPLE("enter sleep pir mode success");
    }
    
    return ret;
}

debug_cmd_reg_t u0_module_cmd_table[] = {
    {"u0",    "u0 chip test tool.",      u0_module_cmd_deal},
};

static void u0_module_cmd_register(void)
{
    debug_cmdline_register(u0_module_cmd_table, sizeof(u0_module_cmd_table) / sizeof(u0_module_cmd_table[0]));
}

void u0_module_register(void)
{
    // Initialize U0
    if (u0_tx_mutex != NULL) return;
    u0_tx_mutex = osMutexNew(NULL);
    if (u0_tx_mutex == NULL) return;
    value_change_event_queue = osMessageQueueNew(VALUE_CHANGE_QUEUE_SIZE, sizeof(value_change_event_t), NULL);
    if (value_change_event_queue == NULL) {
        osMutexDelete(u0_tx_mutex);
        u0_tx_mutex = NULL;
        return;
    }
    u0_handler = ms_bridging_init(u0_module_send_func, u0_module_notify_cb);
    if (u0_handler == NULL) {
        osMessageQueueDelete(value_change_event_queue);
        value_change_event_queue = NULL;
        osMutexDelete(u0_tx_mutex);
        u0_tx_mutex = NULL;
        return;
    }
    MX_UART9_Init();
    // HAL_UART_Receive_IT(&huart9, &u9_rx_res, 1);
    u9_rx_state = HAL_UARTEx_ReceiveToIdle_IT(&huart9, u9_rx_buf, U9_MAX_RECV_LEN);
    osThreadNew(ms_bridging_polling_task, NULL, &ms_bd_task_attributes);
    driver_cmd_register_callback("u0_tool", u0_module_cmd_register);
}

