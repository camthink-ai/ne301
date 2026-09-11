/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#include <endian.h>
#include <errno.h>
#include <stdint.h>

#include "mmhal_wlan.h"
#include "mmlog.h"
#include "mmpkt.h"

#include "yaps-hw.h"
#include "yaps.h"

#include "dot11/dot11_frames.h"
#include "driver/driver.h"
#include "driver/health/driver_health.h"
#include "driver/morse_crc/morse_crc.h"
#include "driver/morse_driver/chip_if.h"
#include "driver/morse_driver/morse.h"
#include "driver/transport/morse_transport.h"


#define YAPS_MAX_RX_PAYLOAD 1628
#define YAPS_MAX_TX_PAYLOAD 1514
#define TX_DATA_HEADER_LEN                  \
    (sizeof(struct dot11_data_hdr) +        \
     sizeof(struct dot11_qos_ctrl) +        \
     sizeof(struct morse_buff_skb_header) + \
     MORSE_PKT_WORD_ALIGN +                 \
     MORSE_YAPS_DELIM_SIZE)

MM_STATIC_ASSERT((sizeof(struct mmpkt) + YAPS_MAX_RX_PAYLOAD + sizeof(struct mmdrv_rx_metadata) <=
                  MMHAL_WLAN_MMPKT_RX_MAX_SIZE),
                 "RX pool size must be larger to accommodate packets from the chip");
MM_STATIC_ASSERT(TX_DATA_HEADER_LEN + YAPS_MAX_TX_PAYLOAD + sizeof(struct mmdrv_tx_metadata) <=
                     MMHAL_WLAN_MMPKT_TX_MAX_SIZE,
                 "TX pool size must be larger to accommodate packets to the chip");

#define YAPS_MAX_PKT_SIZE_BYTES         16128
#define YAPS_DEFAULT_READ_SIZE_BYTES    512
#define YAPS_METADATA_PAGE_COUNT        1
#define YAPS_STATUS_REG_READ_TIMEOUT_MS 100


#define YAPS_PHANDLE_CORRUPTION_WAR_EXTRA_PAGE 1

#define YAPS_PAGE_SIZE                         256
#define SDIO_BLOCKSIZE                         512


#define YAPS_CALC_PADDING(_bytes) ((_bytes) & 0x3 ? (4 - ((_bytes) & 0x3)) : 0)




#define YAPS_DELIM_GET_PKT_SIZE(_yaps_aux, _delim) \
    (((_delim) & 0x3FFF) - (_yaps_aux)->reserved_yaps_page_size)
#define YAPS_DELIM_SET_PKT_SIZE(_yaps_aux, _pkt_size) \
    (((_pkt_size) & 0x3FFF) + (_yaps_aux)->reserved_yaps_page_size)
#define YAPS_DELIM_GET_PHANDLE_SIZE(_delim) (((_delim) & 0x3FFF))


#define YAPS_DELIM_GET_POOL_ID(_delim)   (((_delim) >> 14) & 0x7)
#define YAPS_DELIM_SET_POOL_ID(_pool_id) (((_pool_id) & 0x7) << 14)

#define YAPS_DELIM_GET_PADDING(_delim)   (((_delim) >> 17) & 0x3)
#define YAPS_DELIM_SET_PADDING(_padding) (((_padding) & 0x3) << 17)

#define YAPS_DELIM_GET_IRQ(_delim) (((_delim) >> 19) & 0x1)
#define YAPS_DELIM_SET_IRQ(_irq)   (((_irq) & 0x1) << 19)

#define YAPS_DELIM_GET_RESERVED(_delim)    (((_delim) >> 20) & 0x1F)
#define YAPS_DELIM_SET_RESERVED(_reserved) (((_reserved) & 0x1F) << 20)

#define YAPS_DELIM_GET_CRC(_delim) (((_delim) >> 25) & 0x7F)
#define YAPS_DELIM_SET_CRC(_crc)   (((_crc) & 0x7F) << 25)


struct MM_PACKED morse_yaps_status_registers
{

