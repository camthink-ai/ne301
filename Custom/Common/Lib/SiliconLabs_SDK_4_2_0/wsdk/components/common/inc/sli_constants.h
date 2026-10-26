/***************************************************************************/ /**
 * @file
 * @brief Internal Wi-Fi Constants
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
#ifndef SLI_CONSTANTS_H
#define SLI_CONSTANTS_H

#include "sli_routing_utility.h"
#include "sli_command_engine.h"

// Packet queue identifiers
#define SLI_COMMON_Q    0
#define SLI_ZB_Q        1
#define SLI_BT_Q        2
#define SLI_WLAN_MGMT_Q 4
#define SLI_WLAN_DATA_Q 5
#ifdef SAPIS_BT_STACK_ON_HOST
#define SLI_BT_INT_MGMT_Q 6
#define SLI_BT_HCI_Q      7
#endif
#define SLI_LOG_Q 8

// Bus interrupt register values
// Wi-Fi buffer full indication register value from NWP module
#ifndef SLI_WIFI_BUFFER_FULL
#define SLI_WIFI_BUFFER_FULL (1 << 0)
#endif

// BLE buffer full indication register value from NWP module
#ifndef SLI_BLE_BUFFER_FULL
#define SLI_BLE_BUFFER_FULL (1 << 4)
#endif

// RX packet pending register value from NWP module
#define SLI_RX_PKT_PENDING 0x08

// frame descriptor length
#define SLI_FRAME_DESC_LEN 16

#define SLI_NUMBER_OF_SOCKETS 20

#define SLI_SI91X_DNS_REQUEST_MAX_URL_LEN 90
/** Wi-Fi constants */
// Wi-Fi Pre-Shared Key (PSK) length
#define SLI_WIFI_PSK_LEN 64
// Wi-Fi SSID length
#define SLI_WIFI_SSID_LEN 34
// Wi-Fi WPS PIN length
#define SLI_WIFI_WPS_PIN_LEN 8
// Wi-Fi Maximum PMK length
#define SLI_WIFI_MAX_PMK_LENGTH 64
// Wi-Fi Hardware Address Length
#define SLI_WIFI_HARDWARE_ADDRESS_LENGTH 6 // Hardware Address Length
// Wi-Fi Maximum Certificate Send Size
#define SLI_WIFI_MAX_CERT_SEND_SIZE 1400

// Maximum number of stations associated when running as an AP
#define SLI_WIFI_MAX_STATIONS 16

/// Maximum length of the Wi-Fi Pre-Shared Key (PSK) credential.
#define SL_WIFI_MAX_PSK_LENGTH 64

/// Minimum length of the Wi-Fi Pre-Shared Key (PSK) credential.
#define SL_WIFI_MIN_PSK_LENGTH 8

/// Length of the Wi-Fi MAC address.
#define SL_WIFI_MAC_ADDRESS_LENGTH 6

/**
  * @def SL_WIFI_MAX_SSID_LENGTH
  * @brief Defines the maximum length of a Wi-Fi SSID.
  *
  * @details This macro specifies the maximum number of characters (including the null terminator) 
  *          that a Wi-Fi SSID can have. It is used to ensure proper memory allocation and validation 
  *          of SSID strings in Wi-Fi operations.
  *
  * @note The maximum SSID length is 34 characters, which includes the null terminator.
  */
#define SL_WIFI_MAX_SSID_LENGTH 34

#define SLI_SSID_LEN SL_WIFI_MAX_SSID_LENGTH

//**************************** Macros for FEATURE frame Method request START *********************************/
// Preamble Duty Cycle
#define SLI_FEAT_FRAME_PREAMBLE_DUTY_CYCLE (1 << 0)

// Permit Undestinated Packets
#define SLI_FEAT_FRAME_PERMIT_UNDESTINED_PACKETS (1 << 1)

// LP Chain
#define SLI_FEAT_FRAME_LP_CHAIN (1 << 4)

// In Packet Duty Cycle
#define SLI_FEAT_FRAME_IN_PACKET_DUTY_CYCLE (1 << 5)

// PLL Mode
#define PLL_MODE 0

// RF Type
#define RF_TYPE 1 // 0 - External RF 1- Internal RF

// Wireless Mode
#define WIRELESS_MODE 0

