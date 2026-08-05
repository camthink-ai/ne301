/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "s1g_operation.h"

#include "mmlog.h"

bool ie_s1g_operation_parse(const struct dot11_ie_s1g_operation *raw_ie,
                            struct ie_s1g_operation *result)
{

    result->operation_channel_width_mhz = ie_s1g_operation_get_operating_bw(raw_ie);
    if (!(raw_ie->channel_width & DOT11_MASK_S1G_OP_CHAN_WIDTH_PRI_CHAN_WIDTH))
    {
        result->primary_channel_width_mhz = 2;
    }
    else
    {
        result->primary_channel_width_mhz = 1;
    }

    if (result->operation_channel_width_mhz < result->primary_channel_width_mhz)
    {
        MMLOG_INF("Malformed S1G Operation IE (op width < pri width)\n");
        return false;
    }

    result->primary_1mhz_channel_loc =
        dot11_s1g_op_chan_width_get_pri_chan_loc(raw_ie->channel_width);
    result->recommend_no_mcs10 = dot11_s1g_op_chan_width_get_no_mcs10(raw_ie->channel_width);

    result->operating_class = raw_ie->operating_class;
    result->primary_channel_number = raw_ie->primary_channel_number;
    result->operating_channel_index = raw_ie->channel_center_freq;
    return true;
}

bool ie_s1g_operation_is_equal(const struct ie_s1g_operation *s1g_op_a,
                               const struct ie_s1g_operation *s1g_op_b)
{
    return (s1g_op_a->primary_channel_width_mhz == s1g_op_b->primary_channel_width_mhz) &&
           (s1g_op_a->operation_channel_width_mhz == s1g_op_b->operation_channel_width_mhz) &&
           (s1g_op_a->primary_1mhz_channel_loc == s1g_op_b->primary_1mhz_channel_loc) &&
           (s1g_op_a->recommend_no_mcs10 == s1g_op_b->recommend_no_mcs10) &&
           (s1g_op_a->operating_class == s1g_op_b->operating_class) &&
           (s1g_op_a->primary_channel_number == s1g_op_b->primary_channel_number) &&
           (s1g_op_a->operating_channel_index == s1g_op_b->operating_channel_index);
}
