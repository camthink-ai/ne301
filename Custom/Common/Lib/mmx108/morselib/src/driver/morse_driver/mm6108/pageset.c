/*
 * Copyright 2021-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>

#include "mmhal_wlan.h"
#include "mmpkt.h"
#include "mmpkt_list.h"

#include "pager_if.h"
#include "pager_if_hw.h"
#include "pageset.h"

#include "driver/driver.h"
#include "driver/morse_driver/morse.h"
#include "driver/morse_driver/ps.h"
#include "driver/morse_driver/skb_header.h"
#include "driver/transport/morse_transport.h"
#include "mmosal.h"


#ifndef MAX_PAGES_PER_TX_TXN
#define MAX_PAGES_PER_TX_TXN 16
#endif


#ifndef MAX_PAGES_PER_RX_TXN
#define MAX_PAGES_PER_RX_TXN 32
#endif

/** Retries when chip still owns the RX page (sync 0xBB) before host reads too early. */
#ifndef MORSE_PAGESET_CHIP_OWNED_MAX_RETRIES
#define MORSE_PAGESET_CHIP_OWNED_MAX_RETRIES 3
#endif
#ifndef MORSE_PAGESET_CHIP_OWNED_RETRY_MS
#define MORSE_PAGESET_CHIP_OWNED_RETRY_MS 20
#endif


#ifndef PAGE_RETURN_NOTIFY_INT
#define PAGE_RETURN_NOTIFY_INT 4
#endif

#ifdef ENABLE_PAGESET_TRACE
#include "mmtrace.h"
static mmtrace_channel pageset_channel_handle;
#define PAGESET_TRACE_INIT()                                          \
    do {                                                              \
        pageset_channel_handle = mmtrace_register_channel("pageset"); \
    } while (0)
#define PAGESET_TRACE(_fmt, ...) mmtrace_printf(pageset_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define PAGESET_TRACE_INIT() \
    do {                     \
    } while (0)
#define PAGESET_TRACE(_fmt, ...) \
    do {                         \
    } while (0)
#endif

static bool fifo_has_page(const struct page_fifo_hdr *hdr, const struct morse_page *page)
{
    const struct morse_page *fifo_data = hdr->pages;
    uint16_t offset = hdr->rd_offset;

    while (offset != hdr->wr_offset)
    {
        if (fifo_data[offset].addr == page->addr &&
            fifo_data[offset].size_bytes == page->size_bytes)
        {
            return true;
        }

        offset++;
        if (offset == hdr->slots)
        {
            offset = 0;
        }
    }

    return false;
}

static uint16_t fifo_is_empty(struct page_fifo_hdr *hdr)
{
    return (hdr->wr_offset == hdr->rd_offset);
}


static uint16_t fifo_len(struct page_fifo_hdr *hdr)
{
    if (hdr->wr_offset >= hdr->rd_offset)
    {
        return hdr->wr_offset - hdr->rd_offset;
    }
    else
    {
        return hdr->slots + hdr->wr_offset - hdr->rd_offset;
    }
}


static uint16_t fifo_size(struct page_fifo_hdr *hdr)
{
    return hdr->slots;
}

static int fifo_put(struct page_fifo_hdr *hdr, struct morse_page page)
{
    struct morse_page *fifo_data = hdr->pages;

    uint16_t new_wr_offset = hdr->wr_offset + 1;
    if (new_wr_offset >= hdr->slots)
    {
        new_wr_offset = 0;
    }

    if (new_wr_offset == hdr->rd_offset)
    {
        return 0;
    }

    fifo_data[hdr->wr_offset].addr = page.addr;
    fifo_data[hdr->wr_offset].size_bytes = page.size_bytes;

    hdr->wr_offset = new_wr_offset;

    return 1;
}

static int fifo_get(struct page_fifo_hdr *hdr, struct morse_page *page)
{
    struct morse_page *fifo_data = hdr->pages;

    if (fifo_is_empty(hdr))
    {
        return 0;
    }

    page->addr = fifo_data[hdr->rd_offset].addr;
    page->size_bytes = fifo_data[hdr->rd_offset].size_bytes;

    uint16_t new_rd_offset = hdr->rd_offset + 1;
    if (new_rd_offset >= hdr->slots)
    {
        new_rd_offset = 0;
    }
    hdr->rd_offset = new_rd_offset;

    return 1;
}

static int is_pageset_locked(struct morse_pageset *pageset)
{
    return atomic_test_bit(0, &pageset->access_lock);
}

static int pageset_lock(struct morse_pageset *pageset)
{
    if (atomic_test_and_set_bit_lock(0, &pageset->access_lock))
    {
        return -1;
    }
    return 0;
}

