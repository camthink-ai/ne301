/*******************************************************************************
 * @file  rsi_common_apis.c
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

#include "rsi_common.h"

#include "rsi_ble.h"

#include "sl_si91x_status.h"
#include "rsi_ble_common_config.h"
#include "sli_wifi_utility.h"
#include "sli_wifi_constants.h"
#include "rsi_bt_common.h"
#include <stdbool.h>

extern bool device_initialized;
/*
  Global Variables
 * */
rsi_driver_cb_t *rsi_driver_cb = NULL;

typedef enum { BLE_STATE_DISABLED = 0, BLE_STATE_ENABLED = 1 } ble_state_t;

static volatile ble_state_t ble_runtime_state = BLE_STATE_DISABLED;

int32_t rsi_driver_memory_estimate(void);

/** @addtogroup COMMON 
* @{
*/
/*==============================================*/
/**
 *
 * @brief      Provide the memory required by the application. This is a non-blocking API.
 * @param[in]  Void
 * @return     Driver pool size
 *
 */

int32_t rsi_driver_memory_estimate(void)
{
  uint32_t actual_length = 0;

  // Calculate the Memory length of the application
  actual_length += RSI_DRIVER_POOL_SIZE;
  return actual_length;
}

/*==============================================*/
/**
 *
 * @brief      Initialize WiSeConnect ble driver.  This is a non-blocking API.
 *             Designate memory to all driver components from the buffer provided by the application.
 * @param[in]  buffer      -    Pointer to buffer from application. \n Driver uses this buffer to hold driver control for its operation.
 * @param[in]  length      -    Length of the buffer.
 * @return     **Success** -    Returns the memory used, which is less than or equal to buffer length provided. \n
 *             **Failure** -    Non-Zero values\n
 *
 *             			**RSI_ERROR_TIMEOUT**         -    If UART initialization fails in SPI / UART mode   \n
 */

/** @} */
uint8_t *buffer_addr = NULL;
int32_t rsi_ble_driver_init(uint8_t *buffer, uint32_t length)
{
  uint32_t actual_length = 0;

  if (((uintptr_t)buffer & 3) != 0) // To avoid compiler warning, replace uint32_t with uintptr_t
  {
    // Making buffer 4 byte aligned
    // Length -= (4 - ((uint32_t)buffer & 3));
    // To avoid compiler warning, replace uint32_t with uintptr_t
    length -= (4 - ((uintptr_t)buffer & 3));
    buffer = (uint8_t *)(((uintptr_t)buffer + 3) & ~3);
  }

  // Memset user buffer
  memset(buffer, 0, length);

  actual_length += rsi_driver_memory_estimate();
  // If length is not sufficient
  if (length < actual_length) {
    return actual_length;
  }
  buffer_addr = buffer;

  // Store length minus any alignment bytes to first 32-bit address in buffer.
  *(uint32_t *)buffer = length;
  buffer += sizeof(uint32_t);

  // Designate memory for driver cb
  rsi_driver_cb = (rsi_driver_cb_t *)buffer;
  buffer += sizeof(rsi_driver_cb_t);

#ifdef SL_SI91X_ENABLE_LITTLE_ENDIAN
  rsi_driver_cb->endian = IS_LITTLE_ENDIAN;
#else
  rsi_driver_cb->endian = IS_BIG_ENDIAN;
#endif

#if defined(SL_SI91X_PRINT_DBG_LOG)
  // Creates debug prints mutex
  rsi_driver_cb->debug_prints_mutex = osMutexNew(NULL);
#endif

  // Designate memory for bt_common_cb
  rsi_driver_cb->bt_common_cb = (rsi_bt_cb_t *)buffer;
  buffer += ((sizeof(rsi_bt_cb_t) + 3) & ~3);

  // Initialize bt_common_cb
  rsi_bt_cb_init(rsi_driver_cb->bt_common_cb, RSI_PROTO_BT_COMMON);

  // Save the expected response type for BLE card ready event from NWP
  rsi_driver_cb->bt_common_cb->expected_response_type = RSI_BT_EVENT_CARD_READY;
  rsi_driver_cb->bt_common_cb->sync_rsp               = 1;

  // Designate memory for ble_cb
  rsi_driver_cb->ble_cb = (rsi_bt_cb_t *)buffer;
  buffer += ((sizeof(rsi_bt_cb_t) + 3) & ~3);

  // Initialize ble_cb
  rsi_bt_cb_init(rsi_driver_cb->ble_cb, RSI_PROTO_BLE);

  // Designate memory for bt_common_cb
  rsi_driver_cb->bt_global_cb = (rsi_bt_global_cb_t *)buffer;
  buffer += sizeof(rsi_bt_global_cb_t);

  // Fill in bt_global_cb
  buffer += rsi_bt_global_cb_init(rsi_driver_cb, buffer);

  if (length < (uint32_t)(buffer - buffer_addr)) {
    SL_PRINTF(SL_DRIVER_INIT_INSUFFICIENT_BUFFER_2, COMMON, LOG_ERROR, "length: %4x", (uint32_t)(buffer - buffer_addr));
    return buffer - buffer_addr;
  }

  // Update state
  rsi_driver_cb->device_state = RSI_DRIVER_INIT_DONE;

  rsi_driver_cb->ble_cb->ble_buff_total_avail_cnt = RSI_BLE_NUM_CONN_EVENTS;
  rsi_driver_cb->ble_cb->ble_buff_total_mutex     = osMutexNew(NULL);

  SL_PRINTF(SL_DRIVER_INIT_EXIT, COMMON, LOG_INFO, "actual_length=%4x", actual_length);
  return actual_length;
}

