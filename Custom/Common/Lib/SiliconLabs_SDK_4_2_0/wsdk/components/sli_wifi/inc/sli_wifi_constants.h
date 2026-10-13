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

#include "sli_constants.h"

/// Timeout value for Wi-Fi join command
#define SLI_WIFI_CONNECT_TIMEOUT 120000 // in ms, i.e., 120s
// User gain table related
#define SLI_MAX_GAIN_TABLE_SIZE_WITH_SU_TB 160

// Shared Wi-Fi buffer block size (default 1640). Override via SLI_WIFI_BUFFER_CONFIG_BLOCK_SIZE=2324 in project defines.
#ifndef SLI_WIFI_BUFFER_BLOCK_SIZE
#if (SLI_WIFI_BUFFER_CONFIG_BLOCK_SIZE)
#define SLI_WIFI_BUFFER_BLOCK_SIZE SLI_WIFI_BUFFER_CONFIG_BLOCK_SIZE
#else
#define SLI_WIFI_BUFFER_BLOCK_SIZE 1640
#endif
#endif

// Buffer block count for the buffer manager pools (default 10). Override via SLI_WIFI_BUFFER_CONFIG_BLOCK_COUNT=<n> in project config.

#if (SLI_WIFI_BUFFER_CONFIG_BLOCK_COUNT)
#define SLI_WIFI_BUFFER_BLOCK_COUNT SLI_WIFI_BUFFER_CONFIG_BLOCK_COUNT
#else
#define SLI_WIFI_BUFFER_BLOCK_COUNT 10
#endif

// ponytail: max command payload fitting one buffer-manager pool block (block
// size minus hidden pool management pointer and sl_wifi_system_packet_t
// header — the sizeof terms evaluate at the macro's use sites, where the
// packet struct is complete). Larger payloads would memcpy past the block
// and corrupt the pool.
#define SLI_WIFI_MAX_CMD_PAYLOAD \
  (SLI_WIFI_BUFFER_BLOCK_SIZE - sizeof(void *) - sizeof(sl_wifi_system_packet_t))

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
 * @details Used with sl_wifi_set_test_tx_power(); value 310 corresponds to 31.0 dBm.
 */
#define SLI_WIFI_TX_POWER_DECIDBM_MAX 310
/// Default listen interval multiplier for STA (association / power save).
#define DEFAULT_LISTEN_INTERVAL_MULTIPLIER 1

#define SLI_WIFI_SET_WPS_METHOD_PIN   1
#define SLI_WIFI_SET_WPS_GENERATE_PIN 1

/// Maximum WPS credential records per JOIN response session (tri-band). Must match NWP / supplicant configuration.
#define SLI_WIFI_MAX_WPS_CREDENTIALS 3

/// Flags for IP address availability used in sli_wifi_ip_address_info_t structure.
#define SLI_WIFI_IPV4_AVAILABLE (1U << 0) ///< Bit 0: IPv4 address is available
#define SLI_WIFI_IPV6_AVAILABLE (1U << 1) ///< Bit 1: IPv6 address is available

/// Bit fields of the 'options' member of sli_wifi_ap_config_request.
#define SLI_WIFI_AP_OPT_KEEPALIVE_TYPE_MASK      0x03   ///< Bits 0-1: AP keep-alive type
#define SLI_WIFI_AP_OPT_BEACON_STOP              BIT(2) ///< Bit 2: stop beaconing when no clients are associated
#define SLI_WIFI_AP_OPT_DYNAMIC_HIDDEN_SSID_CONF BIT(3) ///< Bit 3: dynamic hidden SSID configuration

/**
 * @def SL_WIFI_FEAT_FW_UPDATE_NEW_CODE
 * @brief Indicates support for a new set of firmware update result codes. This bit is used for internal purpose.
 * @details
 * This bit in the feature bitmap is used to inform the NWP firmware whether
 * the host supports a new set of result codes to differentiate firmware update
 * results from other non-firmware-related results. If this bit is set,
 * the NWP firmware would send result codes from the new set after a firmware update.
 * If the bit is not set, the legacy result codes would be used.
 */
#ifndef SL_WIFI_FEAT_FW_UPDATE_NEW_CODE
#define SL_WIFI_FEAT_FW_UPDATE_NEW_CODE BIT(16)
#endif

/**
 * @def SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE
 * @brief Alias for SL_WIFI_FEAT_FW_UPDATE_NEW_CODE
 * @details Ensure smooth migration to SL_WIFI_FEAT_FW_UPDATE_NEW_CODE
 * @note The macro SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE is being deprecated and
 *       will be removed in the future. Please use
 *       SL_WIFI_FEAT_FW_UPDATE_NEW_CODE instead.
 */
#ifndef SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE
#define SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE SL_WIFI_FEAT_FW_UPDATE_NEW_CODE
#endif

/// Wifi Timeout types
typedef enum {
  SLI_WIFI_AUTHENTICATION_ASSOCIATION_TIMEOUT =
    0, ///< Used for setting association and authentication timeout request in milliseconds
  SLI_WIFI_CHANNEL_ACTIVE_SCAN_TIMEOUT, ///< Used for setting dwell time per channel in milliseconds during active scan
  SLI_WIFI_KEEP_ALIVE_TIMEOUT,          ///< Used for setting WLAN keep alive time in seconds
  SLI_WIFI_CHANNEL_PASSIVE_SCAN_TIMEOUT ///< Used for setting dwell time per channel in milliseconds during passive scan
} sli_wifi_timeout_type_t;

typedef enum { SET_REGION_CODE_FROM_BEACONS, SET_REGION_CODE_FROM_USER } sli_wifi_set_region_code_command_t;

typedef enum { SLI_WIFI_NO_ENCRYPTION, SLI_WIFI_TKIP_ENCRYPTION, SLI_WIFI_CCMP_ENCRYPTION } sli_wifi_encryption_t;

/// Timeout scaling factor for over the air operations
#ifndef SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF
#define SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF 1
#endif

/// Timeout scaling factor for network operations
#ifndef SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF
#define SL_WIFI_NETWORK_COMMANDS_TIMEOUT_SF 1
#endif

/// Base timeout value for Wi-Fi management operations
#define SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE 5000

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

/// Timeout value for set advanced configuration response command
#define SLI_WIFI_RSP_SET_ADVANCED_CONFIG_WAIT_TIME \
  ((SLI_WIFI_MANAGEMENT_COMMANDS_BASE_VALUE * SL_WIFI_MANAGEMENT_COMMANDS_TIMEOUT_SF) + (SLI_DEFAULT_TIMEOUT))

#endif // SLI_WIFI_CONSTANTS_H