// Enable PPP
#define ENABLE_PPP 0

// AFE Type
#define AFE_TYPE 1

#ifndef SLI_FEATURE_ENABLES
#define SLI_FEATURE_ENABLES \
  (SLI_FEAT_FRAME_PREAMBLE_DUTY_CYCLE | SLI_FEAT_FRAME_LP_CHAIN | SLI_FEAT_FRAME_IN_PACKET_DUTY_CYCLE)
#endif

// -----------------------------------------------------------------------------
// Internal macros and enums for vendor-specific IE management
// -----------------------------------------------------------------------------

/// Defines the maximum number of vendor-specific IEs that can be configured.
#define SLI_WIFI_MAX_VENDOR_IE 2

// Defines the Header length in vendor-specific IE buffer.
#define SLI_WIFI_VENDOR_IE_HEADER_LENGTH 2

/// Defines the maximum length of the vendor-specific IE buffer 256 Bytes (254 bytes for data + 2 bytes for header).
#define SLI_WIFI_MAX_VENDOR_IE_BUFFER_LENGTH (254 + SLI_WIFI_VENDOR_IE_HEADER_LENGTH)

/// Defines the frame version for vendor-specific IE.
#define SLI_WIFI_VENDOR_IE_FRAME_VERSION 0

/// Timeout for vendor-specific commands (1 sec).
#define SLI_WIFI_VENDOR_IE_CMD_TIMEOUT 1000

#define SLI_WIFI_COUNTRY_CODE_LENGTH  3
#define SLI_WIFI_MAX_POSSIBLE_CHANNEL 24

// WLAN Management Frame Sub-Type
#define SLI_WIFI_FRAME_SUBTYPE_MASK       0xf0 // WLAN Management Frame Sub-Type Mask
#define SLI_WIFI_FRAME_SUBTYPE_PROBE_RESP 0x50 // WLAN Management Frame Sub-Type Probe Response Frame
#define SLI_WIFI_FRAME_SUBTYPE_BEACON     0x80 // WLAN Management Frame Sub-Type Beacon Frame
#define SLI_WIFI_MINIMUM_FRAME_LENGTH     36   // Minimum Frame Length of WLAN Management Frame
#define SLI_WIFI_HARDWARE_ADDRESS_LENGTH  6    // Hardware Address Length

// WLAN Information Element Type
#define SLI_WLAN_TAG_SSID            0   // WLAN Information Element Type SSID
#define SLI_WLAN_TAG_RSN             48  // WLAN Robust Security Network Information Element
#define SLI_WLAN_TAG_VENDOR_SPECIFIC 221 // WLAN Vendor Specific Information Element

// Authentication key Management Type
#define SLI_AUTH_KEY_MGMT_UNSPEC_802_1X   0x000FAC01 // Unspecified Authentication key Management Type
#define SLI_AUTH_KEY_MGMT_PSK_OVER_802_1X 0x000FAC02 // PSK Authentication key Management Type
#define SLI_AUTH_KEY_MGMT_802_1X_SHA256   0x000FAC05 // SHA256 Authentication key Management Type
#define SLI_AUTH_KEY_MGMT_PSK_SHA256      0x000FAC06 // PSK SHA256 Authentication key Management Type
#define SLI_AUTH_KEY_MGMT_SAE             0x000FAC08 // SAE Authentication key Management Type
#define SLI_AUTH_KEY_MGMT_FT_SAE          0x000FAC09 // FT_SAE Authentication key Management Type

// Authentication key Management Type Flags
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA           0x00000001 // WPA AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA2          0x00000002 // WPA2 AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA_PSK       0x00000004 // WPA_PSK AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA2_PSK      0x00000008 // WPA2_PSK AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_SAE           0x00010000 // SAE AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_FT_SAE        0x00100000 // FT_SAE AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_802_1X_SHA256 0x00020000 // SHA256 AKM Type
#define SLI_WLAN_AUTH_KEY_MGMT_TYPE_PSK_SHA256    0x00040000 // PSK_SHA256 AKM Type

