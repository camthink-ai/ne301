/**
  ******************************************************************************
  * @file    ecp_alt.c
  * @brief   mbedTLS ECP hardware acceleration via STM32N6 PKA (SECP256R1)
  *
  * Implements MBEDTLS_ECP_INTERNAL_ALT callbacks:
  *   - mbedtls_internal_ecp_normalize_jac  (software)
  *   - mbedtls_internal_ecp_double_jac     (PKA complete addition P+P)
  *   - mbedtls_internal_ecp_add_mixed      (PKA complete addition)
  *
  * Enable in config-ccm-psk-tls1_2.h:
  *   MBEDTLS_ECP_INTERNAL_ALT, MBEDTLS_ECP_NORMALIZE_JAC_ALT,
  *   MBEDTLS_ECP_DOUBLE_JAC_ALT, MBEDTLS_ECP_ADD_MIXED_ALT
  ******************************************************************************
  */

#include "common.h"

#if defined(MBEDTLS_ECP_C) && defined(MBEDTLS_ECP_INTERNAL_ALT)

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "pka_hw_alt.h"
#include "stm32n6xx_hal.h"

#include <string.h>

#define EC_P256_BYTES       32u
#define ST_PKA_TIMEOUT      5000u

#ifndef PKA_ECC_COEF_SIGN_POSITIVE
#define PKA_ECC_COEF_SIGN_POSITIVE  0U
#endif
#ifndef PKA_ECC_COEF_SIGN_NEGATIVE
#define PKA_ECC_COEF_SIGN_NEGATIVE  1U
#endif

static const uint8_t P256_A_ABS[EC_P256_BYTES] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03
};

static const uint8_t Z_ONE[EC_P256_BYTES] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

static int mpi_to_be32(const mbedtls_mpi *X, uint8_t out[EC_P256_BYTES])
{
    size_t len = mbedtls_mpi_size(X);

    if (len > EC_P256_BYTES) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    memset(out, 0, EC_P256_BYTES);
    return mbedtls_mpi_write_binary(X, out + (EC_P256_BYTES - len), len);
}

static int be32_to_mpi(mbedtls_mpi *X, const uint8_t in[EC_P256_BYTES])
{
    return mbedtls_mpi_read_binary(X, in, EC_P256_BYTES);
}

static int ec_group_to_hw_p256(const mbedtls_ecp_group *grp,
                               uint8_t p[EC_P256_BYTES],
                               uint8_t gx[EC_P256_BYTES],
                               uint8_t gy[EC_P256_BYTES],
                               uint8_t n[EC_P256_BYTES],
                               uint8_t b[EC_P256_BYTES])
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->P, p, EC_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.X, gx, EC_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.Y, gy, EC_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->N, n, EC_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->B, b, EC_P256_BYTES));

cleanup:
    return ret;
}

