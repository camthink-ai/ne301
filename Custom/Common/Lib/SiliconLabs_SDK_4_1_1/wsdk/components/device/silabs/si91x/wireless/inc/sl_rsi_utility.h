/***************************************************************************/ /**
 * @file
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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
#pragma once

#ifndef _SL_RSI_UTILITY_H_
#define _SL_RSI_UTILITY_H_

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "sl_status.h"
#include "sl_constants.h"
#include "sl_wifi_constants.h"
#include "sl_si91x_host_interface.h"
#include "sl_si91x_protocol_types.h"
#include "sl_utility.h"
#include "sl_si91x_driver.h"
#include "sl_wifi_device.h"
#include "sl_si91x_types.h"
#include "sli_wifi_types.h"

//! @cond Doxygen_Suppress

/// Internal structure for representing a TLS extension
typedef struct {
  uint16_t type;   ///< Specifies the TLS extension type.
  uint16_t length; ///< Length of the value[] field.
  uint8_t value[]; ///< Data corresponding to the specified extension type.
} sli_si91x_tls_extension_info_t;

/// Low Transmit Power Threshold for Wi-Fi.
#define SLI_SI91X_LOW_TRANSMIT_POWER_THRESHOLD 6

/// Medium Transmit Power Threshold for Wi-Fi.
#define SLI_SI91X_MEDIUM_TRANSMIT_POWER_THRESHOLD 4

typedef uint32_t sl_si91x_host_timestamp_t;

/* Indicates the current performance profile */
extern sl_wifi_system_performance_profile_t current_performance_profile;

/* Function used to set whether tcp auto close is enabled or disabled */
void sli_save_tcp_auto_close_choice(bool is_tcp_auto_close_enabled);

/* Function used to check whether tcp auto close is enabled or disabled */
bool sli_is_tcp_auto_close_enabled();

/* Function used to get maximum transmission power */
sl_wifi_max_tx_power_t sli_get_max_tx_power();

/* Function used to update the boot configuration */
void sli_save_boot_configuration(const sl_wifi_system_boot_configuration_t *boot_configuration);

/* Function used to retrieve the boot configuration */
void sli_get_saved_boot_configuration(sl_wifi_system_boot_configuration_t *boot_configuration);
/***************************************************************************/ /**
 * @brief
 *   Get the Efuse Data content from flash.
 * @pre Pre-conditions:
 * - 
 *   @ref sl_wifi_init should be called before this API.
 * @param[out] efuse_data
 *   @ref sli_wifi_efuse_data_t object that contains the Manufacturing software version.
 *   efuse_data_type which holds the type of efuse data to be read.
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 * @note
 *   This API is not supported in the current release.
 ******************************************************************************/
sl_status_t sli_si91x_get_flash_efuse_data(sli_wifi_efuse_data_t *efuse_data, uint8_t efuse_data_type);

/***************************************************************************/ /**
 * @brief
 *   Get the Efuse Data content from driver context.
 * @pre Pre-conditions:
 * - 
 *   @ref sl_wifi_init should be called before this API.
 * @param[out] efuse_data
 *   @ref sli_wifi_efuse_data_t object that contains the Manufacturing software version.
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 ******************************************************************************/
void sli_si91x_get_efuse_data(sli_wifi_efuse_data_t *efuse_data);

/***************************************************************************/ /**
 * @brief
 *   Set the Efuse Data content in driver context.
 * @pre Pre-conditions:
 * - 
 *   @ref sl_wifi_init should be called before this API.
 * @param[out] efuse_data
 *   @ref sli_wifi_efuse_data_t object that contains the Manufacturing software version.
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 ******************************************************************************/
void sli_si91x_set_efuse_data(const sli_wifi_efuse_data_t *efuse_data);

/**
 * A utility function to convert dBm value to si91x specific power value
 * @param wifi_max_tx_power which holds the join power value with dBm as units.
 * @return si91x power level
 */
static inline uint8_t sli_convert_dbm_to_si91x_power_level(sl_wifi_max_tx_power_t wifi_max_tx_power)
{
  uint8_t power_value_in_dBm = wifi_max_tx_power.join_tx_power;
  if (power_value_in_dBm >= SLI_SI91X_LOW_TRANSMIT_POWER_THRESHOLD) {
    return SLI_SI91X_LOW_POWER_LEVEL;
  } else if (power_value_in_dBm >= SLI_SI91X_MEDIUM_TRANSMIT_POWER_THRESHOLD) {
    return SLI_SI91X_MEDIUM_POWER_LEVEL;
  } else {
    return SLI_SI91X_HIGH_POWER_LEVEL;
  }
}

