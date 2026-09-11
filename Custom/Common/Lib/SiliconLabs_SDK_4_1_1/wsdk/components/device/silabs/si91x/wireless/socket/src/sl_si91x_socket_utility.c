/***************************************************************************/ /**
 * @file  sl_si91x_socket_utility.c
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

#include "sl_si91x_socket_utility.h"
#include "sli_wifi_utility.h"
#include "sl_si91x_socket_types.h"
#include "sl_si91x_socket_callback_framework.h"
#include "sl_status.h"
#include "sl_constants.h"
#include "sl_si91x_driver.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_socket_constants.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_core_utilities.h"
#include "sl_rsi_utility.h"
#include "sl_core.h"
#include "sli_wifi.h"
#include "sl_string.h"
#include <string.h>
#include <stdbool.h>
#include "sli_si91x_wifi_command_engine_packet.h"
#include "sli_hal_si91x.h"
#include "sli_si91x_wifi_event_handler.h"
#include "sli_si91x_wifi_command_engine.h"
#include "sli_wifi_command_engine_config.h"
#include "sl_cmsis_utility.h"
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_ip_types.h"
#endif
#include "sli_net_utility.h"

/******************************************************
 *               External Variable Definitions
 ******************************************************/
extern sli_queue_t event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_MAX_EVENTS];
/******************************************************
 *               Macro Definitions
 ******************************************************/
#define SLI_SI91X_SOCKET_ACCEPT_SUCCESS_EVENT (1 << 0)
#define SLI_SI91X_SOCKET_ACCEPT_FAILURE_EVENT (1 << 1)

#ifndef SL_SOCKET_DEFAULT_BUFFER_LIMIT
#define SL_SOCKET_DEFAULT_BUFFER_LIMIT 3
#endif

/******************************************************
 *                    Structures
 ******************************************************/

typedef struct {
  uint8_t in_use;
  uint8_t select_id;
  uint16_t frame_status;
  union {
    sl_si91x_socket_select_callback_t select_callback;
    sli_si91x_socket_select_rsp_t *response_data;
  };
} sli_si91x_select_request_t;

/******************************************************
 *               Static Function Declarations
 ******************************************************/

static void sli_si91x_clear_select_id(uint8_t flag);
static sli_si91x_select_request_t *sli_si91x_get_available_select_id(void);
static void sli_free_sockets_by_port(uint16_t port_number);

/**
 * A internal function to check whether a particular port is available or not.
 * @param port_number port_number which needs to be verified for availability.
 * @return True if available else false.
 */
static bool sli_is_port_available(uint16_t port_number);

/******************************************************
 *               Variable Definitions
 ******************************************************/
sli_si91x_socket_t *sli_si91x_sockets[SLI_NUMBER_OF_SOCKETS]                                 = { 0 };
static sl_si91x_socket_remote_termination_callback_t user_remote_socket_termination_callback = NULL;
static osMutexId_t sli_si91x_socket_mutex                                                    = NULL;
static uint8_t sli_si91x_max_select_count                                                    = 0;

static sli_si91x_select_request_t *select_request_table = NULL;

osEventFlagsId_t si91x_socket_events        = 0;
osEventFlagsId_t si91x_socket_select_events = 0;

uint32_t sl_si91x_socket_selected_ciphers          = SL_SI91X_TLS_DEFAULT_CIPHERS;
uint32_t sl_si91x_socket_selected_extended_ciphers = SL_SI91X_TLS_EXT_CIPHERS;

/******************************************************
 *               Function Definitions
 ******************************************************/

static inline bool sli_is_per_socket_close_enabled(const sli_si91x_socket_t *socket)
{
  return (socket != NULL) && (socket->socket_ext_bitmap & SLI_SI91X_SOCKET_FEAT_PER_SOCKET_CLOSE);
}

static void sli_si91x_socket_rx_queue_flush_handler(const sli_queue_t *handle, void *data, const void *context)
{
  UNUSED_PARAMETER(handle);
  UNUSED_PARAMETER(context);

  sli_command_engine_metadata_t *metadata = (sli_command_engine_metadata_t *)data;

  if (metadata->tx_info.data_packet != NULL) {
    sli_buffer_manager_free_buffer(metadata->tx_info.data_packet);
    metadata->tx_info.data_packet        = NULL;
    metadata->tx_info.data_packet_length = 0;
  }

  sli_buffer_manager_free_buffer(metadata);
  metadata = NULL;
}

void sli_handle_accept_response(sli_si91x_socket_t *si91x_client_socket,
                                const sli_si91x_rsp_ltcp_est_t *accept_response)
{
  //Verifying socket existence
  if (si91x_client_socket == NULL)
    return;
  // Update socket parameters based on the accept response
  si91x_client_socket->id                         = accept_response->socket_id;
  si91x_client_socket->local_address.sin6_port    = accept_response->src_port_num;
  si91x_client_socket->remote_address.sin6_port   = accept_response->dest_port;
  si91x_client_socket->mss                        = accept_response->mss;
  si91x_client_socket->state                      = CONNECTED;
  si91x_client_socket->remote_address.sin6_family = accept_response->ip_version == SL_IPV6_ADDRESS_LENGTH ? AF_INET6
                                                                                                          : AF_INET;

  if (si91x_client_socket->remote_address.sin6_family == AF_INET6) {
#ifdef SLI_SI91X_NETWORK_DUAL_STACK
    memcpy(si91x_client_socket->remote_address.sin6_addr.un.u8_addr,
           accept_response->dest_ip_addr.ipv6_address,
           SL_IPV6_ADDRESS_LENGTH);
#else
#ifndef __ZEPHYR__
    memcpy(si91x_client_socket->remote_address.sin6_addr.__u6_addr.__u6_addr8,
           accept_response->dest_ip_addr.ipv6_address,
           SL_IPV6_ADDRESS_LENGTH);
#else
    memcpy(si91x_client_socket->remote_address.sin6_addr.s6_addr,
           accept_response->dest_ip_addr.ipv6_address,
           SL_IPV6_ADDRESS_LENGTH);
#endif
#endif

  } else {
    memcpy(&((struct sockaddr_in *)&si91x_client_socket->remote_address)->sin_addr,
           accept_response->dest_ip_addr.ipv4_address,
           SL_IPV4_ADDRESS_LENGTH);
  }
}

#ifndef __ZEPHYR__
int sli_handle_select_response(const sli_si91x_socket_select_rsp_t *response,
                               fd_set *readfds,
                               fd_set *writefds,
                               fd_set *exception_fd)
#else
int sli_handle_select_response(const sli_si91x_socket_select_rsp_t *response,
                               sl_si91x_fdset_t *readfds,
                               sl_si91x_fdset_t *writefds,
                               sl_si91x_fdset_t *exception_fd)
#endif
{
  // To track of the total number of file descriptors set
  int total_fd_set_count = 0;

  // Clear file descriptor sets
  SLI_SI91X_NULL_SAFE_FD_ZERO(readfds);
  SLI_SI91X_NULL_SAFE_FD_ZERO(writefds);
  SLI_SI91X_NULL_SAFE_FD_ZERO(exception_fd);

  // Iterate through all host sockets
  for (int host_socket_index = 0; host_socket_index < SLI_NUMBER_OF_SOCKETS; host_socket_index++) {
    const sli_si91x_socket_t *socket = sli_get_si91x_socket(host_socket_index);
    //Verifying socket existence
    if (socket == NULL) {
      continue;
    }

    // Check if the read file descriptor set is provided and if the corresponding bit is set in the response
    if (readfds != NULL && (response->read_fds.fd_array[0] & (1 << socket->id))) {
#ifndef __ZEPHYR__
      FD_SET(host_socket_index, readfds);
#else
      SL_SI91X_FD_SET(host_socket_index, readfds);
#endif
      total_fd_set_count++;
    }

    // Check if the write file descriptor set is provided and if the corresponding bit is set in the response.
    if (writefds != NULL && (response->write_fds.fd_array[0] & (1 << socket->id))) {
#ifndef __ZEPHYR__
      FD_SET(host_socket_index, writefds);
#else
      SL_SI91X_FD_SET(host_socket_index, writefds);
#endif
      total_fd_set_count++;
    }
  }

  return total_fd_set_count;
}

void sli_si91x_set_accept_callback(sli_si91x_socket_t *server_socket,
                                   sl_si91x_socket_accept_callback_t callback,
                                   int32_t client_socket_id)
{
  // Set the user-defined accept callback function and the client socket ID
  server_socket->user_accept_callback = callback;
  server_socket->client_id            = client_socket_id;
}

void sli_si91x_set_remote_socket_termination_callback(sl_si91x_socket_remote_termination_callback_t callback)
{
  user_remote_socket_termination_callback = callback;
}

sl_status_t sli_si91x_socket_init(uint8_t max_select_count)
{
  // Check if the mutex for socket operations is already initialized.
  // If not, create a new mutex to ensure thread-safe access.
  if (sli_si91x_socket_mutex == NULL) {
    sli_si91x_socket_mutex = osMutexNew(NULL); // Create a new mutex.
    if (sli_si91x_socket_mutex == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Socket init: mutex creation failed\r\n");
      return SL_STATUS_FAIL; // Return failure if mutex creation fails.
    }
  }

  // Check if the event flags object for socket events is already initialized.
  // If not, create a new event flag set to manage socket events.
  if (si91x_socket_events == NULL) {
    si91x_socket_events = osEventFlagsNew(NULL); // Create new event flags.
    if (si91x_socket_events == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Socket init: socket event flags creation failed\r\n");
      return SL_STATUS_FAIL; // Return failure if event flag creation fails.
    }
  }

  // Check if the event flags object for socket select events is already initialized.
  // If not, create a new event flag set to manage socket select events.
  if (si91x_socket_select_events == NULL) {
    si91x_socket_select_events = osEventFlagsNew(NULL); // Create new event flags.
    if (si91x_socket_select_events == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Socket init: select event flags creation failed\r\n");
      return SL_STATUS_FAIL; // Return failure if event flag creation fails.
    }
  }

  /* 
  Allocate memory for the select request table based on the number of select instances.
  Heap memory is allocated for the number of instances of this structure based on 
  the number of selects configured by the user during device initialization in opermode.
  Each time a sync or async select command is sent to the firmware, the corresponding 
  structure is updated, and the instance is cleared when the response is received.
  */

  // Check if the select_request_table is uninitialized and max_select_count is valid.
  if (select_request_table == NULL && max_select_count != 0 && max_select_count <= 10) {
    sli_si91x_max_select_count = max_select_count; // Store the max number of selects.

    // Allocate memory for the select request table based on the max_select_count.
    select_request_table = SLI_CALLOC(max_select_count, sizeof(sli_si91x_select_request_t));

    // If memory allocation fails, return failure.
    if (select_request_table == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Socket init: select request table allocation failed\r\n");
      return SL_STATUS_FAIL; // Return failure if memory allocation fails.
    }
  }

  return SL_STATUS_OK; // Return success if initialization is successful.
}

sl_status_t sli_si91x_socket_deinit(void)
{
  // free the sli_si91x_socket_mutex
  if (sli_si91x_socket_mutex != NULL) {
    osMutexDelete(sli_si91x_socket_mutex);
    sli_si91x_socket_mutex = NULL;
  }
  if (si91x_socket_events != NULL) {
    osEventFlagsDelete(si91x_socket_events);
    si91x_socket_events = NULL;
  }
  if (si91x_socket_select_events != NULL) {
    osEventFlagsDelete(si91x_socket_select_events);
    si91x_socket_select_events = NULL;
  }
  if (select_request_table != NULL) {
    SLI_FREE(select_request_table);
    select_request_table = NULL;
  }
  return SL_STATUS_OK;
}

sl_status_t sli_si91x_vap_shutdown(uint8_t vap_id, sli_si91x_bsd_disconnect_reason_t disconnect_reason)
{
  // Iterate through all BSD sockets and modify the state those associated with the given VAP ID
  for (uint8_t socket_index = 0; socket_index < SLI_NUMBER_OF_SOCKETS; socket_index++) {
    if ((sli_si91x_sockets[socket_index] != NULL) && (sli_si91x_sockets[socket_index]->vap_id == vap_id)) {
      sli_si91x_sockets[socket_index]->state             = DISCONNECTED;
      sli_si91x_sockets[socket_index]->disconnect_reason = disconnect_reason;
    }
  }

  return SL_STATUS_OK;
}

