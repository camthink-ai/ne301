/***************************************************************************/ /**
 * @file sli_hal_si91x.c
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
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
#include "sli_queue_manager.h"
#include "sli_routing_utility.h"
#include "sli_hal_si91x_packet.h"
#include "sli_hal_si91x.h"
#include "sl_constants.h"
#include "sl_rsi_utility.h"
#include "sl_si91x_host_interface.h"
#include "sli_si91x_wifi_event_handler.h"
#include "sli_si91x_wifi_command_engine.h"
#include "sli_wifi_command_engine_config.h"
#include "sl_status.h"
#include "cmsis_os2.h"
#include "sl_cmsis_utility.h"
#include "sli_wifi_utility.h"

#ifdef SLI_SI91X_MCU_INTERFACE
#include "rsi_m4.h"
#endif
#include "sli_code_classification.h"
/******************************************************
 *               Structures and Typedefs
******************************************************/
typedef struct {
  uint16_t packet_type;
  void *data; ///< Pointer to the data.
  void *context;
  sli_routing_utility_packet_status_handler_t packet_status_handler; ///< Packet status handler.
} sli_si91x_hal_packet_t;

/******************************************************
 *               Macros
******************************************************/
// M55 port: SDK default is 1024 (sized for Cortex-M4 EFR32 reference). On
// Cortex-M55 the wider ARMv8-M stack frames push real usage to ~1067 B under
// iperf, so 1024 overflowed by ~43 B and trampled an adjacent TX_THREAD /
// TX_MUTEX control block -> HardFault in _tx_mutex_priority_change /
// _tx_thread_system_suspend (wild thread_ptr 0x31226xxx). Do NOT lower below
// ~1536; 2048 gives ~2x headroom over the ~1067 B peak. (conan pristine = 1024.)
#define SLI_HAL_SI91X_THREAD_STACK 2048        ///< Thread stack size
#define SLI_HAL_SI91X_THREAD_NAME  "hal_si91x" ///< Thread name for HAL

#define SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(return_value, expected_value, message) \
  do {                                                                            \
    if ((return_value) != (expected_value)) {                                     \
      SL_DEBUG_LOG_V2(DEBUG, message, return_value);                              \
    }                                                                             \
  } while (0)

#define SLI_HAL_SI91X_IS_FLASH_COMMAND(command)                                                \
  (((command) == SLI_COMMON_RSP_TA_M4_COMMANDS) || ((command) == SLI_WIFI_REQ_SET_CERTIFICATE) \
   || ((command) == SLI_COMMON_RSP_SOFT_RESET)) ///< Check if the command is a flash command

#define SLI_HAL_SI91X_IS_GLOBAL_FRAME(command)                                      \
  (((command) == SLI_WIFI_RSP_OPERMODE) || ((command) == SLI_COMMON_RSP_SOFT_RESET) \
   || ((command) == SLI_COMMON_RSP_PWRMODE) || ((command) == SLI_COMMON_RSP_ENABLE_DISABLE_BLE))
/******************************************************
 *               Function Declarations
******************************************************/
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_hal_si91x_thread(void *args);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_hal_si91x_send_packet(void *packet,
                                             sli_queue_t *tx_queue,
                                             uint32_t event_flag,
                                             uint32_t packet_size,
                                             sli_routing_utility_packet_status_handler_t packet_status_handler,
                                             void *context);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_hal_si91x_send_packet_to_bus(sl_wifi_system_packet_t *buffer);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static uint32_t sli_hal_si91x_wait_for_event(uint32_t event_mask, uint32_t timeout);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static uint32_t sli_hal_si91x_get_wait_time(bool Is_rx_buffer_submitted,
                                            uint32_t events_received,
                                            uint32_t interrupt_status);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_hal_si91x_handle_ble_tx_event(uint32_t *events_received);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_hal_si91x_handle_wifi_tx_event(uint32_t *events_received);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_hal_si91x_handle_rx_event(sl_wifi_buffer_t *rx_buffer,
                                          bool *is_rx_buffer_submitted,
                                          uint32_t *events_received);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_hal_si91x_handle_thread_terminate_event(void);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_cleanup_flags_and_queues(void);

