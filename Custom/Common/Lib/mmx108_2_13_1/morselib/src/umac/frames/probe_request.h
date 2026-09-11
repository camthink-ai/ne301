/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "frames_common.h"
#include "dot11/dot11_frames.h"


struct frame_data_probe_request
{

    const uint8_t *bssid;

    const uint8_t *sta_address;

    const uint8_t *ssid;

    uint8_t ssid_len;

    const uint8_t *extra_ies;

    uint16_t extra_ies_len;
};


void frame_probe_request_build(struct umac_data *umacd, struct consbuf *buf, void *args);


