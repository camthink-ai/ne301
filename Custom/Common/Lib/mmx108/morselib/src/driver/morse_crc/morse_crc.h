/*
 * Copyright 2024 Morse Micro
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


uint8_t morse_crc7_sd(uint8_t crc, const void *data, uint32_t data_len);


uint16_t morse_crc16_xmodem(uint16_t crc, const void *data, size_t data_len);


uint8_t morse_yaps_crc(uint32_t word);
