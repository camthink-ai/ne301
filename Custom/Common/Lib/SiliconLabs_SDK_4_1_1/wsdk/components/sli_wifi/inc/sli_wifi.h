/***************************************************************************/ /**
 * @file    sli_wifi.h
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
#ifndef SLI_WIFI_H
#define SLI_WIFI_H
#include "sl_status.h"
#include "sl_constants.h"
#include "sl_wifi_device.h"
#include "sl_wifi_constants.h"
#include "sl_wifi_types.h"
#include "sli_wifi_types.h"
#include "sli_wifi_constants.h"
#include "sli_wifi_power_profile.h"
#include "cmsis_os2.h"

#define ENABLE_MAC_INFO            BIT(0)
#define QOS_EN                     BIT(12)
#define BROADCAST_IND              BIT(9)
#define PER_CONT_MODE              1
#define FRAME_DESC_SZ              16
#define MIN_802_11_HDR_LEN         24
#define SLI_WIFI_BUFFER_BLOCK_SIZE 1640
#define SLI_SEND_MAC_FRAME         0x0
#define SLI_11AX_BE_RATE_MASK      0x18f
/// Nominal preamble length offset
#define RATE_OFFSET_NOMINAL_PE 5
/// Guard interval and LTF offset
#define RATE_OFFSET_GI_LTF 11
/// DCM offset
#define RATE_OFFSET_DCM 13
/// Coding type offset
#define RATE_OFFSET_CODING_TYPE 9

/** Internal TWT auto-selection defaults (SDK use only) */
#define SLI_TWT_INTERNAL_DEVICE_AVERAGE_THROUGHPUT     20000
#define SLI_TWT_INTERNAL_EXTRA_WAKE_DURATION_PERCENT   0
#define SLI_TWT_INTERNAL_TOLERABLE_DEVIATION           10
#define SLI_TWT_INTERNAL_DEFAULT_WAKE_INTERVAL_MS      1024
#define SLI_TWT_INTERNAL_DEFAULT_WAKE_DURATION_MS      8
#define SLI_TWT_INTERNAL_BEACON_WAKE_UP_COUNT_AFTER_SP 2

/**
 * @struct sli_wifi_twt_selection_t
 * @brief TWT auto-selection request structure.
 */
typedef struct {
  uint8_t twt_enable;
  uint16_t average_tx_throughput;
  uint32_t tx_latency;
  uint32_t rx_latency;
  uint16_t device_average_throughput;
  uint8_t estimated_extra_wake_duration_percent;
  uint8_t twt_tolerable_deviation;
  uint32_t default_wake_interval_ms;
  uint32_t default_minimum_wake_duration_ms;
  uint8_t beacon_wake_up_count_after_sp;
} sli_wifi_twt_selection_t;

/**
 * @enum sli_wifi_rail_cmd_subtype_t
 * @brief Enumeration of Wi-Fi RAIL command subtypes.
 *
 * This enumeration defines the various subtypes of Wi-Fi RAIL commands used for specific operations.
 *
 * @details
 * Each subtype corresponds to a specific Wi-Fi operation or request, such as measuring noise density, 
 * starting or stopping ADC captures, configuring DPD, or resetting PER statistics.
 *
 * @var SLI_WIFI_SUBTYPE_SET_TX_POWER_DBM
 *  Subtype for setting transmission power in dBm.
 * 
 * @var SLI_WIFI_SUBTYPE_TRANSMIT_CW
 *   Subtype for starting Continuous Wave (CW) transmission.
 *
 * @var SLI_WIFI_SUBTYPE_RX_STOP
 *   Subtype for stopping RX (Receive) operations.
 * 
 * @var SLI_WIFI_SUBTYPE_CONFIG_XO_CTUNE
 *   Subtype for configuring XO (Crystal Oscillator) CTUNE (Capacitor Tuning).  
 * 
 * @var SLI_WIFI_SUBTYPE_GET_XO_CTUNE
 *   Subtype for getting CTUNE (Capacitor Tuning) values.
 *
 */
