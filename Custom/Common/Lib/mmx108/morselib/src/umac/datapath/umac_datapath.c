/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "dot11/dot11_ies.h"
#include "mmdrv.h"
#include "mmpkt_list.h"
#include "mmpkt.h"
#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/datapath/umac_datapath_private.h"
#include "umac/data/umac_data.h"
#include "umac/datapath/datapath_defrag.h"
#include "umac/regdb/umac_regdb.h"
#include "umac/supplicant_shim/umac_supp_shim.h"
#include "dot11/dot11.h"
#include "dot11/dot11_utils.h"
#include "umac/umac.h"
#include "umac/config/umac_config.h"
#include "umac/core/umac_core.h"
#include "common/mac_address.h"
#include "umac/ps/umac_ps.h"
#include "umac/stats/umac_stats.h"
#include "umac/interface/umac_interface.h"
#include "umac/connection/umac_connection.h"
#include "umac/rc/umac_rc.h"
#include "umac/ba/umac_ba.h"
#include "umac/twt/umac_twt.h"
#include "umac/ies/mmie.h"
#include "umac/frames/disassociation.h"
#include "umac/frames/deauthentication.h"

#define UMAC_802_1_HEADER_LEN 8
static const uint8_t snap_802_1h[] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };


#define MAX_QOS_DATA_MAC_HEADER_LEN (sizeof(struct dot11_data_hdr) + sizeof(struct dot11_qos_ctrl))


#define ETHERTYPE_THRESHOLD 1536


#define MAX_RX_PROCESS_PER_LOOP (5)


#define MAX_TX_PROCESS_PER_LOOP (5)


#define RX_REORDER_TIMEOUT_MS (100)


#define RX_REORDER_TIMER_PERIOD_MS (RX_REORDER_TIMEOUT_MS / 4)

#ifdef ENABLE_DATAPATH_TRACE
#include "mmtrace.h"
static mmtrace_channel datapath_channel_handle;
#define DATAPATH_TRACE_INIT()     datapath_channel_handle = mmtrace_register_channel("datapath")
#define DATAPATH_TRACE(_fmt, ...) mmtrace_printf(datapath_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define DATAPATH_TRACE_INIT() \
    do {                      \
    } while (0)
#define DATAPATH_TRACE(_fmt, ...) \
    do {                          \
    } while (0)
#endif

static bool umac_datapath_tx_is_paused(struct umac_datapath_data *data, uint16_t mask)
{
    return (data->tx_paused & mask) != 0;
}

enum mmwlan_status umac_datapath_wait_for_tx_ready_(struct umac_datapath_data *data,
                                                    uint32_t timeout_ms,
                                                    uint16_t mask)
{
    if (timeout_ms == UINT32_MAX)
    {
        while (umac_datapath_tx_is_paused(data, mask))
        {
            mmosal_semb_wait(data->tx_flowcontrol_sem, UINT32_MAX);
        }
    }
    else
    {
        uint32_t timeout_at = mmosal_get_time_ms() + timeout_ms;

        while (umac_datapath_tx_is_paused(data, mask))
        {
            int32_t sleep_time_ms = timeout_at - mmosal_get_time_ms();
            if (sleep_time_ms <= 0)
            {
                return MMWLAN_TIMED_OUT;
            }

            mmosal_semb_wait(data->tx_flowcontrol_sem, sleep_time_ms);
        }
    }

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_datapath_wait_for_tx_ready(struct umac_data *umacd, uint32_t timeout_ms)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    return umac_datapath_wait_for_tx_ready_(data, timeout_ms, UINT16_MAX);
}


static bool umac_datapath_validate_buf_len(struct mmpktview *view, uint32_t min_length)
{
    if (mmpkt_get_data_length(view) < min_length)
    {
        MMLOG_WRN("Data length too short. Received: %lu; Expected: %lu\n",
                  mmpkt_get_data_length(view),
                  min_length);
        return false;
    }

    return true;
}


static void umac_datapath_handle_signal_monitor(struct umac_data *umacd, int16_t new_rssi)
{
    enum umac_connection_signal_change status =
        umac_connection_check_signal_change(umacd, new_rssi);
    if (status != UMAC_CONNECTION_SIGNAL_CHANGE_NO_CHANGE)
    {
        umac_supp_notify_signal_change(umacd,
                                       new_rssi,
                                       (status == UMAC_CONNECTION_SIGNAL_CHANGE_ABOVE_THRESHOLD));
    }
}


static void umac_datapath_process_s1g_beacon(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    struct mmpkt *rxbuf = mmpkt_from_view(rxbufview);
    const struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(rxbuf);

    const struct dot11_s1g_beacon_hdr *s1g_header =
        (struct dot11_s1g_beacon_hdr *)mmpkt_remove_from_start(rxbufview, sizeof(*s1g_header));
    if (s1g_header == NULL)
    {
        MMLOG_WRN("S1G Beacon too short (%lu, expect at least %u)\n",
                  mmpkt_get_data_length(rxbufview),
                  sizeof(*s1g_header));
        return;
    }

    if (!umac_connection_addr_matches_bssid(umacd, s1g_header->source_addr))
    {
        MMLOG_DBG("Beacon received from another AP.\n");
        return;
    }


    if (dot11_frame_control_get_next_tbtt_present(s1g_header->frame_control))
    {
        (void)mmpkt_remove_from_start(rxbufview, DOT11_NEXT_TBTT_LEN);
    }

    if (dot11_frame_control_get_cssid_present(s1g_header->frame_control))
    {
        (void)mmpkt_remove_from_start(rxbufview, DOT11_CSSID_LEN);
    }

    if (dot11_frame_control_get_ano_present(s1g_header->frame_control))
    {
        (void)mmpkt_remove_from_start(rxbufview, DOT11_ANO_LEN);
    }

    int16_t new_rssi = rx_metadata->rssi;
    umac_stats_set_rssi(umacd, new_rssi);
    umac_datapath_handle_signal_monitor(umacd, new_rssi);

    umac_connection_process_beacon_ies(umacd,
                                       mmpkt_get_data_start(rxbufview),
                                       mmpkt_get_data_length(rxbufview));
}


static void umac_datapath_process_rx_extension_frame(struct umac_data *umacd,
                                                     struct umac_datapath_data *data,
                                                     struct mmpktview *rxbufview,
                                                     uint16_t frame_control)
{
    uint16_t subtype = dot11_frame_control_get_subtype(frame_control);
    MM_UNUSED(data);

    switch (subtype)
    {
        case DOT11_FC_SUBTYPE_S1G_BEACON:
            umac_datapath_process_s1g_beacon(umacd, rxbufview);
            break;

        default:
            MMLOG_WRN("Recieved unsupported EXT frame: frame_control=0x%04x\n",
                      le16toh(frame_control));
            break;
    }
}

void umac_datapath_process_rx_action_frame(struct umac_data *umacd,
                                           struct umac_sta_data *stad,
                                           struct mmpktview *rxbufview)
{
    const struct dot11_action *frame = (struct dot11_action *)mmpkt_get_data_start(rxbufview);

    MMLOG_DBG("Action Category recieved: %u\n", frame->field.category);
    switch (frame->field.category)
    {
        case DOT11_ACTION_CATEGORY_BLOCK_ACK:
            umac_ba_process_rx_frame(stad,
                                     mmpkt_get_data_start(rxbufview),
                                     mmpkt_get_data_length(rxbufview));
            break;

        case DOT11_ACTION_CATEGORY_PUBLIC:
        case DOT11_ACTION_CATEGORY_SA_QUERY:
        case DOT11_ACTION_CATEGORY_WNM:
            umac_supp_process_mgmt_frame(umacd, rxbufview);
            break;

        default:
            MMLOG_WRN("Unsupported Action Category: %u\n", frame->field.category);
            break;
    }
}


static void umac_datapath_process_unprotected_robust_mgmt_frame(struct umac_data *umacd,
                                                                struct mmpktview *rxbufview)
{
    const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);

    MMOSAL_ASSERT(umac_datapath_validate_buf_len(rxbufview, sizeof(*header)));

    if (frame_is_deauthentication(header))
    {
        const struct dot11_deauth *deauth = (struct dot11_deauth *)mmpkt_get_data_start(rxbufview);
        if (!umac_datapath_validate_buf_len(rxbufview, sizeof(*deauth)))
        {
            return;
        }

        MMLOG_DBG("Recieved unprotected deauth frame.\n");
        umac_supp_process_unprotected_deauth(umacd,
                                             le16toh(deauth->reason_code),
                                             dot11_get_sa(&deauth->hdr),
                                             dot11_get_da(&deauth->hdr));
    }
    else if (frame_is_disassociation(header))
    {
        const struct dot11_disassoc *disassoc =
            (struct dot11_disassoc *)mmpkt_get_data_start(rxbufview);
        if (!umac_datapath_validate_buf_len(rxbufview, sizeof(*disassoc)))
        {
            return;
        }

        MMLOG_DBG("Recieved unprotected disassoc frame.\n");
        umac_supp_process_unprotected_disassoc(umacd,
                                               le16toh(disassoc->reason_code),
                                               dot11_get_sa(&disassoc->hdr),
                                               dot11_get_da(&disassoc->hdr));
    }
    else
    {
        MMLOG_DBG("Recieved unexpected unprotected robust management frame.\n");
    }
}


static void umac_datapath_process_rx_mgmt_frame(struct umac_data *umacd,
                                                struct umac_sta_data *stad,
                                                struct umac_datapath_data *data,
                                                struct mmpktview *rxbufview)
{
    const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);

    MMOSAL_ASSERT(umac_datapath_validate_buf_len(rxbufview, sizeof(*header)));

    uint16_t frame_control_le = header->frame_control;

    if (frame_is_robust_mgmt(rxbufview))
    {
        if (stad == NULL)
        {

            return;
        }

        if (!dot11_frame_control_get_protected(frame_control_le) &&
            umac_sta_data_pmf_is_required(stad))
        {
            const uint8_t *frame_data = mmpkt_get_data_start(rxbufview) + sizeof(*header);
            size_t frame_data_len = mmpkt_get_data_length(rxbufview) - sizeof(*header);


            if (mm_mac_addr_is_multicast(dot11_get_da(header)) &&
                (ie_mmie_find(frame_data, frame_data_len) != NULL))
            {
                if (!bip_is_valid(stad, header, frame_data, frame_data_len))
                {
                    MMLOG_INF("Invalid frame security, dropping.\n");
                    return;
                }
            }
            else
            {
                umac_datapath_process_unprotected_robust_mgmt_frame(umacd, rxbufview);
                return;
            }
        }
    }

    MMOSAL_DEV_ASSERT(data->ops != NULL);
    data->ops->process_rx_mgmt_frame(umacd, stad, rxbufview);
}

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


