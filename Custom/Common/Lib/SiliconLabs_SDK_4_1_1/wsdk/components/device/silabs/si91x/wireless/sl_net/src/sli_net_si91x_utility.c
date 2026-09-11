/***************************************************************************/ /**
 * @file sli_net_si91x_utility.c
 * @brief Internal network manager utility source file for SiWx91x implementation
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
#include "sl_status.h"
#include "sl_net_constants.h"
#include "sli_net_constants.h"
#include "sli_net_common_utility.h"
#include "sl_si91x_constants.h"
#include "sl_si91x_driver.h"
#include "sl_net_si91x.h"
#include "sl_wifi_constants.h"
#include "sl_constants.h"
#include "sli_wifi.h"
#include "sli_wifi_utility.h"
/******************************************************
 *                      Macros
 ******************************************************/
#define SLI_MAX_PRIVATE_KEY_LENGTH          4096
#define SLI_MAX_PRIVATE_KEY_METADATA_LENGTH 8
#define CRED_TYPE_CERT                      0
#define CRED_TYPE_CRED                      1
#define SLI_MIN(a, b)                       ((a) < (b) ? (a) : (b))
/******************************************************
 *                      Extern Variables
 ******************************************************/
extern bool device_initialized;

static uint8_t sli_get_certificate_index(sl_net_credential_id_t id)
{
  if ((id & SL_NET_CREDENTIAL_GROUP_MASK) != 0) {
    return ((uint8_t)id & 0xFF);
  }
  switch (id) {
    case SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID:
    case SL_NET_WIFI_EAP_SERVER_CREDENTIAL_ID:
      return 0;

    default:
      break;
  }
  return -1;
}

static sli_si91x_cert_type_t convert_to_si91x_cert_type(sl_net_credential_id_t id, sl_net_credential_type_t type)
{
  switch (type) {
    case SL_NET_SIGNING_CERTIFICATE:
      if ((id == SL_NET_WIFI_EAP_SERVER_CREDENTIAL_ID) || (id == SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID)) {
        return SLI_SI91X_EAP_CA_CERTIFICATE;
      }
      if ((id & SL_NET_CREDENTIAL_GROUP_MASK) == SL_NET_TLS_SERVER_CREDENTIAL_START) {
        return SLI_SI91X_TLS_CA_CERTIFICATE;
      }
      break;

    case SL_NET_CERTIFICATE:
      if (id == SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID) {
        return SLI_SI91X_EAP_CLIENT;
      }

      switch (id & SL_NET_CREDENTIAL_GROUP_MASK) {
        case SL_NET_TLS_CLIENT_CREDENTIAL_START:
          return SLI_SI91X_TLS_CLIENT;
        case SL_NET_TLS_SERVER_CREDENTIAL_START:
          return SLI_SI91X_TLS_SERVER_CERTIFICATE;
        default:
          break;
      }
      break;

    case SL_NET_PUBLIC_KEY:
      if (id == SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID) {
        return SLI_SI91X_EAP_PUBLIC_KEY;
      }
      break;

    case SL_NET_PRIVATE_KEY:
      if (id == SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID) {
        return SLI_SI91X_EAP_PRIVATE_KEY;
      }
      switch (id & SL_NET_CREDENTIAL_GROUP_MASK) {
        case SL_NET_TLS_CLIENT_CREDENTIAL_START:
          return SLI_SI91X_TLS_CLIENT_PRIVATE_KEY;
        case SL_NET_TLS_SERVER_CREDENTIAL_START:
          return SLI_SI91X_TLS_SERVER_PRIVATE_KEY;
        default:
          break;
      }
      break;
    case SL_NET_PACK_FILE:
      if (id == SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID) {
        return SLI_SI91X_FAST_PAC_FILE;
      }
      break;
    case SL_NET_TLS_PRIVATE_KEY_CBC_WRAP:
      if ((id & SL_NET_CREDENTIAL_GROUP_MASK) == SL_NET_TLS_CLIENT_CREDENTIAL_START) {
        return SLI_SI91X_TLS_CBC_PRIVATE_KEY;
      }
      break;
    case SL_NET_TLS_PRIVATE_KEY_ECB_WRAP:
      if ((id & SL_NET_CREDENTIAL_GROUP_MASK) == SL_NET_TLS_CLIENT_CREDENTIAL_START) {
        return SLI_SI91X_TLS_ECB_PRIVATE_KEY;
      }
      break;
    default:
      return 0;
  }

  return 0;
}

