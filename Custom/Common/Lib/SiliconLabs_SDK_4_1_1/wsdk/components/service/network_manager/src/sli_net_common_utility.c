/***************************************************************************/ /**
 * @file
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
#include "sli_net_common_utility.h"
#include "sl_net.h"
#include "sl_net_constants.h"
#include "cmsis_os2.h"
#include "sl_cmsis_utility.h"
#include "sl_net_types.h"
#include "sl_net_wifi_types.h"
#if defined(SLI_SI917)
#include "sl_net_si91x.h"
#include "sli_net_utility.h"
#endif
#include "sl_wifi.h"
#if defined(SLI_SI91X_OFFLOAD_NETWORK_STACK) || defined(SLI_SI91X_LWIP_HOSTED_NETWORK_STACK)
#include "sl_si91x_types.h"
#endif
#include "string.h"
#ifdef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
#include "sl_net_for_lwip.h"
#endif
#include "sli_wifi_constants.h"
#include "sli_net_types.h"

#define CRED_TYPE_CERT 0
#define CRED_TYPE_CRED 1

// Auto-join / network manager synchronization primitives
osThreadId_t sli_network_manager_id                  = NULL;
osMessageQueueId_t sli_network_manager_request_queue = NULL;
osEventFlagsId_t sli_network_manager_response_flags  = NULL;

const osThreadAttr_t sli_network_manager_attributes = {
  .name       = "network_manager",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = SL_NET_NETWORK_MANAGER_THREAD_STACK_SIZE,
  .priority   = SL_NET_NETWORK_MANAGER_THREAD_PRIORITY,
  .tz_module  = 0,
};
const osMessageQueueAttr_t sli_network_manager_req_queue_attributes = { .name = "sli_network_manager_request_queue" };
const osEventFlagsAttr_t sli_network_manager_rsp_flags_attributes   = { .name = "sli_network_manager_response_flags" };

// Event flags for auto-join process
sl_net_event_handler_t net_event_handler = NULL;

// Structure to hold asynchronous state for each network interface (shared with sl_net_for_lwip.c)
sli_net_async_if_state_t sli_async_state[SL_NET_INTERFACE_MAX];
// Structure to hold client state if any in auto-join
static sli_net_sync_client_if_state_t sli_sync_client_state = { 0 };
// Tracks if a synchronous auto-join caller is actively waiting on the response flags
static bool sli_sync_auto_join_waiting = false;

// Timer and context for auto-join retries without blocking the manager thread
static osTimerId_t sli_auto_join_retry_timer_id = NULL;
typedef struct {
  bool active;
  uint8_t sorted_profile_ids[MAX_WIFI_CLIENT_PROFILES];
  int remaining_attempts;
  sl_net_event_t event;
  sl_net_interface_t interface;
} sli_auto_join_retry_ctx_t;

static sli_auto_join_retry_ctx_t sli_auto_join_retry_ctx = { 0 };

static void sli_auto_join_retry_timer_cb(const void *argument);
static void sli_reset_auto_join_retry_ctx(void);
static void sli_network_manager_cleanup_init_failure(void);

void sli_net_async_reset_all(void)
{
  // Reset entire array, state is set to SLI_NET_ASYNC_IDLE and profile to 0
  memset(sli_async_state, 0, sizeof(sli_async_state));
  return;
}

static void sli_network_manager_cleanup_init_failure(void)
{
  if (sli_network_manager_request_queue != NULL) {
    if (osMessageQueueDelete(sli_network_manager_request_queue) != osOK) {
      SL_DEBUG_LOG_V2(WARN, "Warning: Failed to cleanup request queue during init failure\r\n");
    }
    sli_network_manager_request_queue = NULL;
  }

  if (sli_network_manager_response_flags != NULL) {
    if (osEventFlagsDelete(sli_network_manager_response_flags) != osOK) {
      SL_DEBUG_LOG_V2(WARN, "Warning: Failed to cleanup response flags during init failure\r\n");
    }
    sli_network_manager_response_flags = NULL;
  }

  if (sli_auto_join_retry_timer_id != NULL) {
    (void)osTimerDelete(sli_auto_join_retry_timer_id);
    sli_auto_join_retry_timer_id = NULL;
  }
}

#ifdef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
extern sl_net_wifi_lwip_context_t *wifi_client_context;
#endif

sl_status_t sli_net_register_event_handler(sl_net_event_handler_t function)
{
  net_event_handler = function;
  return SL_STATUS_OK;
}

sl_status_t sli_net_get_wifi_credential_type(sl_net_credential_type_t type, sl_wifi_credential_type_t *wifi_type)
{
  // Map the network credential type to WiFi credential type
  switch (type) {
    case SL_NET_WIFI_PSK:
      // Set the credential type to Pre-Shared Key (PSK)
      *wifi_type = SL_WIFI_PSK_CREDENTIAL;
      break;
    case SL_NET_WIFI_PMK:
      // Set the credential type to Pairwise Master Key (PMK)
      *wifi_type = SL_WIFI_PMK_CREDENTIAL;
      break;
    case SL_NET_WIFI_WEP:
      // Set the credential type to Wired Equivalent Privacy (WEP)
      *wifi_type = SL_WIFI_WEP_CREDENTIAL;
      break;
    case SL_NET_EAP_CLIENT_CREDENTIAL:
      // Set the credential type to Extensible Authentication Protocol (EAP)
      *wifi_type = SL_WIFI_EAP_CREDENTIAL;
      break;
    default:
      return SL_STATUS_INVALID_PARAMETER;
  }

  return SL_STATUS_OK;
}

sl_status_t sli_net_get_net_credential_type(sl_wifi_credential_type_t type, sl_net_credential_type_t *net_type)
{
  // Map the WiFi credential type to network credential type
  switch (type) {
    case SL_WIFI_PSK_CREDENTIAL:
      // Set the credential type to Pre-Shared Key (PSK)
      *net_type = SL_NET_WIFI_PSK;
      break;
    case SL_WIFI_PMK_CREDENTIAL:
      // Set the credential type to Pairwise Master Key (PMK)
      *net_type = SL_NET_WIFI_PMK;
      break;
    case SL_WIFI_WEP_CREDENTIAL:
      // Set the credential type to Wired Equivalent Privacy (WEP)
      *net_type = SL_NET_WIFI_WEP;
      break;
    case SL_WIFI_EAP_CREDENTIAL:
      // Set the credential type to Extensible Authentication Protocol (EAP)
      *net_type = SL_NET_EAP_CLIENT_CREDENTIAL;
      break;
    default:
      return SL_STATUS_INVALID_PARAMETER;
  }

  return SL_STATUS_OK;
}

sl_status_t sli_net_validate_sl_net_profile(const sl_net_profile_t *profile, sl_net_interface_t interface)
{
  switch (interface) {
#ifdef SL_WIFI_COMPONENT_INCLUDED
    case SL_NET_WIFI_CLIENT_INTERFACE:
    case SL_NET_WIFI_CLIENT_2_INTERFACE: {
      if (
        (((const sl_net_wifi_client_profile_t *)profile)->config.ssid.length == 0)
        || (((const sl_net_wifi_client_profile_t *)profile)->config.ssid.length
            > SL_WIFI_MAX_SSID_LENGTH
                - 2)) { //The maximum length of the SSID is 34 characters with 2 characters reserved for NULL termination and internal alignment. Therefore, used `SL_WIFI_MAX_SSID_LENGTH - 2`
        SL_DEBUG_LOG_V2(ERROR,
                        "Invalid SSID length: %d\n",
                        ((const sl_net_wifi_client_profile_t *)profile)->config.ssid.length);
        return SL_STATUS_INVALID_PARAMETER;
      }
      if (((((const sl_net_wifi_client_profile_t *)profile)->config.security == SL_WIFI_OPEN)
           && (((const sl_net_wifi_client_profile_t *)profile)->config.credential_id != SL_WIFI_NO_CREDENTIAL_ID))
          || ((((const sl_net_wifi_client_profile_t *)profile)->config.security != SL_WIFI_OPEN)
              && (((const sl_net_wifi_client_profile_t *)profile)->config.credential_id == SL_WIFI_NO_CREDENTIAL_ID))) {
        SL_DEBUG_LOG_V2(ERROR, "Mismatch between security and credential_id\r\n");
        return SL_STATUS_INVALID_CONFIGURATION;
      }
      return SL_STATUS_OK;
    }
    case SL_NET_WIFI_AP_INTERFACE:
    case SL_NET_WIFI_AP_2_INTERFACE: {
      if ((((const sl_net_wifi_ap_profile_t *)profile)->config.ssid.length == 0)
          || (((const sl_net_wifi_ap_profile_t *)profile)->config.ssid.length > SL_WIFI_MAX_SSID_LENGTH - 2)) {
        SL_DEBUG_LOG_V2(ERROR,
                        "Invalid SSID length: %d\n",
                        ((const sl_net_wifi_ap_profile_t *)profile)->config.ssid.length);
        return SL_STATUS_INVALID_PARAMETER;
      }
      if (((((const sl_net_wifi_ap_profile_t *)profile)->config.security == SL_WIFI_OPEN)
           && (((const sl_net_wifi_ap_profile_t *)profile)->config.credential_id != SL_WIFI_NO_CREDENTIAL_ID))
          || ((((const sl_net_wifi_ap_profile_t *)profile)->config.security != SL_WIFI_OPEN)
              && (((const sl_net_wifi_ap_profile_t *)profile)->config.credential_id == SL_WIFI_NO_CREDENTIAL_ID))) {
        SL_DEBUG_LOG_V2(ERROR, "Mismatch between security and credential_id\r\n");
        return SL_STATUS_INVALID_CONFIGURATION;
      }
      return SL_STATUS_OK;
    }
#endif
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }
}

int sli_net_check_cred_type(sl_net_credential_type_t type)
{
  if ((SL_NET_CERTIFICATE == type) || (SL_NET_PUBLIC_KEY == type) || (SL_NET_PRIVATE_KEY == type)
      || (SL_NET_SIGNING_CERTIFICATE == type) || (SL_NET_PACK_FILE == type) || (SL_NET_TLS_PRIVATE_KEY_CBC_WRAP == type)
      || (SL_NET_TLS_PRIVATE_KEY_ECB_WRAP == type)) {
    return CRED_TYPE_CERT;
  }

  return CRED_TYPE_CRED;
}

#if defined(SLI_SI91X_OFFLOAD_NETWORK_STACK) || defined(SLI_SI91X_LWIP_HOSTED_NETWORK_STACK)
sl_status_t sli_net_get_vap_for_ip_version(uint8_t vap_id, sl_ip_address_type_t ip_type)
{
  sl_status_t status;
  sl_net_interface_t interface;
  uint8_t max_profiles;
  sl_ip_address_type_t combined_ip_types = 0;

#ifdef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
  extern bool bypass_mode_enabled;
  if (bypass_mode_enabled) {
    return SL_STATUS_WIFI_UNSUPPORTED;
  }
#endif

  if (vap_id == SL_SI91X_WIFI_CLIENT_VAP_ID || vap_id == SL_SI91X_WIFI_CLIENT_VAP_ID_1) {
    interface    = SL_NET_WIFI_CLIENT_INTERFACE;
    max_profiles = MAX_WIFI_CLIENT_PROFILES;
  } else if (vap_id == SL_SI91X_WIFI_AP_VAP_ID || vap_id == SL_SI91X_WIFI_AP_VAP_ID_1) {
    interface    = SL_NET_WIFI_AP_INTERFACE;
    max_profiles = MAX_WIFI_AP_PROFILES;
  } else {
    return SL_STATUS_INVALID_PARAMETER;
  }

  for (uint8_t id = 0; id < max_profiles; id++) {
    if (SL_NET_WIFI_CLIENT_INTERFACE == SL_NET_INTERFACE_TYPE(interface)) {
      sl_net_wifi_client_profile_t profile = { 0 };
      status                               = sl_net_get_profile(interface, id, (sl_net_profile_t *)&profile);
      if (status != SL_STATUS_OK || profile.ip.type == 0) {
        continue;
      }
      combined_ip_types |= profile.ip.type;
    } else {
      sl_net_wifi_ap_profile_t profile = { 0 };
      status                           = sl_net_get_profile(interface, id, (sl_net_profile_t *)&profile);
      if (status != SL_STATUS_OK || profile.ip.type == 0) {
        continue;
      }
      combined_ip_types |= profile.ip.type;
    }
  }

  // If no profiles are configured (combined_ip_types == 0), allow the operation
  // to proceed. Applications that bypass sl_net (e.g., using sl_si91x_configure_ip_address
  // directly) won't have profiles set up, and the firmware will handle validation.
  if (combined_ip_types == 0) {
    return SL_STATUS_OK;
  }

  return ((combined_ip_types & ip_type) != 0) ? SL_STATUS_OK : SL_STATUS_INVALID_CONFIGURATION;
}
#endif /* SLI_SI91X_OFFLOAD_NETWORK_STACK || SLI_SI91X_LWIP_HOSTED_NETWORK_STACK */