sl_status_t sli_si91x_req_wakeup(void);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_HAL_SI91X, SL_CODE_CLASS_TIME_CRITICAL)
sl_status_t sli_submit_rx_buffer(uint32_t timeout);
/******************************************************
 *               Variable Definitions
 ******************************************************/
// TX and RX queue handles
static sli_queue_t wifi_tx_queue_handle;
static sli_queue_t ble_tx_queue_handle;
static sli_queue_t rx_queue_handle;

// Event listener thread ID
static osThreadId_t hal_thread_ID = NULL;

// Event flags
static osEventFlagsId_t sli_hal_si91x_events = NULL;

// Global frame pending flag
// Power save, soft reset, operational mode response are global frames
// Other commands are not allowed to be sent while a global frame is being processed by the TA
static bool is_global_frame_pending = false;

// Routing entries for HAL
static sli_routing_entry_t hal_si91x_routing_entires[SLI_HAL_SI91X_MAX] = { [SLI_HAL_SI91X_WIFI_COMMON_PACKET] = {
                                                                             .destination_packet_handler =
                                                                                 sli_si91x_wifi_command_engine_packet_handler,
                                                                             .packet_status_handler = NULL,
                                                                             .packet_type =
                                                                               SLI_HAL_SI91X_WIFI_COMMON_PACKET,
                                                                             },
                                                                            [SLI_HAL_SI91X_DATA_PACKET] = {
                                                                              .destination_packet_handler =
                                                                                 sli_si91x_wifi_data_packet_handler,
                                                                              .packet_status_handler = NULL,
                                                                              .packet_type =
                                                                                SLI_HAL_SI91X_DATA_PACKET,
                                                                             },
                                                                            [SLI_HAL_SI91X_BLUETOOTH_PACKET] = {
                                                                              .destination_packet_handler =
                                                                                 sli_si91x_wifi_ble_packet_handler,
                                                                              .packet_status_handler = NULL,
                                                                              .packet_type =
                                                                                SLI_HAL_SI91X_BLUETOOTH_PACKET,
                                                                             },
                                                                            [SLI_HAL_SI91X_PACKET] = {
                                                                              .destination_packet_handler =
                                                                                 sli_si91x_wifi_command_engine_packet_handler,
                                                                              .packet_status_handler = NULL,
                                                                              .packet_type =
                                                                                  SLI_HAL_SI91X_BLUETOOTH_PACKET,
                                                                            },
                                                                            [SLI_HAL_SI91X_NWP_LOG_PACKET] = {
                                                                              .destination_packet_handler =
                                                                                 sli_si91x_wifi_nwp_log_packet_handler,
                                                                              .packet_status_handler = NULL,
                                                                              .packet_type =
                                                                                SLI_HAL_SI91X_NWP_LOG_PACKET,
                                                                            },
                                                                          };

// Routing table for HAL
sli_routing_table_t hal_si91x_routing_table = { .routing_table      = hal_si91x_routing_entires,
                                                .routing_table_size = SLI_HAL_SI91X_MAX };
/******************************************************
 *              Static Function Definitions
******************************************************/

