/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <endian.h>
#include "common/common.h"
#include "mmlog.h"

#define WLAN_EID_EXTENSION 255

#define MORSE_WARN_ON(x)        \
    do {                        \
        if (x)                  \
            MMLOG_WRN(#x "\n"); \
    } while (0)

static inline void get_random_bytes(uint8_t *buf, unsigned cnt)
{
    uint32_t rnd = 0;
    unsigned ii;
    for (ii = 0; ii < cnt; ii++)
    {
        if ((ii % 4) == 0)
        {
            rnd = mmhal_random_u32(0, UINT32_MAX);
        }
        *buf++ = (rnd & 0xff);
        rnd >>= 8;
    }
}

#define __stringify_(...) #__VA_ARGS__
#define __stringify(...)  __stringify_(__VA_ARGS__)

#ifndef BIT
#define BIT(x) (1u << (x))
#endif

#define MAX_BIT                    31
#define GENMASK(h, l)              ((UINT32_MAX >> (MAX_BIT - h)) & ((UINT32_MAX - (1 << l)) + 1))

#define fallthrough                __attribute__((fallthrough))

#define ARRAY_SIZE(array)          countof(array)

#define BITS_PER_BYTE              (8)
#define DIV_ROUND_UP(n, d)         (((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)          DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(uint64_t))
#define DECLARE_BITMAP(name, bits) uint64_t name[BITS_TO_LONGS(bits)]
