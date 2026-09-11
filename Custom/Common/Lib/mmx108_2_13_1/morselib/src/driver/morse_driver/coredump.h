/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include <stdint.h>

struct driver_data;


enum morse_coredump_mem_region_type
{

    MORSE_MEM_REGION_TYPE_GENERAL = 1,

    MORSE_MEM_REGION_TYPE_ASSERT_INFO = 2,
};


struct morse_coredump_mem_region
{

    uint32_t type;

    uint32_t start;

    uint32_t len;
};


void coredump_log_assert_info(struct driver_data *driverd);