bool sli_net_is_ip_config_success(sl_status_t status)
{
  return (status == SL_STATUS_OK) || (status == SL_STATUS_WIFI_IPV4_OK) || (status == SL_STATUS_WIFI_IPV6_OK);
}

sl_status_t sli_network_manager_init(void)
{
  if (sli_network_manager_request_queue && sli_network_manager_response_flags && sli_network_manager_id) {
    return SL_STATUS_ALREADY_INITIALIZED;
  }

  if (sli_network_manager_request_queue == NULL) {
    sli_network_manager_request_queue = osMessageQueueNew(SLI_NETWORK_MANAGER_QUEUE_DEPTH,
                                                          sizeof(sli_network_manager_message_t),
                                                          &sli_network_manager_req_queue_attributes);
    if (sli_network_manager_request_queue == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to create network manager request queue.\r\n");
      return SL_STATUS_FAIL;
    }
  }

  if (sli_network_manager_response_flags == NULL) {
    sli_network_manager_response_flags = osEventFlagsNew(&sli_network_manager_rsp_flags_attributes);
    if (sli_network_manager_response_flags == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to create network manager response flags.\r\n");
      sli_network_manager_cleanup_init_failure();
      return SL_STATUS_FAIL;
    }
  }

  if (sli_auto_join_retry_timer_id == NULL) {
    sli_auto_join_retry_timer_id = osTimerNew((osTimerFunc_t)sli_auto_join_retry_timer_cb, osTimerOnce, NULL, NULL);
    if (sli_auto_join_retry_timer_id == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to create auto-join retry timer.\r\n");
      sli_network_manager_cleanup_init_failure();
      return SL_STATUS_FAIL;
    }
  }

  if (sli_network_manager_id == NULL) {
    sli_network_manager_id =
      osThreadNew((osThreadFunc_t)sli_network_manager_event_handler, NULL, &sli_network_manager_attributes);
    if (sli_network_manager_id == NULL) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to create network manager thread.\r\n");
      sli_network_manager_cleanup_init_failure();
      return SL_STATUS_FAIL;
    }
  }

  return SL_STATUS_OK;
}

