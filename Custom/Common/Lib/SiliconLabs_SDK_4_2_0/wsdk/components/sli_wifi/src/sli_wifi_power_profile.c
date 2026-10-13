/***************************************************************************/ /**
 * @file sli_wifi_power_profile.c
 * @brief This file contains the implementation of the WiFi power profile functions.
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
#include "sli_wifi_power_profile.h"
#include "sli_wifi_utility.h"
#include "sli_wifi_types.h"
#include "sli_wifi.h"
#include "sl_wifi_types.h"
#include <string.h>
#include "sli_wifi_utility.h"
#include "sli_power_profile.h"
#include "sl_utility.h"

#define SLI_MAX_SIZE_OF_UINT16_T 65535
#ifndef SLI_CONNECTED_GPIO_BASED_PS
#define SLI_CONNECTED_GPIO_BASED_PS 2
#endif
#ifndef SLI_GPIO_BASED_DEEP_SLEEP
#define SLI_GPIO_BASED_DEEP_SLEEP 8
#endif

sl_status_t sli_wifi_set_performance_profile(const sl_wifi_performance_profile_t *profile)
{
  sl_status_t status;
  sl_wifi_system_performance_profile_t selected_coex_profile_mode = { 0 };
  sl_wifi_performance_profile_v2_t current_wifi_profile_mode      = { 0 };
  sl_wifi_performance_profile_v2_t profile_v2                     = { 0 };

  profile_v2.profile                  = profile->profile;
  profile_v2.dtim_aligned_type        = profile->dtim_aligned_type;
  profile_v2.num_of_dtim_skip         = profile->num_of_dtim_skip;
  profile_v2.listen_interval          = (uint32_t)profile->listen_interval;
  profile_v2.monitor_interval         = profile->monitor_interval;
  profile_v2.twt_request              = profile->twt_request;
  profile_v2.twt_selection            = profile->twt_selection;
  profile_v2.beacon_miss_ignore_limit = 1;

  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  SL_WIFI_ARGS_CHECK_NULL_POINTER(&profile_v2);

  if (profile_v2.profile > SL_WIFI_SYSTEM_DEEP_SLEEP_WITH_RAM_RETENTION) {
    return SL_STATUS_INVALID_MODE;
  }

  // Take backup of current wifi profile
  sli_wifi_get_current_performance_profile(&current_wifi_profile_mode);

  // Send the power save command for the requested profile
  status = sli_wifi_send_power_save_request(&profile_v2, NULL);
  if (status != SL_STATUS_OK) {
    sli_wifi_save_current_performance_profile(&current_wifi_profile_mode);
    return status;
  }
  sli_get_coex_performance_profile(&selected_coex_profile_mode);

  if (selected_coex_profile_mode == SL_WIFI_SYSTEM_DEEP_SLEEP_WITHOUT_RAM_RETENTION) {
#ifdef SLI_SI91X_MCU_INTERFACE
    // In soc mode m4 does not get the card ready for next init after deinit, but if device in DEEP_SLEEP_WITHOUT_RAM_RETENTION mode, m4 should wait for card ready for next init
    sli_wifi_set_card_ready_required(true);
#endif
    sli_wifi_reset_coex_current_performance_profile();
  }

  return SL_STATUS_OK;
}

sl_status_t sli_wifi_get_performance_profile(sl_wifi_performance_profile_t *profile)
{
  SL_VERIFY_POINTER_OR_RETURN(profile, SL_STATUS_NULL_POINTER);
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  sl_wifi_performance_profile_v2_t profile_v2 = { 0 };
  sli_wifi_get_current_performance_profile(&profile_v2);

  // Field-by-field copy: v1 and v2 have different layouts
  profile->profile           = profile_v2.profile;
  profile->dtim_aligned_type = profile_v2.dtim_aligned_type;
  profile->num_of_dtim_skip  = profile_v2.num_of_dtim_skip;
  profile->listen_interval   = (uint16_t)profile_v2.listen_interval;
  profile->monitor_interval  = profile_v2.monitor_interval;
  profile->twt_request       = profile_v2.twt_request;
  profile->twt_selection     = profile_v2.twt_selection;

  return SL_STATUS_OK;
}

sl_status_t sli_wifi_get_performance_profile_v2(sl_wifi_performance_profile_v2_t *profile)
{
  if (!sl_si91x_is_device_initialized()) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  sli_wifi_get_current_performance_profile(profile);
  return SL_STATUS_OK;
}