sl_status_t sli_si91x_set_credential(sl_net_credential_id_t id,
                                     sl_net_credential_type_t type,
                                     const void *credential,
                                     uint32_t credential_length)
{

  if ((credential == NULL) || (credential_length == 0)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // As the maximum length of the private key is 4096 bytes in NWP, the length of the credential/key should be less than this maximum supported length.
  if (((type == SL_NET_TLS_PRIVATE_KEY_ECB_WRAP) || (type == SL_NET_TLS_PRIVATE_KEY_CBC_WRAP))
      && (credential_length > (SLI_MAX_PRIVATE_KEY_LENGTH - SLI_MAX_PRIVATE_KEY_METADATA_LENGTH - SL_SI91X_IV_SIZE))) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status;
  sli_si91x_cert_type_t cert_type = convert_to_si91x_cert_type(id, type);
  uint8_t index                   = sli_get_certificate_index(id);

  // Clear the certificate
  status = sl_si91x_wifi_set_certificate_index((uint8_t)cert_type, index, NULL, 0);

  VERIFY_STATUS_AND_RETURN(status);
  // Set the certificate
  status = sl_si91x_wifi_set_certificate_index((uint8_t)cert_type, index, credential, credential_length);

  return status;
}

sl_status_t sli_si91x_get_credential(sl_net_credential_id_t id,
                                     const sl_net_credential_type_t *type,
                                     const void *credential,
                                     const uint32_t *credential_length)
{
  UNUSED_PARAMETER(id);
  UNUSED_PARAMETER(type);
  UNUSED_PARAMETER(credential);
  UNUSED_PARAMETER(credential_length);
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t sli_si91x_delete_credential(sl_net_credential_id_t id, sl_net_credential_type_t type)
{
  sli_si91x_cert_type_t cert_type = convert_to_si91x_cert_type(id, type);
  uint8_t index                   = sli_get_certificate_index(id);

  return sl_si91x_wifi_set_certificate_index((uint8_t)cert_type, index, NULL, 0);
}

sl_status_t sli_net_get_interface_info(sl_net_interface_t interface, sl_net_interface_info_t *info)
{
  sl_status_t status       = 0;
  sl_wifi_buffer_t *buffer = NULL;
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  SL_WIFI_ARGS_CHECK_NULL_POINTER(info);

  // Select command based on interface
  sli_wifi_request_commands_t command;
  switch (interface) {
    case SL_NET_WIFI_CLIENT_INTERFACE:
      command = (sli_wifi_request_commands_t)SLI_WIFI_REQ_QUERY_NETWORK_PARAMS;
      break;
    case SL_NET_WIFI_AP_INTERFACE:
      command = (sli_wifi_request_commands_t)SLI_WIFI_REQ_QUERY_GO_PARAMS;
      break;
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }

  status = sli_wifi_send_command(command,
                                 SLI_WIFI_WLAN_CMD,
                                 NULL,
                                 0,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SL_SI91X_GET_INTERFACE_INFO_TIMEOUT),
                                 NULL,
                                 (void **)&buffer);
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);
  sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
  memset(info, 0, sizeof(sl_net_interface_info_t));

  if (packet->length > 0) {
    if (command == SLI_WIFI_REQ_QUERY_GO_PARAMS) {
      // AP mode
      sli_wifi_client_info_response *response = (sli_wifi_client_info_response *)packet->data;
      // wlan state: no of stations connected in AP mode
      memcpy(&info->hw_info.wifi_info.wlan_state, (uint16_t *)&response->sta_count, sizeof(uint16_t));
      memcpy(&info->hw_info.wifi_info.channel_number, (uint16_t *)&response->channel_number, sizeof(uint16_t));
      memcpy(info->hw_info.wifi_info.ssid,
             response->ssid,
             SLI_MIN(sizeof(info->hw_info.wifi_info.ssid), sizeof(response->ssid)));
      memcpy(info->hw_info.wifi_info.mac_address, response->mac_address, SL_WIFI_MAC_ADDRESS_LENGTH);
      // PSK for AP mode, PMK for Client mode
      memcpy(info->hw_info.wifi_info.psk_pmk, response->psk, SL_WIFI_MAX_PSK_LENGTH);
      memcpy(info->ipv4_address.bytes, response->ipv4_address, SL_IPV4_ADDRESS_LENGTH);
      memcpy(info->ipv6_address.bytes, response->ipv6_address, SL_IPV6_ADDRESS_LENGTH);
    } else {
      // Station mode
      sli_si91x_network_params_response_t *response = (sli_si91x_network_params_response_t *)packet->data;
      memcpy(&info->hw_info.wifi_info.wlan_state, (uint16_t *)&response->wlan_state, sizeof(uint8_t));
      memcpy((uint8_t *)&info->hw_info.wifi_info.channel_number, &response->channel_number, sizeof(uint8_t));
      memcpy(info->hw_info.wifi_info.ssid,
             response->ssid,
             SLI_MIN(sizeof(info->hw_info.wifi_info.ssid), sizeof(response->ssid)));
      memcpy(info->hw_info.wifi_info.mac_address, response->mac_address, SL_WIFI_MAC_ADDRESS_LENGTH);
      memcpy(&info->hw_info.wifi_info.sec_type, &response->sec_type, sizeof(uint8_t));
      // PSK for AP mode, PMK for Client mode
      memcpy(info->hw_info.wifi_info.psk_pmk, response->psk, SL_WIFI_MAX_PSK_LENGTH);
      memcpy(info->ipv4_address.bytes, response->ipv4_address, SL_IPV4_ADDRESS_LENGTH);
      memcpy(info->ipv6_address.bytes, response->ipv6_address, SL_IPV6_ADDRESS_LENGTH);
      memcpy(info->hw_info.wifi_info.bssid, response->bssid, SL_WIFI_MAC_ADDRESS_LENGTH);
      memcpy(&info->hw_info.wifi_info.wireless_mode, &response->wireless_mode, sizeof(uint8_t));
    }
  }
  sli_buffer_manager_free_buffer(buffer);
  return status;
}

sl_status_t sli_net_nat_configure(const sli_net_nat_config_t *sli_nat_config)
{
  sl_status_t status = SL_STATUS_OK;

  if (sli_nat_config == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Check if both STA and AP interfaces are up
  if (!sli_wifi_is_interface_up(SL_WIFI_CLIENT_INTERFACE) || !sli_wifi_is_interface_up(SL_WIFI_AP_INTERFACE)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  // Send the NAT configure command to the driver
  status = sli_wifi_send_command(SLI_WIFI_REQ_NAT,
                                 SLI_SI91X_NETWORK_CMD,
                                 sli_nat_config,
                                 sizeof(sli_net_nat_config_t),
                                 SLI_WIFI_WAIT_FOR_COMMAND_RESPONSE,
                                 NULL,
                                 NULL);

  VERIFY_STATUS_AND_RETURN(status);
  return status;
}
