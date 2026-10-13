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
#pragma once
#include "sli_wifi_device_core_utilities.h"
#include "sl_si91x_types.h"

/** \addtogroup SI91X_DRIVER_FUNCTIONS 
 * \ingroup SL_SI91X_API
 * @{ */
/***************************************************************************/ /**
 * @brief 
 *   Retrieves the saved thread-specific firmware status value.
 *  
 * @details
 *   This function fetches the firmware status value that is specific to the current thread.
 *   On Zephyr builds, this function returns SL_STATUS_FAIL as task-register access is not available.
 *
 * @note
 *   Moving forward, this API will be deprecated. Instead, use the [sl_wifi_get_saved_firmware_status()](../wiseconnect-api-reference-guide-wi-fi/wifi-callback-framework#sl-wifi-get-saved-firmware-status) API. This is retained for backward compatibility.
 *
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
 ******************************************************************************/
static inline SL_DEPRECATED_API_WISECONNECT_4_0 sl_status_t sl_si91x_get_saved_firmware_status(void)
{
#ifndef __ZEPHYR__
  return sli_wifi_get_saved_firmware_status();
#else
  return SL_STATUS_FAIL;
#endif
}
/** @} */
