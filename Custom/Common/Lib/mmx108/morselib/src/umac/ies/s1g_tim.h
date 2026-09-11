/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac/ies/ies_common.h"

#define S1G_TIM_MAX_BLOCK_SIZE 256


const struct dot11_ie_tim *ie_s1g_tim_find(const uint8_t *ies, size_t ies_len);


bool ie_s1g_tim_has_aid(const struct dot11_ie_tim *s1g_tim, uint16_t aid);


void ie_s1g_tim_build(struct consbuf *buf,
                      uint8_t dtim_count,
                      uint8_t dtim_period,
                      bool traffic_indicator,
                      const uint8_t *traffic_bitmap);