sl_status_t sli_network_manager_deinit(void)
{
  // Check if network manager is already deinitialized or never initialized
  if (sli_network_manager_request_queue == NULL && sli_network_manager_response_flags == NULL
      && sli_network_manager_id == NULL) {
    return SL_STATUS_OK; // Already deinitialized
  }

  // If thread and queues exist, send termination request
  if (sli_network_manager_id != NULL && sli_network_manager_request_queue != NULL
      && sli_network_manager_response_flags != NULL) {
    sli_network_manager_message_t message = { 0 };
    message.event_flags                   = SLI_NET_THREAD_TERMINATE_Q_EVENT;

    (void)osEventFlagsClear(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_THREAD_TERMINATE_ACK);
    // Unblock any thread waiting in auto_join_request so it gets a defined failure (deinit in progress)
    sli_sync_auto_join_waiting = false;
    (void)osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE);
    // Send termination request to network manager thread
    osStatus_t queue_status = osMessageQueuePut(sli_network_manager_request_queue, &message, SLI_NET_MSG_PRIO_HIGH, 0);
    if (queue_status != osOK) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to send thread terminate event to network manager: %d\r\n", queue_status);
      return SL_STATUS_FAIL;
    }

    // Wait for the network manager thread to acknowledge termination
    uint32_t ack_flags = osEventFlagsWait(sli_network_manager_response_flags,
                                          SLI_NET_RSP_FLAG_THREAD_TERMINATE_ACK,
                                          osFlagsWaitAny,
                                          osWaitForever);
    if ((ack_flags & (uint32_t)osFlagsError) != 0u) {
      SL_DEBUG_LOG_V2(ERROR, "osEventFlagsWait failed during network manager deinit\r\n");
      return SL_STATUS_FAIL;
    }
    if ((ack_flags & SLI_NET_RSP_FLAG_THREAD_TERMINATE_ACK) == 0u) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to receive thread terminate acknowledgment\r\n");
      return SL_STATUS_FAIL;
    }

    // Reset all asynchronous states
    sli_net_async_reset_all();
    sli_sync_client_state = (sli_net_sync_client_if_state_t){ 0 };

    // Terminate network manager thread
    if (osThreadTerminate(sli_network_manager_id) != osOK) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to terminate network manager thread\r\n");
      return SL_STATUS_FAIL;
    }
    sli_network_manager_id = NULL;
  }

  // Cleanup request queue if it exists
  if (sli_network_manager_request_queue != NULL) {
    if (osMessageQueueDelete(sli_network_manager_request_queue) != osOK) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to delete network manager request queue\r\n");
      return SL_STATUS_FAIL;
    }
    sli_network_manager_request_queue = NULL;
  }

  // Cleanup response flags if it exists
  if (sli_network_manager_response_flags != NULL) {
    if (osEventFlagsDelete(sli_network_manager_response_flags) != osOK) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to delete network manager response flags\r\n");
      return SL_STATUS_FAIL;
    }
    sli_network_manager_response_flags = NULL;
  }

  // Cleanup auto-join retry timer if it exists
  if (sli_auto_join_retry_timer_id != NULL) {
    (void)osTimerStop(sli_auto_join_retry_timer_id);
    if (osTimerDelete(sli_auto_join_retry_timer_id) != osOK) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to delete auto-join retry timer\r\n");
      return SL_STATUS_FAIL;
    }
    sli_auto_join_retry_timer_id = NULL;
  }

  // Reset retry context
  sli_reset_auto_join_retry_ctx();

  return SL_STATUS_OK;
}

// Partition function for QuickSort
static int sli_partition_profile_ids(uint8_t profile_ids[], uint8_t priorities[], int low, int high)
{
  uint8_t pivot = priorities[high]; // Choose the last element as the pivot
  int i         = low - 1;

  for (int j = low; j < high; j++) {
    if (priorities[j] < pivot) { // Compare priorities
      i++;
      // Swap profile IDs
      uint8_t temp_id = profile_ids[i];
      profile_ids[i]  = profile_ids[j];
      profile_ids[j]  = temp_id;

      // Swap priorities
      uint8_t temp_priority = priorities[i];
      priorities[i]         = priorities[j];
      priorities[j]         = temp_priority;
    }
  }

  // Swap profile IDs and priorities for the pivot
  uint8_t temp_id    = profile_ids[i + 1];
  profile_ids[i + 1] = profile_ids[high];
  profile_ids[high]  = temp_id;

  uint8_t temp_priority = priorities[i + 1];
  priorities[i + 1]     = priorities[high];
  priorities[high]      = temp_priority;

  return i + 1; // Return the pivot index
}

