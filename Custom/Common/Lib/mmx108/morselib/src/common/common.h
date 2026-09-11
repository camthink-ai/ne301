/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include <endian.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mmhal_core.h"
#include "mmosal.h"
#include "mmutils.h"

#ifndef NULL

#define NULL ((void *)0)
#endif

#ifndef ETH_ALEN

#define ETH_ALEN (6)
#endif

#ifndef BIT

#define BIT(x) (1u << (x))
#endif

#ifndef STRINGIFY

#define _STRINGIFY(x) #x

#define STRINGIFY(x) _STRINGIFY(x)
#endif


#define QDBM_TO_DBM(gain) ((gain) >> 2)

#define DBM_TO_QDBM(gain) ((gain) << 2)

#ifndef UNKNOWN_VIF_ID

#define UNKNOWN_VIF_ID (0xffff)
#endif


#define MHZ_TO_HZ(x) ((x) * 1000000)



#define countof(x) (sizeof(x) / sizeof((x)[0]))


#define FAST_ROUND_UP(x, m) ((((x) - 1) | ((m) - 1)) + 1)


enum morselib_deep_sleep_veto_id
{

    MORSELIB_VETO_EMMET = MMHAL_VETO_ID_MORSELIB_MIN,

    MORSELIB_VETO_COMMAND,
};


static inline uint32_t min_u32(uint32_t a, uint32_t b)
{
    if (a < b)
    {
        return a;
    }
    else
    {
        return b;
    }
}


static inline uint16_t min_u16(uint16_t a, uint16_t b)
{
    if (a < b)
    {
        return a;
    }
    else
    {
        return b;
    }
}


#define ETHERTYPE_EAPOL 0x888E


#define MAC_ADDR_STR_LEN (18)


#define MAC_ADDR_LEN (6)


static inline void mac_addr_to_str(uint8_t *mac_addr, char *mac_addr_str)
{
    snprintf(mac_addr_str,
             MAC_ADDR_STR_LEN,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0],
             mac_addr[1],
             mac_addr[2],
             mac_addr[3],
             mac_addr[4],
             mac_addr[5]);
}


static inline int8_t decode_hex_char(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 0xa;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 0xa;
    }
    return -1;
}


static inline bool mac_addr_to_bytes(char *mac_addr_str, uint8_t *mac_addr)
{
    unsigned octet_num;
    for (octet_num = 0; octet_num < MAC_ADDR_LEN; octet_num++)
    {
        int8_t upper_nibble;
        int8_t lower_nibble;

        if (octet_num != 0)
        {
            if (*mac_addr_str++ != ':')
            {
                return false;
            }
        }

        upper_nibble = decode_hex_char(*mac_addr_str++);
        if (upper_nibble < 0)
        {
            return false;
        }
        lower_nibble = decode_hex_char(*mac_addr_str++);
        if (lower_nibble < 0)
        {
            return false;
        }

        mac_addr[octet_num] = ((uint8_t)upper_nibble) << 4 | ((uint8_t)lower_nibble);
    }
    return true;
}




#define __BM_BIT_TO_IDX(bm, bit_num) ((bit_num) / sizeof((bm)[0]))

#define __BM_BIT_TO_SHIFT(bm, bit_num) ((bit_num) % sizeof((bm)[0]))

#define BM_DECLARE(name, num_bits) uint32_t name[num_bits / sizeof(uint32_t)]


#define BM_SET(bm, bit_num) \
    (bm)[__BM_BIT_TO_IDX(bm, bit_num)] |= 1ul << __BM_BIT_TO_SHIFT(bm, bit_num)

#define BM_CLR(bm, bit_num) \
    (bm)[__BM_BIT_TO_IDX(bm, bit_num)] &= ~(1ul << __BM_BIT_TO_SHIFT(bm, bit_num))

#define BM_IS_SET(bm, bit_num) \
    (((bm)[__BM_BIT_TO_IDX(bm, bit_num)] & 1ul << __BM_BIT_TO_SHIFT(bm, bit_num)) != 0)



#define PACK_LE16(dst16_data, src8_array)                 \
    do {                                                  \
        dst16_data = *src8_array;                         \
        dst16_data |= ((uint16_t)*(src8_array + 1) << 8); \
    } while (0)


#define PACK_BE16(dst16_data, src8_array)             \
    do {                                              \
        dst16_data = *(src8_array + 1);               \
        dst16_data |= ((uint16_t)*(src8_array) << 8); \
    } while (0)


#define UNPACK_LE16(dst8_array, src16_data)             \
    do {                                                \
        *dst8_array = (uint8_t)(src16_data);            \
        *(dst8_array + 1) = (uint8_t)(src16_data >> 8); \
    } while (0)


