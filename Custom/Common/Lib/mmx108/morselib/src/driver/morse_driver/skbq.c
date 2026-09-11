/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#include <errno.h>

#include "mmpkt.h"
#include "mmpkt_list.h"
#include "morse.h"
#include "skbq.h"
#include "command.h"
#include "skb_header.h"
#include "dot11/dot11.h"
#include "dot11/dot11_frames.h"
#include "driver/driver.h"
#include "mmhal_wlan.h"

#ifdef ENABLE_SKBQ_TRACE
#include "mmtrace.h"
static mmtrace_channel skbq_channel_handle;
#define SKBQ_TRACE_INIT()                                       \
    do {                                                        \
        skbq_channel_handle = mmtrace_register_channel("skbq"); \
    } while (0)
#define SKBQ_TRACE(_fmt, ...) mmtrace_printf(skbq_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define SKBQ_TRACE_INIT() \
    do {                  \
    } while (0)
#define SKBQ_TRACE(_fmt, ...) \
    do {                      \
    } while (0)
#endif


#define DATA_FRAME_CHECKSUM_DATA_LEN (40)
MM_STATIC_ASSERT(DATA_FRAME_CHECKSUM_DATA_LEN == sizeof(struct dot11_data_hdr) +
                                                     sizeof(struct dot11_qos_ctrl) +
                                                     DOT11_CCMP_HEADER_LEN,
                 "Firmware expectation");


static uint32_t tx_status_lifetime_ms = (15 * 1000);

static int __skbq_data_tx_finish(struct mmpkt_list *skbq,
                                 struct mmpkt *mmpkt,
                                 struct morse_skb_tx_status *tx_status);

static struct mmpkt *__skbq_get_pending_by_id(struct morse_skbq *mq, uint32_t pkt_id);

static uint32_t get_timeout_from_tx_mmpkt(struct mmpkt *mmpkt)
{
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);
    return tx_metadata->timeout_abs_ms;
}

static inline uint32_t __morse_skbq_size(const struct morse_skbq *mq)
{
    return mq->skbq_size;
}

static inline uint32_t __morse_skbq_space(const struct morse_skbq *mq)
{
    return MORSE_SKBQ_SIZE - __morse_skbq_size(mq);
}

static int __morse_skbq_put(struct morse_skbq *mq, struct mmpkt *mmpkt)
{
    uint32_t data_length = mmpkt_peek_data_length(mmpkt);
    if (data_length > __morse_skbq_space(mq))
    {
        MMLOG_WRN("Morse SKBQ out of memory %lu:%lu:%lu\n",
                  data_length,
                  __morse_skbq_space(mq),
                  mq->skbq_size);
        return -ENOMEM;
    }
    mmpkt_list_append(&mq->skbq, mmpkt);
    mq->skbq_size += data_length;
    return 0;
}

static void __morse_skbq_pkt_id(struct morse_skbq *mq, struct mmpkt *mmpkt)
{
    struct mmpktview *view = mmpkt_open(mmpkt);
    struct morse_buff_skb_header *hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);

    hdr->tx_info.pkt_id = htole32(mq->pkt_seq++);

    mmpkt_close(&view);
}

static struct morse_skbq *__morse_skbq_match_tx_status_to_skbq(
    struct driver_data *driverd,
    const struct morse_skb_tx_status *tx_sts)
{
    struct morse_skbq *mq = NULL;

    switch (tx_sts->channel)
    {
        case MORSE_SKB_CHAN_DATA:
        case MORSE_SKB_CHAN_DATA_NOACK:
        case MORSE_SKB_CHAN_LOOPBACK:
        {
            int aci = dot11_tid_to_ac(tx_sts->tid);

            mq = driverd->cfg->ops->skbq_tc_q_from_aci(driverd, aci);
            break;
        }

        case MORSE_SKB_CHAN_MGMT:
            mq = driverd->cfg->ops->skbq_mgmt_tc_q(driverd);
            break;

        case MORSE_SKB_CHAN_BEACON:
            mq = driverd->cfg->ops->skbq_bcn_tc_q(driverd);
            break;

        default:
            MMLOG_WRN("unexpected channel on reported tx status [%d]\n", tx_sts->channel);
    }

    return mq;
}

