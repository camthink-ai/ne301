/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "pager_if.h"

#include "driver/morse_driver/skbq.h"




#define CMD_RSVED_PAGES_MAX 2

#define CMD_RSVED_FIFO_LEN 2


#define CMD_RSVED_CMD_PAGES_MAX 1


#define CACHED_PAGES_MAX 32

#define CACHED_PAGES_FIFO_LEN 32


#define MAX_PAGESETS (2)


#define PAGESET_TX_SKBQ_MAX 4

extern const struct chip_if_ops morse_pageset_hw_ops;

struct morse_page
{
    uint32_t addr;
    uint32_t size_bytes;
};

struct morse_pager_pkt_memory
{
    uint32_t base_addr;
    uint16_t page_len;
    uint8_t page_len_reserved;
    uint8_t num;
};

struct page_fifo_hdr
{
    uint16_t wr_offset;
    uint16_t rd_offset;
    uint16_t slots;
    uint16_t reserved;
    struct morse_page *pages;
};


#define DECLARE_PAGE_FIFO(_name, _slots) \
    struct page_fifo_hdr _name;          \
    struct morse_page _name##_pages[(_slots) + 1]


#define INIT_PAGE_FIFO(_name, _slots) \
    do {                              \
        _name.wr_offset = 0;          \
        _name.rd_offset = 0;          \
        _name.slots = (_slots) + 1;   \
        _name.pages = _name##_pages;  \
    } while (0)

struct morse_pageset
{
    struct driver_data *driverd;

    struct morse_skbq data_qs[PAGESET_TX_SKBQ_MAX];
    struct morse_skbq beacon_q;
    struct morse_skbq mgmt_q;
    struct morse_skbq cmd_q;
    volatile atomic_ulong access_lock;

    uint8_t flags;

    struct morse_pager *populated_pager;
    struct morse_pager *return_pager;

    DECLARE_PAGE_FIFO(reserved_pages, CMD_RSVED_FIFO_LEN);
    DECLARE_PAGE_FIFO(cached_pages, CACHED_PAGES_FIFO_LEN);
};


int morse_pageset_init(struct driver_data *driverd,
                       struct morse_pageset *pageset,
                       uint8_t flags,
                       struct morse_pager *populated_pager,
                       struct morse_pager *return_pager);


void morse_pageset_finish(struct morse_pageset *pageset);

void morse_pageset_flush_tx_data(struct morse_pageset *pageset);


void morse_pagesets_stale_tx_work(struct driver_data *driverd);


int morse_pagesets_get_tx_buffered_count(struct driver_data *driverd);


void morse_pagesets_work(struct driver_data *driverd);
