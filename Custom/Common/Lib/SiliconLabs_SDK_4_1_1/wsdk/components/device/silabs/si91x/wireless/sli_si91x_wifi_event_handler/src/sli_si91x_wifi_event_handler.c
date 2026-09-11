/***************************************************************************/ /**
 * @file sli_si91x_wifi_event_handler.c
 * @brief
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
#include "sli_si91x_wifi_event_handler.h"
#include "sli_event_engine.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_types.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_driver.h"
#include "sl_wifi_constants.h"
#include "sl_wifi_types.h"
#include "sl_rsi_utility.h"
#include "cmsis_os2.h"
#include "cmsis_compiler.h"
#include "sl_si91x_core_utilities.h"
#include <string.h>
#include "sli_wifi_constants.h"
#include "sli_wifi_power_profile.h"
#include "sl_additional_status.h"

#ifdef SL_NET_COMPONENT_INCLUDED
#include "sl_net_types.h"
#include "sl_net_constants.h"
#include "sl_net_wifi_types.h"
#include "sli_net_types.h"
#include "sl_net.h"
#endif

#include "sli_wifi_utility.h"
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_si91x_socket_types.h"
#include "sl_si91x_socket_utility.h"
#include "sl_net_si91x_integration_handler.h"
#include "sl_si91x_socket_callback_framework.h"
#include "sl_ip_types.h"
#include "sli_net_utility.h"
#if !defined(__ZEPHYR__) && !defined(SLI_SI91X_LWIP_HOSTED_NETWORK_STACK) \
  && !defined(                                                            \
    SLI_SI91X_NETWORK_DUAL_STACK) // These headers are included only when neither LWIP nor dual stack is used.
#include "netinet_in.h"
#include "netinet6_in6.h"
#endif
#else
// This macro defines a handler for dispatching network events.
// It is used to handle events related to the SI91x module
#ifndef SL_NET_EVENT_DISPATCH_HANDLER
#define SL_NET_EVENT_DISPATCH_HANDLER(metadata) \
  {                                             \
    UNUSED_PARAMETER(metadata);                 \
  }
#endif
#endif

#ifdef SLI_SI91X_ENABLE_BLE
#include "rsi_bt_common.h"
#endif

/******************************************************
 *               External Variable Definitions
 ******************************************************/
extern osMessageQueueId_t network_manager_queue;
extern sl_wifi_event_handler_t si91x_event_handler;

// Declaration of a global flag to indicate if background mode is enabled
extern bool bg_enabled;
/******************************************************
 *               Local Variable Definitions
 ******************************************************/

/******************************************************
 *               Global Variable Definitions
 ******************************************************/
osEventFlagsId_t sli_wifi_event_engine_event_id                  = NULL;
sli_queue_t event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_MAX_EVENTS] = { 0 };

/******************************************************
  *               Local Function Definitions
 ******************************************************/
