/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "driver/morse_driver/mm6108/pageset.h"
#include "driver/morse_driver/mm6108/pager_if_hw.h"
#include "driver/morse_driver/mm8108/yaps.h"
#include "driver/morse_driver/mm8108/yaps-hw.h"


#define MORSE_CHIP_IF_IRQ_MASK_ALL                \
    (GENMASK(13, 0) |                             \
     MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE | \
     MORSE_PAGER_IRQ_BYPASS_CMD_RESP_AVAILABLE)

enum morse_chip_if_flags
{
    MORSE_CHIP_IF_FLAGS_DIR_TO_HOST = BIT(0),
    MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP = BIT(1),
    MORSE_CHIP_IF_FLAGS_COMMAND = BIT(2),

    MORSE_CHIP_IF_FLAGS_BEACON = BIT(3),
    MORSE_CHIP_IF_FLAGS_DATA = BIT(4)
};

enum morse_chip_if
{
    MORSE_CHIP_IF_PAGESET,
    MORSE_CHIP_IF_YAPS
};

struct chip_if_ops
{

    int (*init)(struct driver_data *driverd);


    void (*flush_tx_data)(struct driver_data *driverd);


    void (*finish)(struct driver_data *driverd);


    void (*skbq_get_tx_qs)(struct driver_data *driverd, struct morse_skbq **qs, int *num_qs);


    void (*skbq_close)(struct morse_skbq *mq);


    struct morse_skbq *(*skbq_cmd_tc_q)(struct driver_data *driverd);


    struct morse_skbq *(*skbq_bcn_tc_q)(struct driver_data *driverd);


    struct morse_skbq *(*skbq_mgmt_tc_q)(struct driver_data *driverd);


    struct morse_skbq *(*skbq_tc_q_from_aci)(struct driver_data *driverd, int aci);


    int (*chip_if_handle_irq)(struct driver_data *driverd, uint32_t status);


    int (*skbq_get_tx_buffered_count)(struct driver_data *driverd);


    void (*chip_if_work)(struct driver_data *driverd);


    void (*tx_stale_work)(struct driver_data *driverd);
};

struct morse_chip_if_state
{
    enum morse_chip_if active_chip_if;

    union
    {
        struct
        {
            int pager_count;
            struct morse_pager pagers[MAX_PAGERS];
            int pageset_count;
            struct morse_pageset pagesets[MAX_PAGESETS];
            struct morse_pageset *to_chip_pageset;
            struct morse_pageset *from_chip_pageset;

            struct
            {
                struct
                {
                    uint32_t location;
                    uint32_t to_process[MORSE_PAGER_BYPASS_TX_STATUS_FIFO_DEPTH];
                } tx_status;

                struct
                {
                    uint32_t location;
                    uint32_t to_process[MORSE_PAGER_BYPASS_CMD_RESP_FIFO_DEPTH];
                } cmd_resp;
            } bypass;
            struct morse_pager_pkt_memory pkt_memory;
        };

        struct
        {
            struct morse_yaps yaps;
        };
    };

    bool validate_skb_checksum;
};

struct MM_PACKED morse_chip_if_host_table
{
    union
    {
        struct MM_PACKED
        {
            uint32_t pager_count;
            struct morse_pager_hw_entry pager_table[];
        };
        struct morse_yaps_hw_table yaps_info;
    };
};
