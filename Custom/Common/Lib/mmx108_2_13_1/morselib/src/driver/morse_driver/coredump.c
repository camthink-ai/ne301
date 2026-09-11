/*
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include "coredump.h"
#include "mmosal.h"
#include "morse.h"
#include "common/morse_error.h"
#include "driver/transport/morse_transport.h"


struct MM_PACKED coredump_assert_info
{

    uint32_t hart;

    uint32_t line;

    char info[];
};

void coredump_log_assert_info(struct driver_data *driverd)
{
    struct morse_coredump_mem_region *region;
    struct coredump_assert_info *info;

    morse_trns_claim(driverd);
    for (uint32_t i = 0; i < MM_ARRAY_COUNT(driverd->coredump.memory_regions); i++)
    {
        region = &driverd->coredump.memory_regions[i];
        if (region->type != MORSE_MEM_REGION_TYPE_ASSERT_INFO)
        {
            continue;
        }

        MMLOG_DBG("Looking in region 0x%08x\n", region->start);

        if (region->len < (sizeof(*info) + 1))
        {
            MMLOG_WRN("Size of info region is to small; %d bytes\n", region->len);
            continue;
        }

        info = (struct coredump_assert_info *)mmosal_calloc(1, region->len);
        if (info == NULL)
        {
            MMLOG_WRN("No memory\n");
            continue;
        }

        morse_error_t result =
            morse_trns_read_multi_byte(driverd, region->start, (uint8_t *)info, region->len);
        if (result != MORSE_SUCCESS)
        {
            mmosal_free(info);
            continue;
        }

        uint32_t hart = le32toh(info->hart);
        uint32_t line = le32toh(info->line);

        int info_strlen = strnlen(info->info, (region->len - sizeof(*info)));

        if (info_strlen > 0 || line > 0)
        {
            MMLOG_ERR("FW assert %.*s:%d (hart:%d)\n", info_strlen, info->info, line, hart);
            mmosal_free(info);
            break;
        }

        mmosal_free(info);
    }
    morse_trns_release(driverd);
};
