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
#include "sl_wifi.h"
#include "sl_wifi_types.h"
#include "sl_wifi_constants.h"
#include "sl_wifi_device.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_status.h"
#include "sl_si91x_types.h"
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_driver.h"
#include "sl_rsi_utility.h"
#include "sli_types.h"
#include "sli_wifi_types.h"
#include "sli_wifi.h"
#include "sli_wifi_utility.h"
#include "sli_buffer_manager.h"
#if defined(SLI_SI91X_SOCKETS)
#include "sl_si91x_socket_utility.h"
#endif
#include <stdint.h>
#include <string.h>
#include "sl_utility.h"
#include "sli_wifi_constants.h"
#include "sl_string.h"
#include "sl_constants.h"

#define SLI_ABSOLUTE_POWER_VALUE_TOGGLE 0x80
#define PASSIVE_SCAN_ENABLE             BIT(7)
#define LP_CHAIN_ENABLE                 BIT(6)
#define QUICK_SCAN_ENABLE               1
#define SCAN_RESULTS_TO_HOST            2
#if defined(SL_COMPONENT_CATALOG_PRESENT)
#include "sl_component_catalog.h"
#endif
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_net_si91x_integration_handler.h"
#else
// This macro defines a handler for cleaning up resources.
#define SLI_NETWORK_CLEANUP_HANDLER() \
  {                                   \
  }
#endif

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
#include "rsi_m4.h"
extern rsi_m4ta_desc_t crypto_desc[2];
#endif

#ifdef SLI_SI91X_MCU_INTERFACE
#include "rsi_wisemcu_hardware_setup.h"
#endif

#ifdef SL_CATALOG_LOG_COMPONENT_PRESENT
#include "sl_log_helper.h"
extern uint32_t sl_si91x_log_host_timesync_address;
#if defined(SLI_SI91X_MCU_INTERFACE)
/** Core ID for sl_log_sync_timestamp after timestamp memory is configured (captive / NWP core). */
#define SL_SI91X_WIFI_LOG_INIT_TIMESYNC_CORE_ID ((uint8_t)1u)
/** Context pointer for sl_log_sync_timestamp; none used at Wi-Fi init. */
#define SL_SI91X_WIFI_LOG_INIT_TIMESYNC_CONTEXT_PTR NULL
#endif
#endif

extern sl_wifi_advanced_scan_configuration_t advanced_scan_configuration;
extern bool interface_is_up[SL_WIFI_MAX_INTERFACE_INDEX];

static sl_status_t sli_si91x_configure_scan_request(const sl_wifi_client_configuration_t *ap,
                                                    sli_wifi_request_scan_t *scan_request,
                                                    sl_wifi_interface_t interface)
{
  memset(scan_request, 0, sizeof(*scan_request));

  if (ap->ssid.length > 0) {
    memcpy(scan_request->ssid, ap->ssid.value, ap->ssid.length);
  } else {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if ((interface & SL_WIFI_CLIENT_2_4GHZ_INTERFACE) && (ap->channel_bitmap.channel_bitmap_2_4 > 0)) {
    memcpy(&scan_request->channel_bit_map_2_4,
           &ap->channel_bitmap.channel_bitmap_2_4,
           sizeof(scan_request->channel_bit_map_2_4));
  }

  if ((interface & SL_WIFI_CLIENT_5GHZ_INTERFACE) && (ap->channel_bitmap.channel_bitmap_5 > 0)) {
    memcpy(&scan_request->channel_bit_map_5,
           &ap->channel_bitmap.channel_bitmap_5,
           sizeof(scan_request->channel_bit_map_5));
  }

  sl_wifi_max_tx_power_t wifi_max_tx_power = sli_get_max_tx_power();
  // Within the 1-byte scan_feature_bitmap variable, last 5 bits(bit 3 through bit 7) are allocated for
  // encoding the transmit power during scan procedure.
  scan_request->scan_feature_bitmap = (uint8_t)(wifi_max_tx_power.scan_tx_power << 3);

  return SL_STATUS_OK;
}

static sl_status_t sli_si91x_get_configured_join_request(sl_wifi_interface_t module_interface,
                                                         const void *configuration,
                                                         sli_wifi_join_request_t *join_request)
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(configuration);
  SL_WIFI_ARGS_CHECK_NULL_POINTER(join_request);
  sl_status_t status = SL_STATUS_OK;

  if (module_interface & SL_WIFI_CLIENT_INTERFACE) {
    status = sli_wifi_get_join_configuration(SL_WIFI_CLIENT_INTERFACE, &(join_request->join_feature_bitmap));
    VERIFY_STATUS_AND_RETURN(status);

    const sl_wifi_client_configuration_t *client_configuration = (const sl_wifi_client_configuration_t *)configuration;

    sl_wifi_listen_interval_v2_t listen_interval;
    sli_wifi_get_listen_interval_v2(module_interface, &listen_interval);
    sl_wifi_rate_t saved_data_rate;
    status = sli_wifi_get_saved_rate(&saved_data_rate);
    VERIFY_STATUS_AND_RETURN(status);
    join_request->data_rate = (uint8_t)saved_data_rate;
    memcpy(join_request->ssid, client_configuration->ssid.value, client_configuration->ssid.length);

    join_request->ssid_len      = client_configuration->ssid.length;
    join_request->security_type = (uint8_t)client_configuration->security;

    sl_wifi_mfp_config_t mfp_config = sli_wifi_get_mfp_mode();
    status = sli_wifi_configure_mfp_mode(&mfp_config, join_request->security_type, &join_request->join_feature_bitmap);
    VERIFY_STATUS_AND_RETURN(status);

    sli_wifi_fill_join_request_security_using_encryption(client_configuration->encryption,
                                                         &(join_request->security_type));

    join_request->vap_id                     = SL_WIFI_CLIENT_VAP_ID;
    join_request->listen_interval            = listen_interval.listen_interval;
    join_request->listen_interval_multiplier = (uint8_t)listen_interval.listen_interval_multiplier;
    memcpy(join_request->join_bssid, client_configuration->bssid.octet, sizeof(join_request->join_bssid));
  } else if (module_interface & SL_WIFI_AP_INTERFACE) {
    status = sli_wifi_get_join_configuration(SL_WIFI_AP_INTERFACE, &(join_request->join_feature_bitmap));
    VERIFY_STATUS_AND_RETURN(status);

    const sl_wifi_ap_configuration_t *ap_configuration = (const sl_wifi_ap_configuration_t *)configuration;

    sl_wifi_rate_t saved_ap_data_rate;
    status = sli_wifi_get_saved_rate(&saved_ap_data_rate);
    VERIFY_STATUS_AND_RETURN(status);
    join_request->data_rate = (uint8_t)saved_ap_data_rate;
    memcpy(join_request->ssid, ap_configuration->ssid.value, ap_configuration->ssid.length);

    join_request->ssid_len      = ap_configuration->ssid.length;
    join_request->security_type = (uint8_t)ap_configuration->security;
    join_request->vap_id        = 0;

    sl_wifi_mfp_config_t mfp_config = sli_wifi_get_mfp_mode();
    status = sli_wifi_configure_mfp_mode(&mfp_config, join_request->security_type, &join_request->join_feature_bitmap);
    VERIFY_STATUS_AND_RETURN(status);

    if (sli_wifi_get_opermode() == SL_WIFI_CONCURRENT_MODE) {
      join_request->vap_id = SL_WIFI_AP_VAP_ID;
    }
  } else {
    return SL_STATUS_FAIL;
  }

  sl_wifi_max_tx_power_t wifi_max_tx_power = sli_get_max_tx_power();

  /* Within the 1-byte 'power_level' variable, bit 0 and bit 1 are allocated for encoding power level thresholds(low, mid, high).
 * The Most Significant Bit serves as an indicator for toggling between absolute power value representation.
 * When the MSB is set, the 'power_level' variable encodes the absolute power value using bits 2 through 6. */
  join_request->power_level = (uint8_t)((wifi_max_tx_power.join_tx_power << 2) | SLI_ABSOLUTE_POWER_VALUE_TOGGLE);

  return SL_STATUS_OK;
}