static void insert_pending_mmpkt_to_skbq(struct morse_skbq *mq,
                                         struct mmpkt *mmpkt,
                                         uint32_t insertion_id)
{
    uint32_t pkt_id;
    struct mmpktview *view;
    struct mmpkt *pfirst, *pnext;
    struct morse_buff_skb_header *mhdr;
    struct mmpkt *tail = mmpkt_list_peek_tail(&mq->skbq);


    mmpkt_list_remove(&mq->pending, mmpkt);


    mq->skbq_size += mmpkt_peek_data_length(mmpkt);

    if (tail == NULL)
    {

        mmpkt_list_append(&mq->skbq, mmpkt);
        return;
    }


    view = mmpkt_open(tail);
    mhdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
    pkt_id = mhdr->tx_info.pkt_id;
    mmpkt_close(&view);
    MORSE_WARN_ON(insertion_id == pkt_id);
    if (insertion_id >= pkt_id)
    {
        mmpkt_list_append(&mq->skbq, mmpkt);
        return;
    }


    MMPKT_LIST_WALK(&mq->skbq, pfirst, pnext)
    {
        view = mmpkt_open(pfirst);
        mhdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
        pkt_id = mhdr->tx_info.pkt_id;
        mmpkt_close(&view);
        MORSE_WARN_ON(insertion_id == pkt_id);
        if (insertion_id <= pkt_id)
        {

            mmpkt_list_prepend(&mq->skbq, mmpkt);
            return;
        }
    }


    MMOSAL_ASSERT(false);
}

static bool tx_mmpkt_is_ps_filtered(struct morse_skbq *mq,
                                    struct mmpkt *mmpkt,
                                    struct morse_skb_tx_status *tx_sts)
{
    uint32_t tx_sts_flags = le32toh(tx_sts->flags);
    MMOSAL_ASSERT((tx_sts_flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED));


    if (false)
    {

        __skbq_data_tx_finish(&mq->pending, mmpkt, NULL);
        MMLOG_INF("Dropping SKB as ps filter not supported\n");
        return true;
    }

    MMOSAL_ASSERT(tx_sts->channel == MORSE_SKB_CHAN_DATA || tx_sts->channel == MORSE_SKB_CHAN_MGMT);
    MMOSAL_ASSERT((mq->flags & MORSE_CHIP_IF_FLAGS_DATA));


    insert_pending_mmpkt_to_skbq(mq, mmpkt, tx_sts->pkt_id);
    return true;
}


static void morse_skb_remove_padding_after_sent_to_chip(struct mmpktview *view)
{
    struct morse_buff_skb_header *hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
    mmpkt_remove_from_start(view, sizeof(*hdr) + hdr->offset);

    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt_from_view(view));
    mmpkt_remove_from_end(view, tx_metadata->tail_padding);
    tx_metadata->tail_padding = 0;
}

static void morse_skbq_tx_status_process(struct driver_data *driverd,
                                         struct mmpktview *view,
                                         uint8_t channel)
{
    int i, mismatch = 0;
    struct morse_skb_tx_status *tx_sts = (struct morse_skb_tx_status *)mmpkt_get_data_start(view);
    int count = mmpkt_get_data_length(view) / sizeof(*tx_sts);

    MMOSAL_ASSERT(channel == 0xFF);

    for (i = 0; i < count; tx_sts++, i++)
    {
        struct morse_skbq *mq = __morse_skbq_match_tx_status_to_skbq(driverd, tx_sts);
        uint32_t tx_sts_flags = le32toh(tx_sts->flags);
        bool is_ps_filtered = (tx_sts_flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED);
        if (!mq)
        {
            MMLOG_WRN("No pending skbq match found [pktid:%lu chan:%u]\n",
                      tx_sts->pkt_id,
                      tx_sts->channel);
            mismatch++;
            continue;
        }

        spin_lock(&mq->lock);
        struct mmpkt *tx_mmpkt = __skbq_get_pending_by_id(mq, tx_sts->pkt_id);

        if (!tx_mmpkt)
        {
            MMLOG_WRN("No pending match found [pktid:%lu chan:%u]\n",
                      tx_sts->pkt_id,
                      tx_sts->channel);
            mismatch++;
            spin_unlock(&mq->lock);
            continue;
        }

        if (tx_sts_flags & MORSE_TX_STATUS_PAGE_INVALID)
        {

            __skbq_data_tx_finish(&mq->pending, tx_mmpkt, NULL);
            MMLOG_WRN("Page was invalid");
            spin_unlock(&mq->lock);
            continue;
        }

        if (is_ps_filtered && tx_mmpkt_is_ps_filtered(mq, tx_mmpkt, tx_sts))
        {

            spin_unlock(&mq->lock);
            continue;
        }

        struct mmpktview *tx_mmpkt_view = mmpkt_open(tx_mmpkt);
        morse_skb_remove_padding_after_sent_to_chip(tx_mmpkt_view);
        mmpkt_close(&tx_mmpkt_view);

        morse_skbq_tx_finish(mq, tx_mmpkt, tx_sts);

        spin_unlock(&mq->lock);
    }

    MMLOG_DBG("TX status %d (%d mismatch)\n", count, mismatch);

    if (!driverd->ps.suspended && (driverd->cfg->ops->skbq_get_tx_buffered_count(driverd) == 0))
    {

        driver_task_notify_event(driverd, DRV_EVT_PS_DELAYED_EVAL_PEND);
    }
}

