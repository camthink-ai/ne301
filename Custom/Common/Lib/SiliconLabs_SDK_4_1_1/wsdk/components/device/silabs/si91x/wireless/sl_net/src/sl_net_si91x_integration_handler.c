/***************************************************************************/ /**
 * @file
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2019 Silicon Laboratories Inc. www.silabs.com</b>
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
#include "sl_constants.h"
#include "sl_si91x_types.h"
#include "sl_si91x_driver.h"
#include "sl_net_si91x.h"
#include "sl_net_constants.h"
#include "sli_net_utility.h"
#include "sli_command_engine.h"
#include "sl_net_si91x_integration_handler.h"
#include "sli_wifi_constants.h"
#if defined(SL_WIFI_COMPONENT_INCLUDED)
#include "sl_wifi.h"
#endif
#ifndef __ZEPHYR__
#ifndef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
#ifndef SLI_SI91X_NETWORK_DUAL_STACK // These headers are included only when neither LWIP nor dual stack is used.
#include "netinet_in.h"
#include "netinet6_in6.h"
#endif
#endif
#endif
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_si91x_socket_constants.h"
#include "sl_si91x_socket_utility.h"
#include "sl_ip_types.h"
#endif

#ifdef SLI_SI91X_SOCKETS
#include "sl_si91x_socket_callback_framework.h"
#endif
#include "sl_si91x_core_utilities.h"

#ifdef SLI_SI91X_EMBEDDED_MQTT_CLIENT
#include "si91x_mqtt_client_callback_framework.h"
#include "si91x_mqtt_client_utility.h"
#endif

#ifdef SLI_SI91X_INTERNAL_SNTP_CLIENT
#include "si91x_sntp_client_callback_framework.h"
#endif

#ifdef SLI_SI91X_INTERNAL_HTTP_CLIENT
#include "sl_si91x_http_client_callback_framework.h"
#endif
#include "sli_wifi_utility.h"

#ifdef SLI_SI91X_EMBEDDED_MQTT_CLIENT
/**
 * A internal function to handle to asynchronous mqtt client events.
 */