void pageset_unlock(struct morse_pageset *pageset)
{
    atomic_clear_bit_unlock(0, &pageset->access_lock);
}


static inline struct morse_skbq *skbq_pageset_tc_q_from_aci(struct driver_data *driverd, int aci)
{
    struct morse_pageset *pageset = driverd->chip_if->to_chip_pageset;

    if (!pageset)
    {
        return NULL;
    }

    if (aci >= (int)ARRAY_SIZE(pageset->data_qs))
    {
        return NULL;
    }

    return &pageset->data_qs[aci];
}

static inline struct morse_skbq *pageset2cmdq(struct morse_pageset *pageset)
{
    return &pageset->cmd_q;
}

static struct morse_skbq *skbq_pageset_cmd_tc_q(struct driver_data *driverd)
{
    return (driverd->chip_if->to_chip_pageset) ? &driverd->chip_if->to_chip_pageset->cmd_q : NULL;
}

static struct morse_skbq *skbq_pageset_bcn_tc_q(struct driver_data *driverd)
{
    return (driverd->chip_if->to_chip_pageset) ? &driverd->chip_if->to_chip_pageset->beacon_q :
                                                 NULL;
}

static struct morse_skbq *skbq_pageset_mgmt_tc_q(struct driver_data *driverd)
{
    return (driverd->chip_if->to_chip_pageset) ? &driverd->chip_if->to_chip_pageset->mgmt_q : NULL;
}

static void skbq_pageset_close(struct morse_skbq *mq)
{

    MM_UNUSED(mq);
}

static void skbq_pageset_get_tx_qs(struct driver_data *driverd, struct morse_skbq **qs, int *num_qs)
{
    struct morse_pageset *pageset = driverd->chip_if->to_chip_pageset;
    *qs = pageset->data_qs;
    *num_qs = PAGESET_TX_SKBQ_MAX;
}

static struct morse_skbq *skbq_pageset_get_rx_data_q(struct driver_data *driverd)
{

    const int rx_data_queue = 0;

    return (driverd->chip_if && driverd->chip_if->from_chip_pageset) ?
               &driverd->chip_if->from_chip_pageset->data_qs[rx_data_queue] :
               NULL;
}

static int morse_pageset_get_rx_buffered_count(struct driver_data *driverd)
{
    struct morse_skbq *skbq = skbq_pageset_get_rx_data_q(driverd);

    if (!skbq)
    {
        return 0;
    }

    return skbq->skbq.len;
}

const struct chip_if_ops morse_pageset_hw_ops = {
    .init = morse_pager_hw_pagesets_init,
    .flush_tx_data = morse_pager_hw_pagesets_flush_tx_data,
    .skbq_get_tx_buffered_count = morse_pagesets_get_tx_buffered_count,
    .finish = morse_pager_hw_pagesets_finish,
    .skbq_get_tx_qs = skbq_pageset_get_tx_qs,
    .skbq_close = skbq_pageset_close,
    .skbq_bcn_tc_q = skbq_pageset_bcn_tc_q,
    .skbq_mgmt_tc_q = skbq_pageset_mgmt_tc_q,
    .skbq_cmd_tc_q = skbq_pageset_cmd_tc_q,
    .skbq_tc_q_from_aci = skbq_pageset_tc_q_from_aci,
    .chip_if_handle_irq = morse_pager_irq_handler,
    .chip_if_work = morse_pagesets_work,
    .tx_stale_work = morse_pagesets_stale_tx_work,
};

static bool morse_pageset_page_is_cached(struct morse_pageset *pageset, struct morse_page *page)
{
    MORSE_WARN_ON(pageset == NULL || page == NULL);
    if (pageset == NULL || page == NULL)
    {
        return false;
    }

    if (fifo_has_page(&pageset->reserved_pages, page))
    {
        return true;
    }

    if (fifo_has_page(&pageset->cached_pages, page))
    {
        return true;
    }

    return false;
}

static void morse_pageset_to_chip_return_handler_no_lock(struct driver_data *driverd)
{
    struct morse_pageset *pageset = driverd->chip_if->to_chip_pageset;
    struct morse_pager *pager = pageset->return_pager;
    struct morse_page page;
    int ret;
    bool pager_empty = false;
    uint32_t popped = 0;

    uint32_t max_expected_pops = (fifo_size(&pageset->cached_pages) * 2);

    MORSE_WARN_ON(!is_pageset_locked(pageset));

    while (fifo_len(&pageset->reserved_pages) < CMD_RSVED_PAGES_MAX)
    {
        if (pager->ops->pop(pager, &page))
        {
            pager_empty = true;
            break;
        }

        popped++;
        if (morse_pageset_page_is_cached(pageset, &page))
        {
            continue;
        }

        ret = fifo_put(&pageset->reserved_pages, page);
        MORSE_WARN_ON(!ret);
    }

    if (pager_empty)
    {
        goto exit;
    }

    while (popped < max_expected_pops)
    {
        if (pager->ops->pop(pager, &page))
        {
            break;
        }

        popped++;
        if (morse_pageset_page_is_cached(pageset, &page))
        {
            continue;
        }

        ret = fifo_put(&pageset->cached_pages, page);
        MORSE_WARN_ON(!ret);
    }

exit:
    if (popped)
    {
        pager->ops->notify(pager);
    }
}