static void sli_quicksort_profile_ids(uint8_t profile_ids[], uint8_t priorities[], int low, int high)
{
  if (low < high) {
    // Partition the array and get the pivot index
    int pivot_index = sli_partition_profile_ids(profile_ids, priorities, low, high);

    // Recursively sort elements before and after the partition
    sli_quicksort_profile_ids(profile_ids, priorities, low, pivot_index - 1);
    sli_quicksort_profile_ids(profile_ids, priorities, pivot_index + 1, high);
  }
  return;
}

void sli_notify_net_event_handler(sl_net_event_t event, sl_status_t status, void *data, uint32_t data_size)
{
  if (net_event_handler) {
    net_event_handler(event, status, data, data_size);
  }
  return;
}

static void sli_reset_auto_join_retry_ctx(void)
{
  if (sli_auto_join_retry_timer_id != NULL) {
    (void)osTimerStop(sli_auto_join_retry_timer_id);
  }
  memset(&sli_auto_join_retry_ctx, 0, sizeof(sli_auto_join_retry_ctx));
  return;
}

// Timer callback only enqueues SLI_NET_AUTO_JOIN_RETRY_Q_EVENT; the manager thread
// performs the actual retry, so the delay (e.g. 30 s) does not block the manager.
static void sli_auto_join_retry_timer_cb(const void *argument)
{
  UNUSED_PARAMETER(argument);

  if (sli_network_manager_request_queue == NULL) {
    return;
  }

  sli_network_manager_message_t message = { 0 };
  message.event_flags                   = SLI_NET_AUTO_JOIN_RETRY_Q_EVENT;

  if (osMessageQueuePut(sli_network_manager_request_queue, &message, SLI_NET_MSG_PRIO_NORMAL, 0) != osOK) {
    SL_DEBUG_LOG_V2(DEBUG, "Failed to enqueue auto-join retry event\r\n");
  }
  return;
}

static int sli_get_iterate_profiles_count()
{
  sl_wifi_performance_profile_v2_t performance_profile;
  sl_status_t status = sl_wifi_get_performance_profile_v2(&performance_profile);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(DEBUG, "\r\nFailed to get performance profile: 0x%lx\r\n", status);
    return 1;
  }
  if (performance_profile.profile == SL_WIFI_SYSTEM_HIGH_PERFORMANCE) {
    return AUTO_JOIN_RETRY_COUNT;
  } else {
    return 1;
  }
}

/**
* @brief Attempts to connect to Wi-Fi profiles in the sorted list.
*
* @param sorted_profile_ids Array of sorted profile IDs.
* @param event The network event to notify.
* @param status Pointer to store the connection status.
* @return true if a connection is successful, false otherwise.
*/
static sl_status_t sli_fetch_and_sort_profiles(sl_net_interface_t interface,
                                               uint8_t sorted_profile_ids[],
                                               uint8_t priorities[])
{
  sl_status_t status;

  // Fetch all profile IDs and their priorities
  for (uint8_t profile_id = 0; profile_id < MAX_WIFI_CLIENT_PROFILES; profile_id++) {
    sl_net_wifi_client_profile_t profile;
    status = sl_net_get_profile(interface, profile_id, &profile);
    if (status == SL_STATUS_OK) {
      sorted_profile_ids[profile_id] = profile_id;       // Store the profile ID
      priorities[profile_id]         = profile.priority; // Store the priority
    } else {
      // If fetching a profile fails, set priority to a high value to deprioritize it
      sorted_profile_ids[profile_id] = profile_id;
      priorities[profile_id]         = UINT8_MAX;
    }
  }

  // Sort profile IDs based on their priorities using QuickSort
  sli_quicksort_profile_ids(sorted_profile_ids, priorities, 0, MAX_WIFI_CLIENT_PROFILES - 1);

  return SL_STATUS_OK;
}

static bool sli_attempt_connection_to_profiles(const uint8_t sorted_profile_ids[], sl_status_t *status)
{
  for (int i = 0; i < MAX_WIFI_CLIENT_PROFILES; i++) {
    *status = sl_net_up(SL_NET_WIFI_CLIENT_INTERFACE, sorted_profile_ids[i]); // Use profile ID
    if (*status == SL_STATUS_OK) {
      SL_DEBUG_LOG_V2(DEBUG, "\r\nSuccess to set up Wi-Fi for Profile ID %d\r\n", sorted_profile_ids[i]);
      return true;
    } else {
      SL_DEBUG_LOG_V2(DEBUG, "\r\nFailed to set up Wi-Fi for Profile ID %d: 0x%lx\r\n", sorted_profile_ids[i], *status);
      if (*status == SL_STATUS_SI91X_DHCP_HANDSHAKE_FAILURE) {
        break;
      }
    }
  }
  return false;
}

static bool sli_connect_to_sorted_wifi_profiles(const uint8_t sorted_profile_ids[],
                                                int iterate_profiles_count,
                                                sl_net_event_t event,
                                                sl_net_interface_t interface,
                                                sl_status_t *status)
{
  bool ap_connected = false;

  ap_connected = sli_attempt_connection_to_profiles(sorted_profile_ids, status);
  if (ap_connected) {
    sli_reset_auto_join_retry_ctx();
    return true;
  }

  if (*status == SL_STATUS_SI91X_DHCP_HANDSHAKE_FAILURE) {
    *status = sl_net_down(SL_NET_WIFI_CLIENT_INTERFACE);
    if (*status != SL_STATUS_OK) {
      return ap_connected;
    }
  }

  if (iterate_profiles_count <= 1) {
    sli_reset_auto_join_retry_ctx();
    return ap_connected;
  }

  if (sli_auto_join_retry_timer_id != NULL) {
    sli_auto_join_retry_ctx.active = true;
    memcpy(sli_auto_join_retry_ctx.sorted_profile_ids,
           sorted_profile_ids,
           sizeof(sli_auto_join_retry_ctx.sorted_profile_ids));
    sli_auto_join_retry_ctx.remaining_attempts = iterate_profiles_count - 1;
    sli_auto_join_retry_ctx.event              = event;
    sli_auto_join_retry_ctx.interface          = interface;

    if (osTimerStart(sli_auto_join_retry_timer_id, SLI_SYSTEM_MS_TO_TICKS(SLI_NET_AUTO_JOIN_RETRY_DELAY_MS)) != osOK) {
      SL_DEBUG_LOG_V2(DEBUG, "Failed to start auto-join retry timer\r\n");
      sli_reset_auto_join_retry_ctx();
    }
  }

  return ap_connected;
}