    uint32_t tc_tx_pool_num_pages;

    uint32_t tc_cmd_pool_num_pages;

    uint32_t tc_beacon_pool_num_pages;

    uint32_t tc_mgmt_pool_num_pages;

    uint32_t fc_rx_pool_num_pages;

    uint32_t fc_resp_pool_num_pages;

    uint32_t fc_tx_sts_pool_num_pages;

    uint32_t fc_aux_pool_num_pages;


    uint32_t tc_tx_num_pkts;

    uint32_t tc_cmd_num_pkts;

    uint32_t tc_beacon_num_pkts;

    uint32_t tc_mgmt_num_pkts;

    uint32_t fc_num_pkts;

    uint32_t fc_done_num_pkts;

    uint32_t fc_rx_bytes_in_queue;

    uint32_t tc_delim_crc_fail_detected;

    union
    {

        uint32_t fc_host_ysl_status;

        uint32_t scratch_0;
    };

    union
    {

        uint32_t lock;

        uint32_t scratch_1;
    };


};

struct morse_yaps_hw_aux_data
{
    volatile atomic_ulong access_lock;

    uint32_t yds_addr;
    uint32_t ysl_addr;
    uint32_t status_regs_addr;


    int tc_tx_pool_size;
    int tc_cmd_pool_size;
    int tc_beacon_pool_size;
    int tc_mgmt_pool_size;
    int fc_rx_pool_size;
    int fc_resp_pool_size;
    int fc_tx_sts_pool_size;
    int fc_aux_pool_size;


    int tc_tx_q_size;
    int tc_cmd_q_size;
    int tc_beacon_q_size;
    int tc_mgmt_q_size;
    int fc_q_size;
    int fc_done_q_size;

    int reserved_yaps_page_size;


    struct morse_yaps_status_registers status_regs;
};

static struct morse_chip_if_state chip_if_state;
static struct morse_yaps_hw_aux_data morse_yaps_hw_aux_data;

static int yaps_hw_lock(struct morse_yaps *yaps)
{
    if (atomic_test_and_set_bit_lock(0, &yaps->aux_data->access_lock))
    {
        return -1;
    }
    return 0;
}

void yaps_hw_unlock(struct morse_yaps *yaps)
{
    atomic_clear_bit_unlock(0, &yaps->aux_data->access_lock);
}

static void morse_yaps_fill_aux_data_from_hw_tbl(struct morse_yaps_hw_aux_data *aux_data,
                                                 struct morse_yaps_hw_table *tbl_ptr)
{
    aux_data->ysl_addr = le32toh(tbl_ptr->ysl_addr);
    aux_data->yds_addr = le32toh(tbl_ptr->yds_addr);
    aux_data->status_regs_addr = le32toh(tbl_ptr->status_regs_addr);

    aux_data->tc_tx_pool_size = le16toh(tbl_ptr->tc_tx_pool_size);
    aux_data->fc_rx_pool_size = le16toh(tbl_ptr->fc_rx_pool_size);
    aux_data->tc_cmd_pool_size = tbl_ptr->tc_cmd_pool_size;
    aux_data->tc_beacon_pool_size = tbl_ptr->tc_beacon_pool_size;
    aux_data->tc_mgmt_pool_size = tbl_ptr->tc_mgmt_pool_size;
    aux_data->fc_resp_pool_size = tbl_ptr->fc_resp_pool_size;
    aux_data->fc_tx_sts_pool_size = tbl_ptr->fc_tx_sts_pool_size;
    aux_data->fc_aux_pool_size = tbl_ptr->fc_aux_pool_size;
    aux_data->tc_tx_q_size = tbl_ptr->tc_tx_q_size;
    aux_data->tc_cmd_q_size = tbl_ptr->tc_cmd_q_size;
    aux_data->tc_beacon_q_size = tbl_ptr->tc_beacon_q_size;
    aux_data->tc_mgmt_q_size = tbl_ptr->tc_mgmt_q_size;
    aux_data->fc_q_size = tbl_ptr->fc_q_size;
    aux_data->fc_done_q_size = tbl_ptr->fc_done_q_size;
    aux_data->reserved_yaps_page_size = tbl_ptr->yaps_reserved_page_size;
}

