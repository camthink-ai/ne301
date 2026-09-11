/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "mmpkt.h"
#include "mmdrv.h"

#include "dot11/dot11_utils.h"
#include "umac/ies/s1g_operation.h"

#if defined(ENABLE_MMWLAN_RELAY) && ENABLE_MMWLAN_RELAY


bool umac_relay_validate_relay_args(struct umac_data *umacd, const struct mmwlan_relay_args *args);


enum mmwlan_status umac_relay_enable(struct umac_data *umacd, const struct mmwlan_relay_args *args);


enum mmwlan_status umac_relay_disable(struct umac_data *umacd);


void umac_relay_process_data_frame(struct umac_sta_data *stad,
                                   enum mmwlan_vif rx_vif,
                                   uint8_t rx_tid,
                                   const struct dot11_data_hdr *data_hdr,
                                   uint16_t llc_ethertype,
                                   struct mmpktview **rxbufview);


void umac_relay_process_s1g_action_frame(struct umac_sta_data *stad, struct mmpktview *rxbufview);


void umac_relay_handle_ap_sta_state(struct umac_sta_data *stad,
                                    struct mmwlan_ap_sta_status *sta_status);


void umac_relay_handle_sta_state(struct umac_data *umacd, enum mmwlan_sta_state state);


void umac_relay_handle_ack_status(struct umac_data *umacd, struct mmpkt *pkt, bool frame_acked);


void umac_relay_update_tx_metadata(struct umac_data *umacd,
                                   struct mmpkt *pkt,
                                   struct mmwlan_tx_metadata *metadata);


void umac_relay_emit_ies(struct umac_data *umacd,
                         struct consbuf *buf,
                         enum dot11_fc_type type,
                         enum dot11_fc_subtype subtype);


uint8_t umac_relay_get_depth(struct umac_data *umacd);


bool umac_relay_filter_scan_depth(struct umac_data *umacd, uint8_t *min, uint8_t *max);


void umac_relay_process_beacon_ies(struct umac_data *umacd, const uint8_t *ies, uint32_t ies_len);


enum mmwlan_relay_state umac_relay_get_state(struct umac_data *umacd);


enum mmwlan_status umac_relay_register_depth_change_cb(struct umac_data *umacd,
                                                       mmwlan_relay_depth_change_cb_t cb,
                                                       void *arg);

#else

static inline bool umac_relay_validate_relay_args(struct umac_data *umacd,
                                                  const struct mmwlan_relay_args *args)
{
    MM_UNUSED(umacd);
    MM_UNUSED(args);
    return MMWLAN_NOT_SUPPORTED;
}

static inline enum mmwlan_status umac_relay_enable(struct umac_data *umacd,
                                                   const struct mmwlan_relay_args *args)
{
    MM_UNUSED(umacd);
    MM_UNUSED(args);
    MMLOG_WRN("Unable to enable Relay: not supported\n");
    return MMWLAN_NOT_SUPPORTED;
}

static inline enum mmwlan_status umac_relay_disable(struct umac_data *umacd)
{
    MM_UNUSED(umacd);
    MMLOG_WRN("Unable to disable Relay: not supported\n");
    return MMWLAN_NOT_SUPPORTED;
}

static inline void umac_relay_process_data_frame(struct umac_sta_data *stad,
                                                 enum mmwlan_vif rx_vif,
                                                 uint8_t rx_tid,
                                                 const struct dot11_data_hdr *data_hdr,
                                                 uint16_t llc_ethertype,
                                                 struct mmpktview **rxbufview)
{
    MM_UNUSED(stad);
    MM_UNUSED(rx_vif);
    MM_UNUSED(rx_tid);
    MM_UNUSED(data_hdr);
    MM_UNUSED(llc_ethertype);
    MM_UNUSED(rxbufview);
}

static inline void umac_relay_process_s1g_action_frame(struct umac_sta_data *stad,
                                                       struct mmpktview *rxbufview)
{
    MM_UNUSED(stad);
    MM_UNUSED(rxbufview);
}

static inline void umac_relay_handle_ap_sta_state(struct umac_sta_data *stad,
                                                  struct mmwlan_ap_sta_status *sta_status)
{
    MM_UNUSED(stad);
    MM_UNUSED(sta_status);
}

static inline void umac_relay_handle_sta_state(struct umac_data *umacd, enum mmwlan_sta_state state)
{
    MM_UNUSED(umacd);
    MM_UNUSED(state);
}

static inline void umac_relay_handle_ack_status(struct umac_data *umacd,
                                                struct mmpkt *pkt,
                                                bool frame_acked)
{
    MM_UNUSED(umacd);
    MM_UNUSED(pkt);
    MM_UNUSED(frame_acked);
}

static inline void umac_relay_update_tx_metadata(struct umac_data *umacd,
                                                 struct mmpkt *pkt,
                                                 struct mmwlan_tx_metadata *metadata)
{
    MM_UNUSED(umacd);
    MM_UNUSED(pkt);
    MM_UNUSED(metadata);
}

static inline void umac_relay_emit_ies(struct umac_data *umacd,
                                       struct consbuf *buf,
                                       enum dot11_fc_type type,
                                       enum dot11_fc_subtype subtype)
{
    MM_UNUSED(umacd);
    MM_UNUSED(buf);
    MM_UNUSED(type);
    MM_UNUSED(subtype);
}

static inline uint8_t umac_relay_get_depth(struct umac_data *umacd)
{
    MM_UNUSED(umacd);
    return MMWLAN_RELAY_DEPTH_UNKNOWN;
}

static inline bool umac_relay_filter_scan_depth(struct umac_data *umacd, uint8_t *min, uint8_t *max)
{
    MM_UNUSED(umacd);
    MM_UNUSED(min);
    MM_UNUSED(max);
    return false;
}

static inline void umac_relay_process_beacon_ies(struct umac_data *umacd,
                                                 const uint8_t *ies,
                                                 uint32_t ies_len)
{
    MM_UNUSED(umacd);
    MM_UNUSED(ies);
    MM_UNUSED(ies_len);
}

static inline enum mmwlan_relay_state umac_relay_get_state(struct umac_data *umacd)
{
    MM_UNUSED(umacd);
    return MMWLAN_RELAY_STATE_DISABLED;
}

static inline enum mmwlan_status umac_relay_register_depth_change_cb(
    struct umac_data *umacd,
    mmwlan_relay_depth_change_cb_t cb,
    void *arg)
{
    MM_UNUSED(umacd);
    MM_UNUSED(cb);
    MM_UNUSED(arg);
    return MMWLAN_UNAVAILABLE;
}

#endif


