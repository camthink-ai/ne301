/**
  ******************************************************************************
  * @file    ecdsa_alt.c
  * @brief   mbedTLS ECDSA PKA offload (P-256) via pka_p256_alt
  ******************************************************************************
  */

#include "common.h"

#if defined(MBEDTLS_ECDSA_C)
#if defined(MBEDTLS_ECDSA_SIGN_ALT) || defined(MBEDTLS_ECDSA_VERIFY_ALT)

#include "mbedtls/ecdsa.h"
#include "mbedtls/error.h"
#include "pka_p256_alt.h"

#if defined(MBEDTLS_ECDSA_SIGN_ALT)
int mbedtls_ecdsa_sign(mbedtls_ecp_group *grp,
                       mbedtls_mpi *r,
                       mbedtls_mpi *s,
                       const mbedtls_mpi *d,
                       const unsigned char *buf,
                       size_t blen,
                       int (*f_rng)(void *, unsigned char *, size_t),
                       void *p_rng)
{
    if (grp != NULL && grp->id == MBEDTLS_ECP_DP_SECP256R1) {
        return pka_p256_ecdsa_sign(grp, r, s, d, buf, blen, f_rng, p_rng);
    }
    return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
}
#endif

#if defined(MBEDTLS_ECDSA_VERIFY_ALT)
int mbedtls_ecdsa_verify(mbedtls_ecp_group *grp,
                         const unsigned char *buf,
                         size_t blen,
                         const mbedtls_ecp_point *Q,
                         const mbedtls_mpi *r,
                         const mbedtls_mpi *s)
{
    if (grp != NULL && grp->id == MBEDTLS_ECP_DP_SECP256R1) {
        return pka_p256_ecdsa_verify(grp, buf, blen, Q, r, s);
    }
    return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
}
#endif

#endif /* ALT */
#endif /* MBEDTLS_ECDSA_C */
