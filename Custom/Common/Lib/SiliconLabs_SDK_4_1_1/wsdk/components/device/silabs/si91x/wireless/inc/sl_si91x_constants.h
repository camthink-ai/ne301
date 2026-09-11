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

#include <stdint.h>
#include "sl_wifi_constants.h"
#include "sli_wifi_constants.h"
#include "sl_constants.h"
/** \addtogroup SI91X_LOAD_IMAGE_TYPES
  * @{ */
/// Load the default NWP firmware at location 0.
#define LOAD_NWP_FW 0x31

/// Load the NWP firmware using the image number (@ref SI91X_NWP_FW_IMAGE_NUMBERS)
/// @note This is ONLY applicable in System on Chip (SoC) mode for Si91x devices.
#define SL_SI91X_LOAD_NWP_FW_WITH_IMAGE_NUMBER 0x41

/// Load default NWP firmware active low.
/// @note This is ONLY applicable in Network Co-Processor (NCP) mode for Si91x devices.
#define LOAD_DEFAULT_NWP_FW_ACTIVE_LOW 0x71

/// Burn NWP firmware image
#define BURN_NWP_FW 'B'

/// Burn M4 firmware image
#define BURN_M4_FW '4'
/** @} */

/** \addtogroup SI91X_NWP_FW_IMAGE_NUMBERS
  * @{ */

#define SL_SI91X_NWP_FW_IMAGE_NUMBER_0 0 ///< Image number for the first NWP firmware image
#define SL_SI91X_NWP_FW_IMAGE_NUMBER_1 1 ///< Image number for the second NWP firmware image

/** @} */

/** \addtogroup SI91X_TLS_DEFINE
  * \ingroup SL_SI91X_CONSTANTS
  * @{ */
/*=========================================================================*/
// TLS feature parameters description !//
/*=========================================================================*/
/// Bit to enable SSL feature
#define SL_SI91X_ENABLE_TLS BIT(0)

/// Bitmap to enable TLS version 1.0
#define SL_SI91X_TLS_V_1_0 BIT(2)

/// Bitmap to enable TLS version 1.2
#define SL_SI91X_TLS_V_1_2 BIT(3)

/// Bitmap to enable TLS version 1.1
#define SL_SI91X_TLS_V_1_1 BIT(4)

#if defined(SLI_SI917) || defined(DOXYGEN)
/// Bitmap to enable TLS version 1.3
#define SL_SI91X_TLS_V_1_3 BIT(8)
#endif

/// Bitmap to enable DTLS version 1.0
#define SL_SI91X_DTLS_V_1_0 BIT(2)

/// Bitmap to enable DTLS version 1.2
#define SL_SI91X_DTLS_V_1_2 BIT(3)

/// Bit to enable DTLS feature
#define SL_SI91X_ENABLE_DTLS BIT(13)

/** @} */

/** \addtogroup SI91X_HTTP_CLIENT_DEFINE
  * \ingroup SL_SI91X_CONSTANTS
  * @{ */
/*=========================================================================*/
// HTTP Client feature parameters description !//
/*=========================================================================*/
/// Bit to enable NULL delimiter for HTTP buffer instead of comma
#define SL_SI91X_ENABLE_NULL_DELIMETER BIT(1)

/// HTTP client post big data support feature bitmap
#define SL_SI91X_SUPPORT_HTTP_POST_DATA BIT(5)

/// HTTP version 1.1 support feature bitmap
#define SL_SI91X_HTTP_V_1_1 BIT(6)

/// Bit to enable user given content type in extended header
#define SL_SI91X_HTTP_USER_DEFINED_CONTENT_TYPE BIT(7)

/// To specify index of SSL cert to be used for HTTPS, for index 0 leave them unset
#define SL_SI91X_HTTPS_CERTIFICATE_INDEX_1 BIT(9)
/// To specify index of SSL cert to be used for HTTPS, for index 0 leave them unset
#define SL_SI91X_HTTPS_CERTIFICATE_INDEX_2 BIT(10)

/// To enable SNI
#define SL_SI91X_HTTPS_USE_SNI BIT(11)
/** @} */

//! @cond Doxygen_Suppress
// M4 FW image number for Bootloader
#define SLI_M4_FW_IMAGE_NUMBER (1 << 8)

// Bootloader selects default NWP FW image number
#define SLI_SELECT_DEFAULT_NWP_FW_IMAGE_NUMBER '5'

#define SLI_PING_PONG_CHUNK_SIZE 4096
#define SLI_REG_READ             0xD1
#define SLI_REG_WRITE            0xD2
#define SLI_SEND_RPS_FILE        '2'
#define SLI_FWUP_SUCCESSFUL      'S'
#define SLI_EOF_REACHED          'E'
#define SLI_PONG_VALID           'O'
#define SLI_PING_VALID           'I'
#define SLI_PONG_WRITE           0xD4
#define SLI_PING_WRITE           0xD5
#define SLI_PONG_AVAIL           'O'
#define SLI_PING_AVAIL           'I'