void morse_skbq_process_rx(struct driver_data *driverd, struct mmpkt *mmpkt)
{
    struct mmpktview *view = mmpkt_open(mmpkt);

    struct morse_buff_skb_header *hdr =
        (struct morse_buff_skb_header *)mmpkt_remove_from_start(view, sizeof(*hdr));
    uint8_t channel = hdr->channel;

    if (channel == MORSE_SKB_CHAN_COMMAND)
    {
        SKBQ_TRACE("rx cmd chan");

        mmpkt_close(&view);
        morse_cmd_resp_process(driverd, mmpkt, channel);
    }
    else if (channel == MORSE_SKB_CHAN_TX_STATUS)
    {
        morse_skbq_tx_status_process(driverd, view, channel);
        mmpkt_close(&view);
        mmpkt_release(mmpkt);
    }
    else if (channel == MORSE_SKB_CHAN_LOOPBACK)
    {
        mmpkt_close(&view);
        mmpkt_release(mmpkt);
    }
    else
    {
        SKBQ_TRACE("rx chan %u", (unsigned)channel);
        struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(mmpkt);
        rx_metadata->rssi = (int16_t)le16toh(hdr->rx_status.rssi);
        rx_metadata->freq_100khz = le16toh(hdr->rx_status.freq_100khz);
        rx_metadata->bw_mhz = morse_ratecode_bw_index_to_s1g_bw_mhz(
            morse_ratecode_bw_index_get(hdr->rx_status.morse_rc));
        rx_metadata->flags = ((hdr->rx_status.flags & MORSE_RX_STATUS_FLAGS_DECRYPTED) ?
                                  MMDRV_RX_FLAG_DECRYPTED :
                                  0);
        rx_metadata->noise_dbm = hdr->rx_status.noise_dbm;
        rx_metadata->vif_id = MORSE_RX_STATUS_FLAGS_VIF_ID_GET(hdr->rx_status.flags);
        mmpkt_close(&view);
        mmdrv_host_process_rx_frame(mmpkt, channel);
    }


    driverd->health_check.last_checked = mmosal_get_time_ms();
}


int morse_skbq_purge(struct morse_skbq *mq, struct mmpkt_list *skbq)
{
    struct mmpkt *mmpkt;
    int cnt = 0;

    spin_lock(&mq->lock);
    while ((mmpkt = mmpkt_list_dequeue(skbq)))
    {
        cnt++;
        mmpkt_release(mmpkt);
    }
    spin_unlock(&mq->lock);

    return cnt;
}

int morse_skbq_tx_failed(struct morse_skbq *mq, struct mmpkt_list *skbq)
{
    struct mmpkt *mmpkt;
    int cnt = 0;

    spin_lock(&mq->lock);
    while ((mmpkt = mmpkt_list_peek(skbq)))
    {
        cnt++;
        __skbq_data_tx_finish(skbq, mmpkt, NULL);
    }
    spin_unlock(&mq->lock);

    return cnt;
}

int morse_skbq_enq(struct morse_skbq *mq, struct mmpkt_list *skbq)
{
    uint32_t size;
    int count = 0;
    struct mmpkt *pfirst, *pnext;

    spin_lock(&mq->lock);
    size = __morse_skbq_space(mq);
    MMPKT_LIST_WALK(skbq, pfirst, pnext)
    {
        uint32_t pkt_len = mmpkt_peek_data_length(pfirst);
        if (pkt_len > size)
        {
            break;
        }
        mmpkt_list_remove(skbq, pfirst);
        mmpkt_list_append(&mq->skbq, pfirst);
        count += pkt_len;
        size -= pkt_len;
        mq->skbq_size += pkt_len;
    }
    spin_unlock(&mq->lock);
    return count;
}

