/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmpkt.h"
#include "umac/umac.h"
#include "umac/frames/probe_response.h"


struct umac_scan_response
{

    int16_t rssi;

    uint32_t channel_freq_hz;

    uint8_t bw_mhz;

    uint8_t op_bw_mhz;

    int8_t noise_dbm;

    struct frame_data_probe_response frame;
};


typedef void (*umac_scan_rx_cb_t)(struct umac_data *umacd, const struct umac_scan_response *rsp);


typedef void (*umac_scan_complete_cb_t)(struct umac_data *umacd,
                                        enum mmwlan_scan_state result_code);


struct umac_scan_req
{

    struct mmwlan_scan_args args;

    umac_scan_rx_cb_t rx_cb;

    umac_scan_complete_cb_t complete_cb;
};




void umac_scan_init(struct umac_data *umacd);


void umac_scan_deinit(struct umac_data *umacd);


void umac_scan_abort(struct umac_data *umacd, const struct umac_scan_req *scan_req);


void umac_scan_handle_hw_restarted(struct umac_data *umacd);


bool umac_scan_has_scan_req(struct umac_data *umacd);


void umac_scan_hw_scan_done(struct umac_data *umacd, enum mmwlan_scan_state state);


enum mmwlan_status umac_scan_store_scan_config(struct umac_data *umacd,
                                               struct mmwlan_scan_args *scan_args);




void umac_scan_process_probe_resp(struct umac_data *umacd, struct mmpktview *rxbufview);


void umac_scan_fill_result(struct mmwlan_scan_result *res, const struct umac_scan_response *rsp);


enum mmwlan_status umac_scan_queue_request(struct umac_data *umacd, struct umac_scan_req *scan_req);