static void sli_si91x_wifi_event_engine_common_event_handler(uint32_t event, void *data)
{
  UNUSED_PARAMETER(event);
  if (data == NULL) {
    return;
  }
  sl_status_t status                      = SL_STATUS_OK;
  sli_command_engine_response_t *response = (sli_command_engine_response_t *)data;
  sl_wifi_buffer_t *buffer                = sli_wifi_get_response_buffer(response);
  sli_command_engine_metadata_t *metadata = sli_wifi_get_response_metadata(response);
  const sl_wifi_system_packet_t *packet =
    (buffer != NULL) ? (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL) : NULL;

  if (packet != NULL && SLI_WIFI_RSP_CARDREADY == packet->command) {
    sli_command_engine_packet_type_configuration_t packet_type_info = { 0 };
    // Get packet type configuration for the given command type
    status = sli_command_engine_get_rx_queue_info_from_packet_type(&sli_wifi_command_engine,
                                                                   (uint16_t)(SLI_WLAN_COMMON_CMD),
                                                                   &packet_type_info);
    if (SL_STATUS_OK != status) {
      if (buffer != NULL) {
        sli_buffer_manager_free_buffer(buffer);
      }
      if (metadata != NULL) {
        sli_buffer_manager_free_buffer(metadata);
      }
      sli_buffer_manager_free_buffer(response);
      return;
    }
    // set the event to the application layer
    uint32_t event_result =
      osEventFlagsSet(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
    if ((event_result & osFlagsError) != 0) {
      SL_DEBUG_LOG_V2(WARN, "Warning: Failed to set event flags for CARDREADY response\r\n");
    }
  }
  if (buffer != NULL) {
    sli_buffer_manager_free_buffer(buffer);
  }
  if (metadata != NULL) {
    sli_buffer_manager_free_buffer(metadata);
  }
  sli_buffer_manager_free_buffer(response);

  return;
}

static void sli_si91x_wifi_event_engine_wifi_event_handler(uint32_t event, void *data)
{
  UNUSED_PARAMETER(event);
  if (data == NULL) {
    return;
  }
  sli_command_engine_response_t *engine_response = (sli_command_engine_response_t *)data;
  sli_command_engine_metadata_t *metadata        = sli_wifi_get_response_metadata(engine_response);
  sl_wifi_buffer_t *buffer                       = sli_wifi_get_response_buffer(engine_response);
  if (buffer == NULL) {
    if (metadata != NULL) {
      sli_buffer_manager_free_buffer(metadata);
    }
    sli_buffer_manager_free_buffer(engine_response);
    return;
  }

  sl_wifi_event_t wifi_event = 0;
  uint16_t frame_status      = 0;

  sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  if (packet == NULL) {
    sli_buffer_manager_free_buffer(buffer);
    if (metadata != NULL) {
      sli_buffer_manager_free_buffer(metadata);
    }
    sli_buffer_manager_free_buffer(engine_response);
    return;
  }
  frame_status = sli_wifi_get_wifi_frame_status(packet);
  SL_DEBUG_LOG_V2(DEBUG, "WE-> C: 0x%X, S: 0x%X.\r\n", packet->command, frame_status);

  wifi_event = sli_wifi_convert_event_to_sl_wifi_event(packet->command, frame_status);

  if (SLI_WIFI_RSP_SCAN_RESULTS == packet->command) {
    sli_handle_wifi_beacon(packet);
  }

  if (wifi_event != SL_WIFI_INVALID_EVENT && si91x_event_handler != NULL) {
    si91x_event_handler(wifi_event, buffer);
  }

  sli_buffer_manager_free_buffer(buffer);
  if (metadata != NULL) {
    sli_buffer_manager_free_buffer(metadata);
  }
  sli_buffer_manager_free_buffer(engine_response);
  return;
}

static void sli_si91x_wifi_event_engine_bt_event_handler(uint32_t event, void *data)
{
  UNUSED_VARIABLE(event);
  if (data == NULL) {
    return;
  }
#ifdef SLI_SI91X_ENABLE_BLE
  sl_wifi_buffer_t *rx_buffer = (sl_wifi_buffer_t *)data;
  // Handle Bluetooth response
  rsi_driver_process_bt_resp_handler((sl_wifi_system_packet_t *)rx_buffer->data);
#endif
  sli_buffer_manager_free_buffer(data);
  return;
}

static void sli_si91x_wifi_event_engine_network_event_handler(uint32_t event, void *data)
{
  UNUSED_VARIABLE(event);
  if (data == NULL) {
    return;
  }
  sli_command_engine_response_t *response = (sli_command_engine_response_t *)data;
  sli_command_engine_metadata_t *metadata = sli_wifi_get_response_metadata(response);
  sl_wifi_buffer_t *buffer                = sli_wifi_get_response_buffer(response);

  SL_NET_EVENT_DISPATCH_HANDLER(response);

  if (buffer != NULL) {
    sli_buffer_manager_free_buffer(buffer);
  }
  sli_buffer_manager_free_buffer(response);
  if (metadata != NULL) {
    sli_buffer_manager_free_buffer(metadata);
  }
  return;
}

static void sli_si91x_wifi_event_engine_socket_cmd_event_handler(uint32_t event, void *data)
{
  UNUSED_VARIABLE(event);
  if (data == NULL) {
    return;
  }
  sli_command_engine_response_t *response = (sli_command_engine_response_t *)data;
  sli_command_engine_metadata_t *metadata = sli_wifi_get_response_metadata(response);
  sl_wifi_buffer_t *buffer                = sli_wifi_get_response_buffer(response);

  SL_NET_EVENT_DISPATCH_HANDLER(response);

  if (buffer != NULL) {
    sli_buffer_manager_free_buffer(buffer);
  }
  sli_buffer_manager_free_buffer(response);
  if (metadata != NULL) {
    sli_buffer_manager_free_buffer(metadata);
  }
  return;
}

static void sli_si91x_wifi_event_engine_socket_data_event_handler(uint32_t event, void *data)
{
  UNUSED_VARIABLE(event);
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
  if (data == NULL) {
    return;
  }
  // Handle the socket data event
  sli_si91x_socket_data_event_handler((sl_wifi_buffer_t *)data);
#else
  UNUSED_PARAMETER(data);
#endif
  return;
}

static sl_status_t sli_si91x_convert_command_engine_error_status_to_sl_status(
  const sl_command_engine_error_status_t *error_status)
{
  if (error_status == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  switch (*error_status) {
    case SLI_COMMAND_ENGINE_STATUS_FATAL_ERROR:
      return SL_STATUS_FAIL;
    case SLI_COMMAND_ENGINE_STATUS_INTERFACE_ERROR:
    case SLI_COMMAND_ENGINE_STATUS_COMMAND_TX_FAILED:
      return SL_STATUS_BUS_ERROR;
    case SLI_COMMAND_ENGINE_STATUS_MEMORY_ERROR:
      return SL_STATUS_ALLOCATION_FAILED;
    case SLI_COMMAND_ENGINE_STATUS_COMMAND_TX_TIMEOUT:
    case SLI_COMMAND_ENGINE_STATUS_COMMAND_PROCESSING_TIMEOUT:
      return SL_STATUS_TIMEOUT;
    default:
      return SL_STATUS_FAIL;
  }
}

static void sli_si91x_wifi_event_engine_error_event_handler(uint32_t event, void *data)
{
  UNUSED_VARIABLE(event);
  if (data == NULL) {
    return;
  }

  sl_status_t status =
    sli_si91x_convert_command_engine_error_status_to_sl_status((const sl_command_engine_error_status_t *)data);
  if (si91x_event_handler != NULL) {
    si91x_event_handler(SL_WIFI_COMMAND_ENGINE_STATUS_EVENT, (sl_wifi_buffer_t *)&status);
  }
  sli_buffer_manager_free_buffer(data);
  return;
}

static void sli_si91x_wifi_event_engine_nwp_log_event_handler(uint32_t event, void *data)
{
  UNUSED_VARIABLE(event);
  if (data == NULL) {
    return;
  }
  sl_wifi_buffer_t *buffer              = (sl_wifi_buffer_t *)data;
  const sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)buffer->data;

  if (packet == NULL) {
    SL_DEBUG_LOG_V2(INFO, "NWP log event: null packet");
    sli_buffer_manager_free_buffer(buffer);
    return;
  }

  // Special handling for the log event
  if (SLI_COMMON_RSP_NWP_LOGGING == packet->command) {
    // Extract packet length from descriptor
    uint16_t pkt_length = (uint16_t)(packet->desc[0] + ((packet->desc[1] & 0x0F) << 8));

    // Call the NWP log handler with the log data
    sli_handle_nwp_log_packet((const uint8_t *)packet->data, pkt_length);
  }

  // Free the buffer
  sli_buffer_manager_free_buffer(buffer);
  return;
}

// Weak implementation of the function to process data frames received from the SI91x module
__WEAK sl_status_t sl_si91x_host_process_data_frame(sl_wifi_interface_t interface, sl_wifi_buffer_t *buffer)
{
  UNUSED_PARAMETER(interface);
  UNUSED_PARAMETER(buffer);
  return SL_STATUS_OK;
}

/******************************************************
  *               Global Function Definitions
 ******************************************************/
void sli_wifi_event_engine_signal_async(void)
{
  if (sli_wifi_event_engine_event_id != NULL) {
    osEventFlagsSet(sli_wifi_event_engine_event_id, SLI_EVENT_ENGINE_ASYNC_EVENT);
  }
}

sl_status_t sli_si91x_wifi_data_packet_handler(void *rx_buffer,
                                               uint32_t packet_size,
                                               sli_routing_utility_packet_status_handler_t packet_status_handler,
                                               void *context)
{
  UNUSED_PARAMETER(packet_status_handler);
  UNUSED_PARAMETER(context);
  UNUSED_PARAMETER(packet_size);

  sl_wifi_system_packet_t *rx_packet = (sl_wifi_system_packet_t *)((sl_wifi_buffer_t *)rx_buffer)->data;

  // Clear the queue ID bits in desc[1] to avoid misinterpretation
  rx_packet->desc[1] = (rx_packet->desc[1] & 0x0F);

  if (rx_packet->command == SLI_RECEIVE_RAW_DATA) {

#if defined(SLI_SI91X_OFFLOAD_NETWORK_STACK) && !defined(SLI_SI91X_NETWORK_DUAL_STACK)

    // Offload only mode is enabled
    // Passes the asynchronous socket packet to the event engine for further processing.
    sli_queue_manager_enqueue(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_DATA_EVENT], rx_buffer);

    sli_wifi_event_engine_signal_async();

#elif defined(SLI_SI91X_NETWORK_DUAL_STACK)

    extern bool bypass_mode_enabled;

    if (!bypass_mode_enabled) {

      // Passes the asynchronous socket packet to the event engine for further processing.
      sli_queue_manager_enqueue(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_DATA_EVENT], rx_buffer);

      sli_wifi_event_engine_signal_async();

    } else {
      // If SLI_SI91X_OFFLOAD_NETWORK_STACK is defined and dual stack mode is enabled, process the raw data frame.
      sl_si91x_host_process_data_frame(SL_WIFI_CLIENT_INTERFACE, rx_buffer);
      sli_buffer_manager_free_buffer(rx_buffer);
    }
#else
    // In bypass mode, process the data frame and free the buffer.
    sl_si91x_host_process_data_frame(SL_WIFI_CLIENT_INTERFACE, rx_buffer);
    sli_buffer_manager_free_buffer(rx_buffer);
#endif
  } else if (rx_packet->command == SLI_NET_DUAL_STACK_RX_RAW_DATA_FRAME) {
    // If network dual stack mode is enabled, process the received data frame of type 0x1 and free the buffer.
    sl_si91x_host_process_data_frame(SL_WIFI_CLIENT_INTERFACE, rx_buffer);
    sli_buffer_manager_free_buffer(rx_buffer);
  } else if (rx_packet->command == SLI_WIFI_RX_DOT11_DATA) {
    // Passes the asynchronous Wi-Fi packet to the event engine for further processing.
    sli_queue_manager_enqueue(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_WIFI_EVENT], rx_buffer);
    sli_wifi_event_engine_signal_async();
  }

  return SL_STATUS_OK;
}

