/*
 * Copyright 2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_offload.h"

struct umac_offload_data
{

    uint16_t vif_id;

    uint32_t arp_addr;

    uint32_t arp_refresh_interval_s;

    uint32_t arp_refresh_dest_ip;

    bool arp_refresh_garp;

    bool dhcp_offload_enabled;

    mmwlan_dhcp_lease_update_cb_t dhcp_lease_update_cb;

    void *dhcp_lease_update_cb_arg;

    bool standby_mode_enabled;

    struct mmwlan_standby_config standby_config;

    struct mmwlan_standby_enter_args standby_enter_args;

    struct mmwlan_standby_set_status_payload_args standby_set_status_payload;

    struct mmwlan_standby_set_wake_filter_args standby_wake_filter;

    uint8_t tcp_keepalive_enabled;

    struct mmwlan_tcp_keepalive_offload_args tcp_keepalive_args;

    struct mmwlan_config_whitelist whitelist_filter;
};
