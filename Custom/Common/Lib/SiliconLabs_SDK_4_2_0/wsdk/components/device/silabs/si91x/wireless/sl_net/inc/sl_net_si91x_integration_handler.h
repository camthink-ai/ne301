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
#include "sli_wifi_types.h"
#include "sl_si91x_types.h"
#include "sli_command_engine.h"

#define SL_NET_EVENT_DISPATCH_HANDLER(response) sl_net_si91x_event_dispatch_handler(response)

void sl_net_si91x_event_dispatch_handler(sli_command_engine_response_t *response);

#define SLI_NETWORK_CLEANUP_HANDLER() sli_si91x_network_cleanup_handler()

void sli_si91x_network_cleanup_handler();