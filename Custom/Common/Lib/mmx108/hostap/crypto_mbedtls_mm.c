/*
 * crypto wrapper functions for mbed TLS
 *
 * Adapted from: https://github.com/gstrauss/hostap/blob/mbedtls/src/crypto/crypto_mbedtls.c
 * (revision 216d856f0a3e0702ec9134572a0ab5d3580a70e5).
 *
 * SPDX-FileCopyrightText: 2022 Glenn Strauss <gstrauss@gluelogic.com>
 * SPDX-FileCopyrightText: 2024 Morse Micro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <endian.h>

#include <mbedtls/aes.h>
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/bignum.h>
#include <mbedtls/cmac.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/md5.h>
#include <mbedtls/nist_kw.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/version.h>

#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
#include "pka_p256_alt.h"
#endif

/* Note: this header includes #defines to adjust the names of the global functions in this file
 * to be in the mmint_ namespace to avoid namespace conflicts. */
#include "hostap_morse_common.h"

#include "mmhal_core.h"
#include "mmutils.h"
#include "mmosal.h"

#pragma GCC diagnostic ignored "-Wc++-compat"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define SHA1_MAC_LEN (20)

static int ctr_drbg_init_state;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_entropy_context entropy;
static mbedtls_mpi mpi_sw_A;

#ifdef CRYPTO_MBEDTLS_DEBUG
#define wpa_printf(_lvl, ...) printf(__VA_ARGS__)
#else
#define wpa_printf(_lvl, ...) \
    do {                      \
    } while (0)
#endif

#ifndef MBEDTLS_SHA1_C
#error MBEDTLS_SHA1_C required
#endif

#ifndef MBEDTLS_SHA256_C
#error MBEDTLS_SHA256_C required
#endif

#ifndef MBEDTLS_SHA512_C
#error MBEDTLS_SHA512_C required
#endif

#if MBEDTLS_VERSION_NUMBER < 0x021B0000
#error mbedtls 2.27.0 or greater required
#endif

/*
 * The following code is extracted from src/utils/const_time.h
 *
 * Copyright (c) 2019, The Linux Foundation
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

/**
 * const_time_fill_msb - Fill all bits with MSB value
 * @val: Input value
 * Returns: Value with all the bits set to the MSB of the input val
 */
static inline unsigned int const_time_fill_msb(unsigned int val)
{
    /* Move the MSB to LSB and multiple by -1 to fill in all bits. */
    return (val >> (sizeof(val) * 8 - 1)) * ~0U;
}

/* Returns: -1 if val is zero; 0 if val is not zero */
static inline unsigned int const_time_is_zero(unsigned int val)
{
    /* Set MSB to 1 for 0 and fill rest of bits with the MSB value */
    return const_time_fill_msb(~val & (val - 1));
}

/* Returns: -1 if a == b; 0 if a != b */
static inline unsigned int const_time_eq(unsigned int a, unsigned int b)
{
    return const_time_is_zero(a ^ b);
}

/* Returns: -1 if a == b; 0 if a != b */
static inline uint8_t const_time_eq_u8(unsigned int a, unsigned int b)
{
    return (uint8_t)const_time_eq(a, b);
}

/**
 * const_time_select - Constant time unsigned int selection
 * @mask: 0 (false) or -1 (true) to identify which value to select
 * @true_val: Value to select for the true case
 * @false_val: Value to select for the false case
 * Returns: true_val if mask == -1, false_val if mask == 0
 */
static inline unsigned int const_time_select(unsigned int mask,
                                             unsigned int true_val,
                                             unsigned int false_val)
{
    return (mask & true_val) | (~mask & false_val);
}

/**
 * const_time_select_int - Constant time int selection
 * @mask: 0 (false) or -1 (true) to identify which value to select
 * @true_val: Value to select for the true case
 * @false_val: Value to select for the false case
 * Returns: true_val if mask == -1, false_val if mask == 0
 */
static inline int const_time_select_int(unsigned int mask, int true_val, int false_val)
{
    return (int)const_time_select(mask, (unsigned int)true_val, (unsigned int)false_val);
}

/* End of code extracted from src/utils/const_time.c */

/*
 * The following code is extracted from src/utils/wpabuf.h
 *
 * Copyright (c) 2007-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
struct wpabuf
{
    size_t size; /* total size of the allocated buffer */
    size_t used; /* length of data in the buffer */
    uint8_t *buf; /* pointer to the head of the buffer */
    unsigned int flags;
    /* optionally followed by the allocated buffer */
};

struct wpabuf *wpabuf_alloc(size_t len);

struct wpabuf *wpabuf_alloc_copy(const void *data, size_t len);

void *wpabuf_put(struct wpabuf *buf, size_t len);

void wpabuf_clear_free(struct wpabuf *buf);

static inline void *wpabuf_mhead(struct wpabuf *buf)
{
    return buf->buf;
}

/* End of code extracted from src/utils/wpabuf.c */

static int entropy_poll(void *user_arg, uint8_t *output, size_t len, size_t *out_len)
{
    uint32_t r;
    uint8_t *output_start = output;

    (void)user_arg;
    while (len != 0)
    {
        size_t cpylen = len < sizeof(uint32_t) ? len : sizeof(uint32_t);
        r = mmhal_random_u32(0, UINT32_MAX);
        memcpy(output, &r, cpylen);
        len -= cpylen;
        output += cpylen;
    }

    if (out_len)
    {
        *out_len = output - output_start;
    }

    return 0;
}

static mbedtls_ctr_drbg_context *ctr_drbg_init(void)
{
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    if (mbedtls_entropy_add_source(&entropy,
                                   entropy_poll,
                                   NULL,
                                   MBEDTLS_ENTROPY_MAX_GATHER,
                                   MBEDTLS_ENTROPY_SOURCE_STRONG))
    {
        wpa_printf(MSG_ERROR, "Entropy add failed");
    }
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0))
    {
        wpa_printf(MSG_ERROR, "Init of random number generator failed");
        /* XXX: abort? */
    }
    else
    {
        ctr_drbg_init_state = 1;
    }

    return &ctr_drbg;
}

void crypto_unload(void)
{
    if (ctr_drbg_init_state)
    {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_mpi_free(&mpi_sw_A);
        ctr_drbg_init_state = 0;
    }
}

/* init ctr_drbg on first use
 * crypto_global_init() and crypto_global_deinit() are not available here
 * (available only when CONFIG_TLS=internal, which is not CONFIG_TLS=mbedtls) */
static inline mbedtls_ctr_drbg_context *crypto_mbedtls_ctr_drbg(void)
{
    return ctr_drbg_init_state ? &ctr_drbg : ctr_drbg_init();
}

int crypto_get_random(void *buf, size_t len)
{
    return mbedtls_ctr_drbg_random(crypto_mbedtls_ctr_drbg(), buf, len) ? -1 : 0;
}

static int sha384_512_vector(size_t num_elem,
                             const uint8_t *addr[],
                             const size_t *len,
                             uint8_t *mac,
                             int is384)
{
    mbedtls_sha512_context ctx;
    mbedtls_sha512_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha512_starts(&ctx, is384);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_sha512_update(&ctx, addr[i], len[i]);
    }
    mbedtls_sha512_finish(&ctx, mac);
#else
    mbedtls_sha512_starts_ret(&ctx, is384);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_sha512_update_ret(&ctx, addr[i], len[i]);
    }
    mbedtls_sha512_finish_ret(&ctx, mac);
#endif
    mbedtls_sha512_free(&ctx);
    return 0;
}

int sha512_vector(size_t num_elem, const uint8_t *addr[], const size_t *len, uint8_t *mac)
{
    return sha384_512_vector(num_elem, addr, len, mac, 0);
}

int sha384_vector(size_t num_elem, const uint8_t *addr[], const size_t *len, uint8_t *mac)
{
    return sha384_512_vector(num_elem, addr, len, mac, 1);
}

int sha256_vector(size_t num_elem, const uint8_t *addr[], const size_t *len, uint8_t *mac)
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_starts(&ctx, 0);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_sha256_update(&ctx, addr[i], len[i]);
    }
    mbedtls_sha256_finish(&ctx, mac);
#else
    mbedtls_sha256_starts_ret(&ctx, 0);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_sha256_update_ret(&ctx, addr[i], len[i]);
    }
    mbedtls_sha256_finish_ret(&ctx, mac);
#endif
    mbedtls_sha256_free(&ctx);
    return 0;
}

