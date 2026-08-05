/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/consbuf.h"
#include "umac/data/umac_data.h"
#include "umac/ies/ies_common.h"


struct ie_s1g_operation
{

    uint8_t primary_channel_width_mhz;

    uint8_t operation_channel_width_mhz;

    uint8_t primary_1mhz_channel_loc;

    uint8_t recommend_no_mcs10;

    uint8_t operating_class;

    uint8_t primary_channel_number;

    uint8_t operating_channel_index;
};


static inline const struct dot11_ie_s1g_operation *ie_s1g_operation_find(const uint8_t *ies,
                                                                         size_t ies_len)
{
    return IE_FIND_AND_CAST(ies,
                            ies_len,
                            DOT11_IE_S1G_OPERATION,
                            NULL,
                            struct dot11_ie_s1g_operation);
}


bool ie_s1g_operation_parse(const struct dot11_ie_s1g_operation *raw_ie,
                            struct ie_s1g_operation *result);


static inline uint8_t ie_s1g_operation_get_operating_bw(const struct dot11_ie_s1g_operation *raw_ie)
{
    return dot11_s1g_op_chan_width_get_op_chan_width(raw_ie->channel_width) + 1;
}


bool ie_s1g_operation_is_equal(const struct ie_s1g_operation *s1g_op_a,
                               const struct ie_s1g_operation *s1g_op_b);


