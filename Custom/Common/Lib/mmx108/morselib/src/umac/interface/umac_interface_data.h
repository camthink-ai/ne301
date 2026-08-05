/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_interface.h"
#include "dot11/dot11.h"

struct umac_interface_vif_data
{

    mmwlan_vif_state_cb_t vif_state_cb;

    void *vif_state_cb_arg;


    mmwlan_rx_pkt_ext_cb_t rx_pkt_ext_cb;

    void *rx_pkt_ext_cb_arg;
};

struct umac_interface_data
{

    uint16_t active_interface_types;

    uint16_t vif_id;

    uint8_t mac_addr[DOT11_MAC_ADDR_LEN];

    struct mmdrv_fw_version fw_version;

    uint32_t morse_chip_id;

    const char *morse_chip_id_string;

    struct morse_caps capabilities;

    struct ie_s1g_operation current_s1g_operation;

    umac_interface_inactive_cb_t inactive_callback;

    void *inactive_cb_arg;
};
