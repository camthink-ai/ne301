/*
 * Copyright 2023-2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include "mmwlan.h"
#include "umac/ies/ies_common.h"


struct vendor_ie_list_item
{

    struct vendor_ie_list_item *next;

    uint8_t mgmt_type_mask;

    uint8_t data_len;

    uint8_t element[];
};


struct vendor_ies
{

    struct vendor_ie_list_item *head;
};


enum mmwlan_status vendor_ie_list_add(struct vendor_ies *vendor_ies,
                                      const struct mmwlan_vendor_ie *ie);


void vendor_ie_list_clear(struct vendor_ies *vendor_ies, uint8_t mgmt_type_mask);


void vendor_ies_deinit(struct vendor_ies *vendor_ies);


void vendor_ie_list_emit(struct vendor_ies *vendor_ies,
                         struct consbuf *buf,
                         uint8_t mgmt_type_mask);


const uint8_t *ie_vendor_specific_find(const uint8_t *ies,
                                       size_t ies_len,
                                       const uint8_t *id,
                                       size_t id_len,
                                       enum ie_result *result);


void ie_vendor_specific_build(struct consbuf *buf,
                              const uint8_t oui[3],
                              const uint8_t *data,
                              uint8_t data_len);


