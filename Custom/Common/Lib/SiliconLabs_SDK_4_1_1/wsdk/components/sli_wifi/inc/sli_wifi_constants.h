/***************************************************************************/ /**
 * @file    sli_wifi_constants.h
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
#ifndef SLI_WIFI_CONSTANTS_H
#define SLI_WIFI_CONSTANTS_H
/// Timeout value for Wi-Fi join command
#define SLI_WIFI_CONNECT_TIMEOUT 120000 // in ms, i.e., 120s
// User gain table related
#define SLI_MAX_GAIN_TABLE_SIZE_WITH_SU_TB 160
#ifndef SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME
#define SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME 1000 // 1 second to wait for a command buffer
#endif
#define SLI_WLAN_MGMT_Q 4
#define SLI_BT_Q        2

#define SLI_WIFI_SUCCESS         0 // Success
#define SLI_WIFI_BG_SCAN_DISABLE 0
#define SLI_WIFI_BG_SCAN_ENABLE  1

#define SLI_SEND_RAW_DATA 0x1

#ifndef SLI_WIFI_CONFIG_RTS_THRESHOLD
#define SLI_WIFI_CONFIG_RTS_THRESHOLD 1
#endif
#ifndef SLI_WIFI_RTS_THRESHOLD
#define SLI_WIFI_RTS_THRESHOLD 2346
#endif

#ifndef SLI_CONFIG_RTSTHRESHOLD
#define SLI_CONFIG_RTSTHRESHOLD 1
#endif

#ifndef SLI_RTS_THRESHOLD
#define SLI_RTS_THRESHOLD 2346
#endif

#define SLI_WIFI_INVALID_MODE 0xFFFF

/**
 * @def SLI_WIFI_TX_POWER_DECIDBM_MIN
 * @brief Minimum transmit power in decidBm (tenths of dBm).
 * @details Used with sl_wifi_set_test_tx_power(); value -150 corresponds to -15.0 dBm.
 */
#define SLI_WIFI_TX_POWER_DECIDBM_MIN (-150)

/**
 * @def SLI_WIFI_TX_POWER_DECIDBM_MAX
 * @brief Maximum transmit power in decidBm (tenths of dBm).
 * @details Used with sl_wifi_set_test_tx_power(); value 210 corresponds to 21.0 dBm.
 */
#define SLI_WIFI_TX_POWER_DECIDBM_MAX 210
/// Default listen interval multiplier for STA (association / power save).
#define DEFAULT_LISTEN_INTERVAL_MULTIPLIER 1

/// Flag to indicate that the response status of the command is expected.
#define SLI_WIFI_PACKET_RESPONSE_STATUS (1 << 0)

/// Flag to indicate that the response packet of the command is expected.
#define SLI_WIFI_PACKET_RESPONSE_PACKET (1 << 1)

/// Flag to indicate that all the packet tx has to be suspended until the corresponding command response is received.
#define SLI_WIFI_PACKET_GLOBAL_QUEUE_BLOCK (1 << 3)

/// Flag to indicate that host would receive the response from firmware in asynchronous manner.
#define SLI_WIFI_PACKET_WITH_ASYNC_RESPONSE (1 << 4)

#define SLI_WIFI_SET_WPS_METHOD_PIN   1
#define SLI_WIFI_SET_WPS_GENERATE_PIN 1

/// Maximum WPS credential records per JOIN response session (tri-band). Must match NWP / supplicant configuration.
#define SLI_WIFI_MAX_WPS_CREDENTIALS 3

#define SLI_WIFI_TX_PENDING_FLAG(x) (1 << (x))
#define SLI_WIFI_RESPONSE_FLAG(x)   (1 << (SI91X_CMD_MAX + x))

/// Flags for IP address availability used in sli_wifi_ip_address_info_t structure.
#define SLI_WIFI_IPV4_AVAILABLE (1U << 0) ///< Bit 0: IPv4 address is available
#define SLI_WIFI_IPV6_AVAILABLE (1U << 1) ///< Bit 1: IPv6 address is available

/**
 * @def SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE
 * @brief Indicates support for a new set of firmware update result codes. This bit is used for internal purpose.
 * @details
 * This bit in the feature bitmap is used to inform the NWP firmware whether
 * the host supports a new set of result codes to differentiate firmware update
 * results from other non-firmware-related results. If this bit is set,
 * the NWP firmware would send result codes from the new set after a firmware update.
 * If the bit is not set, the legacy result codes would be used.
 */
#define SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE BIT(16)

/// Wifi Timeout types
typedef enum {
  SLI_WIFI_AUTHENTICATION_ASSOCIATION_TIMEOUT =
    0, ///< Used for setting association and authentication timeout request in milliseconds
  SLI_WIFI_CHANNEL_ACTIVE_SCAN_TIMEOUT, ///< Used for setting dwell time per channel in milliseconds during active scan
  SLI_WIFI_KEEP_ALIVE_TIMEOUT,          ///< Used for setting WLAN keep alive time in seconds
  SLI_WIFI_CHANNEL_PASSIVE_SCAN_TIMEOUT ///< Used for setting dwell time per channel in milliseconds during passive scan
} sli_wifi_timeout_type_t;

/// Lower 16 bits hold raw wait time value
#define SLI_WIFI_WAIT_TIME_BIT_MASK 0x1FFFFFFF

#define SLI_WIFI_DNS_RETRY_COUNT 1

typedef enum {
  SLI_WIFI_RETURN_IMMEDIATELY              = 0,
  SLI_WIFI_ASYNC_RESPONSE_BIT              = (1UL << 29),
  SLI_WIFI_WAIT_FOR_RESPONSE_BIT           = (1UL << 30),
  SLI_WIFI_WAIT_FOR_EVER                   = (1UL << 31),
  SLI_WIFI_WAIT_FOR_OTAF_RESPONSE          = (SLI_WIFI_WAIT_FOR_RESPONSE_BIT | SLI_WIFI_WAIT_FOR_EVER),
  SLI_WIFI_WAIT_FOR_SYNC_SCAN_RESULTS      = (SLI_WIFI_WAIT_FOR_RESPONSE_BIT | (12000 & SLI_WIFI_WAIT_TIME_BIT_MASK)),
  SLI_WIFI_WAIT_FOR_COMMAND_RESPONSE       = (SLI_WIFI_WAIT_FOR_RESPONSE_BIT | (1000 & SLI_WIFI_WAIT_TIME_BIT_MASK)),
  SLI_WIFI_WAIT_FOR_SOCKET_ACCEPT_RESPONSE = (SLI_WIFI_WAIT_FOR_RESPONSE_BIT | (5000 & SLI_WIFI_WAIT_TIME_BIT_MASK)),
  SLI_WIFI_WAIT_FOR_COMMAND_SUCCESS        = (3000 & SLI_WIFI_WAIT_TIME_BIT_MASK),
  SLI_WIFI_WAIT_FOR_DNS_RESOLUTION         = (10 & SLI_WIFI_WAIT_TIME_BIT_MASK), // DNS timeout is in seconds
} sli_wifi_wait_period_t;