bool sli_si91x_socket_matches_vap_and_remote_ip(sl_wifi_operation_mode_t opermode,
                                                const sli_si91x_socket_t *socket,
                                                uint8_t filter_vap_id,
                                                const sl_ip_address_t *dest_ip_address)
{
  if (socket == NULL) {
    return false;
  }

  uint8_t socket_vap_id = (opermode == SL_WIFI_ACCESS_POINT_MODE) ? SL_WIFI_AP_VAP_ID : SL_WIFI_CLIENT_VAP_ID;
  if (opermode == SL_SI91X_CONCURRENT_MODE) {
    socket_vap_id = socket->vap_id;
  }
  bool matches = (socket_vap_id == filter_vap_id);

  if (matches && dest_ip_address != NULL) {
    bool is_same = false;
    if (dest_ip_address->type == SL_IPV4) {
      const struct sockaddr_in *socket_address = (const struct sockaddr_in *)&socket->remote_address;
      is_same = (memcmp(dest_ip_address->ip.v4.bytes, &socket_address->sin_addr.s_addr, SL_IPV4_ADDRESS_LENGTH) == 0);
    } else {
      const struct sockaddr_in6 *ipv6_socket_address = &socket->remote_address;
#ifdef SLI_SI91X_NETWORK_DUAL_STACK
      is_same =
        (memcmp(dest_ip_address->ip.v6.bytes, &ipv6_socket_address->sin6_addr.un.u8_addr, SL_IPV6_ADDRESS_LENGTH) == 0);
#else
#ifndef __ZEPHYR__
      is_same = (memcmp(dest_ip_address->ip.v6.bytes,
                        &ipv6_socket_address->sin6_addr.__u6_addr.__u6_addr8,
                        SL_IPV6_ADDRESS_LENGTH)
                 == 0);
#else
      is_same =
        (memcmp(dest_ip_address->ip.v6.bytes, &ipv6_socket_address->sin6_addr.s6_addr, SL_IPV6_ADDRESS_LENGTH) == 0);
#endif
#endif
    }
    matches = is_same;
  }

  return matches;
}

static void sli_si91x_mark_bsd_sockets_disconnected_matching(uint8_t filter_vap_id,
                                                             const sl_ip_address_t *dest_ip_address,
                                                             sli_si91x_bsd_disconnect_reason_t reason)
{
  sl_wifi_operation_mode_t current_operation_mode = sli_wifi_get_opermode();

  for (uint8_t index = 0; index < SLI_NUMBER_OF_SOCKETS; index++) {
    if (sli_si91x_sockets[index] == NULL) {
      continue;
    }
    if (!sli_si91x_socket_matches_vap_and_remote_ip(current_operation_mode,
                                                    sli_si91x_sockets[index],
                                                    filter_vap_id,
                                                    dest_ip_address)) {
      continue;
    }
    sli_si91x_sockets[index]->state             = DISCONNECTED;
    sli_si91x_sockets[index]->disconnect_reason = reason;
  }
}

void sli_si91x_sync_bsd_socket_states_for_flush_scenarios(const sl_wifi_system_packet_t *packet)
{
  if (packet == NULL) {
    return;
  }

  uint16_t frame_status = sli_wifi_get_wifi_frame_status(packet);

  switch (packet->command) {
    case SLI_WIFI_RSP_JOIN:
      if (frame_status != SL_STATUS_OK) {
        sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_CLIENT_VAP_ID,
                                                         NULL,
                                                         SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      }
      break;
    case SLI_WIFI_RSP_IPCONFV4:
      if (frame_status != SL_STATUS_OK) {
        sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_CLIENT_VAP_ID,
                                                         NULL,
                                                         SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      }
      break;
    case SLI_WIFI_RSP_IPCONFV6:
      if (frame_status != SL_STATUS_OK) {
        sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_CLIENT_VAP_ID,
                                                         NULL,
                                                         SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      }
      break;
    case SLI_WIFI_RSP_IPV4_CHANGE:
      sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_CLIENT_VAP_ID,
                                                       NULL,
                                                       SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      break;
    case SLI_WIFI_RSP_DISCONNECT:
      if (frame_status == SL_STATUS_OK && (SL_WIFI_CLIENT_VAP_ID == sli_wifi_get_vap_id_from_operation_mode(packet))) {
        sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_CLIENT_VAP_ID,
                                                         NULL,
                                                         SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      }
      break;
    case SLI_WIFI_RSP_CLIENT_DISCONNECTED: {
      sl_ip_address_t dest_ip_address = { 0 };
      sl_status_t st                  = sli_si91x_get_dest_ip_address_from_ap_client_disconnect_resp(
        (const sli_si91x_ap_disconnect_resp_t *)packet->data,
        &dest_ip_address);
      if (st == SL_STATUS_OK && !sli_wifi_is_ip_address_zero(&dest_ip_address)) {
        sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_AP_VAP_ID,
                                                         &dest_ip_address,
                                                         SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      }
    } break;
    case SLI_WIFI_RSP_AP_STOP:
      if (frame_status == SL_STATUS_OK) {
        sli_si91x_mark_bsd_sockets_disconnected_matching(SL_WIFI_AP_VAP_ID,
                                                         NULL,
                                                         SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN);
      }
      break;
    default:
      break;
  }
}

void sli_si91x_handle_websocket(sli_si91x_socket_create_request_t *socket_create_request,
                                const sli_si91x_socket_t *si91x_bsd_socket)
{
  socket_create_request->ssl_bitmap |= SLI_SI91X_WEBSOCKET_FEAT;

  // Copy host name
  if (si91x_bsd_socket->websocket_info && si91x_bsd_socket->websocket_info->host_length > 0) {
    memcpy(socket_create_request->webs_host_name,
           si91x_bsd_socket->websocket_info->websocket_data,
           si91x_bsd_socket->websocket_info->host_length);
    socket_create_request->webs_host_name[si91x_bsd_socket->websocket_info->host_length] = '\0'; // Null-terminate
  }

  // Copy resource name
  if (si91x_bsd_socket->websocket_info && si91x_bsd_socket->websocket_info->resource_length > 0) {
    memcpy(socket_create_request->webs_resource_name,
           si91x_bsd_socket->websocket_info->websocket_data + si91x_bsd_socket->websocket_info->host_length,
           si91x_bsd_socket->websocket_info->resource_length);
    socket_create_request->webs_resource_name[si91x_bsd_socket->websocket_info->resource_length] =
      '\0'; // Null-terminate
  }

  // Copy subprotocol name
  if (si91x_bsd_socket->websocket_info && si91x_bsd_socket->websocket_info->subprotocol_length > 0) {
    memcpy(socket_create_request->webs_subprotocol_name,
           si91x_bsd_socket->websocket_info->websocket_data + si91x_bsd_socket->websocket_info->host_length
             + si91x_bsd_socket->websocket_info->resource_length,
           si91x_bsd_socket->websocket_info->subprotocol_length);
    socket_create_request->webs_subprotocol_name[si91x_bsd_socket->websocket_info->subprotocol_length] =
      '\0'; // Null-terminate
  }

  // Copy origin
  if (si91x_bsd_socket->websocket_info && si91x_bsd_socket->websocket_info->origin_length > 0) {
    memcpy(socket_create_request->webs_origin,
           si91x_bsd_socket->websocket_info->websocket_data + si91x_bsd_socket->websocket_info->host_length
             + si91x_bsd_socket->websocket_info->resource_length + si91x_bsd_socket->websocket_info->subprotocol_length,
           si91x_bsd_socket->websocket_info->origin_length);
    socket_create_request->webs_origin[si91x_bsd_socket->websocket_info->origin_length] = '\0'; // Null-terminate
  }
}

sl_status_t sl_si91x_config_socket(sl_si91x_socket_config_t socket_config)
{
  sl_status_t status = SL_STATUS_OK;

  // Send the socket configuration command to the SI91X driver
  status = sli_wifi_send_command(SLI_WIFI_REQ_SOCKET_CONFIG,
                                 SLI_SI91X_SOCKET_CMD,
                                 &socket_config,
                                 sizeof(socket_config),
                                 SLI_WIFI_RSP_SOCKET_CONFIG_WAIT_TIME,
                                 NULL,
                                 NULL);

  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

/**
 * @brief Frees resources associated with a given socket.
 *
 * This function releases the memory allocated for a socket and 
 * deletes any associated OS event flags. It ensures proper cleanup 
 * by setting the socket pointer to NULL after freeing it.
 *
 * @param socket The socket index to be freed.
 */
void sli_si91x_free_socket(int socket)
{
  // Retrieve the socket object associated with the given socket index.
  sli_si91x_socket_t *si91x_socket = sli_get_si91x_socket(socket);

  // If the socket is invalid (NULL), no cleanup is needed; return early.
  if (si91x_socket == NULL) {
    return;
  }

  // Check if the socket has associated OS event flags.
  if (si91x_socket->socket_events != NULL) {
    // Delete the OS event flags associated with the socket to free resources.
    osEventFlagsDelete(si91x_socket->socket_events);

    // Set the event flags pointer to NULL to avoid dangling pointers.
    si91x_socket->socket_events = NULL;
  }

  // Free the domain name if it was allocated
  if (si91x_socket->domain_name != NULL) {
    SLI_FREE(si91x_socket->domain_name);
    si91x_socket->domain_name = NULL;
  }
  sl_status_t status =
    sli_command_engine_remove_packet_type(&sli_wifi_command_engine,
                                          (uint8_t)(SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES + si91x_socket->index));
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(DEBUG, "Failed to remove packet type from command engine\r\n");
  }

  // Set the global socket pointer to NULL to prevent future use of freed memory.
  sli_si91x_sockets[socket] = NULL;

  sli_queue_manager_deinit(&si91x_socket->rx_queue, &sli_si91x_socket_rx_queue_flush_handler, NULL);

  // Free the memory allocated for the socket structure.
  SLI_FREE(si91x_socket);

  // Set the global socket pointer to NULL to prevent future use of freed memory.
  sli_si91x_sockets[socket] = NULL;
}

// Get the SI91X socket with the specified index, if it is valid and not in RESET state
sli_si91x_socket_t *sli_get_si91x_socket(int32_t socket)
{
  if (socket < 0 || socket >= SLI_NUMBER_OF_SOCKETS) {
    return NULL;
  }
  return sli_si91x_sockets[socket];
}

/**
 * @brief Frees all sockets that match the given port number.
 *
 * This helper function iterates through all sockets and frees those
 * that match the specified port number.
 *
 * @param port_number The port number to match against.
 */
static void sli_free_sockets_by_port(uint16_t port_number)
{
  for (uint8_t index = 0; index < SLI_NUMBER_OF_SOCKETS; index++) {
    const sli_si91x_socket_t *socket_id = sli_get_si91x_socket(index);
    // Verifying socket existence and port match
    if (socket_id != NULL && socket_id->local_address.sin6_port == port_number) {
      sli_si91x_free_socket(index);
    }
  }
}

sli_si91x_socket_t *sli_si91x_get_socket_from_id(int socket_id,
                                                 sli_si91x_bsd_socket_state_t excluded_state,
                                                 int16_t role)
{
  sli_si91x_socket_t *possible_socket = NULL;
  for (uint8_t index = 0; index < SLI_NUMBER_OF_SOCKETS; ++index) {
    sli_si91x_socket_t *socket = sli_si91x_sockets[index];
    if (socket != NULL && socket->id == socket_id && socket->state != excluded_state
        && (role == -1 || socket->role == role)) {
      return socket;
    }
  }
  return possible_socket;
}

static sli_si91x_socket_t *sli_si91x_get_socket_from_port(uint16_t src_port)
{
  for (int i = 0; i < SLI_NUMBER_OF_SOCKETS; i++) {
    if (sli_si91x_sockets[i] == NULL) {
      continue;
    }
    if ((sli_si91x_sockets[i]->role == SLI_SI91X_SOCKET_TCP_SERVER)
        && (src_port == sli_si91x_sockets[i]->local_address.sin6_port)) {
      return sli_si91x_sockets[i];
    }
  }

  return NULL;
}

