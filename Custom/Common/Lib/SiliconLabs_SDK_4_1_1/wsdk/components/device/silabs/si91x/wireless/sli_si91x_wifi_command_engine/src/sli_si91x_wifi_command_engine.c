/***************************************************************************/ /**
 * @file sli_si91x_wifi_command_engine.c
 * @brief
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

#include "sli_si91x_wifi_command_engine.h"
#include "sl_constants.h"
#include "sli_wifi_command_engine_config.h"
#include "sli_si91x_wifi_command_engine_packet.h"
#include "sli_si91x_driver.h"
#include "sli_command_engine.h"
#include "sli_si91x_wifi_event_handler.h"

sl_status_t sli_si91x_wifi_command_engine_init(void)
{
  sl_status_t status = SL_STATUS_OK;

  status = sli_si91x_wifi_event_engine_init();
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_command_engine_init(&sli_wifi_command_engine,
                                   (const sli_command_engine_configuration_t *)&sli_wifi_command_engine_config);
  if (status != SL_STATUS_OK) {
    (void)sli_si91x_wifi_event_engine_deinit();
    return status;
  }

  sli_command_engine_packet_type_configuration_t
    sli_wifi_command_engine_packet_type_configuration[SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES] = {
      { .rx_event_handler            = sli_si91x_wifi_command_engine_rx_packet_handler,
        .pre_tx_handler              = NULL,
        .packet_processing_type      = SLI_COMMAND_ENGINE_COMMAND_PACKET,
        .route_packet_type           = SLI_WIFI_COMMAND_PACKET,
        .sync_response_queue         = &cmd_queues[SLI_WLAN_COMMON_CMD],
        .sync_response_event         = SL_WIFI_HOST_COMMON_RESPONSE_EVENT,
        .sync_response_event_id      = &sli_wifi_events,
        .max_in_flight_command_count = 1,
        .async_response_queue        = &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_COMMON_EVENT],
        .async_response_event_id     = &sli_wifi_event_engine_event_id,
        .async_response_event        = SLI_EVENT_ENGINE_ASYNC_EVENT },
      { .rx_event_handler            = sli_si91x_wifi_command_engine_rx_packet_handler,
        .pre_tx_handler              = NULL,
        .packet_processing_type      = SLI_COMMAND_ENGINE_COMMAND_PACKET,
        .route_packet_type           = SLI_WIFI_COMMAND_PACKET,
        .sync_response_queue         = &cmd_queues[SLI_WLAN_WIFI_CMD],
        .sync_response_event         = SL_WIFI_RESPONSE_EVENT,
        .sync_response_event_id      = &sli_wifi_events,
        .max_in_flight_command_count = 1,
        .async_response_queue        = &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_WIFI_EVENT],
        .async_response_event_id     = &sli_wifi_event_engine_event_id,
        .async_response_event        = SLI_EVENT_ENGINE_ASYNC_EVENT },
      { .rx_event_handler            = sli_si91x_wifi_command_engine_rx_packet_handler,
        .pre_tx_handler              = NULL,
        .packet_processing_type      = SLI_COMMAND_ENGINE_COMMAND_PACKET,
        .route_packet_type           = SLI_WIFI_COMMAND_PACKET,
        .sync_response_queue         = &cmd_queues[SLI_WLAN_NETWORK_CMD],
        .sync_response_event         = SL_WIFI_NETWORK_RESPONSE_EVENT,
        .sync_response_event_id      = &sli_wifi_events,
        .max_in_flight_command_count = 1,
        .async_response_queue        = &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_NETWORK_EVENT],
        .async_response_event_id     = &sli_wifi_event_engine_event_id,
        .async_response_event        = SLI_EVENT_ENGINE_ASYNC_EVENT },
      { .rx_event_handler            = sli_si91x_wifi_command_engine_rx_packet_handler,
        .pre_tx_handler              = NULL,
        .packet_processing_type      = SLI_COMMAND_ENGINE_COMMAND_PACKET,
        .route_packet_type           = SLI_BT_PACKET,
        .sync_response_queue         = &cmd_queues[SLI_WLAN_BT_CMD],
        .sync_response_event         = SL_WIFI_BT_RESPONSE_EVENT,
        .sync_response_event_id      = &sli_wifi_events,
        .max_in_flight_command_count = 1,
        .async_response_queue        = &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_BLE_EVENT],
        .async_response_event_id     = &sli_wifi_event_engine_event_id,
        .async_response_event        = SLI_EVENT_ENGINE_ASYNC_EVENT },
      { .rx_event_handler            = sli_si91x_wifi_command_engine_rx_packet_handler,
        .pre_tx_handler              = NULL,
        .packet_processing_type      = SLI_COMMAND_ENGINE_COMMAND_PACKET,
        .route_packet_type           = SLI_WIFI_COMMAND_PACKET,
        .sync_response_queue         = &cmd_queues[SLI_WLAN_SOCKET_CMD],
        .sync_response_event         = SL_WIFI_SOCKET_RESPONSE_EVENT,
        .sync_response_event_id      = &sli_wifi_events,
        .max_in_flight_command_count = 1,
        .async_response_queue        = &event_queue[SLI_WIFI_ASYNC_EVENT_HANDLER_SOCKET_CMD_EVENT],
        .async_response_event_id     = &sli_wifi_event_engine_event_id,
        .async_response_event        = SLI_EVENT_ENGINE_ASYNC_EVENT }
    };

  for (uint8_t packet_type = 0; packet_type < SLI_WIFI_COMMAND_ENGINE_MAX_PACKET_TYPES; packet_type++) {
    status = sli_command_engine_add_packet_type(&sli_wifi_command_engine,
                                                packet_type,
                                                &sli_wifi_command_engine_packet_type_configuration[packet_type]);
    if (status != SL_STATUS_OK) {
      sli_command_engine_deinit(&sli_wifi_command_engine);
      sli_si91x_wifi_event_engine_deinit();
      VERIFY_STATUS_AND_RETURN(status);
    }
  }

  return SL_STATUS_OK;
}

sl_status_t sli_si91x_wifi_command_engine_deinit(void)
{
  sl_status_t status = SL_STATUS_OK;

  // Rely on sli_command_engine_deinit to free all dynamic packet types; remove_packet_type can time out
  // and would skip deinit/event teardown if chained with VERIFY_STATUS_AND_RETURN.

  status = sli_command_engine_deinit(&sli_wifi_command_engine);
  VERIFY_STATUS_AND_RETURN(status);

  status = sli_si91x_wifi_event_engine_deinit();
  VERIFY_STATUS_AND_RETURN(status);

  return SL_STATUS_OK;
}
