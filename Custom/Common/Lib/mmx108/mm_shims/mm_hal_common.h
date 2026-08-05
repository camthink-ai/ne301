/**
 * Copyright 2024-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 * @file
 */

#pragma once

#include "mmhal_core.h"

/** Example hardware version string used for @c mmhal_get_hardware_version() */
#define MMHAL_HARDWARE_VERSION "MM-EKH08-U575"

/**
 * Deep sleep veto IDs used by mmhal. These must be in the range between
 *  MMHAL_VETO_ID_HAL_MIN and MMHAL_VETO_ID_HAL_MAX (inclusive).
 */
enum mmhal_deep_sleep_veto_id
{
    /** UART HAL deep sleep veto. */
    MMHAL_VETO_ID_HAL_UART = MMHAL_VETO_ID_HAL_MIN,
};

/**
 * Retrieve the current active deep sleep vetoes.
 *
 * @returns The current active deep sleep vetoes.
 */
uint32_t mmhal_get_deep_sleep_veto(void);

/**
 * Initialize mmhal resources for random number generator
 */
void mmhal_random_init(void);

/**
 * Read one 32-bit value from the STM32 hardware RNG.
 *
 * @param value  Output value on success.
 * @return 1 on success, 0 if RNG is not ready or HAL_RNG_GenerateRandomNumber failed.
 */
int mmhal_random_get_u32(uint32_t *value);
