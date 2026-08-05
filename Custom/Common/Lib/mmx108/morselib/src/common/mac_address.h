/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "common/common.h"
#include "dot11/dot11.h"


extern const uint8_t mac_addr_zero[6];


extern const uint8_t mac_addr_broadcast[6];


static inline void mac_addr_copy(uint8_t *dest, const uint8_t *src)
{
    memcpy(dest, src, DOT11_MAC_ADDR_LEN);
}


