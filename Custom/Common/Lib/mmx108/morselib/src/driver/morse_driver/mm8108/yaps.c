/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#include <errno.h>

#include "yaps.h"

#include "mmpkt.h"

#include "yaps-hw.h"

#include "mmlog.h"
#include "driver/driver.h"
#include "driver/morse_driver/morse.h"
#include "driver/morse_driver/ps.h"
#include "driver/morse_driver/skb_header.h"
#include "driver/morse_driver/skbq.h"
#include "driver/transport/morse_transport.h"

#define BENCHMARK_PKT_LEN (1496)
#define BENCHMARK_WAIT_MS (5000)


#define CHIP_FULL_RECOVERY_TIMEOUT_MS 30


#ifndef MAX_PKTS_PER_TX_TXN
#define MAX_PKTS_PER_TX_TXN 16
#endif


#ifndef MAX_PKTS_PER_RX_TXN
#define MAX_PKTS_PER_RX_TXN 32
#endif


static struct morse_skbq *skbq_yaps_tc_q_from_aci(struct driver_data *driverd, int aci)
{
    struct morse_yaps *yaps = &driverd->chip_if->yaps;

    if (aci >= (int)ARRAY_SIZE(yaps->data_tx_qs))
    {
        return NULL;
    }
    return &yaps->data_tx_qs[aci];
}

static void skbq_yaps_close(struct morse_skbq *mq)
{


    MM_UNUSED(mq);
}

static void skbq_yaps_get_tx_qs(struct driver_data *driverd, struct morse_skbq **qs, int *num_qs)
{
    *qs = driverd->chip_if->yaps.data_tx_qs;
    *num_qs = YAPS_TX_SKBQ_MAX;
}

static struct morse_skbq *skbq_yaps_bcn_q(struct driver_data *driverd)
{
    return &driverd->chip_if->yaps.beacon_q;
}

static struct morse_skbq *skbq_yaps_mgmt_q(struct driver_data *driverd)
{
    return &driverd->chip_if->yaps.mgmt_q;
}

static struct morse_skbq *skbq_yaps_cmd_q(struct driver_data *driverd)
{
    return &driverd->chip_if->yaps.cmd_q;
}

static int yaps_irq_handler(struct driver_data *driverd, uint32_t status)
{
    if (test_bit(status, MORSE_INT_YAPS_FC_PKT_WAITING_IRQN))
    {
        driver_task_notify_event(driverd, DRV_EVT_RX_PEND);
    }

    if (test_bit(status, MORSE_INT_YAPS_FC_PACKET_FREED_UP_IRQN))
    {
        driver_task_notify_event(driverd, DRV_EVT_TX_PACKET_FREED_UP_PEND);
    }

    return 0;
}

const struct chip_if_ops morse_yaps_ops = {
    .init = morse_yaps_hw_init,
    .flush_tx_data = morse_yaps_hw_yaps_flush_tx_data,
    .skbq_get_tx_buffered_count = morse_yaps_get_tx_buffered_count,
    .finish = morse_yaps_hw_finish,
    .skbq_get_tx_qs = skbq_yaps_get_tx_qs,
    .skbq_close = skbq_yaps_close,
    .skbq_bcn_tc_q = skbq_yaps_bcn_q,
    .skbq_mgmt_tc_q = skbq_yaps_mgmt_q,
    .skbq_cmd_tc_q = skbq_yaps_cmd_q,
    .skbq_tc_q_from_aci = skbq_yaps_tc_q_from_aci,
    .chip_if_handle_irq = yaps_irq_handler,
    .chip_if_work = morse_yaps_work,
    .tx_stale_work = morse_yaps_stale_tx_work,
};


