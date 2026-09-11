/*
 * 802.11 Utilities
 *
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "dot11_utils.h"

void dot11_build_pv0_mgmt_header(struct dot11_hdr *hdr,
                                 uint16_t subtype,
                                 uint8_t to_ds,
                                 const uint8_t *dst_address,
                                 const uint8_t *src_address,
                                 const uint8_t *bssid)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->frame_control = htole16((DOT11_FC_PV0 << DOT11_SHIFT_FC_PROTOCOL_VERSION) |
                                 (DOT11_FC_TYPE_MGMT << DOT11_SHIFT_FC_TYPE) |
                                 (subtype << DOT11_SHIFT_FC_SUBTYPE) |
                                 (to_ds << DOT11_SHIFT_FC_TO_DS));
    memcpy(hdr->addr1, dst_address, sizeof(hdr->addr1));
    memcpy(hdr->addr2, src_address, sizeof(hdr->addr2));
    memcpy(hdr->addr3, bssid, sizeof(hdr->addr3));
}