#define SLI_PING_BUFFER_ADDR 0x418000
#define SLI_PONG_BUFFER_ADDR 0x419000

#define SLI_ACTIVE_LOW_INTR  0x2
#define SLI_ACTIVE_HIGH_INTR 0x0

#define SLI_OPERMODE_RESPONSE_WAIT_TIME      (1000) // Milliseconds
#define SLI_SEND_RAW_DATA_RESPONSE_WAIT_TIME (1000) // Milliseconds

//STM 32 Init Sequence
#define SLI_SI91X_INIT_CMD 0x005c4a12

// SPI transfer type (8 BIT/32 BIT)
#define SLI_MODE_8BIT  0
#define SLI_MODE_32BIT 1

// frame descriptor length
#define SLI_FRAME_DESC_LEN 16

// Bus interrupt register values
// Wi-Fi buffer full indication register value from NWP module
#ifndef SLI_WIFI_BUFFER_FULL
#define SLI_WIFI_BUFFER_FULL (1 << 0)
#endif

// BLE buffer full indication register value from NWP module
#ifndef SLI_BLE_BUFFER_FULL
#define SLI_BLE_BUFFER_FULL (1 << 4)
#endif
// Wi-Fi buffer empty indication register value from NWP module
#define SLI_BUFFER_EMPTY 0x02
// RX packet pending register value from NWP module
#define SLI_RX_PKT_PENDING 0x08
// Assertion Interrupt indication from NWP module
#define SLI_ASSERT_INTR 0x80

//Bootloader defines
#define SLI_SI91X_SAFE_UPGRADE_ADDR        0x55408
#define SLI_SI91X_SAFE_UPGRADE             BIT(12)
#define SLI_SI91X_FW_START_OF_FILE         BIT(0)
#define SLI_SI91X_FW_END_OF_FILE           BIT(1)
#define SLI_SI91X_MIN_CHUNK_SIZE           4096
#define SLI_SI91X_LOOP_COUNT_UPGRADE_IMAGE 0xFFFF
#define SLI_SI91X_RESET_LOOP_COUNTER(X)    X = 0;
#define SLI_SI91X_WHILE_LOOP(X, Y)         while ((X++) < (int32_t)Y)
#define SLI_SI91X_CHECK_LOOP_COUNTER(X, Y) \
  {                                        \
    if (X >= Y)                            \
      return -1;                           \
  }

//***************************** Macros for Crypto Start **********************************/

#define SL_SI91X_KEY_BUFFER_SIZE 32
#define SL_SI91X_TAG_SIZE        16
#define SL_SI91X_IV_SIZE         16

// ECDH defines
#define SLI_ECDH_BUFFER_SIZE 32

// TRNG defines
#define SLI_TRNG_INIT       1
#define SLI_TRNG_ENTROPY    2
#define SLI_TRNG_KEY        3
#define SLI_TRNG_GENERATION 4

#define SLI_TRNG_INIT_MSG_LENGTH 16
#define TRNG_KEY_SIZE            4
#define SLI_TRNG_TEST_DATA_SIZE  64

// CCM defines
#define SLI_SI91X_CCM_IV_MIN_SIZE  7
#define SLI_SI91X_CCM_IV_MAX_SIZE  13
#define SLI_SI91X_CCM_IV_BUFF_LEN  16  // SLI_SI91X_CCM_IV_MAX_SIZE + 3 Bytes for Padding.
#define SLI_SI91X_CCM_AD_MAX_SIZE  128 // Limiting header length to 128.
#define SLI_SI91X_CCM_MSG_MAX_SIZE 1200

// GCM defines
#define SLI_SI91X_GCM_IV_SIZE      12
#define SLI_SI91X_GCM_AD_MAX_SIZE  128
#define SLI_SI91X_GCM_MSG_MAX_SIZE 1400

// Chachapoly defines
#define SLI_SI91X_KEYR_SIZE               16
#define SLI_SI91X_KEYS_SIZE               16
#define SLI_SI91X_CHACHAPOLY_IV_SIZE      12
#define SLI_SI91X_CHACHAPOLY_MSG_MAX_SIZE 1200

//ECDSA defines
#define SLI_SI91X_PRIVATE_KEY_MAX_SIZE 64
#define SLI_SI91X_PUBLIC_KEY_MAX_SIZE  136
#define SLI_SI91X_SIGNATURE_MAX_SIZE   128
#define SLI_SI91X_ECDSA_MSG_MAX_SIZE   1000

