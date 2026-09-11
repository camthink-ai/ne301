/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "frames_common.h"
#include "dot11/dot11_frames.h"


struct frame_data_s1g_action
{

    const uint8_t *bssid;

    const uint8_t *dst_address;

    const uint8_t *src_address;

    uint8_t s1g_action;

    const uint8_t *payload;

    uint32_t payload_len;
};


void frame_s1g_action_build(struct umac_data *umacd, struct consbuf *buf, void *args);


bool frame_s1g_action_parse(struct mmpktview *view, struct frame_data_s1g_action *result);
