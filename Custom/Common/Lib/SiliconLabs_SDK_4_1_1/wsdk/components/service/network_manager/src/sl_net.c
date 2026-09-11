/***************************************************************************/ /**
 * @file  sl_net.c
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

#include "sl_net.h"
#include "sl_net_constants.h"
#include "sl_wifi_device.h"
#include "sli_wifi.h"
#include "sli_wifi_utility.h"
#include "sl_net_default_values.h"
#include "sli_net_common_utility.h"
#include "sli_buffer_manager.h"
#ifdef SLI_NVM3_CONFIG_MANAGER
#include "nvm3_default.h"
#endif

#ifndef NETWORK_INTERFACE_VALID
#error Need to define NETWORK_INTERFACE_VALID in sl_board_configuration.h
#endif

#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_1_INTERFACE) || NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_1_INTERFACE) \
  || NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_2_INTERFACE) || NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_2_INTERFACE)
#include "sl_wifi_device.h"
#endif

// Global array to track the initialization state of each network interface
bool sl_net_interface_initialized[SL_NET_INTERFACE_MAX] = { false };

#ifdef SLI_NVM3_CONFIG_MANAGER
static bool nvm3_default_initialized = false;
#endif

extern bool device_initialized;
// Helper function to check if any network interface is initialized
static bool sli_is_any_interface_initialized(void)
{
  for (size_t i = 0; i < SL_NET_INTERFACE_MAX; i++) {
    if (sl_net_interface_initialized[i]) {
      return true;
    }
  }
  return false;
}

#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_1_INTERFACE) || NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_2_INTERFACE)
static sl_status_t sli_init_wifi_client_interface(sl_net_interface_t interface,
                                                  const void *configuration,
                                                  void *network_context,
                                                  sl_net_event_handler_t event_handler)
{
  sl_status_t status = SL_STATUS_OK;
  if (configuration == NULL) {
    configuration = (const void *)&sl_wifi_default_client_configuration;
  }
  status = sl_net_wifi_client_init(interface, configuration, network_context, event_handler);
  VERIFY_STATUS_AND_RETURN(status);

  sl_net_profile_id_t profile_id = SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID;

#ifdef SLI_NVM3_CONFIG_MANAGER
  // Initialize NVM3 once for credentials and profiles (shared by all interfaces)
  if (!nvm3_default_initialized) {
    status = nvm3_initDefault();
    VERIFY_STATUS_AND_RETURN(status);
    nvm3_default_initialized = true;
  }

  // When NVM3 is enabled, use NVM profile + NVM credential only when both are valid in NVM.
  // If either is missing/corrupt, set both to defaults from sl_net_default_values.h so the device can connect (no mismatch).
  sl_net_wifi_client_profile_t stored_profile;
  bool is_nvm_profile_valid;
  if (DEFAULT_WIFI_CLIENT_PROFILE.config.ssid.length > 0) {
    is_nvm_profile_valid =
      (sl_net_get_profile(interface, profile_id, (sl_net_profile_t *)&stored_profile) == SL_STATUS_OK)
      && (sli_net_validate_sl_net_profile((const sl_net_profile_t *)&stored_profile, interface) == SL_STATUS_OK);
  } else {
    is_nvm_profile_valid =
      true; // No default profile is defined in sl_net_default_values.h, so consider NVM profile as valid
  }
  bool is_nvm_credential_valid;
  if (default_wifi_client_credential.data_length > 0) {
    sl_net_credential_type_t cred_type;
    uint8_t cred_buf[SL_WIFI_MAX_PSK_LENGTH];
    uint32_t cred_len = sizeof(cred_buf);
    is_nvm_credential_valid =
      (sl_net_get_credential(SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID, &cred_type, cred_buf, &cred_len)
       == SL_STATUS_OK);
  } else {
    is_nvm_credential_valid =
      true; // No default credential is defined in sl_net_default_values.h, so consider NVM credential as valid
  }
  if ((is_nvm_profile_valid == false) || (is_nvm_credential_valid == false)) {
    // If either profile or credential present in NVM is invalid, then overwrite both values in NVM with the corresponding default values present in sl_net_default_values.h file.
    if (default_wifi_client_credential.data_length > 0) {
      status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID,
                                     default_wifi_client_credential.type,
                                     (const void *)default_wifi_client_credential.data,
                                     default_wifi_client_credential.data_length);
      VERIFY_STATUS_AND_RETURN(status);
    }
    if (DEFAULT_WIFI_CLIENT_PROFILE.config.ssid.length > 0) {
      status = sl_net_set_profile(interface, profile_id, (const sl_net_profile_t *)&DEFAULT_WIFI_CLIENT_PROFILE);
      VERIFY_STATUS_AND_RETURN(status);
    }
  }
#else
  if (default_wifi_client_credential.data_length > 0) {
    status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID,
                                   default_wifi_client_credential.type,
                                   (const void *)default_wifi_client_credential.data,
                                   default_wifi_client_credential.data_length);
    VERIFY_STATUS_AND_RETURN(status);
  }
  if (DEFAULT_WIFI_CLIENT_PROFILE.config.ssid.length > 0) {
    status = sl_net_set_profile(interface, profile_id, (const sl_net_profile_t *)&DEFAULT_WIFI_CLIENT_PROFILE);
    VERIFY_STATUS_AND_RETURN(status);
  }
#endif

  sl_net_interface_initialized[interface] = true;
  return status;
}
#endif

#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_1_INTERFACE) || NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_2_INTERFACE)
static sl_status_t sli_init_wifi_ap_interface(sl_net_interface_t interface,
                                              const void *configuration,
                                              const void *network_context,
                                              sl_net_event_handler_t event_handler)
{
  sl_status_t status = SL_STATUS_OK;
  if (configuration == NULL) {
    configuration = (const void *)&sl_wifi_default_ap_configuration;
  }
  status = sl_net_wifi_ap_init(interface, configuration, network_context, event_handler);
  VERIFY_STATUS_AND_RETURN(status);

  sl_net_profile_id_t profile_id = SL_NET_DEFAULT_WIFI_AP_PROFILE_ID;

  sl_net_wifi_ap_profile_t ap_profile = DEFAULT_WIFI_ACCESS_POINT_PROFILE;

  sl_wifi_device_configuration_t device_config = *(const sl_wifi_device_configuration_t *)configuration;

  uint32_t max_clients = ((device_config.boot_config.custom_feature_bit_map >> 13) & 0x0Fu);

  // If the SL_WIFI_CUSTOM_FEAT_MAX_NUM_OF_CLIENTS(x) bit is enabled in the device configuration,
  // the AP profile must be set to support fewer or equal to 'x ' clients. If the configured number of clients
  // exceeds this limit, the sl_net_set_profile function will return an error.
  if (max_clients != 0 && max_clients < ap_profile.config.maximum_clients) {
    ap_profile.config.maximum_clients = (uint8_t)max_clients;
  }

#ifdef SLI_NVM3_CONFIG_MANAGER
  // Initialize NVM3 once for credentials and profiles (shared by all interfaces)
  if (!nvm3_default_initialized) {
    status = nvm3_initDefault();
    VERIFY_STATUS_AND_RETURN(status);
    nvm3_default_initialized = true;
  }

  // When NVM3 is enabled, use NVM profile + NVM credential only when both are valid in NVM.
  // If either is missing/corrupt, set both to defaults from sl_net_default_values.h so the device can connect (no mismatch).
  sl_net_wifi_ap_profile_t stored_ap_profile;
  bool is_nvm_profile_valid;
  if (DEFAULT_WIFI_ACCESS_POINT_PROFILE.config.ssid.length > 0) {
    is_nvm_profile_valid =
      (sl_net_get_profile(interface, profile_id, (sl_net_profile_t *)&stored_ap_profile) == SL_STATUS_OK)
      && (sli_net_validate_sl_net_profile((const sl_net_profile_t *)&stored_ap_profile, interface) == SL_STATUS_OK);
  } else {
    is_nvm_profile_valid =
      true; // No default profile is defined in sl_net_default_values.h, so consider NVM profile as valid
  }
  bool is_nvm_credential_valid;
  if (default_wifi_ap_credential.data_length > 0) {
    sl_net_credential_type_t cred_type;
    uint8_t cred_buf[SL_WIFI_MAX_PSK_LENGTH];
    uint32_t cred_len = sizeof(cred_buf);
    is_nvm_credential_valid =
      (sl_net_get_credential(SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID, &cred_type, cred_buf, &cred_len) == SL_STATUS_OK);
  } else {
    is_nvm_credential_valid =
      true; // No default credential is defined in sl_net_default_values.h, so consider NVM credential as valid
  }
  if ((is_nvm_profile_valid == false) || (is_nvm_credential_valid == false)) {
    // If either profile or credential present in NVM is invalid, then overwrite both values in NVM with the corresponding default values present in sl_net_default_values.h file.
    if (default_wifi_ap_credential.data_length > 0) {
      status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID,
                                     default_wifi_ap_credential.type,
                                     (const void *)default_wifi_ap_credential.data,
                                     default_wifi_ap_credential.data_length);
      VERIFY_STATUS_AND_RETURN(status);
    }
    if (DEFAULT_WIFI_ACCESS_POINT_PROFILE.config.ssid.length > 0) {
      status = sl_net_set_profile(interface, profile_id, (const sl_net_profile_t *)&ap_profile);
      VERIFY_STATUS_AND_RETURN(status);
    }
  }
#else
  if (default_wifi_ap_credential.data_length > 0) {
    status = sl_net_set_credential(SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID,
                                   default_wifi_ap_credential.type,
                                   (const void *)default_wifi_ap_credential.data,
                                   default_wifi_ap_credential.data_length);
    VERIFY_STATUS_AND_RETURN(status);
  }
  if (DEFAULT_WIFI_ACCESS_POINT_PROFILE.config.ssid.length > 0) {
    status = sl_net_set_profile(interface, profile_id, (const sl_net_profile_t *)&ap_profile);
    VERIFY_STATUS_AND_RETURN(status);
  }
#endif

  sl_net_interface_initialized[interface] = true;
  return status;
}
#endif

sl_status_t sl_net_init(sl_net_interface_t interface,
                        const void *configuration,
                        void *network_context,
                        sl_net_event_handler_t event_handler)
{
  sl_status_t status = SL_STATUS_OK;
  // Check if the interface is valid and already initialized
  if (!(NETWORK_INTERFACE_VALID(interface))) {
    return SL_STATUS_NOT_SUPPORTED;
  }
  if (sl_net_interface_initialized[interface]) {
    return SL_STATUS_ALREADY_INITIALIZED;
  }

  // Initialize network manager (shared across all interfaces)
  // SL_STATUS_ALREADY_INITIALIZED is expected and acceptable when initializing subsequent interfaces
  status = sli_network_manager_init();
  if (status != SL_STATUS_OK && status != SL_STATUS_ALREADY_INITIALIZED) {
    return status;
  }

  switch (SL_NET_INTERFACE_TYPE(interface)) {
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_1_INTERFACE)
    case SL_NET_WIFI_CLIENT_1_INTERFACE:
      status =
        sli_init_wifi_client_interface(SL_NET_WIFI_CLIENT_1_INTERFACE, configuration, network_context, event_handler);
      break;
#endif

#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_2_INTERFACE)
    case SL_NET_WIFI_CLIENT_2_INTERFACE:
      status =
        sli_init_wifi_client_interface(SL_NET_WIFI_CLIENT_2_INTERFACE, configuration, network_context, event_handler);
      break;
#endif

#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_1_INTERFACE)
    case SL_NET_WIFI_AP_1_INTERFACE:
      status = sli_init_wifi_ap_interface(SL_NET_WIFI_AP_1_INTERFACE, configuration, network_context, event_handler);
      break;
#endif

#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_2_INTERFACE)
    case SL_NET_WIFI_AP_2_INTERFACE:
      status = sli_init_wifi_ap_interface(SL_NET_WIFI_AP_2_INTERFACE, configuration, network_context, event_handler);
      break;
#endif

    default:
      status = SL_STATUS_NOT_SUPPORTED;
      break;
  }
  // Clean up network manager thread if interface initialization failed
  // Only deinitialize if no other interface is using the network manager
  if (status != SL_STATUS_OK && !sli_is_any_interface_initialized()) {
    sli_network_manager_deinit();
  }

  return status;
}

sl_status_t sl_net_deinit(sl_net_interface_t interface)
{
  sl_status_t status = SL_STATUS_OK;
  // Check if the interface is valid and initialized
  if (!(NETWORK_INTERFACE_VALID(interface))) {
    return SL_STATUS_NOT_SUPPORTED;
  }
  if (!sl_net_interface_initialized[interface]) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  switch (SL_NET_INTERFACE_TYPE(interface)) {
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_1_INTERFACE)
    case SL_NET_WIFI_CLIENT_1_INTERFACE:
      status = sl_net_wifi_client_deinit(interface);
      break;
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_2_INTERFACE)
    case SL_NET_WIFI_CLIENT_2_INTERFACE:
      status = sl_net_wifi_client_deinit(interface);
      break;
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_1_INTERFACE)
    case SL_NET_WIFI_AP_1_INTERFACE:
      status = sl_net_wifi_ap_deinit(interface);
      break;
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_2_INTERFACE)
    case SL_NET_WIFI_AP_2_INTERFACE:
      status = sl_net_wifi_ap_deinit(interface);
      break;
#endif
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }

  if (status == SL_STATUS_OK) {
    // Set the initialized flag of every interface to false.
    for (size_t i = 0; i < SL_NET_INTERFACE_MAX; i++) {
      sl_net_interface_initialized[i] = false;
    }

    // Deinitialize network manager thread
    status = sli_network_manager_deinit();
  } else {
    /* ponytail: clear the per-interface flags on a FAILED teardown too.
     * Leaving them set after an aborted deinit pins every later sl_net_init
     * into the ALREADY_INITIALIZED short-circuit while the layers below are
     * half-torn — the recovery loop then fast-fails forever instead of
     * re-running a real init (field: 10 x 9ms cli_init attempts, all dead).
     * The network manager itself stays up; only a clean teardown takes it
     * down. */
    for (size_t i = 0; i < SL_NET_INTERFACE_MAX; i++) {
      sl_net_interface_initialized[i] = false;
    }
  }
  return status;
}

