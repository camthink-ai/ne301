#ifndef __MS_BRIDGING_H
#define __MS_BRIDGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include "Hal/mem.h"
#include "cmsis_os2.h"

#define __DEF_MS_BR_DBG__           (2)
#if __DEF_MS_BR_DBG__ > 0
    #define MS_BR_LOGE(fmt, ...)    printf("E [MS_BR]"fmt"\r\n", ##__VA_ARGS__)
#else
    #define MS_BR_LOGE(fmt, ...)
#endif
#if __DEF_MS_BR_DBG__ > 1
    #define MS_BR_LOGD(fmt, ...)    printf("D [MS_BR]"fmt"\r\n", ##__VA_ARGS__)
#else
    #define MS_BR_LOGD(fmt, ...)
#endif

#define MS_BR_BUF_MAX_SIZE                      (512)
#define MS_BR_FRAME_BUF_NUM                     (8)
#define MS_BR_FRAME_SEND_TIMEOUT_MS             (100)
#define MS_BR_WAIT_ACK_TIMEOUT_MS               (500)
#define MS_BR_WAIT_ACK_DELAY_MS                 (20)
#define MS_BR_RETRY_TIMES                       (3)
#define MS_BR_MALLOC(size)                      hal_mem_alloc_large(size)
#define MS_BR_FREE(ptr)                         hal_mem_free(ptr)
#define MS_BR_MAX_TICK_VALUE                    (osWaitForever)
#define MS_BR_GET_TICK_MS()                     osKernelGetTickCount()      // (osKernelGetTickCount() * 1000U / osKernelGetTickFreq())
#define MS_BR_TICK_DIFF_MS(last, now)           ((now > last) ? (now - last) : (MS_BR_MAX_TICK_VALUE - last + now))
#define MS_BR_DELAY_MS(ms)                      osDelay(ms)
#define MS_BR_SEM_CREATE()                      osSemaphoreNew(1, 0, NULL)
#define MS_BR_SEM_DELETE(sem)                   osSemaphoreDelete((osSemaphoreId_t)sem)
#define MS_BR_SEM_WAIT(sem, ms)                 osSemaphoreAcquire((osSemaphoreId_t)sem, ms)
#define MS_BR_SEM_POST(sem)                     osSemaphoreRelease((osSemaphoreId_t)sem)
// #define MS_BR_MUTEX_CREATE()                    osMutexNew(NULL)
// #define MS_BR_MUTEX_DELETE(mutex)               osMutexDelete((osMutexId_t)mutex)
// #define MS_BR_MUTEX_LOCK(mutex)                 osMutexAcquire((osMutexId_t)mutex, osWaitForever)
// #define MS_BR_MUTEX_UNLOCK(mutex)               osMutexRelease((osMutexId_t)mutex)

#define MS_BR_FRAME_SOF                         0xBD
#define MS_BR_FRAME_HEADER_LEN                  sizeof(ms_bridging_frame_header_t)
#define MS_BR_FRAME_ALL_LEN(frame)              ((frame->header.len > 0) ? (MS_BR_FRAME_HEADER_LEN + frame->header.len + 2) : MS_BR_FRAME_HEADER_LEN)

#define MS_BR_PWR_MODE_NORMAL                   0
#define MS_BR_PWR_MODE_STANDBY                  1
#define MS_BR_PWR_MODE_STOP2                    2