static void morse_pageset_to_chip_return_handler(struct driver_data *driverd, bool have_lock)
{
    struct morse_pageset *pageset = driverd->chip_if->to_chip_pageset;

    if (!have_lock)
    {
        int ret = 0;

        ret = pageset_lock(pageset);
        if (ret)
        {
            MMLOG_WRN("Pageset lock failed %d\n", ret);
            return;
        }
    }

    morse_pageset_to_chip_return_handler_no_lock(driverd);

    if (!have_lock)
    {
        pageset_unlock(pageset);
    }
}

#define BCN_LOSS_CHECK     (500)
#define BCN_LOSS_THRESHOLD (50)

static unsigned int bcn_page_get;
static unsigned int bcn_page_fail;


static void morse_pageset_bcn_loss_monitor()
{
    if (bcn_page_fail > BCN_LOSS_THRESHOLD)
    {
        MMLOG_WRN("%s failed to send %u of %u beacons\n", __func__, bcn_page_fail, bcn_page_get);
    }
    bcn_page_get = 0;
    bcn_page_fail = 0;
}

static bool morse_pageset_rsved_page_is_avail(struct morse_pageset *pageset,
                                              uint8_t channel,
                                              bool have_lock)
{
    struct driver_data *driverd = pageset->driverd;

    switch (channel)
    {
        case MORSE_SKB_CHAN_BEACON:
            bcn_page_get++;
            if (bcn_page_get == BCN_LOSS_CHECK)
            {
                morse_pageset_bcn_loss_monitor();
            }

            if (fifo_len(&pageset->reserved_pages) <= 1)
            {
                bcn_page_fail++;
                MMLOG_WRN("No page available for beacon\n");
                return false;
            }
            return true;

        case MORSE_SKB_CHAN_COMMAND:

            if (fifo_is_empty(&pageset->reserved_pages))
            {
                morse_pageset_to_chip_return_handler(driverd, have_lock);
                if (fifo_is_empty(&pageset->reserved_pages))
                {
                    MMLOG_ERR("%s unexpected command page exhaustion\n", __func__);
                }
                else
                {
                    MMLOG_DBG("%s got command page on second attempt\n", __func__);
                }
            }
            return (!fifo_is_empty(&pageset->reserved_pages));
    }

    return 0;
}

static int morse_pageset_write(struct morse_pageset *pageset, struct mmpkt *mmpkt)
{
    int ret = 0;
    bool from_rsvd = false;
    struct morse_pager *populated_pager = pageset->populated_pager;
    struct morse_page page;
    struct mmpktview *view;
    struct morse_buff_skb_header *hdr;

    ret = pageset_lock(pageset);
    if (ret)
    {
        MMLOG_WRN("Pageset lock failed %d\n", ret);
        return ret;
    }

    view = mmpkt_open(mmpkt);
    hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);

    if (morse_pageset_rsved_page_is_avail(pageset, hdr->channel, true))
    {
        ret = fifo_get(&pageset->reserved_pages, &page);
        from_rsvd = true;
    }
    else
    {
        ret = fifo_get(&pageset->cached_pages, &page);
    }

    if (ret <= 0)
    {
        MMLOG_ERR("No pages available\n");
        ret = -ENOSPC;
        goto exit;
    }

    if (mmpkt_get_data_length(view) > page.size_bytes)
    {
        MMLOG_ERR("%s Data larger than pagesize: [%lu:%lu]\n",
                  __func__,
                  mmpkt_get_data_length(view),
                  page.size_bytes);
        ret = -ENOSPC;
        goto exit;
    }

    ret = populated_pager->ops->write_page(populated_pager,
                                           &page,
                                           0,
                                           mmpkt_get_data_start(view),
                                           mmpkt_get_data_length(view));
    if (ret)
    {
        MMLOG_ERR("Failed to write page: %d\n", ret);

        if (from_rsvd)
        {
            fifo_put(&pageset->reserved_pages, page);
        }
        else
        {
            fifo_put(&pageset->cached_pages, page);
        }

        goto exit;
    }


    ret = populated_pager->ops->put(populated_pager, &page);
    if (ret)
    {
        MMLOG_ERR("%s failed to return page: %d\n", __func__, ret);

        hdr->sync = 0;
        populated_pager->ops->write_page(populated_pager,
                                         &page,
                                         0,
                                         (const uint8_t *)hdr,
                                         sizeof(*hdr));
        populated_pager->ops->put(populated_pager, &page);
        goto exit;
    }