typedef enum {
  SLI_WIFI_SUBTYPE_SET_TX_POWER_DBM = 1,  ///< Set transmission power in dBm.
  SLI_WIFI_SUBTYPE_TRANSMIT_CW      = 4,  ///< Start Continuous Wave (CW) transmission.
  SLI_WIFI_SUBTYPE_RX_STOP          = 10, ///< Stop RX (Receive) operations.
  SLI_WIFI_SUBTYPE_CONFIG_XO_CTUNE  = 21, ///< Configure XO (Crystal Oscillator) CTUNE (Capacitor Tuning).
  SLI_WIFI_SUBTYPE_GET_XO_CTUNE     = 22, ///< Get CTUNE (Capacitor Tuning) values.
} sli_wifi_rail_cmd_subtype_t;

/**
 * @struct sli_wifi_frame_body_type_t
 * @brief Represents the frame body type for Wi-Fi operations.
 *
 * This structure is used to define the subtype of a Wi-Fi request and includes
 * a reserved field for future use.
 *
 * @var sli_wifi_frame_body_type_t::sub_type
 *   Subtype of the request. This field specifies the type of operation or request being made.
 *
 * @var sli_wifi_frame_body_type_t::reserved
 *   Reserved for future use. This field is intended for potential extensions or additional data.
 */
typedef struct {
  uint16_t sub_type; ///< Sub_type of the request.
  uint16_t reserved; ///< Reserved for future use.
} sli_wifi_frame_body_type_t;

typedef struct {
  sli_wifi_frame_body_type_t frame_body_type;
  uint32_t ctune_data;
} sli_wifi_request_configure_xo_ctune_t;
typedef struct {
  sli_wifi_frame_body_type_t frame_body_type;
  uint32_t ctune_data[2];
} sli_wifi_request_get_xo_ctune_t;

typedef struct {
  sli_wifi_frame_body_type_t frame_body_type;
} sli_wifi_request_stop_rx_t;

/**
 * @struct sli_wifi_request_cw_tone_config_t
 * @brief Structure representing the configuration for transmitting Continuous Wave (CW) tones in Wi-Fi.
 *
 * This structure is used to configure and control the transmission of CW tones, 
 * including enabling/disabling the transmission, and specifying the tone configuration.
 *
 * @details
 * - CW tones are primarily used for testing and calibration purposes in Wi-Fi systems.
 * - The structure allows enabling/disabling CW tone transmission and provides a configuration structure for tone settings.
 *
 * @var sli_wifi_request_cw_tone_config_t::frame_body_type
 *   Transmit CW tone request structure. Specifies the frame body type for the request.
 *
 * @var sli_wifi_request_cw_tone_config_t::enable
 *   Enable or disable CW tone transmission. Set to 1 to enable, 0 to disable.
 *
 * @var sli_wifi_request_cw_tone_config_t::cw_tone_config
 *   CW tone configuration structure. Contains detailed settings for tone frequencies, scaling, and modes.
 */
typedef struct {
  sli_wifi_frame_body_type_t frame_body_type; ///< Transmit CW tone request structure.
  uint8_t enable;                             ///< Enable or disable CW tone transmission. 1 to enable, 0 to disable.
  sl_wifi_cw_tone_config_t cw_tone_config;    ///< CW tone configuration structure.
} sli_wifi_request_cw_tone_config_t;

typedef struct {
  sli_wifi_frame_body_type_t frame_body_type;
  int16_t Txpower;
} sli_wifi_request_tx_power_t;

typedef struct {
  uint16_t frame_control; // Frame Control field
  uint16_t duration_id;   // Duration/ID field
  uint8_t addr1[6];       // Address 1 (Receiver Address - RA)
  uint8_t addr2[6];       // Address 2 (Transmitter Address - TA)
  uint8_t addr3[6];       // Address 3 (Destination Address - DA or Source Address - SA)
  uint16_t seq_ctrl;      // Sequence Control field
} sli_ieee80211_hdr_t;

sl_status_t sli_wifi_configure_timeout(sl_wifi_interface_t interface,
                                       sl_wifi_timeout_type_t timeout_type,
                                       uint16_t timeout_value);
sl_status_t sli_wifi_get_timeout(sl_wifi_interface_t interface,
                                 sl_wifi_timeout_type_t timeout_type,
                                 uint16_t *timeout_value);
sl_wifi_interface_t sli_wifi_get_default_interface(void);
sl_status_t sli_wifi_wps_connect(sli_wifi_wps_config_t wps_config, sl_wifi_wps_response_t *wps_response);
sl_status_t sli_wifi_connect(sl_wifi_interface_t interface,
                             const sl_wifi_client_configuration_t *ap,
                             uint32_t timeout_ms);