static sl_status_t sli_si91x_handle_standard_scan(sl_wifi_interface_t interface,
                                                  const sl_wifi_ssid_t *optional_ssid,
                                                  const sl_wifi_scan_configuration_t *configuration)
{
  sli_wifi_request_scan_t scan_request = { 0 };

  if (optional_ssid != NULL) {
    memcpy(scan_request.ssid, optional_ssid->value, optional_ssid->length);
  }

  sl_status_t status = sli_wifi_configure_scan_channel_bitmap(interface,
                                                              configuration,
                                                              scan_request.channel,
                                                              scan_request.channel_bit_map_2_4,
                                                              scan_request.channel_bit_map_5);
  VERIFY_STATUS_AND_RETURN(status);

  if (configuration->type == SL_WIFI_SCAN_TYPE_PASSIVE) {
    scan_request.pscan_bitmap[3] |= PASSIVE_SCAN_ENABLE;
  }
  if (sli_wifi_get_active_application_profile() == SLI_WIFI_APPLICATION_PROFILE_MATTER_NEUTRAL_LESS_SWITCH) {
    scan_request.pscan_bitmap[3] |= LP_CHAIN_ENABLE;
  } else if (configuration->lp_mode) {
    scan_request.pscan_bitmap[3] |= LP_CHAIN_ENABLE;
  }

  sl_wifi_max_tx_power_t wifi_max_tx_power = sli_get_max_tx_power();
  // Within the 1-byte scan_feature_bitmap variable, last 5 bits(bit 3 through bit 7) are allocated for
  // encoding the transmit power during scan procedure.
  scan_request.scan_feature_bitmap = (uint8_t)(wifi_max_tx_power.scan_tx_power << 3);

  if ((optional_ssid != NULL) && (scan_request.channel[0] != 0)) {
    scan_request.scan_feature_bitmap |= QUICK_SCAN_ENABLE;
  }
  if (advanced_scan_configuration.active_channel_time != SL_WIFI_DEFAULT_ACTIVE_CHANNEL_SCAN_TIME
      && advanced_scan_configuration.active_channel_time != 0) {
    status = sli_wifi_configure_timeout(interface,
                                        SL_WIFI_CHANNEL_ACTIVE_SCAN_TIMEOUT,
                                        advanced_scan_configuration.active_channel_time);
    VERIFY_STATUS_AND_RETURN(status);
  }

  if (SL_WIFI_SCAN_TYPE_EXTENDED == configuration->type) {
    scan_request.scan_feature_bitmap |= SCAN_RESULTS_TO_HOST;
  }

  sli_wifi_flush_scan_results_database();

  return sli_wifi_send_command(SLI_WIFI_REQ_SCAN,
                               SLI_WIFI_WLAN_CMD,
                               &scan_request,
                               sizeof(scan_request),
                               SLI_WIFI_RETURN_IMMEDIATELY,
                               NULL,
                               NULL);
}

sl_status_t sl_wifi_init(const sl_wifi_device_configuration_t *configuration,
                         const sl_wifi_device_context_t *device_context,
                         sl_wifi_event_handler_t event_handler)
{
  UNUSED_PARAMETER(device_context);
#ifdef SLI_SI91X_MCU_INTERFACE
#if defined(SLI_SI917)
  sli_wifi_efuse_data_t efuse_data;
#endif
#endif
  sl_status_t status = SL_STATUS_OK;
  status             = sl_si91x_driver_init(configuration, event_handler);
#if defined(SL_CATALOG_LOG_COMPONENT_PRESENT)
  if (status == SL_STATUS_OK) {
#if defined(SLI_SI91X_MCU_INTERFACE)
    status = sl_si91x_configure_timestamp_memory_location(sizeof(uint32_t), &sl_si91x_log_host_timesync_address);
    if (status != SL_STATUS_OK) {
      SL_PRINT_STRING_ERROR("\r\nTimestamp Memory Location Configuration Failed with error: 0x%lX\r\n", status);
    }
    /* After shared timestamp memory is configured, synchronize host and captive-core clocks
     * for logging so M4 and NWP log timestamps are comparable. */
    sl_log_sync_timestamp(SL_SI91X_WIFI_LOG_INIT_TIMESYNC_CORE_ID, SL_SI91X_WIFI_LOG_INIT_TIMESYNC_CONTEXT_PTR);
#endif
    sl_log_level_t level        = sl_log_get_loglevel();
    sli_nwp_log_config_t config = { .log_config_level = (uint8_t)level };
    status                      = sli_nwp_log_configure(&config);
    if (status != SL_STATUS_IN_PROGRESS) {
      SL_PRINT_STRING_ERROR("\r\nNWP Log Configuration Failed with error: 0x%lX\r\n", status);
    }
    status = SL_STATUS_OK;
  }
#endif
#ifdef SL_SI91X_SIDE_BAND_CRYPTO
  if (status == SL_STATUS_OK) {
    uint32_t crypto_desc_ptr = (uint32_t)crypto_desc;
    uint32_t *desc_ptr       = &crypto_desc_ptr;
    status = sl_si91x_m4_ta_secure_handshake(SL_SI91X_ENABLE_SIDE_BAND, sizeof(uint32_t), (uint8_t *)desc_ptr, 0, NULL);
  }
#endif
#ifdef SLI_SI91X_MCU_INTERFACE
#if defined(SLI_SI917)
  if (status == SL_STATUS_OK) {
    /*Getting PTE CRC value to distinguish firmware 17 and 18 boards.*/
    sli_si91x_get_flash_efuse_data(&efuse_data, SL_SI91X_EFUSE_PTE_CRC);

    /*PTE FW version check.*/
    if (efuse_data.pte_crc == FIRMWARE_17_PTE_CRC_VALUE) {
      /* Enable Higher PWM RO Frequency Mode for PMU for FW17 boards*/
      RSI_IPMU_Set_Higher_Pwm_Ro_Frequency_Mode_to_PMU();
      /* Set the RETN_LDO voltage to 0.8V for FW17 boards*/
      RSI_IPMU_Retn_Voltage_To_Default();
    }
  }
#endif
#endif
  return status;
}

sl_status_t sl_wifi_set_antenna(sl_wifi_interface_t interface, sl_wifi_antenna_t antenna)
{
  return sli_wifi_set_antenna(interface, antenna);
}

sl_status_t sl_wifi_wait_for_scan_results(sl_wifi_scan_result_t **scan_results, uint32_t max_scan_result_count)
{
  return sli_wifi_wait_for_scan_results(scan_results, max_scan_result_count);
}

