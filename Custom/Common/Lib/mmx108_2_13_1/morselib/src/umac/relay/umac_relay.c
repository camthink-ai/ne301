/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_relay.h"
#include "umac/config/umac_config.h"
#include "umac_relay_private.h"

#include "common/common.h"
#include "mmlog.h"
#include "mmpkt.h"

#include "dot11/dot11_utils.h"
#include "umac/ap/umac_ap.h"
#include "umac/connection/umac_connection.h"
#include "umac/core/umac_core.h"
#include "umac/datapath/umac_datapath.h"
#include "umac/frames/s1g_action.h"
#include "umac/ies/morse_ie.h"
#include "umac/interface/umac_interface.h"

#if defined(ENABLE_MMWLAN_RELAY) && ENABLE_MMWLAN_RELAY

#define ADDRESS_UPDATE_LATENCY_MS                 (100)
#define ADDRESS_UPDATE_RETRY_INTERVAL_MS          (1000)
#define ADDRESS_UPDATE_RETRY_ON_ERROR_INTERVAL_MS (100)

bool umac_relay_validate_relay_args(struct umac_data *umacd, const struct mmwlan_relay_args *args)
{
    return umacd && args;
}

static void umac_relay_send_reachable_address_update(void *arg1, void *arg2)
{
    struct umac_data *umacd = (struct umac_data *)arg1;
    MM_UNUSED(arg2);
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {
        return;
    }

    struct umac_sta_data *stad = umac_connection_get_stad(umacd);
    if (stad == NULL)
    {
        MMLOG_INF("Not attempting Reachable Address Update: not connected\n");
        umac_core_register_timeout(umacd,
                                   ADDRESS_UPDATE_RETRY_INTERVAL_MS,
                                   umac_relay_send_reachable_address_update,
                                   umacd,
                                   data);
        return;
    }

    const uint8_t *bssid = umac_sta_data_peek_bssid(stad);
    if (bssid == NULL)
    {
        MMLOG_WRN("Not attempting Reachable Address Update: NULL BSSID\n");
        umac_core_register_timeout(umacd,
                                   ADDRESS_UPDATE_RETRY_INTERVAL_MS,
                                   umac_relay_send_reachable_address_update,
                                   umacd,
                                   data);
        return;
    }

    const uint8_t *own_addr = umac_interface_peek_mac_addr(stad);
    if (own_addr == NULL)
    {
        MMLOG_WRN("Not attempting Reachable Address Update: NULL MAC address\n");
        umac_core_register_timeout(umacd,
                                   ADDRESS_UPDATE_RETRY_INTERVAL_MS,
                                   umac_relay_send_reachable_address_update,
                                   umacd,
                                   data);
        return;
    }

    struct mmpkt *pkt = NULL;

    enum mmwlan_status status = umac_relay_get_update_packet(umacd, data, bssid, own_addr, &pkt);

    if (status == MMWLAN_NOT_FOUND)
    {

        return;
    }

    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to generate Reachable Address Update\n");
        umac_core_register_timeout(umacd,
                                   ADDRESS_UPDATE_RETRY_ON_ERROR_INTERVAL_MS,
                                   umac_relay_send_reachable_address_update,
                                   umacd,
                                   data);
        return;
    }

    status = umac_datapath_tx_mgmt_frame(stad, pkt);
    if (status != MMWLAN_SUCCESS)
    {
        MMLOG_WRN("Failed to queue Reachable Address Update\n");
    }

    umac_core_register_timeout(umacd,
                               ADDRESS_UPDATE_RETRY_INTERVAL_MS,
                               umac_relay_send_reachable_address_update,
                               umacd,
                               data);
}

static void schedule_update(struct umac_data *umacd, struct umac_relay_data *data)
{
    if (!umac_core_is_timeout_registered(umacd,
                                         umac_relay_send_reachable_address_update,
                                         umacd,
                                         data))
    {
        umac_core_register_timeout(umacd,
                                   data->reachable_update_latency_ms,
                                   umac_relay_send_reachable_address_update,
                                   umacd,
                                   data);
    }
}

enum mmwlan_status umac_relay_enable(struct umac_data *umacd, const struct mmwlan_relay_args *args)
{
    if (umac_data_get_relay(umacd))
    {
        MMLOG_WRN("Relay already active\n");
        return MMWLAN_UNAVAILABLE;
    }

