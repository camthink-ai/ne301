/***************************************************************************/ /**
 * @file sli_wifi_command_engine_config.h
 * @brief SI91x WLAN command engine and related thread configuration macros.
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
#ifndef SLI_WIFI_COMMAND_ENGINE_CONFIG_H
#define SLI_WIFI_COMMAND_ENGINE_CONFIG_H

#include <stdint.h>

/* Do not include sl_si91x_driver.h here: sli_si91x_driver.h includes this header,
 * and sl_si91x_driver.h includes sli_si91x_driver.h, causing a circular include.
 * Include sl_si91x_driver.h or sli_si91x_driver.h in .c files that need driver types. */
#include "sl_status.h"
#include "sli_command_engine.h"

/**
 * @brief SLI event engine thread priority for SI91x WLAN command-engine builds.
 *
 * If @c SL_WLAN_EVENT_THREAD_PRIORITY is defined before this header is included, it becomes
 * @c SLI_EVENT_ENGINE_THREAD_PRIORITY (consumed by @c sli_event_engine.h); otherwise the default
 * is @c osPriorityRealtime2. This thread should generally run at a higher CMSIS-RTOS priority than
 * the WLAN command engine worker so async callbacks are not starved.
 */
#ifndef SL_WLAN_EVENT_THREAD_PRIORITY
#define SLI_EVENT_ENGINE_THREAD_PRIORITY osPriorityRealtime2
#else
#undef SLI_EVENT_ENGINE_THREAD_PRIORITY
#define SLI_EVENT_ENGINE_THREAD_PRIORITY SL_WLAN_EVENT_THREAD_PRIORITY
#endif

/**
 * @brief SLI event engine thread stack size (bytes) for SI91x WLAN command-engine builds.
 *
 * Define @c SL_SI91X_EVENT_HANDLER_STACK_SIZE before including this header to override the
 * default of 1536 bytes. Include this header before @c sli_event_engine.h (this file includes
 * it last) so these values apply instead of the generic defaults in @c sli_event_engine.h.
 */
#ifndef SL_SI91X_EVENT_HANDLER_STACK_SIZE
#define SLI_EVENT_ENGINE_THREAD_STACK_SIZE 1536
#else
#undef SLI_EVENT_ENGINE_THREAD_STACK_SIZE
#define SLI_EVENT_ENGINE_THREAD_STACK_SIZE SL_SI91X_EVENT_HANDLER_STACK_SIZE
#endif

#include "sli_event_engine.h"
#include "sli_queue_manager_types.h"
#include "sli_routing_utility.h"
#include "sl_wifi_types.h"
#include "sli_code_classification.h"

/******************************************************
 *               Macro Definitions
 ******************************************************/
/**
 * @brief CMSIS-RTOS thread priority for the WLAN command engine worker.
 *
 * Define @c SL_WLAN_COMMAND_ENGINE_THREAD_PRIORITY before including this header to override
 * the default @c osPriorityRealtime.
 *
 * @note Prefer a CMSIS-RTOS priority below the SLI event engine thread so handler callbacks are
 *       not starved; configure that thread with @c SL_WLAN_EVENT_THREAD_PRIORITY (see the block
 *       above that feeds @c SLI_EVENT_ENGINE_THREAD_PRIORITY).
 *
 * @note The command engine worker stack size is fixed at 1636 bytes in
 *       @c sli_si91x_wifi_command_engine_config.c (not overridden by a public macro here).
 */
#ifndef SL_WLAN_COMMAND_ENGINE_THREAD_PRIORITY
#define SL_WLAN_COMMAND_ENGINE_THREAD_PRIORITY osPriorityRealtime
#endif

/******************************************************
 *               Type Definitions
 ******************************************************/
/// Si91x specific command type
typedef enum {
  SLI_WLAN_COMMON_CMD  = 0, ///< SI91X Common Command
  SLI_WLAN_WIFI_CMD    = 1, ///< SI91X Wireless LAN Command
  SLI_WLAN_NETWORK_CMD = 2, ///< SI91X Network Command
  SLI_WLAN_BT_CMD      = 3, ///< SI91X Bluetooth Command
  SLI_WLAN_SOCKET_CMD  = 4, ///< SI91X Socket Command
  SLI_WLAN_CMD_MAX     = 5  ///< SI91X Maximum Command value
} sli_wlan_command_type_t;

typedef enum {
  SLI_WIFI_COMMAND_ENGINE_COMMON_COMMAND_PACKET = 0,
  SLI_WIFI_COMMAND_ENGINE_WIFI_COMMAND_PACKET,
  SLI_WIFI_COMMAND_ENGINE_NETWORK_COMMAND_PACKET,
  SLI_WIFI_COMMAND_ENGINE_BLE_COMMAND_PACKET,
  SLI_WIFI_COMMAND_ENGINE_SOCKET_COMMAND_PACKET,
  SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES
} sli_wifi_command_engine_packet_types_t;

typedef enum {
  SLI_WIFI_ASYNC_EVENT_HANDLER_COMMON_EVENT = 0,
  SLI_WIFI_ASYNC_EVENT_HANDLER_WIFI_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_NETWORK_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_BLE_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_CMD_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_DATA_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_ERROR_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_NWP_LOG_EVENT,
  SLI_WIFI_ASYNC_EVENT_HANDLER_MAX_EVENTS
} sli_wifi_async_event_handler_events_t;

// Indicates RX response received for COMMON command type
#define SL_WIFI_HOST_COMMON_RESPONSE_EVENT (1 << SLI_WLAN_COMMON_CMD)

// Indicates synchronous RX response received for WLAN command type
#define SL_WIFI_RESPONSE_EVENT (1 << SLI_WLAN_WIFI_CMD)

// Indicates synchronous RX response received for NETWORK command type
#define SL_WIFI_NETWORK_RESPONSE_EVENT (1 << SLI_WLAN_NETWORK_CMD)

// Indicates RX response received for SOCKET command type
#define SL_WIFI_SOCKET_RESPONSE_EVENT (1 << SLI_WLAN_SOCKET_CMD)

// Indicates RX response received for BLE command type
#define SL_WIFI_BT_RESPONSE_EVENT (1 << SLI_WLAN_BT_CMD)

extern sli_command_engine_configuration_t sli_wifi_command_engine_config;

/**
 * @brief Command engine instance for Wi-Fi.
 */
extern sli_command_engine_t sli_wifi_command_engine;

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
sl_status_t sli_si91x_wifi_event_engine_init(void);
sl_status_t sli_si91x_wifi_event_engine_deinit(void);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
sl_status_t sli_si91x_wifi_command_engine_get_packet_metadata(const sli_command_engine_t *instance,
                                                              void *packet,
                                                              sli_command_engine_metadata_t *metadata);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
sl_status_t sli_si91x_wifi_command_engine_rx_packet_handler(sli_command_engine_t *instance,
                                                            uint16_t packet_type,
                                                            void *data);

SL_CODE_CLASSIFY(SL_CODE_COMPONENT_SLI_SI91X_WIFI_CMD_ENGINE, SL_CODE_CLASS_TIME_CRITICAL)
sl_status_t sli_si91x_wifi_command_engine_packet_handler(
  void *packet,
  uint32_t packet_size,
  sli_routing_utility_packet_status_handler_t packet_status_handler,
  void *context);

#endif // SLI_WIFI_COMMAND_ENGINE_CONFIG_H