static void umac_datapath_generate_8023_header(const uint8_t *dest_addr,
                                               const uint8_t *src_addr,
                                               uint16_t ethertype,
                                               struct umac_8023_hdr *header)
{
    mac_addr_copy(header->src_addr, src_addr);
    mac_addr_copy(header->dest_addr, dest_addr);
    header->ethertype_be = htobe16(ethertype);
}


static uint16_t umac_datapath_get_llc_ethertype(struct mmpktview *view)
{
    if (!umac_datapath_validate_buf_len(view, UMAC_802_1_HEADER_LEN))
    {
        MMLOG_INF("Packet too short for LLC/SNAP header\n");
        return 0;
    }
    uint8_t *header = mmpkt_get_data_start(view);

    if (memcmp(snap_802_1h, header, sizeof(snap_802_1h)) != 0)
    {
        MMLOG_DUMP_INF("Unable to find matching LLC/SNAP in buffer:\n    ",
                       header,
                       sizeof(snap_802_1h));
        return 0;
    }

    uint16_t ethertype;
    PACK_BE16(ethertype, (header + sizeof(snap_802_1h)));

    return ethertype;
}


static bool umac_datapath_is_eapol_frame(struct mmpktview *rxbufview)
{
    return (umac_datapath_get_llc_ethertype(rxbufview) == ETHERTYPE_EAPOL);
}


static void umac_datapath_process_rx_eapol_frame(struct umac_data *umacd,
                                                 struct umac_datapath_data *data,
                                                 struct mmpktview *rxbufview,
                                                 const struct dot11_hdr *header)
{
    MMOSAL_DEV_ASSERT(data->ops != NULL);



    MMOSAL_ASSERT(mmpkt_remove_from_start(rxbufview, UMAC_802_1_HEADER_LEN) != NULL);

    umac_supp_l2_sock_receive(umacd,
                              mmpkt_get_data_start(rxbufview),
                              mmpkt_get_data_length(rxbufview),
                              dot11_get_sa(header));
}


static void umac_datapath_process_rx_data_frame_after_reorder(
    struct umac_sta_data *stad,
    struct umac_datapath_sta_data *sta_data,
    struct mmpkt *rxbuf,
    struct mmpktview *rxbufview)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);


    const struct dot11_data_hdr *data_hdr =
        (const struct dot11_data_hdr *)mmpkt_get_data_start(rxbufview);
    const size_t data_hdr_len = dot11_data_hdr_get_len(data_hdr);

    data_hdr = (const struct dot11_data_hdr *)mmpkt_remove_from_start(rxbufview, data_hdr_len);

    MMOSAL_ASSERT(data_hdr);

    const struct dot11_hdr *header = &data_hdr->base;

    uint16_t llc_ethertype;
    uint8_t tid_index = MMDRV_SEQ_NUM_BASELINE;
    const struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(rxbuf);
    struct umac_8023_hdr header_8023 = { 0 };

    if (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_QOS_DATA)
    {
        const struct dot11_qos_ctrl *qos_control =
            (struct dot11_qos_ctrl *)mmpkt_remove_from_start(rxbufview, sizeof(*qos_control));

        MMOSAL_ASSERT(qos_control);

        if (!mm_mac_addr_is_multicast(dot11_get_ra(header)))
        {
            tid_index = dot11_qos_control_get_tid(qos_control->field);
        }
    }


    if ((umac_sta_data_get_security_type(stad) != MMWLAN_OPEN) &&
        !dot11_frame_control_get_protected(header->frame_control) &&
        !umac_datapath_is_eapol_frame(rxbufview))
    {
        MMLOG_INF("Received NON EAPOL frame in plain text.\n");
        goto drop;
    }

    if (dot11_frame_control_get_protected(header->frame_control))
    {
        if (!(rx_metadata->flags & MMDRV_RX_FLAG_DECRYPTED))
        {

            MMLOG_WRN("Received frame without HW Decryption (FC: 0x%04x, SEQ: 0x%04x).\n",
                      le16toh(header->frame_control),
                      le16toh(header->sequence_control));
            goto drop;
        }

        uint8_t *ccmp_header = mmpkt_remove_from_start(rxbufview, DOT11_CCMP_HEADER_LEN);
        if (ccmp_header == NULL ||
            !ccmp_is_valid(stad, ccmp_header, UMAC_KEY_RX_COUNTER_SPACE_DEFAULT))
        {

            umac_stats_increment_datapath_rx_ccmp_failures(umacd);
            goto drop;
        }


        if (mmpkt_remove_from_end(rxbufview, DOT11_CCMP_128_MIC_LEN) == NULL)
        {
            MMLOG_WRN("Drop frame as rxbuf was shorter than expected");
            goto drop;
        }
    }


    if (tid_index <= MMWLAN_MAX_QOS_TID &&
        (dot11_frame_control_get_protected(header->frame_control) ||
         umac_sta_data_get_security_type(stad) == MMWLAN_OPEN))
    {
        umac_ba_set_expected_rx_seq_num(stad, tid_index, dot11_get_next_sequence_control(header));
    }




    if (mm_mac_addr_is_broadcast(dot11_get_da(header)) ||
        mm_mac_addr_is_multicast(dot11_get_da(header)))
    {
        if (dot11_frame_control_get_more_fragments(header->frame_control))
        {
            MMLOG_INF("Drop Mcast/Bcast frame with fragment bit on\n");
            goto drop;
        }


        if (dot11_frame_control_get_from_ds(header->frame_control) &&
            umac_interface_addr_matches_mac_addr(stad, dot11_get_sa_data(data_hdr)))
        {
            MMLOG_DBG("Filter out Bcast frame which AP relayed for us\n");
            goto drop;
        }
    }
    else
    {

        MMOSAL_DEV_ASSERT(mmpkt_contains_ptr(rxbufview, (const void *)data_hdr));
        rx_metadata = NULL;
        rxbuf =
            datapath_defrag(umacd, &sta_data->defrag_data, &data_hdr, &rxbufview, rxbuf, tid_index);
        if (rxbuf == NULL)
        {

            MMOSAL_DEV_ASSERT(rxbufview == NULL);
            return;
        }
        else
        {
            header = &data_hdr->base;
            MMOSAL_DEV_ASSERT(mmpkt_from_view(rxbufview) == rxbuf);
            MMOSAL_DEV_ASSERT(mmpkt_contains_ptr(rxbufview, (const void *)data_hdr));
        }
    }

    if (umac_datapath_is_eapol_frame(rxbufview))
    {
        if (dot11_is_4addr_hdr(header->frame_control))
        {
            MMLOG_INF("Received 4-address EAPOL frame. Not supported\n");
            goto drop;
        }
        umac_datapath_process_rx_eapol_frame(umacd, data, rxbufview, header);
        goto drop;
    }

    MMOSAL_DEV_ASSERT(data->ops != NULL);
    if (data->ops->get_sta_state(stad) != MMWLAN_STA_CONNECTED)
    {
        MMLOG_DBG("Controlled Port is currently closed.\n");
        goto drop;
    }


    llc_ethertype = umac_datapath_get_llc_ethertype(rxbufview);
    if (!llc_ethertype)
    {
        goto drop;
    }

    umac_datapath_generate_8023_header(dot11_get_da(header),
                                       dot11_get_sa_data(data_hdr),
                                       llc_ethertype,
                                       &header_8023);


    mmpkt_remove_from_start(rxbufview, UMAC_802_1_HEADER_LEN);


    enum mmwlan_vif vif = MMWLAN_VIF_UNSPECIFIED;
    if (umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA) != UMAC_INTERFACE_VIF_ID_INVALID)
    {
        vif = MMWLAN_VIF_STA;
    }
    else if (umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP) != UMAC_INTERFACE_VIF_ID_INVALID)
    {
        vif = MMWLAN_VIF_AP;
    }
    else
    {
        MMLOG_WRN("Invalid RX VIF\n");
        goto drop;
    }

    mmwlan_rx_pkt_ext_cb_t rx_pkt_cb;
    void *arg = NULL;

    rx_pkt_cb = umac_interface_get_rx_pkt_ext_cb(umacd, vif, &arg);
    if (rx_pkt_cb != NULL)
    {
        struct mmwlan_rx_metadata metadata = {
            .vif = vif,
            .tid = tid_index,
            .ta = dot11_get_ta(&data_hdr->base),
        };

        mmpkt_prepend_data(rxbufview, (const uint8_t *)&header_8023, sizeof(header_8023));
        mmpkt_close(&rxbufview);
        rx_pkt_cb(rxbuf, &metadata, arg);

        return;
    }
    if (data->rx_pkt_callback != NULL)
    {
        mmpkt_prepend_data(rxbufview, (const uint8_t *)&header_8023, sizeof(header_8023));
        mmpkt_close(&rxbufview);
        data->rx_pkt_callback(rxbuf, data->rx_arg);

        return;
    }
    if (data->rx_callback != NULL)
    {
        data->rx_callback((uint8_t *)&header_8023,
                          sizeof(header_8023),
                          mmpkt_get_data_start(rxbufview),
                          mmpkt_get_data_length(rxbufview),
                          data->rx_arg);
        goto drop;
    }
    MMLOG_WRN("No RX callback registered by the network stack.\n");
    goto drop;

drop:
    mmpkt_close(&rxbufview);
    mmpkt_release(rxbuf);
}


static void umac_datapath_flush_rx_reorder_list(struct umac_sta_data *stad,
                                                struct umac_datapath_sta_data *sta_data)
{
    struct mmpkt *pkt;
    while ((pkt = mmpkt_list_dequeue(&sta_data->rx_reorder_list)) != NULL)
    {
        struct mmpktview *view = mmpkt_open(pkt);
        umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, pkt, view);
    }
}

void umac_datapath_flush_rx_reorder_list_for_tid(struct umac_sta_data *stad, uint16_t tid)
{
    struct mmpkt *pkt;
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);

    if (sta_data->rx_reorder_tid != tid)
    {
        return;
    }

    umac_datapath_flush_rx_reorder_list(stad, sta_data);

    while ((pkt = mmpkt_list_dequeue(&sta_data->rx_reorder_list)) != NULL)
    {
        struct mmpktview *view = mmpkt_open(pkt);
        umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, pkt, view);
    }
}


