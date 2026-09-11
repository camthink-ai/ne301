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
#include <stdint.h>

#define SLI_WIFI_WAIT_ON_THREAD_ID 0 ///< Wait on the calling thread's event flags (used by synchronous commands).
#define SLI_WIFI_WAIT_ON_EVENT_ID  1 ///< Wait on the packet-type shared event flags (used by socket/select paths).

#define SLI_WIFI_HEADER_SIZE               16
#define SLI_WIFI_TRANSMIT_TEST_HEADER_SIZE 4

/***************************************************************************/ /**
 * @brief
 *   A utility function to extract firmware status from RX packet.
 *   The extracted firmware status can be given to sli_wifi_convert_and_save_firmware_status() to get sl_status equivalent.
 * @param[in] packet
 *   Packet that contains the frame status which needs to be extracted.
 * @return
 *   Frame status (uint16_t)
 ******************************************************************************/
uint16_t sli_wifi_get_wifi_frame_status(const sl_wifi_system_packet_t *packet);

/***************************************************************************/ /**
 * @brief
 *   Retrieve data from a buffer with a specified offset.
 *
 * @details
 *   This function is designed to retrieve data from a buffer at a specified offset.
 *
 * @param[in]  buffer
 *   A pointer to an [sl_wifi_buffer_t](../wiseconnect-api-reference-guide-wi-fi/sl-wifi-buffer-t) structure from which data is to be retrieved.
 * 
 * @param[in]  offset
 *   Offset from the start of the buffer where data retrieval begins.
 * 
 * @param[out] data_length
 *   Pointer to a variable where the remaining data length from the offset will be stored.
 *
 * @return
 *   Pointer to the data at the specified offset within the buffer.
 *
 ******************************************************************************/
void *sli_wifi_host_get_buffer_data(void *buffer, uint16_t offset, uint16_t *data_length);

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
/* Function used to set maximum transmission power to default value(31 dBm) */
void sli_wifi_reset_max_tx_power();

/* Function used to set wifi rate to default value of 1 Mbps */
void sli_wifi_reset_sl_wifi_rate();

/* Function used to set whether card ready is required or not */
void sli_wifi_set_card_ready_required(bool card_ready_required);
/***************************************************************************/ /**
 * @brief
 *   Get the current Opermode of the module.
 * @return
 *   sl_wifi_operation_mode_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 ******************************************************************************/
void sli_wifi_set_opermode(sl_wifi_operation_mode_t mode);

sl_wifi_operation_mode_t sli_wifi_get_opermode(void);

/**
 * @brief Get the VAP ID from the operation mode and packet descriptor
 * 
 * This function determines the VAP ID based on the current operation mode and,
 * in concurrent mode, the packet descriptor byte 7.
 * 
 * @param rx_packet Pointer to the received packet structure. Can be NULL for non-concurrent modes.
 *                  In concurrent mode, if NULL, defaults to AP VAP ID.
 * @return uint8_t The VAP ID (SL_WIFI_CLIENT_VAP_ID or SL_WIFI_AP_VAP_ID)
 */
uint8_t sli_wifi_get_vap_id_from_operation_mode(const sl_wifi_system_packet_t *rx_packet);

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

/* Function used to set the bluetooth performance profile */
void sli_save_bt_current_performance_profile(const sl_bt_performance_profile_t *profile);
bool sli_wifi_get_card_ready_required();

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

sl_status_t sli_wifi_send_command(uint32_t command,
                                  sli_wifi_command_type_t command_type,
                                  const void *data,
                                  uint32_t data_length,
                                  sli_wifi_wait_period_t wait_period,
                                  void *sdk_context,
                                  void **data_buffer);

sl_status_t sli_wifi_receive_response_buffer(uint16_t command_packet_type,
                                             uint16_t packet_id,
                                             sli_wifi_wait_period_t wait_time,
                                             uint8_t wait_type,
                                             void **response_packet);

sl_status_t sli_wifi_async_send_command(uint32_t command,
                                        sli_wifi_command_type_t command_type,
                                        const void *data,
                                        uint32_t data_length,
                                        void *custom_desc);

