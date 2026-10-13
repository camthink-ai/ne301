/***************************************************************************/ /**
 * @file  sl_si91x_host_control.h
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
#ifndef SL_SI91X_HOST_CONTROL_H
#define SL_SI91X_HOST_CONTROL_H

#include "sl_status.h"
#include <stdint.h>

/***************************************************************************/ /**
 * @addtogroup HOST-INTERFACE Host Interface
 * @ingroup SI91X_SERVICE_APIS
 * @{
 * @brief SiWx91x host control hooks for reset, power cycle, and NCP wakeup.
 ******************************************************************************/

/***************************************************************************/ /**
 * @brief Assert the SiWx91x reset (host-specific; no-op on SoC).
 ******************************************************************************/
void sl_si91x_host_hold_in_reset(void);

/***************************************************************************/ /**
 * @brief Release the SiWx91x reset (host-specific; no-op on SoC).
 ******************************************************************************/
void sl_si91x_host_release_from_reset(void);

/***************************************************************************/ /**
 * @brief Power cycle the SiWx91x by asserting then releasing reset with settle delays.
 * @return SL_STATUS_OK on success, or an error status from the host platform.
 ******************************************************************************/
sl_status_t sl_si91x_host_power_cycle(void);

#ifndef SLI_SI91X_MCU_INTERFACE
/***************************************************************************/ /**
 * @brief Maximum time (ms) to wait for the NWP wake indicator during NCP req_wakeup.
 ******************************************************************************/
#define SLI_SI91X_NCP_REQ_WAKEUP_TIMEOUT_MS 5000

/***************************************************************************/ /**
 * @brief Request the NWP to wake from sleep (NCP GPIO handshake path).
 * @details SoC builds use `sli_si91x_nwp_interface.c` instead; declared in
 *          `sli_si91x_nwp_interface.h`.
 * @return SL_STATUS_OK on success, or a timeout/error status.
 ******************************************************************************/
sl_status_t sli_si91x_req_wakeup(void);
#endif // !SLI_SI91X_MCU_INTERFACE

/** @} (end addtogroup HOST-INTERFACE) */

#endif // SL_SI91X_HOST_CONTROL_H