static void umac_datapath_evaluate_rx_reorder_list(struct umac_data *umacd,
                                                   struct umac_sta_data *stad,
                                                   struct umac_datapath_sta_data *sta_data)
{
    while (!mmpkt_list_is_empty(&sta_data->rx_reorder_list))
    {
        bool dequeue = false;
        struct mmpkt *pkt = mmpkt_list_peek(&sta_data->rx_reorder_list);
        struct mmpktview *view = mmpkt_open(pkt);
        const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(view);
        uint16_t seq_ctrl = le16toh(header->sequence_control);
        int32_t ret;

        ret = umac_ba_get_expected_rx_seq_num(stad, sta_data->rx_reorder_tid);
        if (ret < 0 || seq_ctrl == (uint16_t)ret)
        {
            dequeue = true;
        }
        else if (mmosal_time_has_passed(
                     mmpkt_get_metadata(pkt).rx->read_timestamp_ms + RX_REORDER_TIMEOUT_MS))
        {
            umac_stats_increment_datapath_rx_reorder_timedout(umacd);
            dequeue = true;
        }

        if (dequeue)
        {
            mmpkt_list_remove(&sta_data->rx_reorder_list, pkt);
            umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, pkt, view);
        }
        else
        {
            mmpkt_close(&view);
            return;
        }
    }
}

static void umac_datapath_rx_reorder_timeout_handler(void *arg1, void *arg2)
{
    struct umac_data *umacd = (struct umac_data *)arg1;
    struct umac_sta_data *stad = (struct umac_sta_data *)arg2;
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);

    umac_datapath_evaluate_rx_reorder_list(umacd, stad, sta_data);
    if (!mmpkt_list_is_empty(&sta_data->rx_reorder_list))
    {
        bool ok = umac_core_register_timeout(umacd,
                                             RX_REORDER_TIMER_PERIOD_MS,
                                             umac_datapath_rx_reorder_timeout_handler,
                                             umacd,
                                             stad);
        if (!ok)
        {
            MMLOG_WRN("Failed to schedule RX reorder timeout\n");
        }
    }
}


static void umac_datapath_add_rx_mpdu_to_reorder_list(struct umac_data *umacd,
                                                      struct umac_sta_data *stad,
                                                      struct umac_datapath_sta_data *sta_data,
                                                      struct mmpkt *rxbuf,
                                                      uint16_t seq_ctrl,
                                                      uint8_t reorder_list_maxlen)
{
    struct mmpkt *walk = NULL;
    struct mmpkt *next;
    struct mmpkt *head;
    struct mmpktview *headview;
    const struct dot11_hdr *head_header;
    uint16_t head_seq_ctrl;
    bool reorder_list_full;

    if (mmpkt_list_is_empty(&sta_data->rx_reorder_list))
    {
        bool ok = umac_core_register_timeout(umacd,
                                             RX_REORDER_TIMER_PERIOD_MS,
                                             umac_datapath_rx_reorder_timeout_handler,
                                             umacd,
                                             stad);
        if (!ok)
        {
            MMLOG_WRN("Failed to schedule RX reorder timeout\n");
        }

        mmpkt_list_append(&sta_data->rx_reorder_list, rxbuf);
        umac_stats_increment_datapath_rx_reorder_total(umacd);
        umac_stats_update_datapath_rx_reorder_list_high_water_mark(
            umacd,
            mmpkt_list_length(&sta_data->rx_reorder_list));
        return;
    }

    head = mmpkt_list_peek(&sta_data->rx_reorder_list);
    headview = mmpkt_open(head);
    head_header = (struct dot11_hdr *)mmpkt_get_data_start(headview);
    head_seq_ctrl = le16toh(head_header->sequence_control);
    head_header = NULL;
    mmpkt_close(&headview);
    head = NULL;

    if (head_seq_ctrl == seq_ctrl)
    {
        mmpkt_release(rxbuf);
        umac_stats_increment_datapath_rx_reorder_retransmit_drops(umacd);
        return;
    }

    reorder_list_full = mmpkt_list_length(&sta_data->rx_reorder_list) >= reorder_list_maxlen;


    if (dot11_sequence_control_lt(seq_ctrl, head_seq_ctrl))
    {
        if (reorder_list_full)
        {

            mmpkt_release(rxbuf);
            umac_stats_increment_datapath_rx_reorder_overflow(umacd);
        }
        else
        {
            mmpkt_list_prepend(&sta_data->rx_reorder_list, rxbuf);
            umac_stats_increment_datapath_rx_reorder_total(umacd);
            umac_stats_update_datapath_rx_reorder_list_high_water_mark(
                umacd,
                mmpkt_list_length(&sta_data->rx_reorder_list));
        }
        return;
    }

    if (reorder_list_full)
    {
        struct mmpkt *deq;
        struct mmpktview *deqview;


        deq = mmpkt_list_dequeue(&sta_data->rx_reorder_list);
        deqview = mmpkt_open(deq);
        umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, deq, deqview);
        umac_stats_increment_datapath_rx_reorder_overflow(umacd);
    }



    MMPKT_LIST_WALK(&sta_data->rx_reorder_list, walk, next)
    {
        struct mmpktview *nextview;
        const struct dot11_hdr *next_header;
        uint16_t next_seq_ctrl;

        if (next == NULL)
        {

            break;
        }

        nextview = mmpkt_open(next);
        next_header = (struct dot11_hdr *)mmpkt_get_data_start(nextview);
        next_seq_ctrl = le16toh(next_header->sequence_control);
        mmpkt_close(&nextview);

        if (next_seq_ctrl == seq_ctrl)
        {
            mmpkt_release(rxbuf);
            umac_stats_increment_datapath_rx_reorder_retransmit_drops(umacd);
            return;
        }

        if (dot11_sequence_control_lt(seq_ctrl, next_seq_ctrl))
        {

            break;
        }
    }

    MMOSAL_ASSERT(walk != NULL);
    mmpkt_list_insert_after(&sta_data->rx_reorder_list, walk, rxbuf);
    umac_stats_increment_datapath_rx_reorder_total(umacd);
    umac_stats_update_datapath_rx_reorder_list_high_water_mark(
        umacd,
        mmpkt_list_length(&sta_data->rx_reorder_list));


    umac_datapath_evaluate_rx_reorder_list(umacd, stad, sta_data);
}


static void umac_datapath_process_rx_data_frame(struct umac_data *umacd,
                                                struct umac_sta_data *stad,
                                                struct mmpkt *rxbuf,
                                                struct mmpktview *rxbufview)
{
    int32_t ret;
    uint16_t seq_ctrl;
    uint16_t expected_seq_ctrl;
    uint8_t tid_index = MMDRV_SEQ_NUM_BASELINE;
    const struct dot11_data_hdr *data_hdr =
        (const struct dot11_data_hdr *)mmpkt_get_data_start(rxbufview);
    const struct dot11_hdr *const header = &data_hdr->base;
    const size_t data_hdr_len = dot11_data_hdr_get_len(data_hdr);
    uint8_t reorder_buf_size;

    if (stad == NULL)
    {
        MMLOG_DBG("Dropping frame, not transmitted from our AP.\n");
        goto drop;
    }

    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);

    if ((dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_NULL_DATA) ||
        (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_QOS_NULL))
    {
        MMLOG_DBG("Dropping NULL data frame.\n");
        goto drop;
    }

    if (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_QOS_DATA)
    {
        MMLOG_VRB("Remove extra QOS CNTL bytes (%p)\n", rxbuf);

        const struct dot11_qos_ctrl *qos_control =
            (const struct dot11_qos_ctrl *)((const uint8_t *)data_hdr + data_hdr_len);

        if (!mm_mac_addr_is_multicast(dot11_get_ra(header)))
        {
            tid_index = dot11_qos_control_get_tid(qos_control->field);
        }
    }

    reorder_buf_size = umac_ba_get_reorder_buffer_size(stad, tid_index);

    if (tid_index > MMWLAN_MAX_QOS_TID || reorder_buf_size == 0)
    {
        umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, rxbuf, rxbufview);
        return;
    }


    if (sta_data->rx_reorder_tid != tid_index)
    {
        umac_datapath_flush_rx_reorder_list(stad, sta_data);
        sta_data->rx_reorder_tid = tid_index;
    }

    ret = umac_ba_get_expected_rx_seq_num(stad, tid_index);
    if (ret < 0)
    {

        umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, rxbuf, rxbufview);
        return;
    }


    expected_seq_ctrl = (uint16_t)ret;

    seq_ctrl = le16toh(header->sequence_control);
    if (seq_ctrl == expected_seq_ctrl)
    {		
        umac_datapath_process_rx_data_frame_after_reorder(stad, sta_data, rxbuf, rxbufview);
        rxbuf = NULL;
        rxbufview = NULL;
        umac_datapath_evaluate_rx_reorder_list(umacd, stad, sta_data);
        return;
    }
    else
    {
        if (dot11_sequence_control_lt(seq_ctrl, expected_seq_ctrl))
        {

            umac_stats_increment_datapath_rx_reorder_outdated_drops(umacd);
            MMLOG_DBG("Dropping outdated frame (SEQ: 0x%x, EXP: 0x%x)\n",
                      seq_ctrl,
                      expected_seq_ctrl);
            goto drop;
        }
        else
        {

            mmpkt_close(&rxbufview);
            umac_datapath_add_rx_mpdu_to_reorder_list(umacd,
                                                      stad,
                                                      sta_data,
                                                      rxbuf,
                                                      seq_ctrl,
                                                      reorder_buf_size);
            rxbuf = NULL;
            return;
        }
    }

drop:
    mmpkt_close(&rxbufview);
    mmpkt_release(rxbuf);
}


static bool umac_datapath_process_mgmt_frame_ccmp_header(struct umac_data *umacd,
                                                         struct umac_sta_data *stad,
                                                         struct mmpktview *rxbufview)
{
    const struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);
    uint16_t frame_control_le = header->frame_control;

    if (!dot11_frame_control_get_protected(frame_control_le))
    {
        return true;
    }

    struct mmpkt *rxbuf = mmpkt_from_view(rxbufview);
    const struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(rxbuf);
    if (!(rx_metadata->flags & MMDRV_RX_FLAG_DECRYPTED))
    {

        MMLOG_WRN("Received frame without HW Decryption (FC: 0x%04x).\n",
                  le16toh(frame_control_le));
        return false;
    }


    mmpkt_remove_from_start(rxbufview, sizeof(*header));


    uint8_t *ccmp_header = mmpkt_remove_from_start(rxbufview, DOT11_CCMP_HEADER_LEN);
    if (!ccmp_is_valid(stad, ccmp_header, UMAC_KEY_RX_COUNTER_SPACE_IND_ROBUST_MGMT))
    {
        MMLOG_WRN("Unable to validate frame security, dropping.\n");
        umac_stats_increment_datapath_rx_ccmp_failures(umacd);
        return false;
    }


    uint8_t *hdr_dest = mmpkt_prepend(rxbufview, sizeof(*header));
    memmove(hdr_dest, header, sizeof(*header));

    return true;
}


