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

/**
 * Redo the WAKE handshake while the bus is marked awake.
 *
 * A wakeup that timed out waiting for BUSY still marks the bus awake
 * (suspended = false), so every later morse_ps_wakeup() returns early and the
 * wake pin is never re-asserted. If the chip never saw the original edge it
 * stays asleep forever while the host keeps transacting against it. This
 * toggles WAKE low->high and waits for BUSY again, giving the chip a fresh
 * edge without a full chip reset. Only call from the driver task when TX has
 * been failing persistently.
 */
void morse_ps_kick_wake(struct driver_data *driverd);



int morse_ps_set_dynamic_ps_timeout(struct driver_data *driverd, uint32_t timeout_ms);


int morse_ps_init(struct driver_data *driverd);


void morse_ps_deinit(struct driver_data *driverd);

void morse_ps_work(struct driver_data *driverd);
