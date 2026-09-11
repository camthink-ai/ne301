/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include <stdint.h>
#include "mmutils.h"

#include "yaps.h"

#define MORSE_INT_YAPS_FC_PKT_WAITING_IRQN     0
#define MORSE_INT_YAPS_FC_PACKET_FREED_UP_IRQN 1

#define MORSE_INT_CEIL(_num, _div)             (((_num) + (_div) - 1) / (_div))

struct MM_PACKED morse_yaps_hw_table
{

    uint8_t flags;
    uint8_t padding[3];
    uint32_t ysl_addr;
    uint32_t yds_addr;
    uint32_t status_regs_addr;


    uint16_t tc_tx_pool_size;
    uint16_t fc_rx_pool_size;
    uint8_t tc_cmd_pool_size;
    uint8_t tc_beacon_pool_size;
    uint8_t tc_mgmt_pool_size;
    uint8_t fc_resp_pool_size;
    uint8_t fc_tx_sts_pool_size;
    uint8_t fc_aux_pool_size;


    uint8_t tc_tx_q_size;
    uint8_t tc_cmd_q_size;
    uint8_t tc_beacon_q_size;
    uint8_t tc_mgmt_q_size;
    uint8_t fc_q_size;
    uint8_t fc_done_q_size;

    uint16_t yaps_reserved_page_size;
    uint16_t reserved_unused;
};

struct driver_data;

int morse_yaps_hw_init(struct driver_data *driverd);

void morse_yaps_hw_yaps_flush_tx_data(struct driver_data *driverd);

void morse_yaps_hw_finish(struct driver_data *driverd);

void morse_yaps_hw_read_table(struct driver_data *driverd, struct morse_yaps_hw_table *tbl_ptr);


uint32_t morse_yaps_hw_get_tc_queue_space(struct morse_yaps *yaps,
                                          enum morse_yaps_to_chip_q tc_queue);
