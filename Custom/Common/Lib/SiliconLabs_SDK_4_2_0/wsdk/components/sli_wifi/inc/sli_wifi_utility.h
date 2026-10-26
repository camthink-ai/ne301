/***************************************************************************/ /**
 * @file    sli_wifi_utility.h
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
#ifndef SLI_WIFI_UTILITY_H
#define SLI_WIFI_UTILITY_H

#include "sl_wifi_types.h"
#include "sli_wifi_types.h"
#include "sl_status.h"
#include "sl_wifi_host_interface.h"
#include "sli_wifi_command_engine_config.h"
#include "sli_wifi_constants.h"
#include "sli_utility.h"
#include <stdint.h>

#define SLI_WIFI_HEADER_SIZE                   16
#define SLI_WIFI_TRANSMIT_TEST_HEADER_SIZE     4
#define SLI_WIFI_TRANSMIT_TEST_MAX_PACKET_SIZE 1640
#define SLI_WIFI_TRANSMIT_TEST_MAX_MEMCPY_CHUNK \
  (SLI_WIFI_TRANSMIT_TEST_MAX_PACKET_SIZE - (SLI_WIFI_HEADER_SIZE + SLI_WIFI_TRANSMIT_TEST_HEADER_SIZE))

/**
 * @brief
 *   Retrieve the response buffer associated with a specific command engine response.
 *
 * @param[in] response
 *   Pointer to the command engine response structure.
 *
 * @return
 *   Pointer to the response buffer, or NULL if not found.
 */
sl_wifi_buffer_t *sli_wifi_get_response_buffer(sli_command_engine_response_t *response);

sli_command_engine_metadata_t *sli_wifi_get_response_metadata(sli_command_engine_response_t *response);

/* Function used to get maximum transmission power */
sl_wifi_max_tx_power_t sli_get_max_tx_power();

/* Function used to retrieve the wifi rate */
sl_status_t sli_wifi_get_saved_rate(sl_wifi_rate_t *transfer_rate);

/**
 * @brief 
 *  Function used to obtain wifi credential type like EsAP,PMK,etc..
 * @param id 
 *  Credential ID as identified by [sl_wifi_credential_id_t](../wiseconnect-api-reference-guide-wi-fi/sl-wifi-types#sl-wifi-credential-id-t).
 * @param type 
 *  It specifies type of credential.
 * @param cred 
 *  Pointer to store the wifi credential information of type [sl_wifi_credential_t](../wiseconnect-api-reference-guide-wi-fi/sl-wifi-credential-t)
 * @return sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details. 
 */
sl_status_t sli_wifi_host_get_credentials(sl_wifi_credential_id_t id, uint8_t type, sl_wifi_credential_t *cred);

/* Function used to set the maximum transmission power */
void sli_wifi_save_max_tx_power(uint8_t max_scan_tx_power, uint8_t max_join_tx_power);

/* Function used to get the maximum transmission power in deci-dBm (Everest/siwx3xx) */
sl_wifi_max_tx_power_decidbm_t sli_get_max_tx_power_decidbm(void);

/* Function used to set the maximum transmission power in deci-dBm (Everest/siwx3xx) */
void sli_wifi_save_max_tx_power_decidbm(int16_t scan_tx_power, int16_t join_tx_power);

/* Function converts SDK encryption mode to NWP supported mode */
sl_status_t sli_wifi_get_nwp_encryption(sl_wifi_encryption_t encryption_mode, uint8_t *encryption_request);

/* Function used to update the access point configuration */
sl_status_t sli_wifi_save_ap_configuration(const sl_wifi_ap_configuration_t *wifi_ap_configuration);

/* Function used to destroy the current access point configuration */
void sli_wifi_reset_ap_configuration();

void sli_wifi_flush_scan_results_database(void);

/** Parse beacon/probe response and update scan results database. Called from event handler. */
void sli_handle_wifi_beacon(sl_wifi_system_packet_t *packet);

/***************************************************************************/ /**
 * @brief
 *   Returns the count of stored extended scan results in the internal database.
 * @param[in] interface
 *   Wi-Fi interface (unused; count is global for the stored-scan list).
 * @param[out] scan_count
 *   Pointer to store the number of stored extended scan results.
 * @return
 *   SL_STATUS_OK on success, SL_STATUS_INVALID_PARAMETER if scan_count is NULL.
 * @note
 *   Used by the callback framework when extended scan completes (empty payload)
 *   to pass the result count or data size to the application callback.
 ******************************************************************************/
sl_status_t sli_wifi_get_stored_scan_result_count(sl_wifi_interface_t interface, uint16_t *scan_count);

sl_status_t sli_wifi_get_stored_scan_results(
  sl_wifi_interface_t interface,
  sl_wifi_extended_scan_result_parameters_t *extended_scan_parameters); //Done

/* Function used to retrieve the access point configuration */
sl_status_t sli_wifi_get_saved_ap_configuration(sl_wifi_ap_configuration_t *wifi_ap_confuguration);

