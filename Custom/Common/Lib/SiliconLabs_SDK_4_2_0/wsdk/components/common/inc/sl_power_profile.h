/***************************************************************************/ /**
 * @file
 * @brief WiFi power profile functions
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
#ifndef SL_POWER_PROFILE_H
#define SL_POWER_PROFILE_H

#include "sl_types.h"
#include "sl_status.h"

/** \addtogroup WIFI_POWER_API Power and Performance
  * \ingroup SL_WIFI_FUNCTIONS
  * @{ */
/***************************************************************************/ /**
 * @brief
 *   Set Wi-Fi performance profile.
 * @pre Pre-conditions:
 * 
 *   @ref sl_wifi_init should be called before this API.
 * @param[in] profile
 *   Wi-Fi performance profile as indicated by [sl_wifi_performance_profile_v2_t](../wiseconnect-api-reference-guide-si91x-driver/sl-wifi-performance-profile-v2-t) 
 * @return
 *   sl_status_t. See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 * @note
 *   For SI91x chips Enhanced MAX PSP is supported when profile is set to ASSOCIATED_POWER_SAVE_LOW_LATENCY and SL_WIFI_ENABLE_ENHANCED_MAX_PSP bit is enabled in config feature bitmap
 * @note
 *   This v2 API is defined due to a new configuration member beacon_miss_ignore_limit added to the structure sl_wifi_performance_profile_v2_t.
 *   Default value for beacon_miss_ignore_limit is 1. Recommended max value is 10. Higher value may cause interop issues.
 * @note 
 *   For POWER_SAVE_PROFILE with DEEP_SLEEP_WITHOUT_RAM_RETENTION, call [sl_net_deinit](../wiseconnect-api-reference-guide-nwk-mgmt/net-interface-functions#sl-net-deinit) before calling [sl_net_init](../wiseconnect-api-reference-guide-nwk-mgmt/net-interface-functions#sl-net-init).
 * @note
 *   To configure listen_interval in [sl_wifi_performance_profile_v2_t](../wiseconnect-api-reference-guide-si91x-driver/sl-wifi-performance-profile-v2-t), the SL_WIFI_JOIN_FEAT_PS_CMD_LISTEN_INTERVAL_VALID
 *   flag must be set using @ref sl_wifi_set_join_configuration() before connecting to the AP. Without this flag, the listen_interval value is ignored.
 * @note
 *   The listen interval configured through sl_wifi_set_performance_profile_v2() must not be greater than the listen interval set using @ref sl_wifi_set_listen_interval_v2().
 *   The default listen interval configured in the WiSeConnect SDK is 1000 ms. This interval is used when associating with the AP unless a different value is set by  @ref sl_wifi_set_listen_interval_v2(). 
 * @note
 *   For more details about connected and non-connected mode, see https://www.silabs.com/documents/public/application-notes/an1430-siwx917-soc-low-power.pdf.
 ******************************************************************************/
sl_status_t sl_wifi_set_performance_profile_v2(const sl_wifi_performance_profile_v2_t *profile);
/** @} */

#endif // SL_POWER_PROFILE_H