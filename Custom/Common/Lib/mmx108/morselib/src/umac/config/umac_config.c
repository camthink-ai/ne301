/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac_config.h"
#include "umac_config_data.h"

#include "umac/datapath/umac_datapath.h"


#define MORSE_DEFAULT_TXOP_MAX_US (15008)


#define MORSE_DEFAULT_CONTENTION_WINDOW_MIN 15


#define MORSE_DEFAULT_CONTENTION_WINDOW_MAX 1023

static void populate_default_qos_queue_params(
    struct mmwlan_qos_queue_params qos_queue_params[MMWLAN_QOS_QUEUE_NUM_ACIS])
{

    struct mmwlan_qos_queue_params *params = &qos_queue_params[0];
    params->aci = 0;
    params->aifs = 3;
    params->cw_min = MORSE_DEFAULT_CONTENTION_WINDOW_MIN;
    params->cw_max = MORSE_DEFAULT_CONTENTION_WINDOW_MAX;
    params->txop_max_us = MORSE_DEFAULT_TXOP_MAX_US;

    params = &qos_queue_params[1];
    params->aci = 1;
    params->aifs = 7;
    params->cw_min = MORSE_DEFAULT_CONTENTION_WINDOW_MIN;
    params->cw_max = MORSE_DEFAULT_CONTENTION_WINDOW_MAX;
    params->txop_max_us = MORSE_DEFAULT_TXOP_MAX_US;

    params = &qos_queue_params[2];
    params->aci = 2;
    params->aifs = 2;
    params->cw_min = (MORSE_DEFAULT_CONTENTION_WINDOW_MIN + 1) / 2 - 1;
    params->cw_max = MORSE_DEFAULT_CONTENTION_WINDOW_MIN;
    params->txop_max_us = MORSE_DEFAULT_TXOP_MAX_US;

    params = &qos_queue_params[3];
    params->aci = 3;
    params->aifs = 2;
    params->cw_min = (MORSE_DEFAULT_CONTENTION_WINDOW_MIN + 1) / 4 - 1;
    params->cw_max = (MORSE_DEFAULT_CONTENTION_WINDOW_MIN + 1) / 2 - 1;
    params->txop_max_us = MORSE_DEFAULT_TXOP_MAX_US;
}

void umac_config_init(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->rc_override.tx_rate = MMWLAN_MCS_NONE;
    data->rc_override.bandwidth = MMWLAN_BW_NONE;
    data->rc_override.guard_interval = MMWLAN_GI_NONE;
    data->subbands_enabled = true;
    data->sgi_enabled = true;
    data->supported_channel_width_override = -1;
    data->opclass_check_enabled = true;
    data->ctrl_resp_out_1mhz_enabled = false;
    data->ampdu_enabled = true;
    data->chip_powerdown_enabled = false;
    data->rts_threshold = 0;
    data->fragmentation_threshold = 0;
    data->max_tx_power_dbm = 0;
    data->max_supp_scan_results = 10;
    data->channel_list = NULL;
    data->min_scan_spacing_ms = 0;
    data->listen_interval = 0;
    data->ndp_probe_request_enabled = false;
    data->dynamic_ps_timeout_ms = MMWLAN_DEFAULT_DYNAMIC_PS_TIMEOUT_MS;
    data->ps_mode = MMWLAN_DEFAULT_PS_MODE;
    data->supp_scan_dwell_time_ms = MMWLAN_SCAN_DEFAULT_DWELL_TIME_MS;
    data->beacon_vendor_ie_filter = NULL;
    data->min_health_check_intvl_ms = MMWLAN_DEFAULT_MIN_HEALTH_CHECK_INTERVAL_MS;
    data->max_health_check_intvl_ms = MMWLAN_DEFAULT_MAX_HEALTH_CHECK_INTERVAL_MS;
    data->datapath_rx_reorder_list_maxlen = UMAC_DATAPATH_DEFAULT_RXREORDERQ_MAXLEN;
    populate_default_qos_queue_params(data->default_qos_queue_params);
    data->mcs10_mode = MMWLAN_MCS10_MODE_DISABLED;
    data->supp_scan_home_dwell_time_ms = MMWLAN_SCAN_DEFAULT_DWELL_ON_HOME_MS;
    data->duty_cycle_mode = MMWLAN_DUTY_CYCLE_MODE_SPREAD;
    data->non_tim_mode_enabled = false;
}

