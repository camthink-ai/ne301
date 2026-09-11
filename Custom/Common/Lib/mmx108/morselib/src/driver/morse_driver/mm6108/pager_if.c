/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include "pager_if.h"

#include "driver/driver.h"
#include "driver/morse_driver/hw.h"
#include "driver/morse_driver/morse.h"
#include "driver/morse_driver/skb_header.h"
#include "driver/morse_driver/skbq.h"
#include "driver/transport/morse_transport.h"

static uint32_t enabled_irqs;

int morse_pager_irq_enable(const struct morse_pager *pager, bool enable)
{
    if (enable)
    {
        enabled_irqs |= MORSE_PAGER_IRQ_MASK(pager->id);
    }
    else
    {
        enabled_irqs &= ~MORSE_PAGER_IRQ_MASK(pager->id);
    }

    return morse_hw_irq_enable(pager->driverd, pager->id, enable);
}

int morse_pager_tx_status_irq_enable(struct driver_data *driverd, bool enable)
{
    if (enable)
    {
        enabled_irqs |= MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE;
    }
    else
    {
        enabled_irqs &= ~MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE;
    }

    return morse_hw_irq_enable(driverd, MORSE_PAGER_BYPASS_TX_STATUS_IRQ_NUM, enable);
}

int morse_pager_cmd_resp_irq_enable(struct driver_data *driverd, bool enable)
{
    if (enable)
    {
        enabled_irqs |= MORSE_PAGER_IRQ_BYPASS_CMD_RESP_AVAILABLE;
    }
    else
    {
        enabled_irqs &= ~MORSE_PAGER_IRQ_BYPASS_CMD_RESP_AVAILABLE;
    }

    return morse_hw_irq_enable(driverd, MORSE_PAGER_BYPASS_CMD_RESP_IRQ_NUM, enable);
}

int morse_pager_irq_handler(struct driver_data *driverd, uint32_t status)
{
    int count;
    int ret;
    struct morse_pager *pager;
    struct morse_chip_if_state *chip_if = driverd->chip_if;
    bool rx_pend = false;
    bool tx_buffer_return_pend = false;
    bool is_tx_status_bypass = false;
    bool is_cmd_resp_bypass = false;


    status &= enabled_irqs;

    for (count = 0; count < chip_if->pager_count; count++)
    {
        if (!(status & MORSE_PAGER_IRQ_MASK(count)))
        {
            continue;
        }

        pager = &chip_if->pagers[count];

        if (pager->flags & MORSE_PAGER_FLAGS_POPULATED)
        {
            rx_pend |= true;
        }
        else
        {
            tx_buffer_return_pend |= true;
        }
    }


    is_tx_status_bypass = status & MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE;
    is_cmd_resp_bypass = status & MORSE_PAGER_IRQ_BYPASS_CMD_RESP_AVAILABLE;

    if (is_tx_status_bypass && chip_if->bypass.tx_status.location)
    {
        uint32_t page;

        ret = morse_trns_read_le32(driverd, chip_if->bypass.tx_status.location, &page);
        if (ret == 0)
        {
            for (int ii = 0; ii < MORSE_PAGER_BYPASS_TX_STATUS_FIFO_DEPTH; ii++)
            {
                if (chip_if->bypass.tx_status.to_process[ii] == 0)
                {
                    chip_if->bypass.tx_status.to_process[ii] = page;
                    break;
                }
            }

            rx_pend |= true;
        }
    }

    if (is_cmd_resp_bypass && chip_if->bypass.cmd_resp.location)
    {
        uint32_t page;

        ret = morse_trns_read_le32(driverd, chip_if->bypass.cmd_resp.location, &page);
        if (ret == 0)
        {
            for (int ii = 0; ii < MORSE_PAGER_BYPASS_CMD_RESP_FIFO_DEPTH; ii++)
            {
                if (chip_if->bypass.cmd_resp.to_process[ii] == 0)
                {
                    chip_if->bypass.cmd_resp.to_process[ii] = page;
                    break;
                }
            }

            rx_pend |= true;
        }
    }

    if (rx_pend || tx_buffer_return_pend)
    {
        if (rx_pend)
        {
            driver_task_notify_event(driverd, DRV_EVT_RX_PEND);
        }

        if (tx_buffer_return_pend)
        {
            driver_task_notify_event(driverd, DRV_EVT_PAGE_RETURN_PEND);
        }
    }
    return 0;
}

int morse_pager_init(struct driver_data *driverd,
                     struct morse_pager *pager,
                     int page_size,
                     uint8_t flags,
                     uint8_t id)
{
    pager->driverd = driverd;
    pager->flags = flags;
    pager->page_size_bytes = page_size;
    pager->parent = NULL;
    pager->id = id;

    return 0;
}

void morse_pager_finish(struct morse_pager *pager)
{
    MM_UNUSED(pager);
}