// -----------------------------------------------------------------------------
// Internal macros for multicast allowlist (IP-based) management
// -----------------------------------------------------------------------------
/** Maximum concurrent multicast allowlist entries on the NWP (IPv4 + IPv6 combined).
 *  Valid slot handles returned on ADD are @c 0 .. @c SLI_WIFI_MAX_MC_ALLOWLIST_IP_ADDRESSES - 1
 *  (see @ref sl_wifi_allowlist_mcast_add_ip / @ref sl_wifi_allowlist_mcast_remove_ip).
 *  Host updates use @ref SLI_WIFI_REQ_UPDATE_MC_ALLOWLIST with @ref sli_wifi_mc_allowlist_update_req_t.
 */
#define SLI_WIFI_MAX_MC_ALLOWLIST_IP_ADDRESSES 6
/// Operation field for @ref SLI_WIFI_REQ_UPDATE_MC_ALLOWLIST (command @c 0x5C).
#define SLI_WIFI_MC_ALLOWLIST_OP_ADD        0
#define SLI_WIFI_MC_ALLOWLIST_OP_REMOVE     1
#define SLI_WIFI_MC_ALLOWLIST_OP_REMOVE_ALL 2

// -----------------------------------------------------------------------------
// Generic firmware configuration request (host to NWP)
// -----------------------------------------------------------------------------

/**
 * @brief Byte length of an @ref sli_wifi_fw_config_req_t command buffer including @a _len tail bytes.
 * @details @c sizeof(sli_wifi_fw_config_req_t) counts only the fixed header (flexible member excluded).
 *          For @a _len @c 0 (e.g. @ref sl_wifi_set_beacon_drop_threshold), this equals @c sizeof(struct).
 */
#define SLI_WIFI_FW_CONFIG_REQ_TOTAL_SIZE(_len) ((size_t)sizeof(sli_wifi_fw_config_req_t) + (size_t)(_len))

/**
 * @def SLI_SET_BEACON_DROP_THRESHOLD
 * @brief Bit in @ref sli_wifi_fw_config_req_t::config_bitmap that selects beacon drop threshold (scalar in @c value; @c length 0)
 *        for @ref SLI_WIFI_REQ_SET_BEACON_DROP_THRESHOLD / @ref sl_wifi_set_beacon_drop_threshold.
 */
#define SLI_SET_BEACON_DROP_THRESHOLD BIT(0)

/// Timeout scaling factor for internal firmware operations
#ifndef SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF
#define SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF 1
#endif
/// Additional wait time(in ms) for command timeout calculations
#ifndef SL_TX_ADDITIONAL_WAIT_TIME
#define SL_TX_ADDITIONAL_WAIT_TIME 0
#endif

/// Base timeout value for internal operations
#define SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE 1000
/// Default timeout value for commands
#define SLI_DEFAULT_TIMEOUT (30000 + SL_TX_ADDITIONAL_WAIT_TIME)

#define SLI_WIFI_RSP_QUERY_NETWORK_PARAMS_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))

#define SLI_WIFI_DNS_RETRY_COUNT 1

// driver TX/RX packet structure
/** Command header size in @ref sl_wifi_system_packet_t (bytes). Same value as @c SLI_FRAME_DESC_LEN (16). */
#define SL_SI91X_WIFI_PACKET_DESC_SIZE SLI_FRAME_DESC_LEN

#define SLI_WIFI_WAIT_ON_THREAD_ID 0 ///< Wait on the calling thread's event flags (used by synchronous commands).
#define SLI_WIFI_WAIT_ON_EVENT_ID  1 ///< Wait on the packet-type shared event flags (used by socket/select paths).

#ifndef SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME
#define SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME 1000 // 1 second to wait for a command buffer
#endif
#define SLI_SI91X_DNS_RESPONSE_MAX_ENTRIES 10

/// Timeout scaling factor for internal firmware operations
#ifndef SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF
#define SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF 1
#endif

/// Additional wait time(in ms) for command timeout calculations
#ifndef SL_TX_ADDITIONAL_WAIT_TIME
#define SL_TX_ADDITIONAL_WAIT_TIME 0
#endif

/// Timeout value for Power Mode response command
#define SLI_WIFI_RSP_PWRMODE_WAIT_TIME \
  ((SLI_WIFI_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))

#ifdef SLI_SI91X_MCU_INTERFACE
#define SLI_CONNECTED_M4_BASED_PS 4
#endif