static sl_status_t sli_hal_si91x_send_packet(void *packet,
                                             sli_queue_t *tx_queue,
                                             uint32_t event_flag,
                                             uint32_t packet_size,
                                             sli_routing_utility_packet_status_handler_t packet_status_handler,
                                             void *context)
{

  // Calling this method after calling deinit is fatal.
  UNUSED_PARAMETER(packet_size);

  sli_si91x_hal_packet_t *hal_packet = NULL;

  sl_status_t status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_HAL_METADATA_POOL,
                                                          SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                                          1000,
                                                          (sli_buffer_t *)&hal_packet);

  VERIFY_STATUS_AND_RETURN(status);

  hal_packet->packet_type           = 0;
  hal_packet->data                  = packet;
  hal_packet->packet_status_handler = packet_status_handler;
  hal_packet->context               = context;

  status = sli_queue_manager_enqueue(tx_queue, (sl_slist_node_t *)hal_packet);

  if (status != SL_STATUS_OK) {
    sli_buffer_manager_free_buffer((sli_buffer_t *)hal_packet);
    return status;
  }

  osEventFlagsSet(sli_hal_si91x_events, event_flag);

  return SL_STATUS_IN_PROGRESS;
}

static uint32_t sli_hal_si91x_get_wait_time(bool Is_rx_buffer_submitted,
                                            uint32_t events_received,
                                            uint32_t interrupt_status)
{
  uint32_t wait_time = 0;

  if (events_received == 0) {
    // If there are no events, check if the RX buffer has been submitted
    // If the RX buffer has been submitted, wait indefinitely
    wait_time = !Is_rx_buffer_submitted ? 0 : osWaitForever;
  } else if (is_global_frame_pending && !(events_received & SLI_HAL_SI91X_RX_EVENT)) {
    wait_time = osWaitForever;
  } else if ((events_received & SLI_HAL_SI91X_WIFI_TX_EVENT) && (interrupt_status & SLI_WIFI_BUFFER_FULL)) {
    wait_time = SLI_SYSTEM_MS_TO_TICKS(10); // If the wifi TX event is set and the buffer is full, process immediately
  } else if ((events_received & SLI_HAL_SI91X_BLE_TX_EVENT) && (interrupt_status & SLI_BLE_BUFFER_FULL)) {
    wait_time = SLI_SYSTEM_MS_TO_TICKS(10); // if the BLE TX event is set and the buffer is full, process immediately
  }

  return wait_time;
}

static void sli_hal_si91x_handle_ble_tx_event(uint32_t *events_received)
{
  sli_si91x_hal_packet_t *hal_packet = NULL;
  if (SL_STATUS_OK != sli_queue_manager_dequeue(&ble_tx_queue_handle, (void **)&hal_packet)) {
    /* ponytail: same sticky-event hot spin as the wifi TX twin above — BLE TX
     * event stays set until the queue drains; a failing dequeue on a non-empty
     * queue must yield instead of spinning the main loop at wait_time=0. */
    if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&ble_tx_queue_handle)) {
      osDelay(5);
    }
    return;
  }

  sl_status_t status = sli_hal_si91x_send_packet_to_bus((sl_wifi_system_packet_t *)hal_packet->data);

  if (hal_packet->packet_status_handler != NULL) {
    hal_packet->packet_status_handler(hal_packet->packet_type, status, hal_packet->context);
  }

  SL_DEBUG_LOG_V2(DEBUG, "Packet Sent Attempt status: %d", status);
  sli_buffer_manager_free_buffer(hal_packet);

  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&ble_tx_queue_handle)) {
    *events_received &= ~SLI_HAL_SI91X_BLE_TX_EVENT;
  }

  return;
}