// NWP Configuration defines
#define SL_SI91X_XO_CTUNE_FROM_HOST           BIT(0)
#define SL_SI91X_ENABLE_NWP_WDT_FROM_HOST     BIT(1)
#define SL_SI91X_DISABLE_NWP_WDT_FROM_HOST    BIT(2)
#define SL_SI91X_SET_XTAL_GOOD_TIME_FROM_HOST BIT(3)
#define SL_SI91X_SET_PMU_GOOD_TIME_FROM_HOST  BIT(4)
//! @endcond
/** \addtogroup SL_SI91X_CONSTANTS 
  * @{ */
#define GET_OPN_BOARD_CONFIG 1 ///< Get board OPN board configuration
#define GET_NWP_TIMESTAMP    2 ///< Get NWP timestamp
/** @} */

//***************************** Macros for Crypto End **********************************/
//! @cond Doxygen_Suppress
// Command packet 'unused' bytes
#define SLI_SI91X_COMMAND_FLAGS_INDEX    10
#define SLI_SI91X_COMMAND_RESPONSE_INDEX 11

typedef struct {
  uint32_t buffer_full : 1;
  uint32_t ready_to_transmit : 1;
  uint32_t _reserved2 : 1;
  uint32_t rx_packet_pending : 1;
  uint32_t _reserved4 : 1;
  uint32_t _reserved5 : 1;
  uint32_t _reserved6 : 1;
  uint32_t _reserved7 : 1;
} sli_si91x_interrupt_status_t;

// Timeout used in get_channel API
#define SL_SI91X_GET_CHANNEL_TIMEOUT 30200

/// Timeout used in get_interface_info API (milliseconds)
#define SL_SI91X_GET_INTERFACE_INFO_TIMEOUT 15000

/// Maximum wait time (in milliseconds) for the NCP wake indicator during host-requested NCP wakeup before returning SL_STATUS_TIMEOUT
#define SL_SI91X_NCP_REQ_WAKEUP_TIMEOUT_MS 5000

//! @endcond

/** \addtogroup SL_SI91X_CONSTANTS 
  * @{ */

/// Si91x specific keepalive types.
#define SL_SI91X_AP_KEEP_ALIVE_DISABLE SL_WIFI_AP_KEEP_ALIVE_DISABLE ///< Disable keepalive functionality.
#define SL_SI91X_AP_DEAUTH_BASED_KEEP_ALIVE \
  SL_WIFI_AP_DEAUTH_BASED_KEEP_ALIVE ///< AP performs keepalive functionality based on the RX packets received from its stations.
///< If no packet is received from the station within the AP keep alive timeout period, the AP disconnects the station.
#define SL_SI91X_AP_NULL_BASED_KEEP_ALIVE \
  SL_WIFI_AP_NULL_BASED_KEEP_ALIVE ///< AP performs keepalive functionality by sending a NULL DATA packet to the station.
///< If no ACK is received from the station after a specific number of retries, the AP discards the station.

/// Si91x specific keepalive types
typedef sl_wifi_ap_keepalive_type_t SL_DEPRECATED_API_WISECONNECT_4_0
  sl_si91x_ap_keepalive_type_t; ///< Si91x specific keepalive types

/// Assertion type must be in the range of 0 to 15 (both included)
typedef enum {
  SL_SI91X_ASSERTION_TYPE_LMAC = 0, ///< Assertion type specific to the LMAC core.
  SL_SI91X_ASSERTION_TYPE_SME  = 1, ///< Assertion type specific to the SME (Station Management Entity) core.
  SL_SI91X_ASSERTION_TYPE_UMAC = 2, ///< Assertion type specific to the UMAC core.
  SL_SI91X_ASSERTION_TYPE_NETX = 4, ///< Assertion type specific to the NETX (Networking Stack) core.
  SL_SI91X_ASSERTION_TYPE_CA =
    8, ///< Enables critical assertion indication and provides a RAM dump during critical assertions.
  SL_SI91X_ASSERTION_TYPE_ALL = 15 ///< Enables assertion for all cores.
} sl_si91x_assertion_type_t;

/// Assertion level must be in the range of 0 to 15 (both included)
typedef enum {
  SL_SI91X_ASSERTION_LEVEL_MIN = 0, ///< Minimum assertion level. Indicates that an assertion is mandatory.
  SL_SI91X_ASSERTION_LEVEL_SP  = 1, ///< Assertion for specific print messages, used for debugging or analysis.
  SL_SI91X_ASSERTION_LEVEL_REC =
    2, ///< Recoverable assertion level. Indicates that the system can recover from the assertion.
  SL_SI91X_ASSERTION_LEVEL_INFO = 4, ///< Informational assertion level, used to log general information.
  SL_SI91X_ASSERTION_LEVEL_MAX  = 15 ///< Maximum assertion level. Enables all types of print statements.
} sl_si91x_assertion_level_t;
/** @} */