#define SLI_WIFI_WAIT_FOR(x)          (sli_wifi_wait_period_t)(x)
#define SLI_WIFI_WAIT_FOR_RESPONSE(x) (sli_wifi_wait_period_t)(SLI_WIFI_WAIT_FOR_RESPONSE_BIT | x)

typedef enum { SET_REGION_CODE_FROM_BEACONS, SET_REGION_CODE_FROM_USER } sli_wifi_set_region_code_command_t;

typedef enum { SLI_WIFI_NO_ENCRYPTION, SLI_WIFI_TKIP_ENCRYPTION, SLI_WIFI_CCMP_ENCRYPTION } sli_wifi_encryption_t;

// enumeration for command request used in common control block
typedef enum {
  // Common command requests
  SLI_COMMON_REQ_OPERMODE      = 0x10,
  SLI_COMMON_REQ_FEATURE_FRAME = 0xC8,
  SLI_COMMON_REQ_PWRMODE       = 0x15,
  // Reusing SLI_WIFI_REQ_FW_VERSION as SLI_COMMON_REQ_FW_VERSION
  SLI_COMMON_REQ_FW_VERSION     = 0x49,
  SLI_COMMON_REQ_GET_EFUSE_DATA = 0xA0,

  // Unimplemented common command requests
  SLI_COMMON_REQ_ENCRYPT_CRYPTO        = 0x76,
  SLI_COMMON_REQ_UART_FLOW_CTRL_ENABLE = 0xA4,
  SLI_COMMON_REQ_TA_M4_COMMANDS        = 0xB0,
  SLI_COMMON_REQ_DEBUG_LOG             = 0x26
#ifdef SLI_WAC_MFI_ENABLE
  ,
  SLI_COMMON_REQ_IAP_GET_CERTIFICATE   = 0xB6,
  SLI_COMMON_REQ_IAP_INIT              = 0xB7,
  SLI_COMMON_REQ_IAP_GENERATE_SIGATURE = 0xB8
#endif
  ,
  SLI_COMMON_REQ_SWITCH_PROTO          = 0x77,
  SLI_COMMON_REQ_GET_RAM_DUMP          = 0x92,
  SLI_COMMON_REQ_ASSERT                = 0xE1,
  SLI_COMMON_REQ_SET_RTC_TIMER         = 0xE9,
  SLI_COMMON_REQ_GET_RTC_TIMER         = 0xF2,
  SLI_COMMON_REQ_SET_CONFIG            = 0xBA,
  SLI_COMMON_REQ_GET_CONFIG            = 0x0C,
  SLI_COMMON_REQ_FW_FALLBACK_FROM_HOST = 0x2C,
#ifdef CONFIGURE_GPIO_FROM_HOST
  ,
  SLI_COMMON_REQ_GPIO_CONFIG = 0x28
#endif
} sli_common_cmd_request_t;

typedef enum {
  // Common command responses
  SLI_COMMON_RSP_OPERMODE      = 0x10,
  SLI_COMMON_RSP_FEATURE_FRAME = 0xC8,
  SLI_COMMON_RSP_CARDREADY     = 0x89,
  SLI_COMMON_RSP_PWRMODE       = 0x15,

  // Unimplemented common command responses
  SLI_COMMON_RSP_CLEAR                 = 0x00,
  SLI_COMMON_RSP_ULP_NO_RAM_RETENTION  = 0xCD,
  SLI_COMMON_RSP_ASYNCHRONOUS          = 0xFF,
  SLI_COMMON_RSP_ENCRYPT_CRYPTO        = 0x76,
  SLI_COMMON_RSP_UART_FLOW_CTRL_ENABLE = 0xA4,
  SLI_COMMON_RSP_TA_M4_COMMANDS        = 0xB0,
  SLI_COMMON_RSP_DEBUG_LOG             = 0x26

#ifdef SLI_WAC_MFI_ENABLE
  ,
  SLI_COMMON_RSP_IAP_GET_CERTIFICATE   = 0xB6,
  SLI_COMMON_RSP_IAP_INIT              = 0xB7,
  SLI_COMMON_RSP_IAP_GENERATE_SIGATURE = 0xB8
#endif
  // Reusing SLI_WIFI_REQ_FW_VERSION as SLI_COMMON_REQ_FW_VERSION
  ,
  SLI_COMMON_RSP_GET_EFUSE_DATA        = 0xA0,
  SLI_COMMON_RSP_FW_VERSION            = 0x49,
  SLI_COMMON_RSP_SWITCH_PROTO          = 0x77,
  SLI_COMMON_RSP_GET_RAM_DUMP          = 0x92,
  SLI_COMMON_RSP_ASSERT                = 0xE1,
  SLI_COMMON_RSP_SET_RTC_TIMER         = 0xE9,
  SLI_COMMON_RSP_GET_RTC_TIMER         = 0xF2,
  SLI_COMMON_RSP_SET_CONFIG            = 0xBA,
  SLI_COMMON_RSP_GET_CONFIG            = 0x0C,
  SLI_COMMON_RSP_FW_FALLBACK_FROM_HOST = 0x2C,
#ifdef CONFIGURE_GPIO_FROM_HOST
  ,
  SLI_COMMON_RSP_GPIO_CONFIG = 0x28
#endif
} sli_common_cmd_response_t;

