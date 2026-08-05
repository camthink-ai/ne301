/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "mmlog.h"
#include "umac/umac.h"
#include "umac/config/umac_config.h"


const char *umac_regdb_get_country_code(struct umac_data *umacd);


bool umac_regdb_op_class_match(struct umac_data *umacd,
                               uint8_t op_class,
                               const struct mmwlan_s1g_channel *chan);


const struct mmwlan_s1g_channel *umac_regdb_get_channel(struct umac_data *umacd,
                                                        uint8_t s1g_chan_num);


const struct mmwlan_s1g_channel *umac_regdb_get_channel_from_freq_and_bw(struct umac_data *umacd,
                                                                         uint32_t centre_freq_hz,
                                                                         uint8_t bw_mask);


static inline unsigned umac_regdb_get_num_channels(struct umac_data *umacd)
{
    const struct mmwlan_s1g_channel_list *channel_list = umac_config_get_channel_list(umacd);
    MMOSAL_ASSERT(channel_list != NULL);
    return channel_list->num_channels;
}


static inline const struct mmwlan_s1g_channel *umac_regdb_get_channel_at_index(
    struct umac_data *umacd,
    unsigned idx)
{
    const struct mmwlan_s1g_channel_list *channel_list = umac_config_get_channel_list(umacd);
    MMOSAL_ASSERT(channel_list != NULL);
    if (idx >= channel_list->num_channels)
    {
        return NULL;
    }
    return &channel_list->channels[idx];
}


