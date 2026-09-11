/***************************************************************************/ /**
 * @file    sli_wifi_types.h
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
#ifndef SLI_WIFI_TYPES_H
#define SLI_WIFI_TYPES_H
#include <stddef.h>
#include <stdbool.h>
#include "sl_wifi_device.h"
#include "sl_wifi_constants.h"
#include "sl_wifi_types.h"
#include "sli_queue_manager.h"
#include "cmsis_os2.h"

#define SLI_WIFI_PSK_LEN                 64
#define SLI_WIFI_SSID_LEN                34
#define SLI_WIFI_WPS_PIN_LEN             8
#define SLI_WIFI_MAX_PMK_LENGTH          64
#define SLI_WIFI_HARDWARE_ADDRESS_LENGTH 6 // Hardware Address Length
#define SLI_WIFI_MAX_CERT_SEND_SIZE      1400

// Maximum number of stations associated when running as an AP
#define SLI_WIFI_MAX_STATIONS 16

//! @cond Doxygen_Suppress
//**************************** Macros for FEATURE frame Method request START *********************************/
#define SLI_FEAT_FRAME_PREAMBLE_DUTY_CYCLE       (1 << 0)
#define SLI_FEAT_FRAME_PERMIT_UNDESTINED_PACKETS (1 << 1)
#define SLI_FEAT_FRAME_LP_CHAIN                  (1 << 4)
#define SLI_FEAT_FRAME_IN_PACKET_DUTY_CYCLE      (1 << 5)

#define PLL_MODE      0
#define RF_TYPE       1 // 0 - External RF 1- Internal RF
#define WIRELESS_MODE 0
#define ENABLE_PPP    0
#define AFE_TYPE      1
#ifndef SLI_FEATURE_ENABLES
#define SLI_FEATURE_ENABLES \
  (SLI_FEAT_FRAME_PREAMBLE_DUTY_CYCLE | SLI_FEAT_FRAME_LP_CHAIN | SLI_FEAT_FRAME_IN_PACKET_DUTY_CYCLE)
#endif

//**************************** Macros for FEATURE frame Method request END *********************************/

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

/// Efuse data information
typedef union {
  uint8_t mfg_sw_version; ///< Manufacturing PTE software version
  uint16_t pte_crc;       ///< PTE CRC value
} sli_wifi_efuse_data_t;

/// Antenna select command request structure
typedef struct {
  uint8_t antenna_value; ///< Antenna value to set

  uint8_t gain_2g; ///< Antenna 2G gain value

  uint8_t gain_5g; ///< Antenna 5G gain value

} sli_wifi_antenna_select_t;

//! user configurable gain table structure
typedef struct {
  uint8_t band;               ///< band value
  uint8_t bandwidth;          ///< bandwidth value
  uint16_t size;              ///< payload size
  uint8_t x_offset;           ///< X: bump up offset for 52 tone RU
  uint8_t y_offset;           ///< Y: bump up offset for 106 tone RU
  uint8_t gain_table_version; ///< 0: Old gain table, 1:New gain table
  uint8_t reserved;           ///< Reserved
  uint8_t gain_table[];       ///< payload
} sli_wifi_gain_table_info_t;

/// 11AX configuration parameters
typedef struct {
  uint8_t guard_interval; ///< Period of time inserted between two packets in wireless transmission. Range : 0 - 3
  uint8_t nominal_pe;     ///< Nominal Packet extension Range: 0 - 2
  uint8_t dcm_enable;     ///< Enable or disable dual carrier modulation (DCM). 0 - Disable DCM, 1 - Enable DCM
  uint8_t ldpc_enable;    ///< Enable or disable low-density parity-check (LDPC). 0 - Disable LDPC, 1 - Enable LDPC
  uint8_t
    ng_cb_enable; ///< Enable or disable non-contiguous channel bonding (NG CB). 0 - Disable NG CB, 1 - Enable NG CB
  uint8_t ng_cb_values; ///< Values of non-contiguous channel bonding (NG CB). Range: 0x00 - 0x11
  uint8_t
    uora_enable; ///< Enable or disable uplink orthogonal frequency division multiple random access (UORA). 0 - Disable uora, 1 - Enable uora.
  uint8_t
    trigger_rsp_ind; ///< Trigger_Response_Indication. BIT(0) ? Trigger Response For BE, BIT(1) ? Trigger Response For BK, BIT(2) ? Trigger Response For VI, BIT(3) ? Trigger Response For VO
  uint8_t ipps_valid_value;   ///< IPPS valid value
  uint8_t tx_only_on_ap_trig; ///< Reserved for future use
  uint8_t twt_support;        ///< Enable or Disable TWT. 0 - Disable TWT, 1 - Enable TWT.
  uint8_t
    config_er_su; ///< Extended Range Single User. 0 - NO ER_SU support, 1 - Use ER_SU rates along with Non_ER_SU rates, 2 - Use ER_SU rates only
  uint8_t beamformee_support; ///< Flag indicating Beamformee support.
                              /// *        0: Enabled, 1: Disable SU (Single User), 2 : Disable MU (Multi User).
} sli_wifi_11ax_config_params_t;

