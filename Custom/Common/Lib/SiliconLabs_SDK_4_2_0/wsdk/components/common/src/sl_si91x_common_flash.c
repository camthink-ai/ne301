/***************************************************************************/ /**
 * @file
 * @brief Si91x common flash command APIs.
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
#include "sl_si91x_common_flash.h"
#include <stddef.h>
#include <string.h>
#include "sl_common.h"
#include "sl_types.h"
#include "sli_buffer_manager.h"
#include "sli_constants.h"
#include "sli_utility.h"

#ifdef SLI_SI91X_MCU_INTERFACE

#define SL_SI91X_COMMON_FLASH_WRITE_COMMAND  6
#define SL_SI91X_COMMON_FLASH_READ_COMMAND   8
#define SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE 1400
#define SL_SI91X_COMMON_FLASH_SECTOR_SIZE    4096

#ifndef SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF
#define SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF 1
#endif

#ifndef SL_TX_ADDITIONAL_WAIT_TIME
#define SL_TX_ADDITIONAL_WAIT_TIME 0
#endif

#define SL_SI91X_COMMON_FLASH_INTERNAL_COMMANDS_BASE_VALUE 1000
#define SL_SI91X_COMMON_FLASH_DEFAULT_TIMEOUT              (30000 + SL_TX_ADDITIONAL_WAIT_TIME)
#define SL_SI91X_COMMON_FLASH_TA_M4_COMMANDS_WAIT_TIME                                         \
  ((SL_SI91X_COMMON_FLASH_INTERNAL_COMMANDS_BASE_VALUE * SL_WIFI_INTERNAL_COMMANDS_TIMEOUT_SF) \
   + SL_SI91X_COMMON_FLASH_DEFAULT_TIMEOUT)

typedef struct {
  uint8_t sub_cmd;
  uint32_t addr;
  uint16_t input_buffer_length;
  uint8_t flash_sector_erase_enable;
  uint8_t input_data[SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE];
} SL_ATTRIBUTE_PACKED sl_si91x_common_flash_write_request_t;

typedef struct {
  uint8_t sub_cmd;
  uint32_t nwp_address;
  uint16_t output_buffer_length;
} SL_ATTRIBUTE_PACKED sl_si91x_common_flash_read_request_t;

sl_status_t sl_si91x_command_to_write_common_flash(uint32_t write_address,
                                                   const uint8_t *write_data,
                                                   uint16_t write_data_length,
                                                   uint8_t flash_sector_erase_enable)
{
  if (write_data_length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status                                  = SL_STATUS_OK;
  sl_si91x_common_flash_write_request_t flash_request = { 0 };
  uint32_t send_size                                  = 0;
  uint16_t remaining_length                           = write_data_length;

  if (flash_sector_erase_enable == 1) {
    while (remaining_length > 0) {
      size_t chunk_size = (remaining_length < SL_SI91X_COMMON_FLASH_SECTOR_SIZE) ? remaining_length
                                                                                 : SL_SI91X_COMMON_FLASH_SECTOR_SIZE;

      memset(&flash_request, 0, sizeof(sl_si91x_common_flash_write_request_t));
      flash_request.sub_cmd                   = SL_SI91X_COMMON_FLASH_WRITE_COMMAND;
      flash_request.addr                      = write_address;
      flash_request.input_buffer_length       = (uint16_t)chunk_size;
      flash_request.flash_sector_erase_enable = flash_sector_erase_enable;

      send_size = sizeof(sl_si91x_common_flash_write_request_t);

      status = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                     SLI_WIFI_COMMON_CMD,
                                     &flash_request,
                                     send_size,
                                     SL_SI91X_COMMON_FLASH_TA_M4_COMMANDS_WAIT_TIME,
                                     NULL,
                                     NULL);
      if (status != SL_STATUS_OK) {
        return status;
      }

      write_address += chunk_size;
      remaining_length -= chunk_size;
    }
  } else {
    if (write_data == NULL) {
      return SL_STATUS_INVALID_PARAMETER;
    }

    while (write_data_length > 0) {
      size_t chunk_size = (write_data_length < SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE)
                            ? write_data_length
                            : SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE;

      memset(&flash_request, 0, sizeof(sl_si91x_common_flash_write_request_t));
      flash_request.sub_cmd                   = SL_SI91X_COMMON_FLASH_WRITE_COMMAND;
      flash_request.addr                      = write_address;
      flash_request.input_buffer_length       = (uint16_t)chunk_size;
      flash_request.flash_sector_erase_enable = flash_sector_erase_enable;

      memcpy(&flash_request.input_data, write_data, chunk_size);

      send_size = sizeof(sl_si91x_common_flash_write_request_t) - SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE + chunk_size;
      status    = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                     SLI_WIFI_COMMON_CMD,
                                     &flash_request,
                                     send_size,
                                     SL_SI91X_COMMON_FLASH_TA_M4_COMMANDS_WAIT_TIME,
                                     NULL,
                                     NULL);
      if (status != SL_STATUS_OK) {
        return status;
      }

      write_address += chunk_size;
      write_data += chunk_size;
      write_data_length -= chunk_size;
    }
  }

  return status;
}

sl_status_t sl_si91x_command_to_read_common_flash(uint32_t read_address, size_t length, uint8_t *output_buffer)
{
  if (output_buffer == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  if (length == 0) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  sl_status_t status                    = SL_STATUS_OK;
  sl_wifi_buffer_t *buffer              = NULL;
  const sl_wifi_system_packet_t *packet = NULL;

  while (length > 0) {
    size_t chunk_size = (length < SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE) ? length : SL_SI91X_COMMON_FLASH_MAX_CHUNK_SIZE;

    sl_si91x_common_flash_read_request_t flash_read_request = { 0 };
    flash_read_request.sub_cmd                              = SL_SI91X_COMMON_FLASH_READ_COMMAND;
    flash_read_request.nwp_address                          = read_address;
    flash_read_request.output_buffer_length                 = (uint16_t)chunk_size;

    status = sli_wifi_send_command(SLI_COMMON_REQ_TA_M4_COMMANDS,
                                   SLI_WIFI_COMMON_CMD,
                                   &flash_read_request,
                                   sizeof(sl_si91x_common_flash_read_request_t),
                                   SLI_WIFI_WAIT_FOR_RESPONSE(SL_SI91X_COMMON_FLASH_TA_M4_COMMANDS_WAIT_TIME),
                                   NULL,
                                   (void **)&buffer);
    if (status != SL_STATUS_OK) {
      if (buffer != NULL) {
        sli_buffer_manager_free_buffer((sli_buffer_t)buffer);
      }
      return status;
    }

    packet = sli_wifi_host_get_buffer_data(buffer, 0, NULL);
    if (packet == NULL) {
      sli_buffer_manager_free_buffer((sli_buffer_t)buffer);
      return SL_STATUS_FAIL;
    }

    memcpy(output_buffer, packet->data, packet->length);
    sli_buffer_manager_free_buffer((sli_buffer_t)buffer);

    read_address += chunk_size;
    output_buffer += chunk_size;
    length -= chunk_size;
    buffer = NULL;
  }

  return status;
}

#endif // SLI_SI91X_MCU_INTERFACE
