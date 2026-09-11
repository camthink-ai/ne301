/***************************************************************************/ /**
 * @file sli_si91x_wifi_command_engine_packet.h
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
#pragma once

#ifndef SLI_WIFI_COMMAND_ENGINE_PACKET_H
#define SLI_WIFI_COMMAND_ENGINE_PACKET_H

#include "sli_routing_utility.h" // Ensure this header defines sli_routing_table_t

typedef enum {
  SLI_WIFI_COMMAND_PACKET = 0,
  SLI_WIFI_DATA_PACKET,
  SLI_WIFI_SOCKET_DATA_PACKET,
  SLI_BT_PACKET,
  SLI_WIFI_COMMAND_ENGINE_MAX_PACKET
} sli_wifi_command_engine_packet_t;

extern sli_routing_table_t wifi_command_engine_routing_table;

#endif // SLI_WIFI_COMMAND_ENGINE_PACKET_H
