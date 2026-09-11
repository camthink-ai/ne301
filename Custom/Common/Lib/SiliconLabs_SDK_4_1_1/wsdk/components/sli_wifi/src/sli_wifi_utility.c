/***************************************************************************/ /**
 * @file    sli_wifi_utility.c
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
#include "sli_wifi.h"
#include "sli_wifi_utility.h"
#include "sl_wifi_device.h"
#include "sli_wifi_constants.h"
#include "sli_wifi_types.h"
#include "sl_core.h"
#include "sl_constants.h"
#include "sl_wifi_credentials.h"
#include "assert.h"
#include <string.h>
#include "sl_cmsis_utility.h"
#include "sli_command_engine.h"
#include "sli_queue_manager.h"
#include "sli_wifi_device_core_utilities.h"

#define DEFAULT_BEACON_MISS_IGNORE_LIMIT 1

// 802.11 information element: Element ID (1 octet) + Length (1 octet) before optional body.
#define SLI_WIFI_IE_ELEMENT_ID_LENGTH_OCTETS (2u)
// Upper bound on IE iterations for one beacon/probe MPDU (minimum IE density + one slack step).
#define SLI_WIFI_BEACON_IE_GUARD_MAX_ITERATIONS(tagged_octets_) \
  ((uint32_t)(tagged_octets_) / SLI_WIFI_IE_ELEMENT_ID_LENGTH_OCTETS + 1u)

// IEEE 802.11 SSID element: 0-32 octets in the Information field (buffer SLI_WIFI_SSID_LEN includes NUL).
#define SLI_WIFI_SSID_IE_MAX_OCTETS (32u)

// RSN IE (802.11): version (2) + Group Cipher Suite (4) + Pairwise Cipher Suite Count (2) before pairwise list.
#define SLI_WIFI_RSN_IE_MIN_DATA_OCTETS           (10u)
#define SLI_WIFI_RSN_DATA_OCTETS_BEFORE_PCSC_LIST (8u)

// Little-endian 16-bit field size (e.g. AKM suite count in RSN / WPA IE).
#define SLI_WIFI_LE16_FIELD_OCTETS (2u)

// Defensive cap on AKM suites parsed from an IE (corrupt length fields).
#define SLI_WIFI_MAX_AKM_SUITES_PARSE (24u)

// IEEE 802.11 cipher suite selector type: 802.1X / EAPOL (RSN/WPA AKM suite type 1).
#define SLI_WIFI_CIPHER_SUITE_TYPE_IEEE_802_1X (1u)

// Microsoft WPA IE (vendor specific): fixed layout before pairwise cipher suite list (802.11 + WPA IE).
#define SLI_WIFI_WPA_VENDOR_IE_MIN_OCTETS              (12u)
#define SLI_WIFI_WPA_VENDOR_PAIRWISE_COUNT_OFFSET_LSB  (10u)
#define SLI_WIFI_WPA_VENDOR_PAIRWISE_SUITE_LIST_OFFSET (12u)
#define SLI_WIFI_WPA_VENDOR_OUI_TYPE0                  (0x00u)
#define SLI_WIFI_WPA_VENDOR_OUI_TYPE1                  (0x50u)
#define SLI_WIFI_WPA_VENDOR_OUI_TYPE2                  (0xF2u)
#define SLI_WIFI_WPA_VENDOR_TYPE_WPA                   (0x01u)

static uint32_t client_listen_interval            = 1000;
static uint32_t client_listen_interval_multiplier = 1;

#ifndef __ZEPHYR__
#include "sli_cmsis_os2_ext_task_register.h"
#include "cmsis_os2.h"
// For all the threads this is the index of the thread local array at which the firmware status will be stored.
sli_task_register_id_t sli_fw_status_storage_index = SLI_FW_STATUS_STORAGE_INVALID_INDEX;
#endif

// This value will be used in connect request/ AP configurations to set the TX power of the module
static sl_wifi_max_tx_power_t wifi_max_tx_power = {
  .scan_tx_power = 0x1f, //Default power value set to max value supported in dBm
  .join_tx_power = 0x1f, //Default power value set to max value supported in dBm
};

// This value will be used in set RTS threshold command to set the RTS threshold of the module
static sl_wifi_rts_threshold_t wifi_rts_threshold = {
  .rts_threshold = SLI_WIFI_RTS_THRESHOLD // Default RTS threshold value
};

// This value will be used in set MFP mode command to set the MFP mode of the module
static sl_wifi_mfp_config_t wifi_mfp_config = { .mfp_mode      = SL_WIFI_MFP_DISABLED,
                                                .is_configured = false,
                                                .reserved      = { 0 } };
// Global configuration storage with default values
static sli_wifi_feature_frame_config_t global_feature_config = {
  .pll_mode    = SL_WIFI_PLL_MODE_20MHZ, // Default: PLL mode for 20MHz operations
  .power_chain = SL_WIFI_HP_CHAIN,       // Default: High Power chain
};
extern bool device_initialized;
uint16_t initialized_opermode              = SLI_WIFI_INVALID_MODE;
static bool is_card_ready_required         = true;
static sl_wifi_rate_t saved_wifi_data_rate = SL_WIFI_AUTO_RATE;
static sl_wifi_ap_configuration_t ap_configuration;
static sli_scan_info_t *scan_info_database = NULL;
extern uint8_t firmware_queue_id[SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES];
extern uint8_t command_packet_type[SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES];

#ifndef __ZEPHYR__
sl_status_t sli_fw_status_storage_index_init(void)
{
  sl_status_t status = SL_STATUS_OK;
  // Declare a variable to store the current interrupt state
  CORE_DECLARE_IRQ_STATE;

  // Enter a critical section by disabling interrupts
  // This ensures that the following operations are executed atomically
  CORE_ENTER_CRITICAL();
#ifdef SL_CATALOG_KERNEL_PRESENT
  // Check if the code is running in a thread context & task register index is invalid
  if (osThreadGetId() != NULL && sli_fw_status_storage_index == SLI_FW_STATUS_STORAGE_INVALID_INDEX) {
    // Create a new task register id
    status = sli_osTaskRegisterNew(&sli_fw_status_storage_index);
    if (status != SL_STATUS_OK) {
      CORE_EXIT_CRITICAL();
      if (PRINT_ERROR_LOGS) {
        PRINT_STATUS(ERROR_TAG, status)
      }
      return status;
    }
    // Successfully initialized - status is already SL_STATUS_OK from sli_osTaskRegisterNew
  }
  // If already initialized or not in thread context, status remains SL_STATUS_FAIL (initialized above)
#endif
  CORE_EXIT_CRITICAL();
  return status;
}
#endif

void sli_wifi_set_opermode(sl_wifi_operation_mode_t mode)
{
  initialized_opermode = mode;
}

sl_wifi_operation_mode_t sli_wifi_get_opermode(void)
{
  return initialized_opermode;
}

sl_status_t sli_wifi_set_listen_interval(sl_wifi_interface_t interface, sl_wifi_listen_interval_t listen_interval)
{
  UNUSED_PARAMETER(interface);
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }
  client_listen_interval            = listen_interval.listen_interval;
  client_listen_interval_multiplier = DEFAULT_LISTEN_INTERVAL_MULTIPLIER;
  return SL_STATUS_OK;
}

sl_status_t sli_wifi_set_listen_interval_v2(sl_wifi_interface_t interface, sl_wifi_listen_interval_v2_t listen_interval)
{
  UNUSED_PARAMETER(interface);

  if (listen_interval.listen_interval_multiplier < DEFAULT_LISTEN_INTERVAL_MULTIPLIER) {
    SL_DEBUG_LOG_V2(WARN,
                    "\r\n listen_interval_multiplier minimum value should be 1, Updating to the minimum value.\r\n");
    listen_interval.listen_interval_multiplier = DEFAULT_LISTEN_INTERVAL_MULTIPLIER;
  }
  client_listen_interval            = listen_interval.listen_interval;
  client_listen_interval_multiplier = listen_interval.listen_interval_multiplier;

  return SL_STATUS_OK;
}
sl_status_t sli_wifi_get_listen_interval(sl_wifi_interface_t interface, sl_wifi_listen_interval_t *listen_interval)
{
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  SL_WIFI_ARGS_CHECK_NULL_POINTER(listen_interval);
  listen_interval->listen_interval = client_listen_interval;
  return SL_STATUS_OK;
}

sl_status_t sli_wifi_get_listen_interval_v2(sl_wifi_interface_t interface,
                                            sl_wifi_listen_interval_v2_t *listen_interval)
{
  if (!device_initialized) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (!sli_wifi_is_interface_up(interface)) {
    return SL_STATUS_WIFI_INTERFACE_NOT_UP;
  }

  SL_WIFI_ARGS_CHECK_NULL_POINTER(listen_interval);
  listen_interval->listen_interval            = client_listen_interval;
  listen_interval->listen_interval_multiplier = client_listen_interval_multiplier;
  return SL_STATUS_OK;
}

// Function to identify expected scan result based on filter
static bool sli_filter_scan_info(const sli_scan_info_t *scan_info,
                                 const sl_wifi_extended_scan_result_parameters_t *extended_scan_parameters)
{
  if (NULL == scan_info) {
    return false;
  }

  if ((NULL != extended_scan_parameters->channel_filter)
      && (*(extended_scan_parameters->channel_filter) != scan_info->channel)) {
    return false;
  }

  if ((NULL != extended_scan_parameters->security_mode_filter)
      && (*(extended_scan_parameters->security_mode_filter) != scan_info->security_mode)) {
    return false;
  }

  if ((NULL != extended_scan_parameters->rssi_filter)
      && (*(extended_scan_parameters->rssi_filter) <= scan_info->rssi)) {
    return false;
  }

  if ((NULL != extended_scan_parameters->network_type_filter)
      && (*(extended_scan_parameters->network_type_filter) != scan_info->network_type)) {
    return false;
  }

  return true;
}

// Function to get the total count of stored extended scan results (for callback data_length)
sl_status_t sli_wifi_get_stored_scan_result_count(sl_wifi_interface_t interface, uint16_t *scan_count)
{
  UNUSED_PARAMETER(interface);

  if (NULL == scan_count) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  *scan_count = 0;
  for (sli_scan_info_t *scan_info = scan_info_database; scan_info != NULL; scan_info = scan_info->next) {
    (*scan_count)++;
  }
  return SL_STATUS_OK;
}

// Function to get all or filtered scan results from scan result database
sl_status_t sli_wifi_get_stored_scan_results(sl_wifi_interface_t interface,
                                             sl_wifi_extended_scan_result_parameters_t *extended_scan_parameters) //Done
{
  UNUSED_PARAMETER(interface);
  if (NULL == extended_scan_parameters) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_wifi_extended_scan_result_t *scan_results = extended_scan_parameters->scan_results;
  uint16_t *result_count                       = extended_scan_parameters->result_count;
  uint16_t length                              = extended_scan_parameters->array_length;
  sli_scan_info_t *scan_info                   = scan_info_database;

  if ((NULL == scan_results) || (NULL == result_count) || (0 == length)) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  *result_count = 0;

  while ((0 != length) && (NULL != scan_info)) {
    if (true == sli_filter_scan_info(scan_info, extended_scan_parameters)) {
      scan_results[*result_count].rf_channel    = scan_info->channel;
      scan_results[*result_count].security_mode = scan_info->security_mode;
      scan_results[*result_count].rssi          = scan_info->rssi;
      scan_results[*result_count].network_type  = scan_info->network_type;
      scan_results[*result_count].seen_count    = scan_info->seen_count;
      memcpy(scan_results[*result_count].bssid, scan_info->bssid, SLI_WIFI_HARDWARE_ADDRESS_LENGTH);
      memcpy(scan_results[*result_count].ssid, scan_info->ssid, 34);
      (*result_count)++;
      length--;
    }
    scan_info = scan_info->next;
  }

  return SL_STATUS_OK;
}

// Function to Clean up all the scan results in scan result database
void sli_wifi_flush_scan_results_database(void)
{
  sli_scan_info_t *scan_info = scan_info_database;
  sli_scan_info_t *node      = NULL;

  while (NULL != scan_info) {
    node      = scan_info;
    scan_info = scan_info->next;
    SLI_FREE(node);
  }
  scan_info_database = NULL;

  return;
}

sl_wifi_max_tx_power_t sli_get_max_tx_power()
{
  return wifi_max_tx_power;
}

sl_wifi_rts_threshold_t sli_get_rts_threshold()
{
  return wifi_rts_threshold;
}

sl_wifi_mfp_config_t sli_wifi_get_mfp_mode()
{
  return wifi_mfp_config;
}

sli_wifi_feature_frame_config_t sli_wifi_get_feature_frame_config(void)
{
  return global_feature_config;
}

sl_status_t sli_wifi_get_saved_rate(sl_wifi_rate_t *transfer_rate)
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(transfer_rate);

  *transfer_rate = saved_wifi_data_rate;
  return SL_STATUS_OK;
}

sl_status_t sli_wifi_host_get_credentials(sl_wifi_credential_id_t id, uint8_t type, sl_wifi_credential_t *cred)
{
  uint32_t credential_length = sizeof(sl_wifi_credential_t) - offsetof(sl_wifi_credential_t, pmk);
  sl_status_t status         = sl_wifi_get_credential(id, &cred->type, &cred->pmk, &credential_length);
  VERIFY_STATUS_AND_RETURN(status);

  if (type == SL_WIFI_PSK_CREDENTIAL) {
    if ((cred->type == SL_WIFI_PSK_CREDENTIAL) || (cred->type == SL_WIFI_PMK_CREDENTIAL)) {
      return SL_STATUS_OK;
    } else {
      return SL_STATUS_FAIL;
    }
  } else if (type != cred->type) {
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

void sli_wifi_save_max_tx_power(uint8_t max_scan_tx_power, uint8_t max_join_tx_power)
{
  wifi_max_tx_power.scan_tx_power = max_scan_tx_power;
  wifi_max_tx_power.join_tx_power = max_join_tx_power;
}

void sli_wifi_save_rts_threshold(uint16_t rts_threshold)
{
  wifi_rts_threshold.rts_threshold = rts_threshold;
}

void sli_wifi_save_pll_mode(const sl_wifi_pll_mode_t pll_mode)
{
  global_feature_config.pll_mode = pll_mode;
}

void sli_wifi_save_power_chain(const sl_wifi_power_chain_t power_chain)
{
  global_feature_config.power_chain = power_chain;
}

sl_status_t sli_wifi_save_mfp_mode(const sl_wifi_mfp_config_t *config)
{
  if (config == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  wifi_mfp_config.mfp_mode      = config->mfp_mode;
  wifi_mfp_config.is_configured = config->is_configured;
  return SL_STATUS_OK;
}

sl_status_t sli_wifi_save_ap_configuration(const sl_wifi_ap_configuration_t *wifi_ap_configuration)
{
  // Check if the input pointer is valid
  SL_WIFI_ARGS_CHECK_NULL_POINTER(wifi_ap_configuration);
  ap_configuration = *wifi_ap_configuration;

  return SL_STATUS_OK;
}

void sli_wifi_reset_ap_configuration()
{
  // Initialize the ap_configuration structure with zeros
  ap_configuration = (sl_wifi_ap_configuration_t){ 0 };
}

sl_status_t sli_wifi_get_saved_ap_configuration(sl_wifi_ap_configuration_t *wifi_ap_configuration)
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(wifi_ap_configuration);
  *wifi_ap_configuration = ap_configuration;

  return SL_STATUS_OK;
}

sl_status_t sli_wifi_get_rate_protocol_and_data_rate(const uint8_t data_rate,
                                                     sl_wifi_rate_protocol_t *rate_protocol,
                                                     sl_wifi_rate_t *mask)
{
  // Map the provided data_rate to a rate_protocol and set the mask accordingly
  switch (data_rate) {
    case SL_WIFI_AUTO_RATE: {
      *rate_protocol = SL_WIFI_RATE_PROTOCOL_AUTO;
      break;
    }
    case SL_WIFI_RATE_11B_1:
    case SL_WIFI_RATE_11B_2:
    case SL_WIFI_RATE_11B_5_5:
    case SL_WIFI_RATE_11B_11: {
      *rate_protocol = SL_WIFI_RATE_PROTOCOL_B_ONLY;
      break;
    }
    case SL_WIFI_RATE_11G_6:
    case SL_WIFI_RATE_11G_9:
    case SL_WIFI_RATE_11G_12:
    case SL_WIFI_RATE_11G_18:
    case SL_WIFI_RATE_11G_24:
    case SL_WIFI_RATE_11G_36:
    case SL_WIFI_RATE_11G_48:
    case SL_WIFI_RATE_11G_54: {
      *rate_protocol = SL_WIFI_RATE_PROTOCOL_G_ONLY;
      break;
    }
    case SL_WIFI_RATE_11N_MCS0:
    case SL_WIFI_RATE_11N_MCS1:
    case SL_WIFI_RATE_11N_MCS2:
    case SL_WIFI_RATE_11N_MCS3:
    case SL_WIFI_RATE_11N_MCS4:
    case SL_WIFI_RATE_11N_MCS5:
    case SL_WIFI_RATE_11N_MCS6:
    case SL_WIFI_RATE_11N_MCS7: {
      *rate_protocol = SL_WIFI_RATE_PROTOCOL_N_ONLY;
      break;
    }
    case SL_WIFI_RATE_11AX_MCS0:
    case SL_WIFI_RATE_11AX_MCS1:
    case SL_WIFI_RATE_11AX_MCS2:
    case SL_WIFI_RATE_11AX_MCS3:
    case SL_WIFI_RATE_11AX_MCS4:
    case SL_WIFI_RATE_11AX_MCS5:
    case SL_WIFI_RATE_11AX_MCS6:
    case SL_WIFI_RATE_11AX_MCS7: {
      *rate_protocol = SL_WIFI_RATE_PROTOCOL_AX_ONLY;
      break;
    }
    default: {
      return SL_STATUS_INVALID_CONFIGURATION;
    }
  }

  *mask = data_rate;

  return SL_STATUS_OK;
}
sl_status_t sli_wifi_save_rate(sl_wifi_rate_t transfer_rate)
{
  // Check if the provided transfer rate is valid
  if (transfer_rate == SL_WIFI_RATE_INVALID) {
    return SL_STATUS_INVALID_CONFIGURATION;
  }

  saved_wifi_data_rate = transfer_rate;

  return SL_STATUS_OK;
}

void sli_wifi_reset_max_tx_power()
{
  wifi_max_tx_power.scan_tx_power = 0x1f;
  wifi_max_tx_power.join_tx_power = 0x1f;
}

void sli_wifi_reset_sl_wifi_rate()
{
  saved_wifi_data_rate = 0;
}

bool sli_wifi_get_card_ready_required()
{
  return is_card_ready_required;
}

void sli_wifi_set_card_ready_required(bool card_ready_required)
{
  is_card_ready_required = card_ready_required;
}

//In Access point mode NWP only supports No Encryption, TKIP and CCMP encryptions.
sl_status_t sli_wifi_get_nwp_encryption(sl_wifi_encryption_t encryption_mode, uint8_t *encryption_request)
{
  switch (encryption_mode) {
    case SL_WIFI_NO_ENCRYPTION:
      *encryption_request = SLI_WIFI_NO_ENCRYPTION;
      break;
    case SL_WIFI_TKIP_ENCRYPTION:
      *encryption_request = SLI_WIFI_TKIP_ENCRYPTION;
      break;
    case SL_WIFI_CCMP_ENCRYPTION:
    case SL_WIFI_DEFAULT_ENCRYPTION:
      *encryption_request = SLI_WIFI_CCMP_ENCRYPTION;
      break;
    default:
      return SL_STATUS_NOT_SUPPORTED;
  }

  return SL_STATUS_OK;
}

uint8_t sli_lmac_crc8_c(uint8_t crc8_din, uint8_t crc8_state, uint8_t end)
{
  uint8_t din[8];
  uint8_t state[8];
  uint8_t state_c[8];
  uint8_t crc8_out;

  din[0] = ((crc8_din & BIT(7)) >> 7);
  din[1] = ((crc8_din & BIT(6)) >> 6);
  din[2] = ((crc8_din & BIT(5)) >> 5);
  din[3] = ((crc8_din & BIT(4)) >> 4);
  din[4] = ((crc8_din & BIT(3)) >> 3);
  din[5] = ((crc8_din & BIT(2)) >> 2);
  din[6] = ((crc8_din & BIT(1)) >> 1);
  din[7] = ((crc8_din & BIT(0)) >> 0);

  state[0] = ((crc8_state & BIT(0)) >> 0);
  state[1] = ((crc8_state & BIT(1)) >> 1);
  state[2] = ((crc8_state & BIT(2)) >> 2);
  state[3] = ((crc8_state & BIT(3)) >> 3);
  state[4] = ((crc8_state & BIT(4)) >> 4);
  state[5] = ((crc8_state & BIT(5)) >> 5);
  state[6] = ((crc8_state & BIT(6)) >> 6);
  state[7] = ((crc8_state & BIT(7)) >> 7);

  state_c[7] = (state[7] ^ din[7]) ^ (state[6] ^ din[6]) ^ (state[5] ^ din[5]);

  state_c[6] = (state[6] ^ din[6]) ^ (state[5] ^ din[5]) ^ (state[4] ^ din[4]);

  state_c[5] = (state[5] ^ din[5]) ^ (state[4] ^ din[4]) ^ (state[3] ^ din[3]);

  state_c[4] = (state[4] ^ din[4]) ^ (state[3] ^ din[3]) ^ (state[2] ^ din[2]);

  state_c[3] = (state[1] ^ din[1]) ^ (state[2] ^ din[2]) ^ (state[3] ^ din[3]) ^ (state[7] ^ din[7]);

  state_c[2] = (state[0] ^ din[0]) ^ (state[1] ^ din[1]) ^ (state[2] ^ din[2]) ^ (state[6] ^ din[6]);

  state_c[1] = (state[0] ^ din[0]) ^ (state[1] ^ din[1]) ^ (state[6] ^ din[6]);

  state_c[0] = (state[0] ^ din[0]) ^ (state[7] ^ din[7]) ^ (state[6] ^ din[6]);
  if (!end) {
    crc8_out = (uint8_t)(((state_c[0] & BIT(0)) << 0) | ((state_c[1] & BIT(0)) << 1) | ((state_c[2] & BIT(0)) << 2)
                         | ((state_c[3] & BIT(0)) << 3) | ((state_c[4] & BIT(0)) << 4) | ((state_c[5] & BIT(0)) << 5)
                         | ((state_c[6] & BIT(0)) << 6) | ((state_c[7] & BIT(0)) << 7));
  } else {
    crc8_out = (uint8_t)(((state_c[7] & BIT(0)) << 0) | ((state_c[6] & BIT(0)) << 1) | ((state_c[5] & BIT(0)) << 2)
                         | ((state_c[4] & BIT(0)) << 3) | ((state_c[3] & BIT(0)) << 4) | ((state_c[2] & BIT(0)) << 5));

    crc8_out = ~crc8_out;
    crc8_out &= 0x3f;
  }
  return crc8_out;
}

uint8_t sli_multicast_mac_hash(const uint8_t *mac)
{
  uint8_t crc = 0xff;
  for (uint8_t i = 0; i < 6; i++) {
    crc = sli_lmac_crc8_c(mac[i], crc, ((i == 5) ? 1 : 0));
  }
  return crc;
}

/***************************************************************************/ /**
 * @brief
 *   Returns a pointer to the scan information database.
 * @pre Pre-conditions:
 *   The Wi-Fi scan operation should be completed before accessing the scan information database.
 * @return
 *   Pointer to the scan information database.
 *   The returned pointer can be used to retrieve or manipulate scan information as needed.
 ******************************************************************************/