/// @brief Bridging error code
typedef enum
{
    MS_BR_OK = 0,
    MS_BR_ERR_INVALID_ARG = -0XBF,
    MS_BR_ERR_INVALID_STATE,
    MS_BR_ERR_INVALID_SIZE,
    MS_BR_ERR_INVALID_FMT,
    MS_BR_ERR_NO_MEM,
    MS_BR_ERR_NO_FOUND,
    MS_BR_ERR_TIMEOUT,
    MS_BR_ERR_CRC_CHECK,
    MS_BR_ERR_FAILED,
    MS_BR_ERR_UNKNOW,
} ms_bridging_err_t;
/// @brief Bridging frame type
typedef enum
{
    MS_BR_FRAME_TYPE_REQUEST = 0,
    MS_BR_FRAME_TYPE_RESPONSE,
    MS_BR_FRAME_TYPE_EVENT,
    MS_BR_FRAME_TYPE_EVENT_ACK,
} ms_bridging_frame_type_t;
/// @brief Bridging frame command
typedef enum
{
    MS_BR_FRAME_CMD_KEEPLIVE = 0,       /// keep alive 
    MS_BR_FRAME_CMD_GET_TIME,           /// get time
    MS_BR_FRAME_CMD_SET_TIME,           /// set time
    MS_BR_FRAME_CMD_PWR_CTRL,           /// power control
    MS_BR_FRAME_CMD_PWR_STATUS,         /// power status
    MS_BR_FRAME_CMD_WKUP_FLAG,          /// wakeup flag
    MS_BR_FRAME_CMD_KEY_VALUE,          /// key value
    MS_BR_FRAME_CMD_PIR_VALUE,          /// pir value
    MS_BR_FRAME_CMD_CLEAR_FLAG,         /// clear flag
    MS_BR_FRAME_CMD_RST_N6,             /// reset n6
    MS_BR_FRAME_CMD_PIR_CFG,            /// pir config
    MS_BR_FRAME_CMD_USB_VIN_VALUE,      /// usb vin value
    MS_BR_FRAME_CMD_GET_VERSION,        /// get version
} ms_bridging_frame_cmd_t;
#pragma pack(1)
/// @brief Bridging frame header
typedef struct
{
    uint8_t sof;
    uint16_t id;
    uint16_t len;
    uint16_t type;
    uint16_t cmd;
    uint16_t crc;
} ms_bridging_frame_header_t;
/// @brief Bridging frame
typedef struct
{
    ms_bridging_frame_header_t header;
    void *data;
    uint16_t data_crc;
    uint8_t is_valid;
} ms_bridging_frame_t;
/// @brief Bridging time
typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t week;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} ms_bridging_time_t;
/// @brief Bridging rtc alarm
typedef struct
{
    uint8_t is_valid;       // Whether valid
    uint8_t week_day;       // Day of week (1~7), 0 means disabled (high priority)
    uint8_t date;           // Date (1~31), 0 means disabled (low priority)
    uint8_t hour;           // Hour (0~23)
    uint8_t minute;         // Minute (0~59)
    uint8_t second;         // Second (0~59)
} ms_bridging_alarm_t;
/// @brief Bridging power control
typedef struct
{
    uint8_t power_mode;     // 0: normal, 1: standby, 2: stop2

    uint32_t switch_bits;
    uint32_t wakeup_flags;
    uint32_t sleep_second;
    ms_bridging_alarm_t alarm_a;
    ms_bridging_alarm_t alarm_b;
} ms_bridging_power_ctrl_t;
/// @brief Bridging pir config
typedef struct
{
    uint8_t sensitivity_level;  // [7:0] Sensitivity setting, recommended to set greater than 20. If the environment has no interferpire, it can be set to a minimum value of 10. The smaller the value, the more sensitive, but the easier it is to trigger false alarms. (Effective in interrupt mode, ineffective in read mode)
    uint8_t ignore_time_s;      // [3:0] The time to ignore motion detection after the interrupt output switches back to 0, range: 0.5s ~ 8s, interrupt time = register value * 0.5s + 0.5s. (Effective in interrupt mode, ineffective in read mode)
    uint8_t pulse_count;        // [1:0] Pulse counter, the number of pulses required to be reached within the window time. Range: 1 ~ 4 signed pulses, pulse count = register value + 1. The larger the value, the stronger the anti-interferpire ability, but the sensitivity is slightly reduced. (Effective in interrupt mode, ineffective in read mode)
    uint8_t window_time_s;      // [1:0] Window time, range: 2s~8s, window time = register value * 2s + 2s. (Effective in interrupt mode, ineffective in read mode)
    uint8_t motion_enable;      // [0] Must be 1
    uint8_t interrupt_src;      // Interrupt source. 0 = motion detection, 1 = raw data from the filter. Read mode must be set to 1.
    uint8_t volt_select;        // [1:0] Multiplex ADC resources. The input sources selectable for the ADC are as follows: PIR signal BFP output = 0; PIR signal LPF output = 1; power supply voltage = 2; temperature sensor = 3; choose as needed
    uint8_t reserved1;          // Set to 0
    uint8_t reserved2;          // Set to 0
} ms_bridging_pir_cfg_t;
/// @brief Bridging version info
typedef struct
{
    int major;                  // Major version number
    int minor;                  // Minor version number
    int patch;                  // Patch version number
    int build;                  // Build version number
} ms_bridging_version_t;
#pragma pack(0)
/// @brief Bridging send function (send raw data function, return 0 means success, return orther means failed)
typedef int (*ms_bridging_send_func_t)(uint8_t *buf, uint16_t len, uint32_t timeout_ms);
/// @brief Bridging event callback (MS_BR_FRAME_TYPE_REQUEST, MS_BR_FRAME_TYPE_EVENT)
typedef void (*ms_bridging_notify_cb_t)(void *handler, ms_bridging_frame_t *frame);
/// @brief Bridging handler
typedef struct 
{
    uint8_t is_ready;
    uint16_t global_frame_id;
    uint16_t input_frame_len;
    ms_bridging_frame_t input_frame;

    ms_bridging_frame_t ack_frame[MS_BR_FRAME_BUF_NUM];
    uint32_t ack_frame_received_tick[MS_BR_FRAME_BUF_NUM];

    ms_bridging_frame_t notify_frame[MS_BR_FRAME_BUF_NUM];
    
    ms_bridging_send_func_t send_func;
    ms_bridging_notify_cb_t notify_cb;


#if defined(MS_BR_SEM_CREATE) && defined(MS_BR_SEM_DELETE) && defined(MS_BR_SEM_WAIT) && defined(MS_BR_SEM_POST)
    void *ack_sem;
    void *notify_sem;
#endif
} ms_bridging_handler_t;

