/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "umac/ies/ies_common.h"


#define IE_REACHABLE_ADDRESS_FIXED_LEN (7)

#define IE_REACHABLE_ADDRESS_SUBFIELD_LEN (7)


#define IE_REACHABLE_ADDRESS_FLAG_ADD_REMOVE BIT(0)

#define IE_REACHABLE_ADDRESS_FLAG_RELAY_CAPABLE BIT(1)


const struct dot11_ie_reachable_address *ie_reachable_address_find(const uint8_t *ies,
                                                                   size_t ies_len,
                                                                   enum ie_result *result);


bool ie_reachable_address_parse(const struct dot11_ie_reachable_address *raw_ie,
                                const uint8_t **initiator_mac,
                                uint8_t *address_count);


enum reachable_address_update_type
{

    REACHABLE_ADDRESS_REMOVE,

    REACHABLE_ADDRESS_ADD,
};


bool ie_reachable_address_get_update(const struct dot11_ie_reachable_address *raw_ie,
                                     uint8_t index,
                                     enum reachable_address_update_type *update_type,
                                     bool *relay_capable,
                                     const uint8_t **mac);


bool ie_reachable_address_build_single(struct consbuf *buf,
                                       const uint8_t *initiator_mac,
                                       enum reachable_address_update_type update_type,
                                       bool relay_capable,
                                       const uint8_t *mac);