static inline uint32_t morse_yaps_delimiter(struct morse_yaps *yaps,
                                            unsigned int size,
                                            uint8_t pool_id,
                                            bool irq)
{
    uint32_t delim = 0;

    delim |= YAPS_DELIM_SET_PKT_SIZE(yaps->aux_data, size);
    delim |= YAPS_DELIM_SET_PADDING(YAPS_CALC_PADDING(size));
    delim |= YAPS_DELIM_SET_POOL_ID(pool_id);
    delim |= YAPS_DELIM_SET_IRQ(irq);
    delim |= YAPS_DELIM_SET_CRC((uint32_t)morse_yaps_crc(delim));
    return delim;
}

void morse_yaps_hw_enable_irqs(struct driver_data *driverd, bool enable)
{
    morse_hw_irq_enable(driverd, MORSE_INT_YAPS_FC_PKT_WAITING_IRQN, enable);
    morse_hw_irq_enable(driverd, MORSE_INT_YAPS_FC_PACKET_FREED_UP_IRQN, enable);
}

void morse_yaps_hw_read_table(struct driver_data *driverd, struct morse_yaps_hw_table *tbl_ptr)
{
    morse_yaps_fill_aux_data_from_hw_tbl(driverd->chip_if->yaps.aux_data, tbl_ptr);

    morse_yaps_hw_enable_irqs(driverd, true);
}

static unsigned int morse_yaps_pages_required(struct morse_yaps *yaps, unsigned int size_bytes)
{

    return MORSE_INT_CEIL(size_bytes + yaps->aux_data->reserved_yaps_page_size, YAPS_PAGE_SIZE) +
           YAPS_METADATA_PAGE_COUNT +
           YAPS_PHANDLE_CORRUPTION_WAR_EXTRA_PAGE;
}


static bool morse_yaps_will_fit(struct morse_yaps *yaps,
                                struct mmpkt *mmpkt,
                                enum morse_yaps_to_chip_q tc_queue,
                                bool update)
{
    bool will_fit = true;
    struct mmpktview *view = mmpkt_open(mmpkt);
    uint32_t data_len = mmpkt_get_data_length(view);
    mmpkt_close(&view);
    const uint32_t pages_required = morse_yaps_pages_required(yaps, data_len);
    uint32_t pool_pages_avail = 0;
    uint32_t pkts_in_queue = 0;
    int queue_pkts_avail = 0;

    switch (tc_queue)
    {
        case MORSE_YAPS_TX_Q:
            pool_pages_avail = yaps->aux_data->status_regs.tc_tx_pool_num_pages;
            pkts_in_queue = yaps->aux_data->status_regs.tc_tx_num_pkts;
            queue_pkts_avail = yaps->aux_data->tc_tx_q_size - pkts_in_queue;
            break;

        case MORSE_YAPS_CMD_Q:
            pool_pages_avail = yaps->aux_data->status_regs.tc_cmd_pool_num_pages;
            pkts_in_queue = yaps->aux_data->status_regs.tc_cmd_num_pkts;
            queue_pkts_avail = yaps->aux_data->tc_cmd_q_size - pkts_in_queue;
            break;

        case MORSE_YAPS_BEACON_Q:
            pool_pages_avail = yaps->aux_data->status_regs.tc_beacon_pool_num_pages;
            pkts_in_queue = yaps->aux_data->status_regs.tc_beacon_num_pkts;
            queue_pkts_avail = yaps->aux_data->tc_beacon_q_size - pkts_in_queue;
            break;

        case MORSE_YAPS_MGMT_Q:
            pool_pages_avail = yaps->aux_data->status_regs.tc_mgmt_pool_num_pages;
            pkts_in_queue = yaps->aux_data->status_regs.tc_mgmt_num_pkts;
            queue_pkts_avail = yaps->aux_data->tc_mgmt_q_size - pkts_in_queue;
            break;

        default:
            MMLOG_ERR("yaps invalid tc queue\n");
    }

    MORSE_WARN_ON(queue_pkts_avail < 0);

    if (pages_required > pool_pages_avail)
    {
        will_fit = false;
    }

    if (queue_pkts_avail == 0)
    {
        will_fit = false;
    }

    if (will_fit && update)
    {
        switch (tc_queue)
        {
            case MORSE_YAPS_TX_Q:
                yaps->aux_data->status_regs.tc_tx_pool_num_pages -= pages_required;
                yaps->aux_data->status_regs.tc_tx_num_pkts += 1;
                break;

            case MORSE_YAPS_CMD_Q:
                yaps->aux_data->status_regs.tc_cmd_pool_num_pages -= pages_required;
                yaps->aux_data->status_regs.tc_cmd_num_pkts += 1;
                break;

            case MORSE_YAPS_BEACON_Q:
                yaps->aux_data->status_regs.tc_beacon_pool_num_pages -= pages_required;
                yaps->aux_data->status_regs.tc_beacon_num_pkts += 1;
                break;

            case MORSE_YAPS_MGMT_Q:
                yaps->aux_data->status_regs.tc_mgmt_pool_num_pages -= pages_required;
                yaps->aux_data->status_regs.tc_mgmt_num_pkts += 1;
                break;

            default:
                MMLOG_ERR("yaps invalid tc queue\n");
        }
    }

    return will_fit;
}