static bool sli_handle_disconnect_or_failure_event(const sli_network_manager_message_t *message, sl_net_event_t event)
{
  sl_status_t status = SL_STATUS_OK;
  uint8_t sorted_profile_ids[MAX_WIFI_CLIENT_PROFILES];
  uint8_t priorities[MAX_WIFI_CLIENT_PROFILES];

  if (message->event_flags != SLI_NET_DISCONNECT_Q_EVENT && message->event_flags != SLI_NET_CONNECT_FAILURE_Q_EVENT) {
    return false; // Not a disconnect or failure event
  }

  // Cancel any pending retry state before starting a new sequence
  sli_reset_auto_join_retry_ctx();

  // Notify user that auto-join is in progress
  if (net_event_handler) {
    sl_net_auto_join_status_t join_status = SL_NET_AUTO_JOIN_IN_PROGRESS;
    sli_notify_net_event_handler(event, status, &join_status, sizeof(int));
  }

  status = sli_fetch_and_sort_profiles(message->interface, sorted_profile_ids, priorities);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "\r\nFailed to fetch and sort profiles: 0x%lx\r\n", status);
    return true;
  }

  int iterate_profiles_count = sli_get_iterate_profiles_count();
  bool ap_connected =
    sli_connect_to_sorted_wifi_profiles(sorted_profile_ids, iterate_profiles_count, event, message->interface, &status);

  if (ap_connected || !sli_auto_join_retry_ctx.active) {
    if (sli_sync_auto_join_waiting) {
      uint32_t ack_flag = ap_connected ? SLI_NET_RSP_FLAG_AUTO_JOIN_SUCCESS : SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE;
      // ponytail: real flag errors are the 0xFFFFFFFx family — all carry the
      // osFlagsError indicator bit, so test with `&` (an `==` never fires).
      if (osEventFlagsSet(sli_network_manager_response_flags, ack_flag) & (uint32_t)osFlagsError) {
        SL_DEBUG_LOG_V2(DEBUG,
                        "Failed to signal auto-join %s ACK\n",
                        ap_connected ? (uintptr_t) "success" : (uintptr_t) "failure");
      }
    }

    if (net_event_handler) {
      sl_net_auto_join_status_t join_status = ap_connected ? SL_NET_AUTO_JOIN_CONNECTED : SL_NET_AUTO_JOIN_FAILED;
      sli_notify_net_event_handler(event, status, &join_status, sizeof(int));
    }
  }
  return ap_connected;
}

// Handles SLI_NET_AUTO_JOIN_RETRY_Q_EVENT: performs one retry attempt from sli_auto_join_retry_ctx.
static void sli_handle_auto_join_retry_event(void)
{
  if (!sli_auto_join_retry_ctx.active) {
    return;
  }

  sl_status_t status = SL_STATUS_FAIL;
  bool ap_connected  = sli_attempt_connection_to_profiles(sli_auto_join_retry_ctx.sorted_profile_ids, &status);

  if (ap_connected) {
    if (sli_sync_auto_join_waiting
        && (osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_AUTO_JOIN_SUCCESS)
              & (uint32_t)osFlagsError)) {
      SL_DEBUG_LOG_V2(DEBUG, "Failed to signal auto-join success ACK\r\n");
    }
    if (net_event_handler) {
      sl_net_auto_join_status_t join_status = SL_NET_AUTO_JOIN_CONNECTED;
      sli_notify_net_event_handler(sli_auto_join_retry_ctx.event, status, &join_status, sizeof(int));
    }
    sli_reset_auto_join_retry_ctx();
    return;
  }

  if (status == SL_STATUS_SI91X_DHCP_HANDSHAKE_FAILURE) {
    status = sl_net_down(SL_NET_WIFI_CLIENT_INTERFACE);
    if (status != SL_STATUS_OK) {
      sli_reset_auto_join_retry_ctx();
      return;
    }
  }

  if (sli_auto_join_retry_ctx.remaining_attempts <= 1) {
    if (sli_sync_auto_join_waiting
        && (osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE)
              & (uint32_t)osFlagsError)) {
      SL_DEBUG_LOG_V2(DEBUG, "Failed to signal auto-join failure ACK\r\n");
    }
    if (net_event_handler) {
      sl_net_auto_join_status_t join_status = SL_NET_AUTO_JOIN_FAILED;
      sli_notify_net_event_handler(sli_auto_join_retry_ctx.event, status, &join_status, sizeof(int));
    }
    sli_reset_auto_join_retry_ctx();
    return;
  }

  sli_auto_join_retry_ctx.remaining_attempts--;
  if (osTimerStart(sli_auto_join_retry_timer_id, SLI_SYSTEM_MS_TO_TICKS(SLI_NET_AUTO_JOIN_RETRY_DELAY_MS)) != osOK) {
    SL_DEBUG_LOG_V2(DEBUG, "Failed to restart auto-join retry timer\r\n");
    if (sli_sync_auto_join_waiting) {
      (void)osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE);
    }
    if (net_event_handler) {
      sl_net_auto_join_status_t join_status = SL_NET_AUTO_JOIN_FAILED;
      sli_notify_net_event_handler(sli_auto_join_retry_ctx.event, status, &join_status, sizeof(int));
    }
    sli_reset_auto_join_retry_ctx();
  }
  return;
}

// Helper to handle async WiFi connection success and start IP configuration
static void sli_handle_connect_event(const sli_network_manager_message_t *message)
{
  if (message == NULL) {
    return;
  }

  // Validate interface bounds to prevent buffer overflow
  if (message->interface >= SL_NET_INTERFACE_MAX) {
    SL_DEBUG_LOG_V2(DEBUG, "Invalid interface index: %d\r\n", message->interface);
    return;
  }

  // Only process if in CONNECTING state
  if (sli_async_state[message->interface].state != SLI_NET_ASYNC_CONNECTING) {
    return;
  }

  SL_DEBUG_LOG_V2(DEBUG, "\r\nAsync WiFi connection completed, starting IP configuration\r\n");

  // Notify app that WiFi is connected
  if (net_event_handler) {
    sli_notify_net_event_handler(SL_NET_CONNECT_EVENT, SL_STATUS_OK, NULL, 0);
  }

#ifdef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
  // Start async IP configuration for LWIP stack.
  sl_net_profile_id_t profile_id = sli_async_state[message->interface].profile;
  sl_status_t status             = sli_start_async_ip_config(message->interface, profile_id);

  // Handle IP configuration failure - disconnect WiFi and notify
  if (status != SL_STATUS_OK && status != SL_STATUS_IN_PROGRESS) {
    SL_DEBUG_LOG_V2(ERROR, "IP configuration failed: 0x%lx, disconnecting WiFi\r\n", status);
    sl_status_t disconnect_status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
    if (disconnect_status != SL_STATUS_OK) {
      SL_DEBUG_LOG_V2(ERROR, "WiFi disconnect failed: 0x%lx\r\n", disconnect_status);
    }
    sli_async_state[message->interface].state = SLI_NET_ASYNC_IDLE;
    if (net_event_handler) {
      sli_notify_net_event_handler(SL_NET_IP_CONFIG_EVENT, status, NULL, 0);
    }
    return;
  }

  // Static IP configured successfully
  if (status == SL_STATUS_OK) {
    sli_async_state[message->interface].state = SLI_NET_ASYNC_CONNECTED;
    if (net_event_handler) {
      sli_notify_net_event_handler(SL_NET_IP_CONFIG_EVENT, SL_STATUS_OK, NULL, 0);
    }
    return;
  }

  // DHCP started, will complete asynchronously
  sli_async_state[message->interface].state = SLI_NET_ASYNC_IP_CONFIG;
  SL_DEBUG_LOG_V2(INFO, "DHCP configuration in progress\r\n");
#else
  // For offload stack - IP configuration need to be handled.
#endif
  return;
}