sl_status_t sl_wifi_start_scan(sl_wifi_interface_t interface,
                               const sl_wifi_ssid_t *optional_ssid,
                               const sl_wifi_scan_configuration_t *configuration)
{
  if (configuration == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* Advanced/background scan uses the shared path; standard scan uses Sutlej dBm scan encoding. */
  if (configuration->type == SL_WIFI_SCAN_TYPE_ADV_SCAN) {
    return sli_wifi_start_scan(interface, optional_ssid, configuration);
  }

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  return sli_si91x_handle_standard_scan(interface, optional_ssid, configuration);
}

sl_status_t sl_wifi_get_stored_scan_results(sl_wifi_interface_t interface,
                                            sl_wifi_extended_scan_result_parameters_t *extended_scan_parameters)
{
  return sli_wifi_get_stored_scan_results(interface, extended_scan_parameters);
}

sl_status_t sl_wifi_connect(sl_wifi_interface_t interface,
                            const sl_wifi_client_configuration_t *ap,
                            uint32_t timeout_ms)
{
  sl_status_t status;
  sli_wifi_request_scan_t scan_request;
  sli_wifi_request_eap_config_t eap_req;
  sli_wifi_join_request_t join_request;
  sl_wifi_buffer_t *buffer              = NULL;
  const sl_wifi_system_packet_t *packet = NULL;

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (interface & SL_WIFI_AP_INTERFACE) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  SL_WIFI_ARGS_CHECK_NULL_POINTER(ap);

  status = sli_si91x_configure_scan_request(ap, &scan_request, interface);
  VERIFY_STATUS_AND_RETURN(status);

  if (advanced_scan_configuration.active_channel_time != SL_WIFI_DEFAULT_ACTIVE_CHANNEL_SCAN_TIME) {
    status = sli_wifi_configure_timeout(interface,
                                        SL_WIFI_CHANNEL_ACTIVE_SCAN_TIMEOUT,
                                        advanced_scan_configuration.active_channel_time);
    VERIFY_STATUS_AND_RETURN(status);
  }

  status = sli_wifi_send_command(SLI_WIFI_REQ_SCAN,
                                 SLI_WIFI_WLAN_CMD,
                                 &scan_request,
                                 sizeof(scan_request),
                                 SLI_WIFI_WAIT_FOR(60000),
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_handle_client_security(ap, &eap_req);
  VERIFY_STATUS_AND_RETURN(status);

  memset(&join_request, 0, sizeof(join_request));

  status = sli_si91x_get_configured_join_request(SL_WIFI_CLIENT_INTERFACE, ap, &join_request);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_wifi_send_command(SLI_WIFI_REQ_JOIN,
                                 SLI_WIFI_WLAN_CMD,
                                 &join_request,
                                 sizeof(join_request),
                                 timeout_ms ? SLI_WIFI_WAIT_FOR_RESPONSE(timeout_ms) : SLI_WIFI_RETURN_IMMEDIATELY,
                                 NULL,
                                 (void **)&buffer);
  if (timeout_ms != 0 && status != SL_STATUS_OK) {
    if (buffer != NULL) {
      sli_buffer_manager_free_buffer(buffer);
    }
    sl_status_t temp_status = sli_wifi_send_command(SLI_WIFI_REQ_INIT,
                                                    SLI_WIFI_WLAN_CMD,
                                                    NULL,
                                                    0,
                                                    SLI_WIFI_WAIT_FOR_COMMAND_SUCCESS,
                                                    NULL,
                                                    NULL);
    VERIFY_STATUS_AND_RETURN(temp_status);
  }

  VERIFY_STATUS_AND_RETURN(status);

  if (buffer != NULL) {
    packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
    if (packet == NULL || packet->data[0] != 'C') {
      sli_buffer_manager_free_buffer(buffer);
      return SL_STATUS_NOT_AVAILABLE;
    }
    sli_buffer_manager_free_buffer(buffer);
  }
  return SL_STATUS_OK;
}

sl_status_t sl_wifi_set_advanced_client_configuration(sl_wifi_interface_t interface,
                                                      const sl_wifi_advanced_client_configuration_t *configuration)
{
  return sli_wifi_set_advanced_client_configuration(interface, configuration);
}

sl_status_t sl_wifi_get_signal_strength(sl_wifi_interface_t interface, int32_t *rssi)
{
  return sli_wifi_get_signal_strength(interface, rssi);
}

sl_status_t sl_wifi_get_sta_tsf(sl_wifi_interface_t interface, sl_wifi_tsf64_t *tsf)
{
  return sli_wifi_get_sta_tsf(interface, tsf);
}

sl_status_t sl_wifi_set_mac_address(sl_wifi_interface_t interface, const sl_mac_address_t *mac_address)
{
  return sli_wifi_set_mac_address(interface, mac_address);
}

sl_status_t sl_wifi_get_mac_address(sl_wifi_interface_t interface, sl_mac_address_t *mac)
{
  return sli_wifi_get_mac_address(interface, mac);
}

sl_status_t sl_wifi_set_channel(sl_wifi_interface_t interface, sl_wifi_channel_t channel)
{
  return sli_wifi_set_channel(interface, channel);
}

sl_status_t sl_wifi_config_pll_mode(sl_wifi_pll_mode_t pll_mode)
{
  return sli_wifi_config_pll_mode(pll_mode);
}

sl_status_t sl_wifi_config_power_chain(sl_wifi_power_chain_t power_chain)
{
  return sli_wifi_config_power_chain(power_chain);
}

sl_status_t sl_wifi_get_channel(sl_wifi_interface_t interface, sl_wifi_channel_t *channel_info)
{
  sl_status_t status                  = SL_STATUS_FAIL;
  sl_wifi_buffer_t *buffer            = NULL;
  sli_wifi_request_commands_t command = 0;

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  if ((interface == SL_WIFI_CLIENT_2_4GHZ_INTERFACE) || (interface == SL_WIFI_CLIENT_5GHZ_INTERFACE)
      || (interface == SL_WIFI_CLIENT_DUAL_INTERFACE))
    command = SLI_WIFI_REQ_QUERY_NETWORK_PARAMS;
  else if ((interface == SL_WIFI_AP_2_4GHZ_INTERFACE) || (interface == SL_WIFI_AP_5GHZ_INTERFACE)
           || (interface == SL_WIFI_AP_DUAL_INTERFACE))
    command = SLI_WIFI_REQ_QUERY_GO_PARAMS;

  status = sli_wifi_send_command(command,
                                 SLI_WIFI_WLAN_CMD,
                                 NULL,
                                 0,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SL_SI91X_GET_CHANNEL_TIMEOUT),
                                 NULL,
                                 (void **)&buffer);
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);
  sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  switch (interface) {
    case SL_WIFI_CLIENT_2_4GHZ_INTERFACE: {
      channel_info->channel = ((sli_si91x_network_params_response_t *)packet->data)->channel_number;
      channel_info->band    = SL_WIFI_BAND_2_4GHZ;
      break;
    }
    case SL_WIFI_CLIENT_5GHZ_INTERFACE: {
      channel_info->channel = ((sli_si91x_network_params_response_t *)packet->data)->channel_number;
      channel_info->band    = SL_WIFI_BAND_5GHZ;
      break;
    }
    case SL_WIFI_CLIENT_DUAL_INTERFACE: {
      channel_info->channel = ((sli_si91x_network_params_response_t *)packet->data)->channel_number;
      channel_info->band    = SL_WIFI_BAND_DUAL;
      break;
    }
    case SL_WIFI_AP_2_4GHZ_INTERFACE: {
      memcpy(&channel_info->channel, ((sli_wifi_client_info_response *)packet->data)->channel_number, 2);
      channel_info->band = SL_WIFI_BAND_2_4GHZ;
      break;
    }
    case SL_WIFI_AP_5GHZ_INTERFACE: {
      memcpy(&channel_info->channel, ((sli_wifi_client_info_response *)packet->data)->channel_number, 2);
      channel_info->band = SL_WIFI_BAND_5GHZ;
      break;
    }
    case SL_WIFI_AP_DUAL_INTERFACE: {
      memcpy(&channel_info->channel, ((sli_wifi_client_info_response *)packet->data)->channel_number, 2);
      channel_info->band = SL_WIFI_BAND_DUAL;
      break;
    }
    default:
      break;
  }

  sli_buffer_manager_free_buffer(buffer);
  return status;
}

/*
 * This API doesn't have any affect if it is called after connect/start ap.
 */
sl_status_t sl_wifi_set_max_tx_power(sl_wifi_interface_t interface, sl_wifi_max_tx_power_t max_tx_power)
{
  return sli_wifi_set_max_tx_power(interface, max_tx_power);
}

sl_status_t sl_wifi_get_max_tx_power(sl_wifi_interface_t interface, sl_wifi_max_tx_power_t *max_tx_power)
{
  return sli_wifi_get_max_tx_power(interface, max_tx_power);
}

sl_status_t sl_wifi_set_rts_threshold(sl_wifi_interface_t interface, uint16_t rts_threshold)
{
  return sli_wifi_set_rts_threshold(interface, rts_threshold);
}

sl_status_t sl_wifi_get_rts_threshold(sl_wifi_interface_t interface, uint16_t *rts_threshold)
{
  return sli_wifi_get_rts_threshold(interface, rts_threshold);
}

