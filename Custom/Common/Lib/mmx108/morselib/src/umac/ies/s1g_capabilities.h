/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/consbuf.h"
#include "umac/data/umac_data.h"
#include "umac/ies/ies_common.h"


static inline const struct dot11_ie_s1g_capabilities *ie_s1g_capabilities_find(const uint8_t *ies,
                                                                               size_t ies_len)
{
    return IE_FIND_AND_CAST(ies,
                            ies_len,
                            DOT11_IE_S1G_CAPABILITIES,
                            NULL,
                            struct dot11_ie_s1g_capabilities);
}


uint8_t ie_s1g_capabilities_get_sta_type(const struct dot11_ie_s1g_capabilities *s1g_caps);


bool ie_s1g_capabilities_is_ampdu_support_enabled(const struct dot11_ie_s1g_capabilities *s1g_caps);


bool ie_s1g_capabilities_is_ctrl_resp_1mhz_enabled(
    const struct dot11_ie_s1g_capabilities *s1g_caps);


uint8_t ie_s1g_capabilities_get_sgi_flags(const struct dot11_ie_s1g_capabilities *s1g_caps);


uint8_t ie_s1g_capabilities_get_max_rx_mcs_for_1ss(
    const struct dot11_ie_s1g_capabilities *s1g_caps);


void ie_s1g_capabilities_build(struct umac_data *umacd, struct consbuf *buf);


void ie_s1g_capabilities_build_ap(struct umac_data *umacd, struct consbuf *buf);


bool ie_s1g_capabilities_get_traveling_pilots_support(
    const struct dot11_ie_s1g_capabilities *s1g_caps);