typedef enum {
  SLI_WIFI_REQ_CONFIG                       = 0xBE, ///< Wi-Fi Config
  SLI_WIFI_REQ_INIT                         = 0x12, ///< Wi-Fi Initialization
  SLI_WIFI_REQ_OPERMODE                     = 0x10, ///< Wi-Fi Operation Mode
  SLI_WIFI_REQ_BAND                         = 0x11, ///< Wi-Fi Band
  SLI_WIFI_REQ_SCAN                         = 0x13, ///< Wi-Fi Scan
  SLI_WIFI_REQ_SET_REGION                   = 0x1D, ///< Set the Device Region
  SLI_WIFI_REQ_JOIN                         = 0x14, ///< Wi-Fi Join
  SLI_WIFI_REQ_PWRMODE                      = 0x15, ///< Power Mode
  SLI_WIFI_REQ_MAC_ADDRESS                  = 0x4A, ///< Set MAC Address
  SLI_WIFI_REQ_DISCONNECT                   = 0x19, ///< Wi-Fi Disconnect
  SLI_COMMON_REQ_SOFT_RESET                 = 0x1C, ///< Wi-Fi Soft Reset
  SLI_WIFI_REQ_AP_STOP                      = 0xAE, ///< Stop Access Point
  SLI_WIFI_REQ_QUERY_NETWORK_PARAMS         = 0x18, ///< Query Network Parameters
  SLI_WIFI_REQ_ANTENNA_SELECT               = 0x1B, ///< Antenna Selection
  SLI_WIFI_REQ_HT_CAPABILITIES              = 0x6D, ///< HT Capabilities
  SLI_WIFI_REQ_BG_SCAN                      = 0x6A, ///< Background Scan
  SLI_WIFI_REQ_EAP_CONFIG                   = 0x4C, ///< EAP Configuration
  SLI_WIFI_REQ_HOST_PSK                     = 0xA5, ///< Host PSK
  SLI_WIFI_REQ_AP_CONFIGURATION             = 0x24, ///< Access Point Configuration
  SLI_WIFI_REQ_BEACON_STOP                  = 0x63, ///< Stop Beacon
  SLI_WIFI_REQ_REJOIN_PARAMS                = 0x6F, ///< Rejoin Parameters
  SLI_WIFI_REQ_RSSI                         = 0x3A, ///< RSSI
  SLI_WIFI_REQ_GET_TIMEOUT                  = 0x3C, ///< Get Timeout
  SLI_WIFI_REQ_SET_MAC_ADDRESS              = 0x17, ///< Set MAC Address
  SLI_WIFI_REQ_QUERY_GO_PARAMS              = 0x4E, ///< Query GO Parameters
  SLI_WIFI_REQ_BC_MC_FILTER_STATS           = 0x60, ///< Broadcast/Multicast Filter Statistics
  SLI_WIFI_REQ_EXT_STATS                    = 0x68, ///< Extended Statistics  // Neither part 22q2 nor alpha 2
  SLI_WIFI_REQ_GET_STATS                    = 0xF1, ///< Get Statistics
  SLI_WIFI_REQ_FREQ_OFFSET                  = 0xF3, ///< Frequency Offset
  SLI_WIFI_REQ_RX_STATS                     = 0xA2, ///< RX Statistics
  SLI_WIFI_REQ_ROAM_PARAMS                  = 0x7B, ///< Roam Parameters
  SLI_WIFI_REQ_TX_TEST_MODE                 = 0x7C, ///< TX Test Mode
  SLI_WIFI_REQ_TWT_AUTO_CONFIG              = 0x2E, ///< TWT Auto Configuration
  SLI_WIFI_REQ_DYNAMIC_POOL                 = 0xC7, ///< Dynamic Pool
  SLI_WIFI_COMMON_REQ_FEATURE_FRAME         = 0xC8, ///< Feature Frame
  SLI_WIFI_REQ_FILTER_BCAST_PACKETS         = 0xC9, ///< Filter Broadcast Packets
  SLI_WIFI_REQ_WPS_METHOD                   = 0x72, ///< WPS Method
  SLI_WIFI_REQ_WPS_EXTENDED_CREDENTIALS     = 0x95, ///< Fetch additional WPS credential records
  SLI_WIFI_REQ_GAIN_TABLE                   = 0x47, ///< Gain Table
  SLI_WIFI_REQ_TIMEOUT                      = 0xEA, ///< Timeout
  SLI_WIFI_REQ_SET_REGION_AP                = 0xBD, ///< Set Region AP
  SLI_WIFI_REQ_11AX_PARAMS                  = 0xFF, ///< 11AX Parameters
  SLI_WIFI_REQ_TWT_PARAMS                   = 0x2F, ///< TWT Parameters
  SLI_WIFI_REQ_RESCHEDULE_TWT               = 0x3F, ///< Reschedule TWT
  SLI_WIFI_REQ_SET_TRANSCEIVER_CHANNEL      = 0x7A, ///< Set Transceiver Channel
  SLI_WIFI_REQ_DMP_CONFIGURATION            = 0x78, ///< DMP Configuration
  SLI_WIFI_REQ_TRANSCEIVER_CONFIG_PARAMS    = 0x8C, ///< Transceiver Configuration Parameters
  SLI_WIFI_REQ_TRANSCEIVER_PEER_LIST_UPDATE = 0x8B, ///< Update Transceiver Peer List
  SLI_WIFI_REQ_SET_TRANSCEIVER_MCAST_FILTER = 0x8D, ///< Set Transceiver Multicast Filter
  SLI_WIFI_REQ_TRANSCEIVER_FLUSH_DATA_Q     = 0x8E, ///< Flush Transceiver Data Queue
  SLI_WIFI_REQ_SET_MULTICAST_FILTER         = 0x40, ///< Set Multicast Filter
  SLI_WIFI_REQ_CARDREADY                    = 0x89, ///< Card Ready
  SLI_WIFI_REQ_SCAN_RESULTS                 = 0xAF, ///< Scan Results
  SLI_WIFI_REQ_TSF                          = 0x65, ///< TSF
  SLI_WIFI_REQ_WIFI_RAIL                    = 0x99, ///< Wi-Fi Rail (FWUP)
  SLI_WIFI_REQ_FWUP                         = 0x99, ///< Firmware upgrade (same as WIFI_RAIL)
  SLI_WIFI_REQ_VENDOR_IE                    = 0x38, ///< vendor-specific IE Request
  SLI_COMMON_REQ_NWP_LOGGING                = 0x82, ///< NWP Logging
  SLI_COMMON_REQ_ENABLE_DISABLE_BLE         = 0x2B, ///< Common BLE Enable/Disable

  /* Additional request commands (unified from WLAN) */
  SLI_WIFI_REQ_SET_SLEEP_TIMER           = 0x16,
  SLI_WIFI_REQ_CFG_SAVE                  = 0x20,
  SLI_WIFI_REQ_AUTO_CONFIG_ENABLE        = 0x21,
  SLI_WIFI_REQ_GET_CFG                   = 0x22,
  SLI_WIFI_REQ_USER_STORE_CONFIG         = 0x23,
  SLI_WIFI_REQ_SET_WEP_KEYS              = 0x25,
  SLI_WIFI_REQ_PING_PACKET               = 0x29,
  SLI_WIFI_REQ_NAT                       = 0x2A,
  SLI_WIFI_REQ_SET_PROFILE               = 0x31,
  SLI_WIFI_REQ_GET_PROFILE               = 0x32,
  SLI_WIFI_REQ_DELETE_PROFILE            = 0x33,
  SLI_WIFI_REQ_EVM_OFFSET                = 0x36,
  SLI_WIFI_REQ_EVM_WRITE                 = 0x37,
  SLI_WIFI_REQ_IPCONFV4                  = 0x41,
  SLI_WIFI_REQ_SOCKET_CREATE             = 0x42,
  SLI_WIFI_REQ_SOCKET_CLOSE              = 0x43,
  SLI_WIFI_REQ_DNS_QUERY                 = 0x44,
  SLI_WIFI_REQ_CONNECTION_STATUS         = 0x48,
  SLI_WIFI_REQ_FW_VERSION                = 0x49,
  SLI_WIFI_REQ_CONFIGURE_P2P             = 0x4B,
  SLI_WIFI_REQ_SET_CERTIFICATE           = 0x4D,
  SLI_WIFI_REQ_WEBPAGE_LOAD              = 0x50,
  SLI_WIFI_REQ_HTTP_CLIENT_GET           = 0x51,
  SLI_WIFI_REQ_HTTP_CLIENT_POST          = 0x52,
  SLI_WIFI_REQ_HTTP_CLIENT_PUT           = 0x53,
  SLI_WIFI_REQ_DNS_SERVER_ADD            = 0x55,
  SLI_WIFI_REQ_HOST_WEBPAGE_SEND         = 0x56,
  SLI_WIFI_REQ_WIRELESS_FWUP             = 0x59,
  SLI_WIFI_REQ_SET_BC_MC_FILTER_CONFIG   = 0x5B, ///< Set broadcast/multicast filter configuration
  SLI_WIFI_REQ_UPDATE_MC_ALLOWLIST       = 0x5C, ///< Update multicast allowlist (add/remove/remove all)
  SLI_WIFI_REQ_SET_BEACON_DROP_THRESHOLD = 0x5D, ///< Set beacon drop threshold for power save
  SLI_WIFI_REQ_SOCKET_READ_DATA          = 0x6B,
  SLI_WIFI_REQ_SOCKET_ACCEPT             = 0x6C,
  SLI_WIFI_REQ_SET_SNI_EMBEDDED          = 0x6E,
  SLI_WIFI_REQ_EFUSE_READ                = 0x73,
  SLI_WIFI_REQ_SELECT_REQUEST            = 0x74,
  SLI_WIFI_REQ_WEBPAGE_CLEAR_ALL         = 0x7F,
  SLI_WIFI_REQ_RADIO                     = 0x81,
  SLI_WIFI_REQ_DISCOVER_SERVICE          = 0x8F,
  SLI_WIFI_REQ_IPCONFV6                  = 0x90,
  SLI_WIFI_REQ_IP_ADDRESS_INFO           = 0x94,
  SLI_WIFI_REQ_WMM_PS                    = 0x97,
  SLI_WIFI_REQ_WEBPAGE_ERASE             = 0x9A,
  SLI_WIFI_REQ_JSON_OBJECT_ERASE         = 0x9B,
  SLI_WIFI_REQ_JSON_LOAD                 = 0x9C,
  SLI_WIFI_REQ_SOCKET_CONFIG             = 0xA7,
  SLI_WIFI_REQ_MULTICAST                 = 0xB1,
  SLI_WIFI_REQ_HTTP_ABORT                = 0xB3,
  SLI_WIFI_REQ_HTTP_CREDENTIALS          = 0xB4,
#ifdef SLI_WAC_MFI_ENABLE
  SLI_WIFI_REQ_ADD_MFI_IE = 0xB5,
#endif
#ifndef SLI_SI91X_MCU_INTERFACE
  SLI_WIFI_REQ_CERT_VALID = 0xBC,
#endif
  SLI_WIFI_REQ_CALIB_WRITE           = 0xCA,
  SLI_WIFI_REQ_EMB_MQTT_CLIENT       = 0xCB,
  SLI_WIFI_REQ_CALIB_READ            = 0xCF,
  SLI_WIFI_REQ_MDNSD                 = 0xDB,
  SLI_WIFI_REQ_FULL_FW_VERSION       = 0xE0,
  SLI_WIFI_REQ_FTP                   = 0xE2,
  SLI_WIFI_REQ_FTP_FILE_WRITE        = 0xE3,
  SLI_WIFI_REQ_SNTP_CLIENT           = 0xE4,
  SLI_WIFI_REQ_SMTP_CLIENT           = 0xE6,
  SLI_WIFI_REQ_POP3_CLIENT           = 0xE7,
  SLI_WIFI_REQ_HTTP_CLIENT_POST_DATA = 0xEB,
  SLI_WIFI_REQ_DHCP_USER_CLASS       = 0xEC,
  SLI_WIFI_REQ_DNS_UPDATE            = 0xED,
  SLI_WIFI_REQ_OTA_FWUP              = 0xEF,
  SLI_WIFI_REQ_HTTP_OTAF             = 0xF4,
  SLI_WIFI_REQ_UPDATE_TCP_WINDOW     = 0xF5,
  SLI_WIFI_REQ_GET_RANDOM            = 0xF8
} sli_wifi_request_commands_t;