sl_status_t sl_wifi_set_mfp(sl_wifi_interface_t interface, const sl_wifi_mfp_mode_t config)
{
  return sli_wifi_set_mfp(interface, config);
}

sl_status_t sl_wifi_get_mfp(sl_wifi_interface_t interface, sl_wifi_mfp_mode_t *config)
{
  return sli_wifi_get_mfp(interface, config);
}

sl_status_t sl_wifi_start_ap(sl_wifi_interface_t interface, const sl_wifi_ap_configuration_t *configuration)
{
  sl_status_t status                           = SL_STATUS_OK;
  sl_wifi_buffer_t *rx_buffer                  = NULL;
  const sl_wifi_system_packet_t *join_response = NULL;
  sli_wifi_ap_config_request request           = { 0 };
  sli_wifi_join_request_t join_request         = { 0 };

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (configuration == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  if ((configuration->client_idle_timeout) > UINT8_MAX) {
    return SL_STATUS_INVALID_PARAMETER;
  }

#if defined(SLI_SI917)
  if (!(interface & SL_WIFI_2_4GHZ_INTERFACE)) {
    return SL_STATUS_INVALID_PARAMETER;
  }
#endif

  memcpy(request.ssid, configuration->ssid.value, configuration->ssid.length);

  status = sli_handle_ap_security(configuration, &request);
  VERIFY_STATUS_AND_RETURN(status);

  request.channel         = configuration->channel.channel;
  request.beacon_interval = configuration->beacon_interval;
  request.dtim_period     = configuration->dtim_beacon_count;
  request.max_sta_support = configuration->maximum_clients;
  if (configuration->keepalive_type) {
    request.options             = (configuration->keepalive_type & SLI_WIFI_AP_OPT_KEEPALIVE_TYPE_MASK);
    request.ap_keepalive_period = (uint8_t)configuration->client_idle_timeout;
  }
  if (configuration->beacon_stop) {
    // if beacon_stop is set, use 2nd bit to indicate that Beacon Stop is enabled.
    request.options |= SLI_WIFI_AP_OPT_BEACON_STOP;
  }

  // options in struct sl_wifi_ap_configuration_t is used only for HIDDEN SSID right now, so check only bit 1.
  if (configuration->options & SL_WIFI_HIDDEN_SSID) {
    // if HIDDEN_SSID is set, use 3rd bit of options var in sli_wifi_ap_config_request to indicate that dynamic configuration of Hidden SSID is enabled.
    request.options |= SLI_WIFI_AP_OPT_DYNAMIC_HIDDEN_SSID_CONF;
  }

  status = sli_wifi_send_command(SLI_WIFI_REQ_AP_CONFIGURATION,
                                 SLI_WIFI_WLAN_CMD,
                                 &request,
                                 sizeof(request),
                                 SLI_WIFI_WAIT_FOR(15000),
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);

  if (configuration->is_11n_enabled) {
    sli_wifi_request_ap_high_throughput_capability_t htcaps = { 0 };
    htcaps.mode_11n_enable                                  = true;
    htcaps.ht_caps_bitmap =
      (SL_WIFI_HT_CAPS_NUM_RX_STBC | SL_WIFI_HT_CAPS_SHORT_GI_20MHZ | SL_WIFI_HT_CAPS_GREENFIELD_EN);
    status = sli_wifi_set_high_throughput_capability(SL_WIFI_AP_INTERFACE, htcaps);
    VERIFY_STATUS_AND_RETURN(status);
  }

  status = sli_si91x_get_configured_join_request(SL_WIFI_AP_INTERFACE, configuration, &join_request);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_wifi_send_command(SLI_WIFI_REQ_JOIN,
                                 SLI_WIFI_WLAN_CMD,
                                 &join_request,
                                 sizeof(join_request),
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_JOIN_WAIT_TIME),
                                 NULL,
                                 (void **)&rx_buffer);
  if ((status != SL_STATUS_OK) && (rx_buffer != NULL)) {
    sli_buffer_manager_free_buffer(rx_buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  join_response = (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data((void *)rx_buffer, 0, NULL);

  if (join_response->data[0] != 'G') {
    sli_buffer_manager_free_buffer(rx_buffer);
    return SL_STATUS_NOT_AVAILABLE;
  }

  sli_wifi_save_ap_configuration(configuration);
  if (interface == SL_WIFI_AP_DUAL_INTERFACE)
    interface_is_up[SL_WIFI_AP_DUAL_INTERFACE_INDEX] = true;
  else if (interface == SL_WIFI_AP_5GHZ_INTERFACE)
    interface_is_up[SL_WIFI_AP_5GHZ_INTERFACE_INDEX] = true;
  else if (interface == SL_WIFI_AP_2_4GHZ_INTERFACE)
    interface_is_up[SL_WIFI_AP_2_4GHZ_INTERFACE_INDEX] = true;

  sli_buffer_manager_free_buffer(rx_buffer);
  return SL_STATUS_OK;
}

sl_status_t sl_wifi_get_pairwise_master_key(sl_wifi_interface_t interface,
                                            const uint8_t type,
                                            const sl_wifi_ssid_t *ssid,
                                            const char *pre_shared_key,
                                            uint8_t *pairwise_master_key)
{
  return sli_wifi_get_pairwise_master_key(interface, type, ssid, pre_shared_key, pairwise_master_key);
}

sl_status_t sl_wifi_disconnect_ap_client(sl_wifi_interface_t interface,
                                         const sl_mac_address_t *mac,
                                         sl_wifi_deauth_reason_t reason)
{
  return sli_wifi_disconnect_ap_client(interface, mac, reason);
}

sl_status_t sl_wifi_get_ap_client_info(sl_wifi_interface_t interface, sl_wifi_client_info_response_t *client_info)
{
  return sli_wifi_get_ap_client_info(interface, client_info);
}

sl_status_t sl_wifi_get_firmware_version(sl_wifi_firmware_version_t *version)
{
  return sl_si91x_get_firmware_version((sl_si91x_firmware_version_t *)version);
}

sl_status_t sl_wifi_get_interface_info(sl_wifi_interface_t interface, sl_wifi_interface_info_t *info)
{
  sl_status_t status       = 0;
  sl_wifi_buffer_t *buffer = NULL;
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  SL_WIFI_ARGS_CHECK_NULL_POINTER(info);

  // Select command based on interface
  sli_wifi_request_commands_t command;
  if (interface & SL_WIFI_CLIENT_INTERFACE) {
    command = (sli_wifi_request_commands_t)SLI_WIFI_REQ_QUERY_NETWORK_PARAMS;
  } else if (interface & SL_WIFI_AP_INTERFACE) {
    command = (sli_wifi_request_commands_t)SLI_WIFI_REQ_QUERY_GO_PARAMS;
  } else {
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
  memset(info, 0, sizeof(sl_wifi_interface_info_t));

  if (packet->length > 0) {
    if (command == SLI_WIFI_REQ_QUERY_GO_PARAMS) {
      // AP mode
      sli_wifi_client_info_response *response = (sli_wifi_client_info_response *)packet->data;
      // wlan state: no of stations connected in AP mode
      memcpy(&info->wlan_state, (uint16_t *)&response->sta_count, sizeof(uint16_t));
      memcpy(&info->channel_number, (uint16_t *)&response->channel_number, sizeof(uint16_t));
      memcpy(info->ssid, response->ssid, MIN(sizeof(info->ssid), sizeof(response->ssid)));
      // PSK for AP mode, PMK for Client mode
      memcpy(info->psk_pmk, response->psk, 64);
    } else {
      // Station mode
      sli_si91x_network_params_response_t *response = (sli_si91x_network_params_response_t *)packet->data;
      memcpy(&info->wlan_state, (uint16_t *)&response->wlan_state, sizeof(uint8_t));
      memcpy((uint8_t *)&info->channel_number, &response->channel_number, sizeof(uint8_t));
      memcpy(info->ssid, response->ssid, MIN(sizeof(info->ssid), sizeof(response->ssid)));
      memcpy(&info->sec_type, &response->sec_type, sizeof(uint8_t));
      // PSK for AP mode, PMK for Client mode
      memcpy(info->psk_pmk, response->psk, 64);
      memcpy(info->bssid, response->bssid, 6);
      memcpy(&info->wireless_mode, &response->wireless_mode, sizeof(uint8_t));
    }
  }
  sli_buffer_manager_free_buffer(buffer);
  return status;
}

sl_status_t sl_wifi_get_wireless_info(sl_si91x_rsp_wireless_info_t *info)
{
  sl_status_t status       = 0;
  sl_wifi_buffer_t *buffer = NULL;

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  SL_WIFI_ARGS_CHECK_NULL_POINTER(info);

  if (sli_wifi_get_opermode() == SL_WIFI_ACCESS_POINT_MODE) {
    // Send cmd for wlan info in AP mode
    status = sli_wifi_send_command(SLI_WIFI_REQ_QUERY_GO_PARAMS,
                                   SLI_WIFI_WLAN_CMD,
                                   NULL,
                                   0,
                                   SLI_WIFI_WAIT_FOR_RESPONSE(1000),
                                   NULL,
                                   (void **)&buffer);
  } else if ((sli_wifi_get_opermode() == SL_WIFI_CLIENT_MODE)
             || (sli_wifi_get_opermode() == SL_WIFI_ENTERPRISE_CLIENT_MODE)) {
    //! Send cmd for wlan info in client mode
    status = sli_wifi_send_command(SLI_WIFI_REQ_QUERY_NETWORK_PARAMS,
                                   SLI_WIFI_WLAN_CMD,
                                   NULL,
                                   0,
                                   SLI_WIFI_WAIT_FOR_RESPONSE(1000),
                                   NULL,
                                   (void **)&buffer);
  } else {
    return SL_STATUS_NOT_SUPPORTED;
  }

  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);
  sl_wifi_system_packet_t *packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);

  memset(info, 0, sizeof(sl_si91x_rsp_wireless_info_t));

  //In AP mode, receives a buffer equivalent to sli_wifi_client_info_response.
  if (packet->length > 0 && sli_wifi_get_opermode() == SL_WIFI_ACCESS_POINT_MODE) {
    sli_wifi_client_info_response *response = (sli_wifi_client_info_response *)packet->data;
    // wlan state: no of stations connected in AP mode
    memcpy(&info->wlan_state, (uint16_t *)&response->sta_count, sizeof(uint16_t));
    memcpy(&info->channel_number, (uint16_t *)&response->channel_number, sizeof(uint16_t));
    memcpy(info->ssid, response->ssid, MIN(sizeof(info->ssid), sizeof(response->ssid)));
    memcpy(info->mac_address, response->mac_address, 6);
    // PSK for AP mode, PMK for Client mode
    memcpy(info->psk_pmk, response->psk, 64);
    memcpy(info->ipv4_address, response->ipv4_address, 4);
    memcpy(info->ipv6_address, response->ipv6_address, 16);
  }
  //In Client mode, receives a buffer equivalent to sli_si91x_network_params_response_t.
  else if (packet->length > 0
           && ((sli_wifi_get_opermode() == SL_WIFI_CLIENT_MODE)
               || (sli_wifi_get_opermode() == SL_WIFI_ENTERPRISE_CLIENT_MODE))) {
    sli_si91x_network_params_response_t *response = (sli_si91x_network_params_response_t *)packet->data;
    memcpy(&info->wlan_state, (uint16_t *)&response->wlan_state, sizeof(uint8_t));
    memcpy((uint8_t *)&info->channel_number, &response->channel_number, sizeof(uint8_t));
    memcpy(info->ssid, response->ssid, MIN(sizeof(info->ssid), sizeof(response->ssid)));
    memcpy(info->mac_address, response->mac_address, 6);
    memcpy(&info->sec_type, &response->sec_type, sizeof(uint8_t));
    // PSK for AP mode, PMK for Client mode
    memcpy(info->psk_pmk, response->psk, 64);
    memcpy(info->ipv4_address, response->ipv4_address, 4);
    memcpy(info->ipv6_address, response->ipv6_address, 16);
    memcpy(info->bssid, response->bssid, 6);
    memcpy(&info->wireless_mode, &response->wireless_mode, sizeof(uint8_t));
  }

  sli_buffer_manager_free_buffer(buffer);
  return status;
}

sl_status_t sl_wifi_get_firmware_size(const void *buffer, uint32_t *fw_image_size)
{
  return sl_si91x_get_firmware_size(buffer, fw_image_size);
}

sl_status_t sl_wifi_disconnect(sl_wifi_interface_t interface)
{
  return sli_wifi_disconnect(interface);
}

sl_status_t sl_wifi_stop_ap(sl_wifi_interface_t interface)
{
  return sli_wifi_stop_ap(interface);
}

sl_status_t sl_wifi_get_statistics(sl_wifi_interface_t interface, sl_wifi_statistics_t *statistics)
{
  return sli_wifi_get_statistics(interface, statistics);
}

sl_status_t sl_wifi_get_operational_statistics(sl_wifi_interface_t interface,
                                               sl_wifi_operational_statistics_t *operational_statistics)
{
  return sli_wifi_get_operational_statistics(interface, operational_statistics);
}

sl_status_t sl_wifi_transmit_test_start(sl_wifi_interface_t interface,
                                        const sl_wifi_transmitter_test_info_t *test_tx_info)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  return sli_wifi_transmit_test_start(interface, test_tx_info);
}

sl_status_t sl_wifi_transmit_test_start_11bgn(const sl_wifi_transmitter_test_base_info_t *test_base_info,
                                              const sl_wifi_11bgn_per_params_t *per_params,
                                              const uint8_t *payload,
                                              uint16_t payload_length)
{
  if (test_base_info == NULL || per_params == NULL) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_start_11bgn: null arg");
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (!sl_si91x_is_device_initialized()) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_start_11bgn: not initialized");
    return SL_STATUS_NOT_INITIALIZED;
  }
  /* Si91x legacy PER path does not transport a host-provided payload; reject rather than silently drop it. */
  if (payload != NULL && payload_length > 0) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_start_11bgn: payload not supported on Si91x");
    return SL_STATUS_NOT_SUPPORTED;
  }
  switch (test_base_info->wifi_protocol) {
    case SL_WIFI_RATE_PROTOCOL_B_ONLY:
    case SL_WIFI_RATE_PROTOCOL_G_ONLY:
    case SL_WIFI_RATE_PROTOCOL_N_ONLY:
      break;
    default:
      SL_DEBUG_LOG_V2(ERROR,
                      "sl_wifi_transmit_test_start_11bgn: bad protocol %u",
                      (unsigned)test_base_info->wifi_protocol);
      return SL_STATUS_INVALID_PARAMETER;
  }

  sl_wifi_transmitter_test_info_t tx_test_info;
  sli_wifi_transmitter_test_info_from_base_and_per(test_base_info, per_params, &tx_test_info);

  sl_status_t status = sli_wifi_transmit_test_start(SL_WIFI_CLIENT_INTERFACE, &tx_test_info);
  SL_DEBUG_LOG_V2(DEBUG, "sl_wifi_transmit_test_start_11bgn: status=0x%lx", (unsigned long)status);
  return status;
}