sli_scan_info_t **sli_get_scan_info_database(void)
{
  return &scan_info_database;
}

static void sli_process_tag_info(const sli_wifi_data_tagged_info_t *info, sli_scan_info_t *scan_info);
static void sli_store_scan_info_element(const sli_scan_info_t *info);

// Function to Parse the Beacon and Probe response Frames
// Extended scan reports WPA/WPA2 mixed APs correctly via wpa_vendor_ie_seen and both IE handlers
void sli_handle_wifi_beacon(sl_wifi_system_packet_t *packet)
{
  uint8_t subtype                   = 0;
  sli_wifi_data_frame_t *wifi_frame = (sli_wifi_data_frame_t *)packet->data;
  sli_scan_info_t scan_info         = { 0 };
  uint16_t ies_length               = 0;

  scan_info.rssi    = (~packet->desc[10]);
  scan_info.channel = packet->desc[11];

  // Check for ESS bit and TBSS status bit in capability info
  // 1 in ESS bit indicates that the transmitter is an AP
  if (1 == (wifi_frame->ci[0] & 0x03)) {
    scan_info.network_type = 1;
  } else {
    scan_info.network_type = 0;
  }

  if (wifi_frame->ci[0] & 0x08) {
    scan_info.security_mode = SL_WIFI_WEP;
  } else {
    scan_info.security_mode = SL_WIFI_OPEN;
  }

  subtype = wifi_frame->fc[0] & SLI_WIFI_FRAME_SUBTYPE_MASK;
  switch (subtype) {
    case SLI_WIFI_FRAME_SUBTYPE_PROBE_RESP:
    case SLI_WIFI_FRAME_SUBTYPE_BEACON: {
      if (packet->length <= SLI_WIFI_MINIMUM_FRAME_LENGTH) {
        return;
      }

      memcpy(scan_info.bssid, wifi_frame->bssid, SLI_WIFI_HARDWARE_ADDRESS_LENGTH);
      ies_length                        = packet->length - SLI_WIFI_MINIMUM_FRAME_LENGTH;
      sli_wifi_data_tagged_info_t *info = (sli_wifi_data_tagged_info_t *)wifi_frame->tagged_info;
      uint32_t ie_guard                 = 0U;
      // Upper bound for IE walks: each IE uses at least SLI_WIFI_IE_ELEMENT_ID_LENGTH_OCTETS (ID + Length).
      // Tie the guard to the initial tagged-parameter length so valid long beacons are not truncated.
      const uint16_t tagged_bytes_total      = ies_length;
      const uint32_t max_ie_guard_iterations = SLI_WIFI_BEACON_IE_GUARD_MAX_ITERATIONS(tagged_bytes_total);

      while (ies_length != 0U) {
        if (++ie_guard > max_ie_guard_iterations) {
          break;
        }
        // data_length is uint8_t; promote to uint16_t so step fits the MPDU slice in ies_length.
        const uint16_t ie_hdr_len = (uint16_t)sizeof(sli_wifi_data_tagged_info_t);
        if (ies_length < ie_hdr_len) {
          break;
        }
        uint16_t step = (uint16_t)(ie_hdr_len + (uint16_t)info->data_length);
        if (step > ies_length) {
          break;
        }
        sli_process_tag_info(info, &scan_info);
        ies_length = (uint16_t)(ies_length - step);
        info       = (sli_wifi_data_tagged_info_t *)((uint8_t *)info + step);
      }

      // Ensure transient flag is never stored (defensive if more code sets it later)
      scan_info.wpa_vendor_ie_seen = false;
      sli_store_scan_info_element(&scan_info);
    } break;
    default:
      return;
  }

  return;
}

