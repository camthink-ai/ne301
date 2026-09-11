/*
 * Copyright 2021-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "ies_common.h"
#include "mmlog.h"

const uint8_t *ie_find_and_validate_length(const uint8_t *ies,
                                           size_t ies_len,
                                           uint8_t ie_id,
                                           size_t expected_length,
                                           enum ie_result *result)
{
    const uint8_t *curr = ies;
    const uint8_t *end = ies + ies_len;

    enum ie_result res = IE_NOT_FOUND;

    while (curr < end)
    {
        if (!IE_VALID(curr, end))
        {
            res = IES_INVALID;
            break;
        }
        if (IE_ID(curr) == ie_id)
        {
            res = IE_FOUND;
            break;
        }
        curr = IE_NEXT(curr);
    }

    if (expected_length != 0 && expected_length != IE_LEN(curr))
    {
        MMLOG_VRB("IE 0x%02x length mismatch (expect %u, got %u)\n",
                  ie_id,
                  expected_length,
                  IE_LEN(curr));
        curr = NULL;
        res = IE_WRONG_LEN;
    }

    if (result)
    {
        *result = res;
    }

    return res == IE_FOUND ? curr : NULL;
}