static int morse_yaps_hw_write_pkt_err_check(struct morse_yaps *yaps,
                                             struct mmpkt *mmpkt,
                                             enum morse_yaps_to_chip_q tc_queue)
{
    struct mmpktview *view = mmpkt_open(mmpkt);
    uint32_t data_len = mmpkt_get_data_length(view);
    mmpkt_close(&view);

    if (data_len + yaps->aux_data->reserved_yaps_page_size > YAPS_MAX_PKT_SIZE_BYTES)
    {
        return -EMSGSIZE;
    }
    if (tc_queue > MORSE_YAPS_NUM_TC_Q)
    {
        return -EINVAL;
    }
    if (!morse_yaps_will_fit(yaps, mmpkt, tc_queue, true))
    {
        return -ENOMEM;
    }

    return 0;
}

static int morse_yaps_hw_write_pkt(struct morse_yaps *yaps,
                                   struct mmpkt *mmpkt,
                                   enum morse_yaps_to_chip_q tc_queue,
                                   struct mmpkt *next_pkt)
{
    int ret = 0;

    ret = yaps_hw_lock(yaps);
    if (ret)
    {
        MMLOG_ERR("YAPS lock failed %d\n", ret);
        return ret;
    }


    ret = morse_yaps_hw_write_pkt_err_check(yaps, mmpkt, tc_queue);
    if (ret)
    {
        MMLOG_WRN("Write pkt check failed %d\n", ret);
        goto exit;
    }

    struct mmpktview *view = mmpkt_open(mmpkt);


    bool set_irq = (next_pkt == NULL) || !morse_yaps_will_fit(yaps, next_pkt, tc_queue, false);
    uint32_t delim = morse_yaps_delimiter(yaps, mmpkt_get_data_length(view), tc_queue, set_irq);
    delim = htole32(delim);
    mmpkt_prepend_data(view, (uint8_t *)&delim, sizeof(delim));

    ret = morse_trns_write_multi_byte(yaps->driverd,
                                      yaps->aux_data->yds_addr,
                                      mmpkt_get_data_start(view),
                                      mmpkt_get_data_length(view));


    mmpkt_remove_from_start(view, sizeof(delim));
    mmpkt_close(&view);

exit:
    yaps_hw_unlock(yaps);
    return ret;
}

