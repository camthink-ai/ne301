/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "umac_wnm_sleep.h"
#include "stdbool.h"
#include "wnm_sleep_fsm.h"


typedef void (
    *umac_wnm_sleep_request_cb_t)(struct umac_data *umacd, enum mmwlan_status status, void *arg);

struct umac_wnm_sleep_data
{
    struct wnm_sleep_fsm_instance fsm_inst;
    umac_wnm_sleep_request_cb_t callback;
    uint8_t retry_counter;
    void *arg;

    struct mmosal_semb *semb;
    volatile enum mmwlan_status *status;
};
