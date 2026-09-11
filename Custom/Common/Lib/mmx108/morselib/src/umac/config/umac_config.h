/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "umac/data/umac_data.h"


void umac_config_init(struct umac_data *umacd);


struct umac_config_rc_override
{
    enum mmwlan_mcs tx_rate;
    enum mmwlan_bw bandwidth;
    enum mmwlan_gi guard_interval;
};


void umac_config_rc_set_override(struct umac_data *umacd,
                                 const struct umac_config_rc_override *rc_override);


const struct umac_config_rc_override *umac_config_rc_get_override(struct umac_data *umacd);


void umac_config_rc_set_subbands_enabled(struct umac_data *umacd, bool subbands_enabled);


bool umac_config_rc_are_subbands_enabled(struct umac_data *umacd);


void umac_config_rc_set_sgi_enabled(struct umac_data *umacd, bool sgi_enabled);


bool umac_config_rc_is_sgi_enabled(struct umac_data *umacd);


void umac_config_set_supported_channel_width_field_override(struct umac_data *umacd, int override);


int umac_config_get_supported_channel_width_field_override(struct umac_data *umacd);


void umac_config_set_opclass_check_enabled(struct umac_data *umacd, bool enabled);


bool umac_config_is_opclass_check_enabled(struct umac_data *umacd);


void umac_config_set_ctrl_resp_out_1mhz_enabled(struct umac_data *umacd, bool enabled);


bool umac_config_is_ctrl_resp_out_1mhz_enabled(struct umac_data *umacd);


void umac_config_set_ampdu_enabled(struct umac_data *umacd, bool enabled);


bool umac_config_is_ampdu_enabled(struct umac_data *umacd);


void umac_config_set_non_tim_mode_enabled(struct umac_data *umacd, bool non_tim_mode_enabled);


bool umac_config_is_non_tim_mode_enabled(struct umac_data *umacd);


void umac_config_set_chip_powerdown_enabled(struct umac_data *umacd, bool chip_powerdown_enabled);


bool umac_config_is_chip_powerdown_enabled(struct umac_data *umacd);


void umac_config_set_rts_threshold(struct umac_data *umacd, uint32_t threshold);


uint32_t umac_config_get_rts_threshold(struct umac_data *umacd);


void umac_config_set_frag_threshold(struct umac_data *umacd, uint32_t threshold);


uint32_t umac_config_get_frag_threshold(struct umac_data *umacd);


void umac_config_set_supp_scan_dwell_time(struct umac_data *umacd, uint32_t dwell_time_ms);


uint32_t umac_config_get_supp_scan_dwell_time(struct umac_data *umacd);


void umac_config_set_supp_scan_home_dwell_time(struct umac_data *umacd,
                                               uint32_t home_dwell_time_ms);


uint32_t umac_config_get_supp_scan_home_dwell_time(struct umac_data *umacd);


void umac_config_set_max_tx_power(struct umac_data *umacd, uint16_t tx_power_dbm);


uint16_t umac_config_get_max_tx_power(struct umac_data *umacd);


void umac_config_set_channel_list(struct umac_data *umacd,
                                  const struct mmwlan_s1g_channel_list *channel_list);


const struct mmwlan_s1g_channel_list *umac_config_get_channel_list(struct umac_data *umacd);


void umac_config_set_min_scan_spacing_ms(struct umac_data *umacd, uint32_t min_scan_spacing_ms);


uint32_t umac_config_get_min_scan_spacing_ms(struct umac_data *umacd);


void umac_config_set_max_supp_scan_results(struct umac_data *umacd, uint16_t max_scan_results);


uint16_t umac_config_get_max_supp_scan_results(struct umac_data *umacd);


void umac_config_set_listen_interval(struct umac_data *umacd, uint16_t listen_interval);


uint16_t umac_config_get_listen_interval(struct umac_data *umacd);


void umac_config_set_ndp_probe_support(struct umac_data *umacd, bool enabled);


void umac_config_set_dynamic_ps_timeout(struct umac_data *umacd, uint32_t timeout_ms);


uint32_t umac_config_get_dynamic_ps_timeout(struct umac_data *umacd);


bool umac_config_is_ndp_probe_supported(struct umac_data *umacd);


void umac_config_set_ps_mode(struct umac_data *umacd, enum mmwlan_ps_mode mode);


enum mmwlan_ps_mode umac_config_get_ps_mode(struct umac_data *umacd);


void umac_config_set_beacon_vendor_ie_filter(struct umac_data *umacd,
                                             const struct mmwlan_beacon_vendor_ie_filter *filter);


const struct mmwlan_beacon_vendor_ie_filter *umac_config_get_beacon_vendor_ie_filter(
    struct umac_data *umacd);


void umac_config_set_health_check_interval(struct umac_data *umacd,
                                           uint32_t min_health_check_intvl_ms,
                                           uint32_t max_health_check_intvl_ms);


void umac_config_get_health_check_interval(struct umac_data *umacd,
                                           uint32_t *min_health_check_intvl_ms,
                                           uint32_t *max_health_check_intvl_ms);


void umac_config_set_datapath_rx_reorder_list_maxlen(struct umac_data *umacd,
                                                     uint32_t rx_reorder_list_maxlen);


uint32_t umac_config_get_datapath_rx_reorder_list_maxlen(struct umac_data *umacd);


void umac_config_set_default_qos_queue_params(struct umac_data *umacd,
                                              const struct mmwlan_qos_queue_params *params);


const struct mmwlan_qos_queue_params *umac_config_get_default_qos_queue_params(
    struct umac_data *umacd);


void umac_config_set_mcs10_mode(struct umac_data *umacd, enum mmwlan_mcs10_mode mcs10_mode);


enum mmwlan_mcs10_mode umac_config_get_mcs10_mode(struct umac_data *umacd);


void umac_config_set_duty_cycle_mode(struct umac_data *umacd,
                                     enum mmwlan_duty_cycle_mode duty_cycle_mode);


enum mmwlan_duty_cycle_mode umac_config_get_duty_cycle_mode(struct umac_data *umacd);


