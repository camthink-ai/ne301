/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmdrv.h"
#include "mmpkt.h"
#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "umac/datapath/umac_datapath_data.h"
#include "umac/datapath/umac_datapath_private.h"
#include "umac/data/umac_data.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "dot11/dot11.h"
#include "dot11/dot11_utils.h"
#include "umac/ap/umac_ap.h"
#include "umac/core/umac_core.h"
#include "common/mac_address.h"
#include "umac/stats/umac_stats.h"
#include "umac/rc/umac_rc.h"

static void umac_datapath_process_rx_mgmt_frame_ap(struct umac_data *umacd,
                                                   struct umac_sta_data *stad,
                                                   struct mmpktview *rxbufview)
{
    const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);
    uint16_t frame_control_le = header->frame_control;
    uint16_t subtype = dot11_frame_control_get_subtype(frame_control_le);

    if (subtype == DOT11_FC_SUBTYPE_PROBE_REQ)
    {
        bool to_ds = dot11_frame_control_get_to_ds(header->frame_control);
        bool from_ds = dot11_frame_control_get_from_ds(header->frame_control);
        if (to_ds || from_ds)
        {
            MMLOG_WRN("Don't know how to handle this probe req (to_ds=%d, from_ds=%d)\n",
                      to_ds,
                      from_ds);
        }
        else
        {
            struct mmpkt *rxbuf = mmpkt_from_view(rxbufview);
            const struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(rxbuf);
            umac_supp_process_probe_req_frame(umacd,
                                              mmpkt_get_data_end(rxbufview),
                                              mmpkt_get_data_length(rxbufview),
                                              rx_metadata->rssi);
            umac_ap_handle_probe_req(umacd, rxbufview);
        }
    }
    else
    {
        if (subtype == DOT11_FC_SUBTYPE_ACTION)
        {
            umac_datapath_process_rx_action_frame(umacd, stad, rxbufview);
        }
        else
        {
            umac_supp_process_mgmt_frame(umacd, rxbufview);
        }
    }
}


static void umac_datapath_construct_80211_data_header_ap(struct umac_sta_data *stad,
                                                         const struct umac_8023_hdr *hdr_8023,
                                                         struct dot11_data_hdr *data_hdr)
{
    uint16_t frame_control = DOT11_MASK_FC_FROM_DS |
                             DOT11_FC_TYPE_DATA << DOT11_SHIFT_FC_TYPE |
                             DOT11_FC_SUBTYPE_QOS_DATA << DOT11_SHIFT_FC_SUBTYPE;

    umac_sta_data_get_bssid(stad, data_hdr->base.addr2);

    if (mm_mac_addr_is_multicast(hdr_8023->dest_addr))
    {

        mac_addr_copy(data_hdr->base.addr1, hdr_8023->dest_addr);
        mac_addr_copy(data_hdr->base.addr3, hdr_8023->src_addr);
    }
    else
    {
        umac_sta_data_get_peer_addr(stad, data_hdr->base.addr1);
        if (mm_mac_addr_is_equal(data_hdr->base.addr1, hdr_8023->dest_addr))
        {

            mac_addr_copy(data_hdr->base.addr3, hdr_8023->src_addr);
        }
        else
        {

            frame_control |= DOT11_MASK_FC_TO_DS;
            mac_addr_copy(data_hdr->base.addr3, hdr_8023->dest_addr);
            mac_addr_copy(data_hdr->addr4, hdr_8023->src_addr);
        }
    }

    data_hdr->base.frame_control = htole16(frame_control);
    MMLOG_DBG("802.11: FC=%04x, A1=" MM_MAC_ADDR_FMT ", A2=" MM_MAC_ADDR_FMT ", A3=" MM_MAC_ADDR_FMT
              "\n",
              data_hdr->base.frame_control,
              MM_MAC_ADDR_VAL(data_hdr->base.addr1),
              MM_MAC_ADDR_VAL(data_hdr->base.addr2),
              MM_MAC_ADDR_VAL(data_hdr->base.addr3));
}

