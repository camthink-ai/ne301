/*
 * Copyright 2017-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "mmpkt_list.h"
#include "mmpkt.h"
#include "skb_header.h"

#include "driver/shim/atomic.h"

struct morse_skbq
{
    uint32_t pkt_seq;
    uint16_t flags;
    struct spinlock lock;
    struct driver_data *driverd;
    struct mmpkt_list skbq;
    struct mmpkt_list pending;
};

int morse_skbq_purge(struct morse_skbq *mq, struct mmpkt_list *skbq);

int morse_skbq_deq_num_items(struct morse_skbq *mq, struct mmpkt_list *skbq, int num_items);

struct mmpkt *morse_skbq_alloc_mmpkt_for_cmd(uint32_t length);

int morse_skbq_mmpkt_tx(struct morse_skbq *mq, struct mmpkt *mmpkt, uint8_t channel);

int morse_skbq_tx_complete(struct morse_skbq *mq, struct mmpkt_list *skbq);

int morse_skbq_tx_finish(struct morse_skbq *mq,
                         struct mmpkt *mmpkt,
                         struct morse_skb_tx_status *tx_sts);

struct mmpkt *morse_skbq_tx_pending(struct morse_skbq *mq);

void morse_skbq_init(struct driver_data *driverd,
                     bool from_chip,
                     struct morse_skbq *mq,
                     uint16_t flags);

void morse_skbq_finish(struct morse_skbq *mq);


int morse_skbq_tx_flush(struct morse_skbq *mq);


int morse_skbq_check_for_stale_tx(struct morse_skbq *mq);


int morse_skbq_get_tx_status_lifetime_ms(void);


uint32_t morse_skbq_count_tx_ready(struct morse_skbq *mq);


uint32_t morse_skbq_count(struct morse_skbq *mq);


void morse_skbq_data_traffic_pause(struct driver_data *driverd);


void morse_skbq_data_traffic_resume(struct driver_data *driverd);


bool morse_validate_skb_checksum(uint8_t *data);


void morse_skbq_process_rx(struct driver_data *driverd, struct mmpkt *mmpkt);


int morse_skbq_drop_stale_pkts(struct morse_skbq *mq);