// Function to update a existing entry or create new entry for scan results database
static sli_scan_info_t *sli_update_or_create_scan_info_element(const sli_scan_info_t *info)
{
  sli_scan_info_t **scan_db_head = sli_get_scan_info_database();
  sli_scan_info_t *element       = NULL;
  sli_scan_info_t *prev          = NULL;

  element = *scan_db_head;
  while (NULL != element) {
    if (0 == memcmp(info->bssid, element->bssid, SLI_WIFI_HARDWARE_ADDRESS_LENGTH)) {
      element->seen_count++;
      element->channel       = info->channel;
      element->security_mode = info->security_mode;
      element->rssi          = info->rssi;
      element->network_type  = info->network_type;
      memcpy(element->ssid, info->ssid, 34);

      // Remove the element from its current position so it can be re-inserted in sorted order
      if (NULL == prev) {
        *scan_db_head = element->next;
      } else {
        prev->next = element->next;
      }
      element->next = NULL;
      return element;
    }
    prev    = element;
    element = element->next;
  }

  if (NULL == element) {
    element = (sli_scan_info_t *)SLI_MALLOC(sizeof(sli_scan_info_t));
    if (element == NULL) {
      return NULL;
    }
    memcpy(element, info, sizeof(sli_scan_info_t));
    element->seen_count = 1;
    element->next       = NULL;
    return element;
  }

  return NULL;
}