// Find and return an available socket and its index
sl_status_t sli_get_free_socket(sli_si91x_socket_t **socket, int *socket_fd)
{
  *socket            = NULL;
  *socket_fd         = -1;
  sl_status_t status = SL_STATUS_NOT_FOUND;

  osMutexAcquire(sli_si91x_socket_mutex, 0xFFFFFFFFUL);
  int8_t socket_index = -1;
  // Iterate through all available sockets to find a free one
  for (uint8_t index = 0; index < SLI_NUMBER_OF_SOCKETS; index++) {

    // If the socket is in use skip it
    if (sli_si91x_sockets[index] != NULL) {
      continue;
    }

    socket_index = index;
    break;
  }

  if (socket_index == -1) {
    osMutexRelease(sli_si91x_socket_mutex);
    return SL_STATUS_NOT_FOUND;
  }

  // Allocate new socket
  // NOTE: The 'new_socket' variable is intentionally used to prevent race conditions between allocation and initialization.
  // Removing or optimizing this intermediate variable may lead to unexpected issues.
  sli_si91x_socket_t *new_socket = SLI_MALLOC(sizeof(sli_si91x_socket_t));

  if (new_socket == NULL) {
    osMutexRelease(sli_si91x_socket_mutex);
    SL_DEBUG_LOG_V2(ERROR, "Socket struct malloc failed\r\n");
    return SL_STATUS_ALLOCATION_FAILED;
  }

  memset(new_socket, 0, sizeof(sli_si91x_socket_t));
  sli_si91x_sockets[socket_index]                    = new_socket;
  sli_si91x_sockets[socket_index]->id                = -1;
  sli_si91x_sockets[socket_index]->index             = socket_index;
  sli_si91x_sockets[socket_index]->data_buffer_limit = SL_SOCKET_DEFAULT_BUFFER_LIMIT;

  status = sli_queue_manager_init(&sli_si91x_sockets[socket_index]->rx_queue, SLI_BUFFER_MANAGER_QUEUE_NODE_POOL);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Socket RX queue init failed: 0x%lX\r\n", status);
    SLI_FREE(sli_si91x_sockets[socket_index]);
    sli_si91x_sockets[socket_index] = NULL;
    osMutexRelease(sli_si91x_socket_mutex);
    return SL_STATUS_FAIL;
  }

  // Fill the socket packet type configuration structure
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.rx_event_handler =
    sli_si91x_wifi_command_engine_rx_packet_handler;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.pre_tx_handler = sli_si91x_socket_pre_tx_handler;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.packet_processing_type =
    SLI_COMMAND_ENGINE_COMMAND_PACKET;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.route_packet_type = SLI_WIFI_COMMAND_PACKET;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.sync_response_queue =
    &sli_si91x_sockets[socket_index]->rx_queue;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.sync_response_event         = (1 << socket_index);
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.sync_response_event_id      = &si91x_socket_events;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.max_in_flight_command_count = 1;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.async_response_queue =
    &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_CMD_EVENT];
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.async_response_event_id =
    &sli_wifi_event_engine_event_id;
  sli_si91x_sockets[socket_index]->socket_packet_type_configuration.async_response_event = SLI_EVENT_ENGINE_ASYNC_EVENT;

  status = sli_command_engine_add_packet_type(&sli_wifi_command_engine,
                                              (SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES + socket_index),
                                              &sli_si91x_sockets[socket_index]->socket_packet_type_configuration);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Command engine add_packet_type failed: 0x%lX\r\n", status);
    sli_queue_manager_deinit(&sli_si91x_sockets[socket_index]->rx_queue, NULL, NULL);
    SLI_FREE(sli_si91x_sockets[socket_index]);
    sli_si91x_sockets[socket_index] = NULL;
    osMutexRelease(sli_si91x_socket_mutex);
    return SL_STATUS_FAIL;
  }

  // If a free socket is found, set the socket pointer to point to it
  *socket = sli_si91x_sockets[socket_index];
  // Set the socket_fd to the index of the free socket, which can be used as a file descriptor
  *socket_fd = socket_index;
  // Exit the loop because a free socket has been found.

  osMutexRelease(sli_si91x_socket_mutex);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sli_si91x_socket_pre_tx_handler(sli_command_engine_t *instance, uint16_t packet_type, void *data)
{
  if (data == NULL || instance == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  sli_command_engine_metadata_t *metadata = (sli_command_engine_metadata_t *)data;

  sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)metadata->tx_info.data_packet;
  if (packet == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (packet->command == SLI_WIFI_REQ_SOCKET_ACCEPT || packet->command == SLI_WIFI_RSP_SOCKET_CLOSE
      || packet->command == SLI_WIFI_REQ_SOCKET_READ_DATA) {
    // fill the firmware_queue_id in the packet header
    packet->desc[1] |= (SLI_WLAN_MGMT_Q << 4);
  }

  if (packet->command == SLI_WIFI_REQ_SOCKET_READ_DATA && packet_type >= SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES) {
    // Check if the socket is already closed
    // Extract socket index from packet_type: packet_type = socket->index + SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES
    uint8_t socket_index             = (uint8_t)(packet_type - SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES);
    const sli_si91x_socket_t *socket = sli_get_si91x_socket(socket_index);

    // If socket is NULL or already closed (DISCONNECTED state), generate dummy metaresponse
    if (socket == NULL || socket->state == DISCONNECTED) {
      sl_status_t status                                              = SL_STATUS_OK;
      sli_command_engine_packet_type_configuration_t packet_type_info = { 0 };

      // Get packet type configuration for the given command type
      status = sli_command_engine_get_rx_queue_info_from_packet_type(instance, packet_type, &packet_type_info);
      VERIFY_STATUS_AND_RETURN(status);
      sli_command_engine_metadata_t *dummy_metadata = NULL;
      // Allocate metadata buffer (hybrid allocation allows pool + heap fallback)
      status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                  SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                  1000,
                                                  (sli_buffer_t *)&dummy_metadata);
      VERIFY_STATUS_AND_RETURN(status);
      // Initialize dummy metadata with socket closed status
      dummy_metadata->packet_status              = (uint16_t)SL_STATUS_SI91X_SOCKET_CLOSED;
      dummy_metadata->tx_info.data_packet        = NULL;
      dummy_metadata->tx_info.data_packet_length = 0;
      dummy_metadata->tx_info.flags              = 0;
      dummy_metadata->tx_info.packet_id          = 0;

      // Enqueue dummy metaresponse to wake up waiting application thread
      status = sli_queue_manager_enqueue(packet_type_info.sync_response_queue, dummy_metadata);
      if (status == SL_STATUS_OK) {
        // Set event flag to notify waiting thread
        osEventFlagsSet(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
      } else {
        // If enqueue fails, free the allocated metadata
        sli_buffer_manager_free_buffer(dummy_metadata);
      }
      return SL_STATUS_FAIL;
    }
  }

  return SL_STATUS_OK;
}

static bool sli_is_port_available(uint16_t port_number)
{
  // Port 0 requests auto-assigned local port; skip availability check.
  if (port_number == 0) {
    return true;
  }

  // Check whether local port is already used or not
  for (uint8_t socket_index = 0; socket_index < SLI_NUMBER_OF_SOCKETS; socket_index++) {
    if (sli_si91x_sockets[socket_index] != NULL
        && sli_si91x_sockets[socket_index]->local_address.sin6_port == port_number) {
      return false;
    }
  }

  return true;
}

/**
 * @brief This function is responsible to copy the TLS extension information provided by application into socket structure.
 * 
 * @param socket_tls_extensions pointer to TLS extension in socket structure
 * @param tls_extension pointer to the TLS information provided by application
 * @param option_length length of the TLS extension buffer passed by the application
 * @return sl_status_t possible return values are SL_STATUS_OK, SL_STATUS_NULL_POINTER,
 *         SL_STATUS_INVALID_PARAMETER, and SL_STATUS_SI91X_MEMORY_ERROR
 */
sl_status_t sli_si91x_add_tls_extension(sli_si91x_tls_extensions_t *socket_tls_extensions,
                                        const sl_si91x_socket_type_length_value_t *tls_extension,
                                        socklen_t option_length)
{
  const size_t tls_extension_header_size = sizeof(sl_si91x_socket_type_length_value_t);

  if (socket_tls_extensions == NULL || tls_extension == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  if (option_length < (socklen_t)tls_extension_header_size) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (tls_extension->length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if ((tls_extension->type != SL_SI91X_TLS_EXTENSION_SNI_TYPE)
      && (tls_extension->type != SL_SI91X_TLS_EXTENSION_ALPN_TYPE)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (option_length != (socklen_t)(tls_extension_header_size + tls_extension->length)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // To check if memory available for new extension in buffer of socket, max 256 Bytes only
  if (SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA - socket_tls_extensions->current_size_of_extensions
      < (int)(tls_extension_header_size + tls_extension->length)) {
    return SL_STATUS_SI91X_MEMORY_ERROR;
  }

  const uint16_t extension_size = (uint16_t)(tls_extension_header_size + tls_extension->length);

  // copies TLS extension provided by app into SDK socket struct
  memcpy(&socket_tls_extensions->buffer[socket_tls_extensions->current_size_of_extensions],
         tls_extension,
         extension_size);
  socket_tls_extensions->current_size_of_extensions += extension_size;
  socket_tls_extensions->total_extensions++;

  return SL_STATUS_OK;
}

int sli_si91x_configure_tls_extension(sli_si91x_tls_extensions_t *socket_tls_extensions,
                                      const sl_si91x_socket_type_length_value_t *tls_extension,
                                      socklen_t option_length)
{
  const sl_status_t status = sli_si91x_add_tls_extension(socket_tls_extensions, tls_extension, option_length);

  if (status == SL_STATUS_SI91X_MEMORY_ERROR) {
    SLI_SET_ERROR_AND_RETURN(ENOMEM);
  }
  if (status != SL_STATUS_OK) {
    SLI_SET_ERROR_AND_RETURN(EINVAL);
  }

  return SLI_SI91X_NO_ERROR;
}

int32_t sli_get_socket_command_from_host_packet(sl_wifi_buffer_t *buffer)
{
  const sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)buffer->data;
  return (packet == NULL ? -1 : packet->command);
}

void sli_si91x_create_socket_request(sli_si91x_socket_t *si91x_bsd_socket,
                                     sli_si91x_socket_create_request_t *socket_create_request,
                                     int type,
                                     const int *backlog,
                                     sli_wifi_wait_period_t *wait_period)
{
  if (si91x_bsd_socket->local_address.sin6_family == AF_INET6) {
    socket_create_request->ip_version = SL_IPV6_VERSION;
#ifdef SLI_SI91X_NETWORK_DUAL_STACK
    memcpy(socket_create_request->dest_ip_addr.ipv6_address,
           si91x_bsd_socket->remote_address.sin6_addr.un.u8_addr,
           SL_IPV6_ADDRESS_LENGTH);
#else
#ifndef __ZEPHYR__
    memcpy(socket_create_request->dest_ip_addr.ipv6_address,
           si91x_bsd_socket->remote_address.sin6_addr.__u6_addr.__u6_addr8,
           SL_IPV6_ADDRESS_LENGTH);
#else
    memcpy(socket_create_request->dest_ip_addr.ipv6_address,
           si91x_bsd_socket->remote_address.sin6_addr.s6_addr,
           SL_IPV6_ADDRESS_LENGTH);
#endif
#endif
  } else {
    socket_create_request->ip_version = SL_IPV4_ADDRESS_LENGTH;
    memcpy(socket_create_request->dest_ip_addr.ipv4_address,
           &((struct sockaddr_in *)&si91x_bsd_socket->remote_address)->sin_addr,
           SL_IPV4_ADDRESS_LENGTH);
  }

  socket_create_request->local_port  = si91x_bsd_socket->local_address.sin6_port;
  socket_create_request->remote_port = si91x_bsd_socket->remote_address.sin6_port;

  // Fill socket type
  socket_create_request->socket_type = (uint16_t)type;

  if (type == SLI_SI91X_SOCKET_TCP_SERVER) {
    socket_create_request->max_count = (backlog == NULL) ? 0 : (uint16_t)*backlog;
    socket_create_request->socket_bitmap |= SLI_SI91X_SOCKET_FEAT_LTCP_ACCEPT;
    si91x_bsd_socket->socket_events = osEventFlagsNew(NULL);
  } else {
    socket_create_request->max_count = 0;
  }

  if (si91x_bsd_socket->recv_data_callback == NULL) {
    socket_create_request->socket_bitmap |= SLI_SI91X_SOCKET_FEAT_SYNCHRONOUS;
  }

  socket_create_request->socket_bitmap |= SLI_SI91X_SOCKET_FEAT_TCP_RX_WINDOW;

  // Set the RX window size
  socket_create_request->rx_window_size = SLI_TCP_RX_WINDOW_SIZE;

  // Fill VAP_ID
  socket_create_request->vap_id                = si91x_bsd_socket->vap_id;
  socket_create_request->tos                   = 0;
  socket_create_request->max_tcp_retries_count = si91x_bsd_socket->max_tcp_retries ? si91x_bsd_socket->max_tcp_retries
                                                                                   : SLI_MAX_TCP_RETRY_COUNT;
  socket_create_request->tcp_keepalive_initial_time = si91x_bsd_socket->tcp_keepalive_initial_time
                                                        ? si91x_bsd_socket->tcp_keepalive_initial_time
                                                        : SLI_DEFAULT_TCP_KEEP_ALIVE_TIME;
  socket_create_request->tcp_mss                    = si91x_bsd_socket->mss;

  // Check for SSL feature and fill it in SSL bitmap
  if ((si91x_bsd_socket->ssl_bitmap & SL_SI91X_ENABLE_TLS) || (si91x_bsd_socket->ssl_bitmap & SL_SI91X_ENABLE_DTLS)) {
    socket_create_request->ssl_bitmap         = si91x_bsd_socket->ssl_bitmap;
    socket_create_request->ssl_ciphers_bitmap = sl_si91x_socket_selected_ciphers;
#if defined(SLI_SI917)
    socket_create_request->ssl_ext_ciphers_bitmap = sl_si91x_socket_selected_extended_ciphers;
#endif
    // Check if cert index is not default index
    if (si91x_bsd_socket->certificate_index > SLI_SI91X_CERT_INDEX_0) {
      socket_create_request->socket_bitmap |= SLI_SI91X_SOCKET_FEAT_CERT_INDEX;
    }

    socket_create_request->socket_cert_inx = si91x_bsd_socket->certificate_index;

    // Check if extension is provided my application and memcopy until the provided size of extensions
    if (si91x_bsd_socket->tls_extensions.total_extensions > 0) {
      memcpy(socket_create_request->tls_extension_data,
             si91x_bsd_socket->tls_extensions.buffer,
             si91x_bsd_socket->tls_extensions.current_size_of_extensions);

      socket_create_request->total_extension_length = si91x_bsd_socket->tls_extensions.current_size_of_extensions;
      socket_create_request->no_of_tls_extensions   = si91x_bsd_socket->tls_extensions.total_extensions;
    }
    *wait_period = SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_SOCKET_CREATE_WAIT_TIME);
  }

  // Check for HIGH_PERFORMANCE feature bit
  if (si91x_bsd_socket->ssl_bitmap & SLI_SI91X_HIGH_PERFORMANCE_SOCKET) {
    socket_create_request->ssl_bitmap |= SLI_SI91X_HIGH_PERFORMANCE_SOCKET;
  }

  // Check for Websocket feature bit
  if (si91x_bsd_socket->ssl_bitmap & SLI_SI91X_WEBSOCKET_FEAT) {
    sli_si91x_handle_websocket(socket_create_request, si91x_bsd_socket);
  }

  // Check for TCP ACK INDICATION feature bit
  if (si91x_bsd_socket->socket_bitmap & SLI_SI91X_SOCKET_FEAT_TCP_ACK_INDICATION) {
    socket_create_request->socket_bitmap |= SLI_SI91X_SOCKET_FEAT_TCP_ACK_INDICATION;
  }

  // Check for per-socket graceful close feature bit
  if (si91x_bsd_socket->socket_ext_bitmap & SLI_SI91X_SOCKET_FEAT_PER_SOCKET_CLOSE) {
    socket_create_request->socket_ext_bitmap |= SLI_SI91X_SOCKET_FEAT_PER_SOCKET_CLOSE;
  }

#if defined(SLI_SI917)
  // Set socket's max retransmission timeout value and TOS (Type of Service) if applicable
  socket_create_request->max_retransmission_timeout_value = (uint8_t)si91x_bsd_socket->max_retransmission_timeout_value;
  socket_create_request->tos                              = (uint16_t)si91x_bsd_socket->tos;
#endif

  // Add domain_name if present
  if (si91x_bsd_socket->domain_name != NULL) {
    size_t domain_name_len =
      sl_strnlen((char *)si91x_bsd_socket->domain_name, sizeof(socket_create_request->domain_name) - 1);
    memcpy(socket_create_request->domain_name, si91x_bsd_socket->domain_name, domain_name_len);
    socket_create_request->domain_name[domain_name_len] = '\0';
  } else {
    socket_create_request->domain_name[0] = '\0';
  }
}

// Prepare socket request based on socket type and send the request down to the driver.
// socket type : [SL_SOCKET_TCP_SERVER, SL_SOCKET_TCP_CLIENT, SL_SOCKET_LUDP, SL_SOCKET_UDP_CLIENT]
sl_status_t sli_create_and_send_socket_request(int socketIdIndex, int type, const int *backlog)
{
  sl_status_t status                                               = SL_STATUS_OK;
  sli_si91x_socket_create_request_t socket_create_request          = { 0 };
  const sli_si91x_socket_create_response_t *socket_create_response = NULL;
  sli_si91x_socket_t *si91x_bsd_socket                             = sli_get_si91x_socket(socketIdIndex);
  sli_wifi_wait_period_t wait_period = SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_SOCKET_CREATE_WAIT_TIME);
  //Verifying socket existence
  if (si91x_bsd_socket == NULL) {
    return -1;
  }

  sl_ip_address_type_t ip_type = (si91x_bsd_socket->local_address.sin6_family == AF_INET6) ? SL_IPV6 : SL_IPV4;
  status                       = sli_net_get_vap_for_ip_version(si91x_bsd_socket->vap_id, ip_type);
  if (status != SL_STATUS_OK) {
    errno = EAFNOSUPPORT;
    return -1;
  }

  if (type == SLI_SI91X_SOCKET_TCP_CLIENT) {
    wait_period = SLI_WIFI_WAIT_FOR_RESPONSE(100000); // timeout is 10 sec
  }

  sl_wifi_buffer_t *buffer        = NULL;
  sl_wifi_system_packet_t *packet = NULL;

  sli_si91x_create_socket_request(si91x_bsd_socket, &socket_create_request, type, backlog, &wait_period);

  // Store socket role for future references.
  si91x_bsd_socket->role = type;
  status                 = sli_wifi_send_command(SLI_WIFI_REQ_SOCKET_CREATE,
                                 SLI_SI91X_SOCKET_CMD,
                                 &socket_create_request,
                                 sizeof(socket_create_request),
                                 wait_period,
                                 NULL,
                                 (void **)&buffer);

  // If the status is not OK and there's a buffer, free the buffer
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  // Extract socket creation response information
  packet                 = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  socket_create_response = (sli_si91x_socket_create_response_t *)packet->data;

  si91x_bsd_socket->id = (int32_t)(socket_create_response->socket_id[0] | (socket_create_response->socket_id[1] << 8));
  si91x_bsd_socket->local_address.sin6_port =
    (uint16_t)(socket_create_response->module_port[0] | (socket_create_response->module_port[1] << 8));

  if (type != SLI_SI91X_SOCKET_TCP_SERVER) {
    si91x_bsd_socket->remote_address.sin6_port =
      (uint16_t)(socket_create_response->dst_port[0] | socket_create_response->dst_port[1] << 8);
  }

  si91x_bsd_socket->mss = (uint16_t)((socket_create_response->mss[0]) | (socket_create_response->mss[1] << 8));

  // If socket is already bound to an local address and port, there is no need to copy it again.
  if (si91x_bsd_socket->state == BOUND) {
    sli_buffer_manager_free_buffer(buffer);
    return SL_STATUS_OK;
  }

  // Copy the local address (IPv4 or IPv6) based on family type
  if (si91x_bsd_socket->local_address.sin6_family == AF_INET) {
    memcpy(&((struct sockaddr_in *)&si91x_bsd_socket->local_address)->sin_addr.s_addr,
           socket_create_response->module_ip_addr.ipv4_addr,
           SL_IPV4_ADDRESS_LENGTH);
  } else {
#ifdef SLI_SI91X_NETWORK_DUAL_STACK
    memcpy(si91x_bsd_socket->local_address.sin6_addr.un.u8_addr,
           socket_create_response->module_ip_addr.ipv6_addr,
           SL_IPV6_ADDRESS_LENGTH);
#else
#ifndef __ZEPHYR__
    memcpy(si91x_bsd_socket->local_address.sin6_addr.__u6_addr.__u6_addr8,
           socket_create_response->module_ip_addr.ipv6_addr,
           SL_IPV6_ADDRESS_LENGTH);
#else
    memcpy(si91x_bsd_socket->local_address.sin6_addr.s6_addr,
           socket_create_response->module_ip_addr.ipv6_addr,
           SL_IPV6_ADDRESS_LENGTH);
#endif
#endif
  }

  // Free the buffer
  sli_buffer_manager_free_buffer(buffer);

  return SL_STATUS_OK;
}

int sli_si91x_socket(int family, int type, int protocol, sl_si91x_socket_receive_data_callback_t callback)
{
  // Validate the socket parameters
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(family != AF_INET && family != AF_INET6, EAFNOSUPPORT);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(type != SOCK_STREAM && type != SOCK_DGRAM, EINVAL);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(protocol != IPPROTO_TCP && protocol != IPPROTO_UDP && protocol != 0, EINVAL);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE((type == SOCK_STREAM && (protocol != IPPROTO_TCP && protocol != 0)), EPROTOTYPE);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE((type == SOCK_DGRAM && (protocol != IPPROTO_UDP && protocol != 0)), EPROTOTYPE);

  // Initialize a new socket structure
  sli_si91x_socket_t *si91x_socket;
  int socket_index = -1;

  sli_get_free_socket(&si91x_socket, &socket_index);

  // Check if there is enough memory to create the socket
  if (socket_index < 0) {
    SLI_SET_ERROR_AND_RETURN(ENOMEM);
  }

  // Populate the socket structure with provided parameters and callbacks
  si91x_socket->type                      = type;
  si91x_socket->local_address.sin6_family = (uint8_t)family;
  si91x_socket->protocol                  = protocol;
  si91x_socket->state                     = INITIALIZED;
  si91x_socket->recv_data_callback        = callback;
  si91x_socket->client_id                 = -1;

  // Return the socket index
  return socket_index;
}

int sli_si91x_accept(int socket, struct sockaddr *addr, socklen_t *addr_len, sl_si91x_socket_accept_callback_t callback)
{
  sl_status_t status                               = SL_STATUS_OK;
  sli_si91x_socket_t *si91x_client_socket          = NULL;
  sli_si91x_socket_t *si91x_server_socket          = sli_get_si91x_socket(socket);
  sli_si91x_socket_accept_request_t accept_request = { 0 };
  int32_t client_socket_id                         = -1;
  sl_wifi_buffer_t *buffer                         = NULL;

  // Check if the server socket is valid
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_server_socket == NULL, EBADF);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_server_socket->type != SOCK_STREAM, EOPNOTSUPP);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_server_socket->state != LISTEN, EINVAL);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_server_socket->client_id != -1, EINVAL);

  // Create a new instance for socket
  client_socket_id = sli_si91x_socket(si91x_server_socket->local_address.sin6_family,
                                      si91x_server_socket->type,
                                      si91x_server_socket->protocol,
                                      si91x_server_socket->recv_data_callback);

  si91x_client_socket = sli_get_si91x_socket(client_socket_id);
  //Verifying socket existence
  if (si91x_client_socket == NULL)
    return -1;
  memcpy(&si91x_client_socket->local_address, &si91x_server_socket->local_address, sizeof(struct sockaddr_in6));

  // Fill VAP_ID
  si91x_client_socket->vap_id = si91x_server_socket->vap_id;

  // Fill per socket close feature bit
  if (si91x_server_socket->socket_ext_bitmap & SLI_SI91X_SOCKET_FEAT_PER_SOCKET_CLOSE) {
    si91x_client_socket->socket_ext_bitmap |= SLI_SI91X_SOCKET_FEAT_PER_SOCKET_CLOSE;
  }

  // Create accept request
  accept_request.socket_id   = (uint8_t)si91x_server_socket->id;
  accept_request.source_port = si91x_server_socket->local_address.sin6_port;

  // Set the callback and client socket ID.
  sli_si91x_set_accept_callback(si91x_server_socket, callback, client_socket_id);
  if (callback != NULL) {
    status = sli_wifi_send_command(SLI_WIFI_REQ_SOCKET_ACCEPT,
                                   (client_socket_id + SI91X_CMD_MAX),
                                   &accept_request,
                                   sizeof(accept_request),
                                   SLI_WIFI_RETURN_IMMEDIATELY,
                                   NULL,
                                   NULL);
    SLI_SOCKET_VERIFY_STATUS_AND_RETURN(status, SL_STATUS_IN_PROGRESS, SLI_SI91X_UNDEFINED_ERROR);
    return SL_STATUS_OK;
  } else {
    status = sli_wifi_send_command(SLI_WIFI_REQ_SOCKET_ACCEPT,
                                   (client_socket_id + SI91X_CMD_MAX),
                                   &accept_request,
                                   sizeof(accept_request),
                                   (SLI_WIFI_WAIT_FOR_EVER | SLI_WIFI_WAIT_FOR_RESPONSE_BIT),
                                   NULL,
                                   (void **)&buffer);
    SLI_SOCKET_VERIFY_STATUS_AND_RETURN(status, SL_STATUS_OK, SLI_SI91X_UNDEFINED_ERROR);
  }

  // If the accept request fails, clean up allocated memory and return an error
  if (status != SL_STATUS_OK) {
    sli_si91x_shutdown(client_socket_id, SHUTDOWN_BY_ID);
    if (buffer != NULL) {
      sli_buffer_manager_free_buffer(buffer);
    }
    SLI_SET_ERROR_AND_RETURN(SLI_SI91X_UNDEFINED_ERROR);
  }

  sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  if (packet == NULL) {
    SLI_SET_ERROR_AND_RETURN(SLI_SI91X_UNDEFINED_ERROR);
  }
  const sli_si91x_rsp_ltcp_est_t *ltcp = (sli_si91x_rsp_ltcp_est_t *)packet->data;

  sli_handle_accept_response(si91x_client_socket, ltcp);

  // If addr_len is NULL or invalid value, just return the client socket ID
  if (addr != NULL && *addr_len > 0) {
    // Copy the remote address to the provided sockaddr structure
    memcpy(addr,
           &si91x_client_socket->remote_address,
           (*addr_len > sizeof(struct sockaddr_in6)) ? sizeof(struct sockaddr_in6) : *addr_len);

    // Update addr_len based on the family of the local address
    *addr_len = si91x_client_socket->local_address.sin6_family == AF_INET ? sizeof(struct sockaddr_in)
                                                                          : sizeof(struct sockaddr_in6);
  }
  // Free resources and return the client socket ID
  sli_buffer_manager_free_buffer(buffer);

  return client_socket_id;
}

