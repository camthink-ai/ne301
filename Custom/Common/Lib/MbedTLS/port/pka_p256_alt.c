/**
  ******************************************************************************
  * @file    pka_p256_alt.c
  * @brief   STM32 PKA P-256: ModExp, ModInv, MulMod, ECCMul, ECDH, ECDSA
  ******************************************************************************
  */

#include "common.h"

#if defined(MBEDTLS_ECP_C) && defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "pka_p256_alt.h"
#include "pka_hw_alt.h"
#include "stm32n6xx_hal.h"

#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"

#include <string.h>

#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n, b, i)                          \
    do {                                                \
        (n) = ((uint32_t) (b)[(i)] << 24)               \
            | ((uint32_t) (b)[(i) + 1] << 16)           \
            | ((uint32_t) (b)[(i) + 2] << 8)            \
            | ((uint32_t) (b)[(i) + 3]);                \
    } while (0)
#endif

static const uint8_t P256_A_ABS[PKA_P256_BYTES] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03
};

static int pka_session_begin(PKA_HandleTypeDef *hpka, int *locked)
{
    int ret;

    ret = pka_hw_lock();
    if (ret != 0) {
        return ret;
    }
    *locked = 1;

    __HAL_RCC_PKA_CLK_ENABLE();
    hpka->Instance = PKA;
    if (HAL_PKA_Init(hpka) != HAL_OK) {
        __HAL_RCC_PKA_CLK_DISABLE();
        pka_hw_unlock();
        *locked = 0;
        return MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
    }
    HAL_PKA_RAMReset(hpka);
    return 0;
}

static void pka_session_end(PKA_HandleTypeDef *hpka, int locked)
{
    if (locked == 0) {
        return;
    }
    HAL_PKA_DeInit(hpka);
    __HAL_RCC_PKA_CLK_DISABLE();
    pka_hw_unlock();
}

int pka_p256_mpi_fits(const mbedtls_mpi *X)
{
    if (X == NULL) {
        return 0;
    }
    return (mbedtls_mpi_size(X) <= PKA_P256_BYTES) ? 1 : 0;
}

int pka_p256_modulus_fits(const mbedtls_mpi *N)
{
    return pka_p256_mpi_fits(N);
}

static int mpi_to_be32(const mbedtls_mpi *X, uint8_t out[PKA_P256_BYTES])
{
    size_t len = mbedtls_mpi_size(X);

    if (len > PKA_P256_BYTES) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    memset(out, 0, PKA_P256_BYTES);
    return mbedtls_mpi_write_binary(X, out + (PKA_P256_BYTES - len), len);
}

static int be32_to_mpi(mbedtls_mpi *X, const uint8_t in[PKA_P256_BYTES])
{
    return mbedtls_mpi_read_binary(X, in, PKA_P256_BYTES);
}

static int mpi_to_u32(const mbedtls_mpi *X, uint32_t words[PKA_P256_WORDS])
{
    uint8_t buf[PKA_P256_BYTES];
    int ret;

    ret = mpi_to_be32(X, buf);
    if (ret != 0) {
        return ret;
    }

    for (size_t i = 0; i < PKA_P256_WORDS; i++) {
        GET_UINT32_BE(words[i], buf, (i * 4));
    }
    return 0;
}

static int u32_to_mpi(mbedtls_mpi *X, const uint32_t words[PKA_P256_WORDS])
{
    uint8_t buf[PKA_P256_BYTES];

    for (size_t i = 0; i < PKA_P256_WORDS; i++) {
        buf[(i * 4) + 0] = (unsigned char) (words[i] >> 24);
        buf[(i * 4) + 1] = (unsigned char) (words[i] >> 16);
        buf[(i * 4) + 2] = (unsigned char) (words[i] >> 8);
        buf[(i * 4) + 3] = (unsigned char) (words[i]);
    }
    return mbedtls_mpi_read_binary(X, buf, PKA_P256_BYTES);
}

