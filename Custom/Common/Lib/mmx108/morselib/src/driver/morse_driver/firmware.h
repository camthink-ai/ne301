/*
 * Copyright 2017-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#pragma once

#include "mmhal_wlan.h"

#include "driver/shim/driver_types.h"

#define BCF_DATABASE_SIZE  (1024)
#define MORSE_FW_MAX_SIZE  (13 * 32 * 1024)
#define MORSE_BCF_MAX_SIZE (BCF_DATABASE_SIZE * 1024)

#define IFLASH_BASE_ADDR   0x400000
#define DFLASH_BASE_ADDR   0xC00000

#define MAX_BCF_NAME_LEN   64

struct driver_data;


typedef void (
    *morse_file_read_cb_t)(uint32_t offset, uint32_t requested_len, struct mmhal_robuf *robuf);

enum morse_fw_info_tlv_type
{
    MORSE_FW_INFO_TLV_BCF_ADDR = 1
};

struct MM_PACKED morse_fw_info_tlv
{
    uint16_t type;
    uint16_t length;
    uint8_t val[];
};


int morse_firmware_init(struct driver_data *driverd,
                        morse_file_read_cb_t fw_callback,
                        morse_file_read_cb_t bcf_callback);

int morse_firmware_load_mbin(struct driver_data *driverd, morse_file_read_cb_t file_read_cb);

int morse_bcf_load_mbin(struct driver_data *driverd,
                        morse_file_read_cb_t file_read_cb,
                        unsigned int bcf_address);

int morse_bcf_get_metadata(struct mmwlan_bcf_metadata *metadata);


int morse_firmware_parse_extended_host_table(struct driver_data *driverd, uint8_t *mac_addr);