// Function to store a given scan info element in scan results database
static void sli_store_scan_info_element(const sli_scan_info_t *info)
{
  sli_scan_info_t *element       = NULL;
  sli_scan_info_t *head          = NULL;
  sli_scan_info_t *tail          = NULL;
  sli_scan_info_t **scan_db_head = sli_get_scan_info_database();

  if (NULL == info) {
    return;
  }

  element = sli_update_or_create_scan_info_element(info);
  if (NULL == element) {
    return;
  }

  if (NULL == *scan_db_head) {
    *scan_db_head = element;
    return;
  }

  tail = *scan_db_head;
  while (NULL != tail) {
    if (element->rssi < tail->rssi) {
      element->next = tail;
      if (NULL == head) {
        *scan_db_head = element;
      } else {
        head->next = element;
      }
      break;
    }

    head = tail;
    tail = tail->next;

    if (NULL == tail) {
      head->next = element;
    }
  }

  return;
}

// Function to identify Authentication Key Management Type
static uint32_t sli_get_key_management_info(const sli_wifi_cipher_suite_t *akms, uint16_t akmsc)
{
  uint32_t key_mgmt = 0;
  uint32_t oui_type;

  if (NULL == akms) {
    return 0;
  }
  // Limit AKM suite count to a safe upper bound (defensive against corrupt IE length fields).
  if (akmsc > SLI_WIFI_MAX_AKM_SUITES_PARSE) {
    akmsc = SLI_WIFI_MAX_AKM_SUITES_PARSE;
  }
  for (int i = 0; i < akmsc; i++) {
    oui_type = ((akms[i].cs_oui[0] << 24) | (akms[i].cs_oui[1] << 16) | (akms[i].cs_oui[2] << 8) | akms[i].cs_type);

    switch (oui_type) {
      case SLI_AUTH_KEY_MGMT_UNSPEC_802_1X:
        key_mgmt |= SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA | SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA2;
        break;
      case SLI_AUTH_KEY_MGMT_PSK_OVER_802_1X:
        key_mgmt |= SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA_PSK | SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA2_PSK;
        break;
      case SLI_AUTH_KEY_MGMT_802_1X_SHA256:
        key_mgmt |= SLI_WLAN_AUTH_KEY_MGMT_TYPE_802_1X_SHA256;
        break;
      case SLI_AUTH_KEY_MGMT_PSK_SHA256:
        key_mgmt |= SLI_WLAN_AUTH_KEY_MGMT_TYPE_PSK_SHA256;
        break;
      case SLI_AUTH_KEY_MGMT_SAE:
        key_mgmt |= SLI_WLAN_AUTH_KEY_MGMT_TYPE_SAE;
        break;
      case SLI_AUTH_KEY_MGMT_FT_SAE:
        key_mgmt |= SLI_WLAN_AUTH_KEY_MGMT_TYPE_FT_SAE;
        break;
      default:
        break;
    }
  }
  return key_mgmt;
}

