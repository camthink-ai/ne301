/*
 * Copyright 2021-2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "ies_common.h"
#include "mmlog.h"


#define IE_SIZEOF_HEADER 2


#define IE_ID(p) ((p)[0])

#define IE_LEN(p) ((p)[1])

#define IE_INFO(p) ((p) + IE_SIZEOF_HEADER)

#define IE_NEXT(p) (IE_INFO(p) + IE_LEN(p))

#define IE_VALID(p, end) ((IE_INFO(p) <= (end)) && (IE_NEXT(p) <= (end)))

#define IE_TOTAL_LEN(p) (IE_SIZEOF_HEADER + IE_LEN(p))

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

const uint8_t *ie_vendor_specific_find(const uint8_t *ies,
                                       size_t ies_len,
                                       const uint8_t *id,
                                       size_t id_len,
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

        if (IE_ID(curr) == DOT11_IE_VENDOR_SPECIFIC &&
            id_len <= IE_LEN(curr) &&
            (memcmp(id, IE_INFO(curr), id_len) == 0))
        {
            res = IE_FOUND;
            break;
        }
        curr = IE_NEXT(curr);
    }

    if (result)
    {
        *result = res;
    }

    return res == IE_FOUND ? curr : NULL;
}
