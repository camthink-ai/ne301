/*
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "driver/morse_driver/ext_host_table.h"

struct MM_PACKED extended_host_table_pager_bypass_cmd_resp
{
    struct extended_host_table_tlv_hdr header;
    uint32_t cmd_resp_buffer_addr;
};

struct MM_PACKED extended_host_table_pager_bypass_tx_status
{
    struct extended_host_table_tlv_hdr header;
    uint32_t tx_status_buffer_addr;
};

struct MM_PACKED extended_host_table_pager_pkt_memory
{
    struct extended_host_table_tlv_hdr header;

    uint32_t base_addr;

    uint16_t page_len;

    uint8_t page_len_reserved;

    uint8_t num;
};

struct MM_PACKED extended_host_table_insert_skb_checksum
{
    struct extended_host_table_tlv_hdr header;
    uint8_t insert_and_validate_checksum;
};
