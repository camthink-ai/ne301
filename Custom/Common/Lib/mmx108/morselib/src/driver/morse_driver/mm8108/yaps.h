/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include <stdint.h>

#include "mmpkt.h"

#include "driver/morse_driver/skbq.h"


#define YAPS_TX_SKBQ_MAX 4

#define test_bit(_n, _p) !!(_n & (1u << _p))

extern const struct chip_if_ops morse_yaps_ops;

enum morse_yaps_to_chip_q
{
    MORSE_YAPS_TX_Q = 0,
    MORSE_YAPS_CMD_Q,
    MORSE_YAPS_BEACON_Q,
    MORSE_YAPS_MGMT_Q,

    MORSE_YAPS_NUM_TC_Q
};

enum morse_yaps_from_chip_q
{
    MORSE_YAPS_RX_Q = 4,
    MORSE_YAPS_CMD_RESP_Q,
    MORSE_YAPS_TX_STATUS_Q,
    MORSE_YAPS_AUX_Q,


    MORSE_YAPS_NUM_FC_Q
};

struct morse_yaps
{
    struct driver_data *driverd;
    struct morse_yaps_hw_aux_data *aux_data;

    const struct morse_yaps_ops *ops;
    struct morse_skbq data_tx_qs[YAPS_TX_SKBQ_MAX];
    struct morse_skbq beacon_q;
    struct morse_skbq mgmt_q;
    struct morse_skbq data_rx_q;
    struct morse_skbq cmd_q;
    struct morse_skbq cmd_resp_q;

    struct
    {
        bool is_full;
        bool enabled;
    } chip_queue_full;

    uint8_t flags;


    struct mmpkt *rx_scratch_pkt;
};

struct morse_yaps_ops
{

    int (*write_pkt)(struct morse_yaps *yaps,
                     struct mmpkt *mmpkt,
                     enum morse_yaps_to_chip_q tc_queue,
                     struct mmpkt *next_pkt);


    int (*read_pkt)(struct morse_yaps *yaps, struct mmpkt **mmpkt);


    int (*update_status)(struct morse_yaps *yaps);
};


int morse_yaps_init(struct driver_data *driverd, struct morse_yaps *yaps, uint8_t flags);


void morse_yaps_finish(struct morse_yaps *yaps);


void morse_yaps_flush_tx_data(struct morse_yaps *yaps);


void morse_yaps_work(struct driver_data *driverd);


void morse_yaps_stale_tx_work(struct driver_data *driverd);


int morse_yaps_get_tx_status_pending_count(struct driver_data *driverd);


int morse_yaps_get_tx_buffered_count(struct driver_data *driverd);