/// bg scan command request structure
typedef struct {
  /// enable or disable BG scan
  uint16_t bgscan_enable;

  /// Is it instant bgscan or normal bgscan
  uint16_t enable_instant_bgscan;

  /// bg scan threshold value
  uint16_t bgscan_threshold;

  /// tolerance threshold
  uint16_t rssi_tolerance_threshold;

  /// periodicity
  uint16_t bgscan_periodicity;

  /// active scan duration
  uint16_t active_scan_duration;

  /// passive scan duration
  uint16_t passive_scan_duration;

  /// multi probe
  uint8_t multi_probe;
} sli_wifi_request_bg_scan_t;

/// Scan command request structure
typedef struct {
  uint8_t channel[4];              ///< RF channel to scan, 0=All, 1-14 for 2.4 GHz channels 1-14
  uint8_t ssid[SLI_WIFI_SSID_LEN]; ///< SSID to scan, 0=All
  uint8_t pscan_bitmap[4];         ///< Pscan bitmap
  uint8_t _reserved;               ///< Reserved
  uint8_t scan_feature_bitmap;     ///< Scan feature bitmap
  uint8_t channel_bit_map_2_4[2];  ///< Channel bit map for 2.4GHz
  uint8_t channel_bit_map_5[4];    ///< Channel bit map for 5GHz
} sli_wifi_request_scan_t;

/// Enterprise configuration command request structure
typedef struct {
  uint8_t eap_method[32]; ///< EAP method

  uint8_t inner_method[32]; ///< Inner method

  uint8_t user_identity[64]; ///< Username

  uint8_t password[128]; ///< Password

  int8_t okc_enable[4]; ///< Opportunistic key caching enable

  uint8_t private_key_password[82]; ///< Private key password for encrypted private keys

} sli_wifi_request_eap_config_t;

/// join command request  structure
#pragma pack(1)
typedef struct {
  /// reserved bytes:Can be used for security Type
  uint8_t reserved1;

  /// 0- Open, 1-WPA, 2-WPA2,6-MIXED_MODE, 7-WPA3, 8-WP3_Transition
  uint8_t security_type;

  /// data rate, 0=auto, 1=1 Mbps, 2=2 Mbps, 3=5.5Mbps, 4=11 Mbps, 12=54 Mbps
  uint8_t data_rate;

  /// transmit power level
  uint8_t power_level;

  /// pre-shared key, 63-byte string , last character is NULL
  uint8_t psk[SLI_WIFI_PSK_LEN];

  /// ssid of access point to join to, 34-byte string
  uint8_t ssid[SLI_WIFI_SSID_LEN];

  /// feature bitmap for join
  uint8_t join_feature_bitmap;

  /// reserved bytes
  uint8_t reserved2[1];

  /// Multiply the listen interval by the configured value in the association request. Default is 1, max recommended is 10. Higher values may cause interoperability issues.
  uint8_t listen_interval_multiplier;

  /// length of ssid given
  uint8_t ssid_len;

  /// listen interval
  uint32_t listen_interval;

  /// vap id, 0 - station mode, 1 - AP mode
  uint8_t vap_id;

  /// join bssid for mac based join
  uint8_t join_bssid[6];
} sli_wifi_join_request_t;
#pragma pack()

/// Roam parameters request
typedef struct {
  uint32_t roam_enable;           ///< Enable or disable roaming
  uint32_t roam_threshold;        ///< roaming threshold
  uint32_t roam_hysteresis;       ///< roaming hysteresis
} sli_wifi_request_roam_params_t; /// roam parameters request