//! @endcond

/** \addtogroup EXTERNAL_HOST_INTERFACE_FUNCTIONS
 * \ingroup EXTERNAL_HOST_INTERFACE
 */

//! @cond Doxygen_Suppress

// These aren't host APIs. These should go into a wifi bus API header
/* Function used to set buffer pointer to point to specified memory address */
sl_status_t sl_si91x_bus_read_memory(uint32_t addr, uint16_t length, const uint8_t *buffer);

/* Function used to set specified memory address to point to buffer */
sl_status_t sl_si91x_bus_write_memory(uint32_t addr, uint16_t length, const uint8_t *buffer);

/* Function used to send boot instruction (e.g. register read/write) for NCP firmware upgrade */
sl_status_t sli_si91x_boot_instruction(uint8_t type, uint16_t *data);

/*==============================================*/
/**
 * @brief       Send chunk of data from Host to Si91x using SPI slave mode.
 * @param[in]   data_length   -  Actual data length to send 
 * @param[in]   buffer        - Pointer to data  
 * @return      sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.  
 *
 */
sl_status_t sli_si91x_bus_write_slave(uint32_t data_length, const uint8_t *buffer);

/* Function used to read contents of the register */
sl_status_t sli_si91x_bus_read_register(uint8_t address, uint8_t register_size, uint16_t *output);

/* Function used to write data into register */
sl_status_t sli_si91x_bus_write_register(uint8_t address, uint8_t register_size, uint16_t data);

/* Function used to read frame */
sl_status_t sli_si91x_bus_read_frame(sl_wifi_buffer_t **buffer);

/* Function used to write frames */
sl_status_t sli_si91x_bus_write_frame(sl_wifi_system_packet_t *packet,
                                      const uint8_t *payloadparam,
                                      uint16_t size_param);

/* Function used to check the bus availability */
sl_status_t sl_si91x_bus_init(void);

/* Function used to release bus-owned buffers; must be called before buffer manager deinit */
sl_status_t sl_si91x_bus_deinit(void);

/* Function used to check the bus availability */
sl_status_t sli_si91x_bus_rx_irq_handler(void);

/* Function used to check the bus availability */
void sli_si91x_bus_rx_done_handler(void);

sl_status_t sl_si91x_host_power_cycle(void);

/***************************************************************************/ /**
 * @brief
 *   The @ref sli_si91x_bus_enable_high_speed() should be called only if the SPI clock frequency is more than 25 MHz.
 * @note
 *   SPI initialization has to be done in low-speed mode only.
 *   After device SPI is configured, this API is used for high-speed mode (>25 MHz).
 *   In addition to this API, the following API sl_si91x_host_enable_high_speed_bus has to be ported by the user to implement the host clock switch.
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 ******************************************************************************/
sl_status_t sli_si91x_bus_enable_high_speed();

/* Function used to read the interrupt register */
sl_status_t sli_si91x_bus_read_interrupt_status(uint16_t *interrupt_status);

/* Function used to block specified interrupts */
sl_status_t sli_si91x_bus_set_interrupt_mask(uint32_t mask);

/* Function used to initialize SPI interface on ULP wakeup */
void sli_si91x_ulp_wakeup_init(void);

bool sli_si91x_get_flash_command_status();

void sli_si91x_update_flash_command_status(bool flag);

bool sli_si91x_is_sdk_ok_to_sleep();
//! @endcond

/**
* @addtogroup EXTERNAL_HOST_INTERFACE_FUNCTIONS
* @{ 
*/

/***************************************************************************/
/**
 * @brief
 *   Delay execution for a specified number of milliseconds.
 * 
 * @details
 *   This function introduces a delay for the specified amount of time in milliseconds. It uses the underlying OS 
 *   delay function (`osDelay`) to yield the CPU, allowing other tasks to execute during the delay period. This 
 *   ensures that the delay does not block the execution flow.
 * 
 * @param[in] delay_milliseconds 
 *   The time delay in milliseconds.
 *****************************************************************************/
void sl_si91x_host_delay_ms(uint32_t delay_milliseconds);

/**
 * @brief
 *   Retrieves the current timestamp.
 * 
 * @details
 *   This function retrieves the current timestamp from the host system. The timestamp can be used for various purposes such as logging, time measurements, and synchronization.
 * 
 * @return
 *   The current timestamp of type sl_si91x_host_timestamp_t.
 */
sl_si91x_host_timestamp_t sl_si91x_host_get_timestamp(void);

/**
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
 *   The elapsed time in milliseconds of type sl_si91x_host_timestamp_t.
 */