typedef enum {
  SLI_WIFI_RSP_CONFIG                       = 0xBE, ///< Wi-Fi Config
  SLI_WIFI_RSP_OPERMODE                     = 0x10, ///< Wi-Fi Operation Mode
  SLI_COMMON_RSP_ENABLE_DISABLE_BLE         = 0x2B, ///< Common BLE Enable/Disable response
  SLI_WIFI_RSP_INIT                         = 0x12, ///< Wi-Fi Initialization
  SLI_WIFI_RSP_BAND                         = 0x11, ///< Wi-Fi
  SLI_WIFI_RSP_SCAN                         = 0x13, ///< Wi-Fi Scan
  SLI_WIFI_RSP_SET_REGION                   = 0x1D, ///< Set the Device Region
  SLI_WIFI_RSP_JOIN                         = 0x14, ///< Wi-Fi Join
  SLI_WIFI_RSP_PWRMODE                      = 0x15, ///< Power Mode
  SLI_WIFI_RSP_MAC_ADDRESS                  = 0x4A, ///< Set MAC Address
  SLI_WIFI_RSP_DISCONNECT                   = 0x19, ///< Wi-Fi Disconnect
  SLI_COMMON_RSP_SOFT_RESET                 = 0x1C, ///< Wi-Fi Soft Reset
  SLI_WIFI_RSP_AP_STOP                      = 0xAE, ///< Stop Access Point
  SLI_WIFI_RSP_QUERY_NETWORK_PARAMS         = 0x18, ///< Query Network Parameters
  SLI_WIFI_RSP_ANTENNA_SELECT               = 0x1B, ///< Antenna Selection
  SLI_WIFI_RSP_HT_CAPABILITIES              = 0x6D, ///< HT Capabilities
  SLI_WIFI_RSP_BG_SCAN                      = 0x6A, ///< Background Scan
  SLI_WIFI_RSP_EAP_CONFIG                   = 0x4C, ///< EAP Configuration
  SLI_WIFI_RSP_HOST_PSK                     = 0xA5, ///< Host PSK
  SLI_WIFI_RSP_AP_CONFIGURATION             = 0x24, ///< Access Point Configuration
  SLI_WIFI_RSP_BEACON_STOP                  = 0x63, ///< Stop Beacon
  SLI_WIFI_RSP_REJOIN_PARAMS                = 0x6F, ///< Rejoin Parameters
  SLI_WIFI_RSP_RSSI                         = 0x3A, ///< RSSI
  SLI_WIFI_RSP_GET_TIMEOUT                  = 0x3C, ///< Get Timeout
  SLI_WIFI_RSP_SET_MAC_ADDRESS              = 0x17, ///< Set MAC Address
  SLI_WIFI_RSP_QUERY_GO_PARAMS              = 0x4E, ///< Query GO Parameters
  SLI_WIFI_RSP_BC_MC_FILTER_STATS           = 0x60, ///< Broadcast/Multicast Filter Statistics
  SLI_WIFI_RSP_EXT_STATS                    = 0x68, ///< Extended Statistics  // Neither part 22q2 nor alpha 2
  SLI_WIFI_RSP_GET_STATS                    = 0xF1, ///< Get Statistics
  SLI_WIFI_RSP_RX_STATS                     = 0xA2, ///< RX Statistics
  SLI_WIFI_RSP_ROAM_PARAMS                  = 0x7B, ///< Roam Parameters
  SLI_WIFI_RSP_TWT_AUTO_CONFIG              = 0x2E, ///< TWT Auto Configuration
  SLI_WIFI_RSP_FILTER_BCAST_PACKETS         = 0xC9, ///< Filter Broadcast Packets
  SLI_WIFI_REQ_GET_DPD_DATA                 = 0xDC, ///< Get DPD Data
  SLI_WIFI_RSP_MODULE_STATE                 = 0x70,
  SLI_WIFI_RSP_TWT_ASYNC                    = 0x71,
  SLI_WIFI_RSP_WPS_METHOD                   = 0x72, ///< WPS Method
  SLI_WIFI_RSP_WPS_EXTENDED_CREDENTIALS     = 0x95, ///< Fetch additional WPS credential records
  SLI_WIFI_RSP_GAIN_TABLE                   = 0x47, ///< Gain Table
  SLI_WIFI_RSP_TIMEOUT                      = 0xEA, ///< Timeout
  SLI_WIFI_RSP_11AX_PARAMS                  = 0xFF, ///< 11AX Parameters
  SLI_WIFI_RSP_ASYNCHRONOUS                 = 0xFF, ///< Asynchronous response (same value as 11AX_PARAMS)
  SLI_WIFI_RSP_TWT_PARAMS                   = 0x2F, ///< TWT Parameters
  SLI_WIFI_RSP_SET_REGION_AP                = 0xBD, ///< Set Region AP
  SLI_WIFI_RSP_RESCHEDULE_TWT               = 0x3F, ///< Reschedule TWT
  SLI_WIFI_RSP_TRANSCEIVER_SET_CHANNEL      = 0x7A, ///< Set Transceiver Channel
  SLI_WIFI_RSP_TRANSCEIVER_CONFIG_PARAMS    = 0x8C, ///< Transceiver Configuration Parameters
  SLI_WIFI_RSP_TRANSCEIVER_PEER_LIST_UPDATE = 0x8B, ///< Update Transceiver Peer List
  SLI_WIFI_RSP_TRANSCEIVER_SET_MCAST_FILTER = 0x8D, ///< Set Transceiver Multicast Filter
  SLI_WIFI_RSP_TRANSCEIVER_FLUSH_DATA_Q     = 0x8E, ///< Flush Transceiver Data Queue
  SLI_WIFI_RSP_SET_MULTICAST_FILTER         = 0x40, ///< Set Multicast Filter
  SLI_WIFI_RSP_CARDREADY                    = 0x89, ///< Card Ready
  SLI_WIFI_RSP_SCAN_RESULTS                 = 0xAF, ///< Scan Results
  SLI_WIFI_RSP_TSF                          = 0x65, ///< TSF
  SLI_WIFI_RSP_VENDOR_IE                    = 0x38, ///< vendor-specific IE Response
  SLI_COMMON_RSP_NWP_LOGGING                = 0x82, ///< NWP Logging
  SLI_WIFI_RSP_HTTP_OTAF                    = 0xF4,
  SLI_WIFI_RSP_CLIENT_CONNECTED             = 0xC2,
  SLI_WIFI_RSP_CLIENT_DISCONNECTED          = 0xC3,
  SLI_WIFI_RSP_TRANSCEIVER_TX_DATA_STATUS   = 0x3D,
  SLI_WIFI_RX_DOT11_DATA                    = 0x03,
  SLI_WIFI_RATE_RSP_STATS                   = 0x88,

  /* Additional response commands (unified from sli_wlan_cmd_response_t) */
  SLI_WIFI_RSP_CLEAR                     = 0x00,
  SLI_WIFI_RSP_CFG_SAVE                  = 0x20,
  SLI_WIFI_RSP_AUTO_CONFIG_ENABLE        = 0x21,
  SLI_WIFI_RSP_GET_CFG                   = 0x22,
  SLI_WIFI_RSP_USER_STORE_CONFIG         = 0x23,
  SLI_WIFI_RSP_SET_WEP_KEYS              = 0x25,
  SLI_WIFI_RSP_PING_PACKET               = 0x29,
  SLI_WIFI_RSP_NAT                       = 0x2A,
  SLI_WIFI_RSP_P2P_CONNECTION_REQUEST    = 0x30,
  SLI_WIFI_RSP_SET_PROFILE               = 0x31,
  SLI_WIFI_RSP_GET_PROFILE               = 0x32,
  SLI_WIFI_RSP_DELETE_PROFILE            = 0x33,
  SLI_WIFI_RSP_EVM_OFFSET                = 0x36,
  SLI_WIFI_RSP_EVM_WRITE                 = 0x37,
  SLI_WIFI_RSP_IPCONFV4                  = 0x41,
  SLI_WIFI_RSP_SOCKET_CREATE             = 0x42,
  SLI_WIFI_RSP_SOCKET_CLOSE              = 0x43,
  SLI_WIFI_RSP_DNS_QUERY                 = 0x44,
  SLI_WIFI_RSP_CONNECTION_STATUS         = 0x48,
  SLI_WIFI_RSP_FW_VERSION                = 0x49,
  SLI_WIFI_RSP_CONFIGURE_P2P             = 0x4B,
  SLI_WIFI_RSP_SET_CERTIFICATE           = 0x4D,
  SLI_WIFI_RSP_WEBPAGE_LOAD              = 0x50,
  SLI_WIFI_RSP_HTTP_CLIENT_GET           = 0x51,
  SLI_WIFI_RSP_HTTP_CLIENT_POST          = 0x52,
  SLI_WIFI_RSP_HTTP_CLIENT_PUT           = 0x53,
  SLI_WIFI_RSP_WFD_DEVICE                = 0x54,
  SLI_WIFI_RSP_DNS_SERVER_ADD            = 0x55,
  SLI_WIFI_RSP_HOST_WEBPAGE_SEND         = 0x56,
  SLI_WIFI_RSP_WIRELESS_FWUP_OK          = 0x59,
  SLI_WIFI_RSP_WIRELESS_FWUP_DONE        = 0x5A,
  SLI_WIFI_RSP_SET_BC_MC_FILTER_CONFIG   = 0x5B, ///< Set broadcast/multicast filter configuration
  SLI_WIFI_RSP_UPDATE_MC_ALLOWLIST       = 0x5C, ///< Update multicast allowlist
  SLI_WIFI_RSP_SET_BEACON_DROP_THRESHOLD = 0x5D, ///< Set beacon drop threshold
  SLI_WIFI_RSP_CONN_ESTABLISH            = 0x61,
  SLI_WIFI_RSP_REMOTE_TERMINATE          = 0x62,
  SLI_WIFI_RSP_URL_REQUEST               = 0x64,
  SLI_WIFI_RSP_SOCKET_READ_DATA          = 0x6B,
  SLI_WIFI_RSP_SOCKET_ACCEPT             = 0x6C,
  SLI_WIFI_RSP_SET_SNI_EMBEDDED          = 0x6E,
  SLI_WIFI_RSP_EFUSE_READ                = 0x73,
  SLI_WIFI_RSP_SELECT_REQUEST            = 0x74,
  SLI_WIFI_RSP_TX_TEST_MODE              = 0x7C,
  SLI_WIFI_RSP_WEBPAGE_CLEAR_ALL         = 0x7F,
  SLI_WIFI_RSP_RADIO                     = 0x81,
  SLI_WIFI_RSP_DISCOVER_SERVICE          = 0x8F,
  SLI_WIFI_RSP_IP_ADDRESS_INFO           = 0x94,
  SLI_WIFI_RSP_WMM_PS                    = 0x97,
  SLI_WIFI_RSP_FWUP                      = 0x99,
  SLI_WIFI_RSP_WEBPAGE_ERASE             = 0x9A,
  SLI_WIFI_RSP_JSON_OBJECT_ERASE         = 0x9B,
  SLI_WIFI_RSP_JSON_LOAD                 = 0x9C,
  SLI_WIFI_RSP_JSON_UPDATE               = 0x9D,
  SLI_WIFI_RSP_IPCONFV6                  = 0xA1,
  SLI_WIFI_RSP_SOCKET_CONFIG             = 0xA7,
  SLI_WIFI_RSP_IPV4_CHANGE               = 0xAA,
  SLI_WIFI_RSP_TCP_ACK_INDICATION        = 0xAB,
  SLI_WIFI_RSP_UART_DATA_ACK             = 0xAC,
  SLI_WIFI_RSP_MULTICAST                 = 0xB1,
  SLI_WIFI_RSP_HTTP_ABORT                = 0xB3,
  SLI_WIFI_RSP_HTTP_CREDENTIALS          = 0xB4,
#ifdef SLI_WAC_MFI_ENABLE
  SLI_WIFI_RSP_ADD_MFI_IE = 0xB5,
#endif
#ifndef SLI_SI91X_MCU_INTERFACE
  SLI_WIFI_RSP_CERT_VALID = 0xBC,
#endif
  SLI_WIFI_RSP_CALIB_WRITE           = 0xCA,
  SLI_WIFI_RSP_DYNAMIC_POOL          = 0xC7,
  SLI_WIFI_RSP_EMB_MQTT_CLIENT       = 0xCB,
  SLI_WIFI_RSP_EMB_MQTT_PUBLISH_PKT  = 0xCC,
  SLI_WIFI_RSP_CALIB_READ            = 0xCF,
  SLI_WIFI_RSP_MDNSD                 = 0xDB,
  SLI_WIFI_RSP_GET_DPD_DATA          = 0xDC,
  SLI_WIFI_RSP_FULL_FW_VERSION       = 0xE0,
  SLI_WIFI_RSP_FTP                   = 0xE2,
  SLI_WIFI_RSP_FTP_FILE_WRITE        = 0xE3,
  SLI_WIFI_RSP_SNTP_CLIENT           = 0xE4,
  SLI_WIFI_RSP_SNTP_SERVER           = 0xE5,
  SLI_WIFI_RSP_SMTP_CLIENT           = 0xE6,
  SLI_WIFI_RSP_POP3_CLIENT           = 0xE7,
  SLI_WIFI_RSP_POP3_CLIENT_TERMINATE = 0xE8,
  SLI_WIFI_RSP_HTTP_CLIENT_POST_DATA = 0xEB,
  SLI_WIFI_RSP_DHCP_USER_CLASS       = 0xEC,
  SLI_WIFI_RSP_DNS_UPDATE            = 0xED,
  SLI_WIFI_RSP_JSON_EVENT            = 0xEE,
  SLI_WIFI_RSP_OTA_FWUP              = 0xEF,
  SLI_WIFI_RSP_MQTT_REMOTE_TERMINATE = 0xF0,
  SLI_WIFI_RSP_FREQ_OFFSET           = 0xF3,
  SLI_WIFI_RSP_UPDATE_TCP_WINDOW     = 0xF5,
  SLI_WIFI_RSP_GET_RANDOM            = 0xF8
} sli_wifi_response_commands_t;
#endif

