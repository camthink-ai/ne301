/*******************************************************************************
 * @file  rsi_common_apis.h
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#ifndef RSI_COMMON_APIS_H
#define RSI_COMMON_APIS_H

#if defined(SL_SI91X_PRINT_DBG_LOG)
#include <stdio.h>
#include "rsi_common.h"
#endif
#include <stdint.h>
#include <stdbool.h>
/******************************************************
 * *                      Macros
 * ******************************************************/
#if !(defined(FRDM_K28F) || defined(MXRT_595s))
#if defined(SL_SI91X_PRINT_DBG_LOG)
#define LOG_PRINT(...)                                               \
  {                                                                  \
    osMutexAcquire(rsi_driver_cb->debug_prints_mutex, 0xFFFFFFFFUL); \
    printf(__VA_ARGS__);                                             \
    osMutexRelease(rsi_driver_cb->debug_prints_mutex);               \
  }
#elif defined(DEBUGOUT)
#define LOG_PRINT(...) DEBUGOUT(__VA_ARGS__)
#else
#define LOG_PRINT(...)
#endif
#endif

// success return value
#define RSI_SUCCESS 0

// failure return value
#define RSI_FAILURE -1

// Feature not supported
#define RSI_FEATURE_NOT_SUPPORTED 0x00F7

//Load Image types

#ifndef LOAD_NWP_FW // Defined in sl_si91x_constants.h to prevent redefinition errors, so it is undefined here.
#define LOAD_NWP_FW 0x31
#endif
#define LOAD_DEFAULT_NWP_FW_ACTIVE_LOW 0x71

// Upgrade images
#define BURN_NWP_FW 'B'

// Upgrade images
#define RSI_JUMP_TO_PC 'J'

#define RSI_ACTIVE_LOW_INTR  0x2
#define RSI_ACTIVE_HIGH_INTR 0x0
#define RSI_RX_BUFFER_CHECK  0x15
// TX buffer full macro
#define RSI_TX_BUFFER_FULL -2
#define RSI_SOFT_RESET     0
#define RSI_HARD_RESET     1

/******************************************************
 * *                    Constants
 * ******************************************************/
/******************************************************
 * *                   Enumerations
 * ******************************************************/
// enumerations for power save profile modes
typedef enum rsi_power_save_profile_mode_e {
  RSI_ACTIVE        = 0,
  RSI_SLEEP_MODE_1  = 1,
  RSI_SLEEP_MODE_2  = 2,
  RSI_SLEEP_MODE_8  = 8,
  RSI_SLEEP_MODE_10 = 10,
} rsi_power_save_profile_mode_t;

// enumerations for power save profile types
typedef enum rsi_power_save_profile_type_e {
  RSI_MAX_PSP  = 0,
  RSI_FAST_PSP = 1,
  RSI_UAPSD    = 2
} rsi_power_save_profile_type_t;

/******************************************************
 * *                 Type Definitions
 * ******************************************************/
/******************************************************
 * *                    Structures
 * ******************************************************/
/******************************************************
 * *                 Global Variables
 * ******************************************************/
/******************************************************
 * *               Function Declarations
 * ******************************************************/

extern int32_t rsi_ble_driver_init(uint8_t *buffer, uint32_t length);
extern int32_t rsi_ble_driver_deinit(void);

/*==============================================*/
/**
 * @fn         int32_t rsi_ble_enable(void)
 * @brief      Enables the BLE stack at runtime. This is a blocking API and unblocks only when
 *             a response is received or the command times out.
 *
 * @pre        Pre-conditions (application responsibility):
 *             - Call after [sl_wifi_init()](../wiseconnect-api-reference-guide-wi-fi/wifi-common-api#sl-wifi-init)
 *               / driver init; the device must be initialized.
 *             - BLE re-enable does not overlap an active WLAN connection used for the
 *               post-provision segment i.e., Wi-Fi disconnect has to be called before re-enable.
 *             - Invoke this API only from the BLE task context or another context that is
 *               permitted to issue BLE/common commands.
 *             - Wait for the BLE enable operation to complete and consume the result before
 *               issuing another rsi_ble_enable() request.
 *             - Refer to the wifi_station_ble_provisioning_aws and wifi_station_ble_throughput_app
 *               example applications for implementation details.
 *
 * @return     The following values are returned:
 *             - 0 — BLE was disabled; enable command completed and runtime state
 *               is updated to enabled.
 *             - -3 — `rsi_ble_enable()` called when BLE
 *               is already enabled (no firmware command sent).
 *             - 0x11 — Driver/device not initialized.
 *             - 0x07 — Firmware response not received within
 *               `SLI_COMMON_RSP_BLE_ENABLE_DISABLE_WAIT_TIME`.
 */
extern int32_t rsi_ble_enable(void);

/*==============================================*/
/**
 * @fn         int32_t rsi_ble_disable(void)
 * @brief      Disables the BLE stack at runtime. This is a blocking API and unblocks only when
 *             a response is received or the command times out. Disabling only stops the BLE
 *             stack without stopping advertising, scanning, or terminating connections. Before calling this API, the
 *             application must stop all BLE activities.
 *
 * @pre        Pre-conditions (application responsibility):
 *             - Call after [sl_wifi_init()](../wiseconnect-api-reference-guide-wi-fi/wifi-common-api#sl-wifi-init)
 *               / driver init; the device must be initialized.
 *             - Stop all BLE advertising and scanning, and disconnect all existing BLE links
 *               before calling this API.
 *             - Invoke this API only from the BLE task context or another context that is
 *               permitted to issue BLE/common commands.
 *             - Wait for the BLE disable operation to complete and consume the result before
 *               issuing another rsi_ble_disable() request.
 *             - Refer to the wifi_station_ble_provisioning_aws and wifi_station_ble_throughput_app
 *               example applications for implementation details.
 *
 * @return     The following values are returned:
 *             - 0 — BLE was enabled; disable command completed and runtime state
 *               is updated to disabled.
 *             - -3 — `rsi_ble_disable()` called when BLE
 *               is already disabled (no firmware command sent).
 *             - -57 — Disable requested while a BLE connection is still up. Application
 *               must complete disconnect before retrying.
 *             - 0x11 — Driver/device not initialized.
 *             - 0x07 — Firmware response not received within
 *               `SLI_COMMON_RSP_BLE_ENABLE_DISABLE_WAIT_TIME`.
 */
extern int32_t rsi_ble_disable(void);
extern void rsi_ble_set_opermode_state(bool is_ble_enabled);
extern bool rsi_ble_state_is_enabled(void);
extern int32_t rsi_get_fw_version(uint8_t *response, uint16_t length);
extern int32_t rsi_get_module_type(uint8_t *response);
extern int32_t rsi_common_debug_log(int32_t assertion_type, int32_t assertion_level);
extern int32_t rsi_get_ram_log(uint32_t addr, uint32_t length);
extern int32_t rsi_driver_version(uint8_t *request);

/**
 * @brief      Wait for BT card ready
 * @return     void
 * */
void rsi_bt_common_init(void);

#endif