sl_si91x_host_timestamp_t sl_si91x_host_elapsed_time(uint32_t starting_timestamp);

/**
 * @brief
 *   Returns a pointer to the data in a Wi-Fi buffer at the given offset.
 *
 * @details
 *   Provides access to the raw data in an sl_wifi_buffer_t, e.g. when
 *   processing received frames in a custom sl_si91x_host_process_data_frame.
 *
 * @param[in] buffer
 *   Pointer to the sl_wifi_buffer_t structure.
 * @param[in] offset
 *   Byte offset from the start of the buffer data.
 * @param[out] data_length
 *   If non-NULL, receives the number of bytes available from offset to end of buffer.
 *
 * @return
 *   Pointer to the data at the given offset, or NULL if offset is beyond buffer length.
 */
void *sl_si91x_host_get_buffer_data(sl_wifi_buffer_t *buffer, uint16_t offset, uint16_t *data_length);

/**
 * @brief
 *   Checks if the device is initialized.
 * 
 * @details
 *   This function verifies whether the device has been properly initialized. It is typically used to ensure that the device is ready for operation before performing any further actions.
 * 
 * @return
 *   Returns `true` if the device is initialized, `false` otherwise.
 */
bool sl_si91x_is_device_initialized(void);

/***************************************************************************/ /**
 * @brief
 *   Retrieves the current status of the TX command.
 *
 * @details
 *   This function returns the current status flag indicating whether a TX (transmit) command is in progress or completed.
 *   It is typically used to check if the system is ready to send a new TX command or if a previous command is still pending.
 *
 * @return
 *   Returns `true` if a TX command is in progress, `false` otherwise.
 ******************************************************************************/
bool sli_si91x_get_tx_command_status(void);

/***************************************************************************/ /**
 * @brief
 *   Updates the status of the TX command.
 *
 * @details
 *   This function sets the status flag for the TX (transmit) command. It can be used to mark the TX command as in progress or completed.
 *   Typically, this is called internally when starting or finishing a TX command to keep track of the command state.
 *
 * @param[in] flag
 *   Set to `true` to indicate a TX command is in progress, or `false` to indicate it is completed.
 ******************************************************************************/
void sli_si91x_update_tx_command_status(bool flag);

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
/**
 * @brief 
 *    Configures the Server Name Indication (SNI) extension for a socket.
 *
 *  @details
 *    This function sets up the SNI extension, which is used in TLS communication
 *    to specify the hostname of the server the client intends to connect to. It prepares
 *    the necessary request structure and initiates the configuration process for the
 *    embedded socket.
 *
 * @param[in] sni_extension 
 *    Pointer to the SNI extension data of type `sl_si91x_socket_type_length_value_t`.
 *    This structure contains the type, length, and value of the SNI extension.
 * @param[in] sni_target_protocol
 *    SNI target type (e.g., HTTPS or MQTT).
 *
 * @return sl_status_t
 *    - SL_STATUS_OK: Operation completed successfully.
 *    - SL_STATUS_WOULD_OVERFLOW: The SNI extension size exceeds the allowed limit.
 *    - Other error codes: Refer to [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) 
 *      and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details. 
 */
sl_status_t sli_si91x_set_sni_for_embedded_socket(const sli_si91x_tls_extension_info_t *sni_extension,
                                                  sli_si91x_sni_target_protocol_t sni_target_protocol);

/***************************************************************************/ /**
 * @brief
 *   Configure SNI (Server Name Indication) extension for TLS connection.
 *
 * @details
 *   This function configures the SNI extension for embedded socket TLS connections.
 *   It can either use a pre-configured SNI extension structure or create one from
 *   a hostname string. Callers using public socket types should convert to internal
 *   sli_si91x_tls_extension_info_t before calling this function.
 *
 * @param[in] sni_extension
 *   Pointer to internal SNI extension structure. Can be NULL if using hostname.
 *
 * @param[in] host_name
 *   Hostname string to create SNI extension from. Can be NULL if using sni_extension.
 *
 * @param[in] sni_target_protocol
 *   Specifies the protocol type (HTTP/HTTPS/MQTT) for which SNI is configured.
 *
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 *   - SL_STATUS_OK: SNI configured successfully
 *   - SL_STATUS_ALLOCATION_FAILED: Memory allocation failed
 *   - Other error codes from underlying API calls
 ******************************************************************************/
sl_status_t sli_configure_sni(const sli_si91x_tls_extension_info_t *sni_extension,
                              const uint8_t *host_name,
                              sli_si91x_sni_target_protocol_t sni_target_protocol);
#endif

/** @} */

#endif // _SL_RSI_UTILITY_H_
