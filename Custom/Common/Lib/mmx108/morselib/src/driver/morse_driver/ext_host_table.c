/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stdint.h>

#include "ext_host_table.h"
#include "mmosal.h"
#include "morse.h"

static void update_capabilities_from_ext_host_table(
    struct driver_data *driverd,
    struct extended_host_table_capabilites_s1g *caps)
{
    int i;

    for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++)
    {
        driverd->capabilities.flags[i] = le32toh(caps->flags[i]);
        MMLOG_INF("Firmware Manifest Flags%d: 0x%lx\n", i, le32toh(caps->flags[i]));
    }
    driverd->capabilities.ampdu_mss = caps->ampdu_mss;
    driverd->capabilities.morse_mmss_offset = caps->morse_mmss_offset;
    driverd->capabilities.beamformee_sts_capability = caps->beamformee_sts_capability;
    driverd->capabilities.maximum_ampdu_length_exponent = caps->maximum_ampdu_length;
    driverd->capabilities.number_sounding_dimensions = caps->number_sounding_dimensions;

    MMLOG_INF("\tAMPDU Minimum start spacing: %u\n", caps->ampdu_mss);
    MMLOG_INF("\tMorse Minimum Start Spacing offset: %u\n", caps->morse_mmss_offset);
    MMLOG_INF("\tBeamformee STS Capability: %u\n", caps->beamformee_sts_capability);
    MMLOG_INF("\tNumber of Sounding Dimensions: %u\n", caps->number_sounding_dimensions);
    MMLOG_INF("\tMaximum AMPDU Length Exponent: %u\n", caps->maximum_ampdu_length);
}


static bool ext_host_parse_common_tlv(struct driver_data *driverd,
                                      struct extended_host_table_tlv *tlv)
{
    bool handled = true;

    switch (le16toh(tlv->hdr.tag))
    {
        case MORSE_FW_HOST_TABLE_TAG_S1G_CAPABILITIES:
            update_capabilities_from_ext_host_table(
                driverd,
                (struct extended_host_table_capabilites_s1g *)tlv);
            break;

        default:
            handled = false;
            break;
    }

    return handled;
}

void ext_host_table_parse_tlvs(struct driver_data *driverd, uint8_t *head, uint8_t *end)
{
    while (head < end)
    {
        struct extended_host_table_tlv *tlv = (struct extended_host_table_tlv *)head;

        if ((head + tlv->hdr.length) > end)
        {
            MMLOG_WRN("TLV exceeds bounds\n");
            MMOSAL_DEV_ASSERT(false);
            break;
        }

        bool handled = ext_host_parse_common_tlv(driverd, tlv);
        handled |= driverd->cfg->ext_host_parse_tlv(driverd, tlv);

        if (!handled)
        {
            MMLOG_WRN("Unknown TLV %d\n", le16toh(tlv->hdr.tag));
        }

        head += le16toh(tlv->hdr.length);
        if (tlv->hdr.length == 0)
        {
            MMLOG_WRN("Found a 0 length TLV in the extended host table\n");
            break;
        }
    }
}
