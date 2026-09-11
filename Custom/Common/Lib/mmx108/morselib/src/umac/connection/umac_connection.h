/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmwlan_internal.h"
#include "mmpkt.h"
#include "umac/data/umac_data.h"
#include "umac/frames/association.h"
#include "umac/frames/authentication.h"
#include "umac/frames/deauthentication.h"
#include "umac/ies/s1g_operation.h"


enum umac_connection_signal_change
{
    UMAC_CONNECTION_SIGNAL_CHANGE_NO_CHANGE,
    UMAC_CONNECTION_SIGNAL_CHANGE_ABOVE_THRESHOLD,
    UMAC_CONNECTION_SIGNAL_CHANGE_BELOW_THRESHOLD
};


struct umac_connection_bss_cfg
{

    struct ie_s1g_operation channel_cfg;

    uint16_t beacon_interval;
};


void umac_connection_init(struct umac_data *umacd);


void umac_connection_deinit(struct umac_data *umacd);


bool umac_connection_validate_sta_args(const struct mmwlan_sta_args *args);


enum mmwlan_status umac_connection_start(struct umac_data *umacd,
                                         const struct mmwlan_sta_args *args,
                                         mmwlan_sta_status_cb_t sta_status_cb,
                                         uint8_t *extra_assoc_ies);



enum mmwlan_status umac_connection_reassoc(struct umac_data *umacd);


enum mmwlan_status umac_connection_stop(struct umac_data *umacd);


enum mmwlan_status umac_connection_start_dpp(struct umac_data *umacd,
                                             const struct mmwlan_dpp_args *args);


enum mmwlan_status umac_connection_stop_dpp(struct umac_data *umacd);


void umac_connection_handle_dpp_event(struct umac_data *umacd,
                                      const struct mmwlan_dpp_cb_args *event);


void umac_connection_handle_hw_restarted(struct umac_data *umacd);


const struct mmwlan_sta_args *umac_connection_get_sta_args(struct umac_data *umacd);


static inline bool umac_connection_is_raw_enabled(const struct mmwlan_sta_args *sta_args)
{
    return sta_args->raw_sta_priority >= 0;
}


static inline bool umac_connection_is_cac_enabled(const struct mmwlan_sta_args *sta_args)
{
    return sta_args->cac_mode == MMWLAN_CAC_ENABLED;
}


enum mmwlan_sta_state umac_connection_get_state(struct umac_data *umacd);


void umac_connection_roam(struct umac_data *umacd, const uint8_t *bssid);


void umac_connection_handle_port_state(struct umac_data *umacd, bool authorized);


enum mmwlan_status umac_connection_register_link_cb(struct umac_data *umacd,
                                                    mmwlan_link_state_cb_t callback,
                                                    void *arg);


int umac_connection_get_ssid(struct umac_data *umacd, uint8_t *ssid);


enum mmwlan_status umac_connection_set_bss_cfg(struct umac_data *umacd,
                                               const uint8_t *bssid,
                                               struct umac_connection_bss_cfg *config);


enum mmwlan_status umac_connection_get_bssid(struct umac_data *umacd, uint8_t *bssid);


bool umac_connection_bss_is_configured(struct umac_data *umacd);


bool umac_connection_addr_matches_bssid(struct umac_data *umacd, const uint8_t *addr);


enum mmwlan_status umac_connection_get_aid(struct umac_data *umacd, uint16_t *aid);


void umac_connection_set_drv_qos_cfg_default(struct umac_data *umacd);


void umac_connection_process_assoc_reassoc_rsp(struct umac_data *umacd,
                                               struct mmpktview *rxbufview);


enum mmwlan_status umac_connection_process_assoc_req(struct umac_data *umacd,
                                                     struct frame_data_assoc_req *params);


void umac_connection_process_disassoc_req(struct umac_data *umacd, struct mmpktview *rxbufview);


void umac_connection_process_auth_resp(struct umac_data *umacd, struct mmpktview *rxbufview);


enum mmwlan_status umac_connection_process_auth_req(struct umac_data *umacd,
                                                    struct frame_data_auth *params);


void umac_connection_process_deauth_rx(struct umac_data *umacd, struct mmpktview *rxbufview);


enum mmwlan_status umac_connection_process_deauth_tx(struct umac_data *umacd,
                                                     struct frame_data_deauth *params);


void umac_connection_handle_ack_status(struct mmpkt *mmpkt, struct umac_data *umacd, bool acked);


void umac_connection_handle_beacon_loss(struct umac_data *umacd);


void umac_connection_set_monitor_disable(struct umac_data *umacd, bool disable);


void umac_connection_set_signal_monitor(struct umac_data *umacd,
                                        int16_t threshold,
                                        int16_t hysteresis);


enum umac_connection_signal_change umac_connection_check_signal_change(struct umac_data *umacd,
                                                                       int16_t rssi);


void umac_connection_set_sta_autoconnect(struct umac_data *umacd,
                                         enum mmwlan_sta_autoconnect_mode mode);


enum mmwlan_status umac_connection_update_beacon_vendor_ie_filter(
    struct umac_data *umacd,
    const struct mmwlan_beacon_vendor_ie_filter *filter);


void umac_connection_process_beacon_ies(struct umac_data *umacd,
                                        const uint8_t *ies,
                                        uint32_t ies_len);


void umac_connection_populate_tx_metadata(struct umac_data *umacd,
                                          struct mmdrv_tx_metadata *tx_metadata);


void umac_connection_signal_sta_event(struct umac_data *umacd, enum mmwlan_sta_event event);


struct umac_sta_data *umac_connection_get_stad(struct umac_data *umacd);


uint16_t umac_connection_get_vif_id(struct umac_data *umacd);