// Helper to process disconnect or connect failure events.
static void sli_handle_disconnect_or_connect_failure_event(const sli_network_manager_message_t *message)
{
  if (message == NULL) {
    return;
  }

  // Validate interface bounds to prevent buffer overflow
  if (message->interface >= SL_NET_INTERFACE_MAX) {
    SL_DEBUG_LOG_V2(DEBUG, "Invalid interface index: %d\r\n", message->interface);
    return;
  }

  const bool sync_auto_join = (sli_sync_client_state.profile == SL_NET_AUTO_JOIN);

  if (sync_auto_join) {
    if (sli_handle_disconnect_or_failure_event(message, SL_NET_AUTO_JOIN_EVENT)) {
      SL_DEBUG_LOG_V2(DEBUG, "\r\n Connected synchronously\r\n");
      sli_sync_client_state.state = SLI_NET_STATE_CONNECTED;
    } else if (sli_auto_join_retry_ctx.active) {
      sli_sync_client_state.state = SLI_NET_STATE_CONNECTING;
    } else {
      sli_sync_client_state.state = SLI_NET_STATE_DISCONNECTED;
    }
    return; // Done for sync auto-join path
  }

  // Handle async operations (not auto-join) - notify WiFi connection failure
  const bool is_async_operation = (sli_async_state[message->interface].state != SLI_NET_ASYNC_IDLE);
  if (is_async_operation && net_event_handler) {
    sli_notify_net_event_handler(SL_NET_CONNECT_EVENT, SL_STATUS_FAIL, NULL, 0);
    sli_async_state[message->interface].state = SLI_NET_ASYNC_IDLE; // Reset state on failure
  }
  return;
}

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
// Helper: determine client interface for offload stack
static sl_wifi_interface_t sli_get_client_interface(const sl_net_wifi_client_profile_t *profile)
{
  sl_wifi_interface_t client_interface = 0;

  if (profile->config.channel.band == SL_WIFI_BAND_2_4GHZ || profile->config.channel.band == SL_WIFI_AUTO_BAND) {
    client_interface = SL_WIFI_CLIENT_2_4GHZ_INTERFACE;
  } else if (profile->config.channel.band == SL_WIFI_BAND_5GHZ) {
    client_interface = SL_WIFI_CLIENT_5GHZ_INTERFACE;
  } else if (profile->config.channel.band == SL_WIFI_BAND_DUAL) {
    client_interface = SL_WIFI_CLIENT_DUAL_INTERFACE;
  }

  return client_interface;
}
#endif /* SLI_SI91X_OFFLOAD_NETWORK_STACK */

#ifdef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
// Helper: populate IP details after connect for LWIP hosted stack
static void sli_populate_lwip_ip_details(sl_net_wifi_client_profile_t *profile, sl_net_profile_id_t raw_profile_id)
{
  UNUSED_PARAMETER(raw_profile_id);
#if LWIP_IPV4 && LWIP_IPV6
  if ((profile->ip.type & SL_IPV4) == SL_IPV4) {
    ip_addr_t *addr;
    addr = &wifi_client_context->netif.ip_addr;
    memcpy(profile->ip.ip.v4.ip_address.bytes, &addr->u_addr.ip4.addr, sizeof(addr->u_addr.ip4.addr));
    addr = &wifi_client_context->netif.gw;
    memcpy(profile->ip.ip.v4.gateway.bytes, &addr->u_addr.ip4.addr, sizeof(addr->u_addr.ip4.addr));
    addr = &wifi_client_context->netif.netmask;
    memcpy(profile->ip.ip.v4.netmask.bytes, &addr->u_addr.ip4.addr, sizeof(addr->u_addr.ip4.addr));
  }
  if ((profile->ip.type & SL_IPV6) == SL_IPV6) {
    for (int i = 0; i < 4; i++) {
      profile->ip.ip.v6.link_local_address.value[i] = ntohl(wifi_client_context->netif.ip6_addr[0].u_addr.ip6.addr[i]);
      profile->ip.ip.v6.global_address.value[i]     = ntohl(wifi_client_context->netif.ip6_addr[1].u_addr.ip6.addr[i]);
      profile->ip.ip.v6.gateway.value[i]            = ntohl(wifi_client_context->netif.ip6_addr[2].u_addr.ip6.addr[i]);
    }
  }
#else /* LWIP_IPV4 && LWIP_IPV6 */
#if LWIP_IPV4
  u32_t *addr;
  addr = &wifi_client_context->netif.ip_addr.addr;
  memcpy(profile->ip.ip.v4.ip_address.bytes, addr, sizeof(*addr));
  addr = &wifi_client_context->netif.gw.addr;
  memcpy(profile->ip.ip.v4.gateway.bytes, addr, sizeof(*addr));
  addr = &wifi_client_context->netif.netmask.addr;
  memcpy(profile->ip.ip.v4.netmask.bytes, addr, sizeof(*addr));
#elif LWIP_IPV6
  for (int i = 0; i < 4; i++) {
    profile->ip.ip.v6.link_local_address.value[i] = ntohl(wifi_client_context->netif.ip6_addr[0].addr[i]);
    profile->ip.ip.v6.global_address.value[i]     = ntohl(wifi_client_context->netif.ip6_addr[1].addr[i]);
    profile->ip.ip.v6.gateway.value[i]            = ntohl(wifi_client_context->netif.ip6_addr[2].addr[i]);
  }
#endif /* LWIP_IPV6 */
#endif /* LWIP_IPV4 && LWIP_IPV6 */
  return;
}
#endif /* SLI_SI91X_LWIP_HOSTED_NETWORK_STACK */

