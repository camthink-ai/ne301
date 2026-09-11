/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/umac.h"
#include "umac/data/umac_data.h"
#include "mmwlan.h"
#include "mmpkt.h"


void umac_rc_init(void);


void umac_rc_deinit(struct umac_sta_data *stad);


void umac_rc_start(struct umac_sta_data *stad, uint8_t sgi_flags, uint8_t max_mcs);


void umac_rc_stop(struct umac_sta_data *stad);


struct mmrc_rate_table;


void umac_rc_init_rate_table_mgmt(struct umac_data *umacd,
                                  struct mmrc_rate_table *table,
                                  bool rts_required);


void umac_rc_init_rate_table_data(struct umac_sta_data *stad,
                                  struct mmrc_rate_table *table,
                                  bool rts_required,
                                  uint32_t frame_size);


void umac_rc_feedback(struct umac_sta_data *stad, struct mmdrv_tx_metadata *tx_metadata);


struct mmwlan_rc_stats *umac_rc_get_rc_stats(struct umac_sta_data *stad);


void umac_rc_free_rc_stats(struct mmwlan_rc_stats *stats);