static int ecp_pka_complete_addition(const uint8_t modulus[EC_P256_BYTES],
                                     const uint8_t x1[EC_P256_BYTES],
                                     const uint8_t y1[EC_P256_BYTES],
                                     const uint8_t z1[EC_P256_BYTES],
                                     const uint8_t x2[EC_P256_BYTES],
                                     const uint8_t y2[EC_P256_BYTES],
                                     const uint8_t z2[EC_P256_BYTES],
                                     uint8_t out_x[EC_P256_BYTES],
                                     uint8_t out_y[EC_P256_BYTES],
                                     uint8_t out_z[EC_P256_BYTES])
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_ECCCompleteAdditionInTypeDef in = {0};
    PKA_ECCCompleteAdditionOutTypeDef out = {0};

    ret = pka_hw_lock();
    if (ret != 0) {
        return ret;
    }
    locked = 1;

    __HAL_RCC_PKA_CLK_ENABLE();
    hpka.Instance = PKA;
    MBEDTLS_MPI_CHK((HAL_PKA_Init(&hpka) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);

    HAL_PKA_RAMReset(&hpka);

    in.modulusSize = EC_P256_BYTES;
    in.coefSign = PKA_ECC_COEF_SIGN_NEGATIVE;
    in.coefA = P256_A_ABS;
    in.modulus = modulus;
    in.basePointX1 = x1;
    in.basePointY1 = y1;
    in.basePointZ1 = z1;
    in.basePointX2 = x2;
    in.basePointY2 = y2;
    in.basePointZ2 = z2;

    MBEDTLS_MPI_CHK((HAL_PKA_ECCCompleteAddition(&hpka, &in, ST_PKA_TIMEOUT) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);

    out.ptX = out_x;
    out.ptY = out_y;
    out.ptZ = out_z;
    HAL_PKA_ECCCompleteAddition_GetResult(&hpka, &out);

cleanup:
    HAL_PKA_DeInit(&hpka);
    __HAL_RCC_PKA_CLK_DISABLE();
    if (locked != 0) {
        pka_hw_unlock();
    }
    return ret;
}

unsigned char mbedtls_internal_ecp_grp_capable(const mbedtls_ecp_group *grp)
{
    if (grp == NULL) {
        return 0U;
    }

    return (grp->id == MBEDTLS_ECP_DP_SECP256R1) ? 1U : 0U;
}

int mbedtls_internal_ecp_init(const mbedtls_ecp_group *grp)
{
    (void) grp;
    return 0;
}

void mbedtls_internal_ecp_free(const mbedtls_ecp_group *grp)
{
    (void) grp;
}

#if defined(MBEDTLS_ECP_NORMALIZE_JAC_ALT)
int mbedtls_internal_ecp_normalize_jac(const mbedtls_ecp_group *grp, mbedtls_ecp_point *pt)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    mbedtls_mpi Zi, ZZ, ZZZ;

    mbedtls_mpi_init(&Zi);
    mbedtls_mpi_init(&ZZ);
    mbedtls_mpi_init(&ZZZ);

    if (grp == NULL || pt == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    if (mbedtls_mpi_cmp_int(&pt->MBEDTLS_PRIVATE(Z), 0) == 0) {
        return 0;
    }

    MBEDTLS_MPI_CHK(mbedtls_mpi_inv_mod(&Zi, &pt->MBEDTLS_PRIVATE(Z), &grp->MBEDTLS_PRIVATE(P)));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&ZZ, &Zi, &Zi));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&ZZ, &ZZ, &grp->MBEDTLS_PRIVATE(P)));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&ZZZ, &ZZ, &Zi));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&ZZZ, &ZZZ, &grp->MBEDTLS_PRIVATE(P)));

    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&pt->MBEDTLS_PRIVATE(X), &pt->MBEDTLS_PRIVATE(X), &ZZ));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&pt->MBEDTLS_PRIVATE(X), &pt->MBEDTLS_PRIVATE(X), &grp->MBEDTLS_PRIVATE(P)));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&pt->MBEDTLS_PRIVATE(Y), &pt->MBEDTLS_PRIVATE(Y), &ZZZ));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&pt->MBEDTLS_PRIVATE(Y), &pt->MBEDTLS_PRIVATE(Y), &grp->MBEDTLS_PRIVATE(P)));
    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(&pt->MBEDTLS_PRIVATE(Z), 1));

cleanup:
    mbedtls_mpi_free(&ZZZ);
    mbedtls_mpi_free(&ZZ);
    mbedtls_mpi_free(&Zi);
    return ret;
}
#endif /* MBEDTLS_ECP_NORMALIZE_JAC_ALT */