/// Timeout scaling factor for internal firmware operations
#ifndef SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF
#define SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF 1
#endif

/// Timeout scaling factor for over the air operations
#ifndef SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF
#define SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF 1
#endif

/// Timeout scaling factor for network operations
#ifndef SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF
#define SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF 1
#endif

/// Base timeout value for internal operations
#define SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE 1000

/// Base timeout value for Wi-Fi management operations
#define SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE 5000

/// Additional wait time(in ms) for command timeout calculations
#ifndef SL_TX_ADDITIONAL_WAIT_TIME
#define SL_TX_ADDITIONAL_WAIT_TIME 0
#endif

/// Default timeout value for commands
#define SLI_DEFAULT_TIMEOUT (30000 + SL_TX_ADDITIONAL_WAIT_TIME)

/// Internal commands timeout defines
/// Timeout value for waiting on operation mode response command
#define SLI_WIFI_RSP_OPERMODE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for waiting on band response command
#define SLI_WIFI_RSP_BAND_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for MAC address response command
#define SLI_WIFI_RSP_MAC_ADDRESS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for disconnect response command
// ponytail: clamped to 10s (was 1000*SF + 30000 = 31s). This is the
// interface-DOWN command (sl_wifi_disconnect / disconnect_ap_client): a
// healthy NWP answers in well under a second, so the 31s only ever fired
// against a dead NWP — where it froze teardown for half a minute per
// command (0x19 recovery field: ap down alone burned 32s).
#define SLI_WIFI_RSP_DISCONNECT_WAIT_TIME (10000)
/// Timeout value for rx stats response command
#define SLI_WIFI_RSP_RX_STATS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Antenna Select response command
#define SLI_WIFI_RSP_ANTENNA_SELECT_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Debug Log response command
#define SLI_COMMON_RSP_DEBUG_LOG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Get RAM Dump response command
#define SLI_COMMON_RSP_GET_RAM_DUMP_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for waiting on feature frame response command
#define SLI_COMMON_RSP_FEATURE_FRAME_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Power Mode response command
#define SLI_WIFI_RSP_PWRMODE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Sub-command IDs for SLI_COMMON_REQ_ENABLE_DISABLE_BLE payload
#define SLI_BLE_SUB_CMD_ENABLE  0x01
#define SLI_BLE_SUB_CMD_DISABLE 0x02