exit:
    mmpkt_close(&view);
    pageset_unlock(pageset);
    return ret;
}


static int morse_pageset_get_next_page(struct morse_pager *populated_pager,
                                       struct morse_chip_if_state *chip_if,
                                       struct morse_page *page,
                                       enum mmhal_wlan_pkt_class *pkt_class)
{

    for (int ii = 0; ii < MORSE_PAGER_BYPASS_CMD_RESP_FIFO_DEPTH; ii++)
    {
        if (chip_if->bypass.cmd_resp.to_process[ii] != 0)
        {
            page->addr = chip_if->bypass.cmd_resp.to_process[ii];
            page->size_bytes = populated_pager->page_size_bytes;
            chip_if->bypass.cmd_resp.to_process[ii] = 0;
            PAGESET_TRACE("got cmd_resp page");
            *pkt_class = MMHAL_WLAN_PKT_COMMAND;
            return MORSE_SUCCESS;
        }
    }

    for (int ii = 0; ii < MORSE_PAGER_BYPASS_TX_STATUS_FIFO_DEPTH; ii++)
    {
        if (chip_if->bypass.tx_status.to_process[ii] != 0)
        {
            page->addr = chip_if->bypass.tx_status.to_process[ii];
            page->size_bytes = populated_pager->page_size_bytes;
            chip_if->bypass.tx_status.to_process[ii] = 0;
            PAGESET_TRACE("got tx status page");
            *pkt_class = MMHAL_WLAN_PKT_COMMAND;
            return MORSE_SUCCESS;
        }
    }


    int ret = populated_pager->ops->pop(populated_pager, page);
    if (ret)
    {
        return ret;
    }
    *pkt_class = MMHAL_WLAN_PKT_DATA_TID0;
    return MORSE_SUCCESS;
}