// Shutdown a socket
int sli_si91x_shutdown(int socket, int how)
{
  sl_status_t status                                            = SL_STATUS_OK;
  sli_si91x_socket_close_request_t socket_close_request         = { 0 };
  const sl_si91x_socket_close_response_t *socket_close_response = NULL;
  sli_wifi_wait_period_t wait_period                            = SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_WAIT_FOR_EVER);
  sl_wifi_buffer_t *response_buffer                             = NULL;

  const sli_si91x_socket_t *si91x_socket = sli_get_si91x_socket(socket);

  // Verify the socket's existence
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket == NULL, EBADF);

  // SHUTDOWN_BY_PORT is not supported for UDP sockets.
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket->type == SOCK_DGRAM && how == SHUTDOWN_BY_PORT, EINVAL);

  // SHUTDOWN_BY_PORT is valid only for listening(server) sockets.
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket->state != LISTEN && how == SHUTDOWN_BY_PORT, EINVAL);

  //The firmware uses socket_id to close client sockets and port_id to close server sockets.
  //Closing a client socket:
  //Set how to SHUTDOWN_BY_ID. In the request structure, specify the target socket by setting socket_id to the desired value and set the port field to 0.
  //Closing a server socket:
  //Set how to SHUTDOWN_BY_PORT. In this case, set socket_id to 0 and provide the server's source port number in the port field.
  int close_request_type = (si91x_socket->state == LISTEN) ? SHUTDOWN_BY_PORT : how;

  // Check the state of the socket and perform cleanup if necessary
  if (si91x_socket->state == BOUND            // Socket is in a bound state
      || si91x_socket->state == INITIALIZED   // Socket is in an initialized state
      || (si91x_socket->state == DISCONNECTED // Socket is disconnected
          && si91x_socket->disconnect_reason
               == SLI_SI91X_BSD_DISCONNECT_REASON_INTERFACE_DOWN) // Disconnection due to interface down
      || (si91x_socket->state == DISCONNECTED                     // Socket is disconnected
          && si91x_socket->disconnect_reason
               == SLI_SI91X_BSD_DISCONNECT_REASON_REMOTE_CLOSED // Disconnection due to remote side terminate
          && sli_is_tcp_auto_close_enabled()                    // Check if TCP auto-close is enabled
          && !sli_is_per_socket_close_enabled(si91x_socket))) { // Per-socket close forces explicit close path
    // Free the resources associated with this socket
    sli_si91x_free_socket(socket);

    // Return success as the cleanup is successfully performed
    return SLI_SI91X_NO_ERROR;
  }

  /* Wait until the socket has sent all outstanding data packets in its queue.
     Polls si91x_socket->data_buffer_count and sleeps per iteration until no
     transmit buffers remain. */
  /* ponytail: unbounded drain — a wedged TX pipe used to park the closer here
     at 500 wake/s (2ms sleeps). 10ms keeps the drain responsive but caps the
     wake storm at 100/s. */
  while (si91x_socket->data_buffer_count) {
    osDelay(SLI_SYSTEM_MS_TO_TICKS(10));
  }

  /*If socket is server socket, SHUTDOWN_BY_PORT is to be used irrespective of 'how' parameter.*/
  socket_close_request.socket_id   = (uint16_t)((close_request_type == SHUTDOWN_BY_ID) ? si91x_socket->id : 0);
  socket_close_request.port_number = (close_request_type == SHUTDOWN_BY_ID) ? 0 : si91x_socket->local_address.sin6_port;

  status = sli_wifi_send_command(SLI_WIFI_REQ_SOCKET_CLOSE,
                                 (si91x_socket->index + SI91X_CMD_MAX),
                                 &socket_close_request,
                                 sizeof(socket_close_request),
                                 wait_period,
                                 NULL,
                                 (void **)&response_buffer);

  // Treat SOCKET_CLOSED and COMMAND_GIVEN_IN_INVALID_STATE (0x21, returned by the NWP after a
  // rejoin failure already tore the socket down) as logical close success: free the host slot.
  if (status == SL_STATUS_SI91X_SOCKET_CLOSED || status == SL_STATUS_SI91X_COMMAND_GIVEN_IN_INVALID_STATE) {
    if (close_request_type == SHUTDOWN_BY_ID) {
      sli_si91x_free_socket(socket);
    } else {
      sli_free_sockets_by_port(socket_close_request.port_number);
    }
    sli_buffer_manager_free_buffer(response_buffer);
    return SLI_SI91X_NO_ERROR;
  }

  // If the status is not OK and there's a buffer, free the buffer
  if ((status != SL_STATUS_OK) && (response_buffer != NULL)) {
    sli_buffer_manager_free_buffer(response_buffer);
  }
  SLI_SOCKET_VERIFY_STATUS_AND_RETURN(status, SL_STATUS_OK, SLI_SI91X_UNDEFINED_ERROR);

  sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(response_buffer, 0, NULL);
  socket_close_response           = (sl_si91x_socket_close_response_t *)packet->data;

  if (close_request_type == SHUTDOWN_BY_ID && si91x_socket->id == socket_close_response->socket_id) {
    sli_si91x_free_socket(socket);
    sli_buffer_manager_free_buffer(response_buffer);
    return SLI_SI91X_NO_ERROR;
  }

  // Handle shutdown by port: free all sockets matching the port
  if (close_request_type == SHUTDOWN_BY_PORT) {
    sli_free_sockets_by_port(socket_close_response->port_number);
  }

  sli_buffer_manager_free_buffer(response_buffer);
  return SLI_SI91X_NO_ERROR;
}