    if (umac_connection_get_state(umacd) != MMWLAN_STA_DISABLED || umac_data_get_ap(umacd) != NULL)
    {
        MMLOG_WRN("Must start relay before STA and AP\n");
        return MMWLAN_UNAVAILABLE;
    }

    unsigned forwarding_table_size = args->forwarding_table_size;
    if (forwarding_table_size == 0)
    {
        forwarding_table_size = MMWLAN_RELAY_DEFAULT_FORWARDING_TABLE_SIZE;
    }

    struct umac_relay_data *data = umac_data_alloc_relay(
        umacd,
        (sizeof(*data) + sizeof(data->table_pool[0]) * forwarding_table_size));
    if (data == NULL)
    {
        return MMWLAN_NO_MEM;
    }

    data->args = *args;
    data->args.forwarding_table_size = forwarding_table_size;
    data->reachable_update_latency_ms = ADDRESS_UPDATE_LATENCY_MS;

    data->depth = args->is_root_node ? 0 : MMWLAN_RELAY_DEPTH_UNKNOWN;
    data->recover_depth = data->depth;
    umac_ap_signal_beacon_critical_update(umacd);

    umac_relay_table_init(data);

    if (!args->is_root_node && umac_connection_get_state(umacd) == MMWLAN_STA_CONNECTED)
    {
        struct umac_sta_data *stad = umac_connection_get_stad(umacd);
        if (stad != NULL)
        {
            MMLOG_INF("Sending 4addr QoS NULL for relay\n");
            enum mmwlan_status status = umac_datapath_build_and_tx_4addr_qos_null_frame(stad);
            if (status != MMWLAN_SUCCESS)
            {
                MMLOG_WRN("Failed to TX 4addr QoS NULL\n");
            }
        }
    }

    MMLOG_INF("S1G Relay enabled (%s node)\n", args->is_root_node ? "root" : "non-root");
    return MMWLAN_SUCCESS;
}

enum mmwlan_status umac_relay_disable(struct umac_data *umacd)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {
        MMLOG_WRN("Relay not active\n");
        return MMWLAN_SUCCESS;
    }
    umac_data_dealloc_relay(umacd);
    return MMWLAN_SUCCESS;
}

void umac_relay_emit_ies(struct umac_data *umacd,
                         struct consbuf *buf,
                         enum dot11_fc_type type,
                         enum dot11_fc_subtype subtype)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {
        return;
    }

    bool is_probe_rsp = (type == DOT11_FC_TYPE_MGMT && subtype == DOT11_FC_SUBTYPE_PROBE_RSP);
    bool is_s1g_beacon = (type == DOT11_FC_TYPE_EXT && subtype == DOT11_FC_SUBTYPE_S1G_BEACON);

    if (is_probe_rsp || is_s1g_beacon)
    {
        ie_morse_s1g_relay_build(umacd, buf);
    }
}

uint8_t umac_relay_get_depth(struct umac_data *umacd)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    return (data != NULL) ? data->depth : MMWLAN_RELAY_DEPTH_UNKNOWN;
}

bool umac_relay_filter_scan_depth(struct umac_data *umacd, uint8_t *min, uint8_t *max)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    MMOSAL_DEV_ASSERT(min && max);
    if (data == NULL)
    {
        return false;
    }

    *min = 0;
    *max = MM_MIN(data->depth, data->recover_depth) - 1;

    uint8_t override_depth = umac_config_get_relay_depth_override(umacd);
    if (override_depth)
    {
        *min = override_depth - 1;
        *max = override_depth - 1;
    }
    return true;
}

