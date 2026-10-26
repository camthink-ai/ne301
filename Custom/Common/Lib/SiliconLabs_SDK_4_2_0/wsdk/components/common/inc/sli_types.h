/***************************************************************************/ /**
 * @file
 * @brief Internal Wi-Fi type definitions
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
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
#ifndef SLI_TYPES_H
#define SLI_TYPES_H

#include "sl_constants.h"
#include "sl_ieee802_types.h"
#include "sli_constants.h"
#include "sl_types.h"

/// Access point disconnect response structure
#pragma pack(1)
typedef struct {
  sl_mac_address_t client_mac_address;                ///< Client MAC address
  uint8_t flag;                                       ///< Flag field
  uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH];       ///< Remote IPv4 Address
  uint8_t link_local_address[SL_IPV6_ADDRESS_LENGTH]; ///< Remote link-local IPv6 Address
  uint8_t global_address[SL_IPV6_ADDRESS_LENGTH];     ///< Remote unicast global IPv6 Address
} sli_si91x_ap_disconnect_resp_t;

#pragma pack()

/// Internal SiWx91x Socket information query
/// @note: This is internal structure and should not be used by the application. This is identical to sl_si91x_sock_info_query_t and, would be cleaned to have single structure in future.
typedef struct {
  uint8_t sock_id[SLI_SI91X_2BYTE_FIELD_SIZE]; ///< Identifier for the socket

  uint8_t sock_type[SLI_SI91X_2BYTE_FIELD_SIZE]; ///< Type of the socket (TCP, UDP, and so on.)

  uint8_t source_port[SLI_SI91X_2BYTE_FIELD_SIZE]; ///< Port number used by the source

  uint8_t dest_port[SLI_SI91X_2BYTE_FIELD_SIZE]; ///< Port number used by the destination

  union {
    uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH]; ///< IPv4 address of the remote host

    uint8_t ipv6_address[SL_IPV6_ADDRESS_LENGTH]; ///< IPv6 address of the remote host

  } dest_ip_address; ///< IP address of the destination host
} sli_sock_info_query_t;

#pragma pack(1)
typedef struct {
  /// uint8, 0= NOT Connected, 1= Connected
  uint8_t wlan_state;

  /// channel number of connected AP
  uint8_t channel_number;

  /// PSK
  uint8_t psk[SL_WIFI_MAX_PSK_LENGTH];

  /// Mac address
  uint8_t mac_address[SL_WIFI_MAC_ADDRESS_LENGTH];

  /// uint8[32], SSID of connected access point
  uint8_t ssid[SLI_SSID_LEN];

  /// 2 bytes, 0= AdHoc, 1= Infrastructure
  uint8_t connType[SLI_SI91X_2BYTE_FIELD_SIZE];

  /// security type
  uint8_t sec_type;

  /// uint8, 0= Manual IP Configuration,1= DHCP
  uint8_t dhcpMode;

  /// uint8[4], Module IP Address
  uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH];

  /// uint8[4], Module Subnet Mask
  uint8_t subnetMask[SL_IPV4_ADDRESS_LENGTH];

  /// uint8[4], Gateway address for the Module
  uint8_t gateway[SL_IPV4_ADDRESS_LENGTH];

  /// number of sockets opened
  uint8_t num_open_socks[SLI_SI91X_2BYTE_FIELD_SIZE];

  /// prefix length for ipv6 address
  uint8_t prefix_length[SLI_SI91X_2BYTE_FIELD_SIZE];

  /// modules ipv6 address
  uint8_t ipv6_address[SL_IPV6_ADDRESS_LENGTH];

  /// router ipv6 address
  uint8_t defaultgw6[SL_IPV6_ADDRESS_LENGTH];

  /// BIT(0) =1 - ipv4, BIT(1)=2 - ipv6, BIT(0) & BIT(1)=3 - BOTH
  uint8_t tcp_stack_used;

  /// sockets information array
  sli_sock_info_query_t socket_info[10];

  /// BSSID address of connected AP
  uint8_t bssid[SL_WIFI_MAC_ADDRESS_LENGTH];

  /// Wireless mode used in connected AP (6 - AX, 4 - N, 3 - G, 1 - B)
  uint8_t wireless_mode;
} sli_si91x_network_params_response_t;
#pragma pack()

/// DNS query response structure
typedef struct {
  //! Ip version of the DNS server
  uint8_t ip_version[SLI_SI91X_2BYTE_FIELD_SIZE];

  //! DNS response count
  uint8_t ip_count[SLI_SI91X_2BYTE_FIELD_SIZE];

  //! DNS address responses
  union {
    uint8_t ipv4_address[SL_IPV4_ADDRESS_LENGTH];
    uint8_t ipv6_address[SL_IPV6_ADDRESS_LENGTH];
  } ip_address[SLI_SI91X_DNS_RESPONSE_MAX_ENTRIES];
} sli_si91x_dns_response_t;
/// DNS query request structure
typedef struct {
  //! Ip version value
  uint8_t ip_version[SLI_SI91X_2BYTE_FIELD_SIZE];

  //! URL name
  uint8_t url_name[SLI_SI91X_DNS_REQUEST_MAX_URL_LEN];

  //! DNS servers count
  uint8_t dns_server_number[SLI_SI91X_2BYTE_FIELD_SIZE];

  //! Timeout in seconds
  uint8_t initial_timeout_sec;

  //! Retry count
  uint8_t retry_count;
} sli_si91x_dns_query_request_t;

typedef struct {
  sl_wifi_performance_profile_v2_t wifi_performance_profile;
  sl_bt_performance_profile_t bt_performance_profile;
  sl_wifi_system_coex_mode_t coex_mode;
} sli_wifi_performance_profile_t;

/// structure for power save request
typedef struct {
  /// power mode to set
  uint8_t power_mode;

  /// set LP/ULP/ULP-without RAM retention
  uint8_t ulp_mode_enable;

  /// set DTIM aligment required
  // 0 - module wakes up at beacon which is just before or equal to listen_interval
  // 1 - module wakes up at DTIM beacon which is just before or equal to listen_interval
  uint8_t dtim_aligned_type;

  /// Set PSP type, 0-Max PSP, 1- FAST PSP, 2-APSD
  uint8_t psp_type;

  /// Monitor interval for the FAST PSP mode
  // default is 50 ms, and this parameter is valid for FAST PSP only
  uint16_t monitor_interval;
  /// Number of DTIMs to skip
  uint8_t num_of_dtim_skip;
  /// Listen interval
  uint16_t listen_interval;
  /// Wake up for the next beacon if the number of missed beacons exceeds the limit. The default value is 1, with a recommended maximum value of 10. Higher values may cause interoperability issues.
  uint8_t beacon_miss_ignore_limit;
} sli_wifi_power_save_request_t;

#endif // SLI_TYPES_H