static int morse_pageset_read(struct morse_pageset *pageset)
{
    struct driver_data *driverd = pageset->driverd;
    struct mmpkt *mmpkt = NULL;
    struct mmpktview *view;
    struct morse_pager *return_pager = pageset->return_pager;
    struct morse_pager *populated_pager = pageset->populated_pager;
    struct morse_chip_if_state *chip_if = driverd->chip_if;
    struct morse_page page = { .addr = 0, .size_bytes = 0 };
    struct morse_buff_skb_header *hdr;
    uint32_t mmpkt_len;
    int ret;
    uint8_t *buf;
    const int max_checksum_rounds = 2;
    int count = 0;
    bool checksum_valid = !(driverd->chip_if->validate_skb_checksum);
    enum mmhal_wlan_pkt_class pkt_class = MMHAL_WLAN_PKT_DATA_TID0;

    ret = morse_pageset_get_next_page(populated_pager, chip_if, &page, &pkt_class);
    if (ret != MORSE_SUCCESS)
    {

        page.addr = 0;
        goto exit;
    }

    mmpkt_len = FAST_ROUND_UP(page.addr >> 20, 4);
    page.addr = ((page.addr & 0xFFFFF) | 0x80100000);


    mmpkt = mmhal_wlan_alloc_mmpkt_for_rx(pkt_class, mmpkt_len, sizeof(struct mmdrv_rx_metadata));
    if (!mmpkt)
    {
        ret = -ENOMEM;
        mmdrv_host_stats_increment_datapath_driver_rx_alloc_failures();
        goto exit;
    }

    view = mmpkt_open(mmpkt);
    buf = mmpkt_append(view, mmpkt_len);
    mmpkt_close(&view);


    ret = populated_pager->ops->read_page(populated_pager, &page, 0, buf, mmpkt_len);

    if (ret)
    {
        MMLOG_ERR("Failed to read page: %d\n", ret);
        mmdrv_host_stats_increment_datapath_driver_rx_read_failures();
        goto exit;
    }

    mmdrv_get_rx_metadata(mmpkt)->read_timestamp_ms = mmosal_get_time_ms();

    hdr = (struct morse_buff_skb_header *)buf;

    {
        int chip_owned_round = 0;

        while (hdr->sync == MORSE_SKB_HEADER_CHIP_OWNED_SYNC &&
               chip_owned_round < MORSE_PAGESET_CHIP_OWNED_MAX_RETRIES)
        {
            chip_owned_round++;
            if ((chip_owned_round % 4) == 0)
            {
                mmosal_task_yield();
            }
            mmosal_task_sleep(MORSE_PAGESET_CHIP_OWNED_RETRY_MS);
            ret = populated_pager->ops->read_page(populated_pager, &page, 0, buf, mmpkt_len);
            if (ret)
            {
                MMLOG_ERR("Failed to re-read chip-owned page: %d\n", ret);
                mmdrv_host_stats_increment_datapath_driver_rx_read_failures();
                goto exit;
            }
            hdr = (struct morse_buff_skb_header *)buf;
        }

        if (hdr->sync == MORSE_SKB_HEADER_SYNC && chip_owned_round > 0)
        {
            MMLOG_VRB("%s chip-owned page ready after %d retries\n",
                      __func__,
                      chip_owned_round);
        }
    }

    if (hdr->sync != MORSE_SKB_HEADER_SYNC)
    {
        bool chip_owned = (hdr->sync == MORSE_SKB_HEADER_CHIP_OWNED_SYNC);

        if (chip_owned)
        {
            int put_ret = populated_pager->ops->put(populated_pager, &page);

            MMLOG_VRB("%s chip-owned defer page[addr:0x%08lx len:%u] put=%d\n",
                      __func__,
                      page.addr,
                      hdr->len,
                      put_ret);
            page.addr = 0;
            ret = 0;
            goto exit;
        }

        MMLOG_WRN("%s sync error:0x%02X page[addr:0x%08lx len:%u]\n",
                  __func__,
                  hdr->sync,
                  page.addr,
                  hdr->len);

        ret = 0;
        mmdrv_host_stats_increment_datapath_driver_rx_read_failures();
        goto exit;
    }

    if (sizeof(*hdr) + hdr->len > mmpkt_len)
    {
        MMLOG_INF("rx page too short. dropping\n");
        ret = -EIO;
        mmdrv_host_stats_increment_datapath_driver_rx_read_failures();
        goto exit;
    }

    while (!checksum_valid && count < max_checksum_rounds)
    {
        checksum_valid = morse_validate_skb_checksum(buf);
        if (checksum_valid)
        {
            break;
        }

        if (hdr->channel != MORSE_SKB_CHAN_TX_STATUS)
        {
            break;
        }
        ret = populated_pager->ops->read_page(populated_pager, &page, 0, buf, mmpkt_len);
        if (ret)
        {
            break;
        }
        count++;
    }

    if (!checksum_valid)
    {
        MMLOG_WRN("%s: SKB checksum is invalid, page:[a:0x%08lx len:%lu] hdr:[c:%02X s:%02X]",
                  __func__,
                  page.addr,
                  mmpkt_len,
                  hdr->channel,
                  hdr->sync);
        mmdrv_host_stats_increment_datapath_driver_rx_read_failures();
        goto exit;
    }

    if (hdr->offset > 3)
    {
        MMLOG_ERR("%s: corrupted mmpkt header offset [offset=%u], hdr.len %u, page addr: 0x%08lx\n",
                  __func__,
                  hdr->offset,
                  hdr->len,
                  page.addr);


        hdr->offset = (hdr->len & 0x03) ? (4 - (uint32_t)(hdr->len & 3)) : 0;
    }


    mmpkt_len = sizeof(*hdr) + le16toh(hdr->len);
    mmpkt_truncate(mmpkt, mmpkt_len);

    morse_skbq_process_rx(driverd, mmpkt);
    mmpkt = NULL;

exit:
    if (mmpkt)
    {
        mmpkt_release(mmpkt);
    }

    if (page.addr)
    {

        ret = return_pager->ops->put(return_pager, &page);
        if (ret)
        {
            MMLOG_ERR("page ret fail\n");
        }
    }

    return ret;
}


static int morse_pageset_num_pages(struct morse_pageset *pageset, struct mmpkt *mmpkt)
{
    struct mmpktview *view = mmpkt_open(mmpkt);
    struct morse_buff_skb_header *hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
    int num_pages = 0;

    if (hdr->channel == MORSE_SKB_CHAN_COMMAND)
    {
        num_pages =
            min(CMD_RSVED_CMD_PAGES_MAX,
                (int)fifo_len(&pageset->reserved_pages) + (int)fifo_len(&pageset->cached_pages));
    }
    else
    {
        if (morse_pageset_rsved_page_is_avail(pageset, hdr->channel, false))
        {
            num_pages = (CMD_RSVED_PAGES_MAX - CMD_RSVED_CMD_PAGES_MAX);
        }


        num_pages = min(MAX_PAGES_PER_TX_TXN, num_pages + (int)fifo_len(&pageset->cached_pages));
    }

    if (num_pages == 0 && (hdr->channel & (MORSE_SKB_CHAN_BEACON | MORSE_SKB_CHAN_COMMAND)))
    {
        MMLOG_INF("%s: No pages left for mmpkt channel %d\n", __func__, hdr->channel);
    }

    mmpkt_close(&view);
    return num_pages;
}