int sha1_vector(size_t num_elem, const uint8_t *addr[], const size_t *len, uint8_t *mac)
{
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha1_starts(&ctx);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_sha1_update(&ctx, addr[i], len[i]);
    }
    mbedtls_sha1_finish(&ctx, mac);
#else
    mbedtls_sha1_starts_ret(&ctx);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_sha1_update_ret(&ctx, addr[i], len[i]);
    }
    mbedtls_sha1_finish_ret(&ctx, mac);
#endif
    mbedtls_sha1_free(&ctx);
    return 0;
}

static int hmac_vector(const uint8_t *key,
                       size_t key_len,
                       size_t num_elem,
                       const uint8_t *addr[],
                       const size_t *len,
                       uint8_t *mac,
                       mbedtls_md_type_t md_type)
{
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1) != 0)
    {
        mbedtls_md_free(&ctx);
        return -1;
    }
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    for (size_t i = 0; i < num_elem; ++i)
    {
        mbedtls_md_hmac_update(&ctx, addr[i], len[i]);
    }
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);
    return 0;
}

int hmac_sha512_vector(const uint8_t *key,
                       size_t key_len,
                       size_t num_elem,
                       const uint8_t *addr[],
                       const size_t *len,
                       uint8_t *mac)
{
    return hmac_vector(key, key_len, num_elem, addr, len, mac, MBEDTLS_MD_SHA512);
}

int hmac_sha512(const uint8_t *key,
                size_t key_len,
                const uint8_t *data,
                size_t data_len,
                uint8_t *mac)
{
    return hmac_vector(key, key_len, 1, &data, &data_len, mac, MBEDTLS_MD_SHA512);
}

int hmac_sha384_vector(const uint8_t *key,
                       size_t key_len,
                       size_t num_elem,
                       const uint8_t *addr[],
                       const size_t *len,
                       uint8_t *mac)
{
    return hmac_vector(key, key_len, num_elem, addr, len, mac, MBEDTLS_MD_SHA384);
}

int hmac_sha384(const uint8_t *key,
                size_t key_len,
                const uint8_t *data,
                size_t data_len,
                uint8_t *mac)
{
    return hmac_vector(key, key_len, 1, &data, &data_len, mac, MBEDTLS_MD_SHA384);
}

int hmac_sha256_vector(const uint8_t *key,
                       size_t key_len,
                       size_t num_elem,
                       const uint8_t *addr[],
                       const size_t *len,
                       uint8_t *mac)
{
    return hmac_vector(key, key_len, num_elem, addr, len, mac, MBEDTLS_MD_SHA256);
}

int hmac_sha256(const uint8_t *key,
                size_t key_len,
                const uint8_t *data,
                size_t data_len,
                uint8_t *mac)
{
    return hmac_vector(key, key_len, 1, &data, &data_len, mac, MBEDTLS_MD_SHA256);
}

int hmac_sha1_vector(const uint8_t *key,
                     size_t key_len,
                     size_t num_elem,
                     const uint8_t *addr[],
                     const size_t *len,
                     uint8_t *mac)
{
    return hmac_vector(key, key_len, num_elem, addr, len, mac, MBEDTLS_MD_SHA1);
}

int hmac_sha1(const uint8_t *key,
              size_t key_len,
              const uint8_t *data,
              size_t data_len,
              uint8_t *mac)
{
    return hmac_vector(key, key_len, 1, &data, &data_len, mac, MBEDTLS_MD_SHA1);
}

int hmac_md5(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *mac)
{
    /* Not needed for WPA3. */
    return -1;
}

/* sha256-prf.c sha384-prf.c sha512-prf.c */

/* hmac_prf_bits - IEEE Std 802.11ac-2013, 11.6.1.7.2 Key derivation function */
static int hmac_prf_bits(const uint8_t *key,
                         size_t key_len,
                         const char *label,
                         const uint8_t *data,
                         size_t data_len,
                         uint8_t *buf,
                         size_t buf_len_bits,
                         mbedtls_md_type_t md_type)
{
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    if (mbedtls_md_setup(&ctx, md_info, 1) != 0)
    {
        mbedtls_md_free(&ctx);
        return -1;
    }
    mbedtls_md_hmac_starts(&ctx, key, key_len);

    uint16_t ctr, n_le = htole16(buf_len_bits);
    const uint8_t *const addr[] = { (uint8_t *)&ctr, (uint8_t *)label, data, (uint8_t *)&n_le };
    const size_t len[] = { 2, strlen(label), data_len, 2 };
    const size_t mac_len = mbedtls_md_get_size(md_info);
    size_t buf_len = (buf_len_bits + 7) / 8;
    for (ctr = 1; buf_len >= mac_len; buf_len -= mac_len, ++ctr)
    {
#if __BYTE_ORDER == __BIG_ENDIAN
        ctr = htole16(ctr);
#endif
        for (size_t i = 0; i < MM_ARRAY_COUNT(addr); ++i)
        {
            mbedtls_md_hmac_update(&ctx, addr[i], len[i]);
        }
        mbedtls_md_hmac_finish(&ctx, buf);
        mbedtls_md_hmac_reset(&ctx);
        buf += mac_len;
#if __BYTE_ORDER == __BIG_ENDIAN
        ctr = le16toh(ctr);
#endif
    }

    if (buf_len)
    {
        uint8_t hash[MBEDTLS_MD_MAX_SIZE];
#if __BYTE_ORDER == __BIG_ENDIAN
        ctr = htole16(ctr);
#endif
        for (size_t i = 0; i < MM_ARRAY_COUNT(addr); ++i)
        {
            mbedtls_md_hmac_update(&ctx, addr[i], len[i]);
        }
        mbedtls_md_hmac_finish(&ctx, hash);
        memcpy(buf, hash, buf_len);
        buf += buf_len;
        mbedtls_platform_zeroize(hash, mac_len);
    }

    /* Mask out unused bits in last octet if it does not use all the bits */
    if ((buf_len_bits &= 0x7))
    {
        buf[-1] &= (uint8_t)(0xff << (8 - buf_len_bits));
    }

    mbedtls_md_free(&ctx);
    return 0;
}

int sha512_prf(const uint8_t *key,
               size_t key_len,
               const char *label,
               const uint8_t *data,
               size_t data_len,
               uint8_t *buf,
               size_t buf_len)
{
    return hmac_prf_bits(key, key_len, label, data, data_len, buf, buf_len * 8, MBEDTLS_MD_SHA512);
}

int sha384_prf(const uint8_t *key,
               size_t key_len,
               const char *label,
               const uint8_t *data,
               size_t data_len,
               uint8_t *buf,
               size_t buf_len)
{
    return hmac_prf_bits(key, key_len, label, data, data_len, buf, buf_len * 8, MBEDTLS_MD_SHA384);
}

int sha256_prf(const uint8_t *key,
               size_t key_len,
               const char *label,
               const uint8_t *data,
               size_t data_len,
               uint8_t *buf,
               size_t buf_len)
{
    return hmac_prf_bits(key, key_len, label, data, data_len, buf, buf_len * 8, MBEDTLS_MD_SHA256);
}

int sha256_prf_bits(const uint8_t *key,
                    size_t key_len,
                    const char *label,
                    const uint8_t *data,
                    size_t data_len,
                    uint8_t *buf,
                    size_t buf_len_bits)
{
    return hmac_prf_bits(key, key_len, label, data, data_len, buf, buf_len_bits, MBEDTLS_MD_SHA256);
}

/* sha1-prf.c */

/* sha1_prf - SHA1-based Pseudo-Random Function (PRF) (IEEE 802.11i, 8.5.1.1) */

int sha1_prf(const uint8_t *key,
             size_t key_len,
             const char *label,
             const uint8_t *data,
             size_t data_len,
             uint8_t *buf,
             size_t buf_len)
{
    /*(note: algorithm differs from hmac_prf_bits() */
    /*(note: smaller code size instead of expanding hmac_sha1_vector()
     * as is done in hmac_prf_bits(); not expecting large num of loops) */
    uint8_t counter = 0;
    const uint8_t *addr[] = { (uint8_t *)label, data, &counter };
    const size_t len[] = { strlen(label) + 1, data_len, 1 };

    for (; buf_len >= SHA1_MAC_LEN; buf_len -= SHA1_MAC_LEN, ++counter)
    {
        if (hmac_sha1_vector(key, key_len, 3, addr, len, buf))
        {
            return -1;
        }
        buf += SHA1_MAC_LEN;
    }

    if (buf_len)
    {
        uint8_t hash[SHA1_MAC_LEN];
        if (hmac_sha1_vector(key, key_len, 3, addr, len, hash))
        {
            return -1;
        }
        memcpy(buf, hash, buf_len);
        mbedtls_platform_zeroize(hash, sizeof(hash));
    }

    return 0;
}

