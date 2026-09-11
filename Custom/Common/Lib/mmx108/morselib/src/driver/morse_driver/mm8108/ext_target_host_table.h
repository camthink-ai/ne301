/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "yaps-hw.h"

#include "driver/morse_driver/ext_host_table.h"

struct MM_PACKED extended_host_table_yaps_table
{
    struct extended_host_table_tlv_hdr header;
    struct morse_yaps_hw_table yaps_table;
};
