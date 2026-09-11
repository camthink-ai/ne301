/*
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */



#pragma once

#include <stdint.h>

struct driver_data;


int morse_beacon_start(struct driver_data *driverd, uint16_t vif_id);


int morse_beacon_stop(struct driver_data *driverd);


int morse_beacon_work(struct driver_data *driverd);


void morse_beacon_irq_handle(struct driver_data *driverd, uint32_t status1_reg);
