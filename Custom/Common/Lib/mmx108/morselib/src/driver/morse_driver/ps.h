/**
 * Copyright 2017-2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include "morse.h"

enum ps_waker_id
{
    PS_WAKER_UMAC,
    PS_WAKER_PAGESET,
    PS_WAKER_COMMAND,
    PS_WAKER_IRQ,
};


int morse_ps_enable(struct driver_data *driverd, enum ps_waker_id waker_id);


int morse_ps_enable_async(struct driver_data *driverd, enum ps_waker_id waker_id);


int morse_ps_disable(struct driver_data *driverd, enum ps_waker_id waker_id);


int morse_ps_disable_async(struct driver_data *driverd, enum ps_waker_id waker_id);


void morse_ps_network_activity(struct driver_data *driverd);


int morse_ps_set_dynamic_ps_timeout(struct driver_data *driverd, uint32_t timeout_ms);


int morse_ps_init(struct driver_data *driverd);


void morse_ps_deinit(struct driver_data *driverd);

void morse_ps_work(struct driver_data *driverd);