#if defined(MBEDTLS_ECP_DOUBLE_JAC_ALT)
int mbedtls_internal_ecp_double_jac(const mbedtls_ecp_group *grp,
                                  mbedtls_ecp_point *R,
                                  const mbedtls_ecp_point *P)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    uint8_t p_buf[EC_P256_BYTES];
    uint8_t gx_buf[EC_P256_BYTES];
    uint8_t gy_buf[EC_P256_BYTES];
    uint8_t n_buf[EC_P256_BYTES];
    uint8_t b_buf[EC_P256_BYTES];
    uint8_t px_buf[EC_P256_BYTES];
    uint8_t py_buf[EC_P256_BYTES];
    uint8_t pz_buf[EC_P256_BYTES];
    uint8_t out_x[EC_P256_BYTES];
    uint8_t out_y[EC_P256_BYTES];
    uint8_t out_z[EC_P256_BYTES];

    if (grp == NULL || R == NULL || P == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    if (mbedtls_mpi_cmp_int(&P->MBEDTLS_PRIVATE(Z), 0) == 0) {
        return mbedtls_ecp_copy(R, P);
    }

    MBEDTLS_MPI_CHK(ec_group_to_hw_p256(grp, p_buf, gx_buf, gy_buf, n_buf, b_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(X), px_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(Y), py_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(Z), pz_buf));

    MBEDTLS_MPI_CHK(ecp_pka_complete_addition(p_buf, px_buf, py_buf, pz_buf,
                                              px_buf, py_buf, pz_buf,
                                              out_x, out_y, out_z));

    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(X), out_x));
    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(Y), out_y));
    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(Z), out_z));

cleanup:
    return ret;
}
#endif /* MBEDTLS_ECP_DOUBLE_JAC_ALT */

#if defined(MBEDTLS_ECP_ADD_MIXED_ALT)
int mbedtls_internal_ecp_add_mixed(const mbedtls_ecp_group *grp,
                                   mbedtls_ecp_point *R,
                                   const mbedtls_ecp_point *P,
                                   const mbedtls_ecp_point *Q)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    uint8_t p_buf[EC_P256_BYTES];
    uint8_t gx_buf[EC_P256_BYTES];
    uint8_t gy_buf[EC_P256_BYTES];
    uint8_t n_buf[EC_P256_BYTES];
    uint8_t b_buf[EC_P256_BYTES];
    uint8_t px_buf[EC_P256_BYTES];
    uint8_t py_buf[EC_P256_BYTES];
    uint8_t pz_buf[EC_P256_BYTES];
    uint8_t qx_buf[EC_P256_BYTES];
    uint8_t qy_buf[EC_P256_BYTES];
    uint8_t out_x[EC_P256_BYTES];
    uint8_t out_y[EC_P256_BYTES];
    uint8_t out_z[EC_P256_BYTES];

    if (grp == NULL || R == NULL || P == NULL || Q == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    if (mbedtls_mpi_cmp_int(&P->MBEDTLS_PRIVATE(Z), 0) == 0) {
        return mbedtls_ecp_copy(R, Q);
    }

    if (mbedtls_mpi_cmp_int(&Q->MBEDTLS_PRIVATE(Z), 0) == 0) {
        return mbedtls_ecp_copy(R, P);
    }
    if (mbedtls_mpi_cmp_int(&Q->MBEDTLS_PRIVATE(Z), 1) != 0) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    MBEDTLS_MPI_CHK(ec_group_to_hw_p256(grp, p_buf, gx_buf, gy_buf, n_buf, b_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(X), px_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(Y), py_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(Z), pz_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&Q->MBEDTLS_PRIVATE(X), qx_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(&Q->MBEDTLS_PRIVATE(Y), qy_buf));

    MBEDTLS_MPI_CHK(ecp_pka_complete_addition(p_buf, px_buf, py_buf, pz_buf,
                                              qx_buf, qy_buf, Z_ONE,
                                              out_x, out_y, out_z));

    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(X), out_x));
    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(Y), out_y));
    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(Z), out_z));

cleanup:
    return ret;
}
#endif /* MBEDTLS_ECP_ADD_MIXED_ALT */

#endif /* MBEDTLS_ECP_C && MBEDTLS_ECP_INTERNAL_ALT */