static bool morse_yaps_is_valid_delimiter(uint32_t delim)
{
    uint8_t calc_crc = morse_yaps_crc(delim);
    int pkt_size = YAPS_DELIM_GET_PHANDLE_SIZE(delim);
    int padding = YAPS_DELIM_GET_PADDING(delim);

    if (calc_crc != YAPS_DELIM_GET_CRC(delim))
    {
        return false;
    }

    if (pkt_size == 0)
    {
        return false;
    }

    if ((pkt_size + padding) > YAPS_MAX_PKT_SIZE_BYTES)
    {
        return false;
    }


    if (YAPS_CALC_PADDING(pkt_size) != padding)
    {
        return false;
    }

    return true;
}

static int morse_yaps_hw_read_pkt(struct morse_yaps *yaps, struct mmpkt **mmpkt)
{
    int ret = 0;
    uint32_t delim;
    int total_len;
    int pkt_len;
    struct mmpktview *view;
    uint8_t *buf;

    ret = yaps_hw_lock(yaps);
    if (ret)
    {
        MMLOG_WRN("YAPS lock failed %d\n", ret);
        return ret;
    }


    ret = morse_trns_read_multi_byte(yaps->driverd,
                                     yaps->aux_data->ysl_addr,
                                     (uint8_t *)&delim,
                                     sizeof(delim));
    if (ret)
    {
        MMLOG_WRN("Failed to read delim %d\n", ret);
        goto exit;
    }

    delim = le32toh(delim);

    if (delim == 0x0)
    {
        goto exit;
    }

    if (!morse_yaps_is_valid_delimiter(delim))
    {

        MMOSAL_DEV_ASSERT(false);
        MMLOG_ERR("Invalid RX delim\n");
        ret = -EIO;
        goto exit;
    }

    pkt_len = YAPS_DELIM_GET_PKT_SIZE(yaps->aux_data, delim);
    total_len = pkt_len + YAPS_DELIM_GET_PADDING(delim);

    MMOSAL_ASSERT(total_len <= YAPS_MAX_RX_PAYLOAD);

    enum morse_yaps_from_chip_q fc_queue =
        (enum morse_yaps_from_chip_q)YAPS_DELIM_GET_POOL_ID(delim);

    uint8_t pkt_class = (fc_queue == MORSE_YAPS_RX_Q) ? MMHAL_WLAN_PKT_DATA_TID0 :
                                                        MMHAL_WLAN_PKT_COMMAND;


    *mmpkt = mmhal_wlan_alloc_mmpkt_for_rx(pkt_class, total_len, sizeof(struct mmdrv_rx_metadata));
    if (!*mmpkt)
    {
        mmdrv_host_stats_increment_datapath_driver_rx_alloc_failures();
        ret = -ENOMEM;
        if (fc_queue == MORSE_YAPS_RX_Q)
        {
            MMLOG_WRN("No mem for skb, dropping\n");

            *mmpkt = yaps->rx_scratch_pkt;

            mmpkt_truncate(*mmpkt, 0);
        }
        else
        {
            MMLOG_ERR("No mem for skb (q=%u)\n", fc_queue);
            goto exit;
        }
    }

    view = mmpkt_open(*mmpkt);

    buf = mmpkt_append(view, total_len);

    ret = morse_trns_read_multi_byte(yaps->driverd,

                                     yaps->aux_data->ysl_addr + 4,
                                     buf,
                                     total_len);
    if (ret)
    {
        mmdrv_host_stats_increment_datapath_driver_rx_read_failures();
        MMLOG_ERR("Failed to read packet content %d\n", ret);
        mmpkt_close(&view);

        if (*mmpkt != yaps->rx_scratch_pkt)
        {
            mmpkt_release(*mmpkt);
        }
        *mmpkt = NULL;
        goto exit;
    }

    mmpkt_close(&view);


    if (*mmpkt == yaps->rx_scratch_pkt)
    {
        *mmpkt = NULL;
        ret = -ENOMEM;
    }

exit:
    yaps_hw_unlock(yaps);
    return ret;
}