static int ec_group_to_hw_p256(const mbedtls_ecp_group *grp,
                               uint8_t p[PKA_P256_BYTES],
                               uint8_t gx[PKA_P256_BYTES],
                               uint8_t gy[PKA_P256_BYTES],
                               uint8_t n[PKA_P256_BYTES],
                               uint8_t b[PKA_P256_BYTES])
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->P, p, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.X, gx, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.Y, gy, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->N, n, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->B, b, PKA_P256_BYTES));

cleanup:
    return ret;
}

int pka_p256_modexp_mpi(mbedtls_mpi *R,
                        const mbedtls_mpi *A,
                        const mbedtls_mpi *E,
                        const mbedtls_mpi *N)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_ModExpInTypeDef in = {0};
    uint8_t base[PKA_P256_BYTES];
    uint8_t exp[PKA_P256_BYTES];
    uint8_t mod[PKA_P256_BYTES];
    uint8_t out[PKA_P256_BYTES];
    size_t elen;

    if (!pka_p256_mpi_fits(A) || !pka_p256_mpi_fits(E) || !pka_p256_modulus_fits(N)) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    MBEDTLS_MPI_CHK(mpi_to_be32(A, base));
    MBEDTLS_MPI_CHK(mpi_to_be32(E, exp));
    MBEDTLS_MPI_CHK(mpi_to_be32(N, mod));

    elen = mbedtls_mpi_size(E);
    elen = ((elen + 3U) / 4U) * 4U;
    if (elen > PKA_P256_BYTES) {
        elen = PKA_P256_BYTES;
    }

    MBEDTLS_MPI_CHK(pka_session_begin(&hpka, &locked));

    in.expSize = (uint32_t) elen;
    in.OpSize = PKA_P256_BYTES;
    in.pOp1 = base;
    in.pExp = exp;
    in.pMod = mod;

    MBEDTLS_MPI_CHK((HAL_PKA_ModExp(&hpka, &in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);
    HAL_PKA_ModExp_GetResult(&hpka, out);
    MBEDTLS_MPI_CHK(be32_to_mpi(R, out));

cleanup:
    pka_session_end(&hpka, locked);
    return ret;
}

int pka_p256_modinv_mpi(mbedtls_mpi *R,
                        const mbedtls_mpi *A,
                        const mbedtls_mpi *N)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_ModInvInTypeDef in = {0};
    uint32_t op[PKA_P256_WORDS];
    uint32_t res[PKA_P256_WORDS];
    uint8_t mod[PKA_P256_BYTES];

    if (!pka_p256_mpi_fits(A) || !pka_p256_modulus_fits(N)) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    MBEDTLS_MPI_CHK(mpi_to_u32(A, op));
    MBEDTLS_MPI_CHK(mpi_to_be32(N, mod));

    MBEDTLS_MPI_CHK(pka_session_begin(&hpka, &locked));

    in.size = PKA_P256_WORDS;
    in.pOp1 = op;
    in.pMod = mod;

    MBEDTLS_MPI_CHK((HAL_PKA_ModInv(&hpka, &in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);
    HAL_PKA_Arithmetic_GetResult(&hpka, res);
    MBEDTLS_MPI_CHK(u32_to_mpi(R, res));

cleanup:
    pka_session_end(&hpka, locked);
    return ret;
}

int pka_p256_mulmod_mpi(mbedtls_mpi *R,
                        const mbedtls_mpi *A,
                        const mbedtls_mpi *B,
                        const mbedtls_mpi *N)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_MulInTypeDef mul_in = {0};
    PKA_ModRedInTypeDef red_in = {0};
    uint32_t a_w[PKA_P256_WORDS];
    uint32_t b_w[PKA_P256_WORDS];
    uint32_t prod[PKA_P256_WORDS * 2];
    uint32_t red_out[PKA_P256_WORDS];
    uint8_t mod[PKA_P256_BYTES];

    if (!pka_p256_mpi_fits(A) || !pka_p256_mpi_fits(B) || !pka_p256_modulus_fits(N)) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    MBEDTLS_MPI_CHK(mpi_to_u32(A, a_w));
    MBEDTLS_MPI_CHK(mpi_to_u32(B, b_w));
    MBEDTLS_MPI_CHK(mpi_to_be32(N, mod));

    MBEDTLS_MPI_CHK(pka_session_begin(&hpka, &locked));

    mul_in.size = PKA_P256_WORDS;
    mul_in.pOp1 = a_w;
    mul_in.pOp2 = b_w;
    MBEDTLS_MPI_CHK((HAL_PKA_Mul(&hpka, &mul_in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);
    HAL_PKA_Arithmetic_GetResult(&hpka, prod);

    red_in.OpSize = PKA_P256_WORDS * 2U;
    red_in.modSize = PKA_P256_BYTES;
    red_in.pOp1 = prod;
    red_in.pMod = mod;
    MBEDTLS_MPI_CHK((HAL_PKA_ModRed(&hpka, &red_in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);
    HAL_PKA_Arithmetic_GetResult(&hpka, red_out);
    MBEDTLS_MPI_CHK(u32_to_mpi(R, red_out));

cleanup:
    pka_session_end(&hpka, locked);
    return ret;
}

int pka_p256_ec_mul(mbedtls_ecp_group *grp,
                    mbedtls_ecp_point *R,
                    const mbedtls_mpi *k,
                    const mbedtls_ecp_point *P)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_ECCMulInTypeDef in = {0};
    PKA_ECCMulOutTypeDef out = {0};
    uint8_t p[PKA_P256_BYTES];
    uint8_t gx[PKA_P256_BYTES];
    uint8_t gy[PKA_P256_BYTES];
    uint8_t n[PKA_P256_BYTES];
    uint8_t b[PKA_P256_BYTES];
    uint8_t px[PKA_P256_BYTES];
    uint8_t py[PKA_P256_BYTES];
    uint8_t k_buf[PKA_P256_BYTES];
    uint8_t rx[PKA_P256_BYTES];
    uint8_t ry[PKA_P256_BYTES];

    if (grp == NULL || R == NULL || k == NULL || P == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (!pka_p256_mpi_fits(k)) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    if (P->MBEDTLS_PRIVATE(Z).MBEDTLS_PRIVATE(p) != NULL &&
        mbedtls_mpi_cmp_int(&P->MBEDTLS_PRIVATE(Z), 0) == 0) {
        return mbedtls_ecp_set_zero(R);
    }

    MBEDTLS_MPI_CHK(ec_group_to_hw_p256(grp, p, gx, gy, n, b));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(X), px));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(Y), py));
    MBEDTLS_MPI_CHK(mpi_to_be32(k, k_buf));

    MBEDTLS_MPI_CHK(pka_session_begin(&hpka, &locked));

    in.scalarMulSize = PKA_P256_BYTES;
    in.modulusSize = PKA_P256_BYTES;
    in.coefSign = PKA_ECC_COEF_SIGN_NEGATIVE;
    in.coefA = P256_A_ABS;
    in.coefB = b;
    in.modulus = p;
    in.pointX = px;
    in.pointY = py;
    in.scalarMul = k_buf;
    in.primeOrder = n;

    MBEDTLS_MPI_CHK((HAL_PKA_ECCMul(&hpka, &in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);

    out.ptX = rx;
    out.ptY = ry;
    HAL_PKA_ECCMul_GetResult(&hpka, &out);

    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(X), rx));
    MBEDTLS_MPI_CHK(be32_to_mpi(&R->MBEDTLS_PRIVATE(Y), ry));
    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(&R->MBEDTLS_PRIVATE(Z), 1));

cleanup:
    pka_session_end(&hpka, locked);
    return ret;
}

