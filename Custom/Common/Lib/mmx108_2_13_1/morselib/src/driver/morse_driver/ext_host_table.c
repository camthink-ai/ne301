/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stdint.h>

#include "common/morse_commands.h"
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

static void log_max_ap_num_sta_from_ext_host_table(
    struct extended_host_table_max_ap_num_sta *max_ap_num_sta)
{
    MMLOG_INF("Firmware maximum AP STAs: %lu\n", le32toh(max_ap_num_sta->num_stas));
}

static const char *boot_code_to_str(enum morse_cmd_boot_code code)
{
    switch (code)
    {
        case MORSE_CMD_BOOT_CODE_NONE:
            return "not set";
        case MORSE_CMD_BOOT_CODE_BCF_INIT:
            return "BCF initialisation incomplete";
        case MORSE_CMD_BOOT_CODE_VALID_BCF_NOT_FOUND:
            return "valid BCF not found";
        case MORSE_CMD_BOOT_CODE_BCF_LEN_INVALID:
            return "BCF length invalid";
        case MORSE_CMD_BOOT_CODE_BCF_CRC_INVALID:
            return "BCF CRC invalid";
        case MORSE_CMD_BOOT_CODE_BCF_UNSUPPORTED_VERSION:
            return "BCF unsupported version";
        case MORSE_CMD_BOOT_CODE_BCF_REGDOM_LEN_INVALID:
            return "BCF regdom length invalid";
        case MORSE_CMD_BOOT_CODE_BCF_REGDOM_CRC_INVALID:
            return "BCF regdom CRC invalid";
        case MORSE_CMD_BOOT_CODE_BCF_PARSE_FAIL:
            return "BCF parse failed";
        case MORSE_CMD_BOOT_CODE_COMPLETE:
            return "complete";
        default:
            return "unknown";
    }
}

static void handle_boot_code_from_ext_host_table(struct extended_host_table_boot_code *boot_code)
{
    enum morse_cmd_boot_code code = (enum morse_cmd_boot_code)boot_code->code;

    if (code != MORSE_CMD_BOOT_CODE_COMPLETE)
    {
        MMLOG_ERR("Firmware failed to boot: %s (code %u)\n",
                  boot_code_to_str(code),
                  boot_code->code);
    }
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

        case MORSE_FW_HOST_TABLE_TAG_MAX_AP_NUM_STA:
            log_max_ap_num_sta_from_ext_host_table(
                (struct extended_host_table_max_ap_num_sta *)tlv);
            break;

        case MORSE_FW_HOST_TABLE_TAG_BOOT_CODE:
            handle_boot_code_from_ext_host_table((struct extended_host_table_boot_code *)tlv);
            break;

        case MORSE_FW_HOST_TABLE_TAG_HEADLESS_CONFIG_ADDR:

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
