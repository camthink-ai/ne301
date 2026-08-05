/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "frames_common.h"
#include "dot11/dot11_frames.h"


struct frame_data_action
{

    const uint8_t *bssid;

    const uint8_t *dst_address;

    const uint8_t *src_address;

    const uint8_t *action_field;

    uint32_t action_field_len;
};


bool frame_is_robust_action(struct mmpktview *view);


void frame_action_build(struct umac_data *umacd, struct consbuf *buf, void *args);