// Helper function to process RSN element
static void sli_process_rsn_element(const sli_wifi_data_tagged_info_t *info, sli_scan_info_t *scan_info)
{
  // RSN IE must hold version (2), group cipher (4), and pairwise cipher count (2) before variable fields.
  // Truncated IE: leave security_mode unchanged (capabilities and earlier IEs remain authoritative).
  if (info->data_length < SLI_WIFI_RSN_IE_MIN_DATA_OCTETS) {
    return;
  }
  scan_info->security_mode          = SL_WIFI_WPA2_ENTERPRISE;
  const sli_wifi_rsn_element_t *rsn = (const sli_wifi_rsn_element_t *)info->data;
  const uint8_t *const rsn_end      = info->data + info->data_length;
  uint16_t pcsc                     = (uint16_t)(rsn->pcsc[0] | (rsn->pcsc[1] << 8));
  uint16_t max_pcsc                 = (uint16_t)((info->data_length - SLI_WIFI_RSN_DATA_OCTETS_BEFORE_PCSC_LIST)
                                 / (uint16_t)sizeof(sli_wifi_cipher_suite_t));
  if (pcsc > max_pcsc) {
    pcsc = max_pcsc;
  }
  // Derive AKM suite count offset only after pcsc is clamped (defensive against corrupt IE counts).
  const uint8_t *akmslc = (const uint8_t *)rsn->pcsl + ((size_t)pcsc * sizeof(sli_wifi_cipher_suite_t));
  // rsn_end is one past the last IE octet; need SLI_WIFI_LE16_FIELD_OCTETS for little-endian AKM suite count.
  if (akmslc + SLI_WIFI_LE16_FIELD_OCTETS > rsn_end) {
    return;
  }
  uint16_t akmsc = (uint16_t)(akmslc[0] | (akmslc[1] << 8));
  uint16_t max_akmsc =
    (uint16_t)((size_t)(rsn_end - (akmslc + SLI_WIFI_LE16_FIELD_OCTETS)) / sizeof(sli_wifi_cipher_suite_t));
  if (akmsc > max_akmsc) {
    akmsc = max_akmsc;
  }
  const sli_wifi_cipher_suite_t *akms = (const sli_wifi_cipher_suite_t *)(akmslc + SLI_WIFI_LE16_FIELD_OCTETS);
  uint8_t wlan_gcs_oui[3]             = { 0x00, 0x0F, 0xAC };

  SL_DEBUG_LOG_V2(DEBUG, "RSN OUI %02x:%02x:%02x.\r\n", rsn->gcs.cs_oui[0], rsn->gcs.cs_oui[1], rsn->gcs.cs_oui[2]);
  SL_DEBUG_LOG_V2(DEBUG, "Pairwise cipher suite count: %u.\r\n", pcsc);

  if (!memcmp(rsn->gcs.cs_oui, wlan_gcs_oui, 3)) {
    // If WPA vendor IE was also present, report WPA/WPA2 mixed (PSK); enterprise stays WPA2_ENTERPRISE
    scan_info->security_mode = (scan_info->wpa_vendor_ie_seen) ? SL_WIFI_WPA_WPA2_MIXED : SL_WIFI_WPA2;
    uint32_t key             = sli_get_key_management_info(akms, akmsc);

    if ((akmsc > 0u) && (akms[0].cs_type == SLI_WIFI_CIPHER_SUITE_TYPE_IEEE_802_1X)) {
      scan_info->security_mode = SL_WIFI_WPA2_ENTERPRISE;
    }

    if (key & SLI_WLAN_AUTH_KEY_MGMT_TYPE_802_1X_SHA256) {
      scan_info->security_mode = SL_WIFI_WPA3_ENTERPRISE;
      if ((key & SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA) || (key & SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA2)) {
        scan_info->security_mode = SL_WIFI_WPA3_TRANSITION_ENTERPRISE;
      }
    }

    if (key & SLI_WLAN_AUTH_KEY_MGMT_TYPE_SAE) {
      scan_info->security_mode = SL_WIFI_WPA3;
      if ((key & SLI_WLAN_AUTH_KEY_MGMT_TYPE_PSK_SHA256) || (key & SLI_WLAN_AUTH_KEY_MGMT_TYPE_WPA2_PSK)) {
        scan_info->security_mode = SL_WIFI_WPA3_TRANSITION;
      }
    }
  }
}

// Helper function to process Vendor Specific element
static void sli_process_vendor_specific_element(const sli_wifi_data_tagged_info_t *info, sli_scan_info_t *scan_info)
{
  const uint8_t *wpa_ie = info->data;
  uint16_t len          = info->data_length;

  if (len < SLI_WIFI_WPA_VENDOR_IE_MIN_OCTETS || wpa_ie[0] != SLI_WIFI_WPA_VENDOR_OUI_TYPE0
      || wpa_ie[1] != SLI_WIFI_WPA_VENDOR_OUI_TYPE1 || wpa_ie[2] != SLI_WIFI_WPA_VENDOR_OUI_TYPE2
      || wpa_ie[3] != SLI_WIFI_WPA_VENDOR_TYPE_WPA) {
    return;
  }

  const uint8_t *const vendor_end = wpa_ie + len;

  // Pairwise (unicast) cipher suite count is LE16 at SLI_WIFI_WPA_VENDOR_PAIRWISE_COUNT_OFFSET_LSB; list follows.
  uint16_t ucsc       = (uint16_t)(wpa_ie[SLI_WIFI_WPA_VENDOR_PAIRWISE_COUNT_OFFSET_LSB]
                             | ((uint16_t)wpa_ie[SLI_WIFI_WPA_VENDOR_PAIRWISE_COUNT_OFFSET_LSB + 1U] << 8));
  const uint8_t *ucsl = wpa_ie + SLI_WIFI_WPA_VENDOR_PAIRWISE_SUITE_LIST_OFFSET;
  size_t max_ucsc     = (size_t)(vendor_end - ucsl) / sizeof(sli_wifi_cipher_suite_t);
  // Reject truncated IEs: do not clamp counts (that would promote OPEN/WEP to WPA on corrupt data).
  if (ucsc > max_ucsc) {
    return;
  }
  const uint8_t *list_count = ucsl + ((size_t)ucsc * sizeof(sli_wifi_cipher_suite_t));
  if ((list_count + SLI_WIFI_LE16_FIELD_OCTETS) > vendor_end) {
    return;
  }

  uint16_t akmsc                      = (uint16_t)(list_count[0] | (list_count[1] << 8));
  const sli_wifi_cipher_suite_t *akms = (const sli_wifi_cipher_suite_t *)(list_count + SLI_WIFI_LE16_FIELD_OCTETS);
  size_t rem                          = (size_t)(vendor_end - (const uint8_t *)akms);
  uint16_t max_akmsc                  = (uint16_t)(rem / sizeof(sli_wifi_cipher_suite_t));
  if (akmsc > max_akmsc) {
    return;
  }

  // Layout matches declared counts; record WPA IE for WPA/WPA2 mixed detection and refine security_mode.
  scan_info->wpa_vendor_ie_seen = true;

  // If RSN was not seen yet, WPA IE implies WPA; refine using AKM (802.1X vs PSK).
  if (scan_info->security_mode == SL_WIFI_OPEN || scan_info->security_mode == SL_WIFI_WEP) {
    scan_info->security_mode = SL_WIFI_WPA;
    if ((akmsc > 0u) && (akms[akmsc - 1u].cs_type == SLI_WIFI_CIPHER_SUITE_TYPE_IEEE_802_1X)) {
      scan_info->security_mode = SL_WIFI_WPA_ENTERPRISE;
    }
  } else if (scan_info->security_mode == SL_WIFI_WPA2) {
    scan_info->security_mode = SL_WIFI_WPA_WPA2_MIXED;
  }
}

// Function to parse Information elements in WiFi Beacon or Probe response frames
static void sli_process_tag_info(const sli_wifi_data_tagged_info_t *info, sli_scan_info_t *scan_info)
{
  switch (info->tag) {
    case SLI_WLAN_TAG_SSID: {
      // IEEE 802.11 SSID element body is at most SLI_WIFI_SSID_IE_MAX_OCTETS octets; scan_info.ssid[] is NUL-terminated.
      uint8_t n = info->data_length;
      if (n > SLI_WIFI_SSID_IE_MAX_OCTETS) {
        n = SLI_WIFI_SSID_IE_MAX_OCTETS;
      }
      memcpy(scan_info->ssid, info->data, n);
      scan_info->ssid[n] = 0;
      break;
    }

    case SLI_WLAN_TAG_RSN:
      sli_process_rsn_element(info, scan_info);
      break;

    case SLI_WLAN_TAG_VENDOR_SPECIFIC:
      sli_process_vendor_specific_element(info, scan_info);
      break;

    default:
      break;
  }

  return;
}