static bool yaps_read_pkt(struct morse_yaps *yaps)
{
    struct driver_data *driverd = yaps->driverd;
    struct mmpkt_list skbq = MMPKT_LIST_INIT;
    struct morse_skbq *mq = NULL;
    struct morse_buff_skb_header *hdr;
    int skb_bytes_remaining;
    int skb_len;
    int ret = 0;
    bool more_packets = true;
    struct mmpkt *mmpkt = NULL;

    ret = yaps->ops->read_pkt(yaps, &mmpkt);

    if (!mmpkt && !ret)
    {

        more_packets = false;
        goto exit_return_page;
    }

    if (ret)
    {
        more_packets = true;
        goto exit_return_page;
    }

    struct mmpktview *view = mmpkt_open(mmpkt);
    hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
    mmpkt_close(&view);

    mmdrv_get_rx_metadata(mmpkt)->read_timestamp_ms = mmosal_get_time_ms();


    if (hdr->sync != MORSE_SKB_HEADER_SYNC)
    {
        MMLOG_ERR("Sync value error [0xAA:%d], hdr.len %d\n", hdr->sync, hdr->len);
        ret = -EIO;
        more_packets = false;
        goto exit_return_page;
    }

    if (yaps->driverd->chip_if->validate_skb_checksum && !morse_validate_skb_checksum(mmpkt->buf))
    {
        MMLOG_WRN("SKB checksum is invalid hdr:[c:%02X s:%02X len:%d]",
                  hdr->channel,
                  hdr->sync,
                  hdr->len);


        if (hdr->channel != MORSE_SKB_CHAN_TX_STATUS)
        {
            ret = -EIO;
            more_packets = false;
            goto exit;
        }
    }


    switch (hdr->channel)
    {
        case MORSE_SKB_CHAN_DATA:
        case MORSE_SKB_CHAN_NDP_FRAMES:
        case MORSE_SKB_CHAN_TX_STATUS:
        case MORSE_SKB_CHAN_DATA_NOACK:
        case MORSE_SKB_CHAN_BEACON:
        case MORSE_SKB_CHAN_MGMT:
        case MORSE_SKB_CHAN_LOOPBACK:
            mq = &yaps->data_rx_q;
            break;

        case MORSE_SKB_CHAN_COMMAND:
            mq = &yaps->cmd_resp_q;
            break;

        default:
            MMLOG_ERR("channel value error [%d]\n", hdr->channel);
            ret = -EIO;
            more_packets = false;
            goto exit_return_page;
    }


    skb_len = sizeof(*hdr) + le16toh(hdr->len) + le16toh(hdr->offset);
    skb_bytes_remaining = morse_skbq_space(mq);


    if (skb_len > skb_bytes_remaining)
    {
        MMLOG_ERR("Page will not fit in SKBQ, dropping - len %d remain %d\n",
                  skb_len,
                  skb_bytes_remaining);
        ret = -ENOMEM;
        more_packets = true;

        morse_skbq_process_rx(driverd, mmpkt);
        goto exit_return_page;
    }


    mmpkt_truncate(mmpkt, skb_len);

    morse_skbq_process_rx(driverd, mmpkt);
    mmpkt = NULL;
    goto exit;

exit_return_page:
    if (ret && mq != NULL)
    {
        MMLOG_ERR("Failed %d\n", ret);
        morse_skbq_purge(mq, &skbq);
        goto exit;
    }

exit:
    if (ret && mmpkt)
    {
        mmpkt_release(mmpkt);
    }

    morse_hw_pager_update_consec_failure_cnt(yaps->driverd, ret);
    return more_packets;
}

