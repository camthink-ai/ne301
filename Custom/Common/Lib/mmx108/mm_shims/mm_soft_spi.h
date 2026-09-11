/*
 * Copyright 2025 Morse Micro (porting layer)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bit-banged SPI master for MM610x/MM810x HaLow (MM-APPNOTE-51):
 *   Mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit words, active-low CS.
 *
 * Per-bit timing (§3.3):
 *   MOSI --[SETUP_LOOPS]--> SCLK↑ --[SAMPLE_LOOPS]--> read MISO --> SCLK↓ --[LOW_LOOPS]-->
 *
 * Delays are __NOP loop counts (same unit as the original MM_SOFT_SPI_DELAY_CYCLES=100).
 * Do not confuse with CPU cycles / DWT.
 *
 * Tuning (in mm_soft_spi.c or -D on the compiler command line):
 *   MM_SOFT_SPI_SETUP_LOOPS   before SCLK rise (tISU)
 *   MM_SOFT_SPI_SAMPLE_LOOPS  after SCLK rise, before MISO read (tODLY) — keep ~100 on NE301
 *   MM_SOFT_SPI_LOW_LOOPS     after SCLK fall
 *
 * Full legacy symmetric behaviour: -DMM_SOFT_SPI_DELAY_LOOPS=100
 * (or MM_SOFT_SPI_DELAY_CYCLES, alias)
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mm_soft_spi_init(void);
uint8_t mm_soft_spi_transfer_byte(uint8_t tx);
void mm_soft_spi_write_buf(const uint8_t *tx, unsigned len);
void mm_soft_spi_read_buf(uint8_t *rx, unsigned len);

#ifdef __cplusplus
}
#endif
