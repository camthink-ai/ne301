/***************************************************************************/ /**
 * @file sli_si91x_wifi_command_engine_config.c
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
#include <stdint.h>
#include <string.h>
#include "sl_constants.h"
#include "sl_status.h"
#include "sli_wifi_command_engine_config.h"
#include "sli_si91x_wifi_event_handler.h"
#include "sli_si91x_wifi_command_engine_packet.h"
#include "sli_wifi_types.h"
#include "sli_hal_si91x.h"
#include "sli_si91x_driver.h"
#include "cmsis_os2.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_types.h"
#include "sl_si91x_protocol_types.h"
#include "sl_wifi_constants.h"
#include "sl_rsi_utility.h"
#include "sl_si91x_core_utilities.h"
#include "sli_wifi_power_profile.h"
#include "sl_additional_status.h"
#include "sli_wifi_utility.h"
#include "sli_buffer_manager.h"
#include "sli_queue_manager.h"

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_si91x_socket_utility.h"
#include "sl_ip_types.h"
#include "sli_net_utility.h"
#endif

#ifdef SLI_SI91X_SOCKETS
#include "sl_si91x_socket_utility.h"
#endif

#ifdef SL_NET_COMPONENT_INCLUDED
#include "sli_net_common_utility.h"
#endif

#include "sli_code_classification.h"

/******************************************************
 *               Macro Definitions
 ******************************************************/

/******************************************************
 *               Extern Declarations
 ******************************************************/
extern bool bg_enabled;

/* ponytail: app-side wedge detector hook (Custom/Hal/Network/netif_manager).
 * Receives the real bus-write result of every DATA packet; a density of
 * failures there triggers the firmware-error recovery. */
extern void sl_net_notify_bus_tx_failure(sl_status_t status);

#ifdef SL_NET_COMPONENT_INCLUDED
extern osMessageQueueId_t sli_network_manager_request_queue;
#endif
/******************************************************
 *               Local Type Definitions
 ******************************************************/
typedef bool (*sli_flush_packet_compare_function_t)(const sli_queue_t *handle,
                                                    const void *data,
                                                    const void *node_match_data);

/******************************************************
 *               Local Function Declarations
 ******************************************************/
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static bool sli_si91x_wifi_command_engine_flush_metadata(const sli_command_engine_t *instance,
                                                         uint16_t packet_type,
                                                         sli_command_engine_packet_type_configuration_t *packet_config,
                                                         sli_command_engine_metadata_t *metadata);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_si91x_process_flush_one_metadata(sli_command_engine_metadata_t *tx_metadata,
                                                 sli_command_engine_packet_type_configuration_t *packet_config,
                                                 uint16_t frame_status);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_si91x_try_rearm_command_engine_dynamic_tx(
  sli_command_engine_t *instance,
  const sli_command_engine_queue_info_t *queue_info,
  const sli_command_engine_packet_type_configuration_t *packet_config);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static bool sli_inflight_queue_has_matching_frame_id(const sli_command_engine_queue_info_t *queue_info,
                                                     uint16_t frame_id);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static bool sli_is_command_in_flight_queue(sli_command_engine_t *instance, uint16_t packet_type, uint16_t frame_id);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static bool sli_flush_packet_match_all(const sli_queue_t *handle, const void *data, const void *node_match_data);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static bool sli_flush_packet_mqtt_compare_function(const sli_queue_t *handle, const void *data, const void *context);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_flush_queue_for_packet_type(sli_command_engine_t *instance,
                                                   sli_command_engine_queue_info_t *queue_info,
                                                   sli_command_engine_packet_type_configuration_t *packet_config,
                                                   sli_flush_packet_compare_function_t compare_function,
                                                   const void *user_data,
                                                   uint16_t frame_status);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_post_packet_to_event_engine(sl_wifi_buffer_t *rx_buffer);
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_flush_socket_queues(sli_command_engine_t *instance, uint16_t packet_type, uint16_t error_status);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_flush_all_socket_queues(sli_command_engine_t *instance,
                                               uint16_t error_status,
                                               const uint8_t *vap_id,
                                               const sl_ip_address_t *dest_ip_address);
#endif
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_flush_all_command_engine_static_queues(sli_command_engine_t *instance, uint16_t error_status);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static sl_status_t sli_si91x_forward_wifi_events_for_network_operations(sli_command_engine_t *instance,
                                                                        uint16_t packet_type,
                                                                        sl_wifi_buffer_t *rx_buffer);
#ifdef SL_NET_COMPONENT_INCLUDED
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static void sli_post_disconnect_event_to_network_manager(sl_net_interface_t interface);
SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
static bool sli_si91x_client_vap_id_to_net_interface(uint8_t vap_id, sl_net_interface_t *out_interface);
#endif

/******************************************************
 *             Extern Variable Declarations
 ******************************************************/

/* ponytail: LWIP/transceiver RAW data packets used to ship a NULL
 * packet_status_handler, so the app only ever saw the pool alloc result (0x19
 * once CE_DATA_POOL backed up) — and since the HAL thread frees each TX block
 * right after its bus-write attempt (even on failure), a half-wedged pool keeps
 * leaking blocks back and interleaving alloc-OKs. Forward the real bus-write
 * status (wakeup failure / BUS_WRITE_ERROR) to the app's density detector: a
 * failure here means the chip never received the frame at all, the earliest
 * hard-death signal available. Note: hal_packet->packet_type is hardcoded to 0
 * on enqueue, so the packet_type argument carries no information — entry-level
 * routing is what scopes this handler to DATA packets only. */
static void sli_wifi_data_tx_status_handler(uint16_t packet_type, sl_status_t status, void *context)
{
  UNUSED_PARAMETER(packet_type);
  UNUSED_PARAMETER(context);
  sl_net_notify_bus_tx_failure(status);
}

/******************************************************
 *               Local Variable Definitions
 ******************************************************/