static int morse_yaps_hw_update_status(struct morse_yaps *yaps)
{
    int ret;
    uint32_t reg_read_timeout;

    struct morse_yaps_status_registers *status_regs = &yaps->aux_data->status_regs;

    ret = yaps_hw_lock(yaps);
    if (ret)
    {
        MMLOG_WRN("Yaps lock failed %d\n", ret);
        return ret;
    }

    reg_read_timeout = mmosal_get_time_ms() + YAPS_STATUS_REG_READ_TIMEOUT_MS;
    do {
        if (mmosal_time_has_passed(reg_read_timeout))
        {
            MMLOG_ERR("Timed out reading status registers: %d\n", ret);
            ret = -ETIMEDOUT;
            break;
        }
        ret = morse_trns_read_multi_byte(yaps->driverd,
                                         yaps->aux_data->status_regs_addr,
                                         (uint8_t *)status_regs,
                                         sizeof(*status_regs));
    } while (!ret && le32toh(status_regs->lock));


    if (ret)
    {
        if (ret != -ENODEV)
        {
            MORSE_WARN_ON(ret);
            MMLOG_ERR("Error reading yaps status registers: %d\n", ret);
        }
        driver_health_demand_check(yaps->driverd);
        goto exit_unlock;
    }

    status_regs->tc_tx_pool_num_pages = le32toh(status_regs->tc_tx_pool_num_pages);
    status_regs->tc_cmd_pool_num_pages = le32toh(status_regs->tc_cmd_pool_num_pages);
    status_regs->tc_beacon_pool_num_pages = le32toh(status_regs->tc_beacon_pool_num_pages);
    status_regs->tc_mgmt_pool_num_pages = le32toh(status_regs->tc_mgmt_pool_num_pages);
    status_regs->fc_rx_pool_num_pages = le32toh(status_regs->fc_rx_pool_num_pages);
    status_regs->fc_resp_pool_num_pages = le32toh(status_regs->fc_resp_pool_num_pages);
    status_regs->fc_tx_sts_pool_num_pages = le32toh(status_regs->fc_tx_sts_pool_num_pages);
    status_regs->fc_aux_pool_num_pages = le32toh(status_regs->fc_aux_pool_num_pages);
    status_regs->tc_tx_num_pkts = le32toh(status_regs->tc_tx_num_pkts);
    status_regs->tc_cmd_num_pkts = le32toh(status_regs->tc_cmd_num_pkts);
    status_regs->tc_beacon_num_pkts = le32toh(status_regs->tc_beacon_num_pkts);
    status_regs->tc_mgmt_num_pkts = le32toh(status_regs->tc_mgmt_num_pkts);
    status_regs->fc_num_pkts = le32toh(status_regs->fc_num_pkts);
    status_regs->fc_done_num_pkts = le32toh(status_regs->fc_done_num_pkts);
    status_regs->fc_rx_bytes_in_queue = le32toh(status_regs->fc_rx_bytes_in_queue);
    status_regs->tc_delim_crc_fail_detected = le32toh(status_regs->tc_delim_crc_fail_detected);
    status_regs->fc_host_ysl_status = le32toh(status_regs->fc_host_ysl_status);

    if (status_regs->tc_delim_crc_fail_detected)
    {

        MORSE_WARN_ON(status_regs->tc_delim_crc_fail_detected);
        MMLOG_ERR("to-chip yaps delimiter CRC fail\n");
        ret = -EIO;
    }


    if (status_regs->fc_num_pkts && ret == MORSE_SUCCESS)
    {
        driver_task_notify_event(yaps->driverd, DRV_EVT_RX_PEND);
    }

exit_unlock:
    yaps_hw_unlock(yaps);

    return ret;
}

const struct morse_yaps_ops morse_yaps_hw_ops = {
    .write_pkt = morse_yaps_hw_write_pkt,
    .read_pkt = morse_yaps_hw_read_pkt,
    .update_status = morse_yaps_hw_update_status,
};

