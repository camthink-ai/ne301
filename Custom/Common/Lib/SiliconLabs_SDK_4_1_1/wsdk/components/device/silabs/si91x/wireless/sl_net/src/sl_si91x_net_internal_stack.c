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
#include "sl_status.h"
#include "sl_utility.h"
#include "sl_net.h"
#include "sl_wifi.h"
#include "sl_net_wifi_types.h"
#include "sl_net_si91x.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_driver.h"
#include "sl_rsi_utility.h"
#include "sli_net_utility.h"
#include "sl_si91x_core_utilities.h"
#include <stdbool.h>
#include <string.h>
#include "sli_wifi_utility.h"
// Define a bit mask for DHCP unicast offer
#define SL_SI91X_DHCP_UNICAST_OFFER ((uint32_t)1U << 3)

// Global variable indicating if the device is initialized
extern bool device_initialized;

// Per-family IP configuration status captured during the last IP configuration attempt.
// Retrieved by the application via sl_wifi_get_ip_config_failure_reason().
//  - SL_STATUS_OK             : that family configured successfully
//  - firmware error code      : that family failed to configure
//  - SL_STATUS_NOT_AVAILABLE  : that family was not requested
static sl_status_t sli_ipv4_config_status = SL_STATUS_NOT_AVAILABLE;
static sl_status_t sli_ipv6_config_status = SL_STATUS_NOT_AVAILABLE;

static sl_status_t sli_send_client_ip_address_info_if_applicable(const sl_net_ip_configuration_t *ip_config,
                                                                 uint8_t virtual_ap_id)
{
  sl_status_t status = SL_STATUS_OK;

  if ((virtual_ap_id == SL_WIFI_CLIENT_VAP_ID) || (virtual_ap_id == SL_WIFI_CLIENT_VAP_ID_1)) {
    sli_wifi_ip_address_info_t ip_info = { 0 };

    if (ip_config->type & SL_IPV4) {
      ip_info.flags |= SLI_WIFI_IPV4_AVAILABLE;
      memcpy(ip_info.ipv4_address, ip_config->ip.v4.ip_address.bytes, sizeof(sl_ipv4_address_t));
    }

    if (ip_config->type & SL_IPV6) {
      ip_info.flags |= SLI_WIFI_IPV6_AVAILABLE;
      memcpy(ip_info.ipv6_address, ip_config->ip.v6.link_local_address.bytes, sizeof(sl_ipv6_address_t));
    }

    if (ip_info.flags != 0) {
      status = sli_wifi_send_ip_address_info(SL_WIFI_CLIENT_INTERFACE, &ip_info);
      SL_DEBUG_LOG_V2(DEBUG, "sli_wifi_send_ip_address_info status: 0x%lX", status);
    }
  }

  return status;
}

static sl_status_t sli_configure_ipv4_address(sl_net_ip_configuration_t *ip_config,
                                              uint8_t virtual_ap_id,
                                              uint32_t wait_time)
{
  sli_si91x_req_ipv4_params_t ip_req = { 0 };
  sl_wifi_buffer_t *buffer           = NULL;

  memset(&ip_req, 0, sizeof(ip_req));
  ip_req.vap_id = virtual_ap_id;

  if (SL_IP_MANAGEMENT_STATIC_IP == ip_config->mode) {
    ip_req.dhcp_mode = SLI_NET_STATIC_IP;
    memcpy(ip_req.ipaddress, ip_config->ip.v4.ip_address.bytes, SL_IPV4_ADDRESS_LENGTH);
    memcpy(ip_req.netmask, ip_config->ip.v4.netmask.bytes, SL_IPV4_ADDRESS_LENGTH);
    memcpy(ip_req.gateway, ip_config->ip.v4.gateway.bytes, SL_IPV4_ADDRESS_LENGTH);
  } else {
    ip_req.dhcp_mode = (SLI_NET_DHCP | SL_SI91X_DHCP_UNICAST_OFFER);
  }

  if (NULL != ip_config->host_name) {
    ip_req.dhcp_mode |= SLI_NET_DHCP_HOSTNAME;
    memcpy(ip_req.hostname, ip_config->host_name, sizeof(ip_req.hostname));
  }

  ip_req.dhcp_discover_rtr_interval_min = ip_config->dhcp_config.min_discover_retry_interval;
  ip_req.dhcp_request_rtr_interval_min  = ip_config->dhcp_config.min_request_retry_interval;
  ip_req.dhcp_discover_rtr_interval_max = ip_config->dhcp_config.max_discover_retry_interval;
  ip_req.dhcp_request_rtr_interval_max  = ip_config->dhcp_config.max_request_retry_interval;
  ip_req.dhcp_discover_max_retries      = ip_config->dhcp_config.max_discover_retries;
  ip_req.dhcp_request_max_retries       = ip_config->dhcp_config.max_request_retries;

  sli_ipv4_config_status = sli_wifi_send_command(SLI_WIFI_REQ_IPCONFV4,
                                                 SLI_SI91X_NETWORK_CMD,
                                                 &ip_req,
                                                 sizeof(sli_si91x_req_ipv4_params_t),
                                                 wait_time,
                                                 NULL,
                                                 (void **)&buffer);

  if (sli_ipv4_config_status == SL_STATUS_OK) {
    sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);

    if (SL_IP_MANAGEMENT_DHCP == ip_config->mode) {
      const sli_si91x_rsp_ipv4_params_t *response_data = (sli_si91x_rsp_ipv4_params_t *)packet->data;
      memcpy(ip_config->ip.v4.ip_address.bytes, (const uint8_t *)response_data->ipaddr, sizeof(sl_ipv4_address_t));
      memcpy(ip_config->ip.v4.netmask.bytes, (const uint8_t *)response_data->netmask, sizeof(sl_ipv4_address_t));
      memcpy(ip_config->ip.v4.gateway.bytes, (const uint8_t *)response_data->gateway, sizeof(sl_ipv4_address_t));
    }

    sli_buffer_manager_free_buffer(buffer);
    SL_DEBUG_LOG_V2(DEBUG, "sli_net_configure_ip_address: IPv4 OK (vap_id=%u)", virtual_ap_id);
  } else {
    if (buffer != NULL) {
      sli_buffer_manager_free_buffer(buffer);
    }
    SL_DEBUG_LOG_V2(WARN,
                    "sli_net_configure_ip_address: IPv4 failed (vap_id=%u, status=0x%lX)",
                    virtual_ap_id,
                    sli_ipv4_config_status);
  }

  return sli_ipv4_config_status;
}

