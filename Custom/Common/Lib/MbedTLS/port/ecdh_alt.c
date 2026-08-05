/**
  ******************************************************************************
  * @file    ecdh_alt.c
  * @brief   mbedTLS ECDH PKA offload (P-256) via pka_p256_alt
  ******************************************************************************
  */

#include "common.h"

#if defined(MBEDTLS_ECDH_C)
#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT) || defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT)

#include "mbedtls/ecdh.h"
#include "mbedtls/error.h"
#include "pka_p256_alt.h"

#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT)
int mbedtls_ecdh_gen_public(mbedtls_ecp_group *grp,
                            mbedtls_mpi *d,
                            mbedtls_ecp_point *Q,
                            int (*f_rng)(void *, unsigned char *, size_t),
                            void *p_rng)
{
    if (grp != NULL && grp->id == MBEDTLS_ECP_DP_SECP256R1) {
        return pka_p256_ecdh_gen_public(grp, d, Q, f_rng, p_rng);
    }
    return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
}
#endif

#if defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT)
int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *grp,
                                mbedtls_mpi *z,
                                const mbedtls_ecp_point *Q,
                                const mbedtls_mpi *d,
                                int (*f_rng)(void *, unsigned char *, size_t),
                                void *p_rng)
{
    (void) f_rng;
    (void) p_rng;

    if (grp != NULL && grp->id == MBEDTLS_ECP_DP_SECP256R1) {
        return pka_p256_ecdh_compute_shared(grp, z, Q, d);
    }
    return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
}
#endif

#endif /* ALT */
#endif /* MBEDTLS_ECDH_C */