/// Rejoin parameters
typedef struct {
  uint32_t max_retry_attempts; ///< Maximum number of retries before indicating join failure.
  uint32_t scan_interval;      ///< Scan interval between each retry.
  uint32_t
    beacon_missed_count; ///< Number of missed beacons that will trigger rejoin. Minimum value of beacon_missed_count is 40.
  uint32_t first_time_retry_enable; ///< Retry enable or disable for first time joining.
} sli_wifi_rejoin_params_t;

/// Legacy WLAN host-to-NWP request for deprecated broadcast/TIM filter + beacon threshold (single frame).
/// @details Replaced for new applications by @ref sl_wifi_set_groupcast_filter_config,
///          @ref sl_wifi_set_beacon_drop_threshold, and multicast IP allowlist commands
///          (@ref SLI_WIFI_REQ_SET_BC_MC_FILTER_CONFIG, @ref SLI_WIFI_REQ_UPDATE_MC_ALLOWLIST, etc.).
typedef struct {
  uint8_t beacon_drop_threshold[2];       ///< Beacon drop threshold
  uint8_t filter_bcast_in_tim;            ///< Filter broadcast in TIM
  uint8_t filter_bcast_tim_till_next_cmd; ///< Filter broadcast TIM till next command
} sli_wifi_request_wlan_filter_broadcast_t;

/// PSK command request  structure
typedef struct {
  uint8_t type;                         ///< psk type , 1-psk alone, 2-pmk, 3-generate pmk from psk
  uint8_t psk_or_pmk[SLI_WIFI_PSK_LEN]; ///< psk or pmk
  uint8_t ap_ssid[SLI_WIFI_SSID_LEN];   ///< access point ssid: used for generation pmk
} sli_wifi_request_psk_t;

/// Access point configuration parameters
#pragma pack(1)
typedef struct {
  /// channel number of the access point
  uint16_t channel;

  /// ssid of the AP to be created
  uint8_t ssid[SLI_WIFI_SSID_LEN];

  /// security type of the Access point
  uint8_t security_type;

  /// encryption mode
  uint8_t encryption_mode;

  /// password in case of security mode
  uint8_t psk[SLI_WIFI_MAX_PMK_LENGTH];

  /// Beacon interval of the access point in time units (1 TU = 1024 microseconds). Allowed values are integers in the range of 100 to 1000 in multiples of 100.
  uint16_t beacon_interval;

  /// DTIM period of the access point
  uint16_t dtim_period;

  /// Bitmap controlling AP keep alive type, and Hidden SSID dynamic configurability.
  /// 0-1st bit - keepalive type
  /// 2nd bit - beacon stop
  /// 3rd bit - Hidden SSID dynamic configurability
  uint8_t options;

  /// Keep alive time after which AP will disconnect the station if there are no wireless exchanges from station to AP.
  uint8_t ap_keepalive_period;

  /// Number of clients supported
  uint16_t max_sta_support;
} sli_wifi_ap_config_request;
#pragma pack()

/// High throughputs enable command
typedef struct {
  uint16_t mode_11n_enable; ///< 11n mode enable
  uint16_t ht_caps_bitmap;  ///< HT caps bitmap
} sli_wifi_request_ap_high_throughput_capability_t;

/// disassociate command request structure
#pragma pack(1)
typedef struct {
  /// 0- Module in Client mode, 1- AP mode
  uint16_t mode_flag;

  /// client MAC address, Ignored/Reserved in case of client mode
  sl_mac_address_t client_mac_address;
} sli_wifi_disassociation_request_t;
#pragma pack()

/// WPS method request
typedef struct {
  /// wps method: 0 - push button, 1 - pin method
  uint16_t wps_method;

  /// If 0 - validate given pin, 1 - generate new pin
  uint16_t generate_pin;

  /// wps pin for validation
  uint8_t wps_pin[SLI_WIFI_WPS_PIN_LEN];
} sli_wifi_wps_method_request_t;

#pragma pack(1)
typedef struct {
  /// wps method: 0 - push button, 1 - pin method
  uint16_t wps_method;

  /// If 0 - validate given pin, 1 - generate new pin
  uint16_t generate_pin;

  /// wps pin for validation
  uint8_t wps_pin[9];

  uint8_t auto_connect;

  uint16_t reserved;
} sli_wifi_wps_config_t;
#pragma pack()

