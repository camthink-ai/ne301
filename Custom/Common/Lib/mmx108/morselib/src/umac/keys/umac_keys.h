/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "umac/data/umac_data.h"


#define UMAC_CONNECTION_KEY_MAX_LEN (32)


#define UMAC_KEY_AES_128_LEN 16


#define UMAC_KEY_AES_256_LEN 32


enum umac_key_type
{
    UMAC_KEY_TYPE_BLANK = 0,
    UMAC_KEY_TYPE_PAIRWISE,
    UMAC_KEY_TYPE_GROUP,
    UMAC_KEY_TYPE_IGTK,
    UMAC_KEY_TYPE_NUM
};


enum umac_key_rx_counter_space
{
    UMAC_KEY_RX_COUNTER_SPACE_DEFAULT,
    UMAC_KEY_RX_COUNTER_SPACE_IND_ROBUST_MGMT,
    UMAC_KEY_RX_COUNTER_NUM
};


struct umac_key
{

    uint8_t key_id;

    enum umac_key_type key_type;

    uint8_t key_data[UMAC_CONNECTION_KEY_MAX_LEN];

    size_t key_len;

    uint64_t rx_seq[UMAC_KEY_RX_COUNTER_NUM];

    uint64_t tx_seq;
};


void umac_keys_init(struct umac_sta_data *stad);


enum mmwlan_status umac_keys_install_key(struct umac_sta_data *stad,
                                         uint16_t vif_id,
                                         struct umac_key *key);


enum mmwlan_status umac_keys_uninstall_key(struct umac_sta_data *stad,
                                           uint16_t vif_id,
                                           uint8_t key_id);


enum mmwlan_status umac_keys_reinstall_keys(struct umac_sta_data *stad, uint16_t vif_id);


enum umac_key_type umac_keys_get_key_type(struct umac_sta_data *stad, uint8_t key_id);


enum mmwlan_status umac_keys_check_and_update_rx_replay(struct umac_sta_data *stad,
                                                        uint8_t key_id,
                                                        uint64_t packet_number,
                                                        enum umac_key_rx_counter_space space);


size_t umac_keys_get_key_len(struct umac_sta_data *stad, uint8_t key_id);


const uint8_t *umac_keys_get_key_data(struct umac_sta_data *stad, uint8_t key_id);


int umac_keys_get_active_key_id(struct umac_sta_data *stad, enum umac_key_type key_type);


void umac_keys_increment_tx_seq(struct umac_sta_data *stad, uint8_t key_id);


