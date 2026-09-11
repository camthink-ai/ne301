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
#pragma once

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "sl_status.h"
#include "sl_additional_status.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_protocol_types.h"
#include "sl_net_constants.h"
#include "sl_net_ip_types.h"
#ifdef SLI_SI91X_INTERNAL_MDNS
#include "sl_mdns.h"
#endif
#ifdef SLI_SI91X_INTERNAL_HTTP_CLIENT
#include "sl_http_client.h"
#endif

sl_status_t sli_convert_rsi_ipv4_address_to_sl_ip_address(sl_ip_address_t *ip_address_buffer,
                                                          const sli_si91x_rsp_ipv4_params_t *ip_params);
sl_status_t sli_convert_si91x_dns_response(sl_ip_address_t *ip_address,
                                           const sli_si91x_dns_response_t *si91x_dns_response);
sl_status_t sli_convert_si91x_event_to_sl_net_event(const uint16_t *event,
                                                    sl_net_event_t *sl_net_event,
                                                    const sl_wifi_system_packet_t *packet);
#ifdef SLI_SI91X_INTERNAL_MDNS
sl_status_t sli_convert_si91x_mdns_response(sl_mdns_response_t *mdns_result, const sli_net_mdns_response_t *raw_result);
#endif
bool sli_wifi_is_ip_address_zero(const sl_ip_address_t *ip_addr);
#ifdef SLI_SI91X_INTERNAL_HTTP_CLIENT
void convert_itoa(uint32_t val, uint8_t *str);
sl_status_t sli_convert_si91x_event_to_sl_http_client_event(const uint16_t *event,
                                                            sl_http_client_event_t *sl_http_client_event);
#endif
sl_status_t sli_si91x_configure_ip_address(sl_net_ip_configuration_t *address,
                                           uint8_t virtual_ap_id,
                                           const uint32_t timeout);

/**
 * @brief Validate that a VAP supports the given IP version for socket operations.
 *
 * Checks whether the provided VAP has a profile configured with the requested
 * IP version. If no profiles are configured, returns SL_STATUS_OK to allow
 * applications that bypass sl_net (e.g., using sl_si91x_configure_ip_address
 * directly) to proceed; firmware will handle validation in such cases.
 *
 * @param[in] vap_id  VAP ID to validate.
 * @param[in] ip_type IP address type: SL_IPV4 or SL_IPV6.
 *
 * @return SL_STATUS_OK if the VAP supports the requested IP version or if
 *         no profiles are configured.
 * @return SL_STATUS_INVALID_CONFIGURATION if profiles are configured but
 *         do not support the requested IP version.
 */
sl_status_t sli_net_get_vap_for_ip_version(uint8_t vap_id, sl_ip_address_type_t ip_type);

/**
 * @brief Returns true when IP configuration fully or partially succeeded.
 *
 * IP configuration reports partial success for a dual-stack profile via
 * @ref SL_STATUS_WIFI_IPV4_OK (only IPv4 configured) or @ref SL_STATUS_WIFI_IPV6_OK
 * (only IPv6 configured). In both cases the link is usable and must not be torn down.
 *
 * @param[in] status Status returned by @ref sli_net_configure_ip_address().
 * @return true if status is @ref SL_STATUS_OK or a partial-success code, false otherwise.
 */
bool sli_net_is_ip_config_success(sl_status_t status);