/// @brief Initialize the bridging handler
/// @param send_func send raw data function
/// @param event_cb event callback(MS_BR_FRAME_TYPE_REQUEST, MS_BR_FRAME_TYPE_EVENT)
/// @return bridging handler
ms_bridging_handler_t *ms_bridging_init(ms_bridging_send_func_t send_func, ms_bridging_notify_cb_t event_cb);

/// @brief Deinitialize the bridging handler
/// @param handler bridging handler
void ms_bridging_deinit(ms_bridging_handler_t *handler);

/// @brief Receive raw data invoke (May be called within the interrupt)
/// @param handler bridging handler
/// @param buf receive buffer
/// @param len receive buffer length
void ms_bridging_recv(ms_bridging_handler_t *handler, uint8_t *buf, uint16_t len);

/// @brief Polling the bridging handler (Traverse notification frames and call notification callbacks)
/// @param handler bridging handler
void ms_bridging_polling(ms_bridging_handler_t *handler);

/// @brief Initiate a request to the other party and wait for a response
/// @param handler bridging handler
/// @param cmd request instruction
/// @param data request data
/// @param len request data length
/// @param data_out response data (need to free by caller)
/// @param len_out response data length
/// @return Bridging error code
int ms_bridging_request(ms_bridging_handler_t *handler, ms_bridging_frame_cmd_t cmd, void *data, uint16_t len, void **data_out, uint16_t *len_out);

/// @brief Send a response to the other party
/// @param handler bridging handler
/// @param frame response frame
/// @param data response data
/// @param len response data length
/// @return Bridging error code
int ms_bridging_response(ms_bridging_handler_t *handler, ms_bridging_frame_t *req_frame, void *data, uint16_t len);

/// @brief Send event notification and wait for confirmation from the other party
/// @param handler bridging handler
/// @param cmd event instruction
/// @param data event data
/// @param len event data length
/// @return Bridging error code
int ms_bridging_send_event(ms_bridging_handler_t *handler, ms_bridging_frame_cmd_t cmd, void *data, uint16_t len);

