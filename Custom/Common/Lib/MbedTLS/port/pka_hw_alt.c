/**
  ******************************************************************************
  * @file    pka_hw_alt.c
  * @brief   Shared mutex for STM32 PKA hardware (RSA + ECP accelerators)
  ******************************************************************************
  */

#include "common.h"
#include "pka_hw_alt.h"

#if defined(MBEDTLS_THREADING_C)

#include "mbedtls/threading.h"

static mbedtls_threading_mutex_t pka_hw_mutex;
static unsigned char pka_hw_mutex_ready;

int pka_hw_lock(void)
{
    if (pka_hw_mutex_ready == 0U) {
        mbedtls_mutex_init(&pka_hw_mutex);
        pka_hw_mutex_ready = 1U;
    }
    return mbedtls_mutex_lock(&pka_hw_mutex);
}

void pka_hw_unlock(void)
{
    if (pka_hw_mutex_ready != 0U) {
        (void) mbedtls_mutex_unlock(&pka_hw_mutex);
    }
}

#endif /* MBEDTLS_THREADING_C */