static void morse_pageset_tx(struct morse_pageset *pageset, struct morse_skbq *mq)
{
    int ret = 0;
    int num_pages;
    int num_items = 0;
    int bytes_sent = 0;
    struct mmpkt *mmpkt;
    struct mmpkt_list skbq_to_send = MMPKT_LIST_INIT;
    struct mmpkt_list skbq_sent = MMPKT_LIST_INIT;
    struct mmpkt_list skbq_failed = MMPKT_LIST_INIT;
    struct mmpkt *pfirst, *pnext;

    spin_lock(&mq->lock);
    mmpkt = mmpkt_list_peek(&mq->skbq);
    if (mmpkt)
    {
        num_pages = morse_pageset_num_pages(pageset, mmpkt);
    }
    spin_unlock(&mq->lock);

    if (!mmpkt)
    {
        return;
    }


    if (mq == &pageset->cmd_q)
    {
        morse_skbq_purge(mq, &mq->pending);
    }

    if (num_pages > 0)
    {
        num_items = morse_skbq_deq_num_items(mq, &skbq_to_send, num_pages);
    }

    MMPKT_LIST_WALK(&skbq_to_send, pfirst, pnext)
    {
        if (num_pages)
        {
            PAGESET_TRACE("tx skb %x", pfirst);
            ret = morse_pageset_write(pageset, pfirst);
        }
        else
        {
            MMLOG_ERR("No pages available\n");
            ret = -ENOSPC;
        }
        morse_hw_pager_update_consec_failure_cnt(pageset->driverd, ret);
        if (ret == 0)
        {
            num_pages--;
            mmpkt_list_remove(&skbq_to_send, pfirst);
            mmpkt_list_append(&skbq_sent, pfirst);
            bytes_sent += mmpkt_peek_data_length(pfirst);
        }
        else
        {
            PAGESET_TRACE("tx skb failed %x", pfirst);
            mmpkt_list_remove(&skbq_to_send, pfirst);
            mmpkt_list_append(&skbq_failed, pfirst);
        }
    }

    if (skbq_failed.len > 0)
    {
        MMLOG_ERR("%s could not write %lu pkts - rc=%d items=%d pages=%d\n",
                  __func__,
                  skbq_failed.len,
                  ret,
                  num_items,
                  num_pages);
        if (mq == &pageset->cmd_q)
        {
            morse_skbq_purge(mq, &skbq_failed);
        }
        else
        {
            morse_skbq_tx_failed(mq, &skbq_failed);
        }
    }

    if (skbq_sent.len > 0)
    {
        morse_skbq_tx_complete(mq, &skbq_sent);
    }

    if (bytes_sent)
    {
        pageset->populated_pager->ops->notify(pageset->populated_pager);
    }
}


static bool morse_pageset_tx_data_handler(struct morse_pageset *pageset)
{
    int16_t aci;
    uint32_t count = 0;
    struct driver_data *driverd = pageset->driverd;

    for (aci = MORSE_ACI_VO; aci >= 0; aci--)
    {
        struct morse_skbq *data_q = skbq_pageset_tc_q_from_aci(driverd, aci);

        if (!driver_is_data_tx_allowed(driverd))
        {
            break;
        }

        morse_pageset_tx(pageset, data_q);

        count += morse_skbq_count(data_q);

        if (aci == MORSE_ACI_BE)
        {
            break;
        }
    }

    return ((count > 0) && driver_is_data_tx_allowed(driverd));
}


static bool morse_pageset_tx_cmd_handler(struct morse_pageset *pageset)
{
    struct morse_skbq *cmd_q = pageset2cmdq(pageset);

    morse_pageset_tx(pageset, cmd_q);

    return (morse_skbq_count(cmd_q) > 0);
}

static bool morse_pageset_tx_beacon_handler(struct morse_pageset *pageset)
{
    struct morse_skbq *beacon_q = &pageset->beacon_q;

    morse_pageset_tx(pageset, beacon_q);

    return (morse_skbq_count(beacon_q) > 0);
}

static bool morse_pageset_tx_mgmt_handler(struct morse_pageset *pageset)
{
    struct morse_skbq *mgmt_q = &pageset->mgmt_q;

    morse_pageset_tx(pageset, mgmt_q);

    return (morse_skbq_count(mgmt_q) > 0);
}