sl_status_t sli_si91x_wifi_ble_packet_handler(void *rx_buffer,
                                              uint32_t packet_size,
                                              sli_routing_utility_packet_status_handler_t packet_status_handler,
                                              void *context)
{
  UNUSED_PARAMETER(packet_status_handler);
  UNUSED_PARAMETER(context);
  UNUSED_PARAMETER(packet_size);

  sl_wifi_system_packet_t *rx_packet = (sl_wifi_system_packet_t *)((sl_wifi_buffer_t *)rx_buffer)->data;

  // Clear the queue ID bits in desc[1] to avoid misinterpretation
  rx_packet->desc[1] = (rx_packet->desc[1] & 0x0F);

  // Pass the BLE packet to the event engine for further processing.
  sli_queue_manager_enqueue(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_BLE_EVENT], rx_buffer);
  sli_wifi_event_engine_signal_async();

  return SL_STATUS_OK;
}

sl_status_t sli_si91x_wifi_nwp_log_packet_handler(void *rx_buffer,
                                                  uint32_t packet_size,
                                                  sli_routing_utility_packet_status_handler_t packet_status_handler,
                                                  void *context)
{
  UNUSED_PARAMETER(packet_status_handler);
  UNUSED_PARAMETER(context);
  UNUSED_PARAMETER(packet_size);

  sl_wifi_system_packet_t *rx_packet = (sl_wifi_system_packet_t *)((sl_wifi_buffer_t *)rx_buffer)->data;

  // Clear the queue ID bits in desc[1] to avoid misinterpretation
  rx_packet->desc[1] = (rx_packet->desc[1] & 0x0F);

  // Pass the NWP log packet to the dedicated NWP log event handler
  sli_queue_manager_enqueue(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_NWP_LOG_EVENT], rx_buffer);
  sli_wifi_event_engine_signal_async();

  return SL_STATUS_OK;
}

