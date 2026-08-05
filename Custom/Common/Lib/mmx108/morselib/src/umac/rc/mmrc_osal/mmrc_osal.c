/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include "mmrc_osal.h"
#include "mmhal_core.h"

void osal_mmrc_seed_random(void)
{
}

uint32_t osal_mmrc_random_u32(uint32_t max)
{
    return mmhal_random_u32(0, max - 1);
}