/**
 * @brief Helper: Handle socket accept request response
 */
static sl_status_t sli_handle_socket_accept_request(sl_wifi_system_packet_t *rx_packet)
{
  const sli_si91x_rsp_ltcp_est_t *accept_response = (sli_si91x_rsp_ltcp_est_t *)rx_packet->data;
  sli_si91x_socket_t *server_socket               = sli_si91x_get_socket_from_port(accept_response->src_port_num);

  if (server_socket == NULL) {
    return SL_STATUS_FAIL;
  }

  int32_t client_socket_id          = server_socket->client_id;
  server_socket->client_id          = -1;
  sli_si91x_socket_t *client_socket = sli_get_si91x_socket(client_socket_id);

  sli_handle_accept_response(client_socket, accept_response);

  if (server_socket->user_accept_callback != NULL) {
    server_socket->user_accept_callback(client_socket_id,
                                        (struct sockaddr *)&server_socket->remote_address,
                                        (uint8_t)server_socket->type);
  }

  return SL_STATUS_OK;
}

/**
 * @brief Helper: Handle remote socket termination
 */
static void sli_handle_remote_terminate(sl_wifi_system_packet_t *rx_packet)
{
  const sl_si91x_socket_close_response_t *remote_socket_closure = (sl_si91x_socket_close_response_t *)rx_packet->data;

  // Reset sockets that match the close request
  for (uint8_t index = 0; index < SLI_NUMBER_OF_SOCKETS; index++) {
    sli_si91x_socket_t *socket = sli_get_si91x_socket(index);

    if (socket == NULL || remote_socket_closure->socket_id != socket->id || socket->state == LISTEN) {
      continue;
    }

    socket->state             = DISCONNECTED;
    socket->disconnect_reason = SLI_SI91X_BSD_DISCONNECT_REASON_REMOTE_CLOSED;

    if (user_remote_socket_termination_callback != NULL) {
      user_remote_socket_termination_callback(socket->id,
                                              socket->local_address.sin6_port,
                                              remote_socket_closure->sent_bytes_count);
    }
    break;
  }
}

/**
 * @brief Helper: Find host socket index by firmware socket ID with break optimization
 */
static int sli_find_host_socket_by_firmware_id(int firmware_socket_id)
{
  for (uint8_t host_socket_index = 0; host_socket_index < SLI_NUMBER_OF_SOCKETS; host_socket_index++) {
    if ((sli_si91x_sockets[host_socket_index] != NULL)
        && (firmware_socket_id == sli_si91x_sockets[host_socket_index]->id)) {
      return host_socket_index;
    }
  }
  return -1; // Not found
}

/**
 * @brief Helper: Handle data read request response
 */
static sl_status_t sli_handle_data_read_request(sl_status_t frame_status,
                                                const sl_wifi_system_packet_t *rx_packet,
                                                const sli_si91x_socket_context_t *sdk_context)
{
  UNUSED_PARAMETER(sdk_context);
  sl_status_t status        = SL_STATUS_OK;
  int8_t firmware_socket_id = -1;

  if (frame_status != SL_STATUS_OK) {
    firmware_socket_id                      = rx_packet->data[0];
    const sli_si91x_socket_t *client_socket = sli_si91x_get_socket_from_id(firmware_socket_id, LISTEN, -1);
    //Verifying socket existence
    if (client_socket == NULL) {
      return SL_STATUS_NOT_FOUND;
    }

    sli_command_engine_packet_type_configuration_t packet_type_info = { 0 };
    // Get packet type configuration for the given command type
    status = sli_command_engine_get_rx_queue_info_from_packet_type(&sli_wifi_command_engine,
                                                                   (uint16_t)(client_socket->index + SI91X_CMD_MAX),
                                                                   &packet_type_info);
    VERIFY_STATUS_AND_RETURN(status);

    sli_command_engine_metadata_t *metadata = NULL;
    // Allocate metadata buffer (hybrid allocation allows pool + heap fallback)
    status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                1000,
                                                (sli_buffer_t *)&metadata);
    VERIFY_STATUS_AND_RETURN(status);
    metadata->packet_status              = (uint16_t)frame_status;
    metadata->tx_info.data_packet        = NULL;
    metadata->tx_info.data_packet_length = 0;
    metadata->tx_info.flags              = 0;
    metadata->tx_info.packet_id          = 0;

    sli_queue_manager_enqueue(packet_type_info.sync_response_queue, metadata);
    osEventFlagsSet(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
  }

  return SL_STATUS_OK;
}

/**
 * @brief Helper: Validate select response parameters
 */
static bool sli_is_valid_select_response(const sli_si91x_socket_select_rsp_t *response)
{
  if (response == NULL) {
    return false;
  }

  return (response->select_id < sli_si91x_max_select_count && select_request_table != NULL
          && select_request_table[response->select_id].in_use);
}

/**
 * @brief Helper: Allocate file descriptor sets for select operation
 */
static sl_status_t sli_allocate_fd_sets(void **read_fd, void **write_fd, void **exception_fd)
{
  // Initialize pointers to NULL for safe cleanup
  *read_fd      = NULL;
  *write_fd     = NULL;
  *exception_fd = NULL;

#ifndef __ZEPHYR__
  *read_fd      = SLI_MALLOC(sizeof(fd_set));
  *write_fd     = SLI_MALLOC(sizeof(fd_set));
  *exception_fd = SLI_MALLOC(sizeof(fd_set));
#else
  *read_fd = SLI_MALLOC(sizeof(sl_si91x_fdset_t));
  *write_fd = SLI_MALLOC(sizeof(sl_si91x_fdset_t));
  *exception_fd = SLI_MALLOC(sizeof(sl_si91x_fdset_t));
#endif

  // Check if any allocation failed
  if (*read_fd == NULL || *write_fd == NULL || *exception_fd == NULL) {
    SL_DEBUG_LOG_V2(ERROR, "Select: fd_set malloc failed\r\n");
    return SL_STATUS_ALLOCATION_FAILED;
  }

  return SL_STATUS_OK;
}

/**
 * @brief Helper:Cleanup allocated file descriptor sets safely
 */
static void sli_cleanup_fd_sets(void *read_fd, void *write_fd, void *exception_fd)
{
  if (read_fd != NULL) {
    SLI_FREE(read_fd);
  }
  if (write_fd != NULL) {
    SLI_FREE(write_fd);
  }
  if (exception_fd != NULL) {
    SLI_FREE(exception_fd);
  }
}

/**
 * @brief Helper: Handle select request response
 */
