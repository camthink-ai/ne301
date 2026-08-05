/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "mac_address.h"

const uint8_t mac_addr_zero[6] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const uint8_t mac_addr_broadcast[6] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