sl_status_t sli_si91x_wifi_event_engine_init(void)
{
  sl_status_t status = SL_STATUS_OK;

  for (uint16_t i = 0; i < SLI_WIFI_ASYNC_EVENT_HANDLER_MAX_EVENTS; i++) {
    status = sli_queue_manager_init(&event_queue[i], SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
    VERIFY_STATUS_AND_RETURN(status);
  }

  // Initialize the event engine
  status = sli_event_engine_init(&sli_wifi_event_engine_event_id);
  VERIFY_STATUS_AND_RETURN(status);

  // Register the Wi-Fi event handler for asynchronous Wi-Fi events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_COMMON_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_COMMON_EVENT,
                                           sli_si91x_wifi_event_engine_common_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine and return the error status
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the error event handler for asynchronous error events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_WIFI_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_WIFI_EVENT,
                                           sli_si91x_wifi_event_engine_wifi_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine and return the error status
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the Wi-Fi event handler for asynchronous Wi-Fi events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_BLE_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_BLE_EVENT,
                                           sli_si91x_wifi_event_engine_bt_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine and return the error status
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the error event handler for asynchronous error events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_NETWORK_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_NETWORK_EVENT,
                                           sli_si91x_wifi_event_engine_network_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine and return the error status
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the Wi-Fi event handler for asynchronous Wi-Fi events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_CMD_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_CMD_EVENT,
                                           sli_si91x_wifi_event_engine_socket_cmd_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine and return the error status
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the Wi-Fi event handler for asynchronous Wi-Fi events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_DATA_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_DATA_EVENT,
                                           sli_si91x_wifi_event_engine_socket_data_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine and return the error status
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the error event handler for asynchronous error events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_ERROR_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_ERROR_EVENT,
                                           sli_si91x_wifi_event_engine_error_event_handler);
  if (SL_STATUS_OK != status) {
    // If registration fails, deinitialize the event engine
    sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  // Register the NWP log event handler for asynchronous NWP logging events
  status = sli_event_engine_register_event(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_NWP_LOG_EVENT],
                                           SLI_WIFI_ASYNC_EVENT_HANDLER_NWP_LOG_EVENT,
                                           sli_si91x_wifi_event_engine_nwp_log_event_handler);
  if (SL_STATUS_OK != status) {
    SL_DEBUG_LOG_V2(ERROR, "Register NWP_LOG failed 0x%lX", status);
    // If registration fails, deinitialize the event engine
    sli_si91x_wifi_event_engine_deinit();
  }

  return status;
}

sl_status_t sli_si91x_wifi_event_engine_deinit(void)
{
  sl_status_t status = sli_event_engine_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  sli_wifi_event_engine_event_id = NULL;

  return SL_STATUS_OK;
}

#ifdef SLI_SI91X_ENABLE_BLE
void sli_si91x_ble_send_packet_tx_status(uint16_t packet_type, sl_status_t status, void *context)
{
  UNUSED_PARAMETER(packet_type); // Packet type not needed in this callback
  const sl_wifi_system_packet_t *packet = (const sl_wifi_system_packet_t *)context;
  // Notify BLE stack that transmission is done
  rsi_bt_common_tx_done(packet, status);
  sli_buffer_manager_free_buffer(context);
  return;
}
#endif
