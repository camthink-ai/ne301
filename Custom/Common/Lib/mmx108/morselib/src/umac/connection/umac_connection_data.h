/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "mmwlan.h"
#include "umac_connection.h"
#include "dot11/dot11.h"
#include "connection_fsm.h"
#include "connection_mon_fsm.h"

struct connection_mon_data
{
    bool disabled;
    struct connection_mon_fsm_instance fsm;
    uint8_t failed_ap_queries;
};

struct connection_signal_monitor
{

    int16_t signal_threshold_dbm;

    int16_t signal_hysteresis_dbm;

    bool is_above_threshold;
};

enum umac_connection_mode
{

    UMAC_CONNECTION_MODE_NONE,

    UMAC_CONNECTION_MODE_STA,

    UMAC_CONNECTION_MODE_DPP,
};

struct umac_connection_data
{

    bool is_initialised;

    bool ecsa_active;
    uint16_t vif_id;
    struct umac_connection_bss_cfg bss_cfg;
    mmwlan_link_state_cb_t link_callback;
    void *link_arg;
    struct mmwlan_sta_args sta_args;
    mmwlan_sta_status_cb_t sta_status_cb;
    struct mmpkt *assoc_req_cache;
    struct connection_fsm_instance conn_fsm;
    struct connection_mon_data conn_mon;
    struct connection_signal_monitor conn_signal_mon;

    uint8_t ampdu_mss;

    uint8_t morse_mmss_offset;

    bool tx_traveling_pilots_supported;

    bool non_tim_mode_supported;

    uint8_t ssid[MMWLAN_SSID_MAXLEN];

    uint16_t ssid_len;

    struct umac_sta_data *stad;

    enum umac_connection_mode mode;

    struct mmwlan_dpp_args dpp_args;

    bool control_resp_1mhz_in_en;

    struct ie_s1g_operation ecsa_s1g_info;
};