#define UNPACK_BE16(dst8_array, src16_data)         \
    do {                                            \
        *(dst8_array + 1) = (uint8_t)(src16_data);  \
        *(dst8_array) = (uint8_t)(src16_data >> 8); \
    } while (0)


#define PACK_LE32(dst32_data, src8_array)                  \
    do {                                                   \
        dst32_data = *src8_array;                          \
        dst32_data |= ((uint32_t)*(src8_array + 1) << 8);  \
        dst32_data |= ((uint32_t)*(src8_array + 2) << 16); \
        dst32_data |= ((uint32_t)*(src8_array + 3) << 24); \
    } while (0)


#define PACK_BE32(dst32_data, src8_array)                  \
    do {                                                   \
        dst32_data = *(src8_array + 3);                    \
        dst32_data |= ((uint32_t)*(src8_array + 2) << 8);  \
        dst32_data |= ((uint32_t)*(src8_array + 1) << 16); \
        dst32_data |= ((uint32_t)*(src8_array) << 24);     \
    } while (0)


#define UNPACK_LE32(dst8_array, src32_data)              \
    do {                                                 \
        *dst8_array = (uint8_t)(src32_data);             \
        *(dst8_array + 1) = (uint8_t)(src32_data >> 8);  \
        *(dst8_array + 2) = (uint8_t)(src32_data >> 16); \
        *(dst8_array + 3) = (uint8_t)(src32_data >> 24); \
    } while (0)


#define UNPACK_BE32(dst8_array, src32_data)              \
    do {                                                 \
        *(dst8_array + 3) = (uint8_t)(src32_data);       \
        *(dst8_array + 2) = (uint8_t)(src32_data >> 8);  \
        *(dst8_array + 1) = (uint8_t)(src32_data >> 16); \
        *(dst8_array) = (uint8_t)(src32_data >> 24);     \
    } while (0)


#define PACK_LE64(dst64_data, src8_array)                  \
    do {                                                   \
        dst64_data = ((uint64_t)*(src8_array + 0) << 0);   \
        dst64_data |= ((uint64_t)*(src8_array + 1) << 8);  \
        dst64_data |= ((uint64_t)*(src8_array + 2) << 16); \
        dst64_data |= ((uint64_t)*(src8_array + 3) << 24); \
        dst64_data |= ((uint64_t)*(src8_array + 4) << 32); \
        dst64_data |= ((uint64_t)*(src8_array + 5) << 40); \
        dst64_data |= ((uint64_t)*(src8_array + 6) << 48); \
        dst64_data |= ((uint64_t)*(src8_array + 7) << 56); \
    } while (0)


#define PACK_BE64(dst64_data, src8_array)                  \
    do {                                                   \
        dst64_data = ((uint64_t)*(src8_array + 7) << 0);   \
        dst64_data |= ((uint64_t)*(src8_array + 6) << 8);  \
        dst64_data |= ((uint64_t)*(src8_array + 5) << 16); \
        dst64_data |= ((uint64_t)*(src8_array + 4) << 24); \
        dst64_data |= ((uint64_t)*(src8_array + 3) << 32); \
        dst64_data |= ((uint64_t)*(src8_array + 2) << 40); \
        dst64_data |= ((uint64_t)*(src8_array + 1) << 48); \
        dst64_data |= ((uint64_t)*(src8_array + 0) << 56); \
    } while (0)


#define UNPACK_LE64(dst8_array, src64_data)              \
    do {                                                 \
        *(dst8_array + 0) = (uint8_t)(src64_data >> 0);  \
        *(dst8_array + 1) = (uint8_t)(src64_data >> 8);  \
        *(dst8_array + 2) = (uint8_t)(src64_data >> 16); \
        *(dst8_array + 3) = (uint8_t)(src64_data >> 24); \
        *(dst8_array + 4) = (uint8_t)(src64_data >> 32); \
        *(dst8_array + 5) = (uint8_t)(src64_data >> 40); \
        *(dst8_array + 6) = (uint8_t)(src64_data >> 48); \
        *(dst8_array + 7) = (uint8_t)(src64_data >> 56); \
    } while (0)


#define UNPACK_BE64(dst8_array, src64_data)              \
    do {                                                 \
        *(dst8_array + 7) = (uint8_t)(src64_data >> 0);  \
        *(dst8_array + 6) = (uint8_t)(src64_data >> 8);  \
        *(dst8_array + 5) = (uint8_t)(src64_data >> 16); \
        *(dst8_array + 4) = (uint8_t)(src64_data >> 24); \
        *(dst8_array + 3) = (uint8_t)(src64_data >> 32); \
        *(dst8_array + 2) = (uint8_t)(src64_data >> 40); \
        *(dst8_array + 1) = (uint8_t)(src64_data >> 48); \
        *(dst8_array + 0) = (uint8_t)(src64_data >> 56); \
    } while (0)




