/*
 * Copyright 2021-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdatomic.h>

#include "mmhal_core.h"
#include "mm_hal_common.h"
#include "mmosal.h"

#include "main.h"
#include "rng.h"

static volatile atomic_uint_fast32_t deep_sleep_vetos = 0;
static struct mmosal_mutex *rng_mutex = NULL;
extern RNG_HandleTypeDef hrng;

static void mmhal_rng_recover(void)
{
    (void)HAL_RNG_DeInit(&hrng);
    MX_RNG_Init();
}

static int mmhal_rng_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        uint32_t state = HAL_RNG_GetState(&hrng);

        if (state == HAL_RNG_STATE_READY) {
            return 1;
        }
        if (state == HAL_RNG_STATE_ERROR || state == HAL_RNG_STATE_RESET) {
            mmhal_rng_recover();
            return (HAL_RNG_GetState(&hrng) == HAL_RNG_STATE_READY) ? 1 : 0;
        }
        mmosal_task_sleep(1);
    }

    return 0;
}

void mmhal_random_init(void)
{
    if (rng_mutex != NULL) {
        return;
    }
    rng_mutex = mmosal_mutex_create("rng");
    MMOSAL_ASSERT(rng_mutex != NULL);
}

int mmhal_random_get_u32(uint32_t *value)
{
    uint32_t rndm;
    uint32_t ii;
    HAL_StatusTypeDef status = HAL_ERROR;

    if (value == NULL) {
        return 0;
    }

    if (rng_mutex == NULL) {
        return 0;
    }

    if (!mmhal_rng_wait_ready(500u)) {
        return 0;
    }

#define RNG_MAX_GENERATE_ATTEMPTS (100)
    for (ii = 0; ii < RNG_MAX_GENERATE_ATTEMPTS; ii++) {
        if (!mmosal_mutex_get(rng_mutex, 1000u)) {
            continue;
        }
        status = HAL_RNG_GenerateRandomNumber(&hrng, &rndm);
        mmosal_mutex_release(rng_mutex);
        if (status == HAL_OK) {
            *value = rndm;
            return 1;
        }
        if (hrng.ErrorCode == HAL_RNG_ERROR_BUSY) {
            (void)mmhal_rng_wait_ready(50u);
            continue;
        }
        if (hrng.ErrorCode == HAL_RNG_ERROR_SEED ||
            hrng.ErrorCode == HAL_RNG_ERROR_RECOVERSEED ||
            HAL_RNG_GetState(&hrng) == HAL_RNG_STATE_ERROR) {
            mmhal_rng_recover();
            continue;
        }
    }

    return 0;
}

uint32_t mmhal_random_u32(uint32_t min, uint32_t max)
{
    uint32_t rndm;

    if (!mmhal_random_get_u32(&rndm)) {
        MMOSAL_ASSERT(0);
        return min;
    }

    if (min == 0 && max == UINT32_MAX) {
        return rndm;
    }
    return rndm % (max - min + 1) + min;
}

void mmhal_set_deep_sleep_veto(uint8_t veto_id)
{
    MMOSAL_ASSERT(veto_id < 32);
    atomic_fetch_or(&deep_sleep_vetos, 1ul << veto_id);
}

void mmhal_clear_deep_sleep_veto(uint8_t veto_id)
{
    MMOSAL_ASSERT(veto_id < 32);
    atomic_fetch_and(&deep_sleep_vetos, ~(1ul << veto_id));
}

uint32_t mmhal_get_deep_sleep_veto(void)
{
    return deep_sleep_vetos;
}