void sli_wifi_set_default_interface(sl_wifi_interface_t interface);
sl_status_t sli_wifi_set_antenna(sl_wifi_interface_t interface, sl_wifi_antenna_t antenna);
sl_status_t sli_wifi_wait_for_scan_results(sl_wifi_scan_result_t **scan_results, uint32_t max_scan_result_count);
sl_status_t sli_wifi_start_scan(sl_wifi_interface_t interface,
                                const sl_wifi_ssid_t *optional_ssid,
                                const sl_wifi_scan_configuration_t *configuration);
sl_status_t sli_wifi_get_stored_scan_results(sl_wifi_interface_t interface,
                                             sl_wifi_extended_scan_result_parameters_t *extended_scan_parameters);
sl_status_t sli_wifi_set_advanced_client_configuration(sl_wifi_interface_t interface,
                                                       const sl_wifi_advanced_client_configuration_t *configuration);
sl_status_t sli_wifi_get_signal_strength(sl_wifi_interface_t interface, int32_t *rssi);
sl_status_t sli_wifi_get_sta_tsf(sl_wifi_interface_t interface, sl_wifi_tsf64_t *tsf);
sl_status_t sli_wifi_set_mac_address(sl_wifi_interface_t interface, const sl_mac_address_t *mac_address);
sl_status_t sli_wifi_get_mac_address(sl_wifi_interface_t interface, sl_mac_address_t *mac);
sl_status_t sli_wifi_set_channel(sl_wifi_interface_t interface, sl_wifi_channel_t channel);
sl_status_t sli_wifi_set_max_tx_power(sl_wifi_interface_t interface, sl_wifi_max_tx_power_t max_tx_power);
sl_status_t sli_wifi_get_max_tx_power(sl_wifi_interface_t interface, sl_wifi_max_tx_power_t *max_tx_power);
sl_status_t sli_wifi_config_pll_mode(sl_wifi_pll_mode_t pll_mode);
sl_status_t sli_wifi_config_power_chain(sl_wifi_power_chain_t power_chain);
sl_status_t sli_wifi_start_ap(sl_wifi_interface_t interface, const sl_wifi_ap_configuration_t *configuration);
sl_status_t sli_wifi_get_pairwise_master_key(sl_wifi_interface_t interface,
                                             const uint8_t type,
                                             const sl_wifi_ssid_t *ssid,
                                             const char *pre_shared_key,
                                             uint8_t *pairwise_master_key);
sl_status_t sli_wifi_get_associated_client_list(const void *client_list_buffer,
                                                uint16_t buffer_length,
                                                uint32_t timeout);
sl_status_t sli_wifi_disconnect_ap_client(sl_wifi_interface_t interface,
                                          const sl_mac_address_t *mac,
                                          sl_wifi_deauth_reason_t reason);
sl_status_t sli_wifi_get_ap_client_info(sl_wifi_interface_t interface, sl_wifi_client_info_response_t *client_info);
sl_status_t sli_wifi_disconnect(sl_wifi_interface_t interface);
sl_status_t sli_wifi_stop_ap(sl_wifi_interface_t interface);
sl_status_t sli_wifi_get_statistics(sl_wifi_interface_t interface, sl_wifi_statistics_t *statistics);
sl_status_t sli_wifi_get_statistics_v2(sl_wifi_interface_t interface, sli_wifi_statistics_v2_t *statistics);
sl_status_t sli_wifi_get_operational_statistics(sl_wifi_interface_t interface,
                                                sl_wifi_operational_statistics_t *operational_statistics);
void sli_wifi_transmitter_test_info_from_base_and_per(const sl_wifi_transmitter_test_base_info_t *test_base_info,
                                                      const void *per_params,
                                                      sl_wifi_transmitter_test_info_t *tx_test_info);
uint16_t sli_wifi_get_encoded_rate(sl_wifi_rate_protocol_t protocol, sl_wifi_mcs_rate_t rate);
sl_status_t sli_wifi_transmit_test_start(sl_wifi_interface_t interface,
                                         const sl_wifi_transmitter_test_info_t *test_tx_info);
sl_status_t sli_wifi_transmit_test_start_v2(const sl_wifi_transmitter_test_base_info_t *test_base_info,
                                            const void *per_params);