sl_status_t sl_wifi_transmit_test_start_11ax(const sl_wifi_transmitter_test_base_info_t *test_base_info,
                                             const sl_wifi_11ax_per_params_t *per_params,
                                             const uint8_t *payload,
                                             uint16_t payload_length)
{
  if (test_base_info == NULL || per_params == NULL) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_start_11ax: null arg");
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (!sl_si91x_is_device_initialized()) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_start_11ax: not initialized");
    return SL_STATUS_NOT_INITIALIZED;
  }
  if (test_base_info->wifi_protocol != SL_WIFI_RATE_PROTOCOL_AX_ONLY) {
    SL_DEBUG_LOG_V2(ERROR,
                    "sl_wifi_transmit_test_start_11ax: bad protocol %u",
                    (unsigned)test_base_info->wifi_protocol);
    return SL_STATUS_INVALID_PARAMETER;
  }
  /* Si91x legacy PER path does not transport a host-provided payload; reject rather than silently drop it. */
  if (payload != NULL && payload_length > 0) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_start_11ax: payload not supported on Si91x");
    return SL_STATUS_NOT_SUPPORTED;
  }

  sl_wifi_transmitter_test_info_t tx_test_info;
  sli_wifi_transmitter_test_info_from_base_and_per(test_base_info, per_params, &tx_test_info);

  sl_status_t status = sli_wifi_transmit_test_start(SL_WIFI_CLIENT_INTERFACE, &tx_test_info);
  SL_DEBUG_LOG_V2(DEBUG, "sl_wifi_transmit_test_start_11ax: status=0x%lx", (unsigned long)status);
  return status;
}