static sl_status_t sli_handle_select_request(sl_wifi_system_packet_t *rx_packet,
                                             sli_si91x_socket_context_t *sdk_context)
{
  sl_wifi_buffer_t *response_buffer       = NULL;
  sli_command_engine_metadata_t *metadata = NULL;
  uint16_t data_len                       = 0;
  uint32_t packet_size                    = 0;

  const sli_si91x_socket_select_rsp_t *socket_select_rsp = (sli_si91x_socket_select_rsp_t *)rx_packet->data;

  if (!sli_is_valid_select_response(socket_select_rsp)) {
    SL_DEBUG_LOG_V2(DEBUG, "\r\n INVALID SELECT ID\r\n");
    return SL_STATUS_OK;
  }

  sli_si91x_select_request_t *select_request = &select_request_table[socket_select_rsp->select_id];
  select_request->frame_status               = (uint16_t)(rx_packet->desc[12] + (rx_packet->desc[13] << 8));

  if (select_request->select_callback != NULL) {
    void *read_fd      = NULL;
    void *write_fd     = NULL;
    void *exception_fd = NULL;

    sl_status_t alloc_status = sli_allocate_fd_sets(&read_fd, &write_fd, &exception_fd);
    if (alloc_status != SL_STATUS_OK) {
      sli_cleanup_fd_sets(read_fd, write_fd, exception_fd);
      SL_DEBUG_LOG_V2(ERROR, "\r\n HEAP EXHAUSTED DURING ALLOCATION \r\n");
      sli_si91x_clear_select_id(select_request->select_id);
      SL_CLEANUP_MALLOC(sdk_context);
      return SL_STATUS_ALLOCATION_FAILED;
    }

#ifndef __ZEPHYR__
    memset(read_fd, 0, sizeof(fd_set));
    memset(write_fd, 0, sizeof(fd_set));
    memset(exception_fd, 0, sizeof(fd_set));
#else
    memset(read_fd, 0, sizeof(sl_si91x_fdset_t));
    memset(write_fd, 0, sizeof(sl_si91x_fdset_t));
    memset(exception_fd, 0, sizeof(sl_si91x_fdset_t));
#endif

    // This function handles responses received from the SI91X socket driver
    sli_handle_select_response(socket_select_rsp, read_fd, write_fd, exception_fd);

    // Call the user-defined select callback function with the updated file descriptor sets and status
    select_request->select_callback(read_fd, write_fd, exception_fd, select_request->frame_status);

    sli_si91x_clear_select_id(select_request->select_id);
    sli_cleanup_fd_sets(read_fd, write_fd, exception_fd);
  } else {
    sl_status_t status = SL_STATUS_OK;
    data_len           = rx_packet->length & 0xFFF;
    packet_size        = sizeof(rx_packet->desc) + data_len;

    status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CP_DATA_RX_POOL,
                                                SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                1000,
                                                (sli_buffer_t *)&response_buffer);

    if (status != SL_STATUS_OK || response_buffer == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "\r\n HEAP EXHAUSTED DURING ALLOCATION \r\n");
      return SL_STATUS_ALLOCATION_FAILED;
    }

    response_buffer->length = packet_size;
    memcpy(response_buffer->data, rx_packet, packet_size);

    status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                1000,
                                                (sli_buffer_t *)&metadata);
    if (status != SL_STATUS_OK || metadata == NULL) {
      sli_buffer_manager_free_buffer((sli_buffer_t)response_buffer);
      SL_DEBUG_LOG_V2(ERROR, "\r\n HEAP EXHAUSTED DURING METADATA ALLOCATION \r\n");
      return SL_STATUS_ALLOCATION_FAILED;
    }

    metadata->packet_status              = select_request->frame_status;
    metadata->tx_info.data_packet        = (void *)response_buffer;
    metadata->tx_info.data_packet_length = (uint16_t)packet_size;
    metadata->tx_info.flags              = 0;
    metadata->tx_info.packet_id          = socket_select_rsp->select_id;

    sli_command_engine_packet_type_configuration_t packet_type_info = { 0 };
    status = sli_command_engine_get_rx_queue_info_from_packet_type(&sli_wifi_command_engine,
                                                                   SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET,
                                                                   &packet_type_info);

    if (status != SL_STATUS_OK) {
      sli_buffer_manager_free_buffer((sli_buffer_t)response_buffer);
      sli_buffer_manager_free_buffer((sli_buffer_t)metadata);
      return status;
    }

    status = sli_queue_manager_enqueue(packet_type_info.sync_response_queue, (void *)metadata);

    if (status != SL_STATUS_OK) {
      sli_buffer_manager_free_buffer((sli_buffer_t)response_buffer);
      sli_buffer_manager_free_buffer((sli_buffer_t)metadata);
      return status;
    }

    osEventFlagsSet(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
  }

  return SL_STATUS_OK;
}

/**
 * @brief Helper: Handle TCP acknowledgment indication
 */
static int sli_handle_tcp_ack_indication(sl_wifi_system_packet_t *rx_packet, sli_si91x_socket_context_t *sdk_context)
{
  const sli_si91x_rsp_tcp_ack_t *tcp_ack = (sli_si91x_rsp_tcp_ack_t *)rx_packet->data;

  // Find the host socket associated with the TCP acknowledgment
  int32_t host_socket = sli_find_host_socket_by_firmware_id(tcp_ack->socket_id);

  // Retrieve the SI91X socket associated with the host socket
  sli_si91x_socket_t *si91x_socket = sli_get_si91x_socket(host_socket);

  if (si91x_socket == NULL) {
    SL_DEBUG_LOG_V2(DEBUG, "TCP ACK: host socket not found (fw id %u)\r\n", (unsigned int)tcp_ack->socket_id);
    SL_CLEANUP_MALLOC(sdk_context);
    return -1;
  }

  // Check if the SLI_SI91X_SOCKET_FEAT_TCP_ACK_INDICATION bit is set move the socket to CONNECTED state.
  if (si91x_socket->socket_bitmap & SLI_SI91X_SOCKET_FEAT_TCP_ACK_INDICATION) {
    si91x_socket->is_waiting_on_ack = false;
  }

  // Check if the SI91X socket and its data transfer callback function exist
  if (si91x_socket->data_transfer_callback != NULL) {
    si91x_socket->data_transfer_callback(host_socket, (uint8_t)(tcp_ack->length[0] | tcp_ack->length[1] << 8));
  }

  return 0;
}

sl_status_t sli_si91x_socket_event_handler(sl_status_t frame_status,
                                           sli_si91x_socket_context_t *sdk_context,
                                           sl_wifi_system_packet_t *rx_packet)
{
  SL_VERIFY_POINTER_OR_RETURN(rx_packet, SL_STATUS_NULL_POINTER);
  int result = 0;

  switch (rx_packet->command) {
    // Handle data read request response
    case SLI_WIFI_REQ_SOCKET_READ_DATA: {
      result = sli_handle_data_read_request(frame_status, (const sl_wifi_system_packet_t *)rx_packet, sdk_context);
      if (result != SL_STATUS_OK) {
        return -1;
      }
      break;
    }
    // Handle connection establishment response
    case SLI_WIFI_REQ_SOCKET_ACCEPT: {
      result = sli_handle_socket_accept_request(rx_packet);
      if (result != SL_STATUS_OK) {
        return -1;
      }
      break;
    }
    // Handle remote socket termination response
    case SLI_WIFI_RSP_REMOTE_TERMINATE: {
      sli_handle_remote_terminate(rx_packet);
      break;
    }
    // Handle select request response
    case SLI_WIFI_RSP_SELECT_REQUEST: {
      // Handle select request response
      result = sli_handle_select_request(rx_packet, sdk_context);
      if (result != SL_STATUS_OK) {
        return result;
      }
      break;
    }
    // This block of code is executed when a TCP acknowledgment indication is received.
    case SLI_WIFI_RSP_TCP_ACK_INDICATION: {
      result = sli_handle_tcp_ack_indication(rx_packet, sdk_context);
      if (result == -1) {
        return -1;
      }
      break;
    }

    default:
      break;
  }
  // Cleanup any dynamically allocated memory in the SDK context
  SL_CLEANUP_MALLOC(sdk_context);

  return SL_STATUS_OK;
}

sl_status_t sli_si91x_socket_data_event_handler(sl_wifi_buffer_t *rx_buffer)
{
  sl_wifi_system_packet_t *rx_packet = (sl_wifi_system_packet_t *)rx_buffer->data;

  if (rx_packet->command != SLI_RECEIVE_RAW_DATA) {
    sli_buffer_manager_free_buffer(rx_buffer);
    return SL_STATUS_NOT_SUPPORTED;
  }
  // Handle the case when raw data is received
  const sl_si91x_socket_metadata_t *firmware_socket_response = (sl_si91x_socket_metadata_t *)rx_packet->data;
  if (firmware_socket_response == NULL) {
    sli_buffer_manager_free_buffer(rx_buffer);
    return SL_STATUS_NULL_POINTER;
  }
  uint8_t *data      = (rx_packet->data + firmware_socket_response->offset);
  int8_t host_socket = -1;

  // Find the host socket corresponding to the received data.
  // Mask the socket ID to the lower byte, since the upper byte can carry WebSocket frame/opcode information.
  const uint8_t host_socket_id_from_fw = (uint8_t)(firmware_socket_response->socket_id & 0x00FFU);
  for (uint8_t host_socket_index = 0; host_socket_index < SLI_NUMBER_OF_SOCKETS; host_socket_index++) {
    if ((sli_si91x_sockets[host_socket_index] != NULL)
        && (host_socket_id_from_fw == sli_si91x_sockets[host_socket_index]->id)
        && (sli_si91x_sockets[host_socket_index]->state != LISTEN)) {
      host_socket = host_socket_index;
    }
  }

  // Retrieve the client socket
  const sli_si91x_socket_t *client_socket = sli_get_si91x_socket(host_socket);
  //Verifying socket existence
  if (client_socket == NULL) {
    sli_buffer_manager_free_buffer(rx_buffer);
    return SL_STATUS_NOT_FOUND;
  }

  // Call the user-defined receive data callback
  if (client_socket->is_receive_cmd_pending == true) {
    sli_command_engine_packet_type_configuration_t packet_type_info = { 0 };
    // Get packet type configuration for the given command type
    sl_status_t status =
      sli_command_engine_get_rx_queue_info_from_packet_type(&sli_wifi_command_engine,
                                                            (uint16_t)(client_socket->index + SI91X_CMD_MAX),
                                                            &packet_type_info);
    if (status != SL_STATUS_OK) {
      sli_buffer_manager_free_buffer(rx_buffer);
      return status;
    }

    sli_command_engine_metadata_t *metadata = NULL;
    // Allocate metadata buffer (hybrid allocation allows pool + heap fallback)
    status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_METADATA_POOL,
                                                SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                                1000,
                                                (sli_buffer_t *)&metadata);
    if (status != SL_STATUS_OK) {
      SL_DEBUG_LOG_V2(ERROR, "Data event: metadata allocation failed\r\n");
      sli_buffer_manager_free_buffer(rx_buffer);
      return SL_STATUS_ALLOCATION_FAILED;
    }

    metadata->packet_status              = sli_wifi_get_wifi_frame_status(rx_packet);
    metadata->tx_info.data_packet        = rx_buffer;
    metadata->tx_info.data_packet_length = (rx_packet->length & 0x0FFF);
    metadata->tx_info.flags              = 0;
    metadata->tx_info.packet_id          = 0;

    sli_queue_manager_enqueue(packet_type_info.sync_response_queue, metadata);
    osEventFlagsSet(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
    return SL_STATUS_OK;

  } else if (client_socket->recv_data_callback != NULL) {
    client_socket->recv_data_callback(host_socket, data, firmware_socket_response->length, firmware_socket_response);
  } else {
    sli_buffer_manager_free_buffer(rx_buffer);
    // No callback registered, drop the packet
    return SL_STATUS_NOT_FOUND;
  }
  sli_buffer_manager_free_buffer(rx_buffer);
  return SL_STATUS_OK;
}

