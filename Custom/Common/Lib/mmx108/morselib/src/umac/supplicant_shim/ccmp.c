/*
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "umac/keys/umac_keys.h"
#include "dot11/dot11.h"


#define CCMP_HEADER_KEY_OCT_KEY_ID 0xC0


static uint8_t parse_ccmp_key_id(uint8_t *ccmp_header)
{
    return (ccmp_header[3] & CCMP_HEADER_KEY_OCT_KEY_ID) >> 6;
}


static uint64_t parse_ccmp_packet_number(const uint8_t *header)
{
    return (((uint64_t)(*(header)) << 0)) |
           (((uint64_t)(*(header + 1)) << 8)) |
           (((uint64_t)(*(header + 4)) << 16)) |
           (((uint64_t)(*(header + 5)) << 24)) |
           (((uint64_t)(*(header + 6)) << 32)) |
           (((uint64_t)(*(header + 7)) << 40));
}

bool ccmp_is_valid(struct umac_sta_data *stad,
                   uint8_t *ccmp_header,
                   enum umac_key_rx_counter_space space)
{
    if (ccmp_header == NULL || stad == NULL)
    {
        return false;
    }

    uint8_t key_id = parse_ccmp_key_id(ccmp_header);


    if (umac_keys_get_key_type(stad, key_id) == UMAC_KEY_TYPE_BLANK)
    {
        return false;
    }

    uint64_t packet_number = parse_ccmp_packet_number(ccmp_header);
    enum mmwlan_status status =
        umac_keys_check_and_update_rx_replay(stad, key_id, packet_number, space);

    return (status == MMWLAN_SUCCESS);
}
