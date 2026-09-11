/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmpkt.h"
#include "umac/data/umac_data.h"
#include "dot11/dot11.h"


#define UMAC_BA_MAX_AGGR_TID 5


void umac_ba_deinit(struct umac_sta_data *stad);


void umac_ba_process_rx_frame(struct umac_sta_data *stad, const uint8_t *frame, uint32_t frame_len);


void umac_ba_session_init(struct umac_sta_data *stad, uint8_t tid, uint16_t ssc, uint16_t timeout);


void umac_ba_session_deinit(struct umac_sta_data *stad,
                            uint8_t tid,
                            enum dot11_delba_initiator initiator);


uint8_t umac_ba_get_reorder_buffer_size(struct umac_sta_data *stad, uint8_t tid);


int32_t umac_ba_get_expected_rx_seq_num(struct umac_sta_data *stad, uint8_t tid);


void umac_ba_set_expected_rx_seq_num(struct umac_sta_data *stad, uint8_t tid, uint16_t seq_num);


bool umac_ba_is_ampdu_permitted(struct umac_sta_data *stad, uint8_t tid);