void umac_config_rc_set_override(struct umac_data *umacd,
                                 const struct umac_config_rc_override *rc_override)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->rc_override = *rc_override;
}

const struct umac_config_rc_override *umac_config_rc_get_override(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return &data->rc_override;
}

void umac_config_rc_set_subbands_enabled(struct umac_data *umacd, bool subbands_enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->subbands_enabled = subbands_enabled;
}

bool umac_config_rc_are_subbands_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->subbands_enabled;
}

void umac_config_rc_set_sgi_enabled(struct umac_data *umacd, bool sgi_enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->sgi_enabled = sgi_enabled;
}

bool umac_config_rc_is_sgi_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->sgi_enabled;
}

void umac_config_set_supported_channel_width_field_override(struct umac_data *umacd, int override)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->supported_channel_width_override = override;
}

int umac_config_get_supported_channel_width_field_override(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->supported_channel_width_override;
}

void umac_config_set_opclass_check_enabled(struct umac_data *umacd, bool enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->opclass_check_enabled = enabled;
}

bool umac_config_is_opclass_check_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->opclass_check_enabled;
}

void umac_config_set_ctrl_resp_out_1mhz_enabled(struct umac_data *umacd, bool enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->ctrl_resp_out_1mhz_enabled = enabled;
}

bool umac_config_is_ctrl_resp_out_1mhz_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->ctrl_resp_out_1mhz_enabled;
}

void umac_config_set_ampdu_enabled(struct umac_data *umacd, bool enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->ampdu_enabled = enabled;
}

bool umac_config_is_ampdu_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->ampdu_enabled;
}

void umac_config_set_non_tim_mode_enabled(struct umac_data *umacd, bool non_tim_mode_enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->non_tim_mode_enabled = non_tim_mode_enabled;
}

bool umac_config_is_non_tim_mode_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->non_tim_mode_enabled;
}

void umac_config_set_chip_powerdown_enabled(struct umac_data *umacd, bool chip_powerdown_enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->chip_powerdown_enabled = chip_powerdown_enabled;
}

bool umac_config_is_chip_powerdown_enabled(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->chip_powerdown_enabled;
}

void umac_config_set_rts_threshold(struct umac_data *umacd, uint32_t threshold)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->rts_threshold = threshold;
}

uint32_t umac_config_get_rts_threshold(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->rts_threshold;
}

void umac_config_set_frag_threshold(struct umac_data *umacd, uint32_t threshold)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->fragmentation_threshold = threshold;
}

uint32_t umac_config_get_frag_threshold(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->fragmentation_threshold;
}

void umac_config_set_supp_scan_dwell_time(struct umac_data *umacd, uint32_t dwell_time_ms)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->supp_scan_dwell_time_ms = dwell_time_ms;
}

uint32_t umac_config_get_supp_scan_dwell_time(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->supp_scan_dwell_time_ms;
}

void umac_config_set_supp_scan_home_dwell_time(struct umac_data *umacd, uint32_t home_dwell_time_ms)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->supp_scan_home_dwell_time_ms = home_dwell_time_ms;
}

uint32_t umac_config_get_supp_scan_home_dwell_time(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->supp_scan_home_dwell_time_ms;
}

void umac_config_set_max_tx_power(struct umac_data *umacd, uint16_t tx_power_dbm)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->max_tx_power_dbm = tx_power_dbm;
}

uint16_t umac_config_get_max_tx_power(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->max_tx_power_dbm;
}

void umac_config_set_channel_list(struct umac_data *umacd,
                                  const struct mmwlan_s1g_channel_list *channel_list)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->channel_list = channel_list;
}

const struct mmwlan_s1g_channel_list *umac_config_get_channel_list(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->channel_list;
}

void umac_config_set_min_scan_spacing_ms(struct umac_data *umacd, uint32_t min_scan_spacing_ms)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->min_scan_spacing_ms = min_scan_spacing_ms;
}

