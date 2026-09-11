/***************************************************************************/ /**
 * @file sli_hal_si91x_constants.h
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
#ifndef SLI_HAL_SI91X_CONSTANTS_H
#define SLI_HAL_SI91X_CONSTANTS_H

#define SLI_HAL_SI91X_WIFI_TX_EVENT          (1 << 1) ///< Event flag for Wi-Fi TX event
#define SLI_HAL_SI91X_BLE_TX_EVENT           (1 << 2) ///< Event flag for BLE TX event
#define SLI_HAL_SI91X_RX_EVENT               (1 << 3) ///< Event flag for RX event
#define SLI_HAL_SI91X_THREAD_TERMINATE_EVENT (1 << 4) ///< Event flag to terminate the HAL thread gracefully
#define SLI_HAL_SI91X_THREAD_TERMINATE_ACKNOWLEDGE_EVENT \
  (1 << 5) ///< Event flag to acknowledge the termination of the HAL thread

#define SLI_HAL_SI91X_BUFFER_AVAILABLE_EVENT (1 << 6) ///< Event flag to indicate buffer availability
#endif