static int morse_yaps_tx(struct morse_yaps *yaps, struct morse_skbq *mq)
{
    int ret = 0;
    int num_items = 0;
    struct mmpkt *mmpkt;
    struct mmpkt_list skbq_to_send = MMPKT_LIST_INIT;
    struct mmpkt_list skbq_sent = MMPKT_LIST_INIT;
    struct mmpkt_list skbq_failed = MMPKT_LIST_INIT;
    struct mmpkt *pfirst, *pnext;
    struct morse_buff_skb_header *hdr;


    spin_lock(&mq->lock);
    mmpkt = mmpkt_list_peek(&mq->skbq);
    spin_unlock(&mq->lock);
    if (!mmpkt)
    {
        return 0;
    }

    if (mq == &yaps->cmd_q)
    {

        if (morse_skbq_purge(mq, &mq->pending))
        {
            MMLOG_ERR("Command/s timed out in pending skbq\n");
        }
    }

    ret = yaps->ops->update_status(yaps);
    if (ret)
    {
        morse_hw_pager_update_consec_failure_cnt(yaps->driverd, ret);
        return ret;
    }

    struct mmpktview *view = mmpkt_open(mmpkt);
    hdr = (struct morse_buff_skb_header *)mmpkt_get_data_start(view);
    enum morse_yaps_to_chip_q tc_queue;
    switch (hdr->channel)
    {
        case MORSE_SKB_CHAN_COMMAND:
            tc_queue = MORSE_YAPS_CMD_Q;
            break;

        case MORSE_SKB_CHAN_BEACON:
            tc_queue = MORSE_YAPS_BEACON_Q;
            break;

        case MORSE_SKB_CHAN_MGMT:
            tc_queue = MORSE_YAPS_MGMT_Q;
            break;

        default:
            tc_queue = MORSE_YAPS_TX_Q;
            break;
    }
    uint32_t queue_packet_space = morse_yaps_hw_get_tc_queue_space(yaps, tc_queue);
    num_items = morse_skbq_deq_num_items(mq, &skbq_to_send, queue_packet_space);
    mmpkt_close(&view);

    MMPKT_LIST_WALK(&skbq_to_send, pfirst, pnext)
    {
        ret = yaps->ops->write_pkt(yaps, pfirst, tc_queue, pnext);
        morse_hw_pager_update_consec_failure_cnt(yaps->driverd, ret);

        if (ret == 0)
        {
            mmpkt_list_remove(&skbq_to_send, pfirst);
            mmpkt_list_append(&skbq_sent, pfirst);
        }
        else
        {
            MMLOG_ERR("TX skb failed for queue %d with err %d\n", tc_queue, ret);
            mmpkt_list_remove(&skbq_to_send, pfirst);
            mmpkt_list_append(&skbq_failed, pfirst);
        }
    }

    if (skbq_failed.len > 0)
    {
        MMLOG_ERR("Failed to write %lu pkts - rc=%d items=%d\n", skbq_failed.len, ret, num_items);
        if (mq == &yaps->cmd_q)
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

    return ret;
}


static bool morse_yaps_tx_data_handler(struct morse_yaps *yaps)
{
    int16_t aci;
    uint32_t count = 0;
    struct driver_data *driverd = yaps->driverd;
    for (aci = MORSE_ACI_VO; aci >= 0; aci--)
    {
        struct morse_skbq *data_q = skbq_yaps_tc_q_from_aci(yaps->driverd, aci);

        if (!driver_is_data_tx_allowed(driverd))
        {
            break;
        }

        yaps->chip_queue_full.is_full = morse_yaps_tx(yaps, data_q);
        count += morse_skbq_count(data_q);

        if (yaps->chip_queue_full.is_full)
        {
            break;
        }

        if (aci == MORSE_ACI_BE)
        {
            break;
        }
    }

    return (count > 0) && driver_is_data_tx_allowed(driverd);
}


static bool morse_yaps_tx_cmd_handler(struct morse_yaps *yaps)
{
    struct morse_skbq *cmd_q = &yaps->cmd_q;

    morse_yaps_tx(yaps, cmd_q);
    return (morse_skbq_count(cmd_q) > 0);
}

static bool morse_yaps_tx_beacon_handler(struct morse_yaps *yaps)
{
    struct morse_skbq *beacon_q = &yaps->beacon_q;

    morse_yaps_tx(yaps, beacon_q);
    return (morse_skbq_count(beacon_q) > 0);
}

static bool morse_yaps_tx_mgmt_handler(struct morse_yaps *yaps)
{
    struct morse_skbq *mgmt_q = &yaps->mgmt_q;

    morse_yaps_tx(yaps, mgmt_q);
    return (morse_skbq_count(mgmt_q) > 0);
}


static bool morse_yaps_rx_handler(struct morse_yaps *yaps)
{
    bool more_packets = true;
    int count = 0;


    do {
        more_packets = yaps_read_pkt(yaps);
        count++;
    } while ((count < MAX_PKTS_PER_RX_TXN) && (more_packets));

    return more_packets;
}

void morse_yaps_stale_tx_work(struct driver_data *driverd)
{
    size_t i;
    int flushed = 0;
    struct morse_yaps *yaps;

    bool pending = driver_task_notification_check_and_clear(driverd, DRV_EVT_STALE_TX_STATUS_PEND);

    if (!pending || !driverd->chip_if || !driverd->stale_status.enabled)
    {
        return;
    }

    yaps = &driverd->chip_if->yaps;
    flushed += morse_skbq_check_for_stale_tx(&yaps->beacon_q);
    flushed += morse_skbq_check_for_stale_tx(&yaps->mgmt_q);

    for (i = 0; i < ARRAY_SIZE(yaps->data_tx_qs); i++)
    {
        flushed += morse_skbq_check_for_stale_tx(&yaps->data_tx_qs[i]);
    }

    if (flushed)
    {
        MMLOG_DBG("Flushed %d stale TX SKBs\n", flushed);

        if (!driverd->ps.suspended && (morse_yaps_get_tx_buffered_count(driverd) == 0))
        {

            driver_task_notify_event(driverd, DRV_EVT_PS_DELAYED_EVAL_PEND);
        }
    }
}

void morse_yaps_work(struct driver_data *driverd)
{
    bool network_activity = false;

    MMLOG_DBG("YAPS work %s %08lx\n",
              driver_task_notification_is_pending(driverd, DRV_EVT_MASK_PAGESET) ? "pending" :
                                                                                   "no pending",
              driverd->driver_task.pending_evts);

    if (!driver_task_notification_is_pending(driverd, DRV_EVT_MASK_PAGESET))
    {
        return;
    }

    MMOSAL_DEV_ASSERT(driverd->chip_if != NULL);
    if (driverd->chip_if == NULL)
    {
        MMLOG_WRN("chip_if NULL\n");
        return;
    }

    struct morse_yaps *yaps = &driverd->chip_if->yaps;


    morse_ps_disable(driverd, PS_WAKER_PAGESET);
    morse_trns_claim(driverd);


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_RX_PEND))
    {
        uint32_t buffered = yaps->data_rx_q.skbq.len;

        if (morse_yaps_rx_handler(yaps))
        {
            driver_task_notify_event(driverd, DRV_EVT_RX_PEND);
        }

        if (yaps->data_rx_q.skbq.len > buffered)
        {
            network_activity = true;
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_COMMAND_PEND))
    {
        if (morse_yaps_tx_cmd_handler(yaps))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_COMMAND_PEND, 10);
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_BEACON_PEND))
    {
        if (morse_yaps_tx_beacon_handler(yaps))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_BEACON_PEND, 1);
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_MGMT_PEND))
    {
        network_activity = true;
        if (morse_yaps_tx_mgmt_handler(yaps))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_MGMT_PEND, 2);
        }
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TRAFFIC_PAUSE_PEND))
    {
        if (driver_task_notification_check(driverd, DRV_EVT_TRAFFIC_PAUSE_PEND))
        {
            MMLOG_ERR("Latency to handle twt traffic pause is too great\n");
        }

        morse_skbq_data_traffic_pause(driverd);
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TRAFFIC_RESUME_PEND))
    {
        if (driver_task_notification_check(driverd, DRV_EVT_TRAFFIC_RESUME_PEND))
        {
            MMLOG_ERR("Latency to handle twt traffic resume is too great\n");
        }

        morse_skbq_data_traffic_resume(driverd);
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_PACKET_FREED_UP_PEND))
    {
        yaps->chip_queue_full.is_full = false;
    }


    if (yaps->chip_queue_full.is_full)
    {
        goto exit;
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_DATA_PEND))
    {
        network_activity = true;
        if (morse_yaps_tx_data_handler(yaps))
        {

            driver_task_schedule_notification(driverd, DRV_EVT_TX_DATA_PEND, 2);
        }
    }

exit:
    if (network_activity)
    {
        morse_ps_network_activity(driverd);
    }

    morse_trns_release(driverd);
    morse_ps_enable(driverd, PS_WAKER_PAGESET);
}

int morse_yaps_get_tx_status_pending_count(struct driver_data *driverd)
{
    size_t i;
    int count = 0;
    struct morse_yaps *yaps;

    if (!driverd->chip_if)
    {
        return 0;
    }

    yaps = &driverd->chip_if->yaps;
    count += yaps->beacon_q.pending.len;
    count += yaps->mgmt_q.pending.len;
    count += yaps->cmd_q.pending.len;

    for (i = 0; i < ARRAY_SIZE(yaps->data_tx_qs); i++)
    {
        count += yaps->data_tx_qs[i].pending.len;
    }

    return count;
}

int morse_yaps_get_tx_buffered_count(struct driver_data *driverd)
{
    size_t i;
    int count = 0;
    struct morse_yaps *yaps;

    if (!driverd->chip_if)
    {
        return 0;
    }

    yaps = &driverd->chip_if->yaps;
    count += yaps->beacon_q.skbq.len + yaps->beacon_q.pending.len;
    count += yaps->mgmt_q.skbq.len + yaps->mgmt_q.pending.len;
    count += yaps->cmd_q.skbq.len + yaps->cmd_q.pending.len;

    for (i = 0; i < ARRAY_SIZE(yaps->data_tx_qs); i++)
    {
        count += morse_skbq_count_tx_ready(&yaps->data_tx_qs[i]) + yaps->data_tx_qs[i].pending.len;
    }

    return count;
}