sl_status_t sl_net_up(sl_net_interface_t interface, sl_net_profile_id_t profile_id)
{
  // Check if the interface is valid and initialized
  if (!(NETWORK_INTERFACE_VALID(interface))) {
    return SL_STATUS_NOT_SUPPORTED;
  }
  if (!sl_net_interface_initialized[interface]) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  switch (SL_NET_INTERFACE_TYPE(interface)) {
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_1_INTERFACE)
    case SL_NET_WIFI_CLIENT_1_INTERFACE:
      return sl_net_wifi_client_up(interface, profile_id);
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_2_INTERFACE)
    case SL_NET_WIFI_CLIENT_2_INTERFACE:
      return sl_net_wifi_client_up(interface, profile_id);
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_1_INTERFACE)
    case SL_NET_WIFI_AP_1_INTERFACE:
      return sl_net_wifi_ap_up(interface, profile_id);
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_2_INTERFACE)
    case SL_NET_WIFI_AP_2_INTERFACE:
      return sl_net_wifi_ap_up(interface, profile_id);
#endif
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }
}

sl_status_t sl_net_up_async(sl_net_interface_t interface, sl_net_profile_id_t profile_id)
{
  // Check if the interface is valid and initialized
  if (!(NETWORK_INTERFACE_VALID(interface))) {
    return SL_STATUS_NOT_SUPPORTED;
  }
  if (!sl_net_interface_initialized[interface]) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  // Auto-join is not supported for async up
  if (profile_id == SL_NET_AUTO_JOIN) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  // Delegate to the internal helper to post async request
  return sli_net_up_async_start(interface, profile_id);
}

