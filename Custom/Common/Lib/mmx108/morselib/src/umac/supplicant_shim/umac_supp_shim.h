/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/umac.h"
#include "umac/data/umac_data.h"
#include "umac/connection/umac_connection.h"
#include "dot11/dot11_frames.h"
#include "umac/frames/authentication.h"
#include "umac/frames/association.h"
#include "umac/keys/umac_keys.h"


#define UMAC_SUPP_STA_CONFIG_NAME "STA"


#define UMAC_SUPP_DPP_CONFIG_NAME "DPP"


enum mmwlan_status umac_supp_add_sta_interface(struct umac_data *umacd, const char *confname);


enum mmwlan_status umac_supp_add_ap_interface(struct umac_data *umacd);


enum mmwlan_status umac_supp_remove_sta_interface(struct umac_data *umacd);


enum mmwlan_status umac_supp_remove_ap_interface(struct umac_data *umacd);


enum mmwlan_status umac_supp_connect(struct umac_data *umacd);


enum mmwlan_status umac_supp_reconnect(struct umac_data *umacd);


enum mmwlan_status umac_supp_dpp_push_button(struct umac_data *umacd);


void umac_supp_dpp_push_button_stop(struct umac_data *umacd);


void umac_supp_disconnect(struct umac_data *umacd);


void umac_supp_deinit(struct umac_data *umacd);


void umac_supp_set_auto_reconnect_disabled(struct umac_data *umacd, bool auto_reconnect_disabled);


void umac_supp_l2_sock_receive(struct umac_data *umacd,
                               const uint8_t *payload,
                               size_t payload_len,
                               const uint8_t *src_addr);


void umac_supp_process_deauth(struct umac_data *umacd);


void umac_supp_wnm_enter(struct umac_data *umacd);


void umac_supp_wnm_exit(struct umac_data *umacd);


void umac_supp_process_auth_resp(struct umac_data *umacd, struct frame_data_auth *auth_data);


void umac_supp_process_assoc_reassoc_resp(struct umac_data *umacd,
                                          struct frame_data_assoc_rsp *assoc_data);


void umac_supp_process_disassoc_req(struct umac_data *umacd, uint16_t reason_code);


void umac_supp_process_unprotected_deauth(struct umac_data *umacd,
                                          uint16_t reason_code,
                                          const uint8_t *sa,
                                          const uint8_t *da);


void umac_supp_process_unprotected_disassoc(struct umac_data *umacd,
                                            uint16_t reason_code,
                                            const uint8_t *sa,
                                            const uint8_t *da);


void umac_supp_process_mgmt_frame(struct umac_data *umacd, struct mmpktview *rxbufview);


void umac_supp_process_probe_req_frame(struct umac_data *umacd,
                                       const uint8_t *frame,
                                       uint32_t frame_len,
                                       int16_t rssi_dbm);


void umac_supp_notify_signal_change(struct umac_data *umacd, int16_t rssi, bool above_threshold);


void umac_supp_roam(struct umac_data *umacd);


void umac_supp_tx_status(struct umac_data *umacd, struct mmpkt *pkt, bool acked);


bool bip_is_valid(struct umac_sta_data *stad,
                  const struct dot11_hdr *header,
                  const uint8_t *data,
                  size_t data_len);


bool ccmp_is_valid(struct umac_sta_data *stad,
                   uint8_t *ccmp_header,
                   enum umac_key_rx_counter_space space);