int morse_skbq_deq(struct morse_skbq *mq, struct mmpkt_list *skbq, uint32_t size)
{
    int count = 0;
    struct mmpkt *pfirst, *pnext;

    spin_lock(&mq->lock);
    MMPKT_LIST_WALK(&mq->skbq, pfirst, pnext)
    {
        uint32_t pkt_len = mmpkt_peek_data_length(pfirst);
        if (pkt_len > size)
        {
            break;
        }
        mmpkt_list_remove(&mq->skbq, pfirst);
        mmpkt_list_append(skbq, pfirst);
        count += pkt_len;
        size -= pkt_len;
        mq->skbq_size -= pkt_len;
    }
    spin_unlock(&mq->lock);
    return count;
}


int morse_skbq_deq_num_items(struct morse_skbq *mq, struct mmpkt_list *skbq, int num_items)
{
    int count = 0;
    struct mmpkt *pfirst, *pnext;

    spin_lock(&mq->lock);
    MMPKT_LIST_WALK(&mq->skbq, pfirst, pnext)
    {
        if (count >= num_items)
        {
            break;
        }
        mmpkt_list_remove(&mq->skbq, pfirst);
        mmpkt_list_append(skbq, pfirst);
        ++count;
        mq->skbq_size -= mmpkt_peek_data_length(pfirst);
    }
    spin_unlock(&mq->lock);
    return count;
}

int morse_skbq_enq_prepend(struct morse_skbq *mq, struct mmpkt_list *skbq)
{
    uint32_t size;
    int count = 0;
    struct mmpkt *pfirst, *pnext;

    spin_lock(&mq->lock);
    size = __morse_skbq_space(mq);
    MMPKT_LIST_WALK(skbq, pfirst, pnext)
    {
        uint32_t pkt_len = mmpkt_peek_data_length(pfirst);
        if (pkt_len > size)
        {
            break;
        }
        mmpkt_list_remove(skbq, pfirst);
        mmpkt_list_append(&mq->skbq, pfirst);
        count += pkt_len;
        size -= pkt_len;
        mq->skbq_size += pkt_len;
    }
    spin_unlock(&mq->lock);
    return count;
}

static int morse_skbq_tx(struct morse_skbq *mq, struct mmpkt *mmpkt, uint8_t channel)
{
    struct driver_data *driverd = mq->driverd;
    int rc;

    spin_lock(&mq->lock);
    rc = __morse_skbq_put(mq, mmpkt);
    if (rc)
    {
        MMLOG_ERR("__morse_skbq_put channel %d failed (%d)\n", channel, rc);
    }


    __morse_skbq_pkt_id(mq, mmpkt);

    spin_unlock(&mq->lock);

    switch (channel)
    {
        case MORSE_SKB_CHAN_DATA:
        case MORSE_SKB_CHAN_LOOPBACK:
        case MORSE_SKB_CHAN_DATA_NOACK:
            if (driver_is_data_tx_allowed(driverd))
            {
                driver_task_notify_event(driverd, DRV_EVT_TX_DATA_PEND);
            }
            break;

        case MORSE_SKB_CHAN_MGMT:
            driver_task_notify_event(driverd, DRV_EVT_TX_MGMT_PEND);
            break;

        case MORSE_SKB_CHAN_BEACON:
            driver_task_notify_event(driverd, DRV_EVT_TX_BEACON_PEND);
            break;

        case MORSE_SKB_CHAN_COMMAND:
            driver_task_notify_event(driverd, DRV_EVT_TX_COMMAND_PEND);
            break;

        default:
            MMLOG_ERR("Invalid SKB channel: %d\n", channel);
            break;
    }

    return rc;
}

int morse_skbq_tx_complete(struct morse_skbq *mq, struct mmpkt_list *skbq)
{
    if (mmpkt_list_is_empty(skbq))
    {
        return 0;
    }


    spin_lock(&mq->lock);
    mmpkt_list_append_list(&mq->pending, skbq);
    spin_unlock(&mq->lock);

    mmosal_task_enter_critical();
    if (mq->driverd->stale_status.enabled)
    {
        mmosal_timer_start(mq->driverd->stale_status.timer);
    }
    mmosal_task_exit_critical();

    return 0;
}


struct mmpkt *morse_skbq_tx_pending(struct morse_skbq *mq)
{
    struct mmpkt *pfirst;