static void sli_hal_si91x_handle_wifi_tx_event(uint32_t *events_received)
{
  sli_si91x_hal_packet_t *hal_packet = NULL;
  sl_status_t status                 = sli_queue_manager_dequeue(&wifi_tx_queue_handle, (void **)&hal_packet);

  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(DEBUG, "Failed to dequeue tx packet");

    if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&wifi_tx_queue_handle)) {
      *events_received &= ~SLI_HAL_SI91X_WIFI_TX_EVENT;
    } else {
      /* ponytail: queue non-empty + dequeue failing is the wedged state (queue
       * corruption / injection). The WIFI_TX_EVENT is sticky until the queue
       * DRAINS, and sli_hal_si91x_get_wait_time() returns 0 for "event set but
       * no branch matched" — so without a yield here the main loop becomes a
       * zero-yield hot spin at tx7 with an SPI interrupt-status read per lap
       * (field snapshot: hal_si91x 41.45% READY during the 0x19 storm, the
       * same defect family the engine drain loops had). Rate-limit the retry:
       * 5ms floor per the anti-spin policy. */
      osDelay(5);
    }
    return;
  }

  status = sli_hal_si91x_send_packet_to_bus((sl_wifi_system_packet_t *)hal_packet->data);

  if (hal_packet->packet_status_handler != NULL) {
    hal_packet->packet_status_handler(hal_packet->packet_type, status, hal_packet->context);
  }

  SL_DEBUG_LOG_V2(DEBUG, "Packet Sent Attempt status: %d", status);

  // TX buffer ownership transfers with async HAL TX: release after TX attempt
  // so the command engine TX-ACK path only clears metadata pointers (see FLUSHED handling).
  sli_buffer_manager_free_buffer(hal_packet->data);
  // Free the HAL packet metadata
  sli_buffer_manager_free_buffer(hal_packet);

  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&wifi_tx_queue_handle)) {
    *events_received &= ~SLI_HAL_SI91X_WIFI_TX_EVENT;
  }

  return;
}

static void sli_hal_si91x_handle_rx_event(sl_wifi_buffer_t *rx_buffer,
                                          bool *is_rx_buffer_submitted,
                                          uint32_t *events_received)
{
  const sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)&(rx_buffer->data[0]);
  uint8_t firmware_queue_id             = ((packet->desc[1] & 0xF0) >> 4); // Extract the queue ID
  uint16_t frame_status                 = (uint16_t)(packet->desc[12] + (packet->desc[13] << 8));
  rx_buffer->length                     = (uint16_t)(packet->length & 0x0FFF);

  SL_DEBUG_LOG_V2(DEBUG, "H RX-> Q: %u, C: 0x%X, L: %lu,", firmware_queue_id, packet->command, rx_buffer->length);
  SL_DEBUG_LOG_V2(DEBUG, " S: 0x%x.\r\n", frame_status);

  if (SLI_HAL_SI91X_IS_FLASH_COMMAND(packet->command)) {
    sli_si91x_update_flash_command_status(false);
  }

  // Since we received response for global frame, reset the flag
  // to indicate that we can now send other commands
  if (SLI_HAL_SI91X_IS_GLOBAL_FRAME(packet->command)) {
    is_global_frame_pending = false;
    SL_DEBUG_LOG_V2(DEBUG, "Global lock released: command 0x%x", (uint16_t)packet->command);
  }

  uint16_t packet_type = 0;

  switch (firmware_queue_id) {
    case SLI_BT_Q:
      packet_type = SLI_HAL_SI91X_BLUETOOTH_PACKET;
      break;
    case SLI_WLAN_MGMT_Q:
      packet_type = SLI_HAL_SI91X_PACKET;
      break;
    case SLI_WLAN_DATA_Q:
      packet_type = SLI_HAL_SI91X_DATA_PACKET;
      break;
    case SLI_LOG_Q:
      packet_type = SLI_HAL_SI91X_NWP_LOG_PACKET;
      break;
    default:
      SL_DEBUG_LOG_V2(ERROR, "Invalid firmware queue ID: %u", firmware_queue_id);
      break;
  }

  sli_routing_utility_route_packet(&hal_si91x_routing_table,
                                   packet_type,
                                   (void *)rx_buffer,
                                   sizeof(sl_wifi_buffer_t) + packet->length,
                                   NULL);

  if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&rx_queue_handle)) {
    *events_received &= ~SLI_HAL_SI91X_RX_EVENT;
  }

  *is_rx_buffer_submitted = false;
  return;
}