sl_wifi_event_t sli_wifi_convert_event_to_sl_wifi_event(uint32_t command, uint16_t frame_status)
{
  // Define a constant indicating a fail indication event
  const sl_wifi_event_t fail_indication = (frame_status != SL_STATUS_OK) ? SL_WIFI_EVENT_FAIL_INDICATION : 0;

  // Switch-case to map SI91x events to SL Wi-Fi events
  switch (command) {
    case SLI_WIFI_RSP_BG_SCAN:
    case SLI_WIFI_RSP_SCAN:
    case SLI_WIFI_RSP_SCAN_RESULTS:
      return SL_WIFI_SCAN_RESULT_EVENT | fail_indication;
    case SLI_WIFI_RSP_JOIN:
      return SL_WIFI_JOIN_EVENT | fail_indication;
    case SLI_WIFI_RSP_GET_STATS:
      if (frame_status != SL_STATUS_OK) {
        return SL_WIFI_STATS_RESPONSE_EVENTS | fail_indication;
      }
      return SL_WIFI_STATS_EVENT;
    case SLI_WIFI_RSP_RX_STATS:
      if (frame_status != SL_STATUS_OK) {
        return SL_WIFI_STATS_RESPONSE_EVENTS | fail_indication;
      }
      return SL_WIFI_STATS_ASYNC_EVENT;
    case SLI_WIFI_RATE_RSP_STATS:
      if (frame_status != SL_STATUS_OK) {
        return SL_WIFI_STATS_RESPONSE_EVENTS | fail_indication;
      }
      return SL_WIFI_STATS_TEST_MODE_EVENT;
    case SLI_WIFI_RSP_EXT_STATS:
      if (frame_status != SL_STATUS_OK) {
        return SL_WIFI_STATS_RESPONSE_EVENTS | fail_indication;
      }
      return SL_WIFI_STATS_ADVANCE_EVENT;
    case SLI_WIFI_RSP_MODULE_STATE:
      if (frame_status != SL_STATUS_OK) {
        return SL_WIFI_STATS_RESPONSE_EVENTS | fail_indication;
      }
      return SL_WIFI_STATS_MODULE_STATE_EVENT;
    case SLI_WIFI_RSP_HTTP_OTAF:
      return SL_WIFI_HTTP_OTA_FW_UPDATE_EVENT | fail_indication;
    case SLI_WIFI_RSP_CLIENT_CONNECTED:
      return SL_WIFI_CLIENT_CONNECTED_EVENT | fail_indication;
    case SLI_WIFI_RSP_CLIENT_DISCONNECTED:
      return SL_WIFI_CLIENT_DISCONNECTED_EVENT | fail_indication;
    case SLI_WIFI_RSP_TWT_ASYNC:
      if (frame_status == SL_STATUS_OK) {
        return SL_WIFI_TWT_RESPONSE_EVENT;
      } else {
        return SL_WIFI_TWT_RESPONSE_EVENT | (frame_status << 16);
      }
    case SLI_WIFI_RSP_TRANSCEIVER_TX_DATA_STATUS:
      return SL_WIFI_TRANSCEIVER_TX_DATA_STATUS_CB | fail_indication;
    case SLI_WIFI_RX_DOT11_DATA:
      return SL_WIFI_TRANSCEIVER_RX_DATA_RECEIVE_CB | fail_indication;
    default:
      return SL_WIFI_INVALID_EVENT;
  }
}

#ifdef SPI_EXTENDED_TX_LEN_2K
#define SLI_WIFI_TX_BLOCK_SIZE 2324
#else
#define SLI_WIFI_TX_BLOCK_SIZE 1640
#endif
// Max command payload fitting one CE TX pool block (block size minus hidden
// pool management pointer and sl_wifi_system_packet_t header). Larger
// payloads would memcpy past the block and corrupt the pool.
#define SLI_WIFI_MAX_CMD_PAYLOAD (SLI_WIFI_TX_BLOCK_SIZE - sizeof(void *) - sizeof(sl_wifi_system_packet_t))

sl_status_t sli_wifi_send_command_with_custom_desc(uint32_t command,
                                                   sli_wifi_command_type_t command_type,
                                                   const void *data,
                                                   uint32_t data_length,
                                                   sli_wifi_wait_period_t wait_period,
                                                   void *sdk_context,
                                                   void **response_buffer,
                                                   uint8_t custom_host_desc)
{

  sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status              = SL_STATUS_OK;

  // Reject payloads that cannot fit in one pool block
  if (data_length > SLI_WIFI_MAX_CMD_PAYLOAD) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_TX_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_HYBRID,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&packet);
  VERIFY_STATUS_AND_RETURN(status);
  // Clear the packet descriptor and copy the command data if available
  memset(packet->desc, 0, sizeof(packet->desc));
  if (data != NULL) {
    memcpy(packet->data, data, data_length);
  }

  // Fill frame type
  packet->length    = data_length & 0xFFF;
  packet->command   = (uint16_t)command;
  packet->unused[1] = custom_host_desc;

  return sli_wifi_send_command_packet(command, command_type, packet, wait_period, sdk_context, response_buffer);
}

sl_status_t sli_wifi_send_command(uint32_t command,
                                  sli_wifi_command_type_t command_type,
                                  const void *data,
                                  uint32_t data_length,
                                  sli_wifi_wait_period_t wait_period,
                                  void *sdk_context,
                                  void **response_buffer)
{
  sl_wifi_system_packet_t *packet = NULL;
  sl_status_t status              = SL_STATUS_OK;

  // Reject payloads that cannot fit in one pool block
  if (data_length > SLI_WIFI_MAX_CMD_PAYLOAD) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_TX_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&packet);
  VERIFY_STATUS_AND_RETURN(status);
  // Clear the packet descriptor and copy the command data if available
  memset(packet->desc, 0, sizeof(packet->desc));
  if (data != NULL) {
    memcpy(packet->data, data, data_length);
  }

  // Fill frame type
  packet->length  = data_length & 0xFFF;
  packet->command = (uint16_t)command;

  return sli_wifi_send_command_packet(command, command_type, packet, wait_period, sdk_context, response_buffer);
}

sl_status_t sli_wifi_send_command_packet(uint32_t command,
                                         sli_wifi_command_type_t command_type,
                                         sl_wifi_system_packet_t *packet,
                                         sli_wifi_wait_period_t wait_period,
                                         void *sdk_context,
                                         void **response_buffer)
{

  sli_command_engine_tx_info_t tx_info = { 0 };
  sl_status_t status                   = SL_STATUS_OK;

  // Set SLI_SI3XX_FEAT_FW_UPDATE_NEW_CODE in the feature bit map to retrieve the latest firmware result codes
  if (command == SLI_WIFI_REQ_OPERMODE) {
    sl_wifi_system_boot_configuration_t *boot_configuration = (sl_wifi_system_boot_configuration_t *)packet->data;
    boot_configuration->feature_bit_map |= SLI_SI91X_FEAT_FW_UPDATE_NEW_CODE;
  }

  if (command_type < SI91X_CMD_MAX) {
    packet->desc[1] |= (firmware_queue_id[command_type] << 4);
    tx_info.packet_type = command_packet_type[command_type];
  } else {
    tx_info.packet_type = command_type;
  }

  tx_info.data_packet        = (void *)packet;
  tx_info.data_packet_length = ((packet->length & 0xFFF) + sizeof(sl_wifi_system_packet_t));
  tx_info.frame_id           = (uint16_t)command;
  tx_info.flags              = SLI_COMMAND_ENGINE_COMMAND_PACKET;
  tx_info.timeout            = 0;
  tx_info.context            = sdk_context;
  tx_info.packet_id          = 0;

  // Check the wait_period to determine the flags for packet handling
  if (wait_period == SLI_WIFI_RETURN_IMMEDIATELY) {
    // If wait_period indicates an immediate return, set flags to 0
    tx_info.flags |= SLI_COMMAND_ENGINE_SEQ_ASYNC_RESPONSE_PACKET;
  } else {
    // If not an immediate return, set the SLI_WIFI_PACKET_RESPONSE_STATUS flag
    tx_info.flags |= SLI_COMMAND_ENGINE_SYNC_RESPONSE_STATUS_PACKET;
    // Additionally, set the SLI_WIFI_PACKET_RESPONSE_PACKET flag if the SL_SLI_SI3XX_WAIT_FOR_RESPONSE_BIT is set in wait_period
    if (response_buffer != NULL) {
      tx_info.flags |= SLI_COMMAND_ENGINE_SYNC_RESPONSE_DATA_PACKET;
    }
  }

  // Check the command type and set the flags accordingly
  switch (command) {
    case SLI_WIFI_REQ_PWRMODE:
    case SLI_WIFI_REQ_OPERMODE:
    case SLI_COMMON_RSP_SOFT_RESET:
    case SLI_COMMON_RSP_ENABLE_DISABLE_BLE:
      tx_info.flags |= SLI_COMMAND_ENGINE_REQUEST_WITH_GLOBAL_TX_BLOCK;
      break;
    default:
      break;
  }

  if (!(tx_info.flags & SLI_COMMAND_ENGINE_SEQ_ASYNC_RESPONSE_PACKET)) {
    // Calculate the wait time based on wait_period
    if ((wait_period & SLI_WIFI_WAIT_FOR_EVER) == SLI_WIFI_WAIT_FOR_EVER) {
      tx_info.timeout = (uint32_t)osWaitForever;
    } else {
      // Store timeout in kernel ticks so command-engine timeout comparisons remain unit-consistent.
      tx_info.timeout = SLI_SYSTEM_MS_TO_TICKS((uint32_t)(wait_period & ~SLI_WIFI_WAIT_FOR_RESPONSE_BIT));
    }
  }

  status = sli_command_engine_send_packet(&sli_wifi_command_engine, &tx_info);
  if (status != SL_STATUS_OK) {
    sli_buffer_manager_free_buffer(packet);
  }
  VERIFY_STATUS_AND_RETURN(status);

  // Check if the command should return immediately or wait for a response
  if (wait_period == SLI_WIFI_RETURN_IMMEDIATELY) {
    return SL_STATUS_IN_PROGRESS;
  }

  return sli_wifi_receive_response_buffer(tx_info.packet_type,
                                          tx_info.packet_id,
                                          wait_period,
                                          SLI_WIFI_WAIT_ON_THREAD_ID,
                                          response_buffer);
}