sl_status_t sl_net_down(sl_net_interface_t interface)
{
  // Check if the interface is valid and initialized
  if (!(NETWORK_INTERFACE_VALID(interface))) {
    return SL_STATUS_NOT_SUPPORTED;
  }
  if (!sl_net_interface_initialized[interface]) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  switch (SL_NET_INTERFACE_TYPE(interface)) {
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_1_INTERFACE)
    case SL_NET_WIFI_CLIENT_1_INTERFACE:
      return sl_net_wifi_client_down(interface);
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_CLIENT_2_INTERFACE)
    case SL_NET_WIFI_CLIENT_2_INTERFACE:
      return sl_net_wifi_client_down(interface);
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_1_INTERFACE)
    case SL_NET_WIFI_AP_1_INTERFACE:
      return sl_net_wifi_ap_down(interface);
#endif
#if NETWORK_INTERFACE_VALID(SL_NET_WIFI_AP_2_INTERFACE)
    case SL_NET_WIFI_AP_2_INTERFACE:
      return sl_net_wifi_ap_down(interface);
#endif
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }
}

sl_status_t sl_net_inet_addr(const char *addr, uint32_t *value)
{
  uint8_t ip_bytes[5] = { 0 };
  int i;
  int digits;
  int j;

  if ((NULL == addr) || (NULL == value)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  digits = 0;

  // Iterate through the characters in the IP address string
  for (i = 0, j = 0; 0 != addr[i]; i++) {
    if ('.' == addr[i]) {
      ++j;
      digits = 0;
      continue;
    }

    // Check if the character is a digit (0-9)
    if ((addr[i] < '0') || (addr[i] > '9')) {
      return SL_STATUS_INVALID_PARAMETER;
    }

    // Convert character to numeric value and update IP bytes
    ip_bytes[j] = (ip_bytes[j] * 10) + (uint8_t)(addr[i] - '0');

    digits++;
    if (digits > 3) {
      return SL_STATUS_INVALID_PARAMETER;
    }
  }

  // Ensure that there are exactly three '.' separators in the IP address
  if (j != 3) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Calculate the 32-bit integer value of the IP address
  *value = (uint32_t)ip_bytes[0];
  *value |= (uint32_t)(ip_bytes[1] << 8);
  *value |= (uint32_t)(ip_bytes[2] << 16);
  *value |= (uint32_t)(ip_bytes[3] << 24);

  return SL_STATUS_OK;
}

sl_status_t sl_net_nat_enable(const sl_net_nat_config_t *nat_config)
{
  if (nat_config == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sli_net_nat_config_t sli_nat_config = {
    .enable                  = 1,
    .tcp_session_timeout     = nat_config->tcp_session_timeout,
    .non_tcp_session_timeout = nat_config->non_tcp_session_timeout,
    .interface               = nat_config->interface,
  };

  return sli_net_nat_configure(&sli_nat_config);
}

sl_status_t sl_net_nat_disable(const sl_net_interface_t interface)
{

  sli_net_nat_config_t sli_nat_config = {
    .enable    = 0,
    .interface = interface,
  };

  return sli_net_nat_configure(&sli_nat_config);
}

sl_status_t sl_net_get_interface_info(sl_net_interface_t interface, sl_net_interface_info_t *info)
{
  return sli_net_get_interface_info(interface, info);
}
