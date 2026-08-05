/*
 * Copyright 2022 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

static inline uint16_t htobe16(uint16_t x)
{
    return __builtin_bswap16(x);
}

static inline uint16_t htole16(uint16_t x)
{
    return x;
}

static inline uint16_t be16toh(uint16_t x)
{
    return __builtin_bswap16(x);
}

static inline uint16_t le16toh(uint16_t x)
{
    return x;
}

static inline uint32_t htobe32(uint32_t x)
{
    return __builtin_bswap32(x);
}

static inline uint32_t htole32(uint32_t x)
{
    return x;
}

static inline uint32_t be32toh(uint32_t x)
{
    return __builtin_bswap32(x);
}

static inline uint32_t le32toh(uint32_t x)
{
    return x;
}

static inline uint64_t htobe64(uint64_t x)
{
    return __builtin_bswap64(x);
}

static inline uint64_t htole64(uint64_t x)
{
    return x;
}
static inline uint64_t be64toh(uint64_t x)
{
    return __builtin_bswap64(x);
}

static inline uint64_t le64toh(uint64_t x)
{
    return x;
}