enum mmwlan_status umac_datapath_tx_mgmt_frame_ap(struct umac_data *umacd,
                                                  struct mmpkt *txbuf,
                                                  struct mmrc_rate *mmrc_rate_override)
{
    enum mmwlan_status status;
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    struct mmpktview *txbufview = mmpkt_open(txbuf);
    struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(txbufview);
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(txbuf);

    uint32_t timeout_ms;
    uint16_t pause_mask;
    int key_id = -1;


    struct umac_sta_data *stad = data->ops->lookup_stad_by_peer_addr(umacd, NULL);
    MMOSAL_DEV_ASSERT(stad != NULL);
    if (stad == NULL)
    {
        MMLOG_WRN("Failed to get common stad\n");
        return MMWLAN_ERROR;
    }




    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);
    uint8_t seq_num_space = MMDRV_SEQ_NUM_BASELINE;
    DOT11_SEQUENCE_CONTROL_SET_SEQUENCE_NUMBER(header->sequence_control,
                                               sta_data->tx_seq_num_spaces[seq_num_space]++);




    tx_metadata->flags = MMDRV_TX_FLAG_IMMEDIATE_REPORT;
    if (key_id >= 0)
    {
        tx_metadata->flags |= MMDRV_TX_FLAG_HW_ENC;
        tx_metadata->key_idx = key_id;
    }

    tx_metadata->tid = MMWLAN_MAX_QOS_TID;

    if (mmrc_rate_override == NULL)
    {
        umac_rc_init_rate_table_mgmt(umacd, &tx_metadata->rc_data, false);
    }
    else
    {
        tx_metadata->rc_data.rates[0] = *mmrc_rate_override;
        tx_metadata->rc_data.rates[1].rate = MMRC_MCS_UNUSED;
        tx_metadata->rc_data.rates[2].rate = MMRC_MCS_UNUSED;
        tx_metadata->rc_data.rates[3].rate = MMRC_MCS_UNUSED;
    }


    pause_mask = ~MMDRV_PAUSE_SOURCE_MASK_PKTMEM;

    timeout_ms = MMWLAN_TX_DEFAULT_TIMEOUT_MS;
    status = umac_datapath_wait_for_tx_ready_(data, timeout_ms, pause_mask);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Tx Datapath Blocked\n");
        mmpkt_close(&txbufview);
        mmpkt_release(txbuf);
        umac_stats_increment_datapath_txq_frames_dropped(umacd);
        return status;
    }

    umac_stats_update_last_tx_time(umacd);

    mmpkt_close(&txbufview);
    if (mmdrv_tx_frame(txbuf, true) < 0)
    {
        MMLOG_WRN("Tx Error\n");
        return MMWLAN_ERROR;
    }

    return MMWLAN_SUCCESS;
}


const uint16_t frames_allowed_pre_association_ap_mode[] = {
    DOT11_VER_TYPE_SUBTYPE(0, MGMT, PROBE_REQ),
    DOT11_VER_TYPE_SUBTYPE(0, MGMT, AUTH),
    DOT11_VER_TYPE_SUBTYPE(0, MGMT, ASSOC_REQ),
    UINT16_MAX,
};


const struct umac_datapath_ops datapath_ops_ap = {
    .process_rx_mgmt_frame = umac_datapath_process_rx_mgmt_frame_ap,
    .lookup_stad_by_peer_addr = umac_ap_lookup_sta_by_addr,
    .lookup_stad_by_tx_dest_addr = umac_ap_lookup_sta_by_dest_addr,
    .lookup_stad_by_aid = umac_ap_lookup_sta_by_aid,
    .set_stad_sleep_state = umac_ap_set_stad_sleep_state,
    .is_stad_tx_paused = umac_ap_is_stad_paused,
    .enqueue_tx_frame = umac_ap_queue_pkt,
    .dequeue_tx_frame = umac_ap_tx_dequeue_frame,
    .construct_80211_data_header = umac_datapath_construct_80211_data_header_ap,
    .get_sta_state = umac_ap_get_state,
    .frames_allowed_pre_association = frames_allowed_pre_association_ap_mode,
};

void umac_datapath_configure_ap_mode(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    data->ops = &datapath_ops_ap;
    MMLOG_INF("Datapath configured for AP mode\n");
}