int pbkdf2_sha1(const char *passphrase,
                const uint8_t *ssid,
                size_t ssid_len,
                int iterations,
                uint8_t *buf,
                size_t buflen)
{
#if MBEDTLS_VERSION_NUMBER >= 0x03030000 /* mbedtls 3.3.0 */
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA1,
                                         (const uint8_t *)passphrase,
                                         strlen(passphrase),
                                         ssid,
                                         ssid_len,
                                         iterations,
                                         32,
                                         buf) ?
               -1 :
               0;
#else
    const mbedtls_md_info_t *md_info;
    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == NULL)
    {
        return -1;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool error = mbedtls_md_setup(&ctx, md_info, 1) ||
                 mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                                           (const uint8_t *)passphrase,
                                           strlen(passphrase),
                                           ssid,
                                           ssid_len,
                                           iterations,
                                           32,
                                           buf);
    int ret = error ? -1 : 0;
    mbedtls_md_free(&ctx);
    return ret;
#endif
}

void *aes_decrypt_init(const uint8_t *key, size_t len)
{
    mbedtls_aes_context *aes = mmosal_malloc(sizeof(*aes));
    if (!aes)
    {
        return NULL;
    }

    mbedtls_aes_init(aes);
    if (mbedtls_aes_setkey_dec(aes, key, len * 8) == 0)
    {
        return aes;
    }

    mbedtls_aes_free(aes);
    mmosal_free(aes);
    return NULL;
}

int aes_decrypt(void *ctx, const uint8_t *crypt, uint8_t *plain)
{
    return mbedtls_aes_crypt_ecb(ctx, MBEDTLS_AES_DECRYPT, crypt, plain);
}

void aes_decrypt_deinit(void *ctx)
{
    mbedtls_aes_free(ctx);
    mmosal_free(ctx);
}

void *aes_encrypt_init(const uint8_t *key, size_t len)
{
    mbedtls_aes_context *aes = mmosal_malloc(sizeof(*aes));
    if (!aes)
    {
        return NULL;
    }

    mbedtls_aes_init(aes);
    if (mbedtls_aes_setkey_dec(aes, key, len * 8) == 0)
    {
        return aes;
    }

    mbedtls_aes_free(aes);
    mmosal_free(aes);
    return NULL;
}

int aes_encrypt(void *ctx, const uint8_t *crypt, uint8_t *plain)
{
    return mbedtls_aes_crypt_ecb(ctx, MBEDTLS_AES_ENCRYPT, crypt, plain);
}

void aes_encrypt_deinit(void *ctx)
{
    mbedtls_aes_free(ctx);
    mmosal_free(ctx);
}

int omac1_aes_vector(const uint8_t *key,
                     size_t key_len,
                     size_t num_elem,
                     const uint8_t *addr[],
                     const size_t *len,
                     uint8_t *mac)
{
    mbedtls_cipher_type_t cipher_type;
    switch (key_len)
    {
        case 16:
            cipher_type = MBEDTLS_CIPHER_AES_128_ECB;
            break;

        case 24:
            cipher_type = MBEDTLS_CIPHER_AES_192_ECB;
            break;

        case 32:
            cipher_type = MBEDTLS_CIPHER_AES_256_ECB;
            break;

        default:
            return -1;
    }
    const mbedtls_cipher_info_t *cipher_info;
    cipher_info = mbedtls_cipher_info_from_type(cipher_type);
    if (cipher_info == NULL)
    {
        return -1;
    }

    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    int ret = -1;
    if (mbedtls_cipher_setup(&ctx, cipher_info) == 0 &&
        mbedtls_cipher_cmac_starts(&ctx, key, key_len * 8) == 0)
    {
        ret = 0;
        for (size_t i = 0; i < num_elem && ret == 0; ++i)
        {
            ret = mbedtls_cipher_cmac_update(&ctx, addr[i], len[i]);
        }
    }
    if (ret == 0)
    {
        ret = mbedtls_cipher_cmac_finish(&ctx, mac);
    }
    mbedtls_cipher_free(&ctx);
    return ret ? -1 : 0;
}

int omac1_aes_128(const uint8_t *key, const uint8_t *data, size_t data_len, uint8_t *mac)
{
    return omac1_aes_vector(key, 16, 1, &data, &data_len, mac);
}

/* crypto.h bignum interfaces */

struct crypto_bignum *crypto_bignum_init(void)
{
    mbedtls_mpi *bn = mmosal_malloc(sizeof(*bn));
    if (bn)
    {
        mbedtls_mpi_init(bn);
    }
    return (struct crypto_bignum *)bn;
}

struct crypto_bignum *crypto_bignum_init_set(const uint8_t *buf, size_t len)
{
    mbedtls_mpi *bn = mmosal_malloc(sizeof(*bn));
    if (bn)
    {
        mbedtls_mpi_init(bn);
        if (mbedtls_mpi_read_binary(bn, buf, len) == 0)
        {
            return (struct crypto_bignum *)bn;
        }
    }

    mmosal_free(bn);
    return NULL;
}

struct crypto_bignum *crypto_bignum_init_uint(unsigned int val)
{
    mbedtls_mpi *bn = mmosal_malloc(sizeof(*bn));
    if (bn)
    {
        mbedtls_mpi_init(bn);
        if (mbedtls_mpi_lset(bn, (int)val) == 0)
        {
            return (struct crypto_bignum *)bn;
        }
    }

    mmosal_free(bn);
    return NULL;
}

void crypto_bignum_deinit(struct crypto_bignum *n, int clear)
{
    mbedtls_mpi_free((mbedtls_mpi *)n);
    mmosal_free(n);
}

int crypto_bignum_to_bin(const struct crypto_bignum *a, uint8_t *buf, size_t buflen, size_t padlen)
{
    size_t n = mbedtls_mpi_size((mbedtls_mpi *)a);
    if (n < padlen)
    {
        n = padlen;
    }
    bool error = (n > buflen || mbedtls_mpi_write_binary((mbedtls_mpi *)a, buf, n));
    return error ? -1 : (int)(n);
}

int crypto_bignum_rand(struct crypto_bignum *r, const struct crypto_bignum *m)
{
    /*assert(r != m);*/ /* r must not be same as m for mbedtls_mpi_random()*/
    return mbedtls_mpi_random((mbedtls_mpi *)r,
                              0,
                              (mbedtls_mpi *)m,
                              mbedtls_ctr_drbg_random,
                              crypto_mbedtls_ctr_drbg()) ?
               -1 :
               0;
}