/// Timeout value for BLE Enable/Disable response command
#define SLI_COMMON_RSP_BLE_ENABLE_DISABLE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF Start response command
#define SLI_COMMON_RSP_PUF_START_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF set key response command
#define SLI_COMMON_RSP_PUF_SET_KEY_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF get key response command
#define SLI_COMMON_RSP_PUF_GET_KEY_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF Dis set key response command
#define SLI_COMMON_RSP_PUF_DIS_SET_KEY_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF Dis get key response command
#define SLI_COMMON_RSP_PUF_DIS_GET_KEY_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF Load key response command
#define SLI_COMMON_RSP_PUF_LOAD_KEY_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for AES Encrypt response command
#define SLI_COMMON_RSP_AES_ENCRYPT_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for AES Decrypt response command
#define SLI_COMMON_RSP_AES_DECRYPT_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for AES MAC response command
#define SLI_COMMON_RSP_AES_MAC_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for PUF Intr key response command
#define SLI_COMMON_RSP_PUF_INTR_KEY_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Gain Table response command
#define SLI_WIFI_RSP_GAIN_TABLE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for EAP Config response command
#define SLI_WIFI_RSP_EAP_CONFIG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Set Region AP response command
#define SLI_WIFI_RSP_SET_REGION_AP_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Set Region response command
#define SLI_WIFI_RSP_SET_REGION_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Dynamic Pool response command
#define SLI_WIFI_RSP_DYNAMIC_POOL_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for set multicast filter response command
#define SLI_WIFI_RSP_SET_MULTICAST_FILTER_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Bcast Packets response command
#define SLI_WIFI_RSP_FILTER_BCAST_PACKETS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for set broadcast/multicast filter configuration response command
#define SLI_WIFI_RSP_SET_BC_MC_FILTER_CONFIG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for update multicast allowlist response command
#define SLI_WIFI_RSP_UPDATE_MC_ALLOWLIST_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for set beacon drop threshold response command
#define SLI_WIFI_RSP_SET_BEACON_DROP_THRESHOLD_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for initialize response command
#define SLI_WIFI_RSP_INIT_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Set configuration response command
#define SLI_COMMON_RSP_SET_CONFIG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Get configuration response command
#define SLI_COMMON_RSP_GET_CONFIG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for configuration response command
#define SLI_WIFI_RSP_CONFIG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Query Network Parameters response command
#define SLI_WIFI_RSP_QUERY_NETWORK_PARAMS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for set Certificate response command
#define SLI_WIFI_RSP_SET_CERTIFICATE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for rejoin parameters response command
#define SLI_WIFI_RSP_REJOIN_PARAMS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for get stats response command
#define SLI_WIFI_RSP_GET_STATS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for ext stats response command
#define SLI_WIFI_RSP_EXT_STATS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for broadcast/multicast filter statistics response command
#define SLI_WIFI_RSP_BC_MC_FILTER_STATS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for set rtc timer response command
#define SLI_COMMON_RSP_SET_RTC_TIMER_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for get rtc timer response command
#define SLI_COMMON_RSP_GET_RTC_TIMER_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for assert response command
#define SLI_COMMON_RSP_ASSERT_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for radio response command
#define SLI_WIFI_RSP_RADIO_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for get EFUSE data response command
#define SLI_COMMON_RSP_GET_EFUSE_DATA_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for EFUSE read response command
#define SLI_WIFI_RSP_EFUSE_READ_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for HT Capabilities response command
#define SLI_WIFI_RSP_HT_CAPABILITIES_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for AP Stop response command
// ponytail: clamped to 10s like SLI_WIFI_RSP_DISCONNECT_WAIT_TIME above —
// sl_wifi_ap_stop is the AP interface-DOWN command; healthy NWP replies in
// ms, dead NWP used to burn 31s here (field rec[N] ap_deinit = 32036 ms).
#define SLI_WIFI_RSP_AP_STOP_WAIT_TIME (10000)
/// Timeout value for encrypt crypto response command
#define SLI_COMMON_RSP_ENCRYPT_CRYPTO_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for TA M4 commands response command
#define SLI_COMMON_RSP_TA_M4_COMMANDS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for set transceiver multicast filter response command
#define SLI_WIFI_RSP_SET_TRANSCEIVER_MCAST_FILTER_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for roam parameters response command
#define SLI_WIFI_RSP_ROAM_PARAMS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for socket configuration response command
#define SLI_WIFI_RSP_SOCKET_CONFIG_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for TX test mode response command
#define SLI_WIFI_RSP_TX_TEST_MODE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for transceiver flush data queue response command
#define SLI_WIFI_RSP_TRANSCEIVER_FLUSH_DATA_Q_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for transceiver peer list update response command
#define SLI_WIFI_RSP_TRANSCEIVER_PEER_LIST_UPDATE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for transceiver configuration parameters response command
#define SLI_WIFI_RSP_TRANSCEIVER_CONFIG_PARAMS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for transceiver set channel response command
#define SLI_WIFI_RSP_SET_TRANSCEIVER_CHANNEL_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value to get Full Firmware Version response command
#define SLI_WIFI_RSP_FULL_FW_VERSION_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for sending IP address info command
#define SLI_WIFI_RSP_SEND_IP_ADDRESS_INFO_WAIT_TIME (200 & SLI_WIFI_WAIT_TIME_BIT_MASK)