sl_status_t sli_si91x_send_socket_data(sli_si91x_socket_t *si91x_socket,
                                       const sli_si91x_socket_send_request_t *request,
                                       const void *data)
{
  sl_wifi_system_packet_t *packet = NULL;
  sli_si91x_socket_send_request_t *send;

  sl_status_t status     = SL_STATUS_OK;
  uint16_t header_length = (request->data_offset - sizeof(sli_si91x_socket_send_request_t));
  uint32_t data_length   = request->length;

  if (data == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_DATA_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&packet);
  VERIFY_STATUS_AND_RETURN(status);

  // Enter atomic section to safely access the queue
  CORE_irqState_t state = CORE_EnterAtomic();
  ++si91x_socket->data_buffer_count;
  CORE_ExitAtomic(state);

  memset(packet->desc, 0, sizeof(packet->desc));

  send = (sli_si91x_socket_send_request_t *)packet->data;
  memcpy(send, request, sizeof(sli_si91x_socket_send_request_t));
  memcpy((send->send_buffer + header_length), data, data_length);

  // Fill the length
  packet->length = (sizeof(sli_si91x_socket_send_request_t) + header_length + data_length) & 0xFFF;
  // Fill the firmware queue id
  packet->desc[1] |= (SLI_WLAN_DATA_Q << 4);

  // Route (send) the packet via routing utility (may be async)
  return sli_routing_utility_route_packet(&wifi_command_engine_routing_table,
                                          SLI_WIFI_SOCKET_DATA_PACKET,
                                          packet,
                                          (packet->length & 0xFFF),
                                          (void *)(&si91x_socket->index));
}

void sli_si91x_send_tx_packet_status_handler(uint16_t packet_type, sl_status_t status, void *context)
{
  UNUSED_PARAMETER(packet_type);
  UNUSED_PARAMETER(status);

  if (context == NULL) {
    return;
  }
  sli_si91x_socket_t *si91x_socket = sli_get_si91x_socket(*(int8_t *)context);
  if (si91x_socket != NULL) {
    CORE_irqState_t state = CORE_EnterAtomic();
    if (si91x_socket->data_buffer_count > 0) {
      --si91x_socket->data_buffer_count;
    }
    CORE_ExitAtomic(state);
  }
  return;
}

int sli_si91x_get_socket_id(sl_wifi_system_packet_t *packet)
{
  // Handle connection establishment response
  switch (packet->command) {
    case SLI_WIFI_RSP_CONN_ESTABLISH:
      return ((sli_si91x_rsp_ltcp_est_t *)packet->data)->socket_id;
    case SLI_WIFI_RSP_REMOTE_TERMINATE:
      return ((sl_si91x_socket_close_response_t *)packet->data)->socket_id;
    case SLI_RECEIVE_RAW_DATA:
      return *((uint8_t *)&(((sl_si91x_socket_metadata_t *)packet->data)->socket_id));
    case SLI_WIFI_RSP_SOCKET_READ_DATA:
      return packet->data[0];
    case SLI_WIFI_RSP_TCP_ACK_INDICATION:
      return ((sli_si91x_rsp_tcp_ack_t *)packet->data)->socket_id;
    case SLI_WIFI_RSP_SOCKET_CREATE:
      return (((sli_si91x_socket_create_response_t *)packet->data)->socket_id[0]
              + (((sli_si91x_socket_create_response_t *)packet->data)->socket_id[1] << 8));
    case SLI_WIFI_RSP_SOCKET_CLOSE:
      if (((sl_si91x_socket_close_response_t *)packet->data)->socket_id != 0) {
        return ((sl_si91x_socket_close_response_t *)packet->data)->socket_id;
      }

      const uint16_t port = ((sl_si91x_socket_close_response_t *)packet->data)->port_number;

      for (int i = 0; i < SLI_NUMBER_OF_SOCKETS; ++i) {

        if (sli_si91x_sockets[i] != NULL && sli_si91x_sockets[i]->local_address.sin6_port == port
            && sli_si91x_sockets[i]->state == LISTEN) {

          return sli_si91x_sockets[i]->id;
        }
      }
      return -1;
    case SLI_WIFI_RSP_SELECT_REQUEST:
      __attribute__((fallthrough));
    default:
      return -1;
  }
}

int sli_si91x_connect(int socket, const struct sockaddr *addr, socklen_t addr_len)
{
  errno = 0;

  sl_status_t status = SL_STATUS_FAIL;
  sli_si91x_socket_t *si91x_socket;

  // Retrieve the socket using the socket index
  si91x_socket = sli_get_si91x_socket(socket);

  // Check if the socket is valid.
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket == NULL, EBADF);

  // Check if the socket is already connected
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket->type == SOCK_STREAM && si91x_socket->state == CONNECTED, EISCONN);

  // Check the socket state based on its type
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket->type == SOCK_STREAM && si91x_socket->state > BOUND, EBADF);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket->type == SOCK_DGRAM && si91x_socket->state != INITIALIZED
                                     && si91x_socket->state != BOUND && si91x_socket->state != UDP_UNCONNECTED_READY,
                                   EBADF);

  // Check if the provided sockaddr length is sufficient
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(
    (si91x_socket->local_address.sin6_family == AF_INET && addr_len < sizeof(struct sockaddr_in))
      || (si91x_socket->local_address.sin6_family == AF_INET6 && addr_len < sizeof(struct sockaddr_in6)),
    EINVAL);

  // Check if the provided sockaddr pointer is valid
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(addr == NULL, EFAULT);

  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket->local_address.sin6_family != addr->sa_family, EAFNOSUPPORT);

  memcpy(&si91x_socket->remote_address,
         addr,
         (addr_len > sizeof(struct sockaddr_in6)) ? sizeof(struct sockaddr_in6) : addr_len);

  // Since socket is already created, there is no need to send create request again.
  if (si91x_socket->type == SOCK_DGRAM && si91x_socket->state == UDP_UNCONNECTED_READY) {
    si91x_socket->state = CONNECTED;

    return SLI_SI91X_NO_ERROR;
  }

  // Prepare socket request based on socket type and send the request to the bus driver
  if (si91x_socket->type == SOCK_STREAM) {
    status = sli_create_and_send_socket_request(socket, SLI_SI91X_SOCKET_TCP_CLIENT, NULL);
  } else if (si91x_socket->type == SOCK_DGRAM) {
    status = sli_create_and_send_socket_request(socket, SLI_SI91X_SOCKET_UDP_CLIENT, NULL);
  }

  // Verify the status of the socket operation and return errors if necessary
  // Preserve errno if already set by sli_create_and_send_socket_request (e.g., EAFNOSUPPORT)
  if (status != SL_STATUS_OK) {
    if (errno == 0) {
      errno = SLI_SI91X_UNDEFINED_ERROR;
    }
    return -1;
  }

  // Update the socket state to "CONNECTED" and return success
  si91x_socket->state = CONNECTED;
  return SLI_SI91X_NO_ERROR;
}

int sli_si91x_bind(int socket_id, const struct sockaddr *addr, socklen_t addr_len)
{
  errno = 0;

  // Retrieve the SI91X socket associated with the given socket ID.
  sli_si91x_socket_t *si91x_socket         = sli_get_si91x_socket(socket_id);
  const struct sockaddr_in *socket_address = (const struct sockaddr_in *)addr;

  // Validate socket, address, and address length
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(si91x_socket == NULL || si91x_socket->state != INITIALIZED, EBADF);
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(
    (si91x_socket->local_address.sin6_family == AF_INET && addr_len < sizeof(struct sockaddr_in))
      || (si91x_socket->local_address.sin6_family == AF_INET6 && addr_len < sizeof(struct sockaddr_in6)),
    EINVAL);

  SLI_SET_ERRNO_AND_RETURN_IF_TRUE(addr == NULL, EFAULT);

  // Check whether local port is already used or not
  if (!sli_is_port_available(socket_address->sin_port)) {
    SLI_SET_ERROR_AND_RETURN(EADDRINUSE);
  }

  // Copy the provided address to the local address structure
  memcpy(&si91x_socket->local_address,
         addr,
         (addr_len > sizeof(struct sockaddr_in6)) ? sizeof(struct sockaddr_in6) : addr_len);

  si91x_socket->state = BOUND;

  // For UDP sockets, create and send a socket request.
  if (si91x_socket->type == SOCK_DGRAM) {
    sl_status_t socket_create_request_status =
      sli_create_and_send_socket_request(socket_id, SLI_SI91X_SOCKET_UDP_CLIENT, NULL);
    if (socket_create_request_status != SLI_SI91X_NO_ERROR) {
      // Preserve errno if already set by sli_create_and_send_socket_request (e.g., EAFNOSUPPORT)
      // Only set to undefined error if errno was not set
      if (errno == 0) {
        errno = SLI_SI91X_UNDEFINED_ERROR;
      }
      return -1;
    }

    si91x_socket->state = UDP_UNCONNECTED_READY;
  }

  return SLI_SI91X_NO_ERROR;
}

#ifndef __ZEPHYR__
bool sli_check_null_sets_and_range(int nfds,
                                   const fd_set *readfds,
                                   const fd_set *writefds,
                                   const struct timeval *timeout)
#else
bool sli_check_null_sets_and_range(int nfds,
                                   const sl_si91x_fdset_t *readfds,
                                   const sl_si91x_fdset_t *writefds,
                                   const struct timeval *timeout)
#endif
{

  // Check if all file descriptor sets are NULL
  if ((readfds == NULL) && (writefds == NULL)) {
    return true; // Invalid argument, no sets specified
  }

  // Check if the number of file descriptors (nfds) is within a valid range
  if (nfds < 0 || nfds > SLI_NUMBER_OF_SOCKETS) {
    return true; // Invalid argument, nfds out of range
  }

  // Check if the provided timeout is valid
  if ((timeout != NULL) && ((timeout->tv_sec < 0) || (timeout->tv_usec < 0))) {
    return true; // Invalid argument, negative timeout
  }
  return false;
}

#ifndef __ZEPHYR__
uint8_t sli_prepare_select_request(int nfds,
                                   const fd_set *readfds,
                                   const fd_set *writefds,
                                   sli_si91x_socket_select_req_t *request)
#else
uint8_t sli_prepare_select_request(int nfds,
                                   const sl_si91x_fdset_t *readfds,
                                   const sl_si91x_fdset_t *writefds,
                                   sli_si91x_socket_select_req_t *request)