int crypto_bignum_add(const struct crypto_bignum *a,
                      const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_add_mpi((mbedtls_mpi *)c, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ?
               -1 :
               0;
}

int crypto_bignum_mod(const struct crypto_bignum *a,
                      const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_mod_mpi((mbedtls_mpi *)c, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ?
               -1 :
               0;
}

int crypto_bignum_exptmod(const struct crypto_bignum *a,
                          const struct crypto_bignum *b,
                          const struct crypto_bignum *c,
                          struct crypto_bignum *d)
{
#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
    if (pka_p256_modulus_fits((const mbedtls_mpi *)c) &&
        pka_p256_mpi_fits((const mbedtls_mpi *)a) &&
        pka_p256_mpi_fits((const mbedtls_mpi *)b))
    {
        mbedtls_mpi R;
        int hw_rc = -1;

        if (b == d || c == d)
        {
            mbedtls_mpi_init(&R);
            if (pka_p256_modexp_mpi(&R,
                                    (const mbedtls_mpi *)a,
                                    (const mbedtls_mpi *)b,
                                    (const mbedtls_mpi *)c) == 0 &&
                mbedtls_mpi_copy((mbedtls_mpi *)d, &R) == 0)
            {
                hw_rc = 0;
            }
            mbedtls_mpi_free(&R);
        }
        else
        {
            if (pka_p256_modexp_mpi((mbedtls_mpi *)d,
                                    (const mbedtls_mpi *)a,
                                    (const mbedtls_mpi *)b,
                                    (const mbedtls_mpi *)c) == 0)
            {
                hw_rc = 0;
            }
        }

        if (hw_rc == 0)
        {
            return 0;
        }
    }
#endif

    /* (check if input params match d; d is the result) */
    /* (a == d) is ok in current mbedtls implementation */
    if (b == d || c == d) /*(not ok; store result in intermediate)*/
    {
        mbedtls_mpi R;
        mbedtls_mpi_init(&R);
        int rc = mbedtls_mpi_exp_mod(&R,
                                     (const mbedtls_mpi *)a,
                                     (const mbedtls_mpi *)b,
                                     (const mbedtls_mpi *)c,
                                     NULL) ||
                         mbedtls_mpi_copy((mbedtls_mpi *)d, &R) ?
                     -1 :
                     0;
        mbedtls_mpi_free(&R);
        return rc;
    }
    else
    {
        return mbedtls_mpi_exp_mod((mbedtls_mpi *)d,
                                   (const mbedtls_mpi *)a,
                                   (const mbedtls_mpi *)b,
                                   (const mbedtls_mpi *)c,
                                   NULL) ?
                   -1 :
                   0;
    }
}

int crypto_bignum_inverse(const struct crypto_bignum *a,
                          const struct crypto_bignum *b,
                          struct crypto_bignum *c)
{
#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
    if (pka_p256_modulus_fits((const mbedtls_mpi *)b) &&
        pka_p256_mpi_fits((const mbedtls_mpi *)a) &&
        pka_p256_modinv_mpi((mbedtls_mpi *)c,
                            (const mbedtls_mpi *)a,
                            (const mbedtls_mpi *)b) == 0)
    {
        return 0;
    }
#endif

    return mbedtls_mpi_inv_mod((mbedtls_mpi *)c, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ?
               -1 :
               0;
}

int crypto_bignum_sub(const struct crypto_bignum *a,
                      const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    return mbedtls_mpi_sub_mpi((mbedtls_mpi *)c, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ?
               -1 :
               0;
}

int crypto_bignum_div(const struct crypto_bignum *a,
                      const struct crypto_bignum *b,
                      struct crypto_bignum *c)
{
    /*(most current use of this crypto.h interface has a == c (result),
     * so store result in an intermediate to avoid overwritten input)*/
    mbedtls_mpi R;
    mbedtls_mpi_init(&R);
    int rc = mbedtls_mpi_div_mpi(&R, NULL, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ||
                     mbedtls_mpi_copy((mbedtls_mpi *)c, &R) ?
                 -1 :
                 0;
    mbedtls_mpi_free(&R);
    return rc;
}

int crypto_bignum_addmod(const struct crypto_bignum *a,
                         const struct crypto_bignum *b,
                         const struct crypto_bignum *c,
                         struct crypto_bignum *d)
{
    return mbedtls_mpi_add_mpi((mbedtls_mpi *)d, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ||
                   mbedtls_mpi_mod_mpi((mbedtls_mpi *)d, (mbedtls_mpi *)d, (const mbedtls_mpi *)c) ?
               -1 :
               0;
}

int crypto_bignum_mulmod(const struct crypto_bignum *a,
                         const struct crypto_bignum *b,
                         const struct crypto_bignum *c,
                         struct crypto_bignum *d)
{
#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
    if (pka_p256_modulus_fits((const mbedtls_mpi *)c) &&
        pka_p256_mpi_fits((const mbedtls_mpi *)a) &&
        pka_p256_mpi_fits((const mbedtls_mpi *)b) &&
        pka_p256_mulmod_mpi((mbedtls_mpi *)d,
                            (const mbedtls_mpi *)a,
                            (const mbedtls_mpi *)b,
                            (const mbedtls_mpi *)c) == 0)
    {
        return 0;
    }
#endif

    return mbedtls_mpi_mul_mpi((mbedtls_mpi *)d, (const mbedtls_mpi *)a, (const mbedtls_mpi *)b) ||
                   mbedtls_mpi_mod_mpi((mbedtls_mpi *)d, (mbedtls_mpi *)d, (const mbedtls_mpi *)c) ?
               -1 :
               0;
}

int crypto_bignum_sqrmod(const struct crypto_bignum *a,
                         const struct crypto_bignum *b,
                         struct crypto_bignum *c)
{
    return crypto_bignum_mulmod(a, a, b, c);
}

int crypto_bignum_rshift(const struct crypto_bignum *a, int n, struct crypto_bignum *r)
{
    return mbedtls_mpi_copy((mbedtls_mpi *)r, (const mbedtls_mpi *)a) ||
                   mbedtls_mpi_shift_r((mbedtls_mpi *)r, n) ?
               -1 :
               0;
}

int crypto_bignum_cmp(const struct crypto_bignum *a, const struct crypto_bignum *b)
{
    return mbedtls_mpi_cmp_mpi((const mbedtls_mpi *)a, (const mbedtls_mpi *)b);
}

int crypto_bignum_is_zero(const struct crypto_bignum *a)
{
#if 0
    /* XXX: src/common/sae.c:sswu() contains comment:
     * "TODO: Make sure crypto_bignum_is_zero() is constant time"
     * Note: mbedtls_mpi_cmp_int() *is not* constant time */
    return (mbedtls_mpi_cmp_int((const mbedtls_mpi *)a, 0) == 0);
#else
    /* XXX: some places in src/common/sae.c require constant time safety */
    /* Access n limbs, but more work needed if n limbs should not leak */
    const mbedtls_mpi_uint *p = ((const mbedtls_mpi *)a)->MBEDTLS_PRIVATE(p);
    size_t n = ((const mbedtls_mpi *)a)->MBEDTLS_PRIVATE(n);
    unsigned int v = 0;
    for (size_t i = 0; i < n; ++i)
    {
        v |= p[i];
    }
    return (v == 0);
#endif
}

int crypto_bignum_is_one(const struct crypto_bignum *a)
{
#if 0
    return (mbedtls_mpi_cmp_int((const mbedtls_mpi *)a, 1) == 0);
#else
    /* XXX: some places in src/common/sae.c require constant time safety */
    /* Access n limbs, but more work needed if n limbs should not leak */
    const mbedtls_mpi_uint *p = ((const mbedtls_mpi *)a)->MBEDTLS_PRIVATE(p);
    mbedtls_mpi_uint v = 0;
    size_t n = ((const mbedtls_mpi *)a)->MBEDTLS_PRIVATE(n);
    for (size_t i = 1; i < n; ++i)
    {
        v |= p[i];
    }
    return n ? ((p[0] == 1) & (v == 0)) : 0; /* n > 0 expected */
#endif
}

int crypto_bignum_is_odd(const struct crypto_bignum *a)
{
    return mbedtls_mpi_get_bit((const mbedtls_mpi *)a, 0);
}

int crypto_bignum_legendre(const struct crypto_bignum *a, const struct crypto_bignum *p)
{
    /* Security Note:
     * mbedtls_mpi_exp_mod() is not documented to run in constant time,
     * though mbedtls/library/bignum.c uses constant_time_internal.h funcs.
     * Compare to crypto_openssl.c:crypto_bignum_legendre()
     * which uses openssl BN_mod_exp_mont_consttime()
     * mbedtls/library/ecp.c has further countermeasures to timing attacks,
     * (but ecp.c funcs are not used here) */

    mbedtls_mpi exp, tmp;
    mbedtls_mpi_init(&exp);
    mbedtls_mpi_init(&tmp);

    /* exp = (p-1) / 2 */
    int res;
    if (mbedtls_mpi_sub_int(&exp, (const mbedtls_mpi *)p, 1) == 0 &&
        mbedtls_mpi_shift_r(&exp, 1) == 0 &&
        mbedtls_mpi_exp_mod(&tmp, (const mbedtls_mpi *)a, &exp, (const mbedtls_mpi *)p, NULL) == 0)
    {
        /*(modified from crypto_openssl.c:crypto_bignum_legendre())*/
        /* Return 1 if tmp == 1, 0 if tmp == 0, or -1 otherwise. Need
         * to use constant time selection to avoid branches here. */
        unsigned int mask;
        res = -1;
        mask = const_time_eq(crypto_bignum_is_one((struct crypto_bignum *)&tmp), 1);
        res = const_time_select_int(mask, 1, res);
        mask = const_time_eq(crypto_bignum_is_zero((struct crypto_bignum *)&tmp), 1);
        res = const_time_select_int(mask, 0, res);
    }
    else
    {
        res = -2;
    }

    mbedtls_mpi_free(&tmp);
    mbedtls_mpi_free(&exp);
    return res;
}

#define CRYPTO_EC_pbits(e) (((const mbedtls_ecp_group *)(e))->pbits)
#define CRYPTO_EC_plen(e)  ((((const mbedtls_ecp_group *)(e))->pbits + 7) >> 3)
#define CRYPTO_EC_P(e)     (&((const mbedtls_ecp_group *)(e))->P)
#define CRYPTO_EC_N(e)     (&((const mbedtls_ecp_group *)(e))->N)
#define CRYPTO_EC_A(e)     (&((const mbedtls_ecp_group *)(e))->A)
#define CRYPTO_EC_B(e)     (&((const mbedtls_ecp_group *)(e))->B)
#define CRYPTO_EC_G(e)     (&((const mbedtls_ecp_group *)(e))->G)

#define ECP_KP_grp(kp)     ((const mbedtls_ecp_group *)&(kp)->MBEDTLS_PRIVATE(grp))
#define ECP_KP_Q(kp)       ((const mbedtls_ecp_point *)&(kp)->MBEDTLS_PRIVATE(Q))
#define ECP_KP_d(kp)       ((const mbedtls_mpi *)&(kp)->MBEDTLS_PRIVATE(d))

struct crypto_ec_key;

static mbedtls_ecp_group_id crypto_mbedtls_ecp_group_id_from_ike_id(int group)
{
    /* https://www.iana.org/assignments/ikev2-parameters/ikev2-parameters.xhtml */
    switch (group)
    {
#ifdef MBEDTLS_ECP_DP_SECP256R1_ENABLED
        case 19:
            return MBEDTLS_ECP_DP_SECP256R1;

#endif
#ifdef MBEDTLS_ECP_DP_SECP384R1_ENABLED
        case 20:
            return MBEDTLS_ECP_DP_SECP384R1;

#endif
#ifdef MBEDTLS_ECP_DP_SECP521R1_ENABLED
        case 21:
            return MBEDTLS_ECP_DP_SECP521R1;

#endif
#ifdef MBEDTLS_ECP_DP_SECP192R1_ENABLED
        case 25:
            return MBEDTLS_ECP_DP_SECP192R1;

#endif
#ifdef MBEDTLS_ECP_DP_SECP224R1_ENABLED
        case 26:
            return MBEDTLS_ECP_DP_SECP224R1;

#endif
#ifdef MBEDTLS_ECP_DP_BP256R1_ENABLED
        case 28:
            return MBEDTLS_ECP_DP_BP256R1;

#endif
#ifdef MBEDTLS_ECP_DP_BP384R1_ENABLED
        case 29:
            return MBEDTLS_ECP_DP_BP384R1;

#endif
#ifdef MBEDTLS_ECP_DP_BP512R1_ENABLED
        case 30:
            return MBEDTLS_ECP_DP_BP512R1;

#endif
#ifdef MBEDTLS_ECP_DP_CURVE25519_ENABLED
        case 31:
            return MBEDTLS_ECP_DP_CURVE25519;

#endif
#ifdef MBEDTLS_ECP_DP_CURVE448_ENABLED
        case 32:
            return MBEDTLS_ECP_DP_CURVE448;

#endif
        default:
            return MBEDTLS_ECP_DP_NONE;
    }
}

static int crypto_mbedtls_keypair_gen(int group, mbedtls_pk_context *pk)
{
    mbedtls_ecp_group_id grp_id = crypto_mbedtls_ecp_group_id_from_ike_id(group);
    if (grp_id == MBEDTLS_ECP_DP_NONE)
    {
        return -1;
    }
    const mbedtls_pk_info_t *pk_info = mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);
    if (pk_info == NULL)
    {
        return -1;
    }
    return mbedtls_pk_setup(pk, pk_info) || mbedtls_ecp_gen_key(grp_id,
                                                                mbedtls_pk_ec(*pk),
                                                                mbedtls_ctr_drbg_random,
                                                                crypto_mbedtls_ctr_drbg()) ?
               -1 :
               0;
}

/* wrap mbedtls_ecdh_context for more future-proof direct access to components
 * (mbedtls_ecdh_context internal implementation may change between releases)
 *
 * If mbedtls_pk_context -- specifically underlying mbedtls_ecp_keypair --
 * lifetime were guaranteed to be longer than that of mbedtls_ecdh_context,
 * then mbedtls_pk_context or mbedtls_ecp_keypair could be stored in crypto_ecdh
 * (or crypto_ec_key could be stored in crypto_ecdh, and crypto_ec_key could
 *  wrap mbedtls_ecp_keypair and components, to avoid MBEDTLS_PRIVATE access) */
struct crypto_ecdh
{
    mbedtls_ecdh_context ctx;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
};

struct crypto_ecdh *crypto_ecdh_init2(int group, struct crypto_ec_key *own_key)
{
    mbedtls_ecp_group_id grp_id = crypto_mbedtls_ecp_group_id_from_ike_id(group);
    if (grp_id == MBEDTLS_ECP_DP_NONE)
    {
        return NULL;
    }
    mbedtls_ecp_keypair *ecp_kp = mbedtls_pk_ec(*(mbedtls_pk_context *)own_key);
    struct crypto_ecdh *ecdh = mmosal_malloc(sizeof(*ecdh));
    if (ecdh == NULL)
    {
        return NULL;
    }
    mbedtls_ecdh_init(&ecdh->ctx);
    mbedtls_ecp_group_init(&ecdh->grp);
    mbedtls_ecp_point_init(&ecdh->Q);
    if (mbedtls_ecdh_setup(&ecdh->ctx, grp_id) == 0 &&
        mbedtls_ecdh_get_params(&ecdh->ctx, ecp_kp, MBEDTLS_ECDH_OURS) == 0)
    {
        /* copy grp and Q for later use
         * (retrieving this info later is more convoluted
         *  even if mbedtls_ecdh_make_public() is considered)*/
#if MBEDTLS_VERSION_NUMBER >= 0x03020000 /* mbedtls 3.2.0 */
        mbedtls_mpi d;
        mbedtls_mpi_init(&d);
        if (mbedtls_ecp_export(ecp_kp, &ecdh->grp, &d, &ecdh->Q) == 0)
        {
            mbedtls_mpi_free(&d);
            return ecdh;
        }
        mbedtls_mpi_free(&d);
#else
        if (mbedtls_ecp_group_load(&ecdh->grp, grp_id) == 0 &&
            mbedtls_ecp_copy(&ecdh->Q, ECP_KP_Q(ecp_kp)) == 0)
        {
            return ecdh;
        }
#endif
    }

    mbedtls_ecp_point_free(&ecdh->Q);
    mbedtls_ecp_group_free(&ecdh->grp);
    mbedtls_ecdh_free(&ecdh->ctx);
    mmosal_free(ecdh);
    return NULL;
}

struct crypto_ecdh *crypto_ecdh_init(int group)
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    struct crypto_ecdh *ecdh = crypto_mbedtls_keypair_gen(group, &pk) == 0 ?
                                   crypto_ecdh_init2(group, (struct crypto_ec_key *)&pk) :
                                   NULL;
    mbedtls_pk_free(&pk);
    return ecdh;
}

struct wpabuf *crypto_ecdh_get_pubkey(struct crypto_ecdh *ecdh, int inc_y)
{
    mbedtls_ecp_group *grp = &ecdh->grp;
    size_t len;
    uint8_t buf[256];
    inc_y = inc_y ? MBEDTLS_ECP_PF_UNCOMPRESSED : MBEDTLS_ECP_PF_COMPRESSED;
    if (mbedtls_ecp_point_write_binary(grp, &ecdh->Q, inc_y, &len, buf, sizeof(buf)) == 0)
    {
#ifdef MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED
        if (mbedtls_ecp_get_type(grp) == MBEDTLS_ECP_TYPE_SHORT_WEIERSTRASS)
        {
            /* omit leading tag byte in return value */
            return wpabuf_alloc_copy(buf + 1, len - 1);
        }
#endif
        return wpabuf_alloc_copy(buf, len);
    }

    return NULL;
}

#if MBEDTLS_VERSION_NUMBER < 0x03040000 /* mbedtls 3.4.0 */
#if defined(MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED)
static int crypto_mbedtls_short_weierstrass_derive_y(mbedtls_ecp_group *grp,
                                                     mbedtls_mpi *bn,
                                                     int parity_bit)
{
    /* y^2 = x^3 + ax + b
     * sqrt(w) = w^((p+1)/4) mod p   (for prime p where p = 3 mod 4) */
    mbedtls_mpi *cy2 =
        (mbedtls_mpi *)crypto_ec_point_compute_y_sqr((struct crypto_ec *)grp,
                                                     (const struct crypto_bignum *)bn); /*x*/
    if (cy2 == NULL)
    {
        return -1;
    }

    /*mbedtls_mpi_free(bn);*/
    /*(reuse bn to store result (y))*/

    mbedtls_mpi exp;
    mbedtls_mpi_init(&exp);
    int ret = mbedtls_mpi_get_bit(&grp->P, 0) != 1 || /*(p = 3 mod 4)*/
              mbedtls_mpi_get_bit(&grp->P, 1) != 1 || /*(p = 3 mod 4)*/
              mbedtls_mpi_add_int(&exp, &grp->P, 1) ||
              mbedtls_mpi_shift_r(&exp, 2) ||
              mbedtls_mpi_exp_mod(bn, cy2, &exp, &grp->P, NULL) ||
              (mbedtls_mpi_get_bit(bn, 0) != parity_bit && mbedtls_mpi_sub_mpi(bn, &grp->P, bn));
    mbedtls_mpi_free(&exp);
    mbedtls_mpi_free(cy2);
    mmosal_free(cy2);
    return ret;
}

#endif
#endif

struct wpabuf *crypto_ecdh_set_peerkey(struct crypto_ecdh *ecdh,
                                       int inc_y,
                                       const uint8_t *key,
                                       size_t len)
{
    if (len == 0) /*(invalid peer key)*/
    {
        return NULL;
    }

    mbedtls_ecp_group *grp = &ecdh->grp;

#if defined(MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED)
    if (mbedtls_ecp_get_type(grp) == MBEDTLS_ECP_TYPE_SHORT_WEIERSTRASS)
    {
        /* add header for mbedtls_ecdh_read_public() */
        uint8_t buf[256];
        if (sizeof(buf) - 2 < len)
        {
            return NULL;
        }
        buf[0] = (uint8_t)(1 + len);
        buf[1] = 0x04;
        memcpy(buf + 2, key, len);
        if (inc_y)
        {
            len >>= 1; /*(repurpose len to prime_len)*/
        }
        else
        {
#if MBEDTLS_VERSION_NUMBER >= 0x03040000 /* mbedtls 3.4.0 */
            buf[1] = 0x02; /*(assume 0x02 vs 0x03; same as derive y below)*/
#else
            /* mbedtls_ecp_point_read_binary() does not currently support
             * MBEDTLS_ECP_PF_COMPRESSED format (buf[1] = 0x02 or 0x03)
             * (returns MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE) */

            /* derive y, amend buf[] with y for UNCOMPRESSED format */
            if (sizeof(buf) - 2 < len * 2)
            {
                return NULL;
            }

            mbedtls_mpi bn;
            mbedtls_mpi_init(&bn);
            int ret = mbedtls_mpi_read_binary(&bn, key, len) ||
                      crypto_mbedtls_short_weierstrass_derive_y(grp, &bn, 0) ||
                      mbedtls_mpi_write_binary(&bn, buf + 2 + len, len);
            mbedtls_mpi_free(&bn);
            if (ret != 0)
            {
                return NULL;
            }
            buf[0] += (uint8_t)len;
#endif
        }

        if (mbedtls_ecdh_read_public(&ecdh->ctx, buf, buf[0] + 1))
        {
            return NULL;
        }
    }
#endif
#if defined(MBEDTLS_ECP_MONTGOMERY_ENABLED)
    if (mbedtls_ecp_get_type(grp) == MBEDTLS_ECP_TYPE_MONTGOMERY)
    {
        if (mbedtls_ecdh_read_public(&ecdh->ctx, key, len))
        {
            return NULL;
        }
    }
#endif

    struct wpabuf *buf = wpabuf_alloc(len);
    if (buf == NULL)
    {
        return NULL;
    }

    if (mbedtls_ecdh_calc_secret(&ecdh->ctx,
                                 &len,
                                 wpabuf_mhead(buf),
                                 len,
                                 mbedtls_ctr_drbg_random,
                                 crypto_mbedtls_ctr_drbg()) == 0)
    {
        wpabuf_put(buf, len);
        return buf;
    }

    wpabuf_clear_free(buf);
    return NULL;
}

void crypto_ecdh_deinit(struct crypto_ecdh *ecdh)
{
    if (ecdh == NULL)
    {
        return;
    }
    mbedtls_ecp_point_free(&ecdh->Q);
    mbedtls_ecp_group_free(&ecdh->grp);
    mbedtls_ecdh_free(&ecdh->ctx);
    mmosal_free(ecdh);
}

size_t crypto_ecdh_prime_len(struct crypto_ecdh *ecdh)
{
    return CRYPTO_EC_plen(&ecdh->grp);
}

struct crypto_ec *crypto_ec_init(int group)
{
    mbedtls_ecp_group_id grp_id = crypto_mbedtls_ecp_group_id_from_ike_id(group);
    if (grp_id == MBEDTLS_ECP_DP_NONE)
    {
        return NULL;
    }
    mbedtls_ecp_group *e = mmosal_malloc(sizeof(*e));
    if (e == NULL)
    {
        return NULL;
    }
    mbedtls_ecp_group_init(e);
    if (mbedtls_ecp_group_load(e, grp_id) == 0)
    {
        return (struct crypto_ec *)e;
    }

    mbedtls_ecp_group_free(e);
    mmosal_free(e);
    return NULL;
}

void crypto_ec_deinit(struct crypto_ec *e)
{
    mbedtls_ecp_group_free((mbedtls_ecp_group *)e);
    mmosal_free(e);
}

size_t crypto_ec_prime_len(struct crypto_ec *e)
{
    return CRYPTO_EC_plen(e);
}

size_t crypto_ec_prime_len_bits(struct crypto_ec *e)
{
    return CRYPTO_EC_pbits(e);
}

size_t crypto_ec_order_len(struct crypto_ec *e)
{
    return (mbedtls_mpi_bitlen(CRYPTO_EC_N(e)) + 7) / 8;
}

const struct crypto_bignum *crypto_ec_get_prime(struct crypto_ec *e)
{
    return (const struct crypto_bignum *)CRYPTO_EC_P(e);
}

const struct crypto_bignum *crypto_ec_get_order(struct crypto_ec *e)
{
    return (const struct crypto_bignum *)CRYPTO_EC_N(e);
}

const struct crypto_bignum *crypto_ec_get_a(struct crypto_ec *e)
{
    static const uint8_t secp256r1_a[] = { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc };
    static const uint8_t secp384r1_a[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
                                           0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xfc };
    static const uint8_t secp521r1_a[] = {
        0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc
    };

    const uint8_t *bin = NULL;
    size_t len = 0;

    /* (mbedtls groups matching supported sswu_curve_param() IKE groups) */
    switch (((mbedtls_ecp_group *)e)->id)
    {
#ifdef MBEDTLS_ECP_DP_SECP256R1_ENABLED
        case MBEDTLS_ECP_DP_SECP256R1:
            bin = secp256r1_a;
            len = sizeof(secp256r1_a);
            break;

#endif
#ifdef MBEDTLS_ECP_DP_SECP384R1_ENABLED
        case MBEDTLS_ECP_DP_SECP384R1:
            bin = secp384r1_a;
            len = sizeof(secp384r1_a);
            break;

#endif
#ifdef MBEDTLS_ECP_DP_SECP521R1_ENABLED
        case MBEDTLS_ECP_DP_SECP521R1:
            bin = secp521r1_a;
            len = sizeof(secp521r1_a);
            break;

#endif
#ifdef MBEDTLS_ECP_DP_BP256R1_ENABLED
        case MBEDTLS_ECP_DP_BP256R1:
            return (const struct crypto_bignum *)CRYPTO_EC_A(e);

#endif
#ifdef MBEDTLS_ECP_DP_BP384R1_ENABLED
        case MBEDTLS_ECP_DP_BP384R1:
            return (const struct crypto_bignum *)CRYPTO_EC_A(e);

#endif
#ifdef MBEDTLS_ECP_DP_BP512R1_ENABLED
        case MBEDTLS_ECP_DP_BP512R1:
            return (const struct crypto_bignum *)CRYPTO_EC_A(e);

#endif
#ifdef MBEDTLS_ECP_DP_CURVE25519_ENABLED
        case MBEDTLS_ECP_DP_CURVE25519:
            return (const struct crypto_bignum *)CRYPTO_EC_A(e);

#endif
#ifdef MBEDTLS_ECP_DP_CURVE448_ENABLED
        case MBEDTLS_ECP_DP_CURVE448:
            return (const struct crypto_bignum *)CRYPTO_EC_A(e);

#endif
        default:
            return NULL;
    }

    /*(note: not thread-safe; returns file-scoped static storage)*/
    if (mbedtls_mpi_read_binary(&mpi_sw_A, bin, len) == 0)
    {
        return (const struct crypto_bignum *)&mpi_sw_A;
    }
    return NULL;
}

const struct crypto_bignum *crypto_ec_get_b(struct crypto_ec *e)
{
    return (const struct crypto_bignum *)CRYPTO_EC_B(e);
}

const struct crypto_ec_point *crypto_ec_get_generator(struct crypto_ec *e)
{
    return (const struct crypto_ec_point *)CRYPTO_EC_G(e);
}

struct crypto_ec_point *crypto_ec_point_init(struct crypto_ec *e)
{
    mbedtls_ecp_point *p = mmosal_malloc(sizeof(*p));
    if (p != NULL)
    {
        mbedtls_ecp_point_init(p);
    }
    return (struct crypto_ec_point *)p;
}

void crypto_ec_point_deinit(struct crypto_ec_point *p, int clear)
{
    mbedtls_ecp_point_free((mbedtls_ecp_point *)p);
    mmosal_free(p);
}

int crypto_ec_point_x(struct crypto_ec *e, const struct crypto_ec_point *p, struct crypto_bignum *x)
{
    /* future: to avoid MBEDTLS_PRIVATE(), could write pt to stack buf,
     *         copy x, then wipe stack buf */

    mbedtls_mpi *px = &((mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(X);
    return mbedtls_mpi_copy((mbedtls_mpi *)x, px) ? -1 : 0;
}

int crypto_ec_point_to_bin(struct crypto_ec *e,
                           const struct crypto_ec_point *point,
                           uint8_t *x,
                           uint8_t *y)
{
    /* future: to avoid MBEDTLS_PRIVATE(), could write pt to stack buf,
     *         copy x and y, then wipe stack buf */

    /* crypto.h documents crypto_ec_point_to_bin() output is big-endian */
    size_t len = CRYPTO_EC_plen(e);
    if (x)
    {
        mbedtls_mpi *px = &((mbedtls_ecp_point *)point)->MBEDTLS_PRIVATE(X);
        if (mbedtls_mpi_write_binary(px, x, len))
        {
            return -1;
        }
    }
    if (y)
    {
#if 0 /*(should not be necessary; py mpi should be in initial state)*/
#ifdef MBEDTLS_ECP_MONTGOMERY_ENABLED
        if (mbedtls_ecp_get_type((mbedtls_ecp_group *)e) ==
            MBEDTLS_ECP_TYPE_MONTGOMERY)
        {
            memset(y, 0, len);
            return 0;
        }
#endif
#endif
        mbedtls_mpi *py = &((mbedtls_ecp_point *)point)->MBEDTLS_PRIVATE(Y);
        if (mbedtls_mpi_write_binary(py, y, len))
        {
            return -1;
        }
    }
    return 0;
}

struct crypto_ec_point *crypto_ec_point_from_bin(struct crypto_ec *e, const uint8_t *val)
{
    size_t len = CRYPTO_EC_plen(e);
    mbedtls_ecp_point *p = mmosal_malloc(sizeof(*p));
    uint8_t buf[1 + MBEDTLS_MPI_MAX_SIZE * 2];
    if (p == NULL)
    {
        return NULL;
    }
    mbedtls_ecp_point_init(p);

#ifdef MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED
    if (mbedtls_ecp_get_type((mbedtls_ecp_group *)e) == MBEDTLS_ECP_TYPE_SHORT_WEIERSTRASS)
    {
#if 0 /* prefer alternative to MBEDTLS_PRIVATE() access */
        mbedtls_mpi *px = &((mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(X);
        mbedtls_mpi *py = &((mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(Y);
        mbedtls_mpi *pz = &((mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(Z);

        if (mbedtls_mpi_read_binary(px, val, len) == 0 &&
            mbedtls_mpi_read_binary(py, val + len, len) == 0 &&
            mbedtls_mpi_lset(pz, 1) == 0)
        {
            return (struct crypto_ec_point *)p;
        }
#else
        buf[0] = 0x04;
        memcpy(buf + 1, val, len * 2);
        if (mbedtls_ecp_point_read_binary((mbedtls_ecp_group *)e, p, buf, 1 + len * 2) == 0)
        {
            return (struct crypto_ec_point *)p;
        }
#endif
    }
#endif
#ifdef MBEDTLS_ECP_MONTGOMERY_ENABLED
    if (mbedtls_ecp_get_type((mbedtls_ecp_group *)e) == MBEDTLS_ECP_TYPE_MONTGOMERY)
    {
        /* crypto.h interface documents crypto_ec_point_from_bin()
         * val is length: prime_len * 2 and is big-endian
         * (Short Weierstrass is assumed by hostap)
         * Reverse to little-endian format for Montgomery */
        for (unsigned int i = 0; i < len; ++i)
        {
            buf[i] = val[len - 1 - i];
        }
        if (mbedtls_ecp_point_read_binary((mbedtls_ecp_group *)e, p, buf, len) == 0)
        {
            return (struct crypto_ec_point *)p;
        }
    }
#endif

    mbedtls_ecp_point_free(p);
    mmosal_free(p);
    return NULL;
}

int crypto_ec_point_add(struct crypto_ec *e,
                        const struct crypto_ec_point *a,
                        const struct crypto_ec_point *b,
                        struct crypto_ec_point *c)
{
    /* mbedtls does not provide an mbedtls_ecp_point add function */
    mbedtls_mpi one;
    mbedtls_mpi_init(&one);
    int ret = mbedtls_mpi_lset(&one, 1) || mbedtls_ecp_muladd((mbedtls_ecp_group *)e,
                                                              (mbedtls_ecp_point *)c,
                                                              &one,
                                                              (const mbedtls_ecp_point *)a,
                                                              &one,
                                                              (const mbedtls_ecp_point *)b) ?
                  -1 :
                  0;
    mbedtls_mpi_free(&one);
    return ret;
}

int crypto_ec_point_mul(struct crypto_ec *e,
                        const struct crypto_ec_point *p,
                        const struct crypto_bignum *b,
                        struct crypto_ec_point *res)
{
#if defined(MBEDTLS_ECP_DP_SECP256R1_ENABLED)
    if (((const mbedtls_ecp_group *)e)->id == MBEDTLS_ECP_DP_SECP256R1 &&
        pka_p256_mpi_fits((const mbedtls_mpi *)b) &&
        pka_p256_ec_mul((mbedtls_ecp_group *)e,
                        (mbedtls_ecp_point *)res,
                        (const mbedtls_mpi *)b,
                        (const mbedtls_ecp_point *)p) == 0)
    {
        return 0;
    }
#endif

    return mbedtls_ecp_mul((mbedtls_ecp_group *)e,
                           (mbedtls_ecp_point *)res,
                           (const mbedtls_mpi *)b,
                           (const mbedtls_ecp_point *)p,
                           mbedtls_ctr_drbg_random,
                           crypto_mbedtls_ctr_drbg()) ?
               -1 :
               0;
}

int crypto_ec_point_invert(struct crypto_ec *e, struct crypto_ec_point *p)
{
    if (mbedtls_ecp_get_type((mbedtls_ecp_group *)e) == MBEDTLS_ECP_TYPE_MONTGOMERY)
    {
        /* e.g. MBEDTLS_ECP_DP_CURVE25519 and MBEDTLS_ECP_DP_CURVE448 */
        wpa_printf(MSG_ERROR, "%s not implemented for Montgomery curves", __func__);
        return -1;
    }

    /* mbedtls does not provide an mbedtls_ecp_point invert function */
    /* below works for Short Weierstrass; incorrect for Montgomery curves */
    mbedtls_mpi *py = &((mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(Y);
    return mbedtls_ecp_is_zero((mbedtls_ecp_point *)p) || /*point at infinity*/
                   mbedtls_mpi_cmp_int(py, 0) == 0 || /*point is its own inverse*/
                   mbedtls_mpi_sub_abs(py, CRYPTO_EC_P(e), py) == 0 ?
               0 :
               -1;
}

#ifdef MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED
static int crypto_ec_point_y_sqr_weierstrass(const mbedtls_ecp_group *e,
                                             const mbedtls_mpi *x,
                                             mbedtls_mpi *y2)
{
    /* MBEDTLS_ECP_TYPE_SHORT_WEIERSTRASS  y^2 = x^3 + a x + b    */

    /* Short Weierstrass elliptic curve group w/o A set treated as A = -3 */
    /* Attempt to match mbedtls/library/ecp.c:ecp_check_pubkey_sw() behavior
     * and elsewhere in mbedtls/library/ecp.c where if A is not set, it is
     * treated as if A = -3. */

#if 0
    /* y^2 = x^3 + ax + b */
    mbedtls_mpi *A = &e->A;
    mbedtls_mpi t, A_neg3;
    if (&e->A.p == NULL)
    {
        mbedtls_mpi_init(&A_neg3);
        if (mbedtls_mpi_lset(&A_neg3, -3) != 0)
        {
            mbedtls_mpi_free(&A_neg3);
            return -1;
        }
        A = &A_neg3;
    }
    mbedtls_mpi_init(&t);
    int ret = /* x^3 */
        mbedtls_mpi_lset(&t, 3) ||
        mbedtls_mpi_exp_mod(y2, x, &t, &e->P, NULL)
        /* ax */
        || mbedtls_mpi_mul_mpi(y2, y2, A) ||
        mbedtls_mpi_mod_mpi(&t, &t, &e->P)
        /* ax + b */
        || mbedtls_mpi_add_mpi(&t, &t, &e->B) ||
        mbedtls_mpi_mod_mpi(&t, &t, &e->P)
        /* x^3 + ax + b */
        || mbedtls_mpi_add_mpi(&t, &t, y2) || /* ax + b + x^3 */
        mbedtls_mpi_mod_mpi(y2, &t, &e->P);
    mbedtls_mpi_free(&t);
    if (A == &A_neg3)
    {
        mbedtls_mpi_free(&A_neg3);
    }
    return ret; /* 0: success, non-zero: failure */
#else
    /* y^2 = x^3 + ax + b = (x^2 + a)x + b */
    return /* x^2 */
        mbedtls_mpi_mul_mpi(y2, x, x) ||
        mbedtls_mpi_mod_mpi(y2, y2, &e->P)
        /* x^2 + a */
        ||
        (e->A.MBEDTLS_PRIVATE(p) ? mbedtls_mpi_add_mpi(y2, y2, &e->A) :
                                   mbedtls_mpi_sub_int(y2, y2, 3)) ||
        mbedtls_mpi_mod_mpi(y2, y2, &e->P)
        /* (x^2 + a)x */
        ||
        mbedtls_mpi_mul_mpi(y2, y2, x) ||
        mbedtls_mpi_mod_mpi(y2, y2, &e->P)
        /* (x^2 + a)x + b */
        || mbedtls_mpi_add_mpi(y2, y2, &e->B) || mbedtls_mpi_mod_mpi(y2, y2, &e->P);
#endif
}

#endif /* MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED */

#if 0 /* not used by hostap */
#ifdef MBEDTLS_ECP_MONTGOMERY_ENABLED
static int
crypto_ec_point_y_sqr_montgomery(const mbedtls_ecp_group *e,
                                 const mbedtls_mpi *x,
                                 mbedtls_mpi *y2)
{
    /* XXX: !!! must be reviewed and audited for correctness !!! */

    /* MBEDTLS_ECP_TYPE_MONTGOMERY         y^2 = x^3 + a x^2 + x  */

    /* y^2 = x^3 + a x^2 + x = (x + a)x^2 + x */
    mbedtls_mpi x2;
    mbedtls_mpi_init(&x2);
    int ret = /* x^2 */
        mbedtls_mpi_mul_mpi(&x2, x, x) ||
        mbedtls_mpi_mod_mpi(&x2, &x2, &e->P)
        /* x + a */
        || mbedtls_mpi_add_mpi(y2, x, &e->A) ||
        mbedtls_mpi_mod_mpi(y2, y2, &e->P)
        /* (x + a)x^2 */
        || mbedtls_mpi_mul_mpi(y2, y2, &x2) ||
        mbedtls_mpi_mod_mpi(y2, y2, &e->P)
        /* (x + a)x^2 + x */
        || mbedtls_mpi_add_mpi(y2, y2, x) ||
        mbedtls_mpi_mod_mpi(y2, y2, &e->P);
    mbedtls_mpi_free(&x2);
    return ret; /* 0: success, non-zero: failure */
}

#endif /* MBEDTLS_ECP_MONTGOMERY_ENABLED */
#endif

struct crypto_bignum *crypto_ec_point_compute_y_sqr(struct crypto_ec *e,
                                                    const struct crypto_bignum *x)
{
    mbedtls_mpi *y2 = mmosal_malloc(sizeof(*y2));
    if (y2 == NULL)
    {
        return NULL;
    }
    mbedtls_mpi_init(y2);

#ifdef MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED
    if (mbedtls_ecp_get_type((const mbedtls_ecp_group *)e) == MBEDTLS_ECP_TYPE_SHORT_WEIERSTRASS &&
        crypto_ec_point_y_sqr_weierstrass((const mbedtls_ecp_group *)e,
                                          (const mbedtls_mpi *)x,
                                          y2) == 0)
    {
        return (struct crypto_bignum *)y2;
    }
#endif
#if 0 /* not used by hostap */
#ifdef MBEDTLS_ECP_MONTGOMERY_ENABLED
    if (mbedtls_ecp_get_type((mbedtls_ecp_group *)e) ==
        MBEDTLS_ECP_TYPE_MONTGOMERY &&
        crypto_ec_point_y_sqr_montgomery((const mbedtls_ecp_group *)e,
                                         (const mbedtls_mpi *)x,
                                         y2) == 0)
    {
        return (struct crypto_bignum *)y2;
    }
#endif
#endif

    mbedtls_mpi_free(y2);
    mmosal_free(y2);
    return NULL;
}

int crypto_ec_point_is_at_infinity(struct crypto_ec *e, const struct crypto_ec_point *p)
{
    return mbedtls_ecp_is_zero((mbedtls_ecp_point *)p);
}

int crypto_ec_point_is_on_curve(struct crypto_ec *e, const struct crypto_ec_point *p)
{
#if 1
    return mbedtls_ecp_check_pubkey((const mbedtls_ecp_group *)e, (const mbedtls_ecp_point *)p) ==
           0;
#else
    /* compute y^2 mod P and compare to y^2 mod P */
    /*(ref: src/eap_common/eap_pwd_common.c:compute_password_element())*/
    const mbedtls_mpi *px = &((const mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(X);
    mbedtls_mpi *cy2 =
        (mbedtls_mpi *)crypto_ec_point_compute_y_sqr(e, (const struct crypto_bignum *)px);
    if (cy2 == NULL)
    {
        return 0;
    }

    mbedtls_mpi y2;
    mbedtls_mpi_init(&y2);
    const mbedtls_mpi *py = &((const mbedtls_ecp_point *)p)->MBEDTLS_PRIVATE(Y);
    int is_on_curve = mbedtls_mpi_mul_mpi(&y2, py, py) || /* y^2 mod P */
                              mbedtls_mpi_mod_mpi(&y2, &y2, CRYPTO_EC_P(e)) ||
                              mbedtls_mpi_cmp_mpi(&y2, cy2) != 0 ?
                          0 :
                          1;

    mbedtls_mpi_free(&y2);
    mbedtls_mpi_free(cy2);
    mmosal_free(cy2);
    return is_on_curve;
#endif
}

int crypto_ec_point_cmp(const struct crypto_ec *e,
                        const struct crypto_ec_point *a,
                        const struct crypto_ec_point *b)
{
    return mbedtls_ecp_point_cmp((const mbedtls_ecp_point *)a, (const mbedtls_ecp_point *)b);
}

int aes_wrap(const uint8_t *kek, size_t kek_len, int n, const uint8_t *plain, uint8_t *cipher)
{
    mbedtls_nist_kw_context ctx;
    mbedtls_nist_kw_init(&ctx);
    size_t out_len = 0;

    int ret = mbedtls_nist_kw_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, kek, kek_len * 8, 1);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret =
        mbedtls_nist_kw_wrap(&ctx, MBEDTLS_KW_MODE_KW, plain, n * 8, cipher, &out_len, 8 * (n + 1));

cleanup:
    mbedtls_nist_kw_free(&ctx);

    return ret;
}
