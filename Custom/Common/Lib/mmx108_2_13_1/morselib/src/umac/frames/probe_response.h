/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "frames_common.h"
#include "dot11/dot11_frames.h"


struct frame_data_probe_response
{

    const uint8_t *destination_address;

    const uint8_t *timestamp;

    const uint8_t *bssid;

    const uint8_t *ssid;

    const uint8_t *ies;

    uint16_t beacon_interval;

    uint16_t capability_info;

    uint16_t ies_len;

    uint8_t ssid_len;
};


bool frame_probe_response_parse(struct mmpktview *view, struct frame_data_probe_response *result);


void frame_probe_response_build(struct umac_data *umacd, struct consbuf *buf, void *args);


