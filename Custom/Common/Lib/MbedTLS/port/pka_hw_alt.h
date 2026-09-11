/**
  ******************************************************************************
  * @file    pka_hw_alt.h
  * @brief   Shared mutex for STM32 PKA hardware (RSA + ECP accelerators)
  ******************************************************************************
  */

#ifndef PKA_HW_ALT_H
#define PKA_HW_ALT_H

#include "common.h"

#if defined(MBEDTLS_THREADING_C)
int pka_hw_lock(void);
void pka_hw_unlock(void);
#else
static inline int pka_hw_lock(void)
{
    return 0;
}
static inline void pka_hw_unlock(void)
{
}
#endif

#endif /* PKA_HW_ALT_H */