/** NOTE: For power save related info
 * https://docs.silabs.com/rs9116/wiseconnect/rs9116w-wifi-at-command-prm/latest/wlan-commands#rsi-pwmode----power-mode
 * ****************************** POWER RELATED DEFINES START *******************************/
#define SLI_POWER_MODE_DISABLE      0
#define SLI_CONNECTED_SLEEP_PS      1
#define SLI_CONNECTED_GPIO_BASED_PS 2
#define SLI_CONNECTED_MSG_BASED_PS  3

#define SLI_GPIO_BASED_DEEP_SLEEP 8
#define SLI_MSG_BASED_DEEP_SLEEP  9

#ifdef SLI_SI91X_MCU_INTERFACE
#define SLI_M4_BASED_DEEP_SLEEP 10
#endif

#define SLI_ULP_WITH_RAM_RETENTION        1
#define SLI_MAX_PSP                       0
#define SLI_FAST_PSP                      1
#define SLI_ULP_WITHOUT_RAM_RET_RETENTION 2
#define DEFAULT_BEACON_MISS_IGNORE_LIMIT  1
#define SLI_DEFAULT_MONITOR_INTERVAL      50

#define ENABLE_MAC_INFO       BIT(0)
#define QOS_EN                BIT(12)
#define BROADCAST_IND         BIT(9)
#define PER_CONT_MODE         1
#define FRAME_DESC_SZ         16
#define MIN_802_11_HDR_LEN    24
#define SLI_SEND_MAC_FRAME    0x0
#define SLI_11AX_BE_RATE_MASK 0x18f
/// Nominal preamble length offset
#define RATE_OFFSET_NOMINAL_PE 5
/// Guard interval and LTF offset
#define RATE_OFFSET_GI_LTF 11
/// DCM offset
#define RATE_OFFSET_DCM 13
/// Coding type offset
#define RATE_OFFSET_CODING_TYPE 4

/** Internal TWT auto-selection defaults (SDK use only) */
#define SLI_TWT_INTERNAL_DEVICE_AVERAGE_THROUGHPUT     20000
#define SLI_TWT_INTERNAL_EXTRA_WAKE_DURATION_PERCENT   0
#define SLI_TWT_INTERNAL_TOLERABLE_DEVIATION           10
#define SLI_TWT_INTERNAL_DEFAULT_WAKE_INTERVAL_MS      1024
#define SLI_TWT_INTERNAL_DEFAULT_WAKE_DURATION_MS      8
#define SLI_TWT_INTERNAL_BEACON_WAKE_UP_COUNT_AFTER_SP 2

/* NWP reserved gaps in text_tx_cmd: 2 bytes after base_info + 4 after per_params = 6 bytes. */
#define SLI_WIFI_TX_TEST_NWP_RESERVED_LEN 2U

/* Largest v2 PER command: base_info + 6 reserved + 11ax per = 59 bytes (2 after base + 4 after per). */
#define SLI_WIFI_TX_TEST_CMD_MAX_LEN                                                   \
  (sizeof(sli_wifi_tx_test_base_info_wire_t) + sizeof(sli_wifi_11ax_per_params_wire_t) \
   + (SLI_WIFI_TX_TEST_NWP_RESERVED_LEN * 3U))

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
  SLI_WIFI_REQ_SIGNAL_QUALITY_STATS         = 0x3B, ///< Signal Quality Statistics
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
  SLI_WIFI_REQ_RATE                         = 0x87, ///< Fixed transmit rate
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
  SLI_WIFI_REQ_SET_ADVANCED_CONFIG       = 0x96, ///< Set advanced Wi-Fi configuration (sub-cmd payload)
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
  SLI_WIFI_RSP_SIGNAL_QUALITY_STATS         = 0x3B, ///< Signal Quality Statistics
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
  SLI_WIFI_RSP_RATE                         = 0x87, ///< Fixed transmit rate
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
  SLI_WIFI_RSP_SET_ADVANCED_CONFIG       = 0x96, ///< Set advanced Wi-Fi configuration response
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