#if defined(MBEDTLS_ECDH_C)
int pka_p256_ecdh_gen_public(mbedtls_ecp_group *grp,
                             mbedtls_mpi *d,
                             mbedtls_ecp_point *Q,
                             int (*f_rng)(void *, unsigned char *, size_t),
                             void *p_rng)
{
    int ret;

    if (grp == NULL || d == NULL || Q == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    MBEDTLS_MPI_CHK(mbedtls_ecp_gen_privkey(grp, d, f_rng, p_rng));
    ret = pka_p256_ec_mul(grp, Q, d, &grp->G);

cleanup:
    return ret;
}

int pka_p256_ecdh_compute_shared(mbedtls_ecp_group *grp,
                                 mbedtls_mpi *z,
                                 const mbedtls_ecp_point *Q,
                                 const mbedtls_mpi *d)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_point P;

    if (grp == NULL || z == NULL || Q == NULL || d == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    mbedtls_ecp_point_init(&P);
    MBEDTLS_MPI_CHK(pka_p256_ec_mul(grp, &P, d, Q));
    MBEDTLS_MPI_CHK(mbedtls_mpi_copy(z, &P.MBEDTLS_PRIVATE(X)));

cleanup:
    mbedtls_ecp_point_free(&P);
    return ret;
}
#endif /* MBEDTLS_ECDH_C */

#if defined(MBEDTLS_ECDSA_C)
int pka_p256_ecdsa_can_do(mbedtls_ecp_group_id gid)
{
    return (gid == MBEDTLS_ECP_DP_SECP256R1) ? 1 : 0;
}

#if defined(MBEDTLS_ECDSA_SIGN_ALT)
static int hash_to_p256(const unsigned char *buf, size_t blen, uint8_t out[PKA_P256_BYTES])
{
    if (blen > PKA_P256_BYTES) {
        blen = PKA_P256_BYTES;
    }
    memset(out, 0, PKA_P256_BYTES);
    memcpy(out + (PKA_P256_BYTES - blen), buf, blen);
    return 0;
}

int pka_p256_ecdsa_sign(mbedtls_ecp_group *grp,
                        mbedtls_mpi *r,
                        mbedtls_mpi *s,
                        const mbedtls_mpi *d,
                        const unsigned char *buf,
                        size_t blen,
                        int (*f_rng)(void *, unsigned char *, size_t),
                        void *p_rng)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_ECDSASignInTypeDef in = {0};
    PKA_ECDSASignOutTypeDef out = {0};
    uint8_t d_buf[PKA_P256_BYTES];
    uint8_t hash[PKA_P256_BYTES];
    uint8_t k_buf[PKA_P256_BYTES];
    uint8_t p[PKA_P256_BYTES];
    uint8_t b[PKA_P256_BYTES];
    uint8_t gx[PKA_P256_BYTES];
    uint8_t gy[PKA_P256_BYTES];
    uint8_t n[PKA_P256_BYTES];
    uint8_t r_buf[PKA_P256_BYTES];
    uint8_t s_buf[PKA_P256_BYTES];
    mbedtls_mpi k;

    if (grp == NULL || r == NULL || s == NULL || d == NULL || buf == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    mbedtls_mpi_init(&k);

    MBEDTLS_MPI_CHK(mpi_to_be32(d, d_buf));
    MBEDTLS_MPI_CHK(hash_to_p256(buf, blen, hash));
    MBEDTLS_MPI_CHK(mbedtls_ecp_gen_privkey(grp, &k, f_rng, p_rng));
    MBEDTLS_MPI_CHK(mpi_to_be32(&k, k_buf));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->P, p, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->B, b, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.X, gx, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.Y, gy, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->N, n, PKA_P256_BYTES));

    MBEDTLS_MPI_CHK(pka_session_begin(&hpka, &locked));

    in.primeOrderSize = PKA_P256_BYTES;
    in.modulusSize = PKA_P256_BYTES;
    in.coefSign = PKA_ECC_COEF_SIGN_NEGATIVE;
    in.coef = P256_A_ABS;
    in.coefB = b;
    in.modulus = p;
    in.integer = k_buf;
    in.basePointX = gx;
    in.basePointY = gy;
    in.hash = hash;
    in.privateKey = d_buf;
    in.primeOrder = n;

    MBEDTLS_MPI_CHK((HAL_PKA_ECDSASign(&hpka, &in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);

    out.RSign = r_buf;
    out.SSign = s_buf;
    HAL_PKA_ECDSASign_GetResult(&hpka, &out, NULL);

    MBEDTLS_MPI_CHK(be32_to_mpi(r, r_buf));
    MBEDTLS_MPI_CHK(be32_to_mpi(s, s_buf));

cleanup:
    mbedtls_mpi_free(&k);
    pka_session_end(&hpka, locked);
    return ret;
}
#endif /* MBEDTLS_ECDSA_SIGN_ALT */

#if defined(MBEDTLS_ECDSA_VERIFY_ALT)
static int point_to_uncompressed(const mbedtls_ecp_point *P, uint8_t q[1 + 2 * PKA_P256_BYTES])
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    q[0] = 0x04;
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(X), q + 1));
    MBEDTLS_MPI_CHK(mpi_to_be32(&P->MBEDTLS_PRIVATE(Y), q + 1 + PKA_P256_BYTES));

