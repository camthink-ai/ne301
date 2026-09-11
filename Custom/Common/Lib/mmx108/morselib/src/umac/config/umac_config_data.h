/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_config.h"

struct umac_config_data
{

    struct umac_config_rc_override rc_override;
    bool subbands_enabled;
    bool sgi_enabled;
    int supported_channel_width_override;
    bool opclass_check_enabled;
    bool ctrl_resp_out_1mhz_enabled;
    bool ampdu_enabled;
    bool chip_powerdown_enabled;
    uint32_t rts_threshold;
    uint32_t fragmentation_threshold;
    uint16_t max_tx_power_dbm;
    uint16_t max_supp_scan_results;
    const struct mmwlan_s1g_channel_list *channel_list;
    uint32_t min_scan_spacing_ms;
    uint16_t listen_interval;
    bool ndp_probe_request_enabled;
    uint32_t dynamic_ps_timeout_ms;
    enum mmwlan_ps_mode ps_mode;
    uint32_t supp_scan_dwell_time_ms;
    const struct mmwlan_beacon_vendor_ie_filter *beacon_vendor_ie_filter;
    uint32_t min_health_check_intvl_ms;
    uint32_t max_health_check_intvl_ms;
    uint32_t datapath_rx_reorder_list_maxlen;
    struct mmwlan_qos_queue_params default_qos_queue_params[MMWLAN_QOS_QUEUE_NUM_ACIS];
    enum mmwlan_mcs10_mode mcs10_mode;
    uint32_t supp_scan_home_dwell_time_ms;
    enum mmwlan_duty_cycle_mode duty_cycle_mode;
    bool non_tim_mode_enabled;
};