/* Function used to retrieve protocol and transfer rate */
sl_status_t sli_wifi_get_rate_protocol_and_data_rate(const uint8_t data_rate,
                                                     sl_wifi_rate_protocol_t *rate_protocol,
                                                     sl_wifi_rate_t *transfer_rate);
/***************************************************************************/ /**
 * @brief
 *   Reset cached maximum TX power to defaults (for example when Wi-Fi is deinitialized).
 * @details
 *   Resets both the legacy \c sl_wifi_max_tx_power_t storage (31 dBm per field) and the
 *   Everest/siwx3xx \c sl_wifi_max_tx_power_decidbm_t storage (310 deci-dBm per field, 31.0 dBm).
 ******************************************************************************/
void sli_wifi_reset_max_tx_power(void);

/* Function used to set wifi rate to default value of 1 Mbps */
void sli_wifi_reset_sl_wifi_rate();

/***************************************************************************/ /**
 * @brief
 *   Get the current Opermode of the module.
 * @return
 *   sl_wifi_operation_mode_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 ******************************************************************************/
void sli_wifi_set_opermode(sl_wifi_operation_mode_t mode);

sl_status_t sli_wifi_set_listen_interval(sl_wifi_interface_t interface, sl_wifi_listen_interval_t listen_interval);
sl_status_t sli_wifi_set_listen_interval_v2(sl_wifi_interface_t interface,
                                            sl_wifi_listen_interval_v2_t listen_interval);
sl_status_t sli_wifi_get_listen_interval(sl_wifi_interface_t interface, sl_wifi_listen_interval_t *listen_interval);
sl_status_t sli_wifi_get_listen_interval_v2(sl_wifi_interface_t interface,
                                            sl_wifi_listen_interval_v2_t *listen_interval);
/*==============================================*/
/**
 * @brief       Calculate crc for a given byte and accumulate crc.
 * @param[in]   crc8_din   -  crc byte input  
 * @param[in]   crc8_state - accumulated crc  
 * @param[in]   end        - last byte crc  
 * @return      crc value  
 *
 */
uint8_t sli_lmac_crc8_c(uint8_t crc8_din, uint8_t crc8_state, uint8_t end);

/*==============================================*/
/**
 * @brief      Calculate 6-bit hash value for given mac address. 
 * @param[in]  mac - pointer to mac address  
 * @return     6-bit Hash value
 *
 */
uint8_t sli_multicast_mac_hash(const uint8_t *mac);

/* Function used to save the MFP mode */
sl_status_t sli_wifi_save_mfp_mode(const sl_wifi_mfp_config_t *mfp_config);
/* Function used to get MFP mode */
sl_wifi_mfp_config_t sli_wifi_get_mfp_mode();
/* Function used to set the RTS threshold */
void sli_wifi_save_rts_threshold(uint16_t rts_threshold);
/* Function used to get RTS threshold */
sl_wifi_rts_threshold_t sli_get_rts_threshold();

sli_wifi_feature_frame_config_t sli_wifi_get_feature_frame_config(void);
void sli_wifi_save_pll_mode(const sl_wifi_pll_mode_t pll_mode);
void sli_wifi_save_power_chain(const sl_wifi_power_chain_t power_chain);

// Accessor for the scan results database head pointer
sli_scan_info_t **sli_get_scan_info_database(void);
uint32_t sl_wifi_host_elapsed_time(uint32_t starting_timestamp);

/* Converts firmware/NWP response (command + frame_status) to common sl_wifi_event_t. Common API for all devices. */
sl_wifi_event_t sli_wifi_convert_event_to_sl_wifi_event(uint32_t command, uint16_t frame_status);

sl_status_t sli_wifi_send_command_with_custom_desc(uint32_t command,
                                                   sli_wifi_command_type_t command_type,
                                                   const void *data,
                                                   uint32_t data_length,
                                                   sli_wifi_wait_period_t wait_period,
                                                   void *sdk_context,
                                                   void **response_buffer,
                                                   uint8_t custom_host_desc);

#ifndef __ZEPHYR__
/***************************************************************************/ /**
 * @brief
 *   Initializes new task register index for storing firmware status.
 *
 * @details
 *   This function sets up the task register index to store the firmware status in thread-specific storage.
 *   For all the threads at this index of the thread local array firmware status will be stored.
 *
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
 ******************************************************************************/
sl_status_t sli_fw_status_storage_index_init(void);
#endif

/***************************************************************************/ /**
 * @brief Cache boot feature bit map pushed from driver init.
 ******************************************************************************/
void sli_wifi_save_boot_feature_bit_map(uint32_t feature_bit_map);

/***************************************************************************/ /**
 * @brief Returns true if 11n-only mode was configured at Wi-Fi init.
 *
 * @details
 *   Checks whether @ref SL_WIFI_FEAT_DISABLE_11AX_SUPPORT was set in
 *   sl_wifi_device_configuration_t::boot_config::feature_bit_map during
 *   sl_wifi_init(). Neutral-less switch profile requires this prerequisite.
 *
 * @return true if 11n-only (11ax disabled) was configured at init, else false.
 ******************************************************************************/
bool sli_wifi_is_11n_only_mode_enabled(void);

#endif
