/*******************************************************************************
 * @file  rsi_bt_common_config.h
 * @brief : This file contains user configurable details to configure the device
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

#ifndef RSI_BT_COMMON_CONFIG_H
#define RSI_BT_COMMON_CONFIG_H
/** @addtogroup BT_BLE_CONSTANTS
 *  @{
 */

#ifndef BD_ADDR_ARRAY_LEN
#define BD_ADDR_ARRAY_LEN 18 ///< length of the Bluetooth device address array
#endif

#ifndef BT_GLOBAL_BUFF_LEN
#define BT_GLOBAL_BUFF_LEN 10000 ///< size of the global buffer for Bluetooth operations
#endif
/*=======================================================================*/
// Discovery command parameters
/*=======================================================================*/

#ifdef __ZEPHYR__
#ifndef RSI_BLE_SET_RAND_ADDR
#define RSI_BLE_SET_RAND_ADDR "00:23:A7:12:34:56"
#endif

#ifndef RSI_BLE_MAX_NBR_PERIPHERALS
#define RSI_BLE_MAX_NBR_PERIPHERALS 3
#endif

#ifndef RSI_BLE_MAX_NBR_CENTRALS
#define RSI_BLE_MAX_NBR_CENTRALS 1
#endif
#endif

/** @} */
#endif //RSI_BT_COMMON_CONFIG_H