static enum mmwlan_frame_filter_flag umac_datapath_rx_frame_filter_matches(struct umac_data *umacd,
                                                                           enum dot11_fc_type type,
                                                                           uint16_t subtype)
{
    const struct umac_datapath_data *datapath = umac_data_get_datapath(umacd);

    if (type == DOT11_FC_TYPE_MGMT)
    {
        return (enum mmwlan_frame_filter_flag)(datapath->rx_frame_filter & (1u << subtype));
    }
    return MMWLAN_FRAME_NO_MATCH;
}


static void umac_datapath_process_rx_other_frame(struct umac_data *umacd,
                                                 struct umac_sta_data *stad,
                                                 struct umac_datapath_data *data,
                                                 struct mmpkt *rxbuf,
                                                 struct mmpktview *rxbufview)
{
    struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);

    MMOSAL_ASSERT(!dot11_is_4addr_hdr(header->frame_control));

    const enum dot11_fc_type frame_type =
        (enum dot11_fc_type)dot11_frame_control_get_type(header->frame_control);
    const uint16_t frame_subtype = dot11_frame_control_get_subtype(header->frame_control);
    const enum mmwlan_frame_filter_flag frame_filter_flag =
        umac_datapath_rx_frame_filter_matches(umacd, frame_type, frame_subtype);

    if (frame_type == DOT11_FC_TYPE_MGMT &&
        !umac_datapath_process_mgmt_frame_ccmp_header(umacd, stad, rxbufview))
    {
        MMLOG_WRN("Dropping management frame due to CCMP header (aid=%u)\n",
                  stad ? umac_sta_data_get_aid(stad) : -1);
        goto drop;
    }

    if (frame_filter_flag != MMWLAN_FRAME_NO_MATCH && data->rx_frame_cb != NULL)
    {
        struct mmdrv_rx_metadata *rx_metadata = mmdrv_get_rx_metadata(rxbuf);
        struct mmwlan_rx_frame_info frame_info = {
            .frame_filter_flag = frame_filter_flag,
            .buf = mmpkt_get_data_start(rxbufview),
            .buf_len = mmpkt_get_data_length(rxbufview),
            .freq_100khz = rx_metadata->freq_100khz,
            .rssi_dbm = rx_metadata->rssi,
            .bw_mhz = rx_metadata->bw_mhz,
        };
        data->rx_frame_cb(&frame_info, data->rx_frame_cb_arg);
    }

    switch (frame_type)
    {
        case DOT11_FC_TYPE_MGMT:
            umac_datapath_process_rx_mgmt_frame(umacd, stad, data, rxbufview);
            break;

        case DOT11_FC_TYPE_EXT:
            umac_datapath_process_rx_extension_frame(umacd, data, rxbufview, header->frame_control);
            break;

        case DOT11_FC_TYPE_CTRL:
            MMLOG_INF("Control frame ignored.\n");
            break;

        case DOT11_FC_TYPE_DATA:

            MMOSAL_ASSERT(false);
            break;

        default:
            MMLOG_ERR("Unexpected frame type received.\n");
            break;
    }

drop:
    mmpkt_close(&rxbufview);
    mmpkt_release(rxbuf);
}


static bool umac_datapath_is_rx_frame_duplicate(struct umac_datapath_sta_data *sta_data,
                                                struct mmpktview *rxbufview)
{
    uint8_t tid_index;
    struct dot11_data_hdr *data_hdr = (struct dot11_data_hdr *)mmpkt_get_data_start(rxbufview);
    struct dot11_hdr *header = &data_hdr->base;
    const uint32_t data_hdr_len = dot11_data_hdr_get_len(data_hdr);


    if (dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_EXT)
    {
        return false;
    }


    MMOSAL_ASSERT(umac_datapath_validate_buf_len(rxbufview, data_hdr_len));


    if (mm_mac_addr_is_broadcast(dot11_get_da(header)) ||
        mm_mac_addr_is_multicast(dot11_get_da(header)))
    {
        return false;
    }


    if (dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_CTRL)
    {
        return false;
    }


    if ((dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_DATA) &&
        (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_QOS_NULL))
    {
        return false;
    }

    if ((dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_DATA) &&
        (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_QOS_DATA))
    {
        const struct dot11_qos_ctrl *qos_control =
            (struct dot11_qos_ctrl *)(mmpkt_get_data_start(rxbufview) + data_hdr_len);
        if (!umac_datapath_validate_buf_len(rxbufview, data_hdr_len + sizeof(*qos_control)))
        {

            return true;
        }
        tid_index = dot11_qos_control_get_tid(qos_control->field);
    }
    else
    {
        tid_index = MMDRV_SEQ_NUM_BASELINE;
    }

    if (tid_index >= MMDRV_SEQ_NUM_SPACES)
    {
        MMLOG_WRN("Received out of range TID: %d.\n", tid_index);

        return true;
    }


    if (dot11_frame_control_get_retry(header->frame_control) &&
        sta_data->rx_seq_num_spaces[tid_index] == header->sequence_control)
    {

        MMLOG_INF("Duplicate detected SEQ: 0x%x\n", header->sequence_control);
        return true;
    }


    sta_data->rx_seq_num_spaces[tid_index] = header->sequence_control;
    return false;
}


static bool umac_datapath_rx_frame_allowed_pre_association(struct umac_datapath_data *data,
                                                           uint16_t frame_ver_type_subtype,
                                                           uint16_t frame_control_le)
{
    MMOSAL_DEV_ASSERT(data != NULL);
    MMOSAL_DEV_ASSERT(data->ops != NULL);
    MMOSAL_DEV_ASSERT(data->ops->frames_allowed_pre_association != NULL);


    if (frame_ver_type_subtype != DOT11_VER_TYPE_SUBTYPE(0, EXT, S1G_BEACON) &&
        dot11_frame_control_get_protected(frame_control_le))
    {
        return false;
    }

    for (const uint16_t *iter = data->ops->frames_allowed_pre_association; *iter != UINT16_MAX;
         iter++)
    {
        if (frame_ver_type_subtype == *iter)
        {
            return true;
        }
    }
    return false;
}

static bool umac_datapath_filter_all_beacons(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    return data->filter_beacons;
}

void umac_datapath_set_filter_all_beacons(struct umac_data *umacd, bool filter)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    data->filter_beacons = filter;
}


static bool umac_datapath_rx_frame_filter(struct umac_data *umacd, struct mmpktview *rxbufview)
{
    bool drop_frame = false;
    uint16_t frame_ver_type_subtype;


    const struct dot11_data_hdr *data_hdr =
        (const struct dot11_data_hdr *)mmpkt_get_data_start(rxbufview);
    const struct dot11_hdr *header = &data_hdr->base;
    uint32_t header_len = dot11_data_hdr_get_len(data_hdr);

    if (!umac_datapath_validate_buf_len(rxbufview, sizeof(header->frame_control)))
    {
        drop_frame = true;
        goto exit;
    }

    frame_ver_type_subtype = dot11_frame_control_get_ver_type_subtype(header->frame_control);

    if (frame_ver_type_subtype == DOT11_VER_TYPE_SUBTYPE(0, CTRL, RTS))
    {

        MMLOG_INF("Dropping RTS frame.\n");
        drop_frame = true;
        goto exit;
    }

    if (frame_ver_type_subtype == DOT11_VER_TYPE_SUBTYPE(0, EXT, S1G_BEACON))
    {
        if (umac_datapath_filter_all_beacons(umacd))
        {
            MMLOG_VRB("Dropping beacon.\n");
            drop_frame = true;
            goto exit;
        }

        header_len = sizeof(struct dot11_s1g_beacon_hdr);
    }


    if (!umac_datapath_validate_buf_len(rxbufview, header_len))
    {
        MMLOG_INF("Frame too short, drop.\n");
        drop_frame = true;
        goto exit;
    }

    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    if (data->ops == NULL)
    {
        MMLOG_WRN("Frame received before datapath configured. Dropping.\n");
        drop_frame = true;
        goto exit;
    }

    if (!umac_datapath_rx_frame_allowed_pre_association(data,
                                                        frame_ver_type_subtype,
                                                        header->frame_control))
    {
        const uint8_t *ta = dot11_get_ta(header);
        MMOSAL_DEV_ASSERT(data->ops != NULL);
        struct umac_sta_data *stad = data->ops->lookup_stad_by_peer_addr(umacd, ta);
        if (stad == NULL)
        {
            MMLOG_INF("Dropping packet from unknown sender " MM_MAC_ADDR_FMT " (%04x)\n",
                      MM_MAC_ADDR_VAL(ta),
                      frame_ver_type_subtype);
            drop_frame = true;
            goto exit;
        }

        if (umac_interface_addr_matches_mac_addr(stad, dot11_get_sa_data(data_hdr)))
        {
            MMLOG_INF("Source address matches our MAC address, dropping received frame.\n");
            drop_frame = true;
            goto exit;
        }

        struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);
        if (umac_datapath_is_rx_frame_duplicate(sta_data, rxbufview))
        {
            MMLOG_INF("Dropping duplicate frame. Type %u, Subtype %u.\n",
                      dot11_frame_control_get_type(header->frame_control),
                      dot11_frame_control_get_subtype(header->frame_control));
            drop_frame = true;
            goto exit;
        }
    }

exit:
    return drop_frame;
}

void umac_datapath_init(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    DATAPATH_TRACE_INIT();

    memset(data, 0, sizeof(*data));

    data->tx_flowcontrol_sem = mmosal_semb_create("txfc");
    MMOSAL_ASSERT(data->tx_flowcontrol_sem != NULL);
}

void umac_datapath_stad_init(struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);

    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);


    memset(sta_data->rx_seq_num_spaces, 0xff, sizeof(sta_data->rx_seq_num_spaces));
}

void umac_datapath_stad_flush_txq(struct umac_data *umacd, struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    const uint16_t aid = umac_sta_data_get_aid(stad);
    MMLOG_DBG("Flushing %d frames for STA AID=%d\n", umac_sta_data_get_queued_len(stad), aid);
    while (umac_sta_data_get_queued_len(stad))
    {
        struct mmpkt *mmpkt = umac_sta_data_pop_pkt(stad);
        MMOSAL_DEV_ASSERT(mmpkt);
        mmpkt_release(mmpkt);
        umac_stats_increment_datapath_txq_frames_dropped(umacd);
        MMLOG_VRB("Popped and dropped packet for stad AID=%d\n", aid);
    }
}

