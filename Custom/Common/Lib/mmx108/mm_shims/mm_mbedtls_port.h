/*
 * Copyright 2025 Morse Micro / NE301
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * Early mbedTLS port setup: CMSIS-RTOS mutex hooks and HAL RNG.
 * Call before any WPA/DPP or lwIP TLS uses mbedTLS entropy.
 */
void mm_mbedtls_port_init(void);
