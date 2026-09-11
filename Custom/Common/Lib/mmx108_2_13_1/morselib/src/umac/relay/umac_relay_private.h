/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "umac_relay.h"
#include "common/consbuf.h"
#include "umac/ies/reachable_address.h"
#include "umac/frames/s1g_action.h"

struct umac_data;


#define UMAC_RELAY_TABLE_INVALID_IDX (0)

#define UMAC_RELAY_NUM_BUCKETS                (256)

#define UMAC_RELAY_PENDING_UPDATE_BUFFER_SIZE (1400)

struct umac_relay_table_entry
{
    struct umac_relay_table_entry *next;
    uint8_t da[MMWLAN_MAC_ADDR_LEN];
    uint8_t next_hop[MMWLAN_MAC_ADDR_LEN];
    bool relay_capable;
};

struct umac_relay_data
{

    struct mmwlan_relay_args args;


    uint8_t depth;


    uint8_t recover_depth;


    uint32_t reachable_update_latency_ms;


    mmwlan_relay_depth_change_cb_t depth_change_cb;
    void *depth_change_cb_arg;


    struct umac_relay_table_entry *table_heads[UMAC_RELAY_NUM_BUCKETS];


    struct umac_relay_table_entry *add_pending_head;


    struct umac_relay_table_entry *remove_pending_head;


    struct umac_relay_table_entry *table_free_list;


    struct umac_relay_table_entry table_pool[];
};


void umac_relay_table_init(struct umac_relay_data *data);


bool umac_relay_table_add(struct umac_relay_data *data,
                          const uint8_t *da,
                          const uint8_t *next_hop,
                          bool relay_capable);


bool umac_relay_table_add_done(struct umac_relay_data *data, const uint8_t *da);


bool umac_relay_table_remove(struct umac_relay_data *data,
                             const uint8_t *da,
                             const uint8_t *next_hop);


bool umac_relay_table_remove_done(struct umac_relay_data *data, const uint8_t *da);


struct umac_relay_table_entry *umac_relay_table_lookup_by_da(struct umac_relay_data *data,
                                                             const uint8_t *da);


void umac_relay_table_mark_entries_as_newly_added(struct umac_relay_data *data);


enum mmwlan_status umac_relay_get_update_packet(struct umac_data *umacd,
                                                struct umac_relay_data *data,
                                                const uint8_t *bssid,
                                                const uint8_t *own_addr,
                                                struct mmpkt **pkt);


void umac_relay_handle_update_acked(struct umac_relay_data *data,
                                    const struct frame_data_s1g_action *s1g_action);
