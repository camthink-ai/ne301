/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/ies/ies_common.h"
#include "umac/data/umac_data.h"


#define MORSE_VENDOR_IE_OPS0_DTIM_CTS_TO_SELF BIT(0)

#define MORSE_VENDOR_IE_OPS0_LEGACY_AMSDU BIT(1)


#define MORSE_VENDOR_IE_CAP0_MMSS_MASK (0x03)

#define MORSE_VENDOR_IE_CAP0_SHORT_ACK_TIMEOUT (0x04)


#define MORSE_VENDOR_IE_CAP0_SET_MMSS_OFFSET(x) ((x) & MORSE_VENDOR_IE_CAP0_MMSS_MASK)

#define MORSE_VENDOR_IE_CAP0_GET_MMSS_OFFSET(x) ((x) & MORSE_VENDOR_IE_CAP0_MMSS_MASK)


const struct dot11_ie_morse_info *ie_morse_info_find(const uint8_t *ies, size_t ies_len);


void ie_morse_info_build(struct umac_data *umacd, struct consbuf *buf);


