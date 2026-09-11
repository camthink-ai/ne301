/*
 * Random number generator
 * Copyright (c) 2010-2011, Jouni Malinen <j@w1.fi>
 * Copyright 2022-2024 Morse Micro
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * Taken from hostap/src/crypto/random.c
 */

#include "common/common.h"
#include "umac_supp_shim_private.h"

#define POOL_WORDS      32
#define POOL_WORDS_MASK (POOL_WORDS - 1)
#define POOL_TAP1       26
#define POOL_TAP2       20
#define POOL_TAP3       14
#define POOL_TAP4       7
#define POOL_TAP5       1

static uint32_t pool[POOL_WORDS];
static unsigned int input_rotate = 0;
static unsigned int pool_pos = 0;

static uint32_t __ROL32(uint32_t x, uint32_t y)
{
    if (y == 0)
    {
        return x;
    }

    return (x << (y & 31)) | (x >> (32 - (y & 31)));
}

static void random_mix_pool(const void *buf, size_t len)
{
    static const uint32_t twist[8] = { 0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
                                       0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278 };
    const uint8_t *pos = (const uint8_t *)buf;
    uint32_t w;

    while (len--)
    {
        w = __ROL32(*pos++, input_rotate & 31);
        input_rotate += pool_pos ? 7 : 14;
        pool_pos = (pool_pos - 1) & POOL_WORDS_MASK;
        w ^= pool[pool_pos];
        w ^= pool[(pool_pos + POOL_TAP1) & POOL_WORDS_MASK];
        w ^= pool[(pool_pos + POOL_TAP2) & POOL_WORDS_MASK];
        w ^= pool[(pool_pos + POOL_TAP3) & POOL_WORDS_MASK];
        w ^= pool[(pool_pos + POOL_TAP4) & POOL_WORDS_MASK];
        w ^= pool[(pool_pos + POOL_TAP5) & POOL_WORDS_MASK];
        pool[pool_pos] = (w >> 3) ^ twist[w & 7];
    }
}

void random_add_randomness(const void *buf, size_t len)
{
    random_mix_pool(buf, len);
}

int random_get_bytes(void *_buf, size_t len)
{
    return crypto_get_random(_buf, len);
}

void random_init(const char *entropy_file)
{
    MM_UNUSED(entropy_file);
}

void random_deinit(void)
{
}
