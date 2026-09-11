/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "dot11/dot11.h"
#include "dot11/dot11_ies.h"
#include "umac/ies/ies_common.h"


static inline const struct dot11_ie_ecsa *ie_ecsa_find(const uint8_t *ies, size_t ies_len)
{
    return (const struct dot11_ie_ecsa *)ie_find_and_validate_length(
        ies,
        ies_len,
        DOT11_IE_ECSA,
        sizeof(struct dot11_ie_ecsa) - sizeof(struct dot11_ie_hdr),
        NULL);
}


static inline const struct dot11_ie_channel_switch_wrapper *ie_chan_switch_wrapper_find(
    const uint8_t *ies,
    size_t ies_len)
{
    return (const struct dot11_ie_channel_switch_wrapper *)
        ie_find(ies, ies_len, DOT11_IE_CHANNEL_SWITCH_WRAPPER, NULL);
}


const struct dot11_ie_wide_bw_chan_switch *ie_wide_bw_chan_switch_find(const uint8_t *ies,
                                                                       size_t ies_len)
{
    return (const struct dot11_ie_wide_bw_chan_switch *)ie_find_and_validate_length(
        ies,
        ies_len,
        DOT11_IE_WIDE_BW_CHAN_SWITCH,
        sizeof(struct dot11_ie_wide_bw_chan_switch) - sizeof(struct dot11_ie_hdr),
        NULL);
}