static void sli_handle_mqtt_client_asynch_events(sli_command_engine_response_t *response)
{
  sl_wifi_buffer_t *buffer = sli_wifi_get_response_buffer(response);

  sl_wifi_system_packet_t *raw_rx_packet = (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  sl_mqtt_client_t *mqtt_client               = NULL;
  sl_si91x_mqtt_client_context_t *sdk_context = NULL;

  if (response->type == SLI_COMMAND_ENGINE_METADATA_RESPONSE) {
    sli_command_engine_metadata_t *metadata = sli_wifi_get_response_metadata(response);

    sdk_context = (sl_si91x_mqtt_client_context_t *)metadata->tx_info.context;
  }

  uint16_t frame_status = sli_wifi_get_wifi_frame_status(raw_rx_packet);

  //Variable to indicate whether a disconnect event is related to a keep-alive terminate error.
  bool is_keep_alive_response_related_disconnect =
    (raw_rx_packet->command == SLI_WIFI_RSP_EMB_MQTT_CLIENT
     && frame_status == (SL_STATUS_SI91X_MQTT_KEEP_ALIVE_TERMINATE_ERROR & ~BIT(16)));

  // Since these responses are unsolicited, We need to create a context for them.
  if (raw_rx_packet->command == SLI_WIFI_RSP_MQTT_REMOTE_TERMINATE
      || raw_rx_packet->command == SLI_WIFI_RSP_EMB_MQTT_PUBLISH_PKT || raw_rx_packet->command == SLI_WIFI_RSP_JOIN
      || is_keep_alive_response_related_disconnect) {

    sli_si91x_get_mqtt_client(&mqtt_client);

    if (mqtt_client == NULL) {
      // Drop MQTT client event, if the client is either connecting or disconnected
      SL_DEBUG_LOG_V2(DEBUG, "Dropping mqtt client event, Si91x Event: %hu", raw_rx_packet->command);
      return;
    }

    if (mqtt_client->state == SL_MQTT_CLIENT_DISCONNECTED) {
      // Drop MQTT client event disconnect, if the client is already in disconnected
      // This can happen if MQTT client is already disconnected state and NWP sends a rejoin failure event.
      SL_DEBUG_LOG_V2(DEBUG, "Dropping mqtt disconnect event: %hu", raw_rx_packet->command);
      return;
    }

    // Send CONNECT_FAILED_EVENT if JOIN is received during TA_INIT state
    if ((raw_rx_packet->command == SLI_WIFI_RSP_JOIN || raw_rx_packet->command == SLI_WIFI_RSP_DISCONNECT)
        && mqtt_client->state == SL_MQTT_CLIENT_TA_INIT) {
      // Build MQTT SDK context for asynchronous MQTT events
      sli_si91x_build_mqtt_sdk_context_if_async(SL_MQTT_CLIENT_CONNECTED_EVENT,
                                                mqtt_client,
                                                NULL,
                                                NULL,
                                                0,
                                                &sdk_context);
    } else {
      // Build MQTT SDK context for asynchronous MQTT events
      sli_si91x_build_mqtt_sdk_context_if_async(
        (raw_rx_packet->command == SLI_WIFI_RSP_MQTT_REMOTE_TERMINATE || raw_rx_packet->command == SLI_WIFI_RSP_JOIN
         || is_keep_alive_response_related_disconnect)
          ? SL_MQTT_CLIENT_DISCONNECTED_EVENT
          : SL_MQTT_CLIENT_MESSAGED_RECEIVED_EVENT,
        mqtt_client,
        NULL,
        NULL,
        0,
        &sdk_context);
    }
  }

  if (sdk_context == NULL) {
    return;
  }

  SL_DEBUG_LOG_V2(DEBUG, "sli_handle_mqtt_client_asynch_events: event %x", sdk_context->event);

  uint16_t si91x_event_status = sli_wifi_get_wifi_frame_status(raw_rx_packet);
  sl_status_t event_status    = sli_wifi_convert_and_save_firmware_status(si91x_event_status);

  // Handle MQTT events
  sli_si91x_mqtt_event_handler(event_status, sdk_context, raw_rx_packet);
}
#endif

#ifdef SLI_SI91X_EMBEDDED_MQTT_CLIENT
static bool sli_handle_mqtt_client_events(sli_command_engine_response_t *response)
{
  // Handle MQTT client-specific events
  sl_wifi_buffer_t *buffer              = sli_wifi_get_response_buffer(response);
  const sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  if (packet->command == SLI_WIFI_REQ_EMB_MQTT_CLIENT || packet->command == SLI_WIFI_RSP_EMB_MQTT_PUBLISH_PKT
      || packet->command == SLI_WIFI_RSP_MQTT_REMOTE_TERMINATE || packet->command == SLI_WIFI_RSP_JOIN) {
    sli_handle_mqtt_client_asynch_events(response);
    return true;
  }
  return false;
}
#endif

#ifdef SLI_SI91X_INTERNAL_SNTP_CLIENT
static bool sli_handle_sntp_client_events(sli_command_engine_response_t *response)
{
  const sl_wifi_buffer_t *buffer        = sli_wifi_get_response_buffer(response);
  const sl_wifi_system_packet_t *packet = (const sl_wifi_system_packet_t *)buffer->data;

  // Handle SNTP client events
  if (packet->command == SLI_WIFI_RSP_SNTP_CLIENT) {
    sli_si91x_sntp_event_handler(response);
    return true;
  }
  return false;
}
#endif

#ifdef SLI_SI91X_SOCKETS

static void sli_handle_socket_events(const sli_command_engine_metadata_t *data, sl_wifi_system_packet_t *packet)
{
  UNUSED_PARAMETER(data);
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
  sli_si91x_sync_bsd_socket_states_for_flush_scenarios(packet);
#endif
  // Handle SI91X socket-related events
  bool is_socket_command =
    (packet->command == SLI_WIFI_REQ_SOCKET_ACCEPT || packet->command == SLI_WIFI_RSP_REMOTE_TERMINATE
     || packet->command == SLI_RECEIVE_RAW_DATA || packet->command == SLI_WIFI_RSP_TCP_ACK_INDICATION
     || packet->command == SLI_WIFI_RSP_SELECT_REQUEST || packet->command == SLI_WIFI_RSP_SOCKET_READ_DATA);
  if (is_socket_command) {
    sl_wifi_system_packet_t *raw_rx_packet = packet;
    uint16_t si91x_event_status            = sli_wifi_get_wifi_frame_status(raw_rx_packet);

    sl_status_t event_status = sli_wifi_convert_and_save_firmware_status(si91x_event_status);
    sli_si91x_socket_event_handler(event_status, NULL, raw_rx_packet);
  }
}
#endif

void sl_net_si91x_event_dispatch_handler(sli_command_engine_response_t *response)
{
  sl_status_t status;
  sl_net_event_t service_event;

  sl_wifi_buffer_t *buffer        = sli_wifi_get_response_buffer(response);
  sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);

#ifdef SLI_SI91X_INTERNAL_HTTP_CLIENT
  sl_http_client_event_t http_event;
#endif

#ifdef SLI_SI91X_EMBEDDED_MQTT_CLIENT
  if (sli_handle_mqtt_client_events(response)) {
    return;
  }
#endif

#ifdef SLI_SI91X_INTERNAL_SNTP_CLIENT
  if (sli_handle_sntp_client_events(response)) {
    return;
  }
#endif

#ifdef SLI_SI91X_SOCKETS
  const sli_command_engine_metadata_t *metadata = sli_wifi_get_response_metadata(response);
  sli_handle_socket_events(metadata, packet);
#endif

  status = sli_convert_si91x_event_to_sl_net_event(&packet->command, &service_event, packet);
  if (status == SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(DEBUG, "><<<< Got net event : %u\r\n", service_event);
    sl_si91x_default_handler(service_event, buffer);
  }
#ifdef SLI_SI91X_INTERNAL_HTTP_CLIENT
  // Check for sl_http_client_event_t
  else if (sli_convert_si91x_event_to_sl_http_client_event(&packet->command, &http_event) == SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(DEBUG, "\r\n>>> HTTP Event received: %u <<<\r\n", http_event);
    void *http_sdk_context                       = NULL;
    sli_command_engine_metadata_t *http_metadata = sli_wifi_get_response_metadata(response);

    if (http_metadata != NULL) {
      http_sdk_context = http_metadata->tx_info.context;
    }

    sli_http_client_default_event_handler(http_event, buffer, http_sdk_context);
  }
#endif
}

void sli_si91x_network_cleanup_handler()
{
#ifdef SLI_SI91X_EMBEDDED_MQTT_CLIENT
  sli_mqtt_client_cleanup();
#endif
}