sli_routing_entry_t wifi_command_engine_routing_entries[SLI_WIFI_COMMAND_ENGINE_MAX_PACKET] = {
  [SLI_WIFI_COMMAND_PACKET] = {
    .destination_packet_handler = sli_hal_si91x_command_send_packet,
    .packet_status_handler     = sli_command_engine_send_packet_tx_status,
    .packet_type = SLI_WIFI_COMMAND_PACKET,
  },
  // LWIP and Transceiver RAW Data packets are sent using this packet type
  [SLI_WIFI_DATA_PACKET] = {
    .destination_packet_handler = sli_hal_si91x_data_send_packet,
    .packet_status_handler     = sli_wifi_data_tx_status_handler,
    .packet_type = SLI_WIFI_DATA_PACKET,
  },
  // Only Internal socket data packets are sent using this packet type
  [SLI_WIFI_SOCKET_DATA_PACKET] = {
    .destination_packet_handler = sli_hal_si91x_data_send_packet,
#ifdef SLI_SI91X_SOCKETS
    .packet_status_handler     = sli_si91x_send_tx_packet_status_handler,
#else
    .packet_status_handler     = NULL,
#endif
    .packet_type = SLI_WIFI_SOCKET_DATA_PACKET,
  },
  // Only BLE packets are sent using this packet type
  [SLI_BT_PACKET] = {
    .destination_packet_handler = sli_hal_si91x_ble_send_packet,
#ifdef SLI_SI91X_ENABLE_BLE
    .packet_status_handler     = sli_si91x_ble_send_packet_tx_status,
#else
    .packet_status_handler     = NULL,
#endif
    .packet_type = SLI_BT_PACKET,
  }
};

sli_routing_table_t wifi_command_engine_routing_table = { .routing_table      = wifi_command_engine_routing_entries,
                                                          .routing_table_size = SLI_WIFI_COMMAND_ENGINE_MAX_PACKET };

/******************************************************
 *               Global Variable Definitions
 ******************************************************/
sli_command_engine_t sli_wifi_command_engine = { 0 };

sli_command_engine_configuration_t sli_wifi_command_engine_config = {
  .name                      = "Wi-Fi Command Engine",
  .get_packet_metadata       = sli_si91x_wifi_command_engine_get_packet_metadata,
  .priority                  = SL_WLAN_COMMAND_ENGINE_THREAD_PRIORITY,
  // M55 port: SDK default 1636 (Cortex-M4 reference). M55 real usage ~1035 B;
  // 2048 for headroom. (conan pristine = 1636.)
  .stack_size                = 2048,
  .routing_table             = &wifi_command_engine_routing_table,
  .error_event_queue         = &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_ERROR_EVENT],
  .error_event_id            = &sli_wifi_event_engine_event_id,
  .error_event               = SLI_EVENT_ENGINE_ASYNC_EVENT,
  .metadata_buffer_pool_type = SLI_BUFFER_MANAGER_CE_METADATA_POOL,
  .error_buffer_pool_type    = SLI_BUFFER_MANAGER_CE_METADATA_POOL,
  .flush_handler             = sli_si91x_wifi_command_engine_flush_metadata,
};

/******************************************************
 *               Function Definitions
 ******************************************************/
