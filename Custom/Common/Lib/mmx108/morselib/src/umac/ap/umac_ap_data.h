/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_ap.h"
#include "mmdrv.h"
#include "umac/ap/traffic_bitmap.h"

struct umac_ap_data
{
    struct mmwlan_ap_args args;
    struct umac_ap_config config;

    const struct mmwlan_s1g_channel *specified_chan;
    uint16_t vif_id;
    uint8_t dtim_count;
    struct umac_sta_data *sta_common;

    uint32_t max_stas;

    struct umac_sta_data **stas;
    uint8_t bitmap[S1G_BITMAP_SUBBLOCKS];

    uint32_t num_pkts_queued;
};

MM_STATIC_ASSERT(MM_MEMBER_SIZE(struct umac_ap_data, bitmap) * 8 >= MAX_SUPPORTED_AID,
                 "Not enough bits");
MM_STATIC_ASSERT(MMWLAN_AP_MAX_STAS_LIMIT < MAX_SUPPORTED_AID, "Unable to support that many STAs");

struct umac_ap_sta_data
{

    enum morse_sta_state sta_state;

    bool asleep;
};
