/*
 * Copyright 2022-2024 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */

#pragma once

#include <stdint.h>

#include "mmosal.h"
#include "mmlog.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

#define BIT_COUNT(_x) (__builtin_popcount(_x))

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define MMRC_OSAL_ASSERT MMOSAL_ASSERT

#define MMRC_OSAL_PR_ERR MMLOG_ERR


void osal_mmrc_seed_random(void);


uint32_t osal_mmrc_random_u32(uint32_t max);