void umac_datapath_stad_flush(struct umac_data *umacd, struct umac_sta_data *stad)
{
    MMOSAL_ASSERT(stad != NULL);
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);
    umac_datapath_flush_rx_reorder_list(stad, sta_data);
    datapath_defrag_deinit(umacd, &sta_data->defrag_data);
    umac_datapath_stad_flush_txq(umacd, stad);
}

static void umac_datapath_flush_txq(struct umac_data *umacd);


static void umac_datapath_flush(struct umac_data *umacd)
{
    bool more = true;
    do {

        more = umac_datapath_process(umacd);
    } while (more);

    umac_stats_clear_datapath_rxq_high_water_mark(umacd);
    umac_stats_clear_datapath_rx_mgmt_q_high_water_mark(umacd);
    umac_stats_clear_datapath_rxq_frames_dropped(umacd);
    umac_datapath_flush_txq(umacd);
    umac_stats_clear_datapath_rx_reorder_overflow(umacd);
    umac_stats_clear_datapath_rx_reorder_outdated_drops(umacd);
    umac_stats_clear_datapath_rx_reorder_total(umacd);
}

void umac_datapath_deinit(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    umac_datapath_flush(umacd);

    mmosal_semb_delete(data->tx_flowcontrol_sem);

    memset(data, 0, sizeof(*data));
}

enum mmwlan_status umac_datapath_register_tx_flow_control_cb(struct umac_data *umacd,
                                                             mmwlan_tx_flow_control_cb_t callback,
                                                             void *arg)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    data->tx_flow_control_callback = callback;
    data->tx_flow_control_arg = arg;
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_datapath_register_rx_cb(struct umac_data *umacd,
                                                mmwlan_rx_cb_t callback,
                                                void *arg)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    data->rx_pkt_callback = NULL;
    umac_interface_register_rx_pkt_ext_cb(umacd, MMWLAN_VIF_UNSPECIFIED, NULL, NULL);
    data->rx_callback = callback;
    data->rx_arg = arg;
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_datapath_register_rx_pkt_cb(struct umac_data *umacd,
                                                    mmwlan_rx_pkt_cb_t callback,
                                                    void *arg)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    data->rx_callback = NULL;
    umac_interface_register_rx_pkt_ext_cb(umacd, MMWLAN_VIF_UNSPECIFIED, NULL, NULL);
    data->rx_pkt_callback = callback;
    data->rx_arg = arg;
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_datapath_register_rx_pkt_ext_cb(struct umac_data *umacd,
                                                        enum mmwlan_vif vif,
                                                        mmwlan_rx_pkt_ext_cb_t callback,
                                                        void *arg)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    data->rx_callback = NULL;
    data->rx_pkt_callback = NULL;
    data->rx_arg = NULL;

    return umac_interface_register_rx_pkt_ext_cb(umacd, vif, callback, arg);
}


static void umac_datapath_process_rx_frame(struct umac_data *umacd,
                                           struct umac_datapath_data *data,
                                           struct mmpkt *rxbuf)
{
    struct mmpktview *rxbufview = mmpkt_open(rxbuf);


    struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);
    uint16_t frame_control_le = header->frame_control;
    uint16_t frame_ver_type_subtype = dot11_frame_control_get_ver_type_subtype(frame_control_le);
    uint16_t frame_type = dot11_frame_control_get_type(frame_control_le);
    uint16_t frame_subtype = dot11_frame_control_get_subtype(frame_control_le);
    MMLOG_VRB("RX frame. Type: %d, Subtype: %d.\n", frame_type, frame_subtype);
	
    const uint8_t *ta = NULL;
    struct umac_sta_data *stad = NULL;
    if (frame_ver_type_subtype != DOT11_VER_TYPE_SUBTYPE(0, EXT, S1G_BEACON))
    {
        ta = dot11_get_ta(header);
        MMOSAL_DEV_ASSERT(data->ops != NULL);
        MMOSAL_DEV_ASSERT(mm_mac_addr_is_multicast(ta) == false);
        stad = data->ops->lookup_stad_by_peer_addr(umacd, ta);
    }


    if (stad == NULL && !umac_datapath_rx_frame_allowed_pre_association(data,
                                                                        frame_ver_type_subtype,
                                                                        frame_control_le))
    {
        if (ta != NULL)
        {
            MMLOG_WRN("Unable to find STA record for " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(ta));
        }
        mmpkt_close(&rxbufview);
        mmpkt_release(rxbuf);
        return;
    }


    if (stad && (frame_type == DOT11_FC_TYPE_DATA || frame_type == DOT11_FC_TYPE_MGMT))
    {
        uint16_t pwr_mgt = dot11_frame_control_get_power_mgmt(frame_control_le);
        bool ok = data->ops->set_stad_sleep_state(stad, pwr_mgt);
        MMOSAL_DEV_ASSERT(ok);
    }

    if (frame_type == DOT11_FC_TYPE_DATA)
    {
        umac_datapath_process_rx_data_frame(umacd, stad, rxbuf, rxbufview);
    }
    else
    {
        umac_datapath_process_rx_other_frame(umacd, stad, data, rxbuf, rxbufview);
    }

}


static inline bool umac_datapath_process_rx(struct umac_data *umacd,
                                            struct umac_datapath_data *data)
{
    unsigned ii;
    for (ii = 0; ii < MAX_RX_PROCESS_PER_LOOP; ii++)
    {
        struct mmpkt *mmpkt;
        MMOSAL_TASK_ENTER_CRITICAL();
        mmpkt = data->rx_mgmt_q.len ? mmpkt_list_dequeue(&data->rx_mgmt_q) :
                                      mmpkt_list_dequeue(&data->rxq);
        MMOSAL_TASK_EXIT_CRITICAL();

        if (mmpkt == NULL)
        {
            return false;
        }

        DATAPATH_TRACE("rx deq %x", (uint32_t)mmpkt);

        MMLOG_VRB("RX dequeue %p\n", mmpkt);
        umac_datapath_process_rx_frame(umacd, data, mmpkt);
    }

    return true;
}


static void umac_datapath_rx_queue_frame(struct umac_data *umacd,
                                         struct umac_datapath_data *data,
                                         struct mmpkt *rxbuf,
                                         struct mmpktview *rxbufview)
{
    MMLOG_VRB("RX queue %p\n", rxbuf);
    DATAPATH_TRACE("rx q %x", (uint32_t)rxbuf);

    struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(rxbufview);
    bool is_mgmt = dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_MGMT;
    mmpkt_close(&rxbufview);

    MMOSAL_TASK_ENTER_CRITICAL();
    if (is_mgmt)
    {
        mmpkt_list_append(&data->rx_mgmt_q, rxbuf);
        umac_stats_update_datapath_rx_mgmt_q_high_water_mark(umacd, data->rx_mgmt_q.len);
    }
    else
    {
        mmpkt_list_append(&data->rxq, rxbuf);
        umac_stats_update_datapath_rxq_high_water_mark(umacd, data->rxq.len);
    }
    MMOSAL_TASK_EXIT_CRITICAL();
}

void umac_datapath_rx_frame(struct umac_data *umacd, struct mmpkt *rxbuf)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    struct mmpktview *rxbufview = mmpkt_open(rxbuf);

    if (umac_datapath_rx_frame_filter(umacd, rxbufview))
    {
        mmpkt_close(&rxbufview);
        mmpkt_release(rxbuf);
        return;
    }

    umac_datapath_rx_queue_frame(umacd, data, rxbuf, rxbufview);

    umac_core_evt_wake(umacd);
}

struct mmpkt *umac_datapath_alloc_mmpkt_for_qos_data_tx(uint32_t payload_len, uint8_t pkt_class)
{
    return umac_datapath_alloc_raw_tx_mmpkt(pkt_class, MAX_QOS_DATA_MAC_HEADER_LEN, payload_len);
}

struct mmpkt *mmwlan_alloc_mmpkt_for_tx(uint32_t payload_len, uint8_t tid)
{
    if (tid > MMWLAN_MAX_QOS_TID)
    {
        MMLOG_ERR("Invalid TID %u\n", tid);
        return NULL;
    }
    return umac_datapath_alloc_mmpkt_for_qos_data_tx(payload_len, MMDRV_PKT_CLASS_DATA_TID0 + tid);
}

struct mmpkt *umac_datapath_alloc_raw_tx_mmpkt(uint8_t pkt_class,
                                               uint32_t space_at_start,
                                               uint32_t space_at_end)
{
    return mmdrv_alloc_mmpkt_for_tx(pkt_class, space_at_start, space_at_end);
}

struct mmpkt *umac_datapath_copy_tx_mmpkt(struct mmpkt *pkt, uint8_t pkt_class)
{
    uint32_t length;
    struct mmpkt *copy;
    struct mmpktview *view_src;
    struct mmpktview *view_dst;

    MMOSAL_DEV_ASSERT(pkt != NULL);

    view_src = mmpkt_open(pkt);
    length = mmpkt_get_data_length(view_src);

    copy = umac_datapath_alloc_mmpkt_for_qos_data_tx(length, pkt_class);
    if (copy == NULL)
    {
        mmpkt_close(&view_src);
        return NULL;
    }

    view_dst = mmpkt_open(copy);
    mmpkt_append_data(view_dst, mmpkt_get_data_start(view_src), length);
    *(mmpkt_get_metadata(copy).tx) = *(mmpkt_get_metadata(pkt).tx);

    mmpkt_close(&view_src);
    mmpkt_close(&view_dst);

    return copy;
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
}

static void umac_datapath_aggr_check(struct umac_data *umacd,
                                     struct umac_sta_data *stad,
                                     uint8_t tid,
                                     uint16_t ssc)
{
    if (tid > UMAC_BA_MAX_AGGR_TID)
    {
        return;
    }

    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    MMOSAL_DEV_ASSERT(data->ops != NULL);
    if (data->ops->get_sta_state(stad) != MMWLAN_STA_CONNECTED)
    {
        return;
    }

    if (!umac_config_is_ampdu_enabled(umacd) ||
        !MORSE_CAP_SUPPORTED(umac_interface_get_capabilities(umacd), AMPDU))
    {
        return;
    }

    umac_ba_session_init(stad, tid, ssc, DOT11_BLOCK_ACK_TIMEOUT_DISABLED);
}

