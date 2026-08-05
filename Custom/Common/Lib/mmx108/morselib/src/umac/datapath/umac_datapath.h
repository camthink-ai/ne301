/*
 * Copyright 2022-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/data/umac_data.h"
#include "umac/frames/frames_common.h"
#include "umac/ps/umac_ps.h"
#include "mmdrv.h"
#include "mmpkt.h"
#include "mmwlan.h"
#include "mmwlan_internal.h"


#ifndef UMAC_DATAPATH_DEFAULT_RXREORDERQ_MAXLEN
#define UMAC_DATAPATH_DEFAULT_RXREORDERQ_MAXLEN (16)
#endif


void umac_datapath_init(struct umac_data *umacd);


void umac_datapath_deinit(struct umac_data *umacd);


void umac_datapath_configure_sta_mode(struct umac_data *umacd);


void umac_datapath_configure_ap_mode(struct umac_data *umacd);


void umac_datapath_configure_scan_mode(struct umac_data *umacd);


void umac_datapath_stad_init(struct umac_sta_data *stad);


void umac_datapath_stad_flush(struct umac_data *umacd, struct umac_sta_data *stad);


void umac_datapath_stad_flush_txq(struct umac_data *umacd, struct umac_sta_data *stad);


void umac_datapath_flush_rx_reorder_list_for_tid(struct umac_sta_data *stad, uint16_t tid);


enum mmwlan_status umac_datapath_register_rx_cb(struct umac_data *umacd,
                                                mmwlan_rx_cb_t callback,
                                                void *arg);


enum mmwlan_status umac_datapath_register_rx_pkt_cb(struct umac_data *umacd,
                                                    mmwlan_rx_pkt_cb_t callback,
                                                    void *arg);


enum mmwlan_status umac_datapath_register_rx_pkt_ext_cb(struct umac_data *umacd,
                                                        enum mmwlan_vif vif,
                                                        mmwlan_rx_pkt_ext_cb_t callback,
                                                        void *arg);


void umac_datapath_rx_frame(struct umac_data *umacd, struct mmpkt *rxbuf);


enum mmwlan_status umac_datapath_register_tx_flow_control_cb(struct umac_data *umacd,
                                                             mmwlan_tx_flow_control_cb_t callback,
                                                             void *arg);


enum umac_datapath_frame_encryption
{

    ENCRYPTION_AUTO,

    ENCRYPTION_DISABLED,

    ENCRYPTION_ENABLED,
};


enum mmwlan_status umac_datapath_tx_frame(struct umac_data *umacd,
                                          struct mmpkt *txbuf,
                                          enum umac_datapath_frame_encryption enc,
                                          const uint8_t *ra);


enum mmwlan_status umac_datapath_wait_for_tx_ready(struct umac_data *umacd, uint32_t timeout_ms);


bool umac_datapath_process(struct umac_data *umacd);


enum mmwlan_status umac_datapath_tx_mgmt_frame(struct umac_sta_data *stad, struct mmpkt *txbuf);


enum mmwlan_status umac_datapath_tx_mgmt_frame_ap(struct umac_data *umacd,
                                                  struct mmpkt *txbuf,
                                                  struct mmrc_rate *mmrc_rate_override);


enum mmwlan_status umac_datapath_build_and_tx_to_ds_qos_null_frame(struct umac_sta_data *stad);


enum mmwlan_status umac_datapath_build_and_tx_4addr_qos_null_frame(struct umac_sta_data *stad);


enum mmwlan_status umac_datapath_build_and_tx_mgmt_frame(struct umac_sta_data *stad,
                                                         mgmt_frame_builder_t builder,
                                                         void *params);


void umac_datapath_handle_tx_status(struct umac_data *umacd, struct mmpkt *mmpkt);


enum mmwlan_status umac_datapath_build_copy_and_queue_mgmt_frame_tx(struct umac_sta_data *stad,
                                                                    mgmt_frame_builder_t builder,
                                                                    void *params,
                                                                    struct mmpkt **copy);


struct mmpkt *umac_datapath_alloc_mmpkt_for_qos_data_tx(uint32_t payload_len, uint8_t pkt_class);


struct mmpkt *umac_datapath_alloc_raw_tx_mmpkt(uint8_t pkt_class,
                                               uint32_t space_at_start,
                                               uint32_t space_at_end);


struct mmpkt *umac_datapath_copy_tx_mmpkt(struct mmpkt *pkt, uint8_t pkt_class);


enum umac_datapath_pause_source
{
    UMAC_DATAPATH_PAUSE_SOURCE_SCAN = 0x100,
    UMAC_DATAPATH_PAUSE_SOURCE_WNM_SLEEP = 0x200,
    UMAC_DATAPATH_PAUSE_SOURCE_STANDBY = 0x400,
    UMAC_DATAPATH_PAUSE_SOURCE_ECSA = 0x800,
};


void umac_datapath_pause(struct umac_data *umacd, uint16_t source_mask);


void umac_datapath_unpause(struct umac_data *umacd, uint16_t source_mask);


void umac_datapath_update_tx_paused(struct umac_data *umacd,
                                    uint16_t source_mask,
                                    mmdrv_host_update_tx_paused_cb_t cb);


void umac_datapath_handle_hw_restarted(struct umac_data *umacd, struct umac_sta_data *stad);


enum mmwlan_status umac_datapath_register_rx_frame_cb(struct umac_data *umacd,
                                                      uint32_t filter,
                                                      mmwlan_rx_frame_cb_t callback,
                                                      void *arg);


void umac_datapath_set_filter_all_beacons(struct umac_data *umacd, bool filter);