void umac_relay_process_beacon_ies(struct umac_data *umacd, const uint8_t *ies, uint32_t ies_len)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {
        return;
    }

    uint8_t parent_depth = 0;

    {
        const struct dot11_ie_morse_s1g_relay *ie = ie_morse_s1g_relay_find(ies, ies_len);
        if (ie != NULL)
        {
            parent_depth = ie->depth;
        }
    }


    uint8_t new_depth = (parent_depth == MMWLAN_RELAY_DEPTH_UNKNOWN) ? MMWLAN_RELAY_DEPTH_UNKNOWN :
                                                                       (parent_depth + 1);
    if (new_depth != data->depth)
    {
        if (new_depth == MMWLAN_RELAY_DEPTH_UNKNOWN)
        {
            MMLOG_WRN("Parent relay indicates path to root broken\n");
            data->recover_depth = data->depth;
            struct mmwlan_vif_state vif_state = {
                .vif = MMWLAN_VIF_STA,
                .link_state = MMWLAN_LINK_DOWN,
            };
            umac_interface_invoke_vif_state_cb(umacd, &vif_state);

        }
        else
        {
            if (data->depth == MMWLAN_RELAY_DEPTH_UNKNOWN)
            {
                struct mmwlan_vif_state vif_state = {
                    .vif = MMWLAN_VIF_STA,
                    .link_state = MMWLAN_LINK_UP,
                };
                umac_interface_invoke_vif_state_cb(umacd, &vif_state);
            }
            uint8_t override_depth = umac_config_get_relay_depth_override(umacd);
            MMOSAL_DEV_ASSERT(override_depth == 0 || new_depth == override_depth);
            MMOSAL_DEV_ASSERT(new_depth <= data->recover_depth);
            data->recover_depth = MMWLAN_RELAY_DEPTH_UNKNOWN;

        }

        MMLOG_INF("Relay depth updated to %u (parent depth %u)\n", new_depth, parent_depth);
        data->depth = new_depth;


        umac_ap_signal_beacon_critical_update(umacd);

        if (data->depth_change_cb != NULL)
        {
            data->depth_change_cb(data->depth, data->depth_change_cb_arg);
        }
    }
}

enum mmwlan_relay_state umac_relay_get_state(struct umac_data *umacd)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {
        return MMWLAN_RELAY_STATE_DISABLED;
    }
    if (data->args.is_root_node)
    {
        return MMWLAN_RELAY_STATE_ROOT;
    }

    if (umac_data_get_ap(umacd) != NULL)
    {
        return MMWLAN_RELAY_STATE_RELAY;
    }
    return MMWLAN_RELAY_STATE_STA;
}