#endif
{

  // Loop through the provided file descriptor sets and populate the select request structure
  for (uint8_t host_socket_index = 0; host_socket_index < nfds; host_socket_index++) {
    // Retrieve the si91x_socket associated with the index
    const sli_si91x_socket_t *socket = sli_get_si91x_socket(host_socket_index);

    // Throw error if the socket file descriptor set is invalid
    if (socket == NULL
#ifndef __ZEPHYR__
        && ((readfds != NULL && FD_ISSET(host_socket_index, readfds))
            || (writefds != NULL && FD_ISSET(host_socket_index, writefds)))) {
#else
        && ((readfds != NULL && SL_SI91X_FD_ISSET(host_socket_index, readfds))
            || (writefds != NULL && SL_SI91X_FD_ISSET(host_socket_index, writefds)))) {
#endif
      return EBADF; // Bad file descriptor
    }

    // The code will reach this if clause in the case of a socket being NULL and the socket being neither set in readfds nor writefds.
    // Continue to next socket if this one is not in use
    if (socket == NULL) {
      continue;
    }

    // Check if the socket is set for read operations in the readfds set
    // Set the corresponding bit in the read file descriptor set
#ifndef __ZEPHYR__
    if ((readfds != NULL) && (FD_ISSET(host_socket_index, readfds))) {
#else
    if ((readfds != NULL) && (SL_SI91X_FD_ISSET(host_socket_index, readfds))) {
#endif
      request->read_fds.fd_array[0] |= (1U << socket->id);
    }

    // Check if the socket is set for write operations in the writefds set
    // Set the corresponding bit in the write file descriptor set
#ifndef __ZEPHYR__
    if ((writefds != NULL) && (FD_ISSET(host_socket_index, writefds))) {
#else
    if ((writefds != NULL) && (SL_SI91X_FD_ISSET(host_socket_index, writefds))) {
#endif
      request->write_fds.fd_array[0] |= (1U << socket->id);
    }

    // Update the maximum file descriptor number encountered
    if (request->num_fd <= socket->id) {
      request->num_fd = (uint8_t)(socket->id + 1);
    }
  }
  return 0; // Indicate success
}

void sli_handle_timeout(const struct timeval *timeout, sli_si91x_socket_select_req_t *request, uint32_t *wait_time)
{
  request->select_timeout.tv_sec  = timeout->tv_sec;
  request->select_timeout.tv_usec = timeout->tv_usec;
  // Convert timeout to milliseconds and add extra wait time for the response
  *wait_time = ((request->select_timeout.tv_sec * 1000) + (request->select_timeout.tv_usec / 1000)
                + SLI_SI91X_HOST_WAIT_FOR_SELECT_RSP);
}

#ifndef __ZEPHYR__
int32_t sli_handle_select_result(const sli_si91x_select_request_t *select_request,
                                 sl_wifi_buffer_t *response_buffer,
                                 fd_set *readfds,
                                 fd_set *writefds,
                                 fd_set *exceptfds)
#else
int32_t sli_handle_select_result(const sli_si91x_select_request_t *select_request,
                                 sl_wifi_buffer_t *response_buffer,
                                 sl_si91x_fdset_t *readfds,
                                 sl_si91x_fdset_t *writefds,
                                 sl_si91x_fdset_t *exceptfds)
#endif
{
  // Initialize the total file descriptor count
  int32_t total_fd_set_count = -1;

  sl_wifi_system_packet_t *response_packet =
    (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(response_buffer, 0, NULL);
  const sli_si91x_socket_select_rsp_t *select_rsp = (sli_si91x_socket_select_rsp_t *)response_packet->data;

  total_fd_set_count = sli_handle_select_response(select_rsp, readfds, writefds, exceptfds);

  // Free the response buffer
  sli_buffer_manager_free_buffer(response_buffer);
  // Clear the select ID in the internal table
  sli_si91x_clear_select_id(select_request->select_id);

  // Return the total number of file descriptors set in the read, write, or exception sets
  return total_fd_set_count;
}

#ifndef __ZEPHYR__
int sli_si91x_select(int nfds,
                     fd_set *readfds,
                     fd_set *writefds,
                     fd_set *exceptfds,
                     const struct timeval *timeout,
                     sl_si91x_socket_select_callback_t callback)
#else
int sli_si91x_select(int nfds,
                     sl_si91x_fdset_t *readfds,
                     sl_si91x_fdset_t *writefds,
                     sl_si91x_fdset_t *exceptfds,
                     const struct timeval *timeout,
                     sl_si91x_socket_select_callback_t callback)
#endif
{
  UNUSED_PARAMETER(exceptfds);                       // exceptfds is not supported by the firmware, so it is unused
  sl_status_t status                 = SL_STATUS_OK; // Initialize status
  uint32_t select_response_wait_time = 0;            // Time to wait for the select response

  // Define a structure to hold the select request parameters
  sli_si91x_socket_select_req_t request = { 0 };

  sl_wifi_buffer_t *response_buffer = NULL;

  if (sli_check_null_sets_and_range(nfds, readfds, writefds, timeout)) {
    SLI_SET_ERROR_AND_RETURN(EINVAL);
  }

  // Prepare the select request structure
  uint8_t error = sli_prepare_select_request(nfds, readfds, writefds, &request);
  if (error != 0) {
    SLI_SET_ERROR_AND_RETURN(EBADF);
  }

  if (timeout != NULL) {
    sli_handle_timeout(timeout, &request, &select_response_wait_time);
  } else {
    // If no timeout is specified, set the request to indicate no timeout and wait indefinitely
    request.no_timeout        = 1;
    select_response_wait_time = SLI_WIFI_WAIT_FOR_EVER;
  }

  // Get an available select ID from the internal table
  sli_si91x_select_request_t *select_request = sli_si91x_get_available_select_id();
  // If no select ID is available, return an error
  SLI_SET_ERRNO_AND_RETURN_IF_TRUE((select_request == NULL), EPERM);
  // Assign the callback function for this select request
  select_request->select_callback = callback;
  // Set the select_id in the request structure
  request.select_id = select_request->select_id;

  // Send the select request asynchronously to the firmware
  status =
    sli_wifi_async_send_command(SLI_WIFI_REQ_SELECT_REQUEST, SLI_SI91X_SOCKET_CMD, &request, sizeof(request), NULL);
  if (status != SL_STATUS_IN_PROGRESS) {
    // If sending the command fails, clear the select ID and return
    sli_si91x_clear_select_id(request.select_id);
  }
  // // Verify that the command was sent successfully
  SLI_SOCKET_VERIFY_STATUS_AND_RETURN(status, SL_STATUS_IN_PROGRESS, SLI_SI91X_UNDEFINED_ERROR);

  // If a callback was provided, return immediately (non-blocking)
  if (callback != NULL) {
    return SLI_WIFI_RETURN_IMMEDIATELY;
  }

  status = sli_wifi_receive_response_buffer(SLI_SI91X_SOCKET_CMD,
                                            request.select_id,
                                            SLI_WIFI_WAIT_FOR_RESPONSE(select_response_wait_time),
                                            SLI_WIFI_WAIT_ON_EVENT_ID,
                                            (void **)&response_buffer);
  if (status != SL_STATUS_OK) {
    // If status fails, clear the select ID
    sli_si91x_clear_select_id(request.select_id);
    if (response_buffer != NULL) {
      // If the response buffer is not NULL, free it
      sli_buffer_manager_free_buffer(response_buffer);
    }
    SLI_SOCKET_VERIFY_STATUS_AND_RETURN(status, SL_STATUS_OK, SLI_SI91X_UNDEFINED_ERROR);
  }
  // Process the response packet
  return sli_handle_select_result(select_request, response_buffer, readfds, writefds, exceptfds);
}

static sli_si91x_select_request_t *sli_si91x_get_available_select_id(void)
{
  // Check if there are any available select request entries.
  if (sli_si91x_max_select_count == 0) {
    return NULL; // Return NULL if no requests can be processed.
  }

  // Enter atomic section to ensure thread-safe access to the select_request_table.
  CORE_irqState_t state = CORE_EnterAtomic();

  // Iterate over the select request table to find an available entry.
  for (unsigned int i = 0; i < sli_si91x_max_select_count; i++) {
    // Check if the current entry is not in use.
    if (!select_request_table[i].in_use) {
      // Assign the current index as the select ID.
      select_request_table[i].select_id = (uint8_t)i;

      // Mark the entry as in use.
      select_request_table[i].in_use = 1;

      // Exit atomic section after modifying the entry.
      CORE_ExitAtomic(state);

      // Return the pointer to the available select request entry.
      return &select_request_table[i];
    }
  }

  // Exit atomic section if no available entry was found.
  CORE_ExitAtomic(state);

  // Return NULL to indicate that no available select ID was found.
  return NULL;
}

static void sli_si91x_clear_select_id(uint8_t id)
{
  // Check if the select request table is empty or if the provided ID is out of range.
  if (sli_si91x_max_select_count == 0 || id >= sli_si91x_max_select_count) {
    return; // If no requests or invalid ID, exit early.
  }

  // Enter atomic section to ensure thread-safe access to the select_request_table.
  CORE_irqState_t state = CORE_EnterAtomic();

  // Mark the entry corresponding to the provided ID as not in use.
  select_request_table[id].in_use = 0;

  // Exit atomic section after the operation is complete.
  CORE_ExitAtomic(state);
}

void sli_si91x_set_socket_event(uint32_t event_mask)
{
  osEventFlagsSet(si91x_socket_events, event_mask);
}

sl_status_t sli_si91x_flush_select_request_table(uint16_t error_code)
{
  UNUSED_PARAMETER(error_code);
  // Iterate over all entries in the select_request_table
  for (unsigned int i = 0; i < sli_si91x_max_select_count; i++) {
    // Check if the current select_request_table entry is in use
    if (select_request_table[i].in_use) {
      // If a callback function exists for this entry, it is async
      if (select_request_table[i].select_callback != NULL) {
        select_request_table[i].in_use = 0; // Mark as not in use
      } else {
        // If there is no callback, the request needs to be reset and so the response data pointer is cleared, indicating no data is available
        // select_request_table[i].response_data = NULL;
        // Set the frame status to indicate rejoin failure in the request
        //  select_request_table[i].frame_status = error_code;
        // Set the appropriate event flag for the socket associated with the select_id
        //  osEventFlagsSet(si91x_socket_select_events, BIT(select_request_table[i].select_id));
      }
    }
  }
  // Return SL_STATUS_OK to indicate the function completed successfully
  return SL_STATUS_OK;
}

sl_status_t sli_si91x_udp_connect_if_unconnected(sli_si91x_socket_t *si91x_socket,
                                                 const struct sockaddr *to_addr,
                                                 socklen_t to_addr_len,
                                                 int socket_id)
{
  sl_status_t status = SL_STATUS_OK;

  if (si91x_socket->type == SOCK_DGRAM && (si91x_socket->state == BOUND || si91x_socket->state == INITIALIZED)) {
    if (si91x_socket->ssl_bitmap & SL_SI91X_ENABLE_DTLS) {
      memcpy(&si91x_socket->remote_address,
             to_addr,
             (to_addr_len > sizeof(struct sockaddr_in6)) ? sizeof(struct sockaddr_in6) : to_addr_len);
    }
    status = sli_create_and_send_socket_request(socket_id, SLI_SI91X_SOCKET_UDP_CLIENT, NULL);

    if (status != SL_STATUS_OK) {
      return status;
    }
    si91x_socket->state = UDP_UNCONNECTED_READY;
  }

  return status;
}

void sl_si91x_set_socket_cipherlist(uint32_t cipher_list)
{
  sl_si91x_socket_selected_ciphers = cipher_list;
}

void sl_si91x_set_extended_socket_cipherlist(uint32_t extended_cipher_list)
{
  sl_si91x_socket_selected_extended_ciphers = extended_cipher_list;
}

sli_si91x_socket_t *get_socket_from_packet(sl_wifi_system_packet_t *socket_packet)
{

  const uint16_t payload_length = (socket_packet->length & 0x0FFF);
  if (payload_length == 0) {
    return NULL;
  }

  int socket_id = sli_si91x_get_socket_id(socket_packet);

  if (socket_packet->command == SLI_WIFI_RSP_CONN_ESTABLISH) {
    socket_packet->command = SLI_WIFI_RSP_SOCKET_ACCEPT;
  }

  if (socket_packet->command == SLI_WIFI_RSP_SOCKET_CREATE) {
    const sli_si91x_socket_create_response_t *socket_create_response =
      ((sli_si91x_socket_create_response_t *)socket_packet->data);
    return sli_si91x_get_socket_from_id(
      -1,
      S_RESET,
      (int16_t)(socket_create_response->socket_type[0] | (socket_create_response->socket_type[1] << 8)));
  } else if (socket_packet->command == SLI_WIFI_RSP_SOCKET_ACCEPT) {
    const uint16_t port = ((sli_si91x_rsp_ltcp_est_t *)socket_packet->data)->src_port_num;
    for (uint8_t i = 0; i < SLI_NUMBER_OF_SOCKETS; ++i) {
      if (sli_si91x_sockets[i] != NULL && sli_si91x_sockets[i]->local_address.sin6_port == port
          && sli_si91x_sockets[i]->state == LISTEN) {
        return sli_si91x_sockets[sli_si91x_sockets[i]->client_id];
      }
    }
    return NULL;
  } else if (socket_packet->command == SLI_WIFI_RSP_SOCKET_CLOSE) {
    if (((sl_si91x_socket_close_response_t *)socket_packet->data)->socket_id != 0) {
      return sli_si91x_get_socket_from_id(socket_id, LISTEN, -1);
    }

    const uint16_t port = ((sl_si91x_socket_close_response_t *)socket_packet->data)->port_number;

    for (uint8_t i = 0; i < SLI_NUMBER_OF_SOCKETS; ++i) {
      if (sli_si91x_sockets[i] != NULL && sli_si91x_sockets[i]->local_address.sin6_port == port
          && sli_si91x_sockets[i]->state == LISTEN) {
        return sli_si91x_sockets[i];
      }
    }

    return NULL;
  } else {
    return sli_si91x_get_socket_from_id(socket_id, LISTEN, -1);
  }
}

sl_status_t sli_si91x_get_dest_ip_address_from_ap_client_disconnect_resp(
  const sli_si91x_ap_disconnect_resp_t *ap_disconnect_resp,
  sl_ip_address_t *dest_ip_address)
{
  if (ap_disconnect_resp == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  if (dest_ip_address == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // Initialize the destination IP address structure
  memset(dest_ip_address, 0, sizeof(sl_ip_address_t));

  // Try IPv4 first (highest priority)
  if (ap_disconnect_resp->flag & BIT(0)) {
    dest_ip_address->type = SL_IPV4;
    memcpy(dest_ip_address->ip.v4.bytes, ap_disconnect_resp->ipv4_address, SL_IPV4_ADDRESS_LENGTH);
    if (!sli_wifi_is_ip_address_zero(dest_ip_address)) {
      return SL_STATUS_OK;
    }
  }

  // Try IPv6 global address (second priority)
  if (ap_disconnect_resp->flag & BIT(2)) {
    dest_ip_address->type = SL_IPV6;
    memcpy(dest_ip_address->ip.v6.bytes, ap_disconnect_resp->global_address, SL_IPV6_ADDRESS_LENGTH);
    if (!sli_wifi_is_ip_address_zero(dest_ip_address)) {
      return SL_STATUS_OK;
    }
  }

  // Try IPv6 link-local address (lowest priority)
  if (ap_disconnect_resp->flag & BIT(1)) {
    dest_ip_address->type = SL_IPV6;
    memcpy(dest_ip_address->ip.v6.bytes, ap_disconnect_resp->link_local_address, SL_IPV6_ADDRESS_LENGTH);
    if (!sli_wifi_is_ip_address_zero(dest_ip_address)) {
      return SL_STATUS_OK;
    }
  }

  return SL_STATUS_NOT_FOUND;
}