/// per stats command request structure
typedef struct {
  /// 0 - start , 1 -stop
  uint8_t start[SL_WIFI_RX_STATS_REQUEST_CMD_OCTETS];

  /// channel number
  uint8_t channel[SL_WIFI_RX_STATS_REQUEST_CHANNEL_OCTETS];
} sli_wifi_request_rx_stats_t;

typedef struct {
  sl_wifi_performance_profile_v2_t wifi_performance_profile;
  sl_bt_performance_profile_t bt_performance_profile;
  sl_wifi_system_coex_mode_t coex_mode;
} sli_wifi_performance_profile_t;

/// Internal structure to track individual feature configurations
typedef struct {
  sl_wifi_pll_mode_t pll_mode;       ///< Configured PLL mode value
  sl_wifi_power_chain_t power_chain; ///< Configured power chain value
} sli_wifi_feature_frame_config_t;

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

/// Si91x specific command type
typedef enum {
  SLI_WIFI_COMMON_CMD   = 0, ///< SI91X Common Command
  SLI_WIFI_WLAN_CMD     = 1, ///< SI91X Wireless LAN Command
  SLI_SI91X_NETWORK_CMD = 2, ///< SI91X Network Command
  SLI_SI91X_BT_CMD      = 3, ///< SI91X Bluetooth Command
  SLI_SI91X_SOCKET_CMD  = 4, ///< SI91X Socket Command
  SI91X_CMD_MAX         = 5  ///< SI91X Maximum Command value
} sli_wifi_command_type_t;

// Scan Information
typedef struct sli_scan_info_s {
  struct sli_scan_info_s *next;
  uint8_t channel;                                 ///< Channel number of the AP
  uint8_t security_mode;                           ///< Security mode of the AP
  uint8_t rssi;                                    ///< RSSI value of the AP
  uint8_t network_type;                            ///< AP network type
  uint8_t ssid[34];                                ///< SSID of the AP
  uint8_t bssid[SLI_WIFI_HARDWARE_ADDRESS_LENGTH]; ///< BSSID of the AP
  bool wpa_vendor_ie_seen;                         ///< true if WPA vendor IE was present (parsing only, not stored)
  uint16_t seen_count;                             ///< Number of times the same AP was observed in the received frames
} sli_scan_info_t;

/// Si91x specific station information
typedef struct {
  uint8_t ip_version[2]; ///< IP version if the connected client
  uint8_t mac[6];        ///< Mac Address of the connected client
  union {
    uint8_t ipv4_address[4];  ///< IPv4 address of the connected client
    uint8_t ipv6_address[16]; ///< IPv6 address of the connected client

  } ip_address; ///< IP address
} sli_wifi_station_info_t;

/// go paramas response structure
#pragma pack(1)
typedef struct {
  /// SSID of the P2p GO
  uint8_t ssid[SLI_WIFI_SSID_LEN];

  /// BSSID of the P2p GO
  uint8_t mac_address[6];

  /// Operating channel of the GO
  uint8_t channel_number[2];

  /// PSK of the GO
  uint8_t psk[64];

  /// IPv4 Address of the GO
  uint8_t ipv4_address[4];

  /// IPv6 Address of the GO
  uint8_t ipv6_address[16];

  /// Number of stations Connected to GO
  uint8_t sta_count[2];

  /// Station information
  sli_wifi_station_info_t sta_info[SLI_WIFI_MAX_STATIONS];
} sli_wifi_client_info_response;
#pragma pack()

/// Set certificate information structure
typedef struct {
  uint16_t total_len;          ///< total length of the certificate
  uint8_t certificate_type;    ///< type of certificate
  uint8_t more_chunks;         ///< more chunks flag
  uint16_t certificate_length; ///< length of the current segment
  uint8_t certificate_inx;     ///< index of certificate
  uint8_t key_password[127];   ///< reserved
} sli_wifi_cert_info_t;

/// Set certificate command request structure
typedef struct {
  sli_wifi_cert_info_t cert_info;                   ///< certificate information structure
  uint8_t certificate[SLI_WIFI_MAX_CERT_SEND_SIZE]; ///< certificate
} sli_wifi_req_set_certificate_t;