sl_status_t sli_wifi_transmit_test_send_frames(const sl_wifi_transmitter_test_base_info_t *base_info,
                                               const void *per_params,
                                               const uint8_t *payload,
                                               uint16_t payload_length);

/**
 * @brief Chunk-builder callback used by @ref sli_wifi_transmit_test_send_payload.
 *
 * Each callback invocation receives a freshly allocated CE-data-pool packet with @c chunk_length bytes
 * already laid out (user payload or pseudo-random pattern). The callback is responsible for filling
 * the firmware descriptor (rate, BBP, queue ID, etc.) for its PHY and routing it via
 * @ref sli_wifi_send_data_packet. On any non-OK status the callback must release @c packet.
 */
typedef sl_status_t (*sli_wifi_transmit_test_chunk_builder_t)(const sl_wifi_transmitter_test_base_info_t *base_info,
                                                              const void *per_params,
                                                              sl_wifi_system_packet_t *packet,
                                                              uint16_t chunk_length);

/**
 * @brief Drive PER payload + pattern-fill chunk loop, dispatching each chunk via @a build_chunk.
 *
 * Allocates CE-data-pool packets, copies user payload (when supplied), then fills any remaining
 * bytes with a 16-bit @c 0xAA55 test pattern, and calls @a build_chunk per chunk. Owns the buffer
 * lifecycle on the per-chunk path; the callback owns release on its own failure path.
 *
 * Used by both the shared 11bgn/11ax send path and the SiWx3xx-port 11ac/11be send path.
 */
sl_status_t sli_wifi_transmit_test_send_payload(const sl_wifi_transmitter_test_base_info_t *base_info,
                                                const void *per_params,
                                                const uint8_t *payload,
                                                uint16_t payload_length,
                                                sli_wifi_transmit_test_chunk_builder_t build_chunk);
sl_status_t sli_wifi_transmit_test_stop(void);
sl_status_t sli_wifi_frequency_offset(sl_wifi_interface_t interface,
                                      const sl_wifi_freq_offset_t *frequency_calibration);
sl_status_t sli_wifi_dpd_calibration(sl_wifi_interface_t interface, const sl_wifi_dpd_calib_data_t *dpd_calib_data);
sl_status_t sli_wifi_start_statistic_report(sl_wifi_interface_t interface, sl_wifi_channel_t channel);
sl_status_t sli_wifi_stop_statistic_report(sl_wifi_interface_t interface);
void sli_wifi_deinit(void);
bool sli_wifi_is_interface_up(sl_wifi_interface_t interface);
sl_status_t sli_wifi_set_transmit_rate(sl_wifi_interface_t interface,
                                       sl_wifi_rate_protocol_t rate_protocol,
                                       sl_wifi_rate_t mask);
sl_status_t sli_wifi_get_transmit_rate(sl_wifi_interface_t interface,
                                       sl_wifi_rate_protocol_t *rate_protocol,
                                       sl_wifi_rate_t *mask);
sl_status_t sli_wifi_get_ap_client_count(sl_wifi_interface_t interface, uint32_t *client_list_count);
sl_status_t sli_wifi_get_ap_client_list(sl_wifi_interface_t interface,
                                        uint16_t client_list_count,
                                        sl_mac_address_t *client_list);
sl_status_t sli_wifi_generate_wps_pin(sl_wifi_wps_pin_t *wps_pin);
sl_status_t sli_wifi_start_wps(sl_wifi_interface_t interface,
                               sl_wifi_wps_mode_t mode,
                               const sl_wifi_wps_pin_t *optional_wps_pin);
sl_status_t sli_wifi_start_wps_v2(sl_wifi_interface_t interface,
                                  sl_wifi_wps_config_t config,
                                  sl_wifi_wps_response_t *response);
sl_status_t sli_wifi_set_roam_configuration(sl_wifi_interface_t interface,
                                            const sl_wifi_roam_configuration_t *roam_configuration);