//======================================================
/**
 *
 * @brief       De-Initialize driver components. Clear all the memory given for driver operations in \ref rsi_ble_driver_init() API.
 * In OS case,  User need to take care of OS variables initialized in \ref rsi_ble_driver_init(). This is a non-blocking API.
 * This API must be called by the thread/task/Master thread that it is not dependent on.
 * OS variables allocated/initialized in \ref rsi_ble_driver_init() API.
 * @pre 		Need to call after the driver initialization
 * @param[in]   Void
 * @return      0              - Success \n
 *              Non-Zero Value - Failure
 */

int32_t rsi_ble_driver_deinit(void)
{
  SL_PRINTF(SL_DRIVER_DEINIT_ENTRY, COMMON, LOG_INFO);

  if (rsi_driver_cb->device_state < RSI_DRIVER_INIT_DONE) {
    // Command given in wrong state
    return RSI_ERROR_COMMAND_GIVEN_IN_WRONG_STATE;
  }
  // Check if buffer is enough for driver components
  if (buffer_addr == NULL) {
    return RSI_FAILURE;
  }

#if defined(SL_SI91X_PRINT_DBG_LOG)
  if (rsi_driver_cb->debug_prints_mutex) {
    osMutexDelete(rsi_driver_cb->debug_prints_mutex);
  }
#endif

  // Delete BT semaphore
  if (rsi_driver_cb->bt_common_cb->bt_cmd_sem) {
    osSemaphoreDelete(rsi_driver_cb->bt_common_cb->bt_cmd_sem);
  }

  if (rsi_driver_cb->bt_common_cb->bt_sem) {
    osSemaphoreDelete(rsi_driver_cb->bt_common_cb->bt_sem);
  }

  if (rsi_driver_cb->ble_cb->bt_cmd_sem) {
    osSemaphoreDelete(rsi_driver_cb->ble_cb->bt_cmd_sem);
  }

  if (rsi_driver_cb->ble_cb->bt_sem) {
    osSemaphoreDelete(rsi_driver_cb->ble_cb->bt_sem);
  }
  if (rsi_driver_cb->ble_cb->ble_buff_total_mutex) {
    osMutexDelete(rsi_driver_cb->ble_cb->ble_buff_total_mutex);
  }

  rsi_driver_cb->device_state = RSI_DEVICE_STATE_NONE;
  SL_PRINTF(SL_DRIVER_DEINIT_SEMAPHORE_DESTROY_FAILED_26, COMMON, LOG_INFO);
  return RSI_SUCCESS;
}

