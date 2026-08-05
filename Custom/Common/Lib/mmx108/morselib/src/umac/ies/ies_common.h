/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/common.h"
#include "common/consbuf.h"
#include "dot11/dot11.h"
#include "dot11/dot11_ies.h"


enum ie_result
{
    IE_FOUND,
    IE_NOT_FOUND,
    IES_INVALID,
    IE_WRONG_LEN,
};


const uint8_t *ie_find_and_validate_length(const uint8_t *ies,
                                           size_t ies_len,
                                           uint8_t ie_id,
                                           size_t length,
                                           enum ie_result *result);


static inline const uint8_t *ie_find(const uint8_t *ies,
                                     size_t ies_len,
                                     uint8_t ie_id,
                                     enum ie_result *result)
{
    return ie_find_and_validate_length(ies, ies_len, ie_id, 0, result);
}


#define IE_FIND_AND_CAST(_ies, _ies_len, _ie_id, _result, _type)                             \
    (const _type *)(ie_find_and_validate_length(_ies,                                        \
                                                _ies_len,                                    \
                                                _ie_id,                                      \
                                                sizeof(_type) - sizeof(struct dot11_ie_hdr), \
                                                _result))


const uint8_t *ie_vendor_specific_find(const uint8_t *ies,
                                       size_t ies_len,
                                       const uint8_t *id,
                                       size_t id_len,
                                       enum ie_result *result);


static inline void ie_build_hdr(struct consbuf *cbuf, uint8_t element_id, uint8_t length)
{
    struct dot11_ie_hdr *hdr = (struct dot11_ie_hdr *)consbuf_reserve(cbuf, sizeof(*hdr));
    if (hdr != NULL)
    {
        hdr->element_id = element_id;
        hdr->length = length;
    }
}