/// @brief Send a event ack to the other party
/// @param handler bridging handler
/// @param frame event frame
/// @return Bridging error code
int ms_bridging_event_ack(ms_bridging_handler_t *handler, ms_bridging_frame_t *event_frame);

/// @brief Send a keep alive to the other party
/// @param handler bridging handler
/// @return Bridging error code
int ms_bridging_request_keep_alive(ms_bridging_handler_t *handler);

/// @brief Send a get time request to the other party
/// @param handler bridging handler
/// @param time time
/// @return Bridging error code
int ms_bridging_request_get_time(ms_bridging_handler_t *handler, ms_bridging_time_t *time);

/// @brief Send a set time request to the other party
/// @param handler bridging handler
/// @param time time
/// @return Bridging error code
int ms_bridging_request_set_time(ms_bridging_handler_t *handler, ms_bridging_time_t *time);

/// @brief Send a power control request to the other party
/// @param handler bridging handler
/// @param power_ctrl power control
/// @return Bridging error code
int ms_bridging_request_power_control(ms_bridging_handler_t *handler, ms_bridging_power_ctrl_t *power_ctrl);

/// @brief Send a power status request to the other party
/// @param handler bridging handler
/// @param power_status power status
/// @return Bridging error code
int ms_bridging_request_power_status(ms_bridging_handler_t *handler, uint32_t *switch_bits);

/// @brief Send a power flag request to the other party
/// @param handler bridging handler
/// @param power_flag power flag
/// @return Bridging error code
int ms_bridging_request_wakeup_flag(ms_bridging_handler_t *handler, uint32_t *wakeup_flag);

/// @brief Send a clear flag request to the other party
/// @param handler bridging handler
/// @return Bridging error code
int ms_bridging_request_clear_flag(ms_bridging_handler_t *handler);

/// @brief Send a reset n6 request to the other party
/// @param handler bridging handler
/// @return Bridging error code
int ms_bridging_request_reset_n6(ms_bridging_handler_t *handler);

/// @brief Send a key value request to the other party
/// @param handler bridging handler
/// @param key_value key value
/// @return Bridging error code
int ms_bridging_request_key_value(ms_bridging_handler_t *handler, uint32_t *key_value);

/// @brief Send a key value event to the other party
/// @param handler bridging handler
/// @param key_value key value
/// @return Bridging error code
int ms_bridging_event_key_value(ms_bridging_handler_t *handler, uint32_t key_value);

/// @brief Send a pir value request to the other party
/// @param handler bridging handler
/// @param pir_value pir value
/// @return Bridging error code
int ms_bridging_request_pir_value(ms_bridging_handler_t *handler, uint32_t *pir_value);

/// @brief Send a pir value event to the other party
/// @param handler bridging handler
/// @param pir_value pir value
/// @return Bridging error code
int ms_bridging_event_pir_value(ms_bridging_handler_t *handler, uint32_t pir_value);

/// @brief Send a pir config request to the other party
/// @param handler bridging handler
/// @param pir_cfg pir config
/// @return Bridging error code
int ms_bridging_request_pir_cfg(ms_bridging_handler_t *handler, ms_bridging_pir_cfg_t *pir_cfg);

/// @brief Send a get version request to the other party
/// @param handler bridging handler
/// @param version version info
/// @return Bridging error code
int ms_bridging_request_version(ms_bridging_handler_t *handler, ms_bridging_version_t *version);

/// @brief Send a usbin value request to the other party
/// @param handler bridging handler
/// @param usbin_value usbin value
/// @return Bridging error code
int ms_bridging_request_usbin_value(ms_bridging_handler_t *handler, uint32_t *usbin_value);

/// @brief Get version from string
/// @param version_str version string
/// @param version version info
/// @return Bridging error code
int ms_bridging_get_version_from_str(const char *version_str, ms_bridging_version_t *version);

#ifdef __cplusplus
}
#endif
#endif /* __MS_BRIDGING_H */