/// Timeout value for transmit test start response command
#define SLI_WIFI_RSP_TRANSMIT_TEST_START_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))

/// Wi-Fi commands wait time out defines
/// Timeout value for multicast response command
#define SLI_WIFI_RSP_MULTICAST_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Beacon Stop response command
#define SLI_WIFI_RSP_BEACON_STOP_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for mDNS service discovery response command
#define SLI_WIFI_RSP_MDNSD_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for frequency offset response command
#define SLI_WIFI_RSP_FREQ_OFFSET_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for EVM offset response command
#define SLI_RSP_EVM_OFFSET_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for EVM write response command
#define SLI_WIFI_RSP_EVM_WRITE_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for calibration write response command
#define SLI_WIFI_RSP_CALIB_WRITE_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for get DPD data response command
#define SLI_WIFI_RSP_GET_DPD_DATA_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for calibration read response command
#define SLI_WIFI_RSP_CALIB_READ_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for join response command
#define SLI_WIFI_RSP_JOIN_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for TWT response command
#define SLI_WIFI_RSP_TWT_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for 11ax parameters request command
#define SLI_WIFI_RSP_11AX_PARAMS_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for BG Scan response command
#define SLI_WIFI_RSP_BG_SCAN_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for WPS response command
#define SLI_WIFI_RSP_WPS_METHOD_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for WPS extended-credentials response (same scale as WPS method)
#define SLI_WIFI_RSP_WPS_EXTENDED_CREDENTIALS_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for firmware upgrade response command
#define SLI_WIFI_RSP_FWUP_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Host PSK response command
#define SLI_WIFI_RSP_HOST_PSK_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value to set Mac address response command
#define SLI_WIFI_RSP_SET_MAC_ADDRESS_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for RSSI response command
#define SLI_WIFI_RSP_RSSI_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for TSF response command
#define SLI_WIFI_RSP_TSF_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for query GO parameters response command
#define SLI_WIFI_RSP_QUERY_GO_PARAMS_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for wifi get timeout command
#define SLI_WIFI_RSP_TIMEOUT_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))

/// Network commands timeout defines
/// Timeout value for set SNI embedded response command
#define SLI_WIFI_RSP_SET_SNI_EMBEDDED_WAIT_TIME ((60000 * SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for HTTP abort response command
#define SLI_WIFI_RSP_HTTP_ABORT_WAIT_TIME ((100000 * SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for HTTP client PUT response command
#define SLI_WIFI_RSP_HTTP_CLIENT_PUT_WAIT_TIME ((100000 * SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for embedded MQTT client response command
#define SLI_WIFI_RSP_EMB_MQTT_CLIENT_WAIT_TIME ((60000 * SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for DNS server add response command
#define SLI_WIFI_RSP_DNS_SERVER_ADD_WAIT_TIME ((150000 * SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
/// Timeout value for Socket create response command
#define SLI_WIFI_RSP_SOCKET_CREATE_WAIT_TIME ((100000 * SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))
