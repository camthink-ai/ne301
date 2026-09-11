/*
 * Copyright 2023-2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stddef.h>
#include <stdio.h>

#include "vendor_ie.h"
#include "mmlog.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "umac/ies/ies_common.h"

const uint8_t *ie_vendor_specific_find(const uint8_t *ies,
                                       size_t ies_len,
                                       const uint8_t *id,
                                       size_t id_len,
                                       enum ie_result *result)
{
    const uint8_t *curr = ies;
    const uint8_t *end = ies + ies_len;

    enum ie_result res = IE_NOT_FOUND;

    while (curr < end)
    {
        if (!IE_VALID(curr, end))
        {
            res = IES_INVALID;
            break;
        }

        if (IE_ID(curr) == DOT11_IE_VENDOR_SPECIFIC &&
            id_len <= IE_LEN(curr) &&
            (memcmp(id, IE_INFO(curr), id_len) == 0))
        {
            res = IE_FOUND;
            break;
        }
        curr = IE_NEXT(curr);
    }

    if (result)
    {
        *result = res;
    }

    return res == IE_FOUND ? curr : NULL;
}

void ie_vendor_specific_build(struct consbuf *buf,
                              const uint8_t oui[3],
                              const uint8_t *data,
                              uint8_t data_len)
{
    uint16_t ie_size = sizeof(struct dot11_ie_vs_hdr) + data_len;
    struct dot11_ie_vendor_specific *ie =
        (struct dot11_ie_vendor_specific *)consbuf_reserve(buf, ie_size);
    if (ie == NULL)
    {
        return;
    }

    ie->vs_header.header.element_id = DOT11_IE_VENDOR_SPECIFIC;
    ie->vs_header.header.length =
        (sizeof(struct dot11_ie_vs_hdr) - sizeof(struct dot11_ie_hdr)) + data_len;
    memcpy(ie->vs_header.oui, oui, sizeof(ie->vs_header.oui));
    if (data_len > 0)
    {
        memcpy(ie->data, data, data_len);
    }
}

enum mmwlan_status vendor_ie_list_add(struct vendor_ies *vendor_ies,
                                      const struct mmwlan_vendor_ie *ie)
{
    MMOSAL_DEV_ASSERT(vendor_ies != NULL);

    if ((MMWLAN_VENDOR_IE_MGMT_ALL & ie->mgmt_type_mask) == 0)
    {
        return MMWLAN_NOT_SUPPORTED;
    }

    struct vendor_ie_list_item *tail = NULL;

    MM_STATIC_ASSERT(sizeof(ie->mgmt_type_mask) == 1, "Loop assumes mask is 8 bits");
    for (size_t bit = 0; bit < 8; bit++)
    {
        if ((ie->mgmt_type_mask & (1 << bit)) == 0)
        {
            continue;
        }

        struct vendor_ie_list_item **link = &vendor_ies->head;
        tail = NULL;

        size_t size = 0;
        while (*link != NULL)
        {
            tail = *link;

            size_t ie_total_len = (size_t)(IE_SIZEOF_HEADER + tail->data_len);
            size += ie_total_len;

            link = &tail->next;
        }

        size += (size_t)(IE_SIZEOF_HEADER + ie->data_len);
        if (size > MMWLAN_VENDOR_IE_TOTAL_MAX_BYTES)
        {
            return MMWLAN_UNAVAILABLE;
        }
    }

    struct vendor_ie_list_item *item =
        (struct vendor_ie_list_item *)mmosal_malloc(sizeof(*item) + ie->data_len);
    if (item == NULL)
    {
        MMLOG_WRN("vendor IE alloc failed (size %u)\n", (unsigned)(sizeof(*item) + ie->data_len));
        return MMWLAN_NO_MEM;
    }

    item->next = NULL;
    item->mgmt_type_mask = ie->mgmt_type_mask;
    item->data_len = ie->data_len;
    memcpy(item->element, ie->data, ie->data_len);

    if (vendor_ies->head == NULL)
    {
        vendor_ies->head = item;
    }
    else
    {
        tail->next = item;
    }

    MMLOG_DBG("vendor IE added: mask=0x%02x data_len=%u", ie->mgmt_type_mask, ie->data_len);

    return MMWLAN_SUCCESS;
}

void vendor_ie_list_clear(struct vendor_ies *vendor_ies, uint8_t mgmt_type_mask)
{
    if (vendor_ies == NULL)
    {
        return;
    }
    struct vendor_ie_list_item **link = &vendor_ies->head;
    while (*link != NULL)
    {
        struct vendor_ie_list_item *item = *link;
        const uint8_t cleared = item->mgmt_type_mask & mgmt_type_mask;
        if (cleared == 0)
        {
            link = &item->next;
            continue;
        }

        item->mgmt_type_mask &= (uint8_t)~mgmt_type_mask;
        MMLOG_VRB("vendor IE clear: cleared_bits=0x%02x remaining_mask=0x%02x\n",
                  cleared,
                  item->mgmt_type_mask & (uint8_t)~mgmt_type_mask);

        if (item->mgmt_type_mask != 0)
        {

            link = &item->next;
            continue;
        }


        *link = item->next;
        mmosal_free(item);
    }
}

void vendor_ies_deinit(struct vendor_ies *vendor_ies)
{
    vendor_ie_list_clear(vendor_ies, MMWLAN_VENDOR_IE_MGMT_ALL);
}

void vendor_ie_list_emit(struct vendor_ies *vendor_ies, struct consbuf *buf, uint8_t mgmt_type_mask)
{
    if (vendor_ies == NULL)
    {
        return;
    }
    for (const struct vendor_ie_list_item *item = vendor_ies->head; item != NULL; item = item->next)
    {
        if ((item->mgmt_type_mask & mgmt_type_mask) == 0)
        {
            continue;
        }
        MMLOG_VRB("emitting vendor IE: len=%u for frame_mask=0x%02x\n",
                  item->data_len,
                  mgmt_type_mask);
        ie_build_hdr(buf, DOT11_IE_VENDOR_SPECIFIC, item->data_len);
        consbuf_append(buf, item->element, item->data_len);
    }
}