/*==============================================*/
/**
 * @brief      Enable BLE at runtime. See @ref rsi_ble_enable() in rsi_common_apis.h for
 *             pre-conditions, application queue usage, and full return-code list.
 * @return     RSI_SUCCESS on success; otherwise see rsi_ble_enable() documentation.
 */
int32_t rsi_ble_enable(void)
{
  sl_status_t status;
  uint8_t sub_cmd = SLI_BLE_SUB_CMD_ENABLE;

  if (!device_initialized) {
    return (int32_t)SL_STATUS_NOT_INITIALIZED;
  }

  if (ble_runtime_state == BLE_STATE_ENABLED) {
    return RSI_ERROR_COMMAND_GIVEN_IN_WRONG_STATE;
  }

  status = sli_wifi_send_command(SLI_COMMON_REQ_ENABLE_DISABLE_BLE,
                                 SLI_WIFI_COMMON_CMD,
                                 &sub_cmd,
                                 sizeof(sub_cmd),
                                 SLI_COMMON_RSP_BLE_ENABLE_DISABLE_WAIT_TIME,
                                 NULL,
                                 NULL);

  if (status == SL_STATUS_OK) {
    ble_runtime_state = BLE_STATE_ENABLED;
  }

  return (int32_t)status;
}

/*==============================================*/
/**
 * @brief      Disable BLE at runtime. See @ref rsi_ble_disable() in rsi_common_apis.h for
 *             quiesce requirements (stop adv/scan, disconnect all links), application queue
 *             usage, and full return-code list.
 * @return     RSI_SUCCESS on success; otherwise see rsi_ble_disable() documentation.
 */
int32_t rsi_ble_disable(void)
{
  sl_status_t status;
  uint8_t sub_cmd = SLI_BLE_SUB_CMD_DISABLE;

  if (!device_initialized) {
    return (int32_t)SL_STATUS_NOT_INITIALIZED;
  }

  if (ble_runtime_state == BLE_STATE_DISABLED) {
    return RSI_ERROR_COMMAND_GIVEN_IN_WRONG_STATE;
  }

  // Check if any BLE devices are connected
  if (rsi_ble_is_device_connected()) {
    return RSI_ERROR_BLE_ACTIVITY_PENDING;
  }

  status = sli_wifi_send_command(SLI_COMMON_REQ_ENABLE_DISABLE_BLE,
                                 SLI_WIFI_COMMON_CMD,
                                 &sub_cmd,
                                 sizeof(sub_cmd),
                                 SLI_COMMON_RSP_BLE_ENABLE_DISABLE_WAIT_TIME,
                                 NULL,
                                 NULL);

  if (status == SL_STATUS_OK) {
    ble_runtime_state = BLE_STATE_DISABLED;
  }

  return (int32_t)status;
}

/*==============================================*/
/**
 * @brief      Set BLE runtime state based on opermode configuration.
 *             Should be called during opermode initialization.
 * @param[in]  is_ble_enabled - true if BLE is enabled in opermode, false otherwise
 * @return     void
 */
void rsi_ble_set_opermode_state(bool is_ble_enabled)
{
  ble_runtime_state = is_ble_enabled ? BLE_STATE_ENABLED : BLE_STATE_DISABLED;
}

/*==============================================*/
/**
 * @brief      Check if BLE is currently enabled.
 * @return     true if BLE is enabled, false if disabled
 */
bool rsi_ble_state_is_enabled(void)
{
  return (ble_runtime_state == BLE_STATE_ENABLED);
}

/** @} */