static bool morse_pageset_rx_handler(struct morse_pageset *pageset)
{
    int ret = 0;
    int count = 0;
    bool return_notify_req = false;

    MORSE_WARN_ON(is_pageset_locked(pageset));


    do {
        ret = morse_pageset_read(pageset);
        morse_hw_pager_update_consec_failure_cnt(pageset->driverd, ret);
        count++;
        return_notify_req = true;
        if ((count % PAGE_RETURN_NOTIFY_INT) == 0)
        {
            pageset->return_pager->ops->notify(pageset->return_pager);
            return_notify_req = false;
        }
    } while ((count < MAX_PAGES_PER_RX_TXN) && (ret == 0));

    if (return_notify_req)
    {
        pageset->return_pager->ops->notify(pageset->return_pager);
    }

    pageset->populated_pager->ops->notify(pageset->populated_pager);

    if (ret == -ENOMEM || count == MAX_PAGES_PER_RX_TXN)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void morse_pagesets_stale_tx_work(struct driver_data *driverd)
{
    size_t i;
    int flushed = 0;
    struct morse_pageset *tx_pageset;

    bool pending = driver_task_notification_check_and_clear(driverd, DRV_EVT_STALE_TX_STATUS_PEND);

    if (!pending ||
        !driverd->chip_if ||
        !driverd->chip_if->to_chip_pageset ||
        !driverd->stale_status.enabled)
    {
        return;
    }

    tx_pageset = driverd->chip_if->to_chip_pageset;
    flushed += morse_skbq_check_for_stale_tx(&tx_pageset->beacon_q);
    flushed += morse_skbq_check_for_stale_tx(&tx_pageset->mgmt_q);

    for (i = 0; i < ARRAY_SIZE(tx_pageset->data_qs); i++)
    {
        flushed += morse_skbq_check_for_stale_tx(&tx_pageset->data_qs[i]);
    }

    if (flushed)
    {
        MMLOG_DBG("Flushed %d stale TX SKBs\n", flushed);
        PAGESET_TRACE("flushed %u", flushed);

        if (!driverd->ps.suspended && (morse_pagesets_get_tx_buffered_count(driverd) == 0))
        {

            driver_task_notify_event(driverd, DRV_EVT_PS_DELAYED_EVAL_PEND);
        }
    }
}

void morse_pagesets_work(struct driver_data *driverd)
{
    bool network_activity = false;

    MMLOG_VRB("Pageset work %s %08lx\n",
              driver_task_notification_is_pending(driverd, DRV_EVT_MASK_PAGESET) ? "pending" :
                                                                                   "no pending",
              driverd->driver_task.pending_evts);
    if (!driver_task_notification_is_pending(driverd, DRV_EVT_MASK_PAGESET))
    {
        return;
    }


    morse_ps_disable(driverd, PS_WAKER_PAGESET);
    morse_trns_claim(driverd);


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_RX_PEND))
    {
        int buffered = morse_pageset_get_rx_buffered_count(driverd);

        if (morse_pageset_rx_handler(driverd->chip_if->from_chip_pageset))
        {
            driver_task_notify_event(driverd, DRV_EVT_RX_PEND);
        }

        if (morse_pageset_get_rx_buffered_count(driverd) > buffered)
        {
            network_activity = true;
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_PAGE_RETURN_PEND))
    {
        MMLOG_VRB("Page return pending\n");
        morse_pageset_to_chip_return_handler(driverd, false);
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_COMMAND_PEND))
    {
        MMLOG_INF("TX command pending\n");
        if (morse_pageset_tx_cmd_handler(driverd->chip_if->to_chip_pageset))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_COMMAND_PEND, 10);
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_BEACON_PEND))
    {
        if (morse_pageset_tx_beacon_handler(driverd->chip_if->to_chip_pageset))
        {
            driver_task_schedule_notification(driverd, DRV_EVT_TX_BEACON_PEND, 1);
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_MGMT_PEND))
    {
        network_activity = true;
        if (morse_pageset_tx_mgmt_handler(driverd->chip_if->to_chip_pageset))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_MGMT_PEND, 2);
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TRAFFIC_PAUSE_PEND))
    {
        if (driver_task_notification_check(driverd, DRV_EVT_TRAFFIC_PAUSE_PEND))
        {
            MMLOG_DBG("Latency to handle traffic pause is too great\n");
        }
        morse_skbq_data_traffic_pause(driverd);
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TRAFFIC_RESUME_PEND))
    {
        if (driver_task_notification_check(driverd, DRV_EVT_TRAFFIC_RESUME_PEND))
        {
            MMLOG_DBG("Latency to handle traffic resume is too great\n");
        }
        morse_skbq_data_traffic_resume(driverd);
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_DATA_PEND))
    {
        network_activity = true;
        if (morse_pageset_tx_data_handler(driverd->chip_if->to_chip_pageset))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_DATA_PEND, 2);
        }
    }

    if (network_activity)
    {
        morse_ps_network_activity(driverd);
    }

    morse_trns_release(driverd);
    morse_ps_enable(driverd, PS_WAKER_PAGESET);
}

