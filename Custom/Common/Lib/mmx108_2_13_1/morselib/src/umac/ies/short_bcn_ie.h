/*
 * Copyright 2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/consbuf.h"
#include "umac/data/umac_data.h"
#include "umac/ies/ies_common.h"
#include "mmlog.h"


#define SHORT_BCN_IE_LENGTH (2)


static inline const struct dot11_ie_short_bcn_int *ie_short_bcn_find(const uint8_t *ies,
                                                                     size_t ies_len)
{
    const struct dot11_ie_short_bcn_int *short_bcn =
        IE_FIND_AND_CAST(ies, ies_len, DOT11_IE_SHORT_BCN_INT, NULL, struct dot11_ie_short_bcn_int);
    if ((short_bcn != NULL) && (short_bcn->header.length == SHORT_BCN_IE_LENGTH))
    {
        return short_bcn;
    }

    return NULL;
}


