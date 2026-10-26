/***************************************************************************/ /**
 * @file sli_wifi_power_profile.h
 * @brief This file contains the declaration of the WiFi power profile functions.
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
#ifndef SLI_WIFI_POWER_PROFILE_H
#define SLI_WIFI_POWER_PROFILE_H
#include "sl_wifi_device.h"
#include "sl_status.h"
#include "sli_wifi_types.h"
#include "sl_types.h"
#include "sli_power_profile.h"
#include "sli_types.h"

sl_status_t sli_wifi_set_performance_profile(const sl_wifi_performance_profile_t *profile);

sl_status_t sli_wifi_get_performance_profile(sl_wifi_performance_profile_t *profile);

sl_status_t sli_wifi_get_performance_profile_v2(sl_wifi_performance_profile_v2_t *profile);

#endif
