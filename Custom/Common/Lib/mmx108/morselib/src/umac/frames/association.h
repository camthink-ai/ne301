/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "frames_common.h"


struct frame_data_assoc_req
{

    const uint8_t *bssid;

    const uint8_t *prev_bssid;

    const uint8_t *sta_address;

    uint8_t ssid_len;

    const uint8_t *ssid;

    uint8_t wpa_ie_len;

    const uint8_t *wpa_ie;

    uint16_t extra_assoc_ies_len;

    const uint8_t *extra_assoc_ies;
};


struct frame_data_assoc_rsp
{

    bool is_reassoc_rsp;

    const uint8_t *bssid;

    uint16_t capability;

    uint16_t status_code;

    uint16_t ies_len;

    const uint8_t *ies;
};


void frame_association_request_build(struct umac_data *umacd, struct consbuf *buf, void *args);


bool frame_association_response_parse(struct mmpktview *view, struct frame_data_assoc_rsp *result);


