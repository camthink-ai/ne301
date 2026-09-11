/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/ies/ies_common.h"


const struct dot11_ie_wmm_param *ie_wmm_param_find(const uint8_t *ies, size_t ies_len);


void ie_wmm_info_build(struct consbuf *buf);