sl_status_t sli_wifi_receive_response_buffer(uint16_t command_packet_type,
                                             uint16_t packet_id,
                                             sli_wifi_wait_period_t wait_period,
                                             uint8_t wait_type,
                                             void **response_buffer)
{

  sli_wifi_wait_period_t wait_time         = 0;
  sl_wifi_system_packet_t *response_packet = NULL;
  uint16_t firmware_status                 = 0;
  sl_status_t status                       = 0;

  // Calculate the wait time based on wait_period
  if ((wait_period & SLI_WIFI_WAIT_FOR_EVER) == SLI_WIFI_WAIT_FOR_EVER) {
    wait_time = osWaitForever;
  } else {
    wait_time = (wait_period & ~SLI_WIFI_WAIT_FOR_RESPONSE_BIT);
  }
  sli_command_engine_metadata_t *metadata_response = NULL;

  // Wait for a response packet and handle it
  status = sli_wifi_driver_wait_for_response_packet(command_packet_type,
                                                    packet_id,
                                                    (uint32_t)wait_time,
                                                    wait_type,
                                                    &metadata_response);
  VERIFY_STATUS_AND_RETURN(status);

  if (metadata_response == NULL) {
    return SL_STATUS_FAIL;
  }
  firmware_status = metadata_response->packet_status;
  // Check if a data packet is present in the response metadata
  if (metadata_response->tx_info.data_packet != NULL) {
    response_packet =
      (sl_wifi_system_packet_t *)sli_wifi_host_get_buffer_data(metadata_response->tx_info.data_packet, 0, NULL);

    // If the response_buffer pointer is not NULL and response packet flag is set, assign the response packet to response_buffer
    if (NULL != response_buffer && (SLI_WIFI_WAIT_FOR_RESPONSE_BIT == (wait_period & SLI_WIFI_WAIT_FOR_RESPONSE_BIT))) {
      // Clear Firmware Queue info from data length parameter
      response_packet->length = (response_packet->length & 0x0FFF);
      *response_buffer        = metadata_response->tx_info.data_packet;
    } else {
      // Free the data packet buffer if not needed
      sli_buffer_manager_free_buffer((sli_buffer_t)metadata_response->tx_info.data_packet);
    }
  }

  sli_buffer_manager_free_buffer((sli_buffer_t)metadata_response);
  return sli_wifi_convert_and_save_firmware_status(firmware_status);
}

sl_status_t sli_wifi_async_send_command(uint32_t command,
                                        sli_wifi_command_type_t command_type,
                                        const void *data,
                                        uint32_t data_length,
                                        void *custom_desc)
{
  sl_wifi_system_packet_t *packet      = NULL;
  sl_status_t status                   = SL_STATUS_OK;
  sli_command_engine_tx_info_t tx_info = { 0 };

  // Reject payloads that cannot fit in one pool block
  if (data_length > SLI_WIFI_MAX_CMD_PAYLOAD) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  // Allocate a buffer for the command with appropriate size
  status = sli_buffer_manager_allocate_buffer(SLI_BUFFER_MANAGER_CE_TX_POOL,
                                              SLI_BUFFER_MANAGER_ALLOCATION_TYPE_DEDICATED,
                                              SLI_WIFI_ALLOCATE_COMMAND_BUFFER_WAIT_TIME,
                                              (sli_buffer_t)&packet);
  VERIFY_STATUS_AND_RETURN(status);
  // Clear the packet descriptor and copy the command data if available
  if (custom_desc != NULL) {
    memcpy(packet->desc, custom_desc, sizeof(packet->desc));
  } else {
    memset(packet->desc, 0, sizeof(packet->desc));
  }
  if (data != NULL) {
    memcpy(packet->data, data, data_length);
  }

  // Fill frame type
  packet->length  = data_length & 0xFFF;
  packet->command = (uint16_t)command;
  if (command_type < SI91X_CMD_MAX) {
    packet->desc[1] |= (firmware_queue_id[command_type] << 4);
    tx_info.packet_type = command_packet_type[command_type];
  } else {
    tx_info.packet_type = command_type;
  }

  tx_info.data_packet        = (void *)packet;
  tx_info.data_packet_length = ((packet->length & 0xFFF) + sizeof(sl_wifi_system_packet_t));
  tx_info.frame_id           = (uint16_t)command;
  tx_info.flags              = SLI_COMMAND_ENGINE_COMMAND_PACKET;
  tx_info.timeout            = 0;
  tx_info.context            = NULL;
  tx_info.packet_id          = 0;
  tx_info.flags |= SLI_COMMAND_ENGINE_ASYNC_RESPONSE_PACKET;

  status = sli_command_engine_send_packet(&sli_wifi_command_engine, &tx_info);
  if (status != SL_STATUS_OK) {
    sli_buffer_manager_free_buffer(packet);
  }
  VERIFY_STATUS_AND_RETURN(status);

  return SL_STATUS_IN_PROGRESS;
}

static bool rx_packet_identity_handler(const sli_queue_t *handle, const void *data, const void *node_match_data)
{
  UNUSED_PARAMETER(handle);
  uint16_t *packet_id                     = (uint16_t *)node_match_data;
  sli_command_engine_metadata_t *metadata = (sli_command_engine_metadata_t *)data;

  SL_DEBUG_LOG_V2(DEBUG,
                  "Comparing expected packetID : %u with packetId of queue node : %u..!\n",
                  *packet_id,
                  metadata->tx_info.packet_id);
  if (*packet_id == metadata->tx_info.packet_id) {
    return true;
  }

  return false;
}