    spin_lock(&mq->lock);
    pfirst = mmpkt_list_peek(&mq->pending);
    spin_unlock(&mq->lock);
    return pfirst;
}


static struct mmpkt *__skbq_get_pending_by_id(struct morse_skbq *mq, uint32_t pkt_id)
{
    struct mmpkt *pfirst, *pnext;
    struct mmpkt *ret = NULL;

    MMPKT_LIST_WALK(&mq->pending, pfirst, pnext)
    {
        struct mmpktview *view = mmpkt_open(pfirst);
        struct morse_buff_skb_header *hdr =
            (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
        uint32_t cur_pkt_id = hdr->tx_info.pkt_id;
        uint8_t channel = hdr->channel;
        mmpkt_close(&view);
        if (cur_pkt_id == pkt_id)
        {
            ret = pfirst;
            break;
        }
        else if (cur_pkt_id < pkt_id)
        {
            if (mmosal_time_has_passed(get_timeout_from_tx_mmpkt(pfirst)))
            {

                MMLOG_WRN("TX SKB timed out [id:%lu,chan:%u] while searching for pkt_id %lu\n",
                          cur_pkt_id,
                          channel,
                          pkt_id);
                __skbq_data_tx_finish(&mq->pending, pfirst, NULL);
            }
        }
    }

    return ret;
}

int morse_skbq_check_for_stale_tx(struct morse_skbq *mq)
{
    int flushed = 0;
    struct mmpkt *pfirst;
    struct mmpkt *pnext;

    if (mq->pending.len == 0)
    {
        return 0;
    }


    spin_lock(&mq->lock);
    MMPKT_LIST_WALK(&mq->pending, pfirst, pnext)
    {
        struct mmpktview *view = mmpkt_open(pfirst);
        struct morse_buff_skb_header *hdr =
            (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
        uint32_t pkt_id = hdr->tx_info.pkt_id;
        uint8_t channel = hdr->channel;
        mmpkt_close(&view);
        if (mmosal_time_has_passed(get_timeout_from_tx_mmpkt(pfirst)))
        {
            MMLOG_WRN("%s: TX SKB timed out [id:%lu,chan:%u]\n", __func__, pkt_id, channel);
            SKBQ_TRACE("skbq stale tx %x", pfirst);
            __skbq_data_tx_finish(&mq->pending, pfirst, NULL);
            flushed++;
        }
    }
    spin_unlock(&mq->lock);

    return flushed;
}


static int __skbq_cmd_finish(struct morse_skbq *mq, struct mmpkt *mmpkt)
{
    if (mq->pending.len > 0)
    {
        mmpkt_list_remove(&mq->pending, mmpkt);
        mmpkt_release(mmpkt);
    }
    else if (mq->skbq.len > 0)
    {

        MMLOG_INF("Command pending queue empty. Removing from SKBQ.\n");
        mmpkt_list_remove(&mq->skbq, mmpkt);
        if (mq->skbq_size >= mmpkt_peek_data_length(mmpkt))
        {
            mq->skbq_size -= mmpkt_peek_data_length(mmpkt);
        }
        mmpkt_release(mmpkt);
    }
    else
    {
        MMLOG_INF("Command Q not found\n");
    }

    return 0;
}


static int __skbq_data_tx_finish(struct mmpkt_list *skbq,
                                 struct mmpkt *mmpkt,
                                 struct morse_skb_tx_status *tx_sts)
{
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);

    mmpkt_list_remove(skbq, mmpkt);

    if (tx_sts && tx_sts->channel == MORSE_SKB_CHAN_BEACON)
    {

        mmpkt_release(mmpkt);
        return 0;
    }

    if (tx_sts)
    {
        unsigned ii;
        uint32_t tx_sts_flags = le32toh(tx_sts->flags);


        tx_metadata->attempts = 0;
        for (ii = 0; ii < sizeof(tx_sts->rates) / sizeof(tx_sts->rates[0]); ii++)
        {
            if (tx_sts->rates[ii].count == 0)
            {
                break;
            }
            tx_metadata->attempts += tx_sts->rates[ii].count;
        }

        tx_metadata->status_flags =
            ((tx_sts_flags & MORSE_TX_STATUS_FLAGS_NO_ACK) ? MMDRV_TX_STATUS_FLAG_NO_ACK : 0) |
            ((tx_sts_flags & MORSE_TX_STATUS_FLAGS_PS_FILTERED) ? MMDRV_TX_STATUS_FLAG_PS_FILTERED :
                                                                  0) |
            ((tx_sts_flags & MORSE_TX_STATUS_DUTY_CYCLE_CANT_SEND) ?
                 MMDRV_TX_STATUS_DUTY_CYCLE_CANT_SEND :
                 0) |
            ((tx_sts_flags & MORSE_TX_STATUS_WAS_AGGREGATED) ? MMDRV_TX_STATUS_WAS_AGGREGATED : 0);
    }
    else
    {
        mmpkt_release(mmpkt);
        return 0;
    }

    mmdrv_host_process_tx_status(mmpkt);

    return 0;
}


int morse_skbq_tx_finish(struct morse_skbq *mq,
                         struct mmpkt *mmpkt,
                         struct morse_skb_tx_status *tx_sts)
{
    int ret_sts;