static void sli_hal_si91x_handle_thread_terminate_event(void)
{
  SL_DEBUG_LOG_V2(INFO, "HAL thread termination event received");
  uint32_t event_result;
  osStatus_t thread_suspend_status;

  event_result = osEventFlagsSet(sli_hal_si91x_events, SLI_HAL_SI91X_THREAD_TERMINATE_ACKNOWLEDGE_EVENT);
  if ((int32_t)event_result < 0) {
    SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(event_result, 0U, "Failed to set thread terminate acknowledge event");
  }

  thread_suspend_status = osThreadSuspend(hal_thread_ID);
  SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(thread_suspend_status, osOK, "Failed to suspend HAL thread");
}

static void sli_hal_si91x_thread(void *args)
{
  UNUSED_PARAMETER(args);
  uint32_t events_received    = 0;
  sl_wifi_buffer_t *rx_buffer = NULL;
  uint16_t interrupt_status   = 0;
  uint32_t wait_time          = osWaitForever;
  bool Is_rx_buffer_submitted = false;
  // ponytail: 5ms floor per the anti-spin policy — this backoff paces the
  // rx-buffer resubmit loop when the RX quota pool is pinned (get_wait_time
  // returns 0 while the buffer is unsubmitted, so this wait is the only throttle).
  uint8_t delay               = 5;

  while (1) {
    wait_time = sli_hal_si91x_get_wait_time(Is_rx_buffer_submitted, events_received, interrupt_status);

    events_received |=
      sli_hal_si91x_wait_for_event((SLI_HAL_SI91X_WIFI_TX_EVENT | SLI_HAL_SI91X_RX_EVENT | SLI_HAL_SI91X_BLE_TX_EVENT
                                    | SLI_HAL_SI91X_THREAD_TERMINATE_EVENT | SLI_HAL_SI91X_BUFFER_AVAILABLE_EVENT),
                                   wait_time);

    sl_status_t status = SL_STATUS_OK;

    // Read the interrupt status
    sli_si91x_bus_read_interrupt_status(&interrupt_status);

    // Can there be a cause where buffer available event is set but interrupt status still indicates buffer full?
    if (events_received & SLI_HAL_SI91X_BUFFER_AVAILABLE_EVENT) {
      events_received &= ~SLI_HAL_SI91X_BUFFER_AVAILABLE_EVENT;
    }

#ifdef SLI_SI91X_MCU_INTERFACE
    if (interrupt_status & SLI_WIFI_BUFFER_FULL) {
      unmask_ta_interrupt(TA_RSI_BUFFER_FULL_CLEAR_EVENT);
    }
#endif

    // If thread terminate event is received, acknowledge and suspend the thread immediately
    if (events_received & SLI_HAL_SI91X_THREAD_TERMINATE_EVENT) {
      sli_hal_si91x_handle_thread_terminate_event();
    }

    if ((events_received & SLI_HAL_SI91X_RX_EVENT)
#ifndef SLI_SI91X_MCU_INTERFACE
        && (interrupt_status & SLI_RX_PKT_PENDING)
#endif
        && (SL_STATUS_OK == (sli_si91x_bus_read_frame(&rx_buffer)))) {
      sli_hal_si91x_handle_rx_event(rx_buffer, &Is_rx_buffer_submitted, &events_received);
    }

    if ((events_received & SLI_HAL_SI91X_WIFI_TX_EVENT) && !(interrupt_status & SLI_WIFI_BUFFER_FULL)
        && !is_global_frame_pending) {
      sli_hal_si91x_handle_wifi_tx_event(&events_received);
    }

    if (events_received & SLI_HAL_SI91X_BLE_TX_EVENT && !(interrupt_status & SLI_BLE_BUFFER_FULL)
        && !is_global_frame_pending) {
      sli_hal_si91x_handle_ble_tx_event(&events_received);
    }

    if (Is_rx_buffer_submitted == false) {
      delay  = (delay < 50) ? delay * 2 : 50; // Exponential backoff for delay with a maximum cap at 50 ms
      status = sli_submit_rx_buffer(delay);

      if (status != SL_STATUS_OK) {
        SL_DEBUG_LOG_V2(ERROR, "Failed to submit RX buffer");
        Is_rx_buffer_submitted = false;
      } else {
        Is_rx_buffer_submitted = true;
        delay                  = 2;
      }
    }
  }
}