int morse_pageset_init(struct driver_data *driverd,
                       struct morse_pageset *pageset,
                       uint8_t flags,
                       struct morse_pager *populated_pager,
                       struct morse_pager *return_pager)
{
    size_t i;

    PAGESET_TRACE_INIT();

    driverd->pageset_consec_failure_cnt = 0;

    pageset->driverd = driverd;
    pageset->flags = flags;
    pageset->populated_pager = populated_pager;
    pageset->return_pager = return_pager;
    driverd->chip_if->active_chip_if = MORSE_CHIP_IF_PAGESET;

    INIT_PAGE_FIFO(pageset->reserved_pages, CMD_RSVED_FIFO_LEN);
    INIT_PAGE_FIFO(pageset->cached_pages, CACHED_PAGES_FIFO_LEN);
    if (pageset->flags & MORSE_CHIP_IF_FLAGS_DATA)
    {
        morse_skbq_init(driverd,
                        pageset->flags & MORSE_PAGER_FLAGS_DIR_TO_HOST,
                        &pageset->beacon_q,
                        MORSE_CHIP_IF_FLAGS_DATA);

        morse_skbq_init(driverd,
                        pageset->flags & MORSE_PAGER_FLAGS_DIR_TO_HOST,
                        &pageset->mgmt_q,
                        MORSE_CHIP_IF_FLAGS_DATA);

        for (i = 0; i < ARRAY_SIZE(pageset->data_qs); i++)
        {
            morse_skbq_init(driverd,
                            pageset->flags & MORSE_PAGER_FLAGS_DIR_TO_HOST,
                            &pageset->data_qs[i],
                            MORSE_CHIP_IF_FLAGS_DATA);
        }
    }

    if (pageset->flags & MORSE_CHIP_IF_FLAGS_COMMAND)
    {
        morse_skbq_init(driverd,
                        pageset->flags & MORSE_PAGER_FLAGS_DIR_TO_HOST,
                        &pageset->cmd_q,
                        MORSE_CHIP_IF_FLAGS_COMMAND);
    }

    populated_pager->parent = pageset;
    return_pager->parent = pageset;

    return 0;
}

void morse_pageset_finish(struct morse_pageset *pageset)
{
    size_t i;

    pageset->return_pager = NULL;
    pageset->populated_pager = NULL;

    if (pageset->flags & MORSE_CHIP_IF_FLAGS_DATA)
    {
        morse_skbq_finish(&pageset->beacon_q);
        morse_skbq_finish(&pageset->mgmt_q);
        for (i = 0; i < ARRAY_SIZE(pageset->data_qs); i++)
        {
            morse_skbq_finish(&pageset->data_qs[i]);
        }
    }

    if (pageset->flags & MORSE_CHIP_IF_FLAGS_COMMAND)
    {
        morse_skbq_finish(&pageset->cmd_q);
    }
}

void morse_pageset_flush_tx_data(struct morse_pageset *pageset)
{
    size_t i;

    if (!(pageset->flags & (MORSE_CHIP_IF_FLAGS_DATA | MORSE_CHIP_IF_FLAGS_BEACON)) ||
        !(pageset->flags & MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP))
    {
        MMLOG_WRN("Invalid pager\n");
        return;
    }

    morse_skbq_tx_flush(&pageset->beacon_q);
    morse_skbq_tx_flush(&pageset->mgmt_q);

    for (i = 0; i < ARRAY_SIZE(pageset->data_qs); i++)
    {
        morse_skbq_tx_flush(&pageset->data_qs[i]);
    }
}

int morse_pagesets_get_tx_buffered_count(struct driver_data *driverd)
{
    size_t i;
    int count = 0;
    struct morse_pageset *tx_pageset;

    if (!driverd->chip_if || !driverd->chip_if->to_chip_pageset)
    {
        return 0;
    }

    tx_pageset = driverd->chip_if->to_chip_pageset;
    if (!(tx_pageset->flags & MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP))
    {
        MMLOG_WRN("Invalid pager\n");
        return 0;
    }

    count += tx_pageset->beacon_q.skbq.len + tx_pageset->beacon_q.pending.len;
    count += tx_pageset->mgmt_q.skbq.len + tx_pageset->mgmt_q.pending.len;
    count += tx_pageset->cmd_q.skbq.len + tx_pageset->cmd_q.pending.len;

    for (i = 0; i < ARRAY_SIZE(tx_pageset->data_qs); i++)
    {
        count +=
            morse_skbq_count_tx_ready(&tx_pageset->data_qs[i]) + tx_pageset->data_qs[i].pending.len;
    }

    return count;
}
