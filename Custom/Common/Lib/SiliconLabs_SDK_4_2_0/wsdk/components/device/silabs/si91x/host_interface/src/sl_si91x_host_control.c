/***************************************************************************/ /**
 * @file  sl_si91x_host_control.c
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
#include "sl_si91x_host_control.h"
#include "cmsis_os2.h"        // osDelay
#include "sl_cmsis_utility.h" // SLI_SYSTEM_MS_TO_TICKS
#include <stdint.h>

#ifndef SLI_SI91X_MCU_INTERFACE
#include "sl_status.h"

// NCP host wake/sleep and timing hooks (provided by the host MCU).
typedef uint32_t sli_si91x_host_timestamp_t;

void sl_si91x_host_set_sleep_indicator(void);
uint32_t sl_si91x_host_get_wake_indicator(void);
sli_si91x_host_timestamp_t sl_si91x_host_get_timestamp(void);
sli_si91x_host_timestamp_t sl_si91x_host_elapsed_time(uint32_t starting_timestamp);

// Weak default; overridden by the SPI/UART bus driver when linked.
__attribute__((weak)) void sli_si91x_ulp_wakeup_init(void)
{
}

// NCP wakeup via GPIO handshake, then bus ULP re-init.
sl_status_t sli_si91x_req_wakeup(void)
{
  sl_si91x_host_set_sleep_indicator();
  uint32_t timestamp = sl_si91x_host_get_timestamp();
  do {
    if (sl_si91x_host_get_wake_indicator()) {
      sli_si91x_ulp_wakeup_init();
      break;
    }
    if (sl_si91x_host_elapsed_time(timestamp) > SLI_SI91X_NCP_REQ_WAKEUP_TIMEOUT_MS) {
      return SL_STATUS_TIMEOUT;
    }
  } while (1);
  return SL_STATUS_OK;
}
#endif // !SLI_SI91X_MCU_INTERFACE

// Settle delay (ms) after asserting and releasing reset.
#define SLI_SI91X_POWER_CYCLE_DELAY_MS 100

// Assert reset, delay, release reset, delay.
sl_status_t sl_si91x_host_power_cycle(void)
{
  sl_si91x_host_hold_in_reset();
  osDelay(SLI_SYSTEM_MS_TO_TICKS(SLI_SI91X_POWER_CYCLE_DELAY_MS));

  sl_si91x_host_release_from_reset();
  osDelay(SLI_SYSTEM_MS_TO_TICKS(SLI_SI91X_POWER_CYCLE_DELAY_MS));

  return SL_STATUS_OK;
}