sl_status_t sl_wifi_transmit_test_stop(sl_wifi_interface_t interface)
{
  UNUSED_PARAMETER(interface);
  if (!sl_si91x_is_device_initialized()) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_stop: not initialized");
    return SL_STATUS_NOT_INITIALIZED;
  }
  sl_status_t status = sli_wifi_transmit_test_stop();
  SL_DEBUG_LOG_V2(DEBUG, "sl_wifi_transmit_test_stop: status=0x%lx", (unsigned long)status);
  return status;
}

sl_status_t sl_wifi_transmit_test_stop_v2(void)
{
  if (!sl_si91x_is_device_initialized()) {
    SL_DEBUG_LOG_V2(ERROR, "sl_wifi_transmit_test_stop_v2: not initialized");
    return SL_STATUS_NOT_INITIALIZED;
  }
  sl_status_t status = sli_wifi_transmit_test_stop();
  SL_DEBUG_LOG_V2(DEBUG, "sl_wifi_transmit_test_stop_v2: status=0x%lx", (unsigned long)status);
  return status;
}

sl_status_t sl_wifi_frequency_offset(sl_wifi_interface_t interface, const sl_wifi_freq_offset_t *frequency_calibration)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  UNUSED_PARAMETER(interface);
  return sli_wifi_frequency_offset(interface, frequency_calibration);
}

sl_status_t sl_wifi_dpd_calibration(sl_wifi_interface_t interface, const sl_wifi_dpd_calib_data_t *dpd_calib_data)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  return sli_wifi_dpd_calibration(interface, dpd_calib_data);
}

sl_status_t sl_wifi_start_statistic_report(sl_wifi_interface_t interface, sl_wifi_channel_t channel)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  return sli_wifi_start_statistic_report(interface, channel);
}

sl_status_t sl_wifi_stop_statistic_report(sl_wifi_interface_t interface)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  return sli_wifi_stop_statistic_report(interface);
}

sl_status_t sl_wifi_set_performance_profile(const sl_wifi_performance_profile_t *profile)
{
  return sli_wifi_set_performance_profile(profile);
}

sl_status_t sl_wifi_get_performance_profile(sl_wifi_performance_profile_t *profile)
{
  return sli_wifi_get_performance_profile(profile);
}

sl_status_t sl_wifi_get_performance_profile_v2(sl_wifi_performance_profile_v2_t *profile)
{
  return sli_wifi_get_performance_profile_v2(profile);
}

sl_wifi_interface_t sl_wifi_get_default_interface(void)
{
  return sli_wifi_get_default_interface();
}

void sl_wifi_set_default_interface(sl_wifi_interface_t interface)
{
  sli_wifi_set_default_interface(interface);
}

sl_status_t sl_wifi_deinit(void)
{
  sl_status_t status = SL_STATUS_OK;
  status             = sl_si91x_driver_deinit();
  if (status != SL_STATUS_OK) {
    return status;
  }
  sli_wifi_deinit();
  return status;
}

// 5GHz interface is currently not supported for Si91x
bool sl_wifi_is_interface_up(sl_wifi_interface_t interface)
{
  return sli_wifi_is_interface_up(interface);
}

sl_status_t sl_wifi_set_certificate_with_index(uint8_t certificate_type,
                                               uint8_t certificate_index,
                                               const uint8_t *buffer,
                                               uint32_t certificate_length)
{
  return sl_si91x_wifi_set_certificate_index(certificate_type, certificate_index, buffer, certificate_length);
}

sl_status_t sl_wifi_set_certificate(uint8_t certificate_type, const uint8_t *buffer, uint32_t certificate_length)
{
  return sl_si91x_wifi_set_certificate_index(certificate_type, 0, buffer, certificate_length);
}

sl_status_t sl_wifi_set_transmit_rate(sl_wifi_interface_t interface,
                                      sl_wifi_rate_protocol_t rate_protocol,
                                      sl_wifi_rate_t mask)
{
  return sli_wifi_set_transmit_rate(interface, rate_protocol, mask);
}

sl_status_t sl_wifi_get_transmit_rate(sl_wifi_interface_t interface,
                                      sl_wifi_rate_protocol_t *rate_protocol,
                                      sl_wifi_rate_t *mask)
{
  return sli_wifi_get_transmit_rate(interface, rate_protocol, mask);
}

sl_status_t sl_wifi_get_ap_client_count(sl_wifi_interface_t interface, uint32_t *client_list_count)
{
  return sli_wifi_get_ap_client_count(interface, client_list_count);
}

sl_status_t sl_wifi_get_ap_client_list(sl_wifi_interface_t interface,
                                       uint16_t client_list_count,
                                       sl_mac_address_t *client_list)
{
  return sli_wifi_get_ap_client_list(interface, client_list_count, client_list);
}
sl_status_t sl_wifi_generate_wps_pin(sl_wifi_wps_pin_t *wps_pin)
{
  return sli_wifi_generate_wps_pin(wps_pin);
}

sl_status_t sl_wifi_start_wps(sl_wifi_interface_t interface,
                              sl_wifi_wps_mode_t mode,
                              const sl_wifi_wps_pin_t *optional_wps_pin)
{
  UNUSED_PARAMETER(optional_wps_pin);

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (mode != SL_WIFI_WPS_PUSH_BUTTON_MODE || (interface & SL_WIFI_AP_INTERFACE) == 0) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  sl_status_t status;
  sli_wifi_join_request_t wps_button_press_request = { 0 };
  sl_wifi_ap_configuration_t ap_configuration      = { 0 };

  sli_wifi_get_saved_ap_configuration(&ap_configuration);
  status = sli_si91x_get_configured_join_request(SL_WIFI_AP_INTERFACE, &ap_configuration, &wps_button_press_request);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_wifi_send_command(SLI_WIFI_REQ_JOIN,
                                 SLI_WIFI_WLAN_CMD,
                                 &wps_button_press_request,
                                 sizeof(wps_button_press_request),
                                 SLI_WIFI_RSP_JOIN_WAIT_TIME,
                                 NULL,
                                 NULL);
  VERIFY_STATUS_AND_RETURN(status);
  return status;
}

sl_status_t sl_wifi_start_wps_v2(sl_wifi_interface_t interface,
                                 sl_wifi_wps_config_t config,
                                 sl_wifi_wps_response_t *response)
{
  return sli_wifi_start_wps_v2(interface, config, response);
}