// Helper: handle auto join request event
static void sli_handle_auto_join_event(const sli_network_manager_message_t *message)
{
  sl_status_t status = SL_STATUS_FAIL;
  sl_net_wifi_client_profile_t profile;
  memset(&profile, 0, sizeof(profile));

  sli_sync_client_state.profile = message->profile_id;
  sli_sync_client_state.state   = SLI_NET_STATE_CONNECTING;

  status = sl_net_get_profile(message->interface, SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID, &profile);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to get default Wi-Fi client profile.\r\n");
    sli_sync_client_state.state = SLI_NET_STATE_DISCONNECTED;
    if (sli_sync_auto_join_waiting
        && (osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE)
              & (uint32_t)osFlagsError)) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to signal auto-join failure ACK\r\n");
    }
    return;
  }

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
  sl_wifi_interface_t client_interface = sli_get_client_interface(&profile);
#else
  sl_wifi_interface_t client_interface = SL_WIFI_CLIENT_INTERFACE;
#endif /* SLI_SI91X_OFFLOAD_NETWORK_STACK */

  status = sl_wifi_connect(client_interface, &profile.config, SLI_WIFI_CONNECT_TIMEOUT);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to initiate Wi-Fi connection: 0x%lx\r\n", status);
    sli_network_manager_message_t retry_message = { 0 };
    retry_message.interface                     = message->interface;
    retry_message.event_flags                   = SLI_NET_CONNECT_FAILURE_Q_EVENT; // Set event flag for retry logic
    sli_sync_client_state.state                 = SLI_NET_STATE_DISCONNECTED;
    if (sli_handle_disconnect_or_failure_event(&retry_message, SL_NET_AUTO_JOIN_EVENT)) {
      SL_DEBUG_LOG_V2(DEBUG, "\r\n Connected via auto-join retry\r\n");
      sli_sync_client_state.state = SLI_NET_STATE_CONNECTED;
    } else {
      SL_DEBUG_LOG_V2(DEBUG, "\r\n Failed to connect via auto-join retry\r\n");
      sli_sync_client_state.state = SLI_NET_STATE_DISCONNECTED;
    }
    return;
  }
#ifdef SLI_SI91X_LWIP_HOSTED_NETWORK_STACK
  // Populate IP details depending on stack type
  sli_populate_lwip_ip_details(&profile, message->profile_id);
#endif /* SLI_SI91X_LWIP_HOSTED_NETWORK_STACK */

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
  // Determine the correct VAP ID based on the interface
  uint8_t vap_id = (message->interface == SL_NET_WIFI_CLIENT_1_INTERFACE) ? SL_SI91X_WIFI_CLIENT_VAP_ID
                                                                          : SL_SI91X_WIFI_CLIENT_VAP_ID_1;
  status         = sl_si91x_configure_ip_address(&profile.ip, vap_id);
  // Keep the connection on full or partial IP configuration success; only treat a total
  // IP configuration failure as a disconnect.
  if (!sli_net_is_ip_config_success(status)) {
    SL_DEBUG_LOG_V2(ERROR,
                    "Failed to configure IP address for Wi-Fi client interface: 0x%lx, VAP ID: %d\n",
                    message->interface,
                    vap_id);
    sli_sync_client_state.state = SLI_NET_STATE_DISCONNECTED;
    if (sli_sync_auto_join_waiting
        && (osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE)
              & (uint32_t)osFlagsError)) {
      SL_DEBUG_LOG_V2(ERROR, "Failed to signal auto-join failure ACK\r\n");
    }
    return;
  }
#endif

  status = sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE, SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID, &profile);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to set Wi-Fi client profile.\r\n");
    sli_sync_client_state.state = SLI_NET_STATE_DISCONNECTED;
  } else {
    SL_DEBUG_LOG_V2(DEBUG, "Successfully set Wi-Fi client profile.\r\n");
    sli_sync_client_state.state = SLI_NET_STATE_CONNECTED;
  }

  if (sli_sync_auto_join_waiting) {
    uint32_t ack_flag = (status == SL_STATUS_OK) ? SLI_NET_RSP_FLAG_AUTO_JOIN_SUCCESS
                                                 : SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE;
    if (osEventFlagsSet(sli_network_manager_response_flags, ack_flag) & (uint32_t)osFlagsError) {
      SL_DEBUG_LOG_V2(DEBUG, "Failed to signal final sync WiFi client ACK\r\n");
    }
  }
  return;
}

// Helper: handle termination
static void sli_handle_thread_terminate(const sli_network_manager_message_t *message)
{
  UNUSED_PARAMETER(message);
  SL_DEBUG_LOG_V2(INFO, "\r\n Terminating network manager thread\r\n");
  if (osEventFlagsSet(sli_network_manager_response_flags, SLI_NET_RSP_FLAG_THREAD_TERMINATE_ACK)
      & (uint32_t)osFlagsError) {
    SL_DEBUG_LOG_V2(DEBUG, "Failed to signal thread terminate ACK\r\n");
    // Continue anyway - thread must terminate
  }
  osThreadSuspend(sli_network_manager_id);
  return;
}

// Helper: handle async up request - WiFi Client
static void sli_handle_async_wifi_client_up(const sli_network_manager_message_t *message)
{
  // Validate interface bounds to prevent buffer overflow
  if (message->interface >= SL_NET_INTERFACE_MAX) {
    SL_DEBUG_LOG_V2(ERROR, "Invalid interface index: %d\r\n", message->interface);
    return;
  }

  sl_status_t status;
  sl_net_wifi_client_profile_t profile;
  memset(&profile, 0, sizeof(profile));

  // Get the client profile
  status = sl_net_get_profile(message->interface, message->profile_id, &profile);
  if (status != SL_STATUS_OK) {
    SL_DEBUG_LOG_V2(ERROR, "Failed to get Wi-Fi client profile: 0x%lx\r\n", status);
    sli_async_state[message->interface].state = SLI_NET_ASYNC_IDLE;
    if (net_event_handler) {
      // Send CONNECT_EVENT with failure status
      sli_notify_net_event_handler(SL_NET_CONNECT_EVENT, status, NULL, 0);
    }
    return;
  }

  // Initiate WiFi connection (non-blocking with timeout=0)
  status = sl_wifi_connect(SL_WIFI_CLIENT_INTERFACE, &profile.config, 0);

  if (status == SL_STATUS_IN_PROGRESS) {
    // WiFi connection initiated successfully (async)
    SL_DEBUG_LOG_V2(DEBUG, "WiFi connection initiated asynchronously\r\n");
    sli_async_state[message->interface].state = SLI_NET_ASYNC_CONNECTING;

  } else {
    // Failed to initiate WiFi connection
    SL_DEBUG_LOG_V2(ERROR, "Failed to initiate Wi-Fi connection: 0x%lx\r\n", status);
    sli_async_state[message->interface].state = SLI_NET_ASYNC_IDLE;
    if (net_event_handler) {
      // Send CONNECT_EVENT with failure status
      sli_notify_net_event_handler(SL_NET_CONNECT_EVENT, status, NULL, 0);
    }
  }
  return;
}