/// Si91x specific command type
typedef enum {
  SLI_WIFI_COMMON_CMD   = 0, ///< SI91X Common Command
  SLI_WIFI_WLAN_CMD     = 1, ///< SI91X Wireless LAN Command
  SLI_SI91X_NETWORK_CMD = 2, ///< SI91X Network Command
  SLI_SI91X_SOCKET_CMD  = 3, ///< SI91X Socket Command
  SLI_SI91X_CMD_MAX     = 4  ///< SI91X Maximum Command value
} sli_wifi_command_type_t;

/// Lower 16 bits hold raw wait time value
#define SLI_WIFI_WAIT_TIME_BIT_MASK 0x1FFFFFFF

/// Bit to enable SSL feature
#define SL_SI91X_ENABLE_TLS BIT(0)

/// Bitmap to enable TLS version 1.0
#define SL_SI91X_TLS_V_1_0 BIT(2)

/// Bitmap to enable TLS version 1.2
#define SL_SI91X_TLS_V_1_2 BIT(3)

/// Bitmap to enable TLS version 1.1
#define SL_SI91X_TLS_V_1_1 BIT(4)

#if defined(SLI_SI917) || defined(DOXYGEN)
/// Bitmap to enable TLS version 1.3
#define SL_SI91X_TLS_V_1_3 BIT(8)
#endif

/// Bitmap to enable DTLS version 1.0
#define SL_SI91X_DTLS_V_1_0 BIT(2)

/// Bitmap to enable DTLS version 1.2
#define SL_SI91X_DTLS_V_1_2 BIT(3)

/// Bit to enable DTLS feature
#define SL_SI91X_ENABLE_DTLS BIT(13)

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

typedef enum {
  SLI_WIFI_COMMAND_PACKET = 0,
  SLI_WIFI_DATA_PACKET,
  SLI_WIFI_SOCKET_DATA_PACKET,
  SLI_WIFI_COMMAND_ENGINE_PACKET = 0
} sli_wifi_command_engine_packet_t;

extern sli_routing_table_t wifi_command_engine_routing_table;

typedef enum {
  SLI_WIFI_COMMAND_ENGINE_COMMON_COMMAND_PACKET = 0,
  SLI_WIFI_COMMAND_ENGINE_WIFI_COMMAND_PACKET,
  SLI_WIFI_COMMAND_ENGINE_NETWORK_COMMAND_PACKET,
  SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET,
} sli_wifi_command_engine_packet_types_t;

typedef enum {
  SLI_WIFI_ASYNC_EVENT_HANDLER_COMMON_EVENT = 0,
  SLI_WIFI_ASYNC_EVENT_HANDLER_WIFI_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_NETWORK_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_BLE_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_CMD_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_DATA_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_ERROR_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_NWP_LOG_EVENT,
} sli_wifi_async_event_handler_events_t;

/// Si91x specific command type
typedef enum {
  SLI_WLAN_COMMON_CMD  = 0, ///< SI91X Common Command
  SLI_WLAN_WIFI_CMD    = 1, ///< SI91X Wireless LAN Command
  SLI_WLAN_NETWORK_CMD = 2, ///< SI91X Network Command
  SLI_WLAN_SOCKET_CMD  = 3, ///< SI91X Socket Command
  SLI_WLAN_CMD_MAX     = 4  ///< SI91X Maximum Command value
} sli_wlan_command_type_t;

/**
  * @enum sl_wifi_vap_id_t
  * @brief Wi-Fi VAP ID
  */
typedef enum {
  SL_WIFI_CLIENT_VAP_ID,   ///< Wi-Fi Client VAP ID
  SL_WIFI_AP_VAP_ID,       ///< Wi-Fi Access point VAP ID
  SL_WIFI_CLIENT_VAP_ID_1, ///< Wi-Fi Client 1 VAP ID
  SL_WIFI_AP_VAP_ID_1,     ///< Wi-Fi Access point 1 VAP ID
} sl_wifi_vap_id_t;

extern sli_queue_t event_queue[];
extern osEventFlagsId_t sli_wifi_event_engine_event_id;

#define SLI_SI91X_2BYTE_FIELD_SIZE 2
#define SLI_SI91X_4BYTE_FIELD_SIZE 4

typedef enum { SLI_SI91X_CLIENT = 0, SLI_SI91X_AP = 1, SLI_SI91X_MAX_INTERFACES } sli_si91x_interfaces_t;

#endif // SLI_CONSTANTS_H