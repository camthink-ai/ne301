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

#ifdef ENABLE_YAPS_TRACE
#include "mmtrace.h"
static mmtrace_channel yaps_channel_handle;
#define YAPS_TRACE_INIT()                                       \
    do {                                                        \
        yaps_channel_handle = mmtrace_register_channel("yaps"); \
    } while (0)
#define YAPS_TRACE(_fmt, ...) mmtrace_printf(yaps_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define YAPS_TRACE_INIT() \
    do {                  \
    } while (0)
#define YAPS_TRACE(_fmt, ...) \
    do {                      \
    } while (0)
#endif

#define BENCHMARK_PKT_LEN (1496)
#define BENCHMARK_WAIT_MS (5000)


#define CHIP_FULL_RECOVERY_TIMEOUT_MS 30

/* NE301 port patch (class C wake recovery, from the 2.10.4 tree): sustained
 * -ENOMEM on TX while the status registers read back all-zero is the
 * signature of a chip that went back to sleep under a bus the host still
 * considers awake. Escalate to a WAKE kick after this long. */
#define MORSE_YAPS_ENOMEM_ESCALATE_MS (3000)


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
    .skbq_get_tx_buffered_count = morse_yaps_get_tx_buffered_count,
    .finish = morse_yaps_hw_finish,
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
    struct morse_buff_skb_header *hdr;
    int mmpkt_len;
    int ret = 0;
    bool more_packets = true;
    struct mmpkt *mmpkt = NULL;

    ret = morse_yaps_hw_read_pkt(yaps, &mmpkt);

    if (!mmpkt && !ret)
    {

        more_packets = false;
        goto exit;
    }

    if (ret)
    {
        more_packets = true;
        goto exit;
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
        goto exit;
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

        MMLOG_INF("Ignoring invalid SKB checksum for TX status\n");
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
        case MORSE_SKB_CHAN_COMMAND:
            break;

        default:
            MMLOG_ERR("channel value error [%d]\n", hdr->channel);
            ret = -EIO;
            more_packets = false;
            goto exit;
    }


    mmpkt_len = sizeof(*hdr) + le16toh(hdr->offset) + le16toh(hdr->len);
    mmpkt_truncate(mmpkt, mmpkt_len);

    morse_skbq_process_rx(driverd, mmpkt);
    mmpkt = NULL;
    goto exit;

