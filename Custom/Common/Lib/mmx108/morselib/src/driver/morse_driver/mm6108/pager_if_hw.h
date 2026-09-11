/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "pager_if.h"




#define MAX_PAGERS (4)

struct morse_pager_hw_table
{
    uint32_t addr;
    uint32_t count;
};

struct MM_PACKED morse_pager_hw_entry
{
    uint8_t flags;
    uint8_t padding;
    uint16_t page_size;
    uint32_t pop_addr;
    uint32_t push_addr;
};

int morse_pager_hw_read_table(struct driver_data *driverd, struct morse_pager_hw_table *tbl_ptr);


int morse_pager_hw_pagesets_init(struct driver_data *driverd);

void morse_pager_hw_pagesets_flush_tx_data(struct driver_data *driverd);

void morse_pager_hw_pagesets_finish(struct driver_data *driverd);