enum mmwlan_status umac_datapath_process_tx_frame(struct umac_data *umacd,
                                                  struct umac_sta_data *stad,
                                                  struct mmpktview *txbufview)
{
    struct mmpkt *txbuf = mmpkt_from_view(txbufview);
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(txbuf);
    uint16_t tid = tx_metadata->tid;
    uint8_t enc = tx_metadata->enc;
    const struct umac_8023_hdr *header_8023 =
        (const struct umac_8023_hdr *)mmpkt_get_data_start(txbufview);
    const uint16_t ethertype = be16toh(header_8023->ethertype_be);
    struct dot11_data_hdr data_hdr = { 0 };
    struct dot11_hdr *header = &data_hdr.base;
    int key_id = -1;
    int key_len = 0;
    int ccmp_len = 0;
    uint32_t rts_threshold = 0;
    bool rts_required = false;
    struct dot11_qos_ctrl qos_ctrl = {};
    enum mmwlan_status status = MMWLAN_ERROR;

    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);


    bool is_eapol = (ethertype == ETHERTYPE_EAPOL);

    MMLOG_DBG("Dequeued frame for TX (%p, ethertype=0x%04x, tid=%u, %s port)\n",
              txbuf,
              ethertype,
              tid,
              is_eapol ? "uncontrolled" : "controlled");

    MMOSAL_DEV_ASSERT(data->ops != NULL);

    data->ops->construct_80211_data_header(stad, header_8023, &data_hdr);
    const uint32_t data_hdr_len = dot11_data_hdr_get_len(&data_hdr);


    mmpkt_remove_from_start(txbufview, 2 * DOT11_MAC_ADDR_LEN);
    MM_STATIC_ASSERT(2 * DOT11_MAC_ADDR_LEN == offsetof(struct umac_8023_hdr, ethertype_be), "");

    if (ethertype >= ETHERTYPE_THRESHOLD)
    {

        mmpkt_prepend_data(txbufview, snap_802_1h, sizeof(snap_802_1h));
    }

    const uint8_t *ra = dot11_get_ra(&(data_hdr.base));
    if (mm_mac_addr_is_zero(ra))
    {
        MMLOG_WRN("Dropping tx data frame with zero RA.\n");
        status = MMWLAN_ERROR;
        goto error;
    }
    bool is_multicast = mm_mac_addr_is_multicast(ra);


    MMOSAL_DEV_ASSERT(is_eapol || enc == ENCRYPTION_ENABLED);

    if (umac_sta_data_get_security_type(stad) != MMWLAN_OPEN && enc != ENCRYPTION_DISABLED)
    {
        enum umac_key_type key_type = is_multicast ? UMAC_KEY_TYPE_GROUP : UMAC_KEY_TYPE_PAIRWISE;

        key_id = umac_keys_get_active_key_id(stad, key_type);
        if (key_id >= 0)
        {
            key_len = umac_keys_get_key_len(stad, key_id);
            header->frame_control |= htole16(DOT11_MASK_FC_PROTECTED);
        }
        else if (enc == ENCRYPTION_ENABLED)
        {
            MMLOG_WRN("Could not find key for type %u.\n", key_type);
            status = MMWLAN_ERROR;
            goto error;
        }
        MMLOG_DBG("Using %s key index %d\n",
                  (key_type == UMAC_KEY_TYPE_GROUP)    ? "GROUP" :
                  (key_type == UMAC_KEY_TYPE_PAIRWISE) ? "PAIR" :
                                                         "??",
                  key_id);
    }

    size_t seq_num_idx = is_multicast ? MMDRV_SEQ_NUM_BASELINE : tid;
    DOT11_SEQUENCE_CONTROL_SET_SEQUENCE_NUMBER(header->sequence_control,
                                               sta_data->tx_seq_num_spaces[seq_num_idx]++);


    if (!is_multicast && !is_eapol)
    {
        umac_datapath_aggr_check(umacd, stad, tid, header->sequence_control);
    }

    rts_threshold = umac_config_get_rts_threshold(umacd);


    qos_ctrl.field = (uint16_t)(tid & DOT11_MASK_QC_TID);

    if (key_len == UMAC_KEY_AES_256_LEN)
    {
        ccmp_len = DOT11_CCMP_HEADER_LEN + DOT11_CCMP_256_MIC_LEN;
    }
    else if (key_len == UMAC_KEY_AES_128_LEN)
    {
        ccmp_len = DOT11_CCMP_HEADER_LEN + DOT11_CCMP_128_MIC_LEN;
    }

    rts_required = (rts_threshold && ((mmpkt_get_data_length(txbufview) +
                                       data_hdr_len +
                                       sizeof(qos_ctrl) +
                                       DOT11_FCS_FIELD_LEN +
                                       ccmp_len) > rts_threshold));


    MMLOG_VRB("Add QOS CNTL bytes\n");
    mmpkt_prepend_data(txbufview, (uint8_t *)&qos_ctrl.field, sizeof(qos_ctrl));
    mmpkt_prepend_data(txbufview, (uint8_t *)&data_hdr, data_hdr_len);

    tx_metadata->flags = 0;
    if (key_id >= 0)
    {
        tx_metadata->flags |= MMDRV_TX_FLAG_HW_ENC;
        umac_keys_increment_tx_seq(stad, key_id);
    }

    tx_metadata->key_idx = key_id;

    if (!is_multicast)
    {
        tx_metadata->tid_max_reorder_buf_size = umac_ba_get_reorder_buffer_size(stad, tid);
        if (umac_ba_is_ampdu_permitted(stad, tid))
        {
            tx_metadata->flags |= MMDRV_TX_FLAG_AMPDU_ENABLED;
        }
    }

    umac_connection_populate_tx_metadata(umacd, tx_metadata);

    tx_metadata->aid = umac_sta_data_get_aid(stad);


    if (is_eapol)
    {
        umac_rc_init_rate_table_mgmt(umacd, &tx_metadata->rc_data, false);
    }
    else if (is_multicast)
    {

        tx_metadata->flags |= MMDRV_TX_FLAG_NO_ACK;
        umac_rc_init_rate_table_mgmt(umacd, &tx_metadata->rc_data, false);
    }
    else
    {
        MMOSAL_DEV_ASSERT(stad != NULL);
        umac_rc_init_rate_table_data(stad,
                                     &tx_metadata->rc_data,
                                     rts_required,
                                     mmpkt_get_data_length(txbufview));
    }

    MMLOG_DBG("Transmitting frame %p\n", txbuf);

    umac_stats_update_last_tx_time(umacd);

    mmpkt_close(&txbufview);
    int ret = mmdrv_tx_frame(txbuf, false);
    if (ret)
    {
        MMLOG_WRN("mmdrv_tx_frame failed with retcode %d\n", ret);
    }
    return (ret == 0 ? MMWLAN_SUCCESS : MMWLAN_ERROR);

error:
    mmpkt_close(&txbufview);
    mmpkt_release(txbuf);
    umac_stats_increment_datapath_txq_frames_dropped(umacd);
    return status;
}

static uint32_t umac_datapath_calculate_tx_timeout_ms(struct umac_data *umacd, bool blocking)
{
    if (blocking)
    {
        const struct mmwlan_twt_config_args *twt_config = umac_twt_get_config(umacd);
        if (twt_config->twt_mode == MMWLAN_TWT_REQUESTER)
        {
            return (twt_config->twt_wake_interval_us / 1000);
        }
        else
        {
            return MMWLAN_TX_DEFAULT_TIMEOUT_MS;
        }
    }
    else
    {
        return 0;
    }
}

enum mmwlan_status umac_datapath_tx_frame(struct umac_data *umacd,
                                          struct mmpkt *txbuf,
                                          enum umac_datapath_frame_encryption enc,
                                          const uint8_t *ra)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    enum mmwlan_status status = MMWLAN_ERROR;
    struct mmpktview *txbufview = mmpkt_open(txbuf);
    const struct umac_8023_hdr *header_8023 =
        (const struct umac_8023_hdr *)mmpkt_get_data_start(txbufview);

    if (mmpkt_get_data_length(txbufview) < sizeof(*header_8023))
    {
        MMLOG_WRN("Tx len is too small %lu\n", mmpkt_get_data_length(txbufview));
        status = MMWLAN_ERROR;
        goto exit;
    }

    MMOSAL_DEV_ASSERT(data->ops != NULL);
    struct umac_sta_data *stad = NULL;
    const char *addr_type;
    const uint8_t *addr;
    if (ra == NULL)
    {
        stad = data->ops->lookup_stad_by_tx_dest_addr(umacd, header_8023->dest_addr);
        addr_type = "DA";
        addr = header_8023->dest_addr;
    }
    else
    {
        stad = data->ops->lookup_stad_by_peer_addr(umacd, ra);
        addr_type = "RA";
        addr = ra;
    }

    if (stad == NULL)
    {
        MMLOG_WRN("No STA record for %s " MM_MAC_ADDR_FMT "\n", addr_type, MM_MAC_ADDR_VAL(addr));
        status = MMWLAN_NOT_FOUND;
        goto exit;
    }

    const uint16_t ethertype = be16toh(header_8023->ethertype_be);
    const bool is_eapol = (ethertype == ETHERTYPE_EAPOL);
    if (enc == ENCRYPTION_DISABLED && !is_eapol)
    {
        MMLOG_WRN(
            "Dropping request to TX non-EAPOL frame with encryption disabled (ethertype=0x%04x)\n",
            ethertype);
        status = MMWLAN_INVALID_ARGUMENT;
        goto exit;
    }

    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(txbuf);
    tx_metadata->enc = is_eapol ? enc : ENCRYPTION_ENABLED;

    if (is_eapol && !data->ops->is_stad_tx_paused(stad))
    {

        return umac_datapath_process_tx_frame(umacd, stad, txbufview);
    }

    mmpkt_close(&txbufview);

    data->ops->enqueue_tx_frame(umacd, stad, txbuf);
    MMLOG_DBG("Queued frame for TX (%p, ethertype=0x%04x, enc=0x%x)\n", txbuf, ethertype, enc);
    umac_core_evt_wake(umacd);
    return MMWLAN_SUCCESS;

exit:
    mmpkt_close(&txbufview);
    mmpkt_release(txbuf);
    return status;
}


static inline bool umac_datapath_process_tx(struct umac_data *umacd,
                                            struct umac_datapath_data *data)
{
    if (data->ops == NULL)
    {
        MMLOG_DBG("No datapath ops loaded, skipping TX work.\n");
        return false;
    }
    bool has_more = false;
    for (unsigned ii = 0; ii < MAX_TX_PROCESS_PER_LOOP; ii++)
    {

        if (umac_datapath_tx_is_paused(data, ~MMDRV_PAUSE_SOURCE_MASK_PKTMEM))
        {
            MMLOG_DBG("TX datapath blocked.\n");
            return false;
        }

        struct mmpkt *mmpkt = NULL;
        struct umac_sta_data *stad = NULL;
        has_more = data->ops->dequeue_tx_frame(umacd, &stad, &mmpkt);

        if (mmpkt == NULL)
        {
            return false;
        }
        MMOSAL_ASSERT(stad != NULL);
        DATAPATH_TRACE("tx deq %x", (uint32_t)mmpkt);

        const struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);

