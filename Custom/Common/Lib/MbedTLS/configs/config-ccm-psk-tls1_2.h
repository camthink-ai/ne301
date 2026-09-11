/**
 * \file config-ccm-psk-tls1_2.h
 *
 * \brief Minimal configuration for TLS 1.2 with PSK and AES-CCM ciphersuites
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */
/*
 * Minimal configuration for TLS 1.2 with PSK and AES-CCM ciphersuites
 *
 * Distinguishing features:
 * - Optimized for small code size, low bandwidth (on a reliable transport),
 *   and low RAM usage.
 * - No asymmetric cryptography (no certificates, no Diffie-Hellman key
 *   exchange).
 * - Fully modern and secure (provided the pre-shared keys are generated and
 *   stored securely).
 * - Very low record overhead with CCM-8.
 *
 * See README.txt for usage instructions.
 */

/* System support */
//#define MBEDTLS_HAVE_TIME /* Optionally used in Hello messages */
/* Other MBEDTLS_HAVE_XXX flags irrelevant for this configuration */
#include "mem.h"

#define HW_CRYPTO_DPA_AES
#define HW_CRYPTO_DPA_GCM
#define HW_CRYPTO_DPA_CTR_FOR_GCM
#define ST_HW_CONTEXT_SAVING

/* Mbed TLS modules */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ALT
#define MBEDTLS_HAL_AES_ALT
#define MBEDTLS_CCM_C
// #define MBEDTLS_CCM_ALT
// #define MBEDTLS_HAL_CCM_ALT
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_MD_C
// #define MBEDTLS_NET_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA1_ALT
#define MBEDTLS_HAL_SHA1_ALT
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_ALT
#define MBEDTLS_HAL_SHA256_ALT
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C

/* TLS protocol feature support */
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#define MBEDTLS_SSL_PROTO_TLS1_2
// #define MBEDTLS_SSL_PROTO_TLS1_3

/*
 * Use only CCM_8 ciphersuites, and
 * save ROM and a few bytes of RAM by specifying our own ciphersuite list
 */
// #define MBEDTLS_SSL_CIPHERSUITES

/*
 * Save RAM at the expense of interoperability: do this only if you control
 * both ends of the connection!  (See comments in "mbedtls/ssl.h".)
 * The optimal size here depends on the typical size of records.
 */
#define MBEDTLS_SSL_IN_CONTENT_LEN              16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN             16384

/* Save RAM at the expense of ROM */
#define MBEDTLS_AES_ROM_TABLES

/* Save some RAM by adjusting to your exact needs */
#define MBEDTLS_PSK_MAX_LEN    32 /* 256-bits keys are generally enough */

/*
 * You should adjust this to the exact number of sources you're using: default
 * is the "platform_entropy_poll" source, but you may want to add other ones
 * Minimum is 2 for the entropy test suite.
 */
#define MBEDTLS_ENTROPY_MAX_SOURCES 2

/* These defines are present so that the config modifying scripts can enable
 * them during tests/scripts/test-ref-configs.pl */
//#define MBEDTLS_USE_PSA_CRYPTO
//#define MBEDTLS_PSA_CRYPTO_C

/* Error messages and TLS debugging traces
 * (huge code size increase, needed for tests/ssl-opt.sh) */
#define MBEDTLS_DEBUG_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_SELF_TEST

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_USE_CRT
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_RSA_C
#define MBEDTLS_RSA_ALT
#define MBEDTLS_HAL_RSA_ALT
#define MBEDTLS_GCM_C
#define MBEDTLS_GCM_ALT
#define MBEDTLS_HAL_GCM_ALT
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_MD_CAN_SHA1
#define MBEDTLS_DEBUG_C

#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT   

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO   hal_mem_calloc_large
#define MBEDTLS_PLATFORM_FREE_MACRO     hal_mem_free

#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C

/* PKA hardware offload for P-256 (port/pka_p256_alt.c, ecp_alt.c, ecdh_alt.c, ecdsa_alt.c) */
#define MBEDTLS_ECP_INTERNAL_ALT
#define MBEDTLS_ECP_NORMALIZE_JAC_ALT
#define MBEDTLS_ECP_DOUBLE_JAC_ALT
#define MBEDTLS_ECP_ADD_MIXED_ALT
#define MBEDTLS_ECDH_GEN_PUBLIC_ALT
#define MBEDTLS_ECDH_COMPUTE_SHARED_ALT
#define MBEDTLS_ECDSA_SIGN_ALT
#define MBEDTLS_ECDSA_VERIFY_ALT

#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/*
 * Morse HaLow DPP (wpa_supplicant in libmorse.a): PK/CSR write and related X509.
 */
#ifndef CONFIG_DPP
#define CONFIG_DPP
#endif
#include "../../mmx108/morselib/include/mm_mbedtls_config.h"