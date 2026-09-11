/***************************************************************************/ /**
 * @file
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
#include "sl_si91x_protocol_types.h"
#include "sl_si91x_constants.h"
#include "sl_si91x_driver.h"
#include "sl_wifi_constants.h"
#include "sl_wifi_credentials.h"
#include "sl_status.h"
#include "sl_constants.h"
#include "sl_wifi_types.h"
#include "sl_rsi_utility.h"
#include "sli_hal_si91x.h"
#include "sli_wifi_constants.h"
#include "cmsis_os2.h" // CMSIS RTOS2
#include "sl_common.h"
#include "sl_cmsis_utility.h"
#include "sl_si91x_types.h"
#include "sl_si91x_core_utilities.h"
#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_si91x_socket_constants.h"
#include "sl_si91x_socket_utility.h"
#endif
#include "sl_core.h"
#include <string.h>
#include "assert.h"
#include "sli_wifi_utility.h"
#include "sli_wifi_power_profile.h"
#include "sli_wifi.h"
#include "sli_queue_manager.h"
#include "sl_string.h"

static bool sli_si91x_tx_command_status = false;
/******************************************************
 *               Macro Declarations
 ******************************************************/
// Macro to check the status and return it if it's not SL_STATUS_OK
#define SLI_VERIFY_STATUS(s) \
  do {                       \
    if (s != SL_STATUS_OK)   \
      return s;              \
  } while (0)

/******************************************************
 *               Local Type Declarations
 ******************************************************/

/******************************************************
 *               Variable Declarations
 ******************************************************/

#ifdef SL_SI91X_SIDE_BAND_CRYPTO
extern osMutexId_t side_band_crypto_mutex;
#endif

static bool sli_si91x_packet_status = 0;

extern bool device_initialized;
void sl_debug_log(const char *format, ...);

extern sli_wifi_performance_profile_t performance_profile;

// NOTE: Boolean value determines whether firmware automatically closes the TCP socket in case of receiving termination from remote node or not.
static bool tcp_auto_close_enabled;

sl_wifi_system_performance_profile_t current_performance_profile = HIGH_PERFORMANCE;

static sl_wifi_system_boot_configuration_t saved_boot_configuration = { 0 };

/******************************************************
 *             Internal Function Declarations
 ******************************************************/

/******************************************************
 *               Function Declarations
 ******************************************************/

void sli_save_boot_configuration(const sl_wifi_system_boot_configuration_t *boot_configuration)
{
  memcpy(&saved_boot_configuration, boot_configuration, sizeof(sl_wifi_system_boot_configuration_t));
}

void sli_get_saved_boot_configuration(sl_wifi_system_boot_configuration_t *boot_configuration)
{
  memcpy(boot_configuration, &saved_boot_configuration, sizeof(sl_wifi_system_boot_configuration_t));
}

void sli_save_tcp_auto_close_choice(bool is_tcp_auto_close_enabled)
{
  tcp_auto_close_enabled = is_tcp_auto_close_enabled;
}

bool sli_is_tcp_auto_close_enabled()
{
  return tcp_auto_close_enabled;
}

sl_si91x_host_timestamp_t sl_si91x_host_get_timestamp(void)
{
  return osKernelGetTickCount();
}

// Calculate elapsed time from the given starting timestamp
sl_si91x_host_timestamp_t sl_si91x_host_elapsed_time(uint32_t starting_timestamp)
{
  return (osKernelGetTickCount() - starting_timestamp);
}

// Delay execution for a specified number of milliseconds using an OS-level delay
void sl_si91x_host_delay_ms(uint32_t delay_milliseconds)
{
  if (delay_milliseconds == osWaitForever) {
    osDelay(osWaitForever);
  } else {
    osDelay(SLI_SYSTEM_MS_TO_TICKS(delay_milliseconds));
  }
}

sl_status_t sl_si91x_host_power_cycle(void)
{
  sl_si91x_host_hold_in_reset();
  sl_si91x_host_delay_ms(100);

  sl_si91x_host_release_from_reset();
  sl_si91x_host_delay_ms(100);

  return SL_STATUS_OK;
}

void print_80211_packet(const uint8_t *packet, uint32_t packet_length, uint16_t max_payload_length)
{
  uint32_t dump_bytes    = 0;
  uint32_t header_length = MAC80211_HDR_MIN_LEN;

  header_length += (packet[0] & BIT(7)) ? 2 : 0;                           /* 2 bytes QoS control */
  header_length += ((packet[1] & BIT(0)) && (packet[1] & BIT(1))) ? 6 : 0; /* 6 byte Addr4 */

  sl_debug_log("%02x %02x | ", packet[0], packet[1]); /* FC */
  sl_debug_log("%02x %02x | ", packet[2], packet[3]); /* Dur */
  sl_debug_log("%02x:%02x:%02x:%02x:%02x:%02x | ",
               packet[4],
               packet[5],
               packet[6],
               packet[7],
               packet[8],
               packet[9]); /* Addr1/RA */
  sl_debug_log("%02x:%02x:%02x:%02x:%02x:%02x | ",
               packet[10],
               packet[11],
               packet[12],
               packet[13],
               packet[14],
               packet[15]); /* Addr2/NWP */
  sl_debug_log("%02x:%02x:%02x:%02x:%02x:%02x | ",
               packet[16],
               packet[17],
               packet[18],
               packet[19],
               packet[20],
               packet[21]);                             /* Addr3/DA */
  sl_debug_log("%02x %02x | ", packet[22], packet[23]); /* Seq control */
  if ((packet[1] & BIT(0)) && (packet[1] & BIT(1))) {   /* Addr4 */
    sl_debug_log("%02x:%02x:%02x:%02x:%02x:%02x | ",
                 packet[24],
                 packet[25],
                 packet[26],
                 packet[27],
                 packet[28],
                 packet[29]);
  }
  if (packet[0] & BIT(7)) {
    sl_debug_log("%02x %02x | ", packet[30], packet[31]); /* QoS control */
  }

  // Determine number of payload bytes to print
  dump_bytes = packet_length - header_length;
  dump_bytes = max_payload_length > dump_bytes ? dump_bytes : max_payload_length;

  for (uint32_t i = header_length; i < header_length + dump_bytes; i++) {
    sl_debug_log("%02x ", packet[i]);
  }

  sl_debug_log("|\r\n");
}

/* Function to get the current status of the NVM command progress
Returns true if an NVM command is in progress, false otherwise*/
bool sli_si91x_get_flash_command_status()
{
  return sli_si91x_packet_status;
}

void sli_si91x_update_flash_command_status(bool flag)
{
  sli_si91x_packet_status = flag;
}

bool sli_si91x_get_tx_command_status()
{
  return sli_si91x_tx_command_status;
}

void sli_si91x_update_tx_command_status(bool flag)
{
  sli_si91x_tx_command_status = flag;
}

/*  This function is used to update the power manager to see whether the device is ready for sleep or not.
 True indicates ready for sleep, and false indicates not ready for sleep.*/
bool sli_si91x_is_sdk_ok_to_sleep()
{
  return ((!sli_si91x_get_flash_command_status()) && (sl_si91x_is_device_initialized())
          && (!sli_si91x_get_tx_command_status()));
}

