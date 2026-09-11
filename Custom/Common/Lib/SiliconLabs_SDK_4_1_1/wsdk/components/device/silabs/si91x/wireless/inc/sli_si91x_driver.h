/***************************************************************************/ /**
 * @file
 * @brief Internal Si91x driver API (sli).
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

#include "cmsis_os2.h"
#include "sl_status.h"
#include "sl_si91x_status.h"
#include "sl_wifi_device.h"
#include "sli_queue_manager.h"
#include "sli_wifi_command_engine_config.h"
#include "sli_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/ /**
 * @addtogroup SLI_SI91X_INTERNAL_CONSTANTS
 * @ingroup SLI_SI91X_DRIVER
 * @{
 ******************************************************************************/

/***************************************************************************/ /**
 * @brief XTAL good time validation range (microseconds).
 * @details Valid range for crystal oscillator stabilization time.
 *          Applies to both SoC and NCP modes.
 * @see sl_si91x_nwp_configuration_t
 ******************************************************************************/
#define SLI_SI91X_XTAL_GOOD_TIME_MIN 600                                      ///< XTAL good time lower bound (µs)
#define SLI_SI91X_XTAL_GOOD_TIME_MAX 5000                                     ///< XTAL good time upper bound (µs)

/***************************************************************************/ /**
 * @brief PMU good time validation ranges (microseconds).
 * @details Valid ranges for Power Management Unit stabilization time.
 *          SoC mode requires 900-2000µs, NCP mode accepts 600-2000µs.
 * @see sl_si91x_nwp_configuration_t
 ******************************************************************************/
#define SLI_SI91X_PMU_GOOD_TIME_MAX     2000 ///< PMU good time common upper bound (µs)
#define SLI_SI91X_PMU_GOOD_TIME_MIN_SOC 900  ///< PMU good time lower bound for SoC mode
#define SLI_SI91X_PMU_GOOD_TIME_MIN_NCP 600  ///< PMU good time lower bound for NCP mode

#ifdef SLI_SI91X_MCU_INTERFACE
#define SLI_SI91X_PMU_GOOD_TIME_MIN SLI_SI91X_PMU_GOOD_TIME_MIN_SOC ///< PMU good time minimum (SoC)
#else
#define SLI_SI91X_PMU_GOOD_TIME_MIN SLI_SI91X_PMU_GOOD_TIME_MIN_NCP ///< PMU good time minimum (NCP)
#endif

/** @} */ // end of SLI_SI91X_INTERNAL_CONSTANTS

/** Command queues used by the command engine (defined in sli_si91x_driver.c). */
extern sli_queue_t cmd_queues[SI91X_CMD_MAX];

/** Event flags for command-engine / NCP notification (defined in sli_si91x_driver.c). */
extern osEventFlagsId_t sli_wifi_events;

/***************************************************************************/ /**
 * @brief Send a Bluetooth command.
 ******************************************************************************/
sl_status_t sli_si91x_driver_send_bt_command(sli_wlan_cmd_request_t command,
                                             sli_wifi_command_type_t command_type,
                                             sl_wifi_system_packet_t *data);

/***************************************************************************/ /**
 * @brief Set event flags (e.g. to wake waiters on sli_si91x_wait_for_event).
 ******************************************************************************/
void sli_wifi_set_event(uint32_t event_mask);

/***************************************************************************/ /**
 * @brief Wait for one of the given events (used during init).
 ******************************************************************************/
uint32_t sli_si91x_wait_for_event(uint32_t event_mask, uint32_t timeout);

/***************************************************************************/ /**
 * @brief Retrieves the current timestamp from the NWP.
 ******************************************************************************/
sl_status_t sli_get_nwp_timestamp(uint32_t *timestamp);

/***************************************************************************/ /**
 * @brief Initialize the Wi-Fi platform for the Si91x device.
 ******************************************************************************/
sl_status_t sli_si91x_wifi_platform_init(void);

/***************************************************************************/ /**
 * @brief De-initialize the Wi-Fi platform for the Si91x device.
 ******************************************************************************/
sl_status_t sli_si91x_wifi_platform_deinit(void);

/***************************************************************************/ /**
 * @brief Convert si91x_status_t to sl_status_t.
 ******************************************************************************/
sl_status_t sli_convert_si91x_status_to_sl_status(si91x_status_t si91x_status);

/***************************************************************************/ /**
 * @brief Get Efuse data from driver context.
 ******************************************************************************/
void sli_si91x_get_efuse_data(sli_wifi_efuse_data_t *efuse_data);

/***************************************************************************/ /**
 * @brief Set Efuse data in driver context.
 ******************************************************************************/
void sli_si91x_set_efuse_data(const sli_wifi_efuse_data_t *efuse_data);

/***************************************************************************/ /**
 * @brief Get flash/efuse data from NWP.
 ******************************************************************************/
sl_status_t sli_si91x_get_flash_efuse_data(sli_wifi_efuse_data_t *efuse_data, uint8_t efuse_data_type);

/***************************************************************************/ /**
 * @brief Get config feature bit map.
 ******************************************************************************/
uint32_t sli_si91x_get_config_feature_bit_map(void);

/***************************************************************************/ /**
 * @brief Set config feature bit map (called from driver init).
 ******************************************************************************/
void sli_si91x_set_config_feature_bit_map(uint32_t config_feature_bit_map);

/***************************************************************************/ /**
 * @brief Store XTAL/PMU good time to be applied during next init (called before init).
 ******************************************************************************/
void sli_si91x_set_xtal_pmu_good_time_from_host(uint32_t code, uint16_t value);

/***************************************************************************/ /**
 * @brief Apply any pending XTAL/PMU good time (called during init).
 ******************************************************************************/
sl_status_t sli_si91x_apply_pending_xtal_pmu_good_time(void);

/***************************************************************************/ /**
 * @brief Encapsulate TX data packet for transceiver mode.
 * @param feature_bit_map Feature bit map from boot config (from sl driver).
 ******************************************************************************/
int32_t sli_encapsulate_tx_data_packet(sl_wifi_transceiver_tx_data_control_t *control,
                                       uint8_t *pkt_data,
                                       uint32_t mac_hdr_len,
                                       uint32_t feature_bit_map);

/***************************************************************************/ /**
 * @brief Verify that the device has booted successfully.
 ******************************************************************************/
sl_status_t sli_verify_device_boot(uint32_t *rom_version);

/***************************************************************************/ /**
 * @brief Select boot option (firmware load/burn etc.).
 ******************************************************************************/
sl_status_t sli_wifi_select_option(const uint8_t configuration);

#ifdef __cplusplus
}
#endif
