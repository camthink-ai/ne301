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
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sl_status.h"

/***************************************************************************/ /**
 * @brief
 *   Allows the Network Processor (NWP) to write content to the common flash from M4.
 *
 * @details
 *   This function enables the NWP to write data to the common flash memory from
 *   the M4 core. It is a blocking API.
 *
 * @param[in] write_address
 *   The address in the common flash memory where the write operation should begin.
 *   - For the M4 region, the write address should start from 0x8000000. Possible
 *     values range from the M4 image end address to the M4 region end address.
 *   - For the NWP region, the write address should range from 0 to (20K-1).
 *   - For sector erase, it should be multiples of 4K.
 *
 * @param[in] write_data
 *   Pointer to the data to be written.
 *
 * @param[in] write_data_length
 *   The total length of the data, which should be multiples of 4K for sector erase.
 *
 * @param[in] flash_sector_erase_enable
 *   Enable or disable sector erase.
 *   - 1: Erases multiples of 4 KB of data.
 *   - 0: Disable, allows writing data onto flash.
 *
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status)
 *   and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes)
 *   for details.
 ******************************************************************************/
sl_status_t sl_si91x_command_to_write_common_flash(uint32_t write_address,
                                                   const uint8_t *write_data,
                                                   uint16_t write_data_length,
                                                   uint8_t flash_sector_erase_enable);

/***************************************************************************/ /**
 * @brief
 *   Sends a command to read data from the NWP flash memory of the SI91x wireless device.
 *
 * @details
 *   This function sends a command to the SI91x wireless device to read from the
 *   NWP flash memory at the specified address. The read data is stored in the
 *   provided output buffer.
 *
 *   This is a blocking API.
 *
 * @param[in] read_address
 *   The address in the NWP flash memory to read from. The address should range
 *   from 0 to (20K-1).
 *
 * @param[in] length
 *   The number of bytes to read from the NWP flash memory.
 *
 * @param[out] output_buffer
 *   Pointer to the buffer where the read data will be stored.
 *
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status)
 *   and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes)
 *   for details.
 ******************************************************************************/
sl_status_t sl_si91x_command_to_read_common_flash(uint32_t read_address, size_t length, uint8_t *output_buffer);
