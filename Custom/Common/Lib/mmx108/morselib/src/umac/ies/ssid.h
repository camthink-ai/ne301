/*
 * Copyright 2021-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/ies/ies_common.h"


static inline const struct dot11_ie_ssid *ie_ssid_find(const uint8_t *ies, size_t ies_len)
{
    return (const struct dot11_ie_ssid *)ie_find(ies, ies_len, DOT11_IE_SSID, NULL);
}


static inline void ie_ssid_build(struct consbuf *buf, const uint8_t *ssid, unsigned ssid_len)
{
    MMOSAL_DEV_ASSERT(ssid_len <= DOT11_SSID_MAXLEN);
    ie_build_hdr(buf, DOT11_IE_SSID, ssid_len);
    consbuf_append(buf, ssid, ssid_len);
}