sl_status_t sli_si91x_wifi_command_engine_get_packet_metadata(const sli_command_engine_t *instance,
                                                              void *buffer,
                                                              sli_command_engine_metadata_t *metadata)
{

  UNUSED_VARIABLE(instance);
  sl_wifi_buffer_t *rx_buffer     = (sl_wifi_buffer_t *)buffer;
  sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status              = SL_STATUS_OK;
  uint8_t queue_id                = 0;
  uint16_t frame_type             = 0;
  uint16_t frame_status           = 0;

  // Process the frame
  packet = (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(rx_buffer, 0, NULL);
  if (packet == NULL) {
    return SL_STATUS_FAIL;
  }
  queue_id     = ((packet->desc[1] & 0xF0) >> 4);                      // Extract the queue ID
  frame_type   = (uint16_t)(packet->desc[2] + (packet->desc[3] << 8)); // Extract the frame type
  frame_status = sli_wifi_get_wifi_frame_status(packet);

  packet->desc[1]                      = (packet->desc[1] & 0x0F);
  metadata->tx_info.data_packet_length = packet->length;
  metadata->tx_info.frame_id           = frame_type;
  metadata->packet_status              = frame_status;

  SL_DEBUG_LOG_V2(DEBUG, "RX-> Q: %u, C: 0x%X, L: %u,", queue_id, frame_type, metadata->tx_info.data_packet_length);
  SL_DEBUG_LOG_V2(DEBUG, " S: 0x%x.\r\n", frame_status);

  switch (queue_id) {
    case SLI_WLAN_MGMT_Q: {
      switch (frame_type) {
        // Handle different frame types within the WLAN management queue
        case SLI_WIFI_RSP_OPERMODE:
        case SLI_COMMON_RSP_SOFT_RESET:
        case SLI_WIFI_RSP_PWRMODE:
        case SLI_COMMON_RSP_GET_EFUSE_DATA:
        case SLI_COMMON_RSP_GET_RAM_DUMP:
        case SLI_WIFI_RSP_ANTENNA_SELECT:
        case SLI_COMMON_RSP_ENCRYPT_CRYPTO:
        case SLI_COMMON_RSP_SET_RTC_TIMER:
        case SLI_COMMON_RSP_GET_RTC_TIMER:
        case SLI_COMMON_RSP_TA_M4_COMMANDS:
        case SLI_COMMON_RSP_SET_CONFIG:
        case SLI_COMMON_RSP_GET_CONFIG:
        case SLI_COMMON_RSP_DEBUG_LOG:
        case SLI_COMMON_RSP_NWP_LOGGING:
        case SLI_COMMON_RSP_FEATURE_FRAME:
        case SLI_COMMON_RSP_ULP_NO_RAM_RETENTION:
        case SLI_COMMON_RSP_ENABLE_DISABLE_BLE:
        case SLI_COMMON_RSP_FW_FALLBACK_FROM_HOST:
        case SLI_WIFI_RSP_CARDREADY: {
          metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_COMMON_COMMAND_PACKET;
          if (frame_type == SLI_COMMON_RSP_ULP_NO_RAM_RETENTION) {
            // This frame will come, when the M4 is waken in without ram retention. This frame is equivalent to SLI_WIFI_RSP_CARDREADY
            frame_type                 = SLI_WIFI_RSP_CARDREADY;
            packet->desc[2]            = frame_type & 0xFF;
            packet->desc[3]            = (frame_type >> 8) & 0xFF;
            metadata->tx_info.frame_id = frame_type;
          }
          break;
        }
        case SLI_WIFI_RSP_BAND:
        case SLI_WIFI_RSP_INIT:
        case SLI_WIFI_RSP_RADIO:
        case SLI_WIFI_RSP_EAP_CONFIG:
        case SLI_WIFI_RSP_SET_CERTIFICATE:
        case SLI_WIFI_RSP_HOST_PSK:
        case SLI_WIFI_RSP_JOIN:
        case SLI_WIFI_RSP_SCAN:
        case SLI_WIFI_RSP_SCAN_RESULTS:
        case SLI_WIFI_RSP_FW_VERSION:
        case SLI_WIFI_RSP_FULL_FW_VERSION:
        case SLI_WIFI_RSP_FWUP:
        case SLI_WIFI_RSP_DISCONNECT:
        case SLI_WIFI_RSP_AP_STOP:
        case SLI_WIFI_RSP_RSSI:
        case SLI_WIFI_RSP_TSF:
        case SLI_WIFI_RSP_AP_CONFIGURATION:
        case SLI_WIFI_RSP_WPS_METHOD:
        case SLI_WIFI_RSP_WPS_EXTENDED_CREDENTIALS:
        case SLI_WIFI_RSP_QUERY_NETWORK_PARAMS:
        case SLI_WIFI_RSP_SET_MAC_ADDRESS:
        case SLI_WIFI_RSP_SET_REGION:
        case SLI_WIFI_RSP_SET_REGION_AP:
        case SLI_WIFI_RSP_MAC_ADDRESS:
        case SLI_WIFI_RSP_BC_MC_FILTER_STATS:
        case SLI_WIFI_RSP_EXT_STATS:
        case SLI_WIFI_RSP_GET_STATS:
        case SLI_WIFI_RSP_RX_STATS:
        case SLI_WIFI_RSP_MODULE_STATE:
        case SLI_WIFI_RSP_QUERY_GO_PARAMS:
        case SLI_WIFI_RSP_ROAM_PARAMS:
        case SLI_WIFI_RSP_HTTP_OTAF:
        case SLI_WIFI_RSP_CLIENT_CONNECTED:
        case SLI_WIFI_RSP_CLIENT_DISCONNECTED:
        case SLI_WIFI_RSP_CALIB_WRITE:
        case SLI_WIFI_RSP_GET_DPD_DATA:
        case SLI_WIFI_RSP_CALIB_READ:
        case SLI_WIFI_RSP_FREQ_OFFSET:
        case SLI_WIFI_RSP_EVM_OFFSET:
        case SLI_WIFI_RSP_EVM_WRITE:
        case SLI_WIFI_RSP_EFUSE_READ:
        case SLI_WIFI_RSP_FILTER_BCAST_PACKETS:
        case SLI_WIFI_RSP_TWT_PARAMS:
        case SLI_WIFI_RSP_TWT_ASYNC:
        case SLI_WIFI_RSP_TWT_AUTO_CONFIG:
        case SLI_WIFI_RSP_RESCHEDULE_TWT:
        case SLI_WIFI_RSP_11AX_PARAMS:
        case SLI_WIFI_RSP_REJOIN_PARAMS:
        case SLI_WIFI_RSP_GAIN_TABLE:
        case SLI_WIFI_RSP_TX_TEST_MODE:
        case SLI_WIFI_RSP_TIMEOUT:
        case SLI_WIFI_RSP_BEACON_STOP:
        case SLI_WIFI_RSP_DYNAMIC_POOL:
        case SLI_WIFI_RSP_TRANSCEIVER_SET_CHANNEL:
        case SLI_WIFI_RSP_TRANSCEIVER_CONFIG_PARAMS:
        case SLI_WIFI_RSP_TRANSCEIVER_PEER_LIST_UPDATE:
        case SLI_WIFI_RSP_TRANSCEIVER_SET_MCAST_FILTER:
        case SLI_WIFI_RSP_TRANSCEIVER_FLUSH_DATA_Q:
        case SLI_WIFI_RSP_TRANSCEIVER_TX_DATA_STATUS:
        case SLI_WIFI_RSP_HT_CAPABILITIES:
        case SLI_WIFI_RSP_SET_MULTICAST_FILTER:
        case SLI_WIFI_RSP_CONFIG:
        case SLI_WIFI_RSP_GET_TIMEOUT:
        case SLI_WIFI_RSP_BG_SCAN:
        case SLI_WIFI_RSP_VENDOR_IE: {
          metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_WIFI_COMMAND_PACKET;
          if (frame_type == SLI_WIFI_RSP_BG_SCAN && frame_status == SL_STATUS_OK) {
            bg_enabled = true;
          }
          break;
        }
        case SLI_WIFI_RSP_IPCONFV4:
        case SLI_WIFI_RSP_IPCONFV6:
        case SLI_WIFI_RSP_IP_ADDRESS_INFO:
        case SLI_WIFI_RSP_IPV4_CHANGE:
        case SLI_WIFI_RSP_OTA_FWUP:
        case SLI_WIFI_RSP_DNS_QUERY:
        case SLI_WIFI_RSP_DNS_SERVER_ADD:
        case SLI_WIFI_RSP_SET_SNI_EMBEDDED:
        case SLI_WIFI_RSP_MULTICAST:
        case SLI_WIFI_RSP_PING_PACKET:
        case SLI_WIFI_RSP_SNTP_CLIENT:
        case SLI_WIFI_RSP_EMB_MQTT_CLIENT:
        case SLI_WIFI_RSP_EMB_MQTT_PUBLISH_PKT:
        case SLI_WIFI_RSP_MQTT_REMOTE_TERMINATE:
        case SLI_WIFI_RSP_DISCOVER_SERVICE:
        case SLI_WIFI_RSP_MDNSD:
        case SLI_WIFI_RSP_NAT:
        case SLI_WIFI_RSP_HTTP_CLIENT_GET:
        case SLI_WIFI_RSP_HTTP_CLIENT_POST:
        case SLI_WIFI_RSP_HTTP_CLIENT_POST_DATA:
        case SLI_WIFI_RSP_HTTP_ABORT:
        case SLI_WIFI_RSP_HTTP_CLIENT_PUT: {
          metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_NETWORK_COMMAND_PACKET;
          if (frame_type == SLI_WIFI_RSP_IPCONFV6) {
            metadata->tx_info.frame_id = SLI_WIFI_REQ_IPCONFV6;
          }
          break;
        }
        case SLI_WIFI_RSP_SOCKET_CONFIG:
        case SLI_WIFI_RSP_SOCKET_CREATE:
        case SLI_WIFI_RSP_SELECT_REQUEST:
        case SLI_WIFI_RSP_TCP_ACK_INDICATION: {
          metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET;
          break;
        }

        case SLI_WIFI_RSP_REMOTE_TERMINATE:
        case SLI_WIFI_RSP_CONN_ESTABLISH:
        case SLI_WIFI_RSP_SOCKET_CLOSE:
        case SLI_WIFI_RSP_SOCKET_READ_DATA:
        case SLI_WIFI_RSP_SOCKET_ACCEPT: {
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
          const sli_si91x_socket_t *socket = get_socket_from_packet(packet);
          if (socket != NULL) {
            metadata->tx_info.packet_type = (uint16_t)(socket->index + SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES);
          } else {
            metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET;
          }
          if (frame_type == SLI_WIFI_RSP_CONN_ESTABLISH) {
            metadata->tx_info.frame_id = SLI_WIFI_REQ_SOCKET_ACCEPT;
          }
#else
          metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET;
#endif
          break;
        }
        case SLI_WIFI_RSP_SET_BC_MC_FILTER_CONFIG:
        case SLI_WIFI_RSP_UPDATE_MC_ALLOWLIST:
        case SLI_WIFI_RSP_SET_BEACON_DROP_THRESHOLD: {
          metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_WIFI_COMMAND_PACKET;
          break;
        }
        default: {
          // frame_type doesn't match any known cases
          SL_DEBUG_LOG_V2(DEBUG, "Unknown frame type: %u\r\n", frame_type);
          status = SL_STATUS_INVALID_INDEX;
          break;
        }
      }
      break;
    }
    case SLI_WLAN_DATA_Q: {
      metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET;
      break;
    }
    case SLI_BT_Q: {
      metadata->tx_info.packet_type = SLI_WIFI_COMMAND_ENGINE_BLE_COMMAND_PACKET;
      break;
    }
    default: {
      // frame_type doesn't match any known cases
      SL_DEBUG_LOG_V2(DEBUG, "Unknown Queue type: %x\r\n", queue_id);
      status = SL_STATUS_INVALID_INDEX;
      break;
    }
  }

  return status;
}

static bool sli_flush_packet_match_all(const sli_queue_t *handle, const void *data, const void *node_match_data)
{
  UNUSED_PARAMETER(handle);
  UNUSED_PARAMETER(node_match_data);
  UNUSED_PARAMETER(data);
  return true; // Match all packets
}

static bool sli_flush_packet_mqtt_compare_function(const sli_queue_t *handle, const void *data, const void *context)
{
  UNUSED_PARAMETER(handle);
  UNUSED_PARAMETER(context);

  const sli_command_engine_metadata_t *metadata = (const sli_command_engine_metadata_t *)data;

  return (metadata->tx_info.frame_id == SLI_WIFI_REQ_EMB_MQTT_CLIENT);
}

// One metadata node: dummy sync error / free async - same behavior as legacy sli_process_flush_metadata_node.
static void sli_si91x_process_flush_one_metadata(sli_command_engine_metadata_t *tx_metadata,
                                                 sli_command_engine_packet_type_configuration_t *packet_config,
                                                 uint16_t frame_status)
{
  if (tx_metadata == NULL || packet_config == NULL) {
    return;
  }

  uint32_t elapsed_time = 0;

  // Check flags to determine response type
  if (tx_metadata->tx_info.flags & SLI_COMMAND_ENGINE_SYNC_RESPONSE_STATUS_PACKET) {
    // For sync responses: check timeout for both packet_queue and inflight_queue
    elapsed_time = sli_wifi_host_elapsed_time(tx_metadata->packet_start_tickcount);

    // If elapsed time exceeds timeout, just free resources (no dummy response needed)
    if (tx_metadata->tx_info.timeout == 0 || elapsed_time >= tx_metadata->tx_info.timeout) {
      // Command has timed out, free resources
      sli_buffer_manager_free_buffer(tx_metadata->tx_info.data_packet);
      sli_buffer_manager_free_buffer(tx_metadata);
      return;
    }

    // Command hasn't timed out, create dummy response
    tx_metadata->packet_status = frame_status;

    // Free data_packet and set to NULL, set data_packet_length to zero
    sli_buffer_manager_free_buffer(tx_metadata->tx_info.data_packet);
    tx_metadata->tx_info.data_packet        = NULL;
    tx_metadata->tx_info.data_packet_length = 0;

    // Enqueue metadata to sync response queue (application threads wait here via sli_wifi_receive_response_buffer)
    if (packet_config->sync_response_queue != NULL) {
      sl_status_t enqueue_status = sli_queue_manager_enqueue(packet_config->sync_response_queue, (void *)tx_metadata);
      if (enqueue_status != SL_STATUS_OK) {
        sli_buffer_manager_free_buffer(tx_metadata);
        return;
      }
    }

    // Set sync response event to wake up waiting threads
    // Application threads mostly wait on thread events, so use thread flags if thread ID is available
    if (tx_metadata->sync_resp_thread_id != NULL && packet_config->sync_response_event != 0) {
      uint32_t thread_flags_result =
        osThreadFlagsSet(tx_metadata->sync_resp_thread_id, packet_config->sync_response_event);
      if ((thread_flags_result & osFlagsError) != 0) {
        SL_DEBUG_LOG_V2(WARN, "Warning: Failed to set thread flags for sync response\r\n");
      }
    } else if (packet_config->sync_response_event_id != NULL && packet_config->sync_response_event != 0) {
      // Fallback to event flags if thread ID is not available
      uint32_t event_flags_result =
        osEventFlagsSet(*packet_config->sync_response_event_id, packet_config->sync_response_event);
      if ((event_flags_result & osFlagsError) != 0) {
        SL_DEBUG_LOG_V2(WARN, "Warning: Failed to set event flags for sync response\r\n");
      }
    }
  } else {
    sli_buffer_manager_free_buffer(tx_metadata->tx_info.data_packet);
    sli_buffer_manager_free_buffer(tx_metadata);
  }
}

// Command-engine queue flush: dummy sync error / free async; INPROGRESS uses a temp copy for the dummy response.
static bool sli_si91x_wifi_command_engine_flush_metadata(const sli_command_engine_t *instance,
                                                         uint16_t packet_type,
                                                         sli_command_engine_packet_type_configuration_t *packet_config,
                                                         sli_command_engine_metadata_t *tx_metadata)
{
  UNUSED_PARAMETER(instance);
  UNUSED_PARAMETER(packet_type);

  if (tx_metadata == NULL || packet_config == NULL) {
    return false;
  }

  const uint16_t frame_status = (uint16_t)SL_STATUS_FAIL;

  if (tx_metadata->tx_status == SLI_COMMAND_ENGINE_PACKET_TX_INPROGRESS) {
    sli_command_engine_metadata_t *temp_metadata = NULL;
    sl_status_t allocation_status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                                       SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                                       1000,
                                                                       (sli_buffer_t *)&temp_metadata);
    if (allocation_status != SL_STATUS_OK) {
      tx_metadata->tx_status = SLI_COMMAND_ENGINE_PACKET_FLUSHED;
      return true;
    }
    memcpy(temp_metadata, tx_metadata, sizeof(sli_command_engine_metadata_t));
    temp_metadata->tx_info.data_packet        = NULL;
    temp_metadata->tx_info.data_packet_length = 0;

    sli_si91x_process_flush_one_metadata(temp_metadata, packet_config, frame_status);

    tx_metadata->tx_status = SLI_COMMAND_ENGINE_TEARDOWN_DONE;
    return true;
  }

  sli_si91x_process_flush_one_metadata(tx_metadata, packet_config, frame_status);
  return true;
}