/// Request timeout Structure
typedef struct {
  uint32_t timeout_bitmap; ///< Timeout bitmap
  uint16_t timeout_value;  ///< Timeout value
} sli_wifi_request_timeout_t;

// Config command request structure
typedef struct {
  /// config type
  uint16_t config_type;

  /// value to set
  uint16_t value;
} sli_wifi_config_request_t;

/// Set region command request structure
typedef struct {
  /// Enable or disable set region from user: 1-take from user configuration,0-Take from Beacons
  uint8_t set_region_code_from_user_cmd;

  /// region code(1-US,2-EU,3-JP,4-World Domain,5-KR)
  uint8_t region_code;

  /// module type (0- Without on board antenna, 1- With on board antenna)
  uint16_t module_type;
} sli_wifi_set_region_request_t;

/// Set region in AP mode command request structure
typedef struct {
  /// Enable or disable set region from user: 1-take from user configuration, 0-Take US or EU or JP
  uint8_t set_region_code_from_user_cmd;

  /// region code(1-US,2-EU,3-JP)
  uint8_t country_code[SLI_WIFI_COUNTRY_CODE_LENGTH];

  /// No of rules
  uint32_t no_of_rules;

  /// Channel information
  struct {
    uint8_t first_channel;  ///< First channel
    uint8_t no_of_channels; ///< Number of channels
    uint8_t max_tx_power;   ///< Max Tx power
  } channel_info[SLI_WIFI_MAX_POSSIBLE_CHANNEL];
} sli_wifi_set_region_ap_request_t;

/***************************************************************************/ /**
 * @brief
 *   Enum to specify the action for vendor-specific IE management.
 ******************************************************************************/
typedef enum SL_ATTRIBUTE_PACKED {
  SLI_WIFI_VENDOR_IE_ACTION_ADD        = 0,   ///< Add a vendor-specific IE
  SLI_WIFI_VENDOR_IE_ACTION_REMOVE     = 1,   ///< Remove a vendor-specific IE
  SLI_WIFI_VENDOR_IE_ACTION_REMOVE_ALL = 0xFF ///< Remove all vendor-specific IEs
} sli_wifi_vendor_ie_action_t;

/***************************************************************************/ /**
 * @brief
 *   Packet structure for sending vendor-specific IE to firmware.
 ******************************************************************************/
typedef struct {
  uint16_t version;           ///< Version number for the structure
  uint8_t action;             ///< Action to perform (Add, Remove, Remove All)
  uint8_t unique_id;          ///< Unique ID for the IE (must be < SLI_MAX_VENDOR_IE)
  uint16_t mgmt_frame_bitmap; ///< Bitmap indicating which management frames to include the IE in
  uint8_t reserved[4];        ///< Reserved for future use
  uint16_t ie_buffer_length;  ///< Length of the IE buffer (must be < SLI_MAX_VENDOR_IE_BUFFER_LENGTH)
  uint8_t ie_buffer[];        ///< Flexible array for raw IE buffer
} sli_wifi_manage_vendor_ie_packet_t;

typedef struct {
  uint8_t
    pll_mode; ///< PLL Mode. 0 - less than 120 Mhz NWP SoC clock; 1 - greater than 120 Mhz NWP SoC clock (Mode 1 is not currently supported for coex)
  uint8_t rf_type;          ///< RF Type.
  uint8_t wireless_mode;    ///< Wireless Mode.
  uint8_t enable_ppp;       ///< Enable PPP.
  uint8_t afe_type;         ///< AFE Type.
  uint32_t feature_enables; ///< Feature Enables.
} sli_wifi_feature_frame_request;

// WLAN Frame
typedef struct {
  uint8_t fc[2];                                   // Frame Control
  uint8_t duration[2];                             // Duration
  uint8_t da[SLI_WIFI_HARDWARE_ADDRESS_LENGTH];    // Destination Address
  uint8_t sa[SLI_WIFI_HARDWARE_ADDRESS_LENGTH];    // Source Address
  uint8_t bssid[SLI_WIFI_HARDWARE_ADDRESS_LENGTH]; // BSS Id
  uint8_t sc[2];                                   // Sequence Control Id
  uint8_t timestamp[8];                            // Time Stamp
  uint8_t bi[2];                                   // Beacon Interval
  uint8_t ci[2];                                   // Capability Information
  uint8_t tagged_info[];                           // Variable Information Elememt
} sli_wifi_data_frame_t;

