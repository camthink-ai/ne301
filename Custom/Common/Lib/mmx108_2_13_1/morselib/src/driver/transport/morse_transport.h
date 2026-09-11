/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */


#pragma once

#include "common/morse_error.h"
#include "driver/morse_driver/morse.h"


void morse_trns_init(void);


void morse_trns_deinit(void);


morse_error_t morse_trns_start(struct driver_data *driverd);


void morse_trns_stop(struct driver_data *driverd);


void morse_trns_set_irq_enabled(struct driver_data *driverd, bool enabled);


void morse_trns_claim(struct driver_data *driverd);


void morse_trns_release(struct driver_data *driverd);


morse_error_t morse_trns_read_multi_byte(struct driver_data *driverd,
                                         uint32_t address,
                                         uint8_t *data,
                                         uint32_t len);


morse_error_t morse_trns_write_multi_byte(struct driver_data *driverd,
                                          uint32_t address,
                                          const uint8_t *data,
                                          uint32_t len);


morse_error_t morse_trns_read_le32(struct driver_data *driverd, uint32_t address, uint32_t *data);


morse_error_t morse_trns_write_le32(struct driver_data *driverd, uint32_t address, uint32_t data);


