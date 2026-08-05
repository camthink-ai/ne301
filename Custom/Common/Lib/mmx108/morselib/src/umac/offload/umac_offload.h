/*
 * Copyright 2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "umac/umac.h"
#include "umac/data/umac_data.h"


void umac_offload_init(struct umac_data *umacd, uint16_t vif_id);


void umac_offload_set_arp_response_offload(struct umac_data *umacd, uint32_t arp_addr);


void umac_offload_set_arp_refresh(struct umac_data *umacd,
                                  uint32_t interval_s,
                                  uint32_t dest_ip,
                                  bool send_as_garp);


void umac_offload_dhcp_enable(struct umac_data *umacd,
                              mmwlan_dhcp_lease_update_cb_t dhcp_lease_updated_cb,
                              void *arg);


void umac_offload_dhcp_lease_update(struct umac_data *umacd,
                                    const struct mmwlan_dhcp_lease_info *lease_info);


void umac_offload_config_tcp_keepalive(struct umac_data *umacd,
                                       const struct mmwlan_tcp_keepalive_offload_args *args);


enum mmwlan_status umac_offload_set_whitelist_filter(
    struct umac_data *umacd,
    const struct mmwlan_config_whitelist *whitelist);


enum mmwlan_status umac_offload_standby_enter(
    struct umac_data *umacd,
    const struct mmwlan_standby_enter_args *standby_enter_args);


enum mmwlan_status umac_offload_standby_exit(struct umac_data *umacd);


enum mmwlan_status umac_offload_standby_set_status_payload(
    struct umac_data *umacd,
    const struct mmwlan_standby_set_status_payload_args *standby_set_status_payload_args);


enum mmwlan_status umac_offload_standby_set_wake_filter(
    struct umac_data *umacd,
    const struct mmwlan_standby_set_wake_filter_args *standby_set_wake_filter_args);


enum mmwlan_status umac_offload_standby_set_config(struct umac_data *umacd,
                                                   const struct mmwlan_standby_config *config);


void umac_offload_restore_all(struct umac_data *umacd);