static int morse_tx_chip_full_timer_init(struct morse_yaps *yaps)
{
    yaps->chip_queue_full.enabled = true;
    return 0;
}

static int morse_tx_chip_full_timer_finish(struct morse_yaps *yaps)
{
    if (!yaps->chip_queue_full.enabled)
    {
        return 0;
    }

    yaps->chip_queue_full.enabled = false;
    return 0;
}

int morse_yaps_init(struct driver_data *driverd, struct morse_yaps *yaps, uint8_t flags)
{
    size_t i;

    yaps->driverd = driverd;
    yaps->flags = flags;
    driverd->chip_if->active_chip_if = MORSE_CHIP_IF_YAPS;

    if (yaps->flags & MORSE_CHIP_IF_FLAGS_DATA)
    {

        morse_skbq_init(driverd, true, &yaps->data_rx_q, MORSE_CHIP_IF_FLAGS_DATA);
        morse_skbq_init(driverd, true, &yaps->beacon_q, MORSE_CHIP_IF_FLAGS_DATA);
        morse_skbq_init(driverd, true, &yaps->mgmt_q, MORSE_CHIP_IF_FLAGS_DATA);
        for (i = 0; i < ARRAY_SIZE(yaps->data_tx_qs); i++)
        {
            morse_skbq_init(driverd, false, &yaps->data_tx_qs[i], MORSE_CHIP_IF_FLAGS_DATA);
        }
    }

    if (yaps->flags & MORSE_CHIP_IF_FLAGS_COMMAND)
    {

        morse_skbq_init(driverd, false, &yaps->cmd_q, MORSE_CHIP_IF_FLAGS_COMMAND);
        morse_skbq_init(driverd, true, &yaps->cmd_resp_q, MORSE_CHIP_IF_FLAGS_COMMAND);
    }

    morse_tx_chip_full_timer_init(yaps);

    return 0;
}

void morse_yaps_finish(struct morse_yaps *yaps)
{
    size_t i;

    if (yaps->flags & MORSE_CHIP_IF_FLAGS_DATA)
    {
        morse_skbq_finish(&yaps->data_rx_q);
        morse_skbq_finish(&yaps->beacon_q);
        morse_skbq_finish(&yaps->mgmt_q);
        for (i = 0; i < ARRAY_SIZE(yaps->data_tx_qs); i++)
        {
            morse_skbq_finish(&yaps->data_tx_qs[i]);
        }
    }

    if (yaps->flags & MORSE_CHIP_IF_FLAGS_COMMAND)
    {
        morse_skbq_finish(&yaps->cmd_q);
        morse_skbq_finish(&yaps->cmd_resp_q);
    }

    morse_tx_chip_full_timer_finish(yaps);
}

void morse_yaps_flush_tx_data(struct morse_yaps *yaps)
{
    size_t i;

    morse_skbq_tx_flush(&yaps->beacon_q);
    morse_skbq_tx_flush(&yaps->mgmt_q);
    for (i = 0; i < ARRAY_SIZE(yaps->data_tx_qs); i++)
    {
        morse_skbq_tx_flush(&yaps->data_tx_qs[i]);
    }
}