void umac_relay_process_data_frame(struct umac_sta_data *stad,
                                   enum mmwlan_vif rx_vif,
                                   uint8_t rx_tid,
                                   const struct dot11_data_hdr *data_hdr,
                                   uint16_t llc_ethertype,
                                   struct mmpktview **_rxbufview)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {

        MMLOG_VRB("Relay not enabled\n");
        return;
    }

    const uint8_t *da = dot11_get_da(&data_hdr->base);
    struct umac_sta_data *sta_mode_stad = umac_connection_get_stad(umacd);
    if (sta_mode_stad != NULL && umac_interface_addr_matches_mac_addr(sta_mode_stad, da))
    {

        MMLOG_DBG("Packet destined for self -- do not relay\n");
        return;
    }

    bool is_multicast = mm_mac_addr_is_multicast(da);
    MMLOG_VRB("SA=" MM_MAC_ADDR_FMT ", DA=" MM_MAC_ADDR_FMT ", TA=" MM_MAC_ADDR_FMT
              ", RA=" MM_MAC_ADDR_FMT ", MC? %u\n",
              MM_MAC_ADDR_VAL(dot11_get_sa_data(data_hdr)),
              MM_MAC_ADDR_VAL(da),
              MM_MAC_ADDR_VAL(dot11_get_ta(&data_hdr->base)),
              MM_MAC_ADDR_VAL(dot11_get_ra(&data_hdr->base)),
              is_multicast);

    const uint8_t *ra = NULL;
    const uint8_t *sa = NULL;
    uint16_t vif_id = MMDRV_VIF_ID_INVALID;

    struct mmpktview *rxbufview = *_rxbufview;

    if (is_multicast)
    {

        if (rx_vif == MMWLAN_VIF_AP && !data->args.is_root_node)
        {


            if (sta_mode_stad != NULL)
            {
                ra = umac_sta_data_peek_bssid(sta_mode_stad);
                sa = dot11_get_sa_data(data_hdr);
                vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
            }
            else
            {
                MMLOG_WRN("Do not know how to handle this %s frame. Dropping\n", "group addressed");
            }


            *_rxbufview = NULL;
        }
        else
        {


            sa = dot11_get_sa_data(data_hdr);
            vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);


            if (sta_mode_stad && umac_interface_addr_matches_mac_addr(sta_mode_stad, sa))
            {
                *_rxbufview = NULL;
            }
        }
    }
    else
    {



        if (rx_vif == MMWLAN_VIF_AP && !data->args.is_root_node)
        {


            if (sta_mode_stad != NULL)
            {
                ra = umac_sta_data_peek_bssid(sta_mode_stad);
                sa = dot11_get_sa_data(data_hdr);
                vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_STA);
            }
            else
            {
                MMLOG_WRN("Do not know how to handle this %s frame. Dropping\n",
                          "individually addressed");
            }
        }
        else
        {


            sa = dot11_get_sa_data(data_hdr);
            vif_id = umac_interface_get_vif_id(umacd, UMAC_INTERFACE_AP);

            if (umac_ap_get_sta_status(umacd, da, NULL) == MMWLAN_SUCCESS)
            {

            }
            else
            {

                struct umac_relay_table_entry *entry = umac_relay_table_lookup_by_da(data, da);
                if (entry == NULL)
                {
                    MMLOG_INF("Do not know how to handle this %s frame. Passing up\n",
                              "individually addressed");
                    return;
                }

                ra = entry->next_hop;
            }
        }


        *_rxbufview = NULL;
    }

    if (vif_id == MMDRV_VIF_ID_INVALID)
    {
        MMLOG_DBG("Unknown dest VIF. Dropping\n");
        goto cleanup;
    }

    if (ra != NULL)
    {
        MMLOG_VRB("Forwarding frame to DA " MM_MAC_ADDR_FMT " via " MM_MAC_ADDR_FMT
                  " on VIF ID %u (rx tid=%u)\n",
                  MM_MAC_ADDR_VAL(da),
                  MM_MAC_ADDR_VAL(ra),
                  vif_id,
                  rx_tid);
    }
    else
    {
        MMLOG_VRB("Forwarding frame to DA " MM_MAC_ADDR_FMT " directly on VIF ID %u (rx tid=%u)\n",
                  MM_MAC_ADDR_VAL(da),
                  vif_id,
                  rx_tid);
    }


    if (rx_tid > MMWLAN_MAX_QOS_TID)
    {
        rx_tid = MMWLAN_TX_DEFAULT_QOS_TID;
    }


    struct umac_8023_hdr *header_8023;
    struct mmpkt *txbuf =
        mmwlan_alloc_mmpkt_for_tx(sizeof(*header_8023) + mmpkt_get_data_length(rxbufview), rx_tid);
    if (txbuf == NULL)
    {
        MMLOG_WRN("Unable to allocate mmmpkt for relaying\n");
        goto cleanup;
    }

    struct mmpktview *txbufview = mmpkt_open(txbuf);

    header_8023 = (struct umac_8023_hdr *)mmpkt_append(txbufview, sizeof(*header_8023));
    umac_datapath_generate_8023_header(da, sa, llc_ethertype, header_8023);

    mmpkt_append_data(txbufview, mmpkt_get_data_start(rxbufview), mmpkt_get_data_length(rxbufview));

    mmpkt_close(&txbufview);

    struct mmdrv_tx_metadata *tx_metadata = mmdrv_get_tx_metadata(txbuf);
    tx_metadata->tid = rx_tid;
    tx_metadata->vif_id = vif_id;

    umac_datapath_tx_frame(umacd, txbuf, ENCRYPTION_ENABLED, ra);

cleanup:
    if (*_rxbufview == NULL)
    {
        struct mmpkt *rxpkt = mmpkt_from_view(rxbufview);
        mmpkt_close(&rxbufview);
        MMLOG_VRB("Releasing packet\n");
        mmpkt_release(rxpkt);
    }
}

static void umac_relay_process_reachable_address_update(
    struct umac_sta_data *stad,
    enum reachable_address_update_type update_type,
    bool relay_capable,
    const uint8_t *initiator_mac,
    const uint8_t *mac)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_relay_data *data = umac_data_get_relay(umacd);

    const uint8_t *sender_mac_addr = umac_sta_data_peek_peer_addr(stad);
    MMOSAL_DEV_ASSERT(sender_mac_addr != NULL);

    MMLOG_INF("Relay update: %s DA=" MM_MAC_ADDR_FMT " - initiator=" MM_MAC_ADDR_FMT
              " - next hop=" MM_MAC_ADDR_FMT "\n",
              update_type == REACHABLE_ADDRESS_ADD ? "add" : "remove",
              MM_MAC_ADDR_VAL(mac),
              MM_MAC_ADDR_VAL(initiator_mac),
              MM_MAC_ADDR_VAL(sender_mac_addr));

    if (update_type == REACHABLE_ADDRESS_ADD)
    {
        bool ok = umac_relay_table_add(data, mac, initiator_mac, relay_capable);
        if (!ok)
        {
            MMLOG_ERR("Out of space to append relay update\n");


            MMOSAL_DEV_ASSERT(false);
        }
    }
    else
    {
        bool removed = umac_relay_table_remove(data, mac, initiator_mac);
        if (!removed)
        {
            MMLOG_DBG("No matching entry to remove\n");

            return;
        }
    }

    if (!data->args.is_root_node)
    {
        schedule_update(umacd, data);
    }
}

