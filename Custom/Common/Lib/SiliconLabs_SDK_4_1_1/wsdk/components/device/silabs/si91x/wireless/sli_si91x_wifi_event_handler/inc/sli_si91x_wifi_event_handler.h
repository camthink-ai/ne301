/***************************************************************************/ /**
 * @file sli_si91x_wifi_event_handler.h
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

#ifndef _SLI_SI91X_WIFI_EVENT_HANDLER_H_
#define _SLI_SI91X_WIFI_EVENT_HANDLER_H_

#include <stdint.h>
#include "sl_status.h"
#include "sli_command_engine.h"
#include "sli_wifi_command_engine_config.h"
#include "sl_wifi_types.h"

#ifdef SLI_SI91X_OFFLOAD_NETWORK_STACK
#include "sl_si91x_socket_types.h"
#include "sl_ip_types.h"
#endif

extern osEventFlagsId_t sli_wifi_event_engine_event_id;
extern sli_queue_t event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_MAX_EVENTS];

sl_status_t sli_si91x_wifi_data_packet_handler(void *packet,
                                               uint32_t packet_size,
                                               sli_routing_utility_packet_status_handler_t packet_status_handler,
                                               void *context);

sl_status_t sli_si91x_wifi_ble_packet_handler(void *rx_buffer,
                                              uint32_t packet_size,
                                              sli_routing_utility_packet_status_handler_t packet_status_handler,
                                              void *context);

sl_status_t sli_si91x_wifi_nwp_log_packet_handler(void *packet,
                                                  uint32_t packet_size,
                                                  sli_routing_utility_packet_status_handler_t packet_status_handler,
                                                  void *context);

sl_status_t sli_si91x_wifi_event_engine_init(void);
sl_status_t sli_si91x_wifi_event_engine_deinit(void);
void sli_wifi_event_engine_signal_async(void);

#ifdef SLI_SI91X_ENABLE_BLE
void sli_si91x_ble_send_packet_tx_status(uint16_t packet_type, sl_status_t status, void *context);
#endif

#endif
