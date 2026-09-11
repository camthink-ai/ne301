/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmdrv.h"
#include "umac/umac.h"
#include "umac/data/umac_data.h"
#include "umac/ies/s1g_operation.h"


enum umac_interface_type
{

    UMAC_INTERFACE_NONE = 1,

    UMAC_INTERFACE_SCAN = 2,

    UMAC_INTERFACE_STA = 4,

    UMAC_INTERFACE_AP = 8,

    UMAC_INTERFACE_ALL =
        (UMAC_INTERFACE_NONE | UMAC_INTERFACE_SCAN | UMAC_INTERFACE_STA | UMAC_INTERFACE_AP),
};


#define UMAC_INTERFACE_VIF_ID_INVALID (0xffff)


void umac_interface_init(struct umac_data *umacd);


enum mmwlan_status umac_interface_add(struct umac_data *umacd,
                                      enum umac_interface_type type,
                                      const uint8_t *mac_addr,
                                      uint16_t *vif_id);


void umac_interface_remove(struct umac_data *umacd, enum umac_interface_type type);


void umac_interface_stop(struct umac_data *umacd);


bool umac_interface_is_active(struct umac_data *umacd);


static inline uint16_t umac_interface_get_vif_id_from_rx_metadata(
    const struct mmdrv_rx_metadata *metadata)
{
    if (metadata == NULL || metadata->vif_id == UINT8_MAX)
    {
        return UINT16_MAX;
    }
    else
    {
        return metadata->vif_id;
    }
}


uint16_t umac_interface_get_vif_id(struct umac_data *umacd, uint16_t type_mask);


uint16_t umac_interface_get_vif_type_mask(struct umac_data *umacd, uint16_t vif_id);


enum mmwlan_status umac_interface_reinstall_vif(struct umac_data *umacd,
                                                enum umac_interface_type type,
                                                uint16_t *vif_id);


enum mmwlan_status umac_interface_get_fw_version(struct umac_data *umacd,
                                                 struct mmdrv_fw_version *version);


uint32_t umac_interface_get_chip_id(struct umac_data *umacd);


const char *umac_interface_get_chip_id_string(struct umac_data *umacd);


enum mmwlan_status umac_interface_get_device_mac_addr(struct umac_data *umacd, uint8_t *mac_addr);


enum mmwlan_status umac_interface_get_vif_mac_addr(struct umac_data *umacd,
                                                   enum mmwlan_vif vif,
                                                   uint8_t *mac_addr);


enum mmwlan_status umac_interface_borrow_vif_mac_addr(struct umac_data *umacd,
                                                      enum mmwlan_vif vif,
                                                      const uint8_t **mac_addr);


enum mmwlan_status umac_interface_set_vif_mac_addr(struct umac_data *umacd,
                                                   enum mmwlan_vif vif,
                                                   const uint8_t *mac_addr);


enum mmwlan_status umac_interface_get_mac_addr(struct umac_sta_data *stad, uint8_t *mac_addr);


const uint8_t *umac_interface_peek_mac_addr(struct umac_sta_data *stad);


bool umac_interface_addr_matches_mac_addr(struct umac_sta_data *stad, const uint8_t *addr);


enum mmwlan_status umac_interface_set_scan(struct umac_data *umacd, bool enabled);


const struct mmwlan_s1g_channel *umac_interface_calc_pri_channel(
    struct umac_data *umacd,
    const struct mmwlan_s1g_channel *operating_chan,
    uint8_t pri_1mhz_chan_idx,
    uint8_t pri_bw_mhz);


enum mmwlan_status umac_interface_set_channel(struct umac_data *umacd,
                                              const struct ie_s1g_operation *s1g_operation);


enum mmwlan_status umac_interface_set_channel_from_regdb(struct umac_data *umacd,
                                                         const struct mmwlan_s1g_channel *channel,
                                                         bool is_off_channel);


const struct ie_s1g_operation *umac_interface_get_current_s1g_operation_info(
    struct umac_data *umacd);


enum mmwlan_status umac_interface_reconfigure_channel(struct umac_data *umacd);


const struct mmwlan_s1g_channel *umac_interface_get_current_channel_regdb_entry(
    struct umac_data *umacd);


const struct morse_caps *umac_interface_get_capabilities(struct umac_data *umacd);


uint8_t umac_interface_max_supported_bw(struct umac_data *umacd);


void umac_interface_configure_periodic_health_check(struct umac_data *umacd);


bool umac_interface_get_control_response_bw_1mhz_out_enabled(struct umac_data *umacd);


enum mmwlan_status umac_interface_set_ndp_probe_support(struct umac_data *umacd, bool enabled);


typedef void (*umac_interface_inactive_cb_t)(void *arg);


void umac_interface_register_inactive_cb(struct umac_data *umacd,
                                         umac_interface_inactive_cb_t callback,
                                         void *arg);


enum mmwlan_status umac_interface_register_vif_state_cb(struct umac_data *umacd,
                                                        enum mmwlan_vif vif,
                                                        mmwlan_vif_state_cb_t callback,
                                                        void *arg);


bool umac_interface_invoke_vif_state_cb(struct umac_data *umacd,
                                        const struct mmwlan_vif_state *state);


enum mmwlan_status umac_interface_register_rx_pkt_ext_cb(struct umac_data *umacd,
                                                         enum mmwlan_vif vif,
                                                         mmwlan_rx_pkt_ext_cb_t callback,
                                                         void *arg);


mmwlan_rx_pkt_ext_cb_t umac_interface_get_rx_pkt_ext_cb(struct umac_data *umacd,
                                                        enum mmwlan_vif vif,
                                                        void **arg);

