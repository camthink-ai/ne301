/*
 * Copyright 2025 Morse Micro / NE301
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mm_mbedtls_port.h"

#if defined(MBEDTLS_THREADING_C) && defined(MBEDTLS_THREADING_ALT)
#include "threading_alt.h"
#endif

static unsigned mm_mbedtls_port_inited;

void mm_mbedtls_port_init(void)
{
    if (mm_mbedtls_port_inited != 0u) {
        return;
    }

#if defined(MBEDTLS_THREADING_C) && defined(MBEDTLS_THREADING_ALT)
    mbedtls_threading_alt_init();
#endif

    mm_mbedtls_port_inited = 1u;
}