sl_status_t sli_wifi_set_advanced_scan_configuration(const sl_wifi_advanced_scan_configuration_t *configuration);
sl_status_t sli_wifi_get_advanced_scan_configuration(sl_wifi_advanced_scan_configuration_t *configuration);
sl_status_t sli_wifi_stop_scan(sl_wifi_interface_t interface);
sl_status_t sli_wifi_get_ap_configuration(sl_wifi_interface_t interface, sl_wifi_ap_configuration_t *configuration);
sl_status_t sli_wifi_reconfigure_ap(sl_wifi_interface_t interface, sl_wifi_ap_reconfiguration_t config);
sl_status_t sli_wifi_test_client_configuration(sl_wifi_interface_t interface,
                                               const sl_wifi_client_configuration_t *ap,
                                               uint32_t timeout_ms);
sl_status_t sli_wifi_enable_target_wake_time(const sl_wifi_twt_request_t *twt_req);
sl_status_t sli_wifi_target_wake_time_auto_selection(sli_wifi_twt_selection_t *twt_auto_request);
sl_status_t sli_wifi_disable_target_wake_time(const sl_wifi_twt_request_t *twt_req);
sl_status_t sli_wifi_reschedule_twt(uint8_t flow_id,
                                    sl_wifi_reschedule_twt_action_t twt_action,
                                    uint64_t suspend_duration);
sl_status_t sli_wifi_filter_broadcast(uint16_t beacon_drop_threshold,
                                      uint8_t filter_bcast_in_tim,
                                      uint8_t filter_bcast_tim_till_next_cmd);
sl_status_t sli_wifi_update_gain_table(uint8_t band,
                                       uint8_t bandwidth,
                                       const uint8_t *payload,
                                       uint16_t payload_length);
sl_status_t sli_wifi_update_su_gain_table(uint8_t band,
                                          uint8_t bandwidth,
                                          const uint8_t *payload,
                                          uint16_t payload_length,
                                          uint8_t x_offset,
                                          uint8_t y_offset);
sl_status_t sli_wifi_set_11ax_config(const sl_wifi_11ax_config_params_t *config_11ax_params);
sl_status_t sli_wifi_transceiver_set_channel(sl_wifi_interface_t interface, sl_wifi_transceiver_set_channel_t channel);
sl_status_t sli_wifi_set_transceiver_parameters(sl_wifi_interface_t interface,
                                                sl_wifi_transceiver_parameters_t *params);
sl_status_t sli_wifi_transceiver_up(sl_wifi_interface_t interface, sl_wifi_transceiver_configuration_t *config);
sl_status_t sli_wifi_update_transceiver_peer_list(sl_wifi_interface_t interface,
                                                  sl_wifi_transceiver_peer_update_t peer);
sl_status_t sli_wifi_set_transceiver_multicast_filter(sl_wifi_interface_t interface,
                                                      sl_wifi_transceiver_mcast_filter_t mcast);
sl_status_t sli_wifi_flush_transceiver_data(sl_wifi_interface_t interface);
sl_status_t sli_wifi_configure_multicast_filter(sl_wifi_multicast_filter_info_t *multicast_filter_info);
sl_status_t sli_wifi_set_groupcast_filter_config(const sl_wifi_groupcast_filter_config_t *config);
sl_status_t sli_wifi_allowlist_mcast_add_ip(const sl_ip_address_t *ip_address, sl_ip_address_handle_t *id);
sl_status_t sli_wifi_allowlist_mcast_remove_ip(sl_ip_address_handle_t id);
sl_status_t sli_wifi_allowlist_mcast_remove_all(void);
sl_status_t sli_wifi_set_beacon_drop_threshold(sl_wifi_interface_t interface, uint16_t beacon_drop_threshold);
/* Function used to update the variable that stores the wifi rate */
sl_status_t sli_wifi_save_rate(sl_wifi_rate_t transfer_rate);
sl_status_t sli_wifi_get_configured_join_request(sl_wifi_interface_t module_interface,
                                                 const void *configuration,
                                                 sli_wifi_join_request_t *join_request);
sl_status_t sli_wifi_get_mfp(sl_wifi_interface_t interface, sl_wifi_mfp_mode_t *config);
sl_status_t sli_wifi_set_mfp(sl_wifi_interface_t interface, const sl_wifi_mfp_mode_t config);
sl_status_t sli_wifi_set_rts_threshold(sl_wifi_interface_t interface, uint16_t rts_threshold);
sl_status_t sli_wifi_get_rts_threshold(sl_wifi_interface_t interface, uint16_t *rts_threshold);
sl_status_t sli_wifi_transmit_cw_tone_start(sl_wifi_interface_t interface, sl_wifi_cw_tone_config_t cw_tone_config);
sl_status_t sli_wifi_transmit_cw_tone_stop(sl_wifi_interface_t interface);
sl_status_t sli_wifi_set_test_tx_power(int16_t txPower);
sl_status_t sli_wifi_stop_rx(sl_wifi_interface_t interface);
sl_status_t sli_wifi_config_xo_ctune(sl_wifi_interface_t interface,
                                     sl_wifi_response_get_ctune_data_t *xo_ctune,
                                     uint32_t ctune_data);