    SKBQ_TRACE("skbq_tx_finish %x", mmpkt);

    if (mq->flags & MORSE_CHIP_IF_FLAGS_COMMAND)
    {
        ret_sts = __skbq_cmd_finish(mq, mmpkt);
    }
    else
    {
        ret_sts = __skbq_data_tx_finish(&mq->pending, mmpkt, tx_sts);
    }

    return ret_sts;
}

int morse_skbq_tx_flush(struct morse_skbq *mq)
{
    struct mmpkt *pfirst, *pnext;
    int cnt = 0;

    spin_lock(&mq->lock);

    MMPKT_LIST_WALK(&mq->pending, pfirst, pnext)
    {
        cnt++;
        mmpkt_list_remove(&mq->pending, pfirst);
        mmpkt_release(pfirst);
    }

    MMPKT_LIST_WALK(&mq->skbq, pfirst, pnext)
    {
        cnt++;
        mmpkt_list_remove(&mq->skbq, pfirst);
        mmpkt_release(pfirst);
    }

    spin_unlock(&mq->lock);

    return cnt;
}

void morse_skbq_init(struct driver_data *driverd,
                     bool from_chip,
                     struct morse_skbq *mq,
                     uint16_t flags)
{
    MM_UNUSED(from_chip);
    SKBQ_TRACE_INIT();
    spin_lock_init(&mq->lock);
    mmpkt_list_init(&mq->skbq);
    mmpkt_list_init(&mq->pending);
    mq->driverd = driverd;
    mq->skbq_size = 0;
    mq->flags = flags;
    mq->pkt_seq = 0;
}

void morse_skbq_finish(struct morse_skbq *mq)
{
    if (mq->skbq_size > 0)
    {
        MMLOG_INF("Purging a non empty MorseQ. Dropping data!\n");
    }

    mq->driverd->cfg->ops->skbq_close(mq);
    morse_skbq_purge(mq, &mq->skbq);
    morse_skbq_purge(mq, &mq->pending);
    mq->skbq_size = 0;
}

uint32_t morse_skbq_size(struct morse_skbq *mq)
{
    uint32_t count;

    spin_lock(&mq->lock);
    count = __morse_skbq_size(mq);
    spin_unlock(&mq->lock);
    return count;
}

uint32_t morse_skbq_count(struct morse_skbq *mq)
{
    uint32_t count = 0;

    spin_lock(&mq->lock);
    count += mq->skbq.len;
    spin_unlock(&mq->lock);

    return count;
}

uint32_t morse_skbq_count_tx_ready(struct morse_skbq *mq)
{
    struct driver_data *driverd = mq->driverd;

    if (!driver_is_data_tx_allowed(driverd))
    {
        return 0;
    }

    return morse_skbq_count(mq);
}

uint32_t morse_skbq_space(struct morse_skbq *mq)
{
    uint32_t space;

    spin_lock(&mq->lock);
    space = __morse_skbq_space(mq);
    spin_unlock(&mq->lock);

    return space;
}

struct mmpkt *morse_skbq_alloc_mmpkt_for_cmd(uint32_t length)
{
    return mmhal_wlan_alloc_mmpkt_for_tx(
        MMHAL_WLAN_PKT_COMMAND,
        FAST_ROUND_UP(sizeof(struct morse_buff_skb_header), MORSE_PKT_WORD_ALIGN) +
            MORSE_YAPS_DELIM_SIZE,
        FAST_ROUND_UP(length, MORSE_PKT_WORD_ALIGN),
        sizeof(struct mmdrv_tx_metadata));
}


static void convert_tx_metadata_to_tx_info(struct morse_skb_tx_info *tx_info,
                                           const struct mmdrv_tx_metadata *tx_metadata)
{
    unsigned ii;

    memset(tx_info, 0, sizeof(*tx_info));
    tx_info->flags = htole32(
        ((tx_metadata->flags & MMDRV_TX_FLAG_AMPDU_ENABLED) ? MORSE_TX_CONF_FLAGS_CTL_AMPDU : 0) |
        ((tx_metadata->flags & MMDRV_TX_FLAG_HW_ENC) ? MORSE_TX_CONF_FLAGS_HW_ENCRYPT : 0) |
        ((tx_metadata->flags & MMDRV_TX_FLAG_IMMEDIATE_REPORT) ?
             MORSE_TX_CONF_FLAGS_IMMEDIATE_REPORT :
             0) |
        MORSE_TX_CONF_FLAGS_KEY_IDX_SET(tx_metadata->key_idx) |
        MORSE_TX_CONF_FLAGS_VIF_ID_SET(tx_metadata->vif_id));

    tx_info->mmss_params = TX_INFO_MMSS_PARAMS_SET_MMSS(tx_metadata->ampdu_mss) |
                           TX_INFO_MMSS_PARAMS_SET_MMSS_OFFSET(tx_metadata->mmss_offset);

    tx_info->tid = tx_metadata->tid;
    tx_info->tid_params =
        ((tx_metadata->flags & MMDRV_TX_FLAG_AMPDU_ENABLED) ? TX_INFO_TID_PARAMS_AMPDU_ENABLED :
                                                              0) |
        (tx_metadata->tid_max_reorder_buf_size ? (tx_metadata->tid_max_reorder_buf_size - 1) : 0);

    for (ii = 0; ii < sizeof(tx_info->rates) / sizeof(tx_info->rates[0]); ii++)
    {
        struct morse_skb_rate_info *skb_rate = &tx_info->rates[ii];

        if (ii < sizeof(tx_metadata->rc_data.rates) / sizeof(tx_metadata->rc_data.rates[0]) &&
            tx_metadata->rc_data.rates[ii].rate != MMRC_MCS_UNUSED)
        {
            const struct mmrc_rate *mmrc_rate = &tx_metadata->rc_data.rates[ii];
            dot11_bandwidth_t bw = morse_ratecode_bw_mhz_to_bw_index(MMRC_MASK(mmrc_rate->bw));
            enum morse_rate_preamble pream = MORSE_RATE_PREAMBLE_S1G_SHORT;
            if (bw == DOT11_BANDWIDTH_1MHZ)
            {
                pream = MORSE_RATE_PREAMBLE_S1G_1M;
            }

            morse_rate_code_t rc = MORSE_RATECODE_INIT(bw, 0, mmrc_rate->rate, pream);
            if (mmrc_rate->flags & MMRC_MASK(MMRC_FLAGS_CTS_RTS))
            {
                morse_ratecode_enable_rts(&rc);
            }

            if (mmrc_rate->guard & MMRC_GUARD_SHORT)
            {
                morse_ratecode_enable_sgi(&rc);
            }

            if (tx_metadata->flags & MMDRV_TX_FLAG_TP_ENABLED)
            {
                morse_ratecode_enable_trav_pilots(&rc);
            }

            if (tx_metadata->flags & MMDRV_TX_FLAG_CR_1MHZ_PRE_ENABLED)
            {
                morse_ratecode_enable_ctrl_resp_1mhz(&rc);
            }

            skb_rate->morse_rc = rc;
            skb_rate->count = mmrc_rate->attempts;
        }
        else
        {
            skb_rate->morse_rc = 0;
            skb_rate->count = 0;
        }
    }
}

static void morse_skbq_ensure_word_alignment(struct mmpktview *view,
                                             struct mmdrv_tx_metadata *tx_metadata)
{
    uint32_t mmpkt_data_start = (uint32_t)mmpkt_get_data_start(view);
    uint8_t offset = mmpkt_data_start & 0x03ul;
    uint32_t mmpkt_data_len;
    uint32_t tail_pad;

