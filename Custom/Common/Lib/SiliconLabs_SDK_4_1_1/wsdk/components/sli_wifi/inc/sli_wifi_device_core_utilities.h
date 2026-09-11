/***************************************************************************/ /**
 * @file
 * @brief Common Wi-Fi core utilities for firmware status handling
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

#include "sli_wifi_types.h"
#include "sl_wifi_device.h"
#include "sl_additional_status.h"
#ifndef __ZEPHYR__
#include "sli_cmsis_os2_ext_task_register.h"

/// External variable representing the index of the thread local array at which the firmware status will be stored.
extern sli_task_register_id_t sli_fw_status_storage_index;
#endif

/// Invalid index for firmware status storage. 0xFF is used as a sentinel value because
/// sli_task_register_id_t is uint8_t; 0xFF is outside the valid range of task register indices.
#define SLI_FW_STATUS_STORAGE_INVALID_INDEX 0xFF

/***************************************************************************/ /**
 * @brief 
 *   Retrieves the saved thread-specific firmware status value.
 *  
 * @details
 *   This function fetches the firmware status value that is specific to the current thread.
 * 
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
 ******************************************************************************/
static inline sl_status_t sli_wifi_get_saved_firmware_status(void)
{
  sl_status_t status = SL_STATUS_FAIL;
#ifndef __ZEPHYR__
  sli_osTaskRegisterGetValue(NULL, sli_fw_status_storage_index, &status);
#endif
  return status;
}

/******************************************************************************
 * @brief
 * 	A utility function that store the firmware status code in thread specific storage.
 * @param[in] converted_firmware_status
 *	Firmware status code that needs to be saved.
 *****************************************************************************/
static inline void sli_wifi_save_firmware_status(sl_status_t converted_firmware_status)
{
#ifndef __ZEPHYR__
  sli_osTaskRegisterSetValue(NULL, sli_fw_status_storage_index, converted_firmware_status);
#endif
}

/******************************************************************************
 * @brief
 *   A utility function that converts frame status sent by firmware to sl_status_t and stores in thread local storage of caller thread.
 * @param[in] firmware_status
 *   firmware_status that needs to be converted to sl_status_t.
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 *****************************************************************************/
static inline sl_status_t sli_wifi_convert_and_save_firmware_status(uint16_t firmware_status)
{
  sl_status_t converted_firmware_status = (firmware_status == SL_STATUS_OK) ? SL_STATUS_OK
                                                                            : (firmware_status | (1U << 16));
#ifndef __ZEPHYR__
  sli_wifi_save_firmware_status(converted_firmware_status);
#endif
  return converted_firmware_status;
}