// WLAN Information Element
typedef struct {
  uint8_t tag;         // Information Element Tag Id
  uint8_t data_length; // Information Element Data Length
  uint8_t data[];      // Information Element Data
} sli_wifi_data_tagged_info_t;

// Cipher suite
typedef struct {
  uint8_t cs_oui[3]; // Cipher Suite OUI
  uint8_t cs_type;   // Cipher Suite Type
} sli_wifi_cipher_suite_t;

// WLAN Robust Security Network Information Element
typedef struct {
  uint8_t version[2];          // RSN Version
  sli_wifi_cipher_suite_t gcs; // Group cipher suite
  uint8_t pcsc[2];             // Pairwise cipher suite count
  uint8_t pcsl[];              // Pairwise cipher suite list
} sli_wifi_rsn_element_t;

// WLAN Vendor Specific Information Element
typedef struct {
  uint8_t oui[3];              // Vendor OUI
  uint8_t vs_oui;              // Vendor specific OUI
  uint8_t type;                // WPA Information Element
  uint8_t wpa_version[2];      // WPA Version
  sli_wifi_cipher_suite_t mcs; // Multicast Cipher Suite
  uint8_t ucsc;                // Unicast Cipher Suite List Count
  uint8_t ucsl[];              // Unicast Cipher Suite List
} sli_wifi_vendor_specific_element_t;

/**
 * @struct sli_wifi_ip_address_info_t
 * @brief IP address information structure for passing IP addresses to firmware.
 * @details This structure is used to send the device's IP address information to firmware after obtaining an IP address.
 *          The firmware uses this information for BSS Max Idle Period keepalive functionality (Gratuitous ARP for IPv4, Neighbor Advertisement for IPv6).
 */
typedef struct {
  uint8_t flags;            // Bit flags: Bit 0 = IPv4 available, Bit 1 = IPv6 available
  uint8_t reserved[3];      // Reserved
  uint8_t ipv4_address[4];  // IPv4 address
  uint8_t ipv6_address[16]; // IPv6 address
} sli_wifi_ip_address_info_t;

/// Operation field for @ref SLI_WIFI_REQ_UPDATE_MC_ALLOWLIST (command @c 0x5C).
#define SLI_WIFI_MC_ALLOWLIST_OP_ADD        0
#define SLI_WIFI_MC_ALLOWLIST_OP_REMOVE     1
#define SLI_WIFI_MC_ALLOWLIST_OP_REMOVE_ALL 2

/**
 * @brief Host-to-NWP payload for L3 IPv4/IPv6 multicast address allowlist updates.
 * @details Used with @ref SLI_WIFI_REQ_UPDATE_MC_ALLOWLIST. This complements layer-2-oriented
 *          broadcast/multicast filtering configured by @ref SLI_WIFI_REQ_SET_BC_MC_FILTER_CONFIG
 *          (@ref sl_wifi_set_groupcast_filter_config): when multicast filtering is enabled,
 *          allowlisted group addresses are still passed toward the host stack per product policy.
 * @note ADD: set @a ip_type to @c SL_IPV4_VERSION (4) or @c SL_IPV6_VERSION (6); fill @c payload.ipv4 or @c payload.ipv6.
 * @note REMOVE: set @c payload.handle to the firmware handle (@c 0 .. @ref SLI_WIFI_MAX_MC_ALLOWLIST_IP_ADDRESSES - 1).
 * @note REMOVE_ALL: @a ip_type and @a payload are ignored by firmware.
 */
typedef struct {
  uint8_t
    operation; ///< @ref SLI_WIFI_MC_ALLOWLIST_OP_ADD, @ref SLI_WIFI_MC_ALLOWLIST_OP_REMOVE, or @ref SLI_WIFI_MC_ALLOWLIST_OP_REMOVE_ALL
  uint8_t ip_type;   ///< ADD: @c SL_IPV4_VERSION or @c SL_IPV6_VERSION; REMOVE / REMOVE_ALL: @c 0.
  uint16_t reserved; ///< Reserved bytes
  union {
    sl_ipv4_address_t ipv4; ///< ADD when @a ip_type is IPv4.
    sl_ipv6_address_t ipv6; ///< ADD when @a ip_type is IPv6.
    uint8_t handle;         ///< REMOVE: slot handle (@ref sl_ip_address_handle_t on-wire width).
  } payload;
} sli_wifi_mc_allowlist_update_req_t;

