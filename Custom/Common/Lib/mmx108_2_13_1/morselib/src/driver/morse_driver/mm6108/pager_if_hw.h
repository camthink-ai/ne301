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


int morse_pager_hw_put(struct morse_pager *pager, struct morse_page *page);


int morse_pager_hw_pop(struct morse_pager *pager, struct morse_page *page);


int morse_pager_hw_notify_pager(const struct morse_pager *pager);


int morse_pager_hw_page_write(struct morse_pager *pager,
                              struct morse_page *page,
                              int offset,
                              const uint8_t *buf,
                              uint32_t num_bytes);


int morse_pager_hw_page_read(struct morse_pager *pager,
                             struct morse_page *page,
                             int offset,
                             uint8_t *buf,
                             uint32_t num_bytes);


int morse_pager_hw_pagesets_init(struct driver_data *driverd);

void morse_pager_hw_pagesets_finish(struct driver_data *driverd);
