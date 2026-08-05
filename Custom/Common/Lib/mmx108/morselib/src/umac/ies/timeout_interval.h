/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/ies/ies_common.h"


static inline const struct dot11_ie_tie *ie_timeout_interval_find(const uint8_t *ies,
                                                                  size_t ies_len)
{
    return (const struct dot11_ie_tie *)ie_find_and_validate_length(
        ies,
        ies_len,
        DOT11_IE_TIE,
        sizeof(struct dot11_ie_tie) - sizeof(struct dot11_ie_hdr),
        NULL);
}