// Helper: handle async up request
static void sli_handle_async_up_event(const sli_network_manager_message_t *message)
{
  if (message == NULL) {
    return;
  }

  // Call interface-specific async up handler
  switch (SL_NET_INTERFACE_TYPE(message->interface)) {
    case SL_NET_WIFI_CLIENT_1_INTERFACE:
    case SL_NET_WIFI_CLIENT_2_INTERFACE:
      sli_handle_async_wifi_client_up(message);
      break;

    case SL_NET_WIFI_AP_1_INTERFACE:
    case SL_NET_WIFI_AP_2_INTERFACE:
      // Implement async AP bring-up later
      break;

    default:
      SL_DEBUG_LOG_V2(INFO, "Unsupported interface for async up: %d\r\n", message->interface);
      break;
  }
  return;
}

void sli_network_manager_event_handler(const void *arg)
{
  UNUSED_PARAMETER(arg);
  sli_network_manager_message_t message = { 0 };
  osStatus_t queue_status;

  while (1) {
    // Wait for messages.
    queue_status = osMessageQueueGet(sli_network_manager_request_queue, &message, NULL, osWaitForever);

    if (queue_status == osOK) {
      const uint32_t flags = message.event_flags;
      switch (flags) {
        case SLI_NET_DISCONNECT_Q_EVENT:
        case SLI_NET_CONNECT_FAILURE_Q_EVENT:
          sli_handle_disconnect_or_connect_failure_event(&message);
          break;
        case SLI_NET_CONNECT_Q_EVENT:
          sli_handle_connect_event(&message);
          break;
        case SLI_NET_AUTO_JOIN_Q_EVENT:
          sli_handle_auto_join_event(&message);
          break;
        case SLI_NET_AUTO_JOIN_RETRY_Q_EVENT:
          sli_handle_auto_join_retry_event();
          break;
        case SLI_NET_ASYNC_UP_Q_EVENT:
          sli_handle_async_up_event(&message);
          break;
        case SLI_NET_THREAD_TERMINATE_Q_EVENT:
          sli_handle_thread_terminate(&message);
          break;
        default:
          SL_DEBUG_LOG_V2(INFO, "Unknown event flag received at NW Manager: 0x%lx\r\n", flags);
          break;
      }
    } else {
      // ponytail: with osWaitForever the only non-osOK outcome is an error —
      // osMessageQueueGet returns instantly (never blocks) when the queue
      // object is dead/NULL (teardown churn during 0x19 recovery). Without
      // this guard the loop spins at this thread's priority forever.
      SL_DEBUG_LOG_V2(ERROR, "network manager: queue get failed %d, backing off\r\n", (int)queue_status);
      osDelay(10);
    }
  }
}

sl_status_t sli_network_manager_auto_join_request(sl_net_interface_t interface, sl_net_profile_id_t profile_id)
{
  if (profile_id != SL_NET_AUTO_JOIN) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (sli_network_manager_request_queue == NULL || sli_network_manager_response_flags == NULL) {
    return SL_STATUS_FAIL; // network manager not initialized
  }
  sli_network_manager_message_t message = { 0 };
  message.interface                     = interface;
  message.event_flags                   = SLI_NET_AUTO_JOIN_Q_EVENT;
  message.profile_id                    = profile_id;
  // Mark that a synchronous caller is now waiting for the ACK on the response flags
  sli_sync_auto_join_waiting = true;
  (void)osEventFlagsClear(sli_network_manager_response_flags,
                          SLI_NET_RSP_FLAG_AUTO_JOIN_SUCCESS | SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE);
  osStatus_t put_status = osMessageQueuePut(sli_network_manager_request_queue, &message, SLI_NET_MSG_PRIO_NORMAL, 0);
  if (put_status != osOK) {
    sli_sync_auto_join_waiting = false;
    return SL_STATUS_FAIL;
  }
  uint32_t ack_flags = osEventFlagsWait(sli_network_manager_response_flags,
                                        SLI_NET_RSP_FLAG_AUTO_JOIN_SUCCESS | SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE,
                                        osFlagsWaitAny,
                                        osWaitForever);
  if ((ack_flags & (uint32_t)osFlagsError) != 0u) {
    SL_DEBUG_LOG_V2(DEBUG, "Auto-join wait aborted (e.g. network manager deinitialized)\r\n");
    sli_sync_auto_join_waiting = false;
    return SL_STATUS_ABORT;
  }
  if ((ack_flags & SLI_NET_RSP_FLAG_AUTO_JOIN_SUCCESS) != 0u) {
    SL_DEBUG_LOG_V2(DEBUG, "Auto-join process completed.\r\n");
    sli_sync_auto_join_waiting = false;
    return SL_STATUS_OK;
  } else if ((ack_flags & SLI_NET_RSP_FLAG_AUTO_JOIN_FAILURE) != 0u) {
    SL_DEBUG_LOG_V2(DEBUG, "Auto-join process failed.\r\n");
    sli_sync_auto_join_waiting = false;
    return SL_STATUS_FAIL;
  }
  sli_sync_auto_join_waiting = false;
  return SL_STATUS_FAIL;
}

sl_status_t sli_net_up_async_start(sl_net_interface_t interface, sl_net_profile_id_t profile_id)
{

  // Validate interface bounds to prevent buffer overflow
  if (interface >= SL_NET_INTERFACE_MAX) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Validate profile_id
  if (profile_id >= MAX_WIFI_CLIENT_PROFILES) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Check if network manager is initialized
  if (sli_network_manager_request_queue == NULL) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  // Check if interface is already processing an async operation
  if (sli_async_state[interface].state != SLI_NET_ASYNC_IDLE) {
    return SL_STATUS_BUSY;
  }

  // Set async state to START and store profile
  sli_async_state[interface].state   = SLI_NET_ASYNC_START;
  sli_async_state[interface].profile = profile_id;

  // Prepare message for network manager thread
  sli_network_manager_message_t message = { 0 };
  message.interface                     = interface;
  message.profile_id                    = profile_id;
  message.event_flags                   = SLI_NET_ASYNC_UP_Q_EVENT;

  // Post message to network manager queue
  osStatus_t put_status = osMessageQueuePut(sli_network_manager_request_queue, &message, SLI_NET_MSG_PRIO_NORMAL, 0);
  if (put_status != osOK) {
    // Revert state on failure
    sli_async_state[interface].state = SLI_NET_ASYNC_IDLE;
    return SL_STATUS_FAIL;
  }

  SL_DEBUG_LOG_V2(DEBUG, "Async up request posted for interface %d, profile %d\r\n", interface, profile_id);
  return SL_STATUS_IN_PROGRESS;
}