cleanup:
    return ret;
}

int pka_p256_ecdsa_verify(mbedtls_ecp_group *grp,
                          const unsigned char *buf,
                          size_t blen,
                          const mbedtls_ecp_point *Q,
                          const mbedtls_mpi *r,
                          const mbedtls_mpi *s)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int locked = 0;
    PKA_HandleTypeDef hpka = {0};
    PKA_ECDSAVerifInTypeDef in = {0};
    uint8_t q_buf[1 + 2 * PKA_P256_BYTES];
    uint8_t hash[PKA_P256_BYTES];
    uint8_t r_buf[PKA_P256_BYTES];
    uint8_t s_buf[PKA_P256_BYTES];
    uint8_t p[PKA_P256_BYTES];
    uint8_t gx[PKA_P256_BYTES];
    uint8_t gy[PKA_P256_BYTES];
    uint8_t n[PKA_P256_BYTES];

    if (grp == NULL || buf == NULL || Q == NULL || r == NULL || s == NULL) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }
    if (grp->id != MBEDTLS_ECP_DP_SECP256R1) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    MBEDTLS_MPI_CHK(point_to_uncompressed(Q, q_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(r, r_buf));
    MBEDTLS_MPI_CHK(mpi_to_be32(s, s_buf));
    MBEDTLS_MPI_CHK(hash_to_p256(buf, blen, hash));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->P, p, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.X, gx, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->G.Y, gy, PKA_P256_BYTES));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&grp->N, n, PKA_P256_BYTES));

    MBEDTLS_MPI_CHK(pka_session_begin(&hpka, &locked));

    in.primeOrderSize = PKA_P256_BYTES;
    in.modulusSize = PKA_P256_BYTES;
    in.coefSign = PKA_ECC_COEF_SIGN_NEGATIVE;
    in.coef = P256_A_ABS;
    in.modulus = p;
    in.basePointX = gx;
    in.basePointY = gy;
    in.pPubKeyCurvePtX = q_buf + 1;
    in.pPubKeyCurvePtY = q_buf + 1 + PKA_P256_BYTES;
    in.RSign = r_buf;
    in.SSign = s_buf;
    in.hash = hash;
    in.primeOrder = n;

    MBEDTLS_MPI_CHK((HAL_PKA_ECDSAVerif(&hpka, &in, PKA_P256_TIMEOUT_MS) != HAL_OK) ?
                    MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0);

    if (HAL_PKA_ECDSAVerif_IsValidSignature(&hpka) != 1U) {
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;
    }

cleanup:
    pka_session_end(&hpka, locked);
    return ret;
}
#endif /* MBEDTLS_ECDSA_VERIFY_ALT */
#endif /* MBEDTLS_ECDSA_C */

#endif /* MBEDTLS_ECP_C && MBEDTLS_ECP_DP_SECP256R1_ENABLED */