        MMLOG_VRB("TX dequeue %p for AID %u, VIF %u\n",
                  mmpkt,
                  umac_sta_data_get_aid(stad),
                  tx_metadata->vif_id);

        umac_datapath_process_tx_frame(umacd, stad, mmpkt_open(mmpkt));
    }
    return has_more;
}

static void umac_datapath_flush_txq(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    bool more = true;
    do {

        more = umac_datapath_process_tx(umacd, data);
    } while (more);

    umac_stats_clear_datapath_txq_high_water_mark(umacd);
    umac_stats_clear_datapath_txq_frames_dropped(umacd);
}

enum mmwlan_status umac_datapath_tx_mgmt_frame(struct umac_sta_data *stad, struct mmpkt *txbuf)
{
    enum mmwlan_status status;
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);
    struct mmpktview *txbufview = mmpkt_open(txbuf);
    struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(txbufview);
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(txbuf);
    uint32_t timeout_ms;
    uint16_t pause_mask;

    uint8_t seq_num_space = MMDRV_SEQ_NUM_BASELINE;

    MMOSAL_DEV_ASSERT(stad != NULL);

    bool is_probe_request =
        (dot11_frame_control_get_type(header->frame_control) == DOT11_FC_TYPE_MGMT) &&
        (dot11_frame_control_get_subtype(header->frame_control) == DOT11_FC_SUBTYPE_PROBE_REQ);
    DOT11_SEQUENCE_CONTROL_SET_SEQUENCE_NUMBER(header->sequence_control,
                                               sta_data->tx_seq_num_spaces[seq_num_space]++);

    int key_id = -1;
    MMOSAL_DEV_ASSERT(data->ops != NULL);
    if ((data->ops->get_sta_state(stad) == MMWLAN_STA_CONNECTED))
    {

        if (umac_sta_data_pmf_is_required(stad) && frame_is_robust_mgmt(txbufview))
        {
            if (mm_mac_addr_is_multicast(dot11_get_da(header)))
            {

                MMLOG_WRN("Unsupported attempt to TX a BC/MC RMF, frame dropped.\n");
                mmpkt_close(&txbufview);
                mmpkt_release(txbuf);
                return MMWLAN_ERROR;
            }
            else
            {
                header->frame_control |= htole16(DOT11_MASK_FC_PROTECTED);
                key_id = umac_keys_get_active_key_id(stad, UMAC_KEY_TYPE_PAIRWISE);
                if (key_id >= 0)
                {
                    umac_keys_increment_tx_seq(stad, key_id);
                }
                else
                {
                    MMLOG_WRN("Dropping frame, no key to encrypt protected management frame\n");
                    mmpkt_close(&txbufview);
                    mmpkt_release(txbuf);
                    return MMWLAN_ERROR;
                }
            }
        }
    }

    tx_metadata->flags = MMDRV_TX_FLAG_IMMEDIATE_REPORT;
    if (key_id >= 0)
    {
        tx_metadata->flags |= MMDRV_TX_FLAG_HW_ENC;
        tx_metadata->key_idx = key_id;
    }

    tx_metadata->tid = MMWLAN_MAX_QOS_TID;
    tx_metadata->aid = umac_sta_data_get_aid(stad);

    umac_rc_init_rate_table_mgmt(umacd, &tx_metadata->rc_data, false);


    pause_mask = ~MMDRV_PAUSE_SOURCE_MASK_PKTMEM;
    if (is_probe_request)
    {
        pause_mask &= ~UMAC_DATAPATH_PAUSE_SOURCE_SCAN;
    }

    timeout_ms = umac_datapath_calculate_tx_timeout_ms(umacd, true);
    status = umac_datapath_wait_for_tx_ready_(data, timeout_ms, pause_mask);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Tx Datapath Blocked (is_probe_request=%d)\n", is_probe_request);
        mmpkt_close(&txbufview);
        mmpkt_release(txbuf);
        umac_stats_increment_datapath_txq_frames_dropped(umacd);
        return status;
    }

    umac_stats_update_last_tx_time(umacd);

    mmpkt_close(&txbufview);
    if (mmdrv_tx_frame(txbuf, true) < 0)
    {
        return MMWLAN_ERROR;
    }

    return MMWLAN_SUCCESS;
}

void umac_datapath_handle_tx_status(struct umac_data *umacd, struct mmpkt *mmpkt)
{
    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    if (tx_metadata->attempts != 0)
    {
        MMOSAL_TASK_ENTER_CRITICAL();
        mmpkt_list_append(&data->tx_status_q, mmpkt);
        MMOSAL_TASK_EXIT_CRITICAL();
        umac_core_evt_wake(umacd);
    }
    else
    {
        mmpkt_release(mmpkt);
    }
    DATAPATH_TRACE("tx_status q %x", (uint32_t)mmpkt);
}


static inline void umac_datapath_process_tx_status_queue(struct umac_data *umacd,
                                                         struct umac_datapath_data *data)
{
    struct mmpkt *mmpkt;
    MMOSAL_TASK_ENTER_CRITICAL();
    mmpkt = mmpkt_list_dequeue_all(&data->tx_status_q);
    MMOSAL_TASK_EXIT_CRITICAL();

    struct mmpkt *next = NULL;
    for (; mmpkt != NULL; mmpkt = next)
    {
        next = mmpkt_get_next(mmpkt);

        DATAPATH_TRACE("tx_status deq %x", (uint32_t)mmpkt);

        struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);


        struct umac_sta_data *stad = data->ops->lookup_stad_by_aid(umacd, tx_metadata->aid);
        if (stad != NULL)
        {
            bool frame_acked = (tx_metadata->status_flags & MMDRV_TX_STATUS_FLAG_NO_ACK) == 0;
            bool valid_ack_status =
                !(tx_metadata->status_flags &
                  (MMDRV_TX_STATUS_FLAG_PS_FILTERED | MMDRV_TX_STATUS_DUTY_CYCLE_CANT_SEND));

            MMLOG_VRB("TX status (0x%02x) indicates %s.\n",
                      tx_metadata->status_flags,
                      valid_ack_status ? (frame_acked ? "acked" : "unacked") : "unsent");

            if (tx_metadata->aid != 0)
            {
                umac_rc_feedback(stad, tx_metadata);
            }

            if (valid_ack_status)
            {
                umac_connection_handle_ack_status(mmpkt, umacd, frame_acked);
            }


            umac_supp_tx_status(umacd, mmpkt, (valid_ack_status ? frame_acked : false));

            if (tx_metadata->status_flags & MMDRV_TX_STATUS_FLAGS_FAIL_MASK)
            {
                MMLOG_WRN("TX status indicates failure with flags 0x%02x\n",
                          tx_metadata->status_flags & MMDRV_TX_STATUS_FLAGS_FAIL_MASK);
            }
        }
        else
        {
            MMLOG_INF("No STA data - link down\n");
        }

        mmpkt_release(mmpkt);
    }
}

static struct mmpkt *umac_datapath_build_3addr_to_ds_qos_null(struct umac_sta_data *stad)
{
    struct consbuf cbuf = CONSBUF_INIT_WITHOUT_BUF;
    consbuf_reserve(&cbuf, sizeof(struct dot11_hdr));
    consbuf_reserve(&cbuf, sizeof(struct dot11_qos_ctrl));

    struct mmpkt *mmpkt =
        umac_datapath_alloc_raw_tx_mmpkt(MMDRV_PKT_CLASS_DATA_TID7, 0, cbuf.offset);
    if (mmpkt == NULL)
    {
        MMLOG_INF("Failed to allocate mgmt frame (len %lu)\n", cbuf.offset);
        return NULL;
    }
    struct mmpktview *view = mmpkt_open(mmpkt);
    consbuf_reinit_from_mmpkt(&cbuf, view);

    struct dot11_hdr *header = (struct dot11_hdr *)consbuf_reserve(&cbuf, sizeof(struct dot11_hdr));
    memset(header, 0, sizeof(*header));
    umac_sta_data_get_bssid(stad, header->addr1);
    umac_interface_get_mac_addr(stad, header->addr2);
    umac_sta_data_get_bssid(stad, header->addr3);
    header->frame_control = htole16(DOT11_MASK_FC_TO_DS |
                                    (DOT11_FC_TYPE_DATA << DOT11_SHIFT_FC_TYPE) |
                                    (DOT11_FC_SUBTYPE_QOS_NULL << DOT11_SHIFT_FC_SUBTYPE));

    struct dot11_qos_ctrl *qos_ctrl =
        (struct dot11_qos_ctrl *)consbuf_reserve(&cbuf, sizeof(struct dot11_qos_ctrl));
    qos_ctrl->field = (DOT11_MASK_QC_TID & MMWLAN_MAX_QOS_TID);

    uint8_t *ret = mmpkt_append(view, cbuf.offset);
    MMOSAL_ASSERT(ret != NULL);

    mmpkt_close(&view);

    return mmpkt;
}

static struct mmpkt *umac_datapath_build_4addr_qos_null(struct umac_sta_data *stad)
{
    struct consbuf cbuf = CONSBUF_INIT_WITHOUT_BUF;
    consbuf_reserve(&cbuf, sizeof(struct dot11_data_hdr));
    consbuf_reserve(&cbuf, sizeof(struct dot11_qos_ctrl));

    struct mmpkt *mmpkt =
        umac_datapath_alloc_raw_tx_mmpkt(MMDRV_PKT_CLASS_DATA_TID7, 0, cbuf.offset);
    if (mmpkt == NULL)
    {
        MMLOG_INF("Failed to allocate mgmt frame (len %lu)\n", cbuf.offset);
        return NULL;
    }
    struct mmpktview *view = mmpkt_open(mmpkt);
    consbuf_reinit_from_mmpkt(&cbuf, view);

    struct dot11_data_hdr *header =
        (struct dot11_data_hdr *)consbuf_reserve(&cbuf, sizeof(struct dot11_data_hdr));
    memset(header, 0, sizeof(*header));
    umac_sta_data_get_peer_addr(stad, header->base.addr1);
    umac_interface_get_mac_addr(stad, header->base.addr2);
    umac_sta_data_get_peer_addr(stad, header->base.addr3);
    umac_interface_get_mac_addr(stad, header->addr4);
    header->base.frame_control = htole16(DOT11_MASK_FC_TO_DS |
                                         DOT11_MASK_FC_FROM_DS |
                                         (DOT11_FC_TYPE_DATA << DOT11_SHIFT_FC_TYPE) |
                                         (DOT11_FC_SUBTYPE_QOS_NULL << DOT11_SHIFT_FC_SUBTYPE));

