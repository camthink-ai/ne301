/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "frames_common.h"
#include "dot11/dot11_frames.h"


struct frame_data_deauth
{

    const uint8_t *bssid;

    const uint8_t *sta_address;

    uint16_t reason_code;
};


void frame_deauthentication_build(struct umac_data *umacd, struct consbuf *buf, void *args);


struct frame_data_deauth_ap
{

    const uint8_t *own_address;

    const uint8_t *sta_address;

    uint16_t reason_code;

    struct umac_sta_data *bip_stad;
};


void frame_deauthentication_from_ap_build(struct umac_data *umacd, struct consbuf *buf, void *args);


bool frame_is_deauthentication(const struct dot11_hdr *header);