bool sl_si91x_is_device_initialized(void)
{
  return device_initialized;
}

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
// Implementation of SNI extension setting for embedded sockets
sl_status_t sli_si91x_set_sni_for_embedded_socket(const sli_si91x_tls_extension_info_t *sni_extension,
                                                  sli_si91x_sni_target_protocol_t sni_target_protocol)
{
  sl_status_t status     = SL_STATUS_OK;
  uint32_t packet_length = 0;

  if (sni_extension == NULL) {
    return SL_STATUS_NULL_POINTER;
  }

  // Validate SNI extension type
  if (sni_extension->type != SL_SI91X_TLS_EXTENSION_SNI_TYPE) {
    return SL_STATUS_INVALID_TYPE;
  }

  // Validate length before any memory operations
  if (sni_extension->length == 0
      || sizeof(sli_si91x_tls_extension_info_t) + sni_extension->length > SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA) {
    return SL_STATUS_WOULD_OVERFLOW;
  }

  sli_si91x_sni_for_embedded_socket_request_t *request = (sli_si91x_sni_for_embedded_socket_request_t *)SLI_MALLOC(
    sizeof(sli_si91x_sni_for_embedded_socket_request_t) + SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA);
  SLI_VERIFY_MALLOC_AND_RETURN(request);

  memset(request, 0, sizeof(sli_si91x_sni_for_embedded_socket_request_t) + SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA);

  request->protocol = (uint16_t)sni_target_protocol;

  request->offset = sizeof(sli_si91x_tls_extension_info_t);
  memcpy(&request->tls_extension_data, sni_extension, SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA);
  request->offset += sni_extension->length;
  packet_length = sizeof(sli_si91x_sni_for_embedded_socket_request_t) + SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA;

  status = sli_wifi_send_command(SLI_WIFI_REQ_SET_SNI_EMBEDDED,
                                 SLI_SI91X_NETWORK_CMD,
                                 request,
                                 packet_length,
                                 SLI_WIFI_WAIT_FOR_RESPONSE(SLI_WIFI_RSP_SET_SNI_EMBEDDED_WAIT_TIME),
                                 NULL,
                                 NULL);
  SLI_FREE(request);

  return status;
}

// Helper function to configure SNI using either extension or hostname
sl_status_t sli_configure_sni(const sli_si91x_tls_extension_info_t *sni_extension,
                              const uint8_t *host_name,
                              sli_si91x_sni_target_protocol_t sni_target_protocol)
{
  if (sni_extension != NULL) {
    return sli_si91x_set_sni_for_embedded_socket(sni_extension, sni_target_protocol);
  }

  if (host_name != NULL && host_name[0] != '\0') {
    size_t host_name_length = sl_strlen((const char *)host_name);
    sl_status_t status      = SL_STATUS_OK;

    // Validate length before allocation
    if (host_name_length > SLI_SI91X_MAX_SIZE_OF_EXTENSION_DATA) {
      return SL_STATUS_SI91X_MEMORY_ERROR;
    }

    sli_si91x_tls_extension_info_t *tls_sni =
      (sli_si91x_tls_extension_info_t *)SLI_MALLOC(sizeof(sli_si91x_tls_extension_info_t) + host_name_length);
    if (tls_sni == NULL) {
      return SL_STATUS_ALLOCATION_FAILED;
    }

    tls_sni->type   = SL_SI91X_TLS_EXTENSION_SNI_TYPE;
    tls_sni->length = (uint16_t)host_name_length;
    memcpy(tls_sni->value, host_name, tls_sni->length);

    status = sli_si91x_set_sni_for_embedded_socket(tls_sni, sni_target_protocol);
    SLI_FREE(tls_sni);
    return status;
  }

  return SL_STATUS_OK;
}
#endif

// Weak fallback so applications that drop the deprecated buffer components
// (sl_si91x_basic_buffers / sl_si91x_mem_pool_buffers[_with_quota]) still
// resolve this public API symbol. When any deprecated component is selected,
// its strong definition preempts this one. The explicit NULL / bounds check
// preserves the legacy contract (return NULL when offset >= buffer->length,
// including length == 0); sli_wifi_host_get_buffer_data uses a looser check.
SL_WEAK void *sl_si91x_host_get_buffer_data(sl_wifi_buffer_t *buffer, uint16_t offset, uint16_t *data_length)
{
  if ((buffer == NULL) || (offset >= buffer->length)) {
    return NULL;
  }
  return sli_wifi_host_get_buffer_data(buffer, offset, data_length);
}