static sl_status_t sli_hal_si91x_send_packet_to_bus(sl_wifi_system_packet_t *buffer)
{
  uint8_t queue_id       = 0;
  uint16_t frame_type    = 0;
  uint16_t packet_length = 0;

  sl_status_t status = sli_si91x_req_wakeup();

  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to wake up SI91X device");
    return status; // Skip processing if wakeup failed
  }

  sli_si91x_update_tx_command_status(true);
  queue_id      = ((buffer->desc[1] & 0xF0) >> 4);                      // Extract the queue ID
  frame_type    = (uint16_t)(buffer->desc[2] + (buffer->desc[3] << 8)); // Extract the frame type
  packet_length = (buffer->length & (~0xF000));

  const uint8_t *data = ((buffer->length & 0XFFF) == 0) ? NULL : buffer->data;
  status              = sli_si91x_bus_write_frame(buffer, data, (buffer->length & 0XFFF));
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "\r\n BUS_WRITE_ERROR \r\n");
    // TODO: Use error_queue once the PR is merged.
    // BREAKPOINT();
  }
#ifdef SLI_SI91X_MCU_INTERFACE
  if (frame_type == SLI_COMMON_REQ_SOFT_RESET) {
    sli_si91x_config_m4_dma_desc_on_reset();
  }
#endif
  SL_DEBUG_LOG_V2(DEBUG, "H TX-> Q: %u, C: 0x%X, L: %u.\r\n", queue_id, frame_type, packet_length);

  if ((status == SL_STATUS_OK) && SLI_HAL_SI91X_IS_FLASH_COMMAND(buffer->command)) {
    sli_si91x_update_flash_command_status(true);
  }

  if (status == SL_STATUS_OK && SLI_HAL_SI91X_IS_GLOBAL_FRAME(buffer->command)) {
    is_global_frame_pending = true;
    SL_DEBUG_LOG_V2(DEBUG, "Global lock set: command 0x%x", (uint16_t)buffer->command);
  }

  sl_si91x_host_clear_sleep_indicator();

  sli_si91x_update_tx_command_status(false);
  return status;
}

static void sli_cleanup_flags_and_queues(void)
{
  sl_status_t queue_deinit_status = SL_STATUS_FAIL;

  is_global_frame_pending = false;

  queue_deinit_status = sli_queue_manager_deinit(&wifi_tx_queue_handle, NULL, NULL);
  SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(queue_deinit_status, SL_STATUS_OK, "Wi-Fi TX queue deinit failed with status %d");

  queue_deinit_status = sli_queue_manager_deinit(&ble_tx_queue_handle, NULL, NULL);
  SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(queue_deinit_status, SL_STATUS_OK, "BLE TX queue deinit failed with status %d");

  queue_deinit_status = sli_queue_manager_deinit(&rx_queue_handle, NULL, NULL);
  SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(queue_deinit_status, SL_STATUS_OK, "RX queue deinit failed with status %d");

  if (sli_hal_si91x_events != NULL) {
    osStatus_t freertos_status = osEventFlagsDelete(sli_hal_si91x_events);
    sli_hal_si91x_events       = NULL;

    SLI_HAL_SI91X_LOG_MESSAGE_ON_ERROR(freertos_status, osOK, "Event flags deletion failed with status %d");
  }
}