//! @cond Doxygen_Suppress
typedef enum {
  // (7+/-1)dBm in 2.4GHz band
  // (5+/-1)dBm in 5GHz band
  SLI_SI91X_LOW_POWER_LEVEL,

  // (5+/-1)dBm in 2.4GHz band
  // (7+/-1)dBm in 5GHz band
  SLI_SI91X_MEDIUM_POWER_LEVEL,

  SLI_SI91X_HIGH_POWER_LEVEL
} sli_si91x_transmit_power_level_t;

/// This macro is an alias for @ref SLI_WIFI_WAIT_FOR_EVER and sets the wait period bit flag to indicate infinite wait time.
/// This macro is deprecated, use SL_NET_WAIT_FOREVER instead.
#define SLI_SI91X_WAIT_FOR_EVER SLI_WIFI_WAIT_FOR_EVER
/*====================================================*/
// Constant Defines
// SPI Status
#define SLI_SPI_SUCCESS       0x58
#define SLI_SPI_BUSY          0x54
#define SLI_SPI_FAIL          0x52
#define RSI_SUCCESS           0
#define SLI_ERROR_BUFFER_FULL -3 // module buffer full  error code
#define SLI_ERROR_IN_SLEEP    -4 // module in sleep error code

//SPI Internal Register Offset
#define SLI_SPI_INT_REG_ADDR    0x00 //@ register access method
#define SLI_SPI_MODE_REG_ADDR   0x08 //@ register access method
#define SLI_SPI_LENGTH_REG_ADDR 0x20

#define SLI_INT_MASK_REG_ADDR 0x41050000 // Interrupt mask register
#define SLI_INT_CLR_REG_ADDR  0x22000010 // Interrupt clear register

// Packet queue identifiers
#define SLI_COMMON_Q    0
#define SLI_ZB_Q        1
#define SLI_BT_Q        2
#define SLI_WLAN_MGMT_Q 4
#define SLI_WLAN_DATA_Q 5
#ifdef SAPIS_BT_STACK_ON_HOST
#define SLI_BT_INT_MGMT_Q 6
#define SLI_BT_HCI_Q      7
#endif
#define SLI_LOG_Q 8

// Event IDs
#define SLI_RX_EVENT       0 // RX event number used in the driver
#define SLI_TX_EVENT       1 // TX event number used in the driver
#define SLI_SOCKET_EVENT   2 // Socket event number used in the driver
#define SLI_MAX_NUM_EVENTS 3 // Max number events used in the driver

#define SLI_HOST_INTF_REG_OUT    0x4105003C
#define SLI_HOST_INTF_REG_IN     0x41050034
#define SLI_HOST_INTF_STATUS_REG 0x41050004

// si91x boot results
#define SLI_EOF_REACHED                          'E'
#define SLI_BOOTUP_OPTIONS_LAST_CONFIG_NOT_SAVED 0xF1
#define SLI_BOOTUP_OPTIONS_CHECKSUM_FAIL         0xF2
#define SLI_INVALID_OPTION                       0xF3
#define SLI_CHECKSUM_SUCCESS                     0xAA
#define SLI_CHECKSUM_FAILURE                     0xCC
#define SLI_CHECKSUM_INVALID_ADDRESS             0x4C
#define SLI_VALID_FIRMWARE_NOT_PRESENT           0x23
#define SLI_BOOTLOADER_VERSION_1P0               0x10
#define SLI_BOOTLOADER_VERSION_1P1               0x11
#define SLI_ROM_VERSION_1P0                      1
#define SLI_ROM_VERSION_1P1                      2

#define SLI_WIFI_REGISTER_VALID (0xAB)

#define SLI_HOST_INTERACT_REG_VALID    (0xAB << 8)
#define SLI_HOST_INTERACT_REG_VALID_FW (0xA0 << 8)

#define SLI_SEND_SOCKET_DATA                 0x0
#define SLI_RECEIVE_RAW_DATA                 0x0
#define SLI_NET_DUAL_STACK_RX_RAW_DATA_FRAME 0x1 // Rx data frame type in network dual stack mode

// WLAN command request type: use unified sli_wifi_request_commands_t (sli_wifi_constants.h)
typedef sli_wifi_request_commands_t sli_wlan_cmd_request_t;

// WLAN command response type: use unified sli_wifi_response_commands_t (sli_wifi_constants.h)
typedef sli_wifi_response_commands_t sli_wlan_cmd_response_t;

typedef enum { SL_SI91X_SOCKET_REMOTE_TERMINATED_EVENT, SL_SI91X_SOCKET_EVENT_COUNT } sl_si91x_socket_event_t;

//! @endcond
