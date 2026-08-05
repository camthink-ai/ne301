/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac/datapath/umac_datapath.h"
#include "dot11/dot11_frames.h"

#pragma once

struct MM_PACKED umac_8023_hdr
{
    struct
    {
        uint8_t dest_addr[DOT11_MAC_ADDR_LEN];
        uint8_t src_addr[DOT11_MAC_ADDR_LEN];
        uint16_t ethertype_be;
    };
};

MM_STATIC_ASSERT(sizeof(struct umac_8023_hdr) == 14, "Invalid 802.3 definition");
MM_STATIC_ASSERT(
    offsetof(struct umac_8023_hdr, ethertype_be) ==
        sizeof(struct umac_8023_hdr) - MM_MEMBER_SIZE(struct umac_8023_hdr, ethertype_be),
    "Ethernet Type must be the last field");


struct umac_datapath_ops
{

    void (*process_rx_mgmt_frame)(struct umac_data *umacd,
                                  struct umac_sta_data *stad,
                                  struct mmpktview *rxbufview);


    struct umac_sta_data *(*lookup_stad_by_peer_addr)(struct umac_data *umacd,
                                                      const uint8_t *peer_addr);


    struct umac_sta_data *(*lookup_stad_by_tx_dest_addr)(struct umac_data *umacd,
                                                         const uint8_t *dest_addr);


    struct umac_sta_data *(*lookup_stad_by_aid)(struct umac_data *umacd, uint16_t aid);


    bool (*set_stad_sleep_state)(struct umac_sta_data *stad, bool asleep);


    bool (*is_stad_tx_paused)(struct umac_sta_data *stad);


    void (*enqueue_tx_frame)(struct umac_data *umacd,
                             struct umac_sta_data *stad,
                             struct mmpkt *txbuf);


    bool (*dequeue_tx_frame)(struct umac_data *umacd,
                             struct umac_sta_data **stad,
                             struct mmpkt **txbuf);


    void (*construct_80211_data_header)(struct umac_sta_data *stad,
                                        const struct umac_8023_hdr *hdr_8023,
                                        struct dot11_data_hdr *data_hdr);


    enum mmwlan_sta_state (*get_sta_state)(struct umac_sta_data *stad);


    const uint16_t *frames_allowed_pre_association;
};


void umac_datapath_process_rx_action_frame(struct umac_data *umacd,
                                           struct umac_sta_data *stad,
                                           struct mmpktview *rxbufview);


enum mmwlan_status umac_datapath_wait_for_tx_ready_(struct umac_datapath_data *data,
                                                    uint32_t timeout_ms,
                                                    uint16_t mask);
