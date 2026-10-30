/*
 * shim/mem.h — host-side include shim for the firmware mbedTLS config.
 *
 * The firmware's actual mbedTLS configuration
 * (Custom/Common/Lib/MbedTLS/configs/config-ccm-psk-tls1_2.h, line 28)
 * does `#include "mem.h"` and resolves it to Custom/Hal/mem.h on device
 * (declarations for hal_mem_calloc_large / hal_mem_free used by the
 * MBEDTLS_PLATFORM_CALLOC_MACRO / MBEDTLS_PLATFORM_FREE_MACRO defines at
 * lines 129-130 of that config).
 *
 * On the host those firmware allocators do not exist; host_user_config.h
 * undefines the two platform allocator macros (falling back to libc
 * calloc/free), so this shim only needs to keep the include resolvable.
 * This mirrors the existing repo precedent
 * tests/app_host/lfstool/shim/Hal/mem.h.
 */
#ifndef SIGN_SPIKE_SHIM_MEM_H
#define SIGN_SPIKE_SHIM_MEM_H

#include <stdlib.h>

#endif /* SIGN_SPIKE_SHIM_MEM_H */
