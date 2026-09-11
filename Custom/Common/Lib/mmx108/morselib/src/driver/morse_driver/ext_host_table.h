/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include <stdint.h>

#include "mmdrv.h"
#include "mmutils.h"

struct driver_data;


#define FW_CAPABILITIES_FLAGS_WIDTH (4)

MM_STATIC_ASSERT(FW_CAPABILITIES_FLAGS_WIDTH == MORSE_CAPS_FLAGS_WIDTH,
                 "Capability subset filled by firmware is too big");

enum morse_fw_extended_host_table_tag
{

    MORSE_FW_HOST_TABLE_TAG_S1G_CAPABILITIES = 0,
    MORSE_FW_HOST_TABLE_TAG_PAGER_BYPASS_TX_STATUS = 1,
    MORSE_FW_HOST_TABLE_TAG_INSERT_SKB_CHECKSUM = 2,
    MORSE_FW_HOST_TABLE_TAG_YAPS_TABLE = 3,
    MORSE_FW_HOST_TABLE_TAG_PAGER_PKT_MEMORY = 4,
    MORSE_FW_HOST_TABLE_TAG_PAGER_BYPASS_CMD_RESP = 5,
};

struct MM_PACKED extended_host_table_tlv_hdr
{

    uint16_t tag;

    uint16_t length;
};

struct MM_PACKED extended_host_table_tlv
{
    struct extended_host_table_tlv_hdr hdr;
    uint8_t data[];
};

struct MM_PACKED extended_host_table_capabilites_s1g
{

    struct extended_host_table_tlv_hdr header;

    uint32_t flags[FW_CAPABILITIES_FLAGS_WIDTH];

    uint8_t ampdu_mss;

    uint8_t beamformee_sts_capability;

    uint8_t number_sounding_dimensions;

    uint8_t maximum_ampdu_length;

    uint8_t morse_mmss_offset;
};

struct MM_PACKED extended_host_table
{

    uint32_t extended_host_table_length;

    uint8_t dev_mac_addr[6];

    uint8_t ext_host_table_data_tlvs[];
};


void ext_host_table_parse_tlvs(struct driver_data *driverd, uint8_t *head, uint8_t *end);