// Flush paths decrement in_flight_command_count without the command engine TX worker running;
// re-post the dynamic TX event when pending packets can be scheduled again.
static void sli_si91x_try_rearm_command_engine_dynamic_tx(
  sli_command_engine_t *instance,
  const sli_command_engine_queue_info_t *queue_info,
  const sli_command_engine_packet_type_configuration_t *packet_config)
{
  if ((instance == NULL) || (queue_info == NULL) || (packet_config == NULL)) {
    return;
  }
  if (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&queue_info->packet_queue)
      && (queue_info->in_flight_command_count < packet_config->max_in_flight_command_count)) {
    (void)sli_command_engine_signal_dynamic_tx(instance);
  }
}

static sl_status_t sli_flush_queue_for_packet_type(sli_command_engine_t *instance,
                                                   sli_command_engine_queue_info_t *queue_info,
                                                   sli_command_engine_packet_type_configuration_t *packet_config,
                                                   sli_flush_packet_compare_function_t compare_function,
                                                   const void *user_data,
                                                   uint16_t frame_status)
{
  if (frame_status == SL_STATUS_OK || queue_info == NULL || packet_config == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sli_command_engine_metadata_t *tx_metadata = NULL;
  sl_status_t status                         = SL_STATUS_OK;

  // Use provided comparator directly, or match-all handler if NULL
  sli_queue_manager_node_match_handler_t match_handler = (compare_function != NULL) ? compare_function
                                                                                    : &sli_flush_packet_match_all;

  // Flush packet_queue: drain pending packets not yet sent to firmware (tx_packet_queues).
  // Each entry is removed and processed for flush (notify/free) via sli_si91x_process_flush_one_metadata.
  while (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&queue_info->packet_queue)) {
    status = sli_queue_manager_remove_node_from_queue(&queue_info->packet_queue,
                                                      match_handler,
                                                      user_data,
                                                      (void **)&tx_metadata);

    if (status != SL_STATUS_OK) {
      break;
    }

    sli_si91x_process_flush_one_metadata(tx_metadata, packet_config, frame_status);
  }

  // Flush inflight_packet_queue: drain packets already sent to firmware (in_flight_queues).
  // For TX_INPROGRESS entries we process a copy and mark the original FLUSHED so the
  // completion path does not double-free; otherwise we process and decrement in_flight_command_count.
  while (!SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(&queue_info->inflight_packet_queue)) {
    status = sli_queue_manager_remove_node_from_queue(&queue_info->inflight_packet_queue,
                                                      match_handler,
                                                      user_data,
                                                      (void **)&tx_metadata);

    if (status != SL_STATUS_OK) {
      break;
    }

    // Decrement in_flight_command_count for all entries removed from inflight queue
    if (queue_info->in_flight_command_count > 0) {
      queue_info->in_flight_command_count--;
    }

    if (tx_metadata->tx_status == SLI_COMMAND_ENGINE_PACKET_TX_INPROGRESS) {
      sli_command_engine_metadata_t *temp_metadata = NULL;

      sl_status_t allocation_status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                                         SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                                         1000,
                                                                         (sli_buffer_t *)&temp_metadata);
      if (allocation_status != SL_STATUS_OK) {
        tx_metadata->tx_status = SLI_COMMAND_ENGINE_PACKET_FLUSHED;
        sli_si91x_try_rearm_command_engine_dynamic_tx(instance, queue_info, packet_config);
        return allocation_status;
      }
      memcpy(temp_metadata, tx_metadata, sizeof(sli_command_engine_metadata_t));
      temp_metadata->tx_info.data_packet        = NULL;
      temp_metadata->tx_info.data_packet_length = 0;

      sli_si91x_process_flush_one_metadata(temp_metadata, packet_config, frame_status);

      tx_metadata->tx_status = SLI_COMMAND_ENGINE_PACKET_FLUSHED;

    } else {

      sli_si91x_process_flush_one_metadata(tx_metadata, packet_config, frame_status);
    }
  }

  sli_si91x_try_rearm_command_engine_dynamic_tx(instance, queue_info, packet_config);
  return SL_STATUS_OK;
}

