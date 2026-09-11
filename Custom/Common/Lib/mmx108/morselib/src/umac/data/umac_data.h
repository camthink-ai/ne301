/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mmwlan.h"




struct umac_data;


void umac_data_init(void);


void umac_data_deinit(void);


bool umac_data_is_initialised(struct umac_data *umacd);


struct umac_data *umac_data_get_umacd(void);


struct umac_config_data *umac_data_get_config(struct umac_data *umacd);


struct umac_connection_data *umac_data_get_connection(struct umac_data *umacd);


struct umac_core_data *umac_data_get_core(struct umac_data *umacd);


struct umac_datapath_data *umac_data_get_datapath(struct umac_data *umacd);


struct umac_interface_data *umac_data_get_interface(struct umac_data *umacd);


struct umac_interface_vif_data *umac_data_get_interface_vif(struct umac_data *umacd,
                                                            enum mmwlan_vif vif);


struct umac_ps_data *umac_data_get_ps(struct umac_data *umacd);


struct umac_scan_data *umac_data_get_scan(struct umac_data *umacd);


struct mmwlan_stats_umac_data *umac_data_get_stats(struct umac_data *umacd);


struct umac_supp_shim_data *umac_data_get_supp_shim(struct umac_data *umacd);


struct umac_twt_data *umac_data_get_twt(struct umac_data *umacd);


struct umac_root_data *umac_data_get_root(struct umac_data *umacd);


struct umac_wnm_sleep_data *umac_data_get_wnm_sleep(struct umac_data *umacd);


struct umac_offload_data *umac_data_get_offload(struct umac_data *umacd);


struct umac_ap_data *umac_data_get_ap(struct umac_data *umacd);


struct umac_ap_data *umac_data_alloc_ap(struct umac_data *umacd);


void umac_data_dealloc_ap(struct umac_data *umacd);






struct umac_sta_data;


struct umac_sta_data *umac_sta_data_alloc_static(struct umac_data *umacd);


struct umac_sta_data *umac_sta_data_alloc(struct umac_data *umacd);


struct umac_data *umac_sta_data_get_umacd(struct umac_sta_data *stad);


struct umac_ba_sta_data *umac_sta_data_get_ba(struct umac_sta_data *stad);


struct umac_keys_sta_data *umac_sta_data_get_keys(struct umac_sta_data *stad);


struct umac_datapath_sta_data *umac_sta_data_get_datapath(struct umac_sta_data *stad);


struct umac_rc_sta_data *umac_sta_data_get_rc(struct umac_sta_data *stad);


struct umac_ap_sta_data *umac_sta_data_get_ap(struct umac_sta_data *stad);


void umac_sta_data_set_aid(struct umac_sta_data *stad, uint16_t aid);


uint16_t umac_sta_data_get_aid(struct umac_sta_data *stad);


void umac_sta_data_set_vif_id(struct umac_sta_data *stad, uint16_t vif_id);


uint16_t umac_sta_data_get_vif_id(struct umac_sta_data *stad);


void umac_sta_data_set_bssid(struct umac_sta_data *stad, const uint8_t *bssid);


void umac_sta_data_get_bssid(struct umac_sta_data *stad, uint8_t *bssid);


const uint8_t *umac_sta_data_peek_bssid(struct umac_sta_data *stad);


bool umac_sta_data_matches_bssid(struct umac_sta_data *stad, const uint8_t *bssid);


void umac_sta_data_set_peer_addr(struct umac_sta_data *stad, const uint8_t *addr);


void umac_sta_data_get_peer_addr(struct umac_sta_data *stad, uint8_t *addr);


const uint8_t *umac_sta_data_peek_peer_addr(struct umac_sta_data *stad);


bool umac_sta_data_matches_peer_addr(struct umac_sta_data *stad, const uint8_t *addr);


void umac_sta_data_set_security(struct umac_sta_data *stad,
                                enum mmwlan_security_type security_type,
                                enum mmwlan_pmf_mode pmf_mode);


bool umac_sta_data_pmf_is_required(struct umac_sta_data *stad);


enum mmwlan_security_type umac_sta_data_get_security_type(struct umac_sta_data *stad);


void umac_sta_data_queue_pkt(struct umac_sta_data *stad, struct mmpkt *mmpkt);


struct mmpkt *umac_sta_data_pop_pkt(struct umac_sta_data *stad);


uint32_t umac_sta_data_get_queued_len(struct umac_sta_data *stad);


bool umac_sta_data_is_paused(struct umac_sta_data *stad);