static sl_status_t sli_configure_ipv6_address(sl_net_ip_configuration_t *ip_config,
                                              uint8_t virtual_ap_id,
                                              uint32_t wait_time)
{
  sli_si91x_req_ipv6_params_t ipv6_request = { 0 };
  sl_wifi_buffer_t *buffer                 = NULL;
  uint16_t prefix_length                   = 64;

  memset(&ipv6_request, 0, sizeof(ipv6_request));
  memcpy(&ipv6_request.prefixLength, &prefix_length, SLI_SI91X_2BYTE_FIELD_SIZE);
  ipv6_request.vap_id = virtual_ap_id;

  if (SL_IP_MANAGEMENT_STATIC_IP == ip_config->mode) {
    memcpy(&ipv6_request.ipaddr6, ip_config->ip.v6.global_address.bytes, SL_IPV6_ADDRESS_LENGTH);
    memcpy(&ipv6_request.gateway6, ip_config->ip.v6.gateway.bytes, SL_IPV6_ADDRESS_LENGTH);
    ipv6_request.mode[0] = SLI_NET_STATIC_IP;
  } else {
    ipv6_request.mode[0] = (SLI_NET_DHCP | SL_SI91X_DHCP_UNICAST_OFFER);
  }

  sli_ipv6_config_status = sli_wifi_send_command(SLI_WIFI_REQ_IPCONFV6,
                                                 SLI_SI91X_NETWORK_CMD,
                                                 &ipv6_request,
                                                 sizeof(sli_si91x_req_ipv6_params_t),
                                                 wait_time,
                                                 NULL,
                                                 (void **)&buffer);

  if (sli_ipv6_config_status == SL_STATUS_OK) {
    sl_wifi_system_packet_t *packet = (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(buffer, 0, NULL);
    const sli_si91x_rsp_ipv6_params_t *ipv6_response = (sli_si91x_rsp_ipv6_params_t *)packet->data;

    memcpy(&ip_config->ip.v6.link_local_address,
           (const uint8_t *)ipv6_response->link_local_address,
           sizeof(ipv6_response->link_local_address));
    memcpy(&ip_config->ip.v6.global_address,
           (const uint8_t *)ipv6_response->global_address,
           sizeof(ipv6_response->global_address));
    memcpy(&ip_config->ip.v6.gateway,
           (const uint8_t *)ipv6_response->gateway_address,
           sizeof(ipv6_response->gateway_address));

    sli_buffer_manager_free_buffer(buffer);
    SL_DEBUG_LOG_V2(DEBUG, "sli_net_configure_ip_address: IPv6 OK (vap_id=%u)", virtual_ap_id);
  } else {
    if (buffer != NULL) {
      sli_buffer_manager_free_buffer(buffer);
    }
    SL_DEBUG_LOG_V2(WARN,
                    "sli_net_configure_ip_address: IPv6 failed (vap_id=%u, status=0x%lX)",
                    virtual_ap_id,
                    sli_ipv6_config_status);
  }

  return sli_ipv6_config_status;
}

static sl_status_t sli_aggregate_ip_config_status(sl_ip_address_type_t requested_type,
                                                  sl_ip_address_type_t configured_type)
{
  const bool ipv4_requested = (requested_type & SL_IPV4) == SL_IPV4;
  const bool ipv6_requested = (requested_type & SL_IPV6) == SL_IPV6;

  if (ipv4_requested && ipv6_requested) {
    if ((configured_type & SL_IPV4) && (configured_type & SL_IPV6)) {
      return SL_STATUS_OK;
    }
    if (configured_type & SL_IPV4) {
      return SL_STATUS_WIFI_IPV4_OK;
    }
    if (configured_type & SL_IPV6) {
      return SL_STATUS_WIFI_IPV6_OK;
    }
    // Dual-stack total failure: return IPv4 error; IPv6 error via sl_wifi_get_ip_config_failure_reason().
    return sli_ipv4_config_status;
  }
  if (ipv4_requested) {
    return (configured_type & SL_IPV4) ? SL_STATUS_OK : sli_ipv4_config_status;
  }
  if (ipv6_requested) {
    return (configured_type & SL_IPV6) ? SL_STATUS_OK : sli_ipv6_config_status;
  }
  return SL_STATUS_OK;
}

sl_status_t sli_net_configure_ip_address(sl_net_ip_configuration_t *ip_config,
                                         uint8_t virtual_ap_id,
                                         const uint32_t timeout)
{
  sl_status_t status = SL_STATUS_OK;
  uint32_t wait_time = (timeout ? SLI_WIFI_WAIT_FOR_RESPONSE(timeout) : SLI_WIFI_RETURN_IMMEDIATELY);

  // Check if the device is initialized
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  // Check for NULL ip_config pointer or timeout is 0
  if ((NULL == ip_config) || (0 == timeout)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  const sl_ip_address_type_t requested_type = ip_config->type;
  const bool ipv4_requested                 = (requested_type & SL_IPV4) == SL_IPV4;
  const bool ipv6_requested                 = (requested_type & SL_IPV6) == SL_IPV6;
  sl_ip_address_type_t configured_type      = 0;

  // Reset the per-family status for this configuration attempt. Families that were not
  // requested are reported as SL_STATUS_NOT_AVAILABLE via sl_wifi_get_ip_config_failure_reason().
  sli_ipv4_config_status = ipv4_requested ? SL_STATUS_FAIL : SL_STATUS_NOT_AVAILABLE;
  sli_ipv6_config_status = ipv6_requested ? SL_STATUS_FAIL : SL_STATUS_NOT_AVAILABLE;

  // Configure each requested family independently so a dual-stack profile can succeed partially.
  if (ipv4_requested && (sli_configure_ipv4_address(ip_config, virtual_ap_id, wait_time) == SL_STATUS_OK)) {
    configured_type |= SL_IPV4;
  }
  if (ipv6_requested && (sli_configure_ipv6_address(ip_config, virtual_ap_id, wait_time) == SL_STATUS_OK)) {
    configured_type |= SL_IPV6;
  }

  // Reflect only the families that configured successfully so downstream link-up,
  // LwIP synchronization, and firmware IP-info use the real address families.
  ip_config->type = configured_type;

  // Send IP address information to firmware for the successfully configured families.
  if (configured_type != 0) {
    status = sli_send_client_ip_address_info_if_applicable(ip_config, virtual_ap_id);
    // Non-fatal: the address is already configured above. IP_ADDRESS_INFO (0x94)
    // is not implemented by firmware < 2.16.5, which never answers and makes the
    // notify time out — that must not fail an otherwise-successful config.
    if (status != SL_STATUS_OK) {
      SL_DEBUG_LOG_V2(WARN, "IP_ADDRESS_INFO notify failed (0x%lX) - continuing\r\n", status);
    }
  }

  return sli_aggregate_ip_config_status(requested_type, configured_type);
}

sl_status_t sl_si91x_configure_ip_address(sl_net_ip_configuration_t *address, uint8_t virtual_ap_id)
{
  return sli_net_configure_ip_address(address, virtual_ap_id, SLI_WIFI_WAIT_FOR_EVER);
}

sl_status_t sl_wifi_get_ip_config_failure_reason(sl_status_t *ipv4_status, sl_status_t *ipv6_status)
{
  if ((NULL == ipv4_status) && (NULL == ipv6_status)) {
    return SL_STATUS_NULL_POINTER;
  }

  if (NULL != ipv4_status) {
    *ipv4_status = sli_ipv4_config_status;
  }
  if (NULL != ipv6_status) {
    *ipv6_status = sli_ipv6_config_status;
  }

  return SL_STATUS_OK;
}
