/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "common/common.h"


#define MAX_SUPPORTED_AID    64
#define S1G_BITMAP_SUBBLOCKS ((MAX_SUPPORTED_AID + 7) / 8)
MM_STATIC_ASSERT((MAX_SUPPORTED_AID >> 3) == S1G_BITMAP_SUBBLOCKS, "");


static bool aid_is_valid(uint16_t aid)
{
    return aid && aid < MAX_SUPPORTED_AID;
}


static inline void ap_traffic_bitmap_set_aid_bit(uint8_t *bitmap, uint16_t aid)
{
    MMOSAL_ASSERT(aid_is_valid(aid));
    uint8_t aid_octet = (aid >> 3);
    uint8_t aid_bit = BIT(aid & 0x07);
    MMOSAL_TASK_ENTER_CRITICAL();
    bitmap[aid_octet] |= aid_bit;
    MMOSAL_TASK_EXIT_CRITICAL();
}


static inline void ap_traffic_bitmap_clear_aid_bit(uint8_t *bitmap, uint16_t aid)
{
    MMOSAL_ASSERT(aid_is_valid(aid));
    uint8_t aid_octet = (aid >> 3);
    uint8_t aid_bit = BIT(aid & 0x07);
    MMOSAL_TASK_ENTER_CRITICAL();
    bitmap[aid_octet] &= ~aid_bit;
    MMOSAL_TASK_EXIT_CRITICAL();
}


static inline bool ap_traffic_bitmap_get_aid_bit(const uint8_t *bitmap, uint16_t aid)
{
    MMOSAL_ASSERT(aid_is_valid(aid));
    uint8_t aid_octet = (aid >> 3);
    uint8_t aid_bit = BIT(aid & 0x07);
    return bitmap[aid_octet] & aid_bit;
}