static sl_status_t sli_flush_all_command_engine_static_queues(sli_command_engine_t *instance, uint16_t error_status)
{
  if (instance == NULL || error_status == SL_STATUS_OK) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sli_command_engine_packet_type_configuration_node_t *dynamic_node = instance->dynamic_packet_type;
  while (dynamic_node != NULL) {
    if (dynamic_node->packet_type == SLI_WIFI_COMMAND_ENGINE_COMMON_COMMAND_PACKET
        || dynamic_node->packet_type == SLI_WIFI_COMMAND_ENGINE_NETWORK_COMMAND_PACKET
        || dynamic_node->packet_type == SLI_WIFI_COMMAND_ENGINE_BLE_COMMAND_PACKET
        || dynamic_node->packet_type == SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET) {
      sli_flush_queue_for_packet_type(instance,
                                      &dynamic_node->queue_info,
                                      &dynamic_node->packet_config,
                                      NULL,
                                      NULL,
                                      error_status);
    }
    dynamic_node = dynamic_node->next;
  }

  return SL_STATUS_OK;
}

static sl_status_t sli_si91x_forward_wifi_events_for_network_operations(sli_command_engine_t *instance,
                                                                        uint16_t packet_type,
                                                                        sl_wifi_buffer_t *rx_buffer)
{
  sl_status_t status = SL_STATUS_OK;

  const sl_wifi_system_packet_t *rx_packet =
    (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(rx_buffer, 0, NULL);

  if (rx_packet == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  uint16_t frame_status = sli_wifi_get_wifi_frame_status(rx_packet);

  switch (rx_packet->command) {
    case SLI_WIFI_RSP_JOIN:
      if (frame_status != SL_STATUS_OK) {
        sli_reset_coex_current_performance_profile();
        sli_post_packet_to_event_engine(rx_buffer);
      }
      break;

    case SLI_WIFI_RSP_IPCONFV6:
      if (frame_status != SL_STATUS_OK
          && (!sli_is_command_in_flight_queue(instance, packet_type, SLI_WIFI_REQ_IPCONFV6))) {
        SL_DEBUG_LOG_V2(WARN, "IPCONFV6 fail flush frame_status=0x%X", frame_status);
        status = sli_flush_all_command_engine_static_queues(instance, frame_status);
        VERIFY_STATUS_AND_RETURN(status);
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
        uint8_t vap_id = SL_WIFI_CLIENT_VAP_ID;
        status         = sli_flush_all_socket_queues(instance, (uint16_t)SL_STATUS_SI91X_SOCKET_CLOSED, &vap_id, NULL);
        VERIFY_STATUS_AND_RETURN(status);
#endif
      }
      break;

    case SLI_WIFI_RSP_DISCONNECT:
      if (frame_status == SL_STATUS_OK
          && (SL_WIFI_CLIENT_VAP_ID == sli_wifi_get_vap_id_from_operation_mode(rx_packet))) {
        SL_DEBUG_LOG_V2(INFO, "DISCONNECT client flush");
        sli_reset_coex_current_performance_profile();
        status = sli_flush_all_command_engine_static_queues(instance, (uint16_t)SL_STATUS_WIFI_CONNECTION_LOST);
        VERIFY_STATUS_AND_RETURN(status);
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
        uint8_t vap_id = SL_WIFI_CLIENT_VAP_ID;
        status         = sli_flush_all_socket_queues(instance, (uint16_t)SL_STATUS_SI91X_SOCKET_CLOSED, &vap_id, NULL);
        VERIFY_STATUS_AND_RETURN(status);
#endif
        sli_post_packet_to_event_engine(rx_buffer);
      }
      break;

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
    case SLI_WIFI_RSP_CLIENT_DISCONNECTED: {
      sl_ip_address_t dest_ip_address = { 0 };
      status                          = sli_si91x_get_dest_ip_address_from_ap_client_disconnect_resp(
        (const sli_si91x_ap_disconnect_resp_t *)rx_packet->data,
        &dest_ip_address);
      uint8_t vap_id = SL_WIFI_AP_VAP_ID;
      if (status == SL_STATUS_OK && !sli_wifi_is_ip_address_zero(&dest_ip_address)) {
        status =
          sli_flush_all_socket_queues(instance, (uint16_t)SL_STATUS_SI91X_SOCKET_CLOSED, &vap_id, &dest_ip_address);
        VERIFY_STATUS_AND_RETURN(status);
        sli_post_packet_to_event_engine(rx_buffer);
      }
    } break;

    case SLI_WIFI_RSP_REMOTE_TERMINATE: {
      status = sli_flush_socket_queues(instance, packet_type, (uint16_t)SL_STATUS_SI91X_SOCKET_CLOSED);
      VERIFY_STATUS_AND_RETURN(status);
    } break;
    case SLI_WIFI_RSP_AP_STOP:
      if (frame_status == SL_STATUS_OK) {
        uint8_t vap_id = SL_WIFI_AP_VAP_ID;
        SL_DEBUG_LOG_V2(INFO, "AP_STOP flush sockets");
        status = sli_flush_all_socket_queues(instance, (uint16_t)SL_STATUS_SI91X_SOCKET_CLOSED, &vap_id, NULL);
        VERIFY_STATUS_AND_RETURN(status);

        sli_post_packet_to_event_engine(rx_buffer);
      }
      break;

#endif // SLI_SI91X_OFFLOAD_NETWORK_STACK

    case SLI_WIFI_RSP_MQTT_REMOTE_TERMINATE: {
      SL_DEBUG_LOG_V2(INFO, "Received MQTT remote terminate, flushing the queues \r\n");
      sli_command_engine_queue_info_t *network_queue_info                   = NULL;
      sli_command_engine_packet_type_configuration_t *network_packet_config = NULL;
      sl_status_t mqtt_lookup_status =
        sli_command_engine_get_dynamic_packet_info(instance,
                                                   SLI_WIFI_COMMAND_ENGINE_NETWORK_COMMAND_PACKET,
                                                   &network_queue_info,
                                                   &network_packet_config);
      if ((mqtt_lookup_status == SL_STATUS_OK) && (network_queue_info != NULL) && (network_packet_config != NULL)) {
        status = sli_flush_queue_for_packet_type(instance,
                                                 network_queue_info,
                                                 network_packet_config,
                                                 sli_flush_packet_mqtt_compare_function,
                                                 (const void *)rx_packet,
                                                 frame_status);
        VERIFY_STATUS_AND_RETURN(status);
      }
      break;
    }

    default:
      break;
  }

  return SL_STATUS_OK;
}

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
static sl_status_t sli_flush_socket_queues(sli_command_engine_t *instance, uint16_t packet_type, uint16_t error_status)
{
  if (error_status == SL_STATUS_OK || instance == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (packet_type < SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  uint8_t socket_index       = (uint8_t)(packet_type - SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES);
  sli_si91x_socket_t *socket = (socket_index >= SLI_NUMBER_OF_SOCKETS) ? NULL : sli_si91x_sockets[socket_index];

  if (socket == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sli_command_engine_packet_type_configuration_node_t *dynamic_node = instance->dynamic_packet_type;

  while (dynamic_node != NULL && dynamic_node->packet_type != packet_type) {
    dynamic_node = dynamic_node->next;
  }

  if (dynamic_node == NULL) {
    return SL_STATUS_NOT_FOUND;
  }

  sli_flush_queue_for_packet_type(instance,
                                  &dynamic_node->queue_info,
                                  &dynamic_node->packet_config,
                                  NULL,
                                  NULL,
                                  error_status);

  if (!socket->is_receive_cmd_pending) {
    return SL_STATUS_OK;
  }

  sli_command_engine_metadata_t *metadata = NULL;
  sl_status_t status                      = SL_STATUS_OK;

  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                              1000,
                                              (sli_buffer_t *)&metadata);

  if (status != SL_STATUS_OK || metadata == NULL) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  memset(metadata, 0, sizeof(sli_command_engine_metadata_t));

  metadata->packet_status              = error_status;
  metadata->tx_info.data_packet        = NULL;
  metadata->tx_info.data_packet_length = 0;
  metadata->tx_info.flags              = 0;
  metadata->tx_info.packet_id          = 0;
  metadata->instance                   = instance;

  if (socket->socket_packet_type_configuration.sync_response_queue != NULL) {
    sl_status_t enqueue_status =
      sli_queue_manager_enqueue(socket->socket_packet_type_configuration.sync_response_queue, (void *)metadata);
    if (enqueue_status != SL_STATUS_OK) {
      // Enqueue failed (e.g., OOM): free metadata to prevent memory leak
      sli_buffer_manager_free_buffer(metadata);
      return enqueue_status;
    }
  }

  if (socket->socket_packet_type_configuration.sync_response_event_id != NULL
      && socket->socket_packet_type_configuration.sync_response_event != 0) {
    uint32_t event_result = osEventFlagsSet(*socket->socket_packet_type_configuration.sync_response_event_id,
                                            socket->socket_packet_type_configuration.sync_response_event);
    if ((event_result & osFlagsError) != 0) {
      SL_DEBUG_LOG_V2(WARN, "Warning: Failed to set event flags for socket flush response\r\n");
    }
  }

  return SL_STATUS_OK;
}

static sl_status_t sli_flush_all_socket_queues(sli_command_engine_t *instance,
                                               uint16_t error_status,
                                               const uint8_t *vap_id,
                                               const sl_ip_address_t *dest_ip_address)
{
  if (instance == NULL || vap_id == NULL || error_status == SL_STATUS_OK) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  SL_DEBUG_LOG_V2(DEBUG, "flush_all_socket_queues err=0x%X\r\n", (unsigned int)error_status);
  sl_status_t status                              = SL_STATUS_OK;
  sl_wifi_operation_mode_t current_operation_mode = sli_wifi_get_opermode();

  for (uint8_t index = 0; index < SLI_NUMBER_OF_SOCKETS; index++) {
    if (sli_si91x_sockets[index] == NULL) {
      continue;
    }
    if (!sli_si91x_socket_matches_vap_and_remote_ip(current_operation_mode,
                                                    sli_si91x_sockets[index],
                                                    *vap_id,
                                                    dest_ip_address)) {
      continue;
    }
    uint16_t socket_packet_type =
      (uint16_t)(sli_si91x_sockets[index]->index + SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES);
    status = sli_flush_socket_queues(instance, socket_packet_type, error_status);
    if (status != SL_STATUS_OK) {
      return status;
    }
  }

  return SL_STATUS_OK;
}

#endif // SLI_SI91X_OFFLOAD_NETWORK_STACK

#ifdef SL_NET_COMPONENT_INCLUDED
static void sli_post_disconnect_event_to_network_manager(sl_net_interface_t interface)
{
  if (sli_network_manager_request_queue == NULL) {
    return;
  }
  sli_network_manager_message_t message = { 0 };
  message.interface                     = interface;
  message.event_flags                   = SLI_NET_DISCONNECT_Q_EVENT;
  if (osMessageQueuePut(sli_network_manager_request_queue, &message, SLI_NET_MSG_PRIO_NORMAL, 0) != osOK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to enqueue disconnect event for auto-join retry\r\n");
  }
}

/** Client VAP → sl_net interface; must stay aligned with @ref sl_net_wifi_client_up in sl_net_si91x.c */
static bool sli_si91x_client_vap_id_to_net_interface(uint8_t vap_id, sl_net_interface_t *out_interface)
{
  if (out_interface == NULL) {
    return false;
  }
  if (vap_id == SL_WIFI_CLIENT_VAP_ID) {
    *out_interface = SL_NET_WIFI_CLIENT_1_INTERFACE;
    return true;
  }
  if (vap_id == SL_WIFI_CLIENT_VAP_ID_1) {
    *out_interface = SL_NET_WIFI_CLIENT_2_INTERFACE;
    return true;
  }
  return false;
}
#endif

sl_status_t sli_si91x_wifi_command_engine_rx_packet_handler(sli_command_engine_t *instance,
                                                            uint16_t packet_type,
                                                            void *data)
{
  if (instance == NULL || data == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  sl_status_t status =
    sli_si91x_forward_wifi_events_for_network_operations(instance, packet_type, (sl_wifi_buffer_t *)data);
  VERIFY_STATUS_AND_RETURN(status);

#ifdef SL_NET_COMPONENT_INCLUDED
  const sl_wifi_system_packet_t *packet =
    (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data((sl_wifi_buffer_t *)data, 0, NULL);
  if (packet != NULL) {
    uint16_t frame_status = sli_wifi_get_wifi_frame_status(packet);
    // Post for auto-join retry: client disconnect from AP or join failure (per client VAP / interface).
    if (packet->command == SLI_WIFI_RSP_JOIN && frame_status != (uint16_t)SL_STATUS_OK) {
      uint8_t vap_id = sli_wifi_get_vap_id_from_operation_mode(packet);
      sl_net_interface_t net_interface;
      if (sli_si91x_client_vap_id_to_net_interface(vap_id, &net_interface)) {
        sli_post_disconnect_event_to_network_manager(net_interface);
      }
    } else if (packet->command == SLI_WIFI_RSP_DISCONNECT && frame_status == (uint16_t)SL_STATUS_OK) {
      uint8_t vap_id = sli_wifi_get_vap_id_from_operation_mode(packet);
      sl_net_interface_t net_interface;
      if (sli_si91x_client_vap_id_to_net_interface(vap_id, &net_interface)) {
        sli_post_disconnect_event_to_network_manager(net_interface);
      }
    }
  }
#endif

#ifdef SLI_SI91X_INTERNAL_HTTP_CLIENT
  const sl_wifi_system_packet_t *http_packet =
    (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data((sl_wifi_buffer_t *)data, 0, NULL);

  // If the packet is NULL, there is nothing to do as we cannot determine the status
  if (http_packet == NULL) {
    return SL_STATUS_OK;
  }

  // check whether the frame response contains the end of data field
  bool is_end_of_data_type = (http_packet->command == SLI_WIFI_RSP_HTTP_CLIENT_GET
                              || http_packet->command == SLI_WIFI_RSP_HTTP_CLIENT_POST
                              || http_packet->command == SLI_WIFI_RSP_HTTP_CLIENT_POST_DATA);

  if (!is_end_of_data_type) {
    return SL_STATUS_OK;
  }

  sl_status_t http_status = sli_wifi_convert_and_save_firmware_status(sli_wifi_get_wifi_frame_status(http_packet));

  if (http_status != SL_STATUS_OK) {
    // For chunked HTTP GET/POST requests, firmware sends an interim ACK with
    // frame status HTTP_GET_CMD_IN_PROGRESS and zero payload length after each
    // chunk. Treat those ACKs as complete so command-engine can free one
    // in-flight slot and schedule the next chunk immediately.
    if ((http_status == SL_STATUS_SI91X_HTTP_GET_CMD_IN_PROGRESS) && (http_packet->length == 0U)) {
      return SL_STATUS_OK;
    }
    return http_status == SL_STATUS_SI91X_HTTP_GET_CMD_IN_PROGRESS ? SL_STATUS_IN_PROGRESS : SL_STATUS_OK;
  }

  // If the first 2 bytes of the received data are 0x0000, the HTTP GET response from the
  // server is an intermediate response and more response packets are expected. In that case
  // the command in flight is not decremented and the queue remains blocked (SL_STATUS_IN_PROGRESS).
  // If the first 2 bytes are non-zero (e.g. 0x0001), the payload marks end of data and
  // processing completes normally (SL_STATUS_OK).
  uint16_t end_of_data = 0;
  memcpy(&end_of_data, http_packet->data, sizeof(uint16_t));

  return end_of_data ? SL_STATUS_OK : SL_STATUS_IN_PROGRESS;
#endif
  return SL_STATUS_OK;
}

static bool sli_inflight_queue_has_matching_frame_id(const sli_command_engine_queue_info_t *queue_info,
                                                     uint16_t frame_id)
{
  if (queue_info == NULL) {
    return false;
  }

  for (const sli_queue_node_t *in_flight = queue_info->inflight_packet_queue.head; in_flight != NULL;
       in_flight                         = in_flight->next) {
    if (in_flight->data == NULL) {
      continue;
    }
    const sli_command_engine_metadata_t *tx_metadata = (const sli_command_engine_metadata_t *)in_flight->data;
    if (tx_metadata->tx_info.frame_id == frame_id) {
      return true;
    }
  }
  return false;
}

static bool sli_is_command_in_flight_queue(sli_command_engine_t *instance, uint16_t packet_type, uint16_t frame_id)
{
  sli_command_engine_queue_info_t *queue_info = NULL;
  sl_status_t status = sli_command_engine_get_dynamic_packet_info(instance, packet_type, &queue_info, NULL);
  if ((status != SL_STATUS_OK) || (queue_info == NULL)) {
    return false;
  }
  return sli_inflight_queue_has_matching_frame_id(queue_info, frame_id);
}

sl_status_t sli_si91x_wifi_command_engine_packet_handler(
  void *buffer,
  uint32_t packet_size,
  sli_routing_utility_packet_status_handler_t packet_status_handler,
  void *context)
{
  UNUSED_PARAMETER(packet_status_handler);
  UNUSED_PARAMETER(context);
  UNUSED_PARAMETER(packet_size);

  sl_status_t status = sli_command_engine_receive_packet(&sli_wifi_command_engine, buffer);
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer((sl_wifi_buffer_t *)buffer);
    return status;
  }
  return status;
}

static void sli_post_packet_to_event_engine(sl_wifi_buffer_t *rx_buffer)
{
  /* Validate parameter: caller retains ownership of rx_buffer */
  if (rx_buffer == NULL) {
    return;
  }

  sli_command_engine_response_t *response = NULL;
  sl_wifi_buffer_t *packet_buffer         = NULL;
  sl_status_t allocation_status;

  /*
   * Allocate an RX-style buffer to hold the copied packet. Use HYBRID so the
   * allocation will fall back to heap if the pool is exhausted.
   */
  allocation_status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_RX_POOL,
                                                         SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                         1000,
                                                         (sli_buffer_t *)&packet_buffer);
  if (allocation_status != SL_STATUS_OK) {
    /* Best-effort: give up if allocation fails */
    return;
  }

  /* Copy the packet payload into the newly allocated buffer */
  const sl_wifi_system_packet_t *rx_packet =
    (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(rx_buffer, 0, NULL);
  sl_wifi_system_packet_t *destination_packet =
    (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(packet_buffer, 0, NULL);
  uint16_t payload_len = (uint16_t)(rx_packet->length & 0x0FFF);
  /* Copy header + payload; dest buffer layout matches source layout */
  memcpy(destination_packet, rx_packet, sizeof(sl_wifi_system_packet_t) + payload_len);

  /*
   * Allocate metadata wrapper (response object) which the event engine expects.
   * Use HYBRID allocation to improve robustness in low-memory situations.
   */
  allocation_status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                         SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                         1000,
                                                         (sli_buffer_t *)&response);
  if (allocation_status != SL_STATUS_OK) {
    /* Clean up data buffer on failure and exit */
    sli_buffer_manager_free_buffer(packet_buffer);
    return;
  }

  /* Populate the response wrapper to indicate a packet-only response */
  response->data = packet_buffer;
  response->type = SLI_COMMAND_ENGINE_PACKET_ONLY_RESPONSE;

  /* Enqueue the wrapped response to the network event queue */
  if (SL_STATUS_OK
      != sli_queue_manager_enqueue(&event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_NETWORK_EVENT], (void *)response)) {
    /* On enqueue failure free both allocated objects */
    sli_buffer_manager_free_buffer(response);
    sli_buffer_manager_free_buffer(packet_buffer);
    return;
  }

  /* Signal the event engine to process the queued packet */
  sli_wifi_event_engine_signal_async();
}
