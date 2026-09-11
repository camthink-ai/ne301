/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/common.h"
#include "common/mac_address.h"
#include "umac/data/umac_data.h"
#include "umac/keys/umac_keys.h"


#define UMAC_KEYS_NUM_KEY_IDS 6


#define UMAC_KEYS_KEYCHAIN_LEN UMAC_KEYS_NUM_KEY_IDS


struct connection_keys_data
{

    struct umac_key *keys[UMAC_KEYS_NUM_KEY_IDS];

    struct umac_key pool[UMAC_KEYS_KEYCHAIN_LEN];

    int active_key_lookup[UMAC_KEY_TYPE_NUM];
};


void connection_keys_init(struct connection_keys_data *data);


bool connection_keys_install_key(struct connection_keys_data *data, struct umac_key *key);


bool connection_keys_uninstall_key(struct connection_keys_data *data, uint8_t key_id);


enum umac_key_type connection_keys_get_key_type(struct connection_keys_data *data, uint8_t key_id);


enum mmwlan_status connection_keys_check_and_update_rx_replay(struct connection_keys_data *data,
                                                              uint8_t key_id,
                                                              uint64_t packet_number,
                                                              enum umac_key_rx_counter_space space);


size_t connection_keys_get_key_len(struct connection_keys_data *data, uint8_t key_id);


const uint8_t *connection_keys_get_key_data(struct connection_keys_data *data, uint8_t key_id);


int connection_keys_get_active_key_id(struct connection_keys_data *data,
                                      enum umac_key_type key_type);


void connection_keys_increment_tx_seq(struct connection_keys_data *data, uint8_t key_id);


uint64_t connection_keys_get_tx_seq(struct connection_keys_data *data, enum umac_key_type key_type);