sl_status_t sli_wifi_read_ctune(sl_wifi_interface_t interface,
                                sl_wifi_response_get_ctune_data_t *get_xo_ctune,
                                const uint32_t *ctune_data);
sl_status_t sli_wifi_add_vendor_ie(sl_wifi_vendor_ie_t *vendor_ie, uint8_t *fw_unique_id);
sl_status_t sli_wifi_remove_vendor_ie(uint8_t unique_id);
sl_status_t sli_wifi_remove_all_vendor_ie(void);

sl_status_t sli_wifi_get_join_configuration(sl_wifi_interface_t interface, uint8_t *join_feature_bitmap);
sl_status_t sli_wifi_set_join_configuration(sl_wifi_interface_t interface, uint8_t join_feature_bitmap);
sl_status_t sli_wifi_set_device_region(sl_wifi_operation_mode_t operation_mode,
                                       sl_wifi_band_mode_t band,
                                       sl_wifi_region_code_t region_code);
void sli_wifi_prepare_mac_frame_header(const void *buf,
                                       const uint8_t *addr1,
                                       const uint8_t *addr2,
                                       const uint8_t *addr3);
sl_status_t sli_wifi_send_mac_data_frame(const sl_wifi_transmitter_test_info_t *per_params,
                                         sl_wifi_system_packet_t *packet,
                                         uint16_t chunk_length);
sl_status_t sli_wifi_send_mac_data_frame_11bgn(const sl_wifi_transmitter_test_base_info_t *tx_test_info,
                                               const sl_wifi_11bgn_per_params_t *per_params,
                                               sl_wifi_system_packet_t *packet,
                                               uint16_t chunk_length);
sl_status_t sli_wifi_send_mac_data_frame_11ax(const sl_wifi_transmitter_test_base_info_t *tx_test_info,
                                              const sl_wifi_11ax_per_params_t *per_params,
                                              sl_wifi_system_packet_t *packet,
                                              uint16_t chunk_length);
/**
 * @brief Route an already-formed Wi-Fi data packet to firmware.
 *
 * @param[in] data Pointer to packet bytes; must not be modified through this pointer by this API
 *            (matches @c sli_routing_utility_route_packet's @c const void * packet parameter).
 *
 * @note Buffer-ownership contract: callers retain ownership of @a data on all return paths.
 *       On the Si91x port, @c sli_routing_utility_route_packet does NOT free the packet on success,
 *       so any cleanup (including the PER MAC chunk-builders) must continue to release the buffer
 *       only on failure. Do NOT change this without auditing every caller in
 *       @ref sli_wifi_send_mac_data_frame_11bgn / @ref sli_wifi_send_mac_data_frame_11ax and the
 *       SiWx3xx 11ac/11be chunk-builders.
 */
sl_status_t sli_wifi_send_data_packet(const void *data, uint16_t length, const void *context);
/**
 * @brief Send IP address information to firmware.
 *
 * @details This API sends the device's IP address (IPv4 and/or IPv6) to the firmware
 *          after the device obtains an IP address. The firmware uses this for BSS Max
 *          Idle Period keepalive functionality (Gratuitous ARP for IPv4, Neighbor
 *          Advertisement for IPv6). This API is called automatically by the SDK when
 *          IP addresses are obtained in both hosted stack and offloaded stack scenarios.
 *
 * @param[in] interface
 *   Wi-Fi interface as identified by @ref sl_wifi_interface_t.
 *
 * @param[in] ip_address_info
 *   Pointer to IP address information structure. At least one address type (IPv4 or IPv6) must be available.
 *
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 */
sl_status_t sli_wifi_send_ip_address_info(sl_wifi_interface_t interface,
                                          const sli_wifi_ip_address_info_t *ip_address_info);
#endif
