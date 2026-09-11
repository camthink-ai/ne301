/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/consbuf.h"
#include "umac/data/umac_data.h"
#include "umac/ies/ies_common.h"
#include "mmlog.h"


#define TWT_IE_MIN_LENGTH (10)

#define TWT_IE_MAX_LENGTH (20)


static inline const struct dot11_ie_twt *ie_twt_find(const uint8_t *ies, size_t ies_len)
{
    const struct dot11_ie_twt *twt =
        IE_FIND_AND_CAST(ies, ies_len, DOT11_IE_TWT, NULL, struct dot11_ie_twt);
    if ((twt != NULL) &&
        (twt->header.length > TWT_IE_MIN_LENGTH) &&
        (twt->header.length < TWT_IE_MAX_LENGTH))
    {
        return twt;
    }

    MMLOG_DBG("Invalid TWT IE length\n");
    return NULL;
}


void ie_twt_build(struct umac_data *umacd, struct consbuf *buf);