exit:
    mmpkt_release(mmpkt);

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
    mmpkt_close(&view);


    morse_skbq_drop_stale_pkts(mq);

    spin_lock(&mq->lock);
    num_items = morse_yaps_hw_get_num_pkts(yaps, tc_queue, &mq->skbq);
    int count = mmpkt_list_dequeue_multiple(&mq->skbq, &skbq_to_send, num_items);

    MMOSAL_DEV_ASSERT(count == num_items);
    spin_unlock(&mq->lock);

    MMPKT_LIST_WALK(&skbq_to_send, pfirst, pnext)
    {
        ret = morse_yaps_hw_write_pkt(yaps, pfirst, tc_queue, pnext);
        morse_hw_pager_update_consec_failure_cnt(yaps->driverd, ret);

        if (ret == -ENOMEM)
        {
            MMLOG_ERR("Insufficient memory for skb TX (TC_Q: %d)\n", tc_queue);
            mmpkt_list_append_list(&skbq_failed, &skbq_to_send);

            /* NE301 port patch (class C wake recovery, from the 2.10.4 tree):
             * upstream asserts here, which on this port resets the whole MCU.
             * -ENOMEM is excluded from the consec-failure counter, so without
             * this nothing else escalates: update_status "succeeds" against a
             * sleeping chip (registers read back zero, pool count 0). Give the
             * WAKE kick a 3 s window to revive the chip; one that will not
             * wake gets a hw restart instead of a device reset. */
            uint32_t now_ms = mmosal_get_time_ms();
            if (yaps->enomem_since_ms == 0)
            {
                yaps->enomem_since_ms = now_ms;
            }
            else if (mmosal_time_has_passed(yaps->enomem_since_ms + MORSE_YAPS_ENOMEM_ESCALATE_MS))
            {
                MMLOG_ERR("queue %d un-writable for >%u ms - kicking WAKE\n",
                          tc_queue,
                          (unsigned)MORSE_YAPS_ENOMEM_ESCALATE_MS);
                yaps->enomem_since_ms = now_ms;
                if (!morse_ps_kick_wake(yaps->driverd))
                {
                    MMLOG_ERR("WAKE kick did not revive chip - requesting hw restart\n");
                    mmdrv_host_hw_restart_required();
                }
            }
            break;
        }

        mmpkt_list_remove(&skbq_to_send, pfirst);
        if (ret == 0)
        {
            yaps->enomem_since_ms = 0;
            mmpkt_list_append(&skbq_sent, pfirst);
        }
        else
        {
            MMLOG_ERR("TX skb failed for queue %d with err %d\n", tc_queue, ret);
            mmpkt_list_append(&skbq_failed, pfirst);
        }
    }

    if (skbq_failed.len > 0)
    {
        MMLOG_ERR("Failed to write %lu pkts - rc=%d items=%d\n", skbq_failed.len, ret, num_items);
        mmpkt_list_clear(&skbq_failed);
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
        MMLOG_WRN("Flushed %d stale TX SKBs\n", flushed);

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
        if (morse_yaps_rx_handler(yaps))
        {
            driver_task_notify_event(driverd, DRV_EVT_RX_PEND);
        }

        network_activity = true;
    }


    if (driver_task_notification_is_pending(yaps->driverd, DRV_EVT_TX_MASK))
    {
        int update_stat_rc = morse_yaps_hw_update_status(yaps);
        morse_hw_pager_update_consec_failure_cnt(yaps->driverd, update_stat_rc);
        if (update_stat_rc)
        {
            MMLOG_WRN("YAPS HW update status failed with code %d\n", update_stat_rc);
            /* NE301 port patch: the TX events above are polled with
             * is_pending() and only their handlers clear them, and this goto
             * skips those handlers — with a dead transport the driver task's
             * inner event loop spanned at full speed (one status-read
             * attempt every ~8 ms, forever), starving the evtloop recovery
             * ladder and the watchdog feeder (field logs: ~3 s flood then a
             * watchdog reset). Drop the stuck TX events instead: a chip in
             * this state only recovers via hw_restart, which flushes these
             * queues anyway, and fresh enqueues re-notify on their own. */
            driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_COMMAND_PEND);
            driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_BEACON_PEND);
            driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_MGMT_PEND);
            driver_task_notification_check_and_clear(driverd, DRV_EVT_TX_DATA_PEND);
            goto exit_no_eval;
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
            MMLOG_WRN("More beacons are waiting to be sent\n");

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
        morse_skbq_data_traffic_pause(driverd);
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TRAFFIC_PAUSE_TIMEOUT))
    {
        mmdrv_host_set_tx_paused(MMDRV_PAUSE_SOURCE_MASK_TRAFFIC_CTRL, false);
        driver_task_notify_event(driverd, DRV_EVT_TRAFFIC_RESUME_PEND);
        MMLOG_WRN("Traffic pause override event fired\n");
    }


    if (driver_task_notification_check_and_clear(driverd, DRV_EVT_TRAFFIC_RESUME_PEND))
    {
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

exit_no_eval:
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

int morse_yaps_init(struct driver_data *driverd, struct morse_yaps *yaps, uint8_t flags)
{
    size_t i;

    YAPS_TRACE_INIT();

    yaps->driverd = driverd;
    yaps->flags = flags;
    yaps->enomem_since_ms = 0;
    driverd->chip_if->active_chip_if = MORSE_CHIP_IF_YAPS;

    if (yaps->flags & MORSE_CHIP_IF_FLAGS_DATA)
    {

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
    }

    return 0;
}

void morse_yaps_finish(struct morse_yaps *yaps)
{
    size_t i;

    if (yaps->flags & MORSE_CHIP_IF_FLAGS_DATA)
    {
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
    }
}
