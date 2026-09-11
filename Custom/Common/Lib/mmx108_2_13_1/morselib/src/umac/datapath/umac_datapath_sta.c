/*
 * Copyright 2025-2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mmpkt.h"
#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "umac/datapath/umac_datapath_private.h"
#include "umac/data/umac_data.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "dot11/dot11.h"
#include "dot11/dot11_utils.h"
#include "umac/connection/umac_connection.h"
#include "umac/interface/umac_interface.h"
#include "umac/scan/umac_scan.h"
#include "common/mac_address.h"
#include "umac/stats/umac_stats.h"

static void umac_datapath_process_rx_mgmt_frame_sta(struct umac_data *umacd,
                                                    struct umac_sta_data *stad,
                                                    struct mmpktview *rxbufview)
{
    const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);
    uint16_t frame_control_le = header->frame_control;
    uint16_t subtype = dot11_frame_control_get_subtype(frame_control_le);

    switch (subtype)
    {
        case DOT11_FC_SUBTYPE_ASSOC_RSP:
        case DOT11_FC_SUBTYPE_REASSOC_RSP:
            umac_connection_process_assoc_reassoc_rsp(umacd, rxbufview);
            break;

        case DOT11_FC_SUBTYPE_PROBE_RSP:
            umac_scan_process_probe_resp(umacd, rxbufview);
            break;

        case DOT11_FC_SUBTYPE_DISASSOC:
            umac_connection_process_disassoc_req(umacd, rxbufview);
            break;

        case DOT11_FC_SUBTYPE_AUTH:
            umac_connection_process_auth_resp(umacd, rxbufview);
            break;

        case DOT11_FC_SUBTYPE_DEAUTH:
            umac_connection_process_deauth_rx(umacd, rxbufview);
            break;

        case DOT11_FC_SUBTYPE_ACTION:
            umac_datapath_process_rx_action_frame(umacd, stad, rxbufview);
            break;

        default:
            MMLOG_WRN("Recieved unsupported MGMT frame: frame_control=0x%04x\n",
                      le16toh(frame_control_le));
            break;
    }
}


static void umac_datapath_construct_80211_data_header_sta(struct umac_sta_data *stad,
                                                          const struct umac_8023_hdr *hdr_8023,
                                                          struct dot11_data_hdr *data_hdr)
{
    uint16_t frame_control = DOT11_MASK_FC_TO_DS |
                             DOT11_FC_TYPE_DATA << DOT11_SHIFT_FC_TYPE |
                             DOT11_FC_SUBTYPE_QOS_DATA << DOT11_SHIFT_FC_SUBTYPE;

    umac_sta_data_get_bssid(stad, data_hdr->base.addr1);
    umac_interface_get_mac_addr(stad, data_hdr->base.addr2);
    mac_addr_copy(data_hdr->base.addr3, hdr_8023->dest_addr);
    if (!umac_interface_addr_matches_mac_addr(stad, hdr_8023->src_addr))
    {

        frame_control |= DOT11_MASK_FC_FROM_DS;
        mac_addr_copy(data_hdr->addr4, hdr_8023->src_addr);
    }
    data_hdr->base.frame_control = htole16(frame_control);

    MMLOG_DBG("802.11: FC=%04x, A1=" MM_MAC_ADDR_FMT ", A2=" MM_MAC_ADDR_FMT ", A3=" MM_MAC_ADDR_FMT
              "\n",
              data_hdr->base.frame_control,
              MM_MAC_ADDR_VAL(data_hdr->base.addr1),
              MM_MAC_ADDR_VAL(data_hdr->base.addr2),
              MM_MAC_ADDR_VAL(data_hdr->base.addr3));
}


static struct umac_sta_data *umac_datapath_lookup_stad_by_peer_addr_sta_mode(
    struct umac_data *umacd,
    const uint8_t *addr)
{
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (addr == NULL)
    {
        return stad;
    }

    MMLOG_DBG("Lookup peer " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(addr));

    if (stad == NULL)
    {
        return NULL;
    }

    if (mm_mac_addr_is_multicast(addr) || umac_sta_data_matches_peer_addr(stad, addr))
    {
        return stad;
    }
    else
    {
        return NULL;
    }
}

static struct umac_sta_data *umac_datapath_lookup_stad_by_tx_dest_addr_sta_mode(
    struct umac_data *umacd,
    const uint8_t *dest_addr)
{
    MMLOG_DBG("Lookup dest addr " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(dest_addr));


    return umac_connection_get_stad(umacd);
}

static struct umac_sta_data *umac_datapath_lookup_stad_by_aid_sta(struct umac_data *umacd,
                                                                  uint16_t aid)
{
    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        return NULL;
    }
    if (umac_sta_data_get_aid(stad) != aid)
    {
        MMLOG_WRN("AID mismatch (%u != %u)\n", umac_sta_data_get_aid(stad), aid);
        return NULL;
    }
    return stad;
}


static enum mmwlan_sta_state umac_datapath_get_state_sta(struct umac_sta_data *stad)
{
    MMOSAL_DEV_ASSERT(stad != NULL);
    return umac_connection_get_state(umac_sta_data_get_umacd(stad));
}


const uint16_t frames_allowed_pre_association_sta_mode[] = {
    DOT11_VER_TYPE_SUBTYPE(0, EXT, S1G_BEACON),
    DOT11_VER_TYPE_SUBTYPE(0, MGMT, PROBE_RSP),
    DOT11_VER_TYPE_SUBTYPE(0, MGMT, ACTION),
    UINT16_MAX,
};

static bool nullop_update_stad_state_sta(struct umac_sta_data *stad,
                                         const struct mmdrv_rx_metadata *metadata,
                                         uint16_t frame_control_le)
{
    MM_UNUSED(frame_control_le);
    return stad != NULL && metadata != NULL;
}

static void umac_datapath_tx_queue_frame_sta(struct umac_data *umacd,
                                             struct umac_sta_data *stad,
                                             struct mmpkt *txbuf)
{
    MMOSAL_TASK_ENTER_CRITICAL();
    umac_sta_data_queue_pkt(stad, txbuf);
    umac_stats_update_datapath_txq_high_water_mark(umacd, umac_sta_data_get_queued_len(stad));
    MMOSAL_TASK_EXIT_CRITICAL();
}

static bool umac_datapath_tx_dequeue_frame_sta(struct umac_data *umacd,
                                               struct umac_sta_data **stad_ptr,
                                               struct mmpkt **txbuf_ptr)
{
    MMOSAL_ASSERT(umacd && stad_ptr && txbuf_ptr);
    *stad_ptr = NULL;
    *txbuf_ptr = NULL;

    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    bool has_more = false;

    if (stad == NULL || umac_sta_data_is_paused(stad))
    {
        return false;
    }
    MMOSAL_TASK_ENTER_CRITICAL();
    *txbuf_ptr = umac_sta_data_pop_pkt(stad);
    has_more = umac_sta_data_get_queued_len(stad);
    MMOSAL_TASK_EXIT_CRITICAL();
    if (*txbuf_ptr != NULL)
    {
        *stad_ptr = stad;
    }
    return has_more;
}

static void umac_datapath_sta_handle_frame_unknown_sta(struct umac_data *umacd, const uint8_t *ta)
{
    MM_UNUSED(umacd);
    MM_UNUSED(ta);

}


static const struct umac_datapath_ops datapath_ops_sta = {
    .process_rx_mgmt_frame = umac_datapath_process_rx_mgmt_frame_sta,
    .lookup_stad_by_peer_addr = umac_datapath_lookup_stad_by_peer_addr_sta_mode,
    .lookup_stad_by_tx_dest_addr = umac_datapath_lookup_stad_by_tx_dest_addr_sta_mode,
    .lookup_stad_by_aid = umac_datapath_lookup_stad_by_aid_sta,
    .update_stad_state_rx = nullop_update_stad_state_sta,
    .is_stad_tx_paused = umac_sta_data_is_paused,
    .enqueue_tx_frame = umac_datapath_tx_queue_frame_sta,
    .dequeue_tx_frame = umac_datapath_tx_dequeue_frame_sta,
    .construct_80211_data_header = umac_datapath_construct_80211_data_header_sta,
    .get_sta_state = umac_datapath_get_state_sta,
    .supp_l2_sock_receive = umac_supp_l2_sock_receive,
    .handle_frame_unknown_sta = umac_datapath_sta_handle_frame_unknown_sta,
    .frames_allowed_pre_association = frames_allowed_pre_association_sta_mode,
};

const struct umac_datapath_ops *const umac_datapath_ops_sta = &datapath_ops_sta;
