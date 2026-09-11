/**
  ******************************************************************************
  * @file    ecp_alt.h
  * @brief   mbedTLS ECP PKA hardware acceleration (STM32N6 PKA, P-256)
  ******************************************************************************
  */

#ifndef ECP_ALT_H
#define ECP_ALT_H

#include "mbedtls/build_info.h"
#include "mbedtls/ecp.h"

#if defined(MBEDTLS_ECP_INTERNAL_ALT)

unsigned char mbedtls_internal_ecp_grp_capable(const mbedtls_ecp_group *grp);
int mbedtls_internal_ecp_init(const mbedtls_ecp_group *grp);
void mbedtls_internal_ecp_free(const mbedtls_ecp_group *grp);

#if defined(MBEDTLS_ECP_ADD_MIXED_ALT)
int mbedtls_internal_ecp_add_mixed(const mbedtls_ecp_group *grp,
                                 mbedtls_ecp_point *R,
                                 const mbedtls_ecp_point *P,
                                 const mbedtls_ecp_point *Q);
#endif

#if defined(MBEDTLS_ECP_DOUBLE_JAC_ALT)
int mbedtls_internal_ecp_double_jac(const mbedtls_ecp_group *grp,
                                    mbedtls_ecp_point *R,
                                    const mbedtls_ecp_point *P);
#endif

#if defined(MBEDTLS_ECP_NORMALIZE_JAC_ALT)
int mbedtls_internal_ecp_normalize_jac(const mbedtls_ecp_group *grp,
                                       mbedtls_ecp_point *pt);
#endif

#endif /* MBEDTLS_ECP_INTERNAL_ALT */

#endif /* ECP_ALT_H */