    if (offset != 0)
    {
        MMLOG_DBG("mmpkt align: prepend %lu bytes\n", offset);
        mmpkt_prepend(view, offset);
    }


    mmpkt_data_len = (uint32_t)mmpkt_get_data_length(view);
    tail_pad = FAST_ROUND_UP(mmpkt_data_len, 4) - mmpkt_data_len;

    if (tail_pad != 0)
    {
        MMLOG_DBG("mmpkt align: append %lu bytes\n", tail_pad);
        const uint8_t padding[] = { 0, 0, 0 };
        mmpkt_append_data(view, padding, tail_pad);
        tx_metadata->tail_padding = tail_pad;
    }
}

int morse_skbq_mmpkt_tx(struct morse_skbq *mq, struct mmpkt *mmpkt, uint8_t channel)
{
    struct morse_buff_skb_header *unaligned_hdr;
    struct morse_buff_skb_header *hdr;
    uint32_t payload_len;
    int ret = 0;
    struct mmdrv_tx_metadata *tx_metadata;
    struct mmpktview *view;
    uint32_t offset;

    if ((mq == NULL) || (mmpkt == NULL))
    {
        return -EINVAL;
    }

    tx_metadata = mmdrv_get_tx_metadata(mmpkt);
    tx_metadata->timeout_abs_ms = mmosal_get_time_ms() + tx_status_lifetime_ms;

    view = mmpkt_open(mmpkt);
    payload_len = mmpkt_get_data_length(view);

    unaligned_hdr = (struct morse_buff_skb_header *)mmpkt_prepend(view, sizeof(*hdr));
    MMOSAL_ASSERT(unaligned_hdr != NULL);


    memset(unaligned_hdr, 0, sizeof(*unaligned_hdr));

    morse_skbq_ensure_word_alignment(view, tx_metadata);


    hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
    offset = (const uint8_t *)unaligned_hdr - (const uint8_t *)hdr;
    MMOSAL_ASSERT(hdr != NULL);

    hdr->sync = MORSE_SKB_HEADER_SYNC;
    hdr->channel = channel;
    hdr->len = htole16(payload_len);
    hdr->offset = offset;
    if (channel != MORSE_SKB_CHAN_COMMAND)
    {
        convert_tx_metadata_to_tx_info(&hdr->tx_info, tx_metadata);
    }
    else
    {
        memset(&hdr->tx_info, 0, sizeof(hdr->tx_info));
    }

    mmpkt_close(&view);

    ret = morse_skbq_tx(mq, mmpkt, channel);
    if (ret)
    {
        MMLOG_ERR("morse_skbq_tx fail: %d\n", ret);
        spin_lock(&mq->lock);
        morse_skbq_tx_finish(mq, mmpkt, NULL);
        spin_unlock(&mq->lock);
    }
    return ret;
}

int morse_skbq_get_tx_status_lifetime_ms(void)
{
    return tx_status_lifetime_ms;
}

void morse_skbq_data_traffic_pause(struct driver_data *driverd)
{
    atomic_set_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED,
                   (volatile atomic_ulong *)&driverd->state_flags);

}

