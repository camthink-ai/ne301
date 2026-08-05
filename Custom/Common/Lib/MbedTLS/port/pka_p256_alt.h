/**
  ******************************************************************************
  * @file    pka_p256_alt.h
  * @brief   STM32 PKA hardware helpers for NIST P-256 (32-byte field)
  ******************************************************************************
  */

#ifndef PKA_P256_ALT_H
#define PKA_P256_ALT_H

#include "common.h"

#if defined(MBEDTLS_ECP_C) && defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)

#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"

#define PKA_P256_BYTES      32u
#define PKA_P256_WORDS      (PKA_P256_BYTES / 4u)
#define PKA_P256_TIMEOUT_MS 5000u

#ifndef PKA_ECC_COEF_SIGN_NEGATIVE
#define PKA_ECC_COEF_SIGN_NEGATIVE  1U
#endif

int pka_p256_mpi_fits(const mbedtls_mpi *X);
int pka_p256_modulus_fits(const mbedtls_mpi *N);

int pka_p256_modexp_mpi(mbedtls_mpi *R,
                        const mbedtls_mpi *A,
                        const mbedtls_mpi *E,
                        const mbedtls_mpi *N);

int pka_p256_modinv_mpi(mbedtls_mpi *R,
                        const mbedtls_mpi *A,
                        const mbedtls_mpi *N);

int pka_p256_mulmod_mpi(mbedtls_mpi *R,
                        const mbedtls_mpi *A,
                        const mbedtls_mpi *B,
                        const mbedtls_mpi *N);

int pka_p256_ec_mul(mbedtls_ecp_group *grp,
                    mbedtls_ecp_point *R,
                    const mbedtls_mpi *k,
                    const mbedtls_ecp_point *P);

#if defined(MBEDTLS_ECDH_C)
int pka_p256_ecdh_gen_public(mbedtls_ecp_group *grp,
                             mbedtls_mpi *d,
                             mbedtls_ecp_point *Q,
                             int (*f_rng)(void *, unsigned char *, size_t),
                             void *p_rng);

int pka_p256_ecdh_compute_shared(mbedtls_ecp_group *grp,
                                 mbedtls_mpi *z,
                                 const mbedtls_ecp_point *Q,
                                 const mbedtls_mpi *d);
#endif

#if defined(MBEDTLS_ECDSA_C)
#if defined(MBEDTLS_ECDSA_SIGN_ALT)
int pka_p256_ecdsa_sign(mbedtls_ecp_group *grp,
                        mbedtls_mpi *r,
                        mbedtls_mpi *s,
                        const mbedtls_mpi *d,
                        const unsigned char *buf,
                        size_t blen,
                        int (*f_rng)(void *, unsigned char *, size_t),
                        void *p_rng);
#endif

#if defined(MBEDTLS_ECDSA_VERIFY_ALT)
int pka_p256_ecdsa_verify(mbedtls_ecp_group *grp,
                          const unsigned char *buf,
                          size_t blen,
                          const mbedtls_ecp_point *Q,
                          const mbedtls_mpi *r,
                          const mbedtls_mpi *s);
#endif
#endif /* MBEDTLS_ECDSA_C */

#endif /* MBEDTLS_ECP_C && MBEDTLS_ECP_DP_SECP256R1_ENABLED */

#endif /* PKA_P256_ALT_H */
