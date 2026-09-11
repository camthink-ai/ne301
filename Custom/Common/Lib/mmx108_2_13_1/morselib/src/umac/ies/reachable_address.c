/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "reachable_address.h"

#include <string.h>

#include "mmlog.h"

static bool ie_reachable_address_length_valid(const struct dot11_ie_reachable_address *raw_ie)
{
    size_t expected;

    if (raw_ie->header.length < IE_REACHABLE_ADDRESS_FIXED_LEN)
    {
        return false;
    }

    expected = IE_REACHABLE_ADDRESS_FIXED_LEN +
               ((size_t)raw_ie->address_count * IE_REACHABLE_ADDRESS_SUBFIELD_LEN);
    return expected == raw_ie->header.length;
}

const struct dot11_ie_reachable_address *ie_reachable_address_find(const uint8_t *ies,
                                                                   size_t ies_len,
                                                                   enum ie_result *result)
{
    const struct dot11_ie_reachable_address *ie = (const struct dot11_ie_reachable_address *)
        ie_find(ies, ies_len, DOT11_IE_REACHABLE_ADDRESS, result);
    if (ie == NULL)
    {
        return NULL;
    }

    if (!ie_reachable_address_length_valid(ie))
    {
        if (result)
        {
            *result = IE_WRONG_LEN;
        }
        MMLOG_DBG("Reachable Address IE length mismatch\n");
        return NULL;
    }

    if (result)
    {
        *result = IE_FOUND;
    }

    return ie;
}

bool ie_reachable_address_parse(const struct dot11_ie_reachable_address *raw_ie,
                                const uint8_t **initiator_mac,
                                uint8_t *address_count)
{
    if (raw_ie == NULL || initiator_mac == NULL || address_count == NULL)
    {
        return false;
    }

    if (!ie_reachable_address_length_valid(raw_ie))
    {
        MMLOG_DBG("Reachable Address IE length mismatch\n");
        return false;
    }

    *initiator_mac = raw_ie->initiator_mac;
    *address_count = raw_ie->address_count;
    return true;
}

bool ie_reachable_address_get_update(const struct dot11_ie_reachable_address *raw_ie,
                                     uint8_t index,
                                     enum reachable_address_update_type *update_type,
                                     bool *relay_capable,
                                     const uint8_t **mac)
{
    const struct dot11_reachable_address_subfield *subfield;

    if (raw_ie == NULL || update_type == NULL || relay_capable == NULL || mac == NULL)
    {
        return false;
    }

    if (!ie_reachable_address_length_valid(raw_ie))
    {
        MMLOG_DBG("Reachable Address IE length mismatch\n");
        return false;
    }

    if (index >= raw_ie->address_count)
    {
        return false;
    }

    subfield =
        (const struct dot11_reachable_address_subfield *)(raw_ie->reachable_addresses +
                                                          (index *
                                                           IE_REACHABLE_ADDRESS_SUBFIELD_LEN));

    if (subfield->flags & IE_REACHABLE_ADDRESS_FLAG_ADD_REMOVE)
    {
        *update_type = REACHABLE_ADDRESS_ADD;
    }
    else
    {
        *update_type = REACHABLE_ADDRESS_REMOVE;
    }
    *relay_capable = (subfield->flags & IE_REACHABLE_ADDRESS_FLAG_RELAY_CAPABLE) != 0;
    *mac = subfield->mac;
    return true;
}

bool ie_reachable_address_build_single(struct consbuf *buf,
                                       const uint8_t *initiator_mac,
                                       enum reachable_address_update_type update_type,
                                       bool relay_capable,
                                       const uint8_t *mac)
{
    struct dot11_ie_reachable_address *ie;
    struct dot11_reachable_address_subfield *subfield;
    uint8_t flags = 0;
    size_t total_len = IE_REACHABLE_ADDRESS_FIXED_LEN + IE_REACHABLE_ADDRESS_SUBFIELD_LEN;
    size_t reserve_len = sizeof(*ie) + IE_REACHABLE_ADDRESS_SUBFIELD_LEN;

    if (buf == NULL || initiator_mac == NULL || mac == NULL || buf->buf == NULL)
    {
        return false;
    }

    if (buf->buf_size < buf->offset || buf->buf_size - buf->offset < reserve_len)
    {
        return false;
    }

    ie = (struct dot11_ie_reachable_address *)consbuf_reserve(buf, reserve_len);
    if (ie == NULL)
    {
        return false;
    }


    if (update_type == REACHABLE_ADDRESS_ADD)
    {
        flags |= IE_REACHABLE_ADDRESS_FLAG_ADD_REMOVE;
    }

    if (relay_capable)
    {
        flags |= IE_REACHABLE_ADDRESS_FLAG_RELAY_CAPABLE;
    }

    ie->header.element_id = DOT11_IE_REACHABLE_ADDRESS;
    ie->header.length = (uint8_t)total_len;
    memcpy(ie->initiator_mac, initiator_mac, DOT11_MAC_ADDR_LEN);
    ie->address_count = 1;

    subfield = (struct dot11_reachable_address_subfield *)ie->reachable_addresses;
    subfield->flags = flags;
    memcpy(subfield->mac, mac, DOT11_MAC_ADDR_LEN);
    return true;
}
