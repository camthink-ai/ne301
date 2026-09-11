/*
 * Copyright 2025 Morse Micro
 */



#pragma once

#include "dot11/dot11.h"
#include "dot11/dot11_ies.h"
#include "umac/ies/ies_common.h"


static inline const struct dot11_ie_s1g_beacon_compatibility *ie_s1g_beacon_compat_find(
    const uint8_t *ies,
    size_t ies_len)
{
    return (const struct dot11_ie_s1g_beacon_compatibility *)ie_find_and_validate_length(
        ies,
        ies_len,
        DOT11_IE_S1G_BEACON_COMPATIBILITY,
        sizeof(struct dot11_ie_s1g_beacon_compatibility) - sizeof(struct dot11_ie_hdr),
        NULL);
}