/**
 * @typedef sli_wifi_groupcast_filter_config_t
 * @brief Alias of @ref sl_wifi_groupcast_filter_config_t for NWP command @ref SLI_WIFI_REQ_SET_BC_MC_FILTER_CONFIG.
 * @details Configures station broadcast/multicast **filter enables** and **filter_mode** (default vs conservative).
 *          Beacon drop threshold for power save is **not** in this frame; use
 *          @ref SLI_WIFI_REQ_SET_BEACON_DROP_THRESHOLD / @ref sl_wifi_set_beacon_drop_threshold.
 *          Multicast **IP** allowlisting uses @ref sli_wifi_mc_allowlist_update_req_t, not this structure.
 */
typedef sl_wifi_groupcast_filter_config_t sli_wifi_groupcast_filter_config_t;

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

/**
 * @brief Variable-length configuration request (host to NWP).
 *
 * Wire layout: three LE @c uint32_t fields, then @a length bytes of opaque @a config_data (no host pointers).
 * Used by @ref SLI_WIFI_REQ_SET_BEACON_DROP_THRESHOLD for beacon drop threshold (scalar: @a length @c 0).
 * Other bitmap options may append a non-zero @a length tail in @a config_data per FW.
 *
 * @note For @a length @c 0, pass @c sizeof(sli_wifi_fw_config_req_t) to @ref sli_wifi_send_command.
 * @note For @a length @gt 0, allocate a contiguous buffer of @ref SLI_WIFI_FW_CONFIG_REQ_TOTAL_SIZE(length),
 *       set header fields, copy the tail into @a config_data, pass the total size to the command layer.
 */
typedef struct {
  uint32_t config_bitmap; ///< Bitmask of which configuration is applied.
  uint32_t value;         ///< Scalar argument when no tail is used; otherwise.
  uint32_t length;        ///< Size in bytes of @a config_data following this struct in the TX buffer.
  uint8_t config_data[];  ///< Opaque tail; C99 flexible array member, size @a length.
} sli_wifi_fw_config_req_t;

// -----------------------------------------------------------------------------
// Extended Wi-Fi statistics (internal; SLI_WIFI_REQ_EXT_STATS response layout)
// -----------------------------------------------------------------------------

/**
 * @brief NWP broadcast/multicast filter statistics (wireless stack path). Internal use.
 * @note Counters in this block are reset after each extended-statistics request.
 */
typedef struct {
  uint32_t bc_rx_count;   ///< Number of Broadcast frames received by NWP
  uint32_t bc_drop_count; ///< Number of Broadcast frames dropped in NWP
  uint32_t bc_pass_count; ///< Number of Broadcast frames accepted by NWP
  uint32_t mc_rx_count;   ///< Number of Multicast frames received by NWP
  uint32_t mc_drop_count; ///< Number of Multicast frames dropped by NWP
  uint32_t mc_pass_count; ///< Number of Multicast frames accepted by NWP
} sli_wifi_bc_mc_filter_stats_t;

/**
 * @brief PPE broadcast/multicast filter statistics on the DUT. Internal use.
 * @note Counters in this block are reset after each extended-statistics request.
 */
typedef struct {
  uint16_t bc_rx_count;   ///< Number of Broadcast frames received by PPE
  uint16_t bc_drop_count; ///< Number of Broadcast frames dropped by PPE
  uint16_t mc_rx_count;   ///< Number of Multicast frames received by PPE
  uint16_t mc_drop_count; ///< Number of Multicast frames dropped by PPE
  uint16_t reserved[4];   ///< Reserved
} sli_wifi_ppe_filter_stats_t;

/**
 * @brief Extended Wi-Fi statistics v2: NWP and PPE B/M filter counters only. Internal use.
 * @note Firmware resets these statistics after a read (successful statistics request/response).
 * @note For wire layout variants accepted by @ref sli_wifi_get_statistics_v2, see that API.
 */
typedef struct {
  sli_wifi_bc_mc_filter_stats_t nwp_filter_stats; ///< NWP B/M filter statistics
  sli_wifi_ppe_filter_stats_t ppe_filter_stats;   ///< PPE B/M filter statistics
} sli_wifi_statistics_v2_t;

#endif // SLI_WIFI_TYPES_H