/******************************************************
 *               Function Definitions
******************************************************/
sl_status_t sli_hal_si91x_init(void)
{
  sl_status_t status = SL_STATUS_OK;

  // Power cycle the SI91x device
  status = sl_si91x_host_power_cycle();
  VERIFY_STATUS_AND_RETURN(status);

  // sl_si91x_bus_init() will be implemented for all available buses
  status = sl_si91x_bus_init();
  VERIFY_STATUS_AND_RETURN(status);

  sli_queue_manager_init(&wifi_tx_queue_handle, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  sli_queue_manager_init(&ble_tx_queue_handle, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  sli_queue_manager_init(&rx_queue_handle, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);

  sli_hal_si91x_events = osEventFlagsNew(NULL);

  if (sli_hal_si91x_events == NULL) {
    sli_cleanup_flags_and_queues();

    SL_DEBUG_LOG_V2(ERROR, "Creation of HAL event flags failed");
    return SL_STATUS_FAIL;
  }

  // Create and start HAL thread
  if (NULL == hal_thread_ID) {
    const osThreadAttr_t attr = {
      .name = SLI_HAL_SI91X_THREAD_NAME,
#ifdef SL_WLAN_HAL_THREAD_PRIORITY
      .priority = SL_WLAN_HAL_THREAD_PRIORITY,
#else
      .priority = osPriorityRealtime1,
#endif
      .stack_mem  = 0,
      .stack_size = SLI_HAL_SI91X_THREAD_STACK,
      .cb_mem     = 0,
      .cb_size    = 0,
      .attr_bits  = 0u,
      .tz_module  = 0u,
    };

    hal_thread_ID = osThreadNew(&sli_hal_si91x_thread, NULL, &attr);
  }

  if (NULL == hal_thread_ID) {
    sli_cleanup_flags_and_queues();

    SL_DEBUG_LOG_V2(ERROR, "Creation of HAL thread failed");
    return SL_STATUS_FAIL;
  }

  return status;
}

sl_status_t sli_hal_si91x_deinit(void)
{
  // Calling this method after without calling init may cause issues.
  uint32_t event_result;
  osStatus_t freertos_status;

  /* ponytail: idempotent teardown. If the flags object (or the thread handle)
   * is already gone, a previous deinit completed the handshake — or init never
   * got that far. Treating that as fatal aborts the caller's teardown at its
   * very first step, which pins device_initialized=true forever while the
   * engines below are already gone (field signature: "Event flags set failed
   * with status 4294967292" once per recovery attempt + every sync command
   * returning NOT_READY + 9ms re-init fast-fails). Force the remainder and
   * report success so the caller reaches its own initialized=false reset. */
  if ((NULL == sli_hal_si91x_events) || (NULL == hal_thread_ID)) {
    if (NULL != hal_thread_ID) {
      osThreadTerminate(hal_thread_ID);
      hal_thread_ID = NULL;
    }
    sli_cleanup_flags_and_queues();
    return SL_STATUS_OK;
  }

  event_result = osEventFlagsSet(sli_hal_si91x_events, SLI_HAL_SI91X_THREAD_TERMINATE_EVENT);
  if ((int32_t)event_result < 0) {
    SL_DEBUG_LOG_V2(ERROR, "Event flags set failed with status %lu", event_result);
    return SL_STATUS_FAIL;
  }

  event_result = osEventFlagsWait(sli_hal_si91x_events,
                                  SLI_HAL_SI91X_THREAD_TERMINATE_ACKNOWLEDGE_EVENT,
                                  osFlagsWaitAny,
                                  osWaitForever);
  if ((int32_t)event_result < 0) {
    SL_DEBUG_LOG_V2(ERROR, "Event flags wait failed with status %lu", event_result);
    return SL_STATUS_FAIL;
  }
  if ((event_result & SLI_HAL_SI91X_THREAD_TERMINATE_ACKNOWLEDGE_EVENT) == 0U) {
    SL_DEBUG_LOG_V2(ERROR, "Event flags wait returned unexpected event flags: 0x%lu", event_result);
    return SL_STATUS_FAIL;
  }

  freertos_status = osThreadTerminate(hal_thread_ID);
  if (freertos_status != osOK) {
    SL_DEBUG_LOG_V2(ERROR, "Thread termination failed with status %d", freertos_status);
    return SL_STATUS_FAIL;
  }
  hal_thread_ID = NULL;
  sli_cleanup_flags_and_queues();

  return SL_STATUS_OK;
}

sl_status_t sli_hal_si91x_command_send_packet(void *packet,
                                              uint32_t packet_size,
                                              sli_routing_utility_packet_status_handler_t packet_status_handler,
                                              void *context)
{
  return sli_hal_si91x_send_packet(packet,
                                   &wifi_tx_queue_handle,
                                   SLI_HAL_SI91X_WIFI_TX_EVENT,
                                   packet_size,
                                   packet_status_handler,
                                   context);
}

sl_status_t sli_hal_si91x_data_send_packet(void *packet,
                                           uint32_t packet_size,
                                           sli_routing_utility_packet_status_handler_t packet_status_handler,
                                           void *context)
{
  return sli_hal_si91x_send_packet(packet,
                                   &wifi_tx_queue_handle,
                                   SLI_HAL_SI91X_WIFI_TX_EVENT,
                                   packet_size,
                                   packet_status_handler,
                                   context);
}

sl_status_t sli_hal_si91x_common_send_packet(void *packet,
                                             uint32_t packet_size,
                                             sli_routing_utility_packet_status_handler_t packet_status_handler,
                                             void *context)
{
  return sli_hal_si91x_send_packet(packet,
                                   &wifi_tx_queue_handle,
                                   SLI_HAL_SI91X_WIFI_TX_EVENT,
                                   packet_size,
                                   packet_status_handler,
                                   context);
}

sl_status_t sli_hal_si91x_ble_send_packet(void *packet,
                                          uint32_t packet_size,
                                          sli_routing_utility_packet_status_handler_t packet_status_handler,
                                          void *context)
{
  return sli_hal_si91x_send_packet(packet,
                                   &ble_tx_queue_handle,
                                   SLI_HAL_SI91X_BLE_TX_EVENT,
                                   packet_size,
                                   packet_status_handler,
                                   context);
}

static uint32_t sli_hal_si91x_wait_for_event(uint32_t event_mask, uint32_t timeout)
{
  uint32_t result = osEventFlagsWait(sli_hal_si91x_events, event_mask, osFlagsWaitAny, timeout);

  // ponytail: catch the 0xFFFFFFFx error family, but keep the legitimate
  // "no event" outcomes (timeout; resource on a non-blocking poll) silent
  // and instant as the original code did — boot-time waits routinely time
  // out. Only the abnormal members (dead/NULL flags object returns
  // osFlagsErrorParameter instantly on EVERY call) get logged once and a
  // 5ms yield, so callers looping on a dead object degrade to a slow
  // poll instead of hot-spinning at this thread's priority.
  if ((result & (uint32_t)osFlagsError) != 0u) {
    if ((result != (uint32_t)osErrorTimeout) && (result != (uint32_t)osErrorResource)) {
      static bool flags_error_logged = false;
      if (!flags_error_logged) {
        flags_error_logged = true;
        SL_DEBUG_LOG_V2(ERROR, "hal: osEventFlagsWait failed 0x%lX\r\n", result);
      }
      osDelay(5);
    }
    return 0;
  }
  return result;
}

sl_status_t sli_hal_si91x_notify_events(uint32_t flags)
{
  // Guard against calling before HAL initialization
  // If HAL event flags are not yet created, silently ignore the event
  // to prevent null-handle faults during system bring-up
  if (sli_hal_si91x_events == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  uint32_t result = osEventFlagsSet(sli_hal_si91x_events, flags);

  // ponytail: a failed set reports the 0xFFFFFFFx error family — treat any
  // of them as "nothing was signalled" instead of returning the raw error
  // as if it were a flags value.
  if ((result & (uint32_t)osFlagsError) != 0u) {
    return 0;
  }

  return result;
}

osPriority_t sli_hal_si91x_get_thread_priority(void)
{
  if (NULL == hal_thread_ID) {
    return osPriorityNone;
  }
  return osThreadGetPriority(hal_thread_ID);
}