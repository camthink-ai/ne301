/*
 * Copyright 2021-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/ies/ies_common.h"


static inline const struct dot11_ie_aid_response *ie_aid_response_find(const uint8_t *ies,
                                                                       size_t ies_len)
{
    return (const struct dot11_ie_aid_response *)ie_find_and_validate_length(
        ies,
        ies_len,
        DOT11_IE_AID_RESPONSE,
        sizeof(struct dot11_ie_aid_response) - sizeof(struct dot11_ie_hdr),
        NULL);
}