int morse_yaps_hw_init(struct driver_data *driverd)
{
    int ret = 0;
    int flags;
    struct morse_yaps *yaps = NULL;

    morse_trns_claim(driverd);

    memset(&chip_if_state, 0, sizeof(struct morse_chip_if_state));
    driverd->chip_if = &chip_if_state;

    yaps = &driverd->chip_if->yaps;

    memset(&morse_yaps_hw_aux_data, 0, sizeof(struct morse_yaps_hw_aux_data));
    yaps->aux_data = &morse_yaps_hw_aux_data;

    yaps->ops = &morse_yaps_hw_ops;


    flags = MORSE_CHIP_IF_FLAGS_DATA |
            MORSE_CHIP_IF_FLAGS_COMMAND |
            MORSE_CHIP_IF_FLAGS_DIR_TO_HOST |
            MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP;

    ret = morse_yaps_init(driverd, yaps, flags);
    if (ret)
    {
        MMLOG_ERR("morse_yaps_init failed %d\n", ret);
        goto err_exit;
    }


    yaps->rx_scratch_pkt = mmhal_wlan_alloc_mmpkt_for_rx(MORSE_YAPS_RX_Q,
                                                         YAPS_MAX_RX_PAYLOAD,
                                                         sizeof(struct mmdrv_rx_metadata));
    if (yaps->rx_scratch_pkt == NULL)
    {
        MMLOG_ERR("Failed to allocate rx scratch packet from data pool\n");
        ret = -ENOMEM;
        goto err_exit;
    }


    morse_trns_release(driverd);

    morse_hw_irq_enable(driverd, MORSE_INT_HW_STOP_NOTIFICATION_NUM, true);

    return ret;

err_exit:
    if (yaps)
    {
        morse_yaps_finish(yaps);
    }
    morse_yaps_hw_finish(driverd);
    morse_trns_release(driverd);
    return ret;
}

void morse_yaps_hw_yaps_flush_tx_data(struct driver_data *driverd)
{
    struct morse_yaps *yaps = &driverd->chip_if->yaps;

    if ((yaps->flags & MORSE_CHIP_IF_FLAGS_DIR_TO_CHIP) &&
        (yaps->flags & (MORSE_CHIP_IF_FLAGS_DATA | MORSE_CHIP_IF_FLAGS_BEACON)))
    {
        morse_yaps_flush_tx_data(yaps);
    }
}

void morse_yaps_hw_finish(struct driver_data *driverd)
{
    struct morse_yaps *yaps;

    if (!driverd->chip_if)
    {
        return;
    }

    yaps = &driverd->chip_if->yaps;
    morse_yaps_hw_enable_irqs(driverd, false);
    mmpkt_release(yaps->rx_scratch_pkt);
    morse_yaps_finish(yaps);
    if (yaps->aux_data)
    {
        memset(yaps->aux_data, 0, sizeof(struct morse_yaps_hw_aux_data));
        yaps->aux_data = NULL;
    }
    yaps->ops = NULL;
    driverd->chip_if = NULL;
}

uint32_t morse_yaps_hw_get_tc_queue_space(struct morse_yaps *yaps,
                                          enum morse_yaps_to_chip_q tc_queue)
{
    switch (tc_queue)
    {
        case MORSE_YAPS_CMD_Q:
            return yaps->aux_data->tc_cmd_q_size - yaps->aux_data->status_regs.tc_cmd_num_pkts;
            break;

        case MORSE_YAPS_BEACON_Q:
            return yaps->aux_data->tc_beacon_q_size -
                   yaps->aux_data->status_regs.tc_beacon_num_pkts;
            break;

        case MORSE_YAPS_MGMT_Q:
            return yaps->aux_data->tc_mgmt_q_size - yaps->aux_data->status_regs.tc_mgmt_num_pkts;
            break;

        case MORSE_YAPS_TX_Q:
            return yaps->aux_data->tc_tx_q_size - yaps->aux_data->status_regs.tc_tx_num_pkts;
            break;

        default:
            MMLOG_ERR("Invalid to-chip queue %u\n", tc_queue);
            MMOSAL_ASSERT(false);
            return 0;
    }
}