static void umac_relay_process_s1g_reachable_address_ie(struct umac_sta_data *stad,
                                                        const struct dot11_ie_reachable_address *ie)
{
    const uint8_t *initiator_mac = NULL;
    uint8_t address_count = 0;
    bool ok = ie_reachable_address_parse(ie, &initiator_mac, &address_count);
    if (!ok)
    {
        MMLOG_WRN("Failed to parse Reachable Address IE\n");
        return;
    }

    for (size_t ii = 0; ii < address_count; ii++)
    {
        enum reachable_address_update_type update_type;
        bool relay_capable;
        const uint8_t *mac;
        ok = ie_reachable_address_get_update(ie, ii, &update_type, &relay_capable, &mac);
        if (!ok)
        {
            MMLOG_WRN("Failed to parse Reachable Address IE subfield\n");
            return;
        }
#ifndef MOCK_umac_relay_process_reachable_address_update
        umac_relay_process_reachable_address_update(stad,
                                                    update_type,
                                                    relay_capable,
                                                    initiator_mac,
                                                    mac);
#else
        MOCK_umac_relay_process_reachable_address_update(stad,
                                                         update_type,
                                                         relay_capable,
                                                         initiator_mac,
                                                         mac);
#endif
    }
}

static void umac_relay_process_s1g_reachable_address_update_frame(
    struct umac_sta_data *stad,
    const struct frame_data_s1g_action *s1g_action)
{
    const uint8_t *ies_iter = s1g_action->payload;
    const uint8_t *ies_end = s1g_action->payload + s1g_action->payload_len;

    while (ies_iter < ies_end)
    {
        const struct dot11_ie_reachable_address *ie =
            ie_reachable_address_find(ies_iter, ies_end - ies_iter, NULL);
        if (ie == NULL)
        {
            return;
        }


        ies_iter = (const uint8_t *)ie + sizeof(ie->header) + ie->header.length;
        umac_relay_process_s1g_reachable_address_ie(stad, ie);
    }
}

void umac_relay_process_s1g_action_frame(struct umac_sta_data *stad, struct mmpktview *rxbufview)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {

        return;
    }

    struct frame_data_s1g_action s1g_action = {};
    bool ok = frame_s1g_action_parse(rxbufview, &s1g_action);
    if (!ok)
    {
        return;
    }

    if (s1g_action.s1g_action == DOT11_S1G_ACTION_REACHABLE_ADDRESS_UPDATE)
    {
        umac_relay_process_s1g_reachable_address_update_frame(stad, &s1g_action);
        return;
    }

    MMLOG_INF("Ignoring S1G Action frame with type %u\n", s1g_action.s1g_action);
}

void umac_relay_handle_ap_sta_state(struct umac_sta_data *stad,
                                    struct mmwlan_ap_sta_status *sta_status)
{
    struct umac_data *umacd = umac_sta_data_get_umacd(stad);
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {

        MMLOG_VRB("Relay not enabled. Ignoring AP STA state update\n");
        return;
    }

    enum reachable_address_update_type update_type;
    switch (sta_status->state)
    {
        case MMWLAN_AP_STA_AUTHORIZED:
            update_type = REACHABLE_ADDRESS_ADD;
            break;
        case MMWLAN_AP_STA_UNKNOWN:
            update_type = REACHABLE_ADDRESS_REMOVE;
            break;
        default:
            MMLOG_DBG("Ignoring AP STA state update for " MM_MAC_ADDR_FMT " state %u\n",
                      MM_MAC_ADDR_VAL(sta_status->mac_addr),
                      sta_status->state);
            return;
    }

    umac_relay_process_reachable_address_update(stad,
                                                update_type,
                                                false,
                                                umac_interface_peek_mac_addr(stad),
                                                sta_status->mac_addr);
}

