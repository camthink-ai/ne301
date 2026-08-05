/*
 * Copyright 2017-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "driver/morse_driver/skbq.h"



#define MORSE_PAGER_FLAGS_DIR_TO_HOST              (1 << 0)
#define MORSE_PAGER_FLAGS_DIR_TO_CHIP              (1 << 1)
#define MORSE_PAGER_FLAGS_FREE                     (1 << 2)
#define MORSE_PAGER_FLAGS_POPULATED                (1 << 3)

#define MORSE_PAGER_INT_STS(driverd)               MORSE_REG_INT1_STS(driverd)
#define MORSE_PAGER_INT_EN(driverd)                MORSE_REG_INT1_EN(driverd)
#define MORSE_PAGER_INT_SET(driverd)               MORSE_REG_INT1_SET(driverd)
#define MORSE_PAGER_INT_CLR(driverd)               MORSE_REG_INT1_CLR(driverd)

#define MORSE_PAGER_TRGR_STS(driverd)              MORSE_REG_TRGR1_STS(driverd)
#define MORSE_PAGER_TRGR_EN(driverd)               MORSE_REG_TRGR1_EN(driverd)
#define MORSE_PAGER_TRGR_SET(driverd)              MORSE_REG_TRGR1_SET(driverd)
#define MORSE_PAGER_TRGR_CLR(driverd)              MORSE_REG_TRGR1_CLR(driverd)

#define MORSE_PAGER_IRQ_MASK(ID)                   (1 << (ID))

#define MORSE_PAGER_BYPASS_TX_STATUS_IRQ_NUM       (15)
#define MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE BIT(MORSE_PAGER_BYPASS_TX_STATUS_IRQ_NUM)
#define MORSE_PAGER_BYPASS_TX_STATUS_FIFO_DEPTH    (4)

#define MORSE_PAGER_BYPASS_CMD_RESP_IRQ_NUM        (29)
#define MORSE_PAGER_IRQ_BYPASS_CMD_RESP_AVAILABLE  BIT(MORSE_PAGER_BYPASS_CMD_RESP_IRQ_NUM)
#define MORSE_PAGER_BYPASS_CMD_RESP_FIFO_DEPTH     (2)

struct driver_data;
struct morse_pageset;
struct morse_page;

struct morse_pager
{
    struct driver_data *driverd;
    struct morse_skbq mq;


    struct morse_pageset *parent;

    uint8_t id;
    uint8_t flags;
    int num_pages;
    int page_size_bytes;


    const struct morse_pager_ops *ops;
    void *aux_data;
};

struct morse_pager_ops
{

    int (*put)(struct morse_pager *pager, struct morse_page *page);


    int (*pop)(struct morse_pager *pager, struct morse_page *page);


    int (*notify)(const struct morse_pager *pager);


    int (*write_page)(struct morse_pager *pager,
                      struct morse_page *page,
                      int offset,
                      const uint8_t *buf,
                      uint32_t num_bytes);


    int (*read_page)(struct morse_pager *pager,
                     struct morse_page *page,
                     int offset,
                     uint8_t *buf,
                     uint32_t num_bytes);
};


int morse_pager_init(struct driver_data *driverd,
                     struct morse_pager *pager,
                     int page_size,
                     uint8_t flags,
                     uint8_t id);


void morse_pager_finish(struct morse_pager *pager);


int morse_pager_irq_enable(const struct morse_pager *pager, bool enable);


int morse_pager_tx_status_irq_enable(struct driver_data *driverd, bool enable);


int morse_pager_cmd_resp_irq_enable(struct driver_data *driverd, bool enable);


int morse_pager_irq_handler(struct driver_data *driverd, uint32_t status);