    struct dot11_qos_ctrl *qos_ctrl =
        (struct dot11_qos_ctrl *)consbuf_reserve(&cbuf, sizeof(struct dot11_qos_ctrl));
    qos_ctrl->field = (DOT11_MASK_QC_TID & MMWLAN_MAX_QOS_TID);

    uint8_t *ret = mmpkt_append(view, cbuf.offset);
    MMOSAL_ASSERT(ret != NULL);

    mmpkt_close(&view);

    return mmpkt;
}

static enum mmwlan_status umac_datapath_tx_qos_null_frame(struct umac_data *umacd,
                                                          struct umac_sta_data *stad,
                                                          struct mmpkt *mmpkt)
{
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);
    struct mmpktview *view = mmpkt_open(mmpkt);
    struct dot11_hdr *header = (struct dot11_hdr *)mmpkt_get_data_start(view);


    DOT11_SEQUENCE_CONTROL_SET_SEQUENCE_NUMBER(
        header->sequence_control,
        sta_data->tx_seq_num_spaces[MMDRV_SEQ_NUM_QOS_NULL]++);


    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(mmpkt);
    tx_metadata->flags = MMDRV_TX_FLAG_IMMEDIATE_REPORT;
    tx_metadata->tid = MMWLAN_MAX_QOS_TID;
    tx_metadata->aid = umac_sta_data_get_aid(stad);

    umac_rc_init_rate_table_mgmt(umacd, &tx_metadata->rc_data, false);

    umac_stats_update_last_tx_time(umacd);

    mmpkt_close(&view);
    if (mmdrv_tx_frame(mmpkt, false))
    {
        return MMWLAN_ERROR;
    }

    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_datapath_build_and_tx_to_ds_qos_null_frame(struct umac_sta_data *stad)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);


    uint32_t timeout_ms = umac_datapath_calculate_tx_timeout_ms(umacd, true);
    enum mmwlan_status status = umac_datapath_wait_for_tx_ready_(data, timeout_ms, UINT16_MAX);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_DBG("TX datapath blocked.\n");
        umac_stats_increment_datapath_txq_frames_dropped(umacd);
        return status;
    }

    struct mmpkt *mmpkt = umac_datapath_build_3addr_to_ds_qos_null(stad);
    if (mmpkt == NULL)
    {
        return MMWLAN_ERROR;
    }
    return umac_datapath_tx_qos_null_frame(umacd, stad, mmpkt);
}

enum mmwlan_status umac_datapath_build_and_tx_4addr_qos_null_frame(struct umac_sta_data *stad)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);


    uint32_t timeout_ms = umac_datapath_calculate_tx_timeout_ms(umacd, true);
    enum mmwlan_status status = umac_datapath_wait_for_tx_ready_(data, timeout_ms, UINT16_MAX);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_DBG("TX datapath blocked.\n");
        umac_stats_increment_datapath_txq_frames_dropped(umacd);
        return status;
    }

    struct mmpkt *mmpkt = umac_datapath_build_4addr_qos_null(stad);
    if (mmpkt == NULL)
    {
        return MMWLAN_ERROR;
    }
    return umac_datapath_tx_qos_null_frame(umacd, stad, mmpkt);
}

enum mmwlan_status umac_datapath_build_and_tx_mgmt_frame(struct umac_sta_data *stad,
                                                         mgmt_frame_builder_t builder,
                                                         void *params)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct mmpkt *txbuf = build_mgmt_frame(umacd, builder, params);
    if (txbuf == NULL)
    {
        return MMWLAN_NO_MEM;
    }

    return umac_datapath_tx_mgmt_frame(stad, txbuf);
}

enum mmwlan_status umac_datapath_build_copy_and_queue_mgmt_frame_tx(struct umac_sta_data *stad,
                                                                    mgmt_frame_builder_t builder,
                                                                    void *params,
                                                                    struct mmpkt **copy)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct mmpkt *txbuf = build_mgmt_frame(umacd, builder, params);
    if (txbuf == NULL)
    {
        return MMWLAN_NO_MEM;
    }

    *copy = umac_datapath_copy_tx_mmpkt(txbuf, MMDRV_PKT_CLASS_MGMT);
    if (*copy == NULL)
    {
        mmpkt_release(txbuf);
        return MMWLAN_NO_MEM;
    }

    return umac_datapath_tx_mgmt_frame(stad, txbuf);
}


#ifndef UNIT_TESTS
bool umac_datapath_process(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    umac_datapath_process_tx_status_queue(umacd, data);
    bool more_rx = umac_datapath_process_rx(umacd, data);
    bool more_tx = umac_datapath_process_tx(umacd, data);
    return more_rx || more_tx;
}

#else
bool umac_datapath_process(struct umac_data *umacd)
{
    MM_UNUSED(umacd);
    return false;
}

#endif

static bool umac_datapath_pause_protected(struct umac_datapath_data *data, uint16_t source_mask)
{
    uint16_t old_pause = data->tx_paused;
    data->tx_paused |= source_mask;
    return old_pause == 0;
}

static bool umac_datapath_unpause_protected(struct umac_datapath_data *data, uint16_t source_mask)
{
    uint16_t old_pause = data->tx_paused;
    data->tx_paused &= ~(source_mask);
    return old_pause != 0 && data->tx_paused == 0;
}

static void umac_datapath_invoke_tx_fc_callback_handler(struct umac_data *umacd,
                                                        const struct umac_evt *evt)
{
    MM_UNUSED(evt);

    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    if (data->tx_flow_control_callback != NULL)
    {
        data->tx_flow_control_callback(data->tx_paused ? MMWLAN_TX_PAUSED : MMWLAN_TX_READY,
                                       data->tx_flow_control_arg);
    }
}

void umac_datapath_update_tx_paused(struct umac_data *umacd,
                                    uint16_t source_mask,
                                    mmdrv_host_update_tx_paused_cb_t cb)
{
    bool pause_state_changed;
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);


    MMOSAL_TASK_ENTER_CRITICAL();
    bool is_paused = cb();
    if (is_paused)
    {
        pause_state_changed = umac_datapath_pause_protected(data, source_mask);
    }
    else
    {
        pause_state_changed = umac_datapath_unpause_protected(data, source_mask);
    }
    MMOSAL_TASK_EXIT_CRITICAL();

    MMLOG_DBG("Datapath paused (|= %08x): %d\n", source_mask, data->tx_paused);
    DATAPATH_TRACE("pause %x %x %u", data->tx_paused, source_mask, pause_state_changed);

    if (pause_state_changed && !is_paused)
    {

        umac_core_evt_wake(umacd);
        mmosal_semb_give(data->tx_flowcontrol_sem);
    }

    if (pause_state_changed && data->tx_flow_control_callback != NULL)
    {
        if (umac_core_evtloop_is_active(umacd))
        {
            data->tx_flow_control_callback(is_paused ? MMWLAN_TX_PAUSED : MMWLAN_TX_READY,
                                           data->tx_flow_control_arg);
        }
        else
        {
            bool ok;
            const struct umac_evt evt = UMAC_EVT_INIT(umac_datapath_invoke_tx_fc_callback_handler);

            ok = umac_core_evt_queue(umacd, &evt);
            if (!ok)
            {
                MMLOG_WRN("Failed to queue INVOKE_TX_FC_CALLBACK event\n");
            }
        }
    }
}

static bool return_true(void)
{
    return true;
}

static bool return_false(void)
{
    return false;
}

void umac_datapath_pause(struct umac_data *umacd, uint16_t source_mask)
{
    umac_datapath_update_tx_paused(umacd, source_mask, return_true);
}

void umac_datapath_unpause(struct umac_data *umacd, uint16_t source_mask)
{
    umac_datapath_update_tx_paused(umacd, source_mask, return_false);
}

void umac_datapath_handle_hw_restarted(struct umac_data *umacd, struct umac_sta_data *stad)
{
    struct umac_datapath_sta_data *sta_data = umac_sta_data_get_datapath(stad);

    const uint8_t *peer_addr = umac_sta_data_peek_peer_addr(stad);

    (void)mmdrv_set_seq_num_spaces(umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA),
                                   sta_data->tx_seq_num_spaces,
                                   peer_addr);
}

enum mmwlan_status umac_datapath_register_rx_frame_cb(struct umac_data *umacd,
                                                      uint32_t filter,
                                                      mmwlan_rx_frame_cb_t callback,
                                                      void *arg)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    data->rx_frame_filter = filter;
    data->rx_frame_cb = callback;
    data->rx_frame_cb_arg = arg;
    return MMWLAN_SUCCESS;
}


static struct umac_sta_data *umac_datapath_lookup_stad_by_peer_addr_sta_mode(
    struct umac_data *umacd,
    const uint8_t *addr)
{
    MMLOG_DBG("Lookup peer " MM_MAC_ADDR_FMT "\n", MM_MAC_ADDR_VAL(addr));

    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
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

static bool nullop_set_stad_sleep_state_sta_mode(struct umac_sta_data *stad, bool asleep)
{
    MM_UNUSED(asleep);
    return stad != NULL;
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


static const struct umac_datapath_ops datapath_ops_sta = {
    .process_rx_mgmt_frame = umac_datapath_process_rx_mgmt_frame_sta,
    .lookup_stad_by_peer_addr = umac_datapath_lookup_stad_by_peer_addr_sta_mode,
    .lookup_stad_by_tx_dest_addr = umac_datapath_lookup_stad_by_tx_dest_addr_sta_mode,
    .lookup_stad_by_aid = umac_datapath_lookup_stad_by_aid_sta,
    .set_stad_sleep_state = nullop_set_stad_sleep_state_sta_mode,
    .is_stad_tx_paused = umac_sta_data_is_paused,
    .enqueue_tx_frame = umac_datapath_tx_queue_frame_sta,
    .dequeue_tx_frame = umac_datapath_tx_dequeue_frame_sta,
    .construct_80211_data_header = umac_datapath_construct_80211_data_header_sta,
    .get_sta_state = umac_datapath_get_state_sta,
    .frames_allowed_pre_association = frames_allowed_pre_association_sta_mode,
};

void umac_datapath_configure_sta_mode(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);
    data->ops = &datapath_ops_sta;
    MMLOG_INF("Datapath configured for STA mode\n");
}

void umac_datapath_configure_scan_mode(struct umac_data *umacd)
{
    struct umac_datapath_data *data = umac_data_get_datapath(umacd);

    if (data->ops == NULL)
    {
        data->ops = &datapath_ops_sta;
    }
}