void umac_relay_handle_ack_status(struct umac_data *umacd, struct mmpkt *pkt, bool frame_acked)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {

        return;
    }

    struct frame_data_s1g_action s1g_action;

    struct mmpktview *view = mmpkt_open(pkt);
    bool valid_s1g_action_frame = frame_s1g_action_parse(view, &s1g_action);

    if (valid_s1g_action_frame &&
        s1g_action.s1g_action == DOT11_S1G_ACTION_REACHABLE_ADDRESS_UPDATE)
    {

        if (frame_acked)
        {
            MMLOG_INF("Reachable Address Update ACKed\n");
            umac_relay_handle_update_acked(data, &s1g_action);
        }
        else
        {

            MMLOG_INF("Reachable Address Update not ACKed\n");
            schedule_update(umacd, data);
        }
    }

    mmpkt_close(&view);
}

void umac_relay_update_tx_metadata(struct umac_data *umacd,
                                   struct mmpkt *pkt,
                                   struct mmwlan_tx_metadata *metadata)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {

        MMLOG_VRB("Relay not enabled. Not updating TX metadata\n");
        return;
    }

    if (metadata->ra != NULL)
    {

        MMLOG_VRB("Not updating packet with RA\n");
        return;
    }

    struct mmpktview *txbufview = mmpkt_open(pkt);
    const struct umac_8023_hdr *header_8023 =
        (const struct umac_8023_hdr *)mmpkt_get_data_start(txbufview);

    MMLOG_VRB("Incoming packet SA=" MM_MAC_ADDR_FMT ", DA=" MM_MAC_ADDR_FMT ", vif=%u\n",
              MM_MAC_ADDR_VAL(header_8023->src_addr),
              MM_MAC_ADDR_VAL(header_8023->dest_addr),
              metadata->vif);

    if (!mm_mac_addr_is_multicast(header_8023->dest_addr) &&
        umac_ap_lookup_sta_by_addr(umacd, header_8023->dest_addr) != NULL)
    {
        if (metadata->vif != MMWLAN_VIF_AP)
        {
            MMLOG_DBG("Rewrite VIF for TX packet to connected STA\n");
        }
        metadata->vif = MMWLAN_VIF_AP;
        goto done;
    }

    struct umac_relay_table_entry *entry =
        umac_relay_table_lookup_by_da(data, header_8023->dest_addr);
    if (entry != NULL)
    {
        if (metadata->vif != MMWLAN_VIF_AP)
        {
            MMLOG_DBG("Rewrite VIF and RA for TX packet to connected STA\n");
        }

        metadata->ra = entry->next_hop;
        metadata->vif = MMWLAN_VIF_AP;
        goto done;
    }


    if (metadata->vif != MMWLAN_VIF_STA)
    {
        MMLOG_VRB("Rewriting VIF to TX packet via STA VIF\n");
    }
    metadata->vif = MMWLAN_VIF_STA;
done:
    mmpkt_close(&txbufview);
}

void umac_relay_handle_sta_state(struct umac_data *umacd, enum mmwlan_sta_state state)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {

        MMLOG_VRB("Relay not enabled. Ignoring STA state update\n");
        return;
    }

    if (state != MMWLAN_STA_CONNECTED)
    {

        umac_relay_table_mark_entries_as_newly_added(data);


        if (data->depth != MMWLAN_RELAY_DEPTH_UNKNOWN && umac_data_get_ap(umacd) != NULL)
        {
            data->recover_depth = data->depth;
            data->depth = MMWLAN_RELAY_DEPTH_UNKNOWN;

            if (data->depth_change_cb != NULL)
            {
                data->depth_change_cb(data->depth, data->depth_change_cb_arg);
            }
        }
    }
    else
    {

        schedule_update(umacd, data);
        if (!data->args.is_root_node)
        {
            struct umac_sta_data *stad = umac_connection_get_stad(umacd);
            if (stad == NULL)
            {
                MMLOG_WRN("STA State connected but connection has no STAD\n");
                return;
            }
            MMLOG_INF("Sending 4addr QoS NULL for relay\n");
            enum mmwlan_status status = umac_datapath_build_and_tx_4addr_qos_null_frame(stad);
            if (status != MMWLAN_SUCCESS)
            {
                MMLOG_WRN("Failed to TX 4addr QoS NULL\n");
            }
        }
    }
}

enum mmwlan_status umac_relay_register_depth_change_cb(struct umac_data *umacd,
                                                       mmwlan_relay_depth_change_cb_t callback,
                                                       void *arg)
{
    struct umac_relay_data *data = umac_data_get_relay(umacd);
    if (data == NULL)
    {
        return MMWLAN_UNAVAILABLE;
    }
    data->depth_change_cb = callback;
    data->depth_change_cb_arg = arg;
    return MMWLAN_SUCCESS;
}

#endif