sl_status_t sl_wifi_wps_get_remaining_credentials(sl_wifi_interface_t interface,
                                                  sl_wifi_wps_response_t *credentials,
                                                  uint8_t credential_count)
{
  SL_VERIFY_POINTER_OR_RETURN(credentials, SL_STATUS_INVALID_PARAMETER);

  if ((credential_count == 0U) || (credential_count > (SLI_WIFI_MAX_WPS_CREDENTIALS - 1U))) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if ((interface & SL_WIFI_CLIENT_INTERFACE) == 0) {
    return SL_STATUS_NOT_SUPPORTED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  sl_wifi_buffer_t *buffer              = NULL;
  const sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status;
  const size_t payload_size = (size_t)credential_count * sizeof(sl_wifi_wps_response_t);

  status = sli_wifi_send_command(SLI_WIFI_REQ_WPS_EXTENDED_CREDENTIALS,
                                 SLI_WIFI_WLAN_CMD,
                                 NULL,
                                 0,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_WPS_EXTENDED_CREDENTIALS_WAIT_TIME),
                                 NULL,
                                 (void **)&buffer);
  if ((status != SL_STATUS_OK) && (buffer != NULL)) {
    sli_buffer_manager_free_buffer(buffer);
  }
  VERIFY_STATUS_AND_RETURN(status);

  packet = (const sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data((void *)buffer, 0, NULL);
  if ((packet == NULL) || (packet->length < payload_size)) {
    sli_buffer_manager_free_buffer(buffer);
    return SL_STATUS_FAIL;
  }

  memcpy(credentials, packet->data, payload_size);
  sli_buffer_manager_free_buffer(buffer);
  return SL_STATUS_OK;
}

sl_status_t sl_wifi_set_roam_configuration(sl_wifi_interface_t interface,
                                           const sl_wifi_roam_configuration_t *roam_configuration)
{
  return sli_wifi_set_roam_configuration(interface, roam_configuration);
}

sl_status_t sl_wifi_set_advanced_scan_configuration(const sl_wifi_advanced_scan_configuration_t *configuration)
{
  return sli_wifi_set_advanced_scan_configuration(configuration);
}

sl_status_t sl_wifi_get_advanced_scan_configuration(sl_wifi_advanced_scan_configuration_t *configuration)
{
  return sli_wifi_get_advanced_scan_configuration(configuration);
}

sl_status_t sl_wifi_stop_scan(sl_wifi_interface_t interface)
{
  return sli_wifi_stop_scan(interface);
}

sl_status_t sl_wifi_set_ap_configuration(sl_wifi_interface_t interface, const sl_wifi_ap_configuration_t *configuration)
{
  UNUSED_PARAMETER(interface);
  UNUSED_PARAMETER(configuration);
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  //Firmware unable to configure the ap configuration of a running AP interface
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t sl_wifi_get_ap_configuration(sl_wifi_interface_t interface, sl_wifi_ap_configuration_t *configuration)
{
  return sli_wifi_get_ap_configuration(interface, configuration);
}

sl_status_t sl_wifi_reconfigure_ap(sl_wifi_interface_t interface, sl_wifi_ap_reconfiguration_t config)
{
  return sli_wifi_reconfigure_ap(interface, config);
}

sl_status_t sl_wifi_test_client_configuration(sl_wifi_interface_t interface,
                                              const sl_wifi_client_configuration_t *ap,
                                              uint32_t timeout_ms)
{
  return sli_wifi_test_client_configuration(interface, ap, timeout_ms);
}

sl_status_t sl_wifi_send_raw_data_frame(sl_wifi_interface_t interface, const void *data, uint16_t data_length)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  SL_VERIFY_POINTER_OR_RETURN(data, SL_STATUS_NULL_POINTER);

  if (data_length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  return sl_si91x_driver_raw_send_command(SLI_SEND_RAW_DATA, data, data_length, SLI_SEND_RAW_DATA_RESPONSE_WAIT_TIME);
}

sl_status_t sl_wifi_enable_target_wake_time(const sl_wifi_twt_request_t *twt_req)
{
  return sli_wifi_enable_target_wake_time(twt_req);
}

sl_status_t sl_wifi_target_wake_time_auto_selection_v2(const sl_wifi_twt_selection_v2_t *config)
{
  SL_VERIFY_POINTER_OR_RETURN(config, SL_STATUS_NULL_POINTER);
  sli_wifi_twt_selection_t twt_req = {
    .twt_enable                            = config->twt_enable,
    .average_tx_throughput                 = config->average_tx_throughput,
    .tx_latency                            = config->tx_latency,
    .rx_latency                            = config->rx_latency,
    .device_average_throughput             = SLI_TWT_INTERNAL_DEVICE_AVERAGE_THROUGHPUT,
    .estimated_extra_wake_duration_percent = SLI_TWT_INTERNAL_EXTRA_WAKE_DURATION_PERCENT,
    .twt_tolerable_deviation               = SLI_TWT_INTERNAL_TOLERABLE_DEVIATION,
    .default_wake_interval_ms              = SLI_TWT_INTERNAL_DEFAULT_WAKE_INTERVAL_MS,
    .default_minimum_wake_duration_ms      = SLI_TWT_INTERNAL_DEFAULT_WAKE_DURATION_MS,
    .beacon_wake_up_count_after_sp         = SLI_TWT_INTERNAL_BEACON_WAKE_UP_COUNT_AFTER_SP,
  };
  return sli_wifi_target_wake_time_auto_selection(&twt_req);
}

sl_status_t sl_wifi_target_wake_time_auto_selection(sl_wifi_twt_selection_t *twt_auto_request)
{
  SL_VERIFY_POINTER_OR_RETURN(twt_auto_request, SL_STATUS_NULL_POINTER);
  /* Use only the four customer params; ignore the six internal fields. Delegate to v2 API which
   * applies SDK defaults for internal fields. Prevents issues from incorrect customer config. */
  sl_wifi_twt_selection_v2_t twt_auto_request_v2 = {
    .twt_enable            = twt_auto_request->twt_enable,
    .average_tx_throughput = twt_auto_request->average_tx_throughput,
    .tx_latency            = twt_auto_request->tx_latency,
    .rx_latency            = twt_auto_request->rx_latency,
  };
  return sl_wifi_target_wake_time_auto_selection_v2(&twt_auto_request_v2);
}

sl_status_t sl_wifi_disable_target_wake_time(const sl_wifi_twt_request_t *twt_req)
{
  return sli_wifi_disable_target_wake_time(twt_req);
}

sl_status_t sl_wifi_reschedule_twt(uint8_t flow_id,
                                   sl_wifi_reschedule_twt_action_t twt_action,
                                   uint64_t suspend_duration)
{
  return sli_wifi_reschedule_twt(flow_id, twt_action, suspend_duration);
}

sl_status_t sl_wifi_filter_broadcast(uint16_t beacon_drop_threshold,
                                     uint8_t filter_bcast_in_tim,
                                     uint8_t filter_bcast_tim_till_next_cmd)
{
  return sli_wifi_filter_broadcast(beacon_drop_threshold, filter_bcast_in_tim, filter_bcast_tim_till_next_cmd);
}

sl_status_t sl_wifi_update_gain_table(uint8_t band, uint8_t bandwidth, const uint8_t *payload, uint16_t payload_length)
{
  return sli_wifi_update_gain_table(band, bandwidth, payload, payload_length);
}

sl_status_t sl_wifi_update_su_gain_table(uint8_t band,
                                         uint8_t bandwidth,
                                         const uint8_t *payload,
                                         uint16_t payload_length,
                                         uint8_t x_offset,
                                         uint8_t y_offset)
{
  return sli_wifi_update_su_gain_table(band, bandwidth, payload, payload_length, x_offset, y_offset);
}

sl_status_t sl_wifi_set_11ax_config(uint8_t guard_interval)
{
#if !(SLI_SI91X_CONFIG_WIFI6_PARAMS)
  UNUSED_PARAMETER(guard_interval);
  return SL_STATUS_NOT_SUPPORTED;
#else
  sl_wifi_11ax_config_params_t config_11ax_params = { 0 };
  config_11ax_params.gi_ltf                       = guard_interval;
  config_11ax_params.dcm_enable                   = SL_WIFI_DCM_ENABLE_DISABLED;
  config_11ax_params.beamformee_support           = SL_WIFI_BEAMFORMEE_SUPPORT_ENABLED;
  config_11ax_params.config_er_su                 = SL_WIFI_CONFIG_ER_SU_NO;
  return sli_wifi_set_11ax_config(&config_11ax_params);
#endif
}

sl_status_t sl_wifi_set_11ax_config_v2(const sl_wifi_11ax_config_params_t *config_11ax_params)
{
#if !(SLI_SI91X_CONFIG_WIFI6_PARAMS)
  UNUSED_PARAMETER(config_11ax_params);
  return SL_STATUS_NOT_SUPPORTED;
#else
  return sli_wifi_set_11ax_config(config_11ax_params);
#endif
}

sl_status_t sl_wifi_set_listen_interval(sl_wifi_interface_t interface, sl_wifi_listen_interval_t listen_interval)
{
  return sli_wifi_set_listen_interval(interface, listen_interval);
}

sl_status_t sl_wifi_set_listen_interval_v2(sl_wifi_interface_t interface, sl_wifi_listen_interval_v2_t listen_interval)
{
  return sli_wifi_set_listen_interval_v2(interface, listen_interval);
}

sl_status_t sl_wifi_get_listen_interval(sl_wifi_interface_t interface, sl_wifi_listen_interval_t *listen_interval)
{
  return sli_wifi_get_listen_interval(interface, listen_interval);
}

sl_status_t sl_wifi_get_listen_interval_v2(sl_wifi_interface_t interface, sl_wifi_listen_interval_v2_t *listen_interval)
{
  return sli_wifi_get_listen_interval_v2(interface, listen_interval);
}

sl_status_t sl_wifi_enable_monitor_mode(sl_wifi_interface_t interface)
{
  UNUSED_PARAMETER(interface);
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t sl_wifi_disable_monitor_mode(sl_wifi_interface_t interface)
{
  UNUSED_PARAMETER(interface);
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t sl_wifi_start_p2p_discovery(sl_wifi_interface_t interface,
                                        const sl_wifi_p2p_configuration_t *configuration,
                                        sl_wifi_credential_id_t credential_id)
{
  UNUSED_PARAMETER(interface);
  UNUSED_PARAMETER(configuration);
  UNUSED_PARAMETER(credential_id);
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t sl_wifi_p2p_connect(sl_wifi_interface_t interface, const sl_wifi_p2p_configuration_t *configuration)
{
  UNUSED_PARAMETER(interface);
  UNUSED_PARAMETER(configuration);
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t sl_wifi_transceiver_set_channel(sl_wifi_interface_t interface, sl_wifi_transceiver_set_channel_t channel)
{
  return sli_wifi_transceiver_set_channel(interface, channel);
}

sl_status_t sl_wifi_set_transceiver_parameters(sl_wifi_interface_t interface, sl_wifi_transceiver_parameters_t *params)
{
  return sli_wifi_set_transceiver_parameters(interface, params);
}

sl_status_t sl_wifi_transceiver_up(sl_wifi_interface_t interface, sl_wifi_transceiver_configuration_t *config)
{
  return sli_wifi_transceiver_up(interface, config);
}

static int32_t sli_validate_wifi_datarate(sl_wifi_data_rate_t data_rate)
{
  switch (data_rate) {
    case SL_WIFI_DATA_RATE_1:
    case SL_WIFI_DATA_RATE_2:
    case SL_WIFI_DATA_RATE_5_5:
    case SL_WIFI_DATA_RATE_11:
    case SL_WIFI_DATA_RATE_6:
    case SL_WIFI_DATA_RATE_9:
    case SL_WIFI_DATA_RATE_12:
    case SL_WIFI_DATA_RATE_18:
    case SL_WIFI_DATA_RATE_24:
    case SL_WIFI_DATA_RATE_36:
    case SL_WIFI_DATA_RATE_48:
    case SL_WIFI_DATA_RATE_54:
      return SL_STATUS_OK;
    default:
      return SL_STATUS_TRANSCEIVER_INVALID_DATA_RATE;
  }
}

sl_status_t sl_wifi_send_transceiver_data(sl_wifi_interface_t interface,
                                          sl_wifi_transceiver_tx_data_control_t *control,
                                          const uint8_t *payload,
                                          uint16_t payload_len)
{
  sl_status_t status = SL_STATUS_OK;

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  if ((!payload_len) || (payload_len > MAX_PAYLOAD_LEN)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  SL_VERIFY_POINTER_OR_RETURN(control, SL_STATUS_NULL_POINTER);
  SL_VERIFY_POINTER_OR_RETURN(payload, SL_STATUS_NULL_POINTER);

  if ((IS_FIXED_DATA_RATE(control->ctrl_flags)) && (sli_validate_wifi_datarate(control->rate))) {
    return SL_STATUS_TRANSCEIVER_INVALID_DATA_RATE;
  }

  status = sl_si91x_driver_send_transceiver_data(control, payload, payload_len, SLI_WIFI_WAIT_FOR(1000));

  return status;
}

sl_status_t sl_wifi_update_transceiver_peer_list(sl_wifi_interface_t interface, sl_wifi_transceiver_peer_update_t peer)
{
  return sli_wifi_update_transceiver_peer_list(interface, peer);
}

sl_status_t sl_wifi_set_transceiver_multicast_filter(sl_wifi_interface_t interface,
                                                     sl_wifi_transceiver_mcast_filter_t mcast)
{
  return sli_wifi_set_transceiver_multicast_filter(interface, mcast);
}

sl_status_t sl_wifi_flush_transceiver_data(sl_wifi_interface_t interface)
{
  return sli_wifi_flush_transceiver_data(interface);
}

sl_status_t sl_wifi_configure_multicast_filter(sl_wifi_multicast_filter_info_t *multicast_filter_info)
{
  return sli_wifi_configure_multicast_filter(multicast_filter_info);
}

sl_status_t sl_wifi_add_vendor_ie(sl_wifi_vendor_ie_t *vendor_ie, uint8_t *fw_unique_id)
{
  return sli_wifi_add_vendor_ie(vendor_ie, fw_unique_id);
}

sl_status_t sl_wifi_remove_vendor_ie(uint8_t unique_id)
{
  return sli_wifi_remove_vendor_ie(unique_id);
}

sl_status_t sl_wifi_remove_all_vendor_ie(void)
{
  return sli_wifi_remove_all_vendor_ie();
}

sl_status_t sl_wifi_set_join_configuration(sl_wifi_interface_t interface, uint8_t join_feature_bitmap)
{
  return sli_wifi_set_join_configuration(interface, join_feature_bitmap);
}

sl_status_t sl_wifi_get_join_configuration(sl_wifi_interface_t interface, uint8_t *join_feature_bitmap)
{
  return sli_wifi_get_join_configuration(interface, join_feature_bitmap);
}

sl_status_t sl_wifi_configure_timeout(sl_wifi_interface_t interface,
                                      sl_wifi_timeout_type_t timeout_type,
                                      uint16_t timeout_value)
{
  return sli_wifi_configure_timeout(interface, timeout_type, timeout_value);
}

sl_status_t sl_wifi_get_timeout(sl_wifi_interface_t interface,
                                sl_wifi_timeout_type_t timeout_type,
                                uint16_t *timeout_value)
{
  return sli_wifi_get_timeout(interface, timeout_type, timeout_value);
}

sl_status_t sl_wifi_set_groupcast_filter_config(const sl_wifi_groupcast_filter_config_t *config)
{
  return sli_wifi_set_groupcast_filter_config(config);
}

sl_status_t sl_wifi_allowlist_mcast_add_ip(const sl_ip_address_t *ip_address, sl_ip_address_handle_t *id)
{
  return sli_wifi_allowlist_mcast_add_ip(ip_address, id);
}

sl_status_t sl_wifi_allowlist_mcast_remove_ip(sl_ip_address_handle_t id)
{
  return sli_wifi_allowlist_mcast_remove_ip(id);
}

sl_status_t sl_wifi_allowlist_mcast_remove_all(void)
{
  return sli_wifi_allowlist_mcast_remove_all();
}

sl_status_t sl_wifi_set_beacon_drop_threshold(sl_wifi_interface_t interface, uint16_t beacon_drop_threshold)
{
  return sli_wifi_set_beacon_drop_threshold(interface, beacon_drop_threshold);
}