sl_status_t sli_wifi_driver_wait_for_response_packet(uint16_t command_packet_type,
                                                     uint16_t packet_id,
                                                     uint32_t wait_period_ms,
                                                     uint8_t wait_type,
                                                     sli_command_engine_metadata_t **metadata_response)
{
  // Check that metadata_response is a valid pointer
  SL_VERIFY_POINTER_OR_RETURN(metadata_response, SL_STATUS_INVALID_PARAMETER);

  uint32_t events                                                 = 0;
  sli_command_engine_metadata_t *buffer                           = NULL;
  sli_command_engine_packet_type_configuration_t packet_type_info = { 0 };
  sl_status_t status                                              = SL_STATUS_OK;

  // Get packet type configuration for the given command type
  status = sli_command_engine_get_rx_queue_info_from_packet_type(&sli_wifi_command_engine,
                                                                 command_packet_type,
                                                                 &packet_type_info);
  VERIFY_STATUS_AND_RETURN(status);

  uint32_t start_time_ticks     = osKernelGetTickCount();
  uint32_t elapsed_time_ticks   = 0;
  uint32_t remaining_time_ticks = 0;
  uint32_t wait_period_ticks    = (wait_period_ms == osWaitForever) ? osWaitForever
                                                                    : SLI_SYSTEM_MS_TO_TICKS(wait_period_ms);
  if (wait_type == SLI_WIFI_WAIT_ON_THREAD_ID) {
    // Wait for thread event flags and retry dequeue until matching response or timeout.
    do {
      remaining_time_ticks = (elapsed_time_ticks > wait_period_ticks) ? 0 : (wait_period_ticks - elapsed_time_ticks);
      SL_DEBUG_LOG_V2(DEBUG,
                      "Waiting on Thread Events: 0x%lX on event id : 0x%X for queue 0x%X\n",
                      packet_type_info.sync_response_event,
                      (unsigned int)packet_type_info.sync_response_event_id,
                      (unsigned int)packet_type_info.sync_response_queue);
      events = osThreadFlagsWait(packet_type_info.sync_response_event, osFlagsWaitAny, remaining_time_ticks);
      SL_DEBUG_LOG_V2(DEBUG,
                      "Got Thread Events: 0x%lX for queue 0x%X\n",
                      events,
                      (unsigned int)packet_type_info.sync_response_queue);
      if (events == (uint32_t)osErrorTimeout || events == (uint32_t)osErrorResource) {
        // Timeout or resource error
        return SL_STATUS_TIMEOUT;
      } else if ((events & (uint32_t)osFlagsError) != 0u) {
        // ponytail: a non-timeout flags error (dead object returns
        // osFlagsErrorParameter instantly) must not reach the mask check
        // below — 0xFFFFFFFC satisfies any single-bit mask.
        return SL_STATUS_FAIL;
      } else if ((packet_type_info.sync_response_event & events) == packet_type_info.sync_response_event) {
        // Remove the node with the matching packet_id from the queue
        status = sli_queue_manager_remove_node_from_queue(packet_type_info.sync_response_queue,
                                                          rx_packet_identity_handler,
                                                          (const void *)&packet_id,
                                                          (void **)metadata_response);
        if (status == SL_STATUS_OK) {
          return SL_STATUS_OK;
        }
        if ((status != SL_STATUS_EMPTY) && (status != SL_STATUS_NOT_FOUND)) {
          VERIFY_STATUS_AND_RETURN(status);
        }
        if (wait_period_ms != osWaitForever) {
          elapsed_time_ticks = sli_wifi_host_elapsed_time(start_time_ticks);
        }
      } else {
        // Other error
        return SL_STATUS_FAIL;
      }
    } while (elapsed_time_ticks < wait_period_ticks);
  } else if (wait_type == SLI_WIFI_WAIT_ON_EVENT_ID) {
    // sync_response_event_id must be a pointer to the event flags handle (see sli_command_engine.h)
    if (packet_type_info.sync_response_event_id == NULL) {
      return SL_STATUS_INVALID_CONFIGURATION;
    }

    do {
      remaining_time_ticks = (elapsed_time_ticks > wait_period_ticks) ? 0 : (wait_period_ticks - elapsed_time_ticks);
      SL_DEBUG_LOG_V2(DEBUG,
                      "Waiting on Events: 0x%lX on event id : 0x%X for queue 0x%X\n",
                      packet_type_info.sync_response_event,
                      (unsigned int)packet_type_info.sync_response_event_id,
                      (unsigned int)packet_type_info.sync_response_queue);

      events = osEventFlagsWait(*packet_type_info.sync_response_event_id,
                                packet_type_info.sync_response_event,
                                (osFlagsWaitAny | osFlagsNoClear),
                                remaining_time_ticks);

      if (events == (uint32_t)osErrorTimeout || events == (uint32_t)osErrorResource) {
        // Timeout or resource error
        return SL_STATUS_TIMEOUT;
      }
      if ((events & (uint32_t)osFlagsError) != 0u) {
        // ponytail: dead/NULL flags object returns osFlagsErrorParameter
        // instantly on EVERY call. Previously it fell through as a fake
        // "event", the queue remove came up EMPTY/NOT_FOUND and the 2ms
        // backoff loop burned the whole caller timeout before returning
        // TIMEOUT — masking the real failure (field "Get MAC failed (0x3)"
        // retry storm). Fail fast so the caller sees the object is gone.
        SL_DEBUG_LOG_V2(ERROR, "sync response wait: osEventFlagsWait failed 0x%lX\r\n", events);
        return SL_STATUS_FAIL;
      }

      SL_DEBUG_LOG_V2(DEBUG,
                      "Got Events: 0x%lX for queue 0x%X\n",
                      events,
                      (unsigned int)packet_type_info.sync_response_queue);

      // Enter atomic section to safely access the queue
      CORE_irqState_t state = CORE_EnterAtomic();

      // Remove the node with the matching packet_id from the queue
      status = sli_queue_manager_remove_node_from_queue(packet_type_info.sync_response_queue,
                                                        rx_packet_identity_handler,
                                                        (const void *)&packet_id,
                                                        (void **)&buffer);

      if (status == SL_STATUS_OK) {
        // If the queue is empty after removal, clear the event flag
        if (SLI_QUEUE_MANAGER_IS_QUEUE_EMPTY(packet_type_info.sync_response_queue)) {
          osEventFlagsClear(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
        }
        CORE_ExitAtomic(state);
        *metadata_response = buffer;
        return SL_STATUS_OK;
      } else if (status == SL_STATUS_EMPTY) {
        // If the queue is empty, clear the event flag
        osEventFlagsClear(*packet_type_info.sync_response_event_id, packet_type_info.sync_response_event);
      }
      CORE_ExitAtomic(state);
      if (status == SL_STATUS_NOT_FOUND) {
        // Add a small delay to avoid busy waiting
        // ponytail: 2ms was 500 wake/s at this priority during response
        // starvation (Get-MAC 0x3 retry storm) — floor at 5ms.
        osDelay(SLI_SYSTEM_MS_TO_TICKS(5));
      }

      // Update elapsed time
      if (wait_period_ms != osWaitForever) {
        elapsed_time_ticks = sli_wifi_host_elapsed_time(start_time_ticks);
      }
    } while (elapsed_time_ticks < wait_period_ticks);

  } else {
    return SL_STATUS_INVALID_PARAMETER;
  }

  return SL_STATUS_TIMEOUT;
}

uint8_t sli_wifi_get_vap_id_from_operation_mode(const sl_wifi_system_packet_t *rx_packet)
{
  // Query the current operation mode
  sl_wifi_operation_mode_t current_operation_mode = sli_wifi_get_opermode();

  // Station modes: CLIENT, ENTERPRISE_CLIENT, TRANSCEIVER, TRANSMIT_TEST
  if (current_operation_mode == SL_WIFI_CLIENT_MODE || current_operation_mode == SL_WIFI_ENTERPRISE_CLIENT_MODE
      || current_operation_mode == SL_WIFI_TRANSCEIVER_MODE || current_operation_mode == SL_WIFI_TRANSMIT_TEST_MODE) {
    return SL_WIFI_CLIENT_VAP_ID;
  }

  // AP mode
  if (current_operation_mode == SL_WIFI_ACCESS_POINT_MODE) {
    return SL_WIFI_AP_VAP_ID;
  }

  // Concurrent mode: check packet descriptor byte 7 to determine VAP ID
  if (current_operation_mode == SL_WIFI_CONCURRENT_MODE) {
    if (rx_packet != NULL) {
      if (rx_packet->desc[7] == SL_WIFI_CLIENT_VAP_ID) {
        return SL_WIFI_CLIENT_VAP_ID;
      } else {
        return SL_WIFI_AP_VAP_ID;
      }
    }
    // Default to AP VAP ID if rx_packet is not provided
    return SL_WIFI_AP_VAP_ID;
  }

  // Default to client VAP ID for unknown modes
  return SL_WIFI_CLIENT_VAP_ID;
}

sl_wifi_buffer_t *sli_wifi_get_response_buffer(sli_command_engine_response_t *response)
{
  if (NULL == response) {
    return NULL;
  }
  return (response->type == SLI_COMMAND_ENGINE_METADATA_RESPONSE)
           ? (sl_wifi_buffer_t *)(((sli_command_engine_metadata_t *)response->data)->tx_info.data_packet)
           : (sl_wifi_buffer_t *)response->data;
}

// Calculate elapsed time from the given starting timestamp
uint32_t sli_wifi_host_elapsed_time(uint32_t starting_timestamp)
{
  return (osKernelGetTickCount() - starting_timestamp);
}

uint16_t sli_wifi_get_wifi_frame_status(const sl_wifi_system_packet_t *packet)
{
  return (uint16_t)(packet->desc[12] + (packet->desc[13] << 8));
}

/**
 * @brief Get metadata from command engine response
 * 
 * @param response Pointer to command engine response
 * @return Pointer to metadata if response type is metadata response, NULL otherwise
 */
sli_command_engine_metadata_t *sli_wifi_get_response_metadata(sli_command_engine_response_t *response)
{
  if (NULL == response) {
    return NULL;
  }
  return (response->type == SLI_COMMAND_ENGINE_METADATA_RESPONSE) ? (sli_command_engine_metadata_t *)(response->data)
                                                                  : NULL;
}

/**
 * @brief Get data pointer from buffer at specified offset
 * 
 * @param buffer Pointer to Wi-Fi buffer
 * @param offset Offset into the buffer data
 * @param data_length Pointer to store remaining data length (optional, can be NULL)
 * @return Pointer to buffer data at specified offset, NULL if invalid buffer or offset
 */
void *sli_wifi_host_get_buffer_data(void *buffer, uint16_t offset, uint16_t *data_length)
{
  if (NULL == buffer) {
    return NULL;
  }

  sl_wifi_buffer_t *temp_buffer = (sl_wifi_buffer_t *)buffer;
  if ((0 != temp_buffer->length) && (offset >= temp_buffer->length)) {
    return NULL;
  }

  if (data_length) {
    *data_length = (uint16_t)(temp_buffer->length) - offset;
  }

  return (void *)&temp_buffer->data[offset];
}
