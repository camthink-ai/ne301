/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "frames_common.h"


struct frame_data_auth
{

    uint16_t auth_alg;

    const uint8_t *bssid;

    const uint8_t *sta_address;

    uint16_t seq;

    uint16_t status_code;

    uint16_t auth_data_len;

    const uint8_t *auth_data;
};


void frame_authentication_build(struct umac_data *umacd, struct consbuf *buf, void *args);


bool frame_authentication_parse(struct mmpktview *view, struct frame_data_auth *result);


int32_t frame_authentication_get_seq_num(const struct frame_data_auth *auth, bool is_tx);