uint32_t umac_config_get_min_scan_spacing_ms(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->min_scan_spacing_ms;
}

void umac_config_set_max_supp_scan_results(struct umac_data *umacd, uint16_t max_scan_results)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->max_supp_scan_results = max_scan_results;
}

uint16_t umac_config_get_max_supp_scan_results(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->max_supp_scan_results;
}

void umac_config_set_listen_interval(struct umac_data *umacd, uint16_t listen_interval)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->listen_interval = listen_interval;
}

uint16_t umac_config_get_listen_interval(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->listen_interval;
}

void umac_config_set_ndp_probe_support(struct umac_data *umacd, bool enabled)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->ndp_probe_request_enabled = enabled;
}

bool umac_config_is_ndp_probe_supported(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->ndp_probe_request_enabled;
}

void umac_config_set_dynamic_ps_timeout(struct umac_data *umacd, uint32_t timeout_ms)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->dynamic_ps_timeout_ms = timeout_ms;
}

uint32_t umac_config_get_dynamic_ps_timeout(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->dynamic_ps_timeout_ms;
}

void umac_config_set_ps_mode(struct umac_data *umacd, enum mmwlan_ps_mode mode)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->ps_mode = mode;
}

enum mmwlan_ps_mode umac_config_get_ps_mode(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->ps_mode;
}

void umac_config_set_beacon_vendor_ie_filter(struct umac_data *umacd,
                                             const struct mmwlan_beacon_vendor_ie_filter *filter)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->beacon_vendor_ie_filter = filter;
}

const struct mmwlan_beacon_vendor_ie_filter *umac_config_get_beacon_vendor_ie_filter(
    struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->beacon_vendor_ie_filter;
}

void umac_config_set_health_check_interval(struct umac_data *umacd,
                                           uint32_t min_health_check_intvl_ms,
                                           uint32_t max_health_check_intvl_ms)
{
    struct umac_config_data *config = umac_data_get_config(umacd);
    config->min_health_check_intvl_ms = min_health_check_intvl_ms;
    config->max_health_check_intvl_ms = max_health_check_intvl_ms;
}

void umac_config_get_health_check_interval(struct umac_data *umacd,
                                           uint32_t *min_health_check_intvl_ms,
                                           uint32_t *max_health_check_intvl_ms)
{
    struct umac_config_data *config = umac_data_get_config(umacd);
    *min_health_check_intvl_ms = config->min_health_check_intvl_ms;
    *max_health_check_intvl_ms = config->max_health_check_intvl_ms;
}

void umac_config_set_datapath_rx_reorder_list_maxlen(struct umac_data *umacd,
                                                     uint32_t rx_reorder_list_maxlen)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->datapath_rx_reorder_list_maxlen = rx_reorder_list_maxlen;
}

uint32_t umac_config_get_datapath_rx_reorder_list_maxlen(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->datapath_rx_reorder_list_maxlen;
}

void umac_config_set_default_qos_queue_params(struct umac_data *umacd,
                                              const struct mmwlan_qos_queue_params *params)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    MMOSAL_ASSERT(params->aci < MM_ARRAY_COUNT(data->default_qos_queue_params));
    data->default_qos_queue_params[params->aci] = *params;
}

const struct mmwlan_qos_queue_params *umac_config_get_default_qos_queue_params(
    struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->default_qos_queue_params;
}

void umac_config_set_mcs10_mode(struct umac_data *umacd, enum mmwlan_mcs10_mode mcs10_mode)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->mcs10_mode = mcs10_mode;
}

enum mmwlan_mcs10_mode umac_config_get_mcs10_mode(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->mcs10_mode;
}

void umac_config_set_duty_cycle_mode(struct umac_data *umacd,
                                     enum mmwlan_duty_cycle_mode duty_cycle_mode)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    data->duty_cycle_mode = duty_cycle_mode;
}

enum mmwlan_duty_cycle_mode umac_config_get_duty_cycle_mode(struct umac_data *umacd)
{
    struct umac_config_data *data = umac_data_get_config(umacd);
    return data->duty_cycle_mode;
}