/***************************************************************************/ /**
 * @brief
 *   Block until a synchronous command-engine response is dequeued.
 *
 * @details
 *   Waits for an RX response on the sync-response queue registered for
 *   @p command_packet_type, then removes and returns the metadata node whose
 *   @c tx_info.packet_id matches @p packet_id. Spurious wakeups (event set but
 *   no matching node) are retried until @p wait_period_ms elapses.
 *
 *   Two wait mechanisms are supported via @p wait_type:
 *   - @ref SLI_WIFI_WAIT_ON_THREAD_ID: blocks on the calling thread's CMSIS
 *     thread flags. Used when the thread that sent the command waits for its
 *     own response.
 *   - @ref SLI_WIFI_WAIT_ON_EVENT_ID: blocks on the per-packet-type shared
 *     @c osEventFlags handle (@c sync_response_event_id). Used when the waiter
 *     may differ from the sender (for example socket read/select).
 *
 *   On success the caller owns @p metadata_response and must free it (and any
 *   attached @c tx_info.data_packet) when no longer needed. Higher-level callers
 *   typically use sli_wifi_receive_response_buffer() instead of calling this
 *   function directly.
 *
 * @param[in] command_packet_type
 *   Command-engine packet type used to look up the sync-response queue and events.
 * @param[in] packet_id
 *   TX packet identifier to match in the response queue. Use @c 0 when the
 *   response is not correlated by packet ID (for example async socket reads).
 * @param[in] wait_period_ms
 *   Maximum time to wait for a matching response, in milliseconds.
 * @param[in] wait_type
 *   Wait mechanism: @ref SLI_WIFI_WAIT_ON_THREAD_ID or @ref SLI_WIFI_WAIT_ON_EVENT_ID.
 * @param[out] metadata_response
 *   On @c SL_STATUS_OK, set to the dequeued command-engine metadata. Unchanged on error.
 *
 * @return
 *   @c SL_STATUS_OK on success.
 *   @c SL_STATUS_TIMEOUT if no matching response arrives within @p wait_period_ms.
 *   @c SL_STATUS_INVALID_PARAMETER if @p metadata_response is NULL or @p wait_type is invalid.
 *   @c SL_STATUS_INVALID_CONFIGURATION if @ref SLI_WIFI_WAIT_ON_EVENT_ID is requested but
 *   the packet type has no @c sync_response_event_id configured.
 *   @c SL_STATUS_FAIL on an unexpected RTOS wait error.
 *   Other queue-manager errors may be propagated on the thread-ID wait path.
 ******************************************************************************/
sl_status_t sli_wifi_driver_wait_for_response_packet(uint16_t command_packet_type,
                                                     uint16_t packet_id,
                                                     uint32_t wait_period_ms,
                                                     uint8_t wait_type,
                                                     sli_command_engine_metadata_t **metadata_response);
/**
 * @brief Internal function to send a command packet to Command Engine
 *
 * @param command The command to be sent.
 * @param command_type The type of the command.
 * @param packet Pointer to the sl_wifi_system_packet_t containing the command data.
 * @param wait_period The wait period for the command response.
 * @param sdk_context Pointer to the SDK context.
 * @param response_buffer Pointer to the buffer where the response will be stored.
 * @return sl_status_t Status of the operation.
 */
sl_status_t sli_wifi_send_command_packet(uint32_t command,
                                         sli_wifi_command_type_t command_type,
                                         sl_wifi_system_packet_t *packet,
                                         sli_wifi_wait_period_t wait_period,
                                         void *sdk_context,
                                         void **response_buffer);

/***************************************************************************/ /**
 * @brief
 *   Calculates the elapsed time since a given starting timestamp.
 * 
 * @details
 *   This function calculates the difference between the current timestamp and a provided starting timestamp. It is useful for measuring the time elapsed during operations.
 * 
 * @param[in] starting_timestamp
 *   The starting timestamp from which the elapsed time is calculated.
 * 
 * @return
 *   The elapsed time in milliseconds of type uint32_t.
 ******************************************************************************/
uint32_t sli_wifi_host_elapsed_time(uint32_t starting_timestamp);

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

#endif