void morse_skbq_data_traffic_resume(struct driver_data *driverd)
{
    atomic_test_and_clear_bit(MORSE_STATE_FLAG_DATA_TX_STOPPED,
                              (volatile atomic_ulong *)&driverd->state_flags);


    driver_task_notify_event(driverd, DRV_EVT_TX_DATA_PEND);
}

bool morse_validate_skb_checksum(uint8_t *data)
{
    struct morse_buff_skb_header *skb_hdr = (struct morse_buff_skb_header *)data;
    uint16_t frame_len = le16toh(skb_hdr->len);
    uint16_t len = frame_len + sizeof(*skb_hdr);
    uint32_t *data_to_xor = (uint32_t *)data;
    uint32_t header_checksum = (le16toh(skb_hdr->checksum_upper) << 8) | (skb_hdr->checksum_lower);
    uint32_t checksum = 0;
    int i;



    if (skb_hdr->channel == MORSE_SKB_CHAN_DATA)
    {
        const struct dot11_hdr *hdr = (struct dot11_hdr *)(data + sizeof(*skb_hdr));
        const uint16_t ver = dot11_frame_control_get_protocol_version(hdr->frame_control);
        const uint16_t type = dot11_frame_control_get_type(hdr->frame_control);


        if (ver == 0 && type == DOT11_FC_TYPE_DATA)
        {
            uint16_t data_len = sizeof(*skb_hdr) + DATA_FRAME_CHECKSUM_DATA_LEN;
            len = min_u16(len, data_len);

            len = len & 0xfffffffc;
        }
    }

    skb_hdr->checksum_upper = 0;
    skb_hdr->checksum_lower = 0;

    for (i = 0; i < len; i += 4)
    {
        checksum ^= *data_to_xor;
        data_to_xor++;
    }
    checksum = checksum & 0x00FFFFFF;

    return checksum == header_checksum;
}
