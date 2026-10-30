#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

#include "app_admission.h"
#include "app_trust_anchor.h"
#include "anchor_vectors.h"
#include "p2_vectors.h"

#define POLICY_REGION (2u * 1024u * 1024u)
#define POLICY_BASE 0x93E00000u
#define TBS_LEN 212u
#define D1_SCALAR 1u

static int g_total;
static int g_fail;

static void expect(int cond, const char *name)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL %s\n", name);
    }
}

static void expect_hex(const uint8_t *got, const uint8_t *want, size_t len, const char *name)
{
    g_total++;
    if (memcmp(got, want, len) != 0) {
        g_fail++;
        printf("FAIL %s\n", name);
    }
}

static void expect_hex_diff(const uint8_t *a, const uint8_t *b, size_t len, const char *name)
{
    g_total++;
    if (memcmp(a, b, len) == 0) {
        g_fail++;
        printf("FAIL %s\n", name);
    }
}

static int test_rng(void *p, unsigned char *out, size_t len)
{
    static uint32_t s0 = 0x243F6A88u, s1 = 0x85A308D3u;
    static uint32_t s2 = 0x13198A2Eu, s3 = 0x03707344u;
    size_t i;
    (void)p;
    for (i = 0; i < len; i++) {
        uint32_t t = s0 ^ (s0 << 11);
        s0 = s1;
        s1 = s2;
        s2 = s3;
        t ^= t >> 8;
        s3 = s3 ^ (s3 >> 19) ^ t;
        out[i] = (unsigned char)(s3 & 0xFFu);
    }
    return 0;
}

static void device_policy(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = 0x00010000u;
    policy->supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy->exec_region_base = POLICY_BASE;
    policy->exec_region_size = POLICY_REGION;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
}

static void fixture_trust(app_admission_trust_view_t *trust)
{
    memset(trust, 0, sizeof(*trust));
    trust->publisher_id = "test-publisher";
    trust->publisher_id_len = 14u;
    trust->spki_der = P2_D1_TEST_SPKI_DER;
    trust->spki_der_len = P2_D1_TEST_SPKI_DER_LEN;
}

static void golden_tbs(uint8_t *buf)
{
    memcpy(buf, P2_GOLDEN_PACKAGE, TBS_LEN);
}

static void resign_tbs(const uint8_t *tbs, size_t tbs_len, uint8_t *pkg, size_t pkg_cap, size_t *pkg_len)
{
    uint8_t hash[32];
    uint8_t sig[72];
    size_t sig_len = 0;
    mbedtls_ecdsa_context ctx;
    int rc;

    memset(&ctx, 0, sizeof(ctx));
    mbedtls_ecdsa_init(&ctx);

    rc = mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0) {
        rc = mbedtls_mpi_lset(&ctx.d, (long)D1_SCALAR);
    }
    if (rc == 0) {
        rc = mbedtls_ecp_mul(&ctx.grp, &ctx.Q, &ctx.d, &ctx.grp.G, test_rng, NULL);
    }
    if (rc == 0) {
        rc = mbedtls_sha256(tbs, tbs_len, hash, 0);
    }
    if (rc == 0) {
        rc = mbedtls_ecdsa_write_signature(&ctx, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                           sig, sizeof(sig), &sig_len, test_rng, NULL);
    }

    if (rc == 0 && tbs_len + sig_len <= pkg_cap) {
        memcpy(pkg, tbs, tbs_len);
        memcpy(pkg + tbs_len, sig, sig_len);
        *pkg_len = tbs_len + sig_len;
    } else {
        *pkg_len = 0;
    }

    mbedtls_ecdsa_free(&ctx);
}

static void patch_u16(uint8_t *tbs, size_t off, uint16_t v)
{
    tbs[off] = (uint8_t)(v & 0xFFu);
    tbs[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
}

static void patch_u32(uint8_t *tbs, size_t off, uint32_t v)
{
    tbs[off] = (uint8_t)(v & 0xFFu);
    tbs[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    tbs[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    tbs[off + 3u] = (uint8_t)((v >> 24) & 0xFFu);
}

static void refresh_native_digest(uint8_t *tbs)
{
    uint8_t digest[32];
    mbedtls_sha256(tbs + 176u, 36u, digest, 0);
    memcpy(tbs + 112u, digest, 32u);
}

static int apply_neg_mutation(const char *name, uint8_t *tbs)
{
    if (strcmp(name, "manifest_uppercase_app_id") == 0) {
        tbs[36] = 0x48u;
        return 0;
    }
    if (strcmp(name, "manifest_reserved_nonzero") == 0) {
        tbs[74] = 0x01u;
        return 0;
    }
    if (strcmp(name, "wrong_board_signed") == 0) {
        tbs[76] = 0x11u;
        return 0;
    }
    if (strcmp(name, "untrusted_key_fingerprint") == 0) {
        tbs[144] = 0x5Du;
        return 0;
    }
    if (strcmp(name, "native_header_size_33") == 0) {
        patch_u16(tbs, 180, 33u);
        refresh_native_digest(tbs);
        return 0;
    }
    if (strcmp(name, "native_entry_offset_oob") == 0) {
        patch_u32(tbs, 104, 4u);
        patch_u32(tbs, 196, 4u);
        refresh_native_digest(tbs);
        return 0;
    }
    if (strcmp(name, "mismatched_manifest_native_len") == 0) {
        patch_u32(tbs, 96, 37u);
        return 0;
    }
    return -1;
}

static void verify_expect(const uint8_t *pkg, size_t pkg_len, app_admission_result_t want,
                          const char *name)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    device_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(pkg, pkg_len, &policy, &trust, &verdict);
    g_total++;
    if (rc != want) {
        g_fail++;
        printf("FAIL %s: got %s want %d\n", name, app_admission_result_str(rc), (int)want);
    }
}

static void test_identifier_charset(void)
{
    uint8_t slot[16];
    size_t i;

    memset(slot, 0, sizeof(slot));
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_empty_refused");
    memcpy(slot, "a", 1);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_TRUE, "id_single_letter_ok");
    memcpy(slot, "0a", 2);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_leading_digit_refused");
    memcpy(slot, "-abc", 4);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_leading_dash_refused");
    memcpy(slot, "abc-", 4);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_trailing_dash_refused");
    memcpy(slot, "ABC", 3);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_uppercase_refused");
    memcpy(slot, "te\0t", 4);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_embedded_nul_refused");
    memcpy(slot, "abc", 3);
    slot[15] = 0x20u;
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_nonzero_padding_refused");
    memcpy(slot, "abcdefghijklmnop", 16);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_TRUE, "id_full_slot_ok");
    memcpy(slot, "a-b-9", 5);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_TRUE, "id_mixed_ok");
    memcpy(slot, "a b", 3);
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_space_refused");
    memcpy(slot, "ab", 2);
    slot[1] = 0x81u;
    expect(app_admission_identifier_valid(slot, 16) == AICAM_FALSE, "id_non_ascii_refused");
    expect(app_admission_identifier_valid(NULL, 16) == AICAM_FALSE, "id_null_refused");
    expect(app_admission_identifier_valid(slot, 0) == AICAM_FALSE, "id_zero_slot_refused");
    for (i = 0; i < sizeof(slot); i++) {
        slot[i] = 0x00u;
    }
}

static void test_golden_positive(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    device_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0x5A, sizeof(verdict));
    rc = app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_OK, "golden_accepted");
    expect(strcmp(verdict.publisher_id, "test-publisher") == 0, "golden_publisher");
    expect(strcmp(verdict.app_id, "hello-app") == 0, "golden_app_id");
    expect(verdict.version_major == 1u && verdict.version_minor == 0u && verdict.version_patch == 0u,
           "golden_version");
    expect(verdict.tbs_len == TBS_LEN, "golden_tbs_len");
    expect(verdict.signature_len == 72u, "golden_sig_len");
    expect(verdict.native_image_size == 4u, "golden_native_image_size");
    expect(verdict.native_entry_offset == 0u, "golden_entry_offset");
    expect(verdict.native_target_addr == POLICY_BASE, "golden_target_addr");
    expect(verdict.required_exec_region_bytes == 4u, "golden_exec_region_decl");
    expect_hex(verdict.tbs_sha256, P2_GOLDEN_TBS_SHA256, 32, "golden_tbs_sha");
    expect_hex(verdict.image_sha256, P2_GOLDEN_PACKAGE + 112u, 32, "golden_image_sha_equals_manifest");
    expect_hex(verdict.publisher_key_sha256, P2_D1_TEST_SPKI_SHA256, 32, "golden_publisher_fp");
}

static void test_fixed_signed_vectors(void)
{
    size_t i;

    verify_expect(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_OK, "vector_golden_ok");

    {
        uint8_t pkg[320];
        size_t len = TBS_LEN + P2_LOW_S_DER_LEN;
        memcpy(pkg, P2_GOLDEN_PACKAGE, TBS_LEN);
        memcpy(pkg + TBS_LEN, P2_LOW_S_DER, P2_LOW_S_DER_LEN);
        verify_expect(pkg, len, APP_ADMISSION_OK, "low_s_variant_accepted");
    }

    {
        uint8_t pkg[320];
        size_t len = TBS_LEN + P2_DER70_VALID_LEN;
        memcpy(pkg, P2_GOLDEN_PACKAGE, TBS_LEN);
        memcpy(pkg + TBS_LEN, P2_DER70_VALID, P2_DER70_VALID_LEN);
        verify_expect(pkg, len, APP_ADMISSION_OK, "der70_valid_accepted");
    }

    {
        uint8_t pkg[320];
        size_t len = TBS_LEN + P2_DER70_VALID_LEN + 1u;
        memcpy(pkg, P2_GOLDEN_PACKAGE, TBS_LEN);
        memcpy(pkg + TBS_LEN, P2_DER70_VALID, P2_DER70_VALID_LEN);
        pkg[len - 1u] = 0xFFu;
        verify_expect(pkg, len, APP_ADMISSION_ERR_BAD_PACKAGE, "der70_tail_ff_refused");
    }

    {
        uint8_t pkg[320];
        memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
        pkg[P2_GOLDEN_PACKAGE_LEN] = 0xFFu;
        verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN + 1u, APP_ADMISSION_ERR_BAD_PACKAGE,
                      "golden_plus_one_byte_refused");
    }

    {
        uint8_t pkg[320];
        memcpy(pkg, P2_RESERVED0_TBS, TBS_LEN);
        memcpy(pkg + TBS_LEN, P2_RESERVED0_DER, P2_RESERVED0_DER_LEN);
        verify_expect(pkg, TBS_LEN + P2_RESERVED0_DER_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                      "reserved0_signed_refused");
    }

    for (i = 0; i < P2_NEG_VECTOR_COUNT; i++) {
        char name[64];
        uint8_t pkg[320];
        uint8_t tbs[TBS_LEN];
        const struct p2_neg_vector *v = &P2_NEG_VECTORS[i];
        uint8_t sha[32];
        snprintf(name, sizeof(name), "p2_neg_%s", v->name);
        golden_tbs(tbs);
        expect(apply_neg_mutation(v->name, tbs) == 0, name);
        mbedtls_sha256(tbs, TBS_LEN, sha, 0);
        expect_hex(sha, v->tbs_sha256, 32, name);
        memcpy(pkg, tbs, TBS_LEN);
        memcpy(pkg + TBS_LEN, v->der, v->der_len);
        verify_expect(pkg, TBS_LEN + v->der_len, (app_admission_result_t)v->expect, name);
    }
}

static void test_signature_boundary(void)
{
    uint8_t tbs[TBS_LEN];
    uint8_t pkg[320];
    size_t pkg_len;

    golden_tbs(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_produces_package");
    verify_expect(pkg, pkg_len, APP_ADMISSION_OK, "fresh_resign_accepted");

    pkg[70] ^= 0x01u;
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_SIGNATURE_INVALID, "tampered_tbs_refused");

    golden_tbs(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    pkg[pkg_len - 1u] ^= 0x80u;
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_SIGNATURE_INVALID, "flipped_sig_byte_refused");

    {
        uint8_t bad_der[8] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00};
        golden_tbs(tbs);
        memcpy(pkg, tbs, TBS_LEN);
        memcpy(pkg + TBS_LEN, bad_der, sizeof(bad_der));
        verify_expect(pkg, TBS_LEN + 8u, APP_ADMISSION_ERR_BAD_PACKAGE, "s_zero_refused");
    }

    {
        uint8_t n_bytes[32];
        uint8_t bad_der[40];
        size_t k;
        for (k = 0; k < sizeof(n_bytes); k++) {
            n_bytes[k] = 0xFFu;
        }
        bad_der[0] = 0x30;
        bad_der[1] = 0x26;
        bad_der[2] = 0x02;
        bad_der[3] = 0x21;
        bad_der[4] = 0x00;
        memcpy(bad_der + 5, n_bytes, 32);
        bad_der[37] = 0x02;
        bad_der[38] = 0x01;
        bad_der[39] = 0x01;
        golden_tbs(tbs);
        memcpy(pkg, tbs, TBS_LEN);
        memcpy(pkg + TBS_LEN, bad_der, sizeof(bad_der));
        verify_expect(pkg, TBS_LEN + sizeof(bad_der), APP_ADMISSION_ERR_BAD_PACKAGE, "s_ge_n_refused");
    }

    {
        uint8_t bad_der[8] = {0x30, 0x06, 0x02, 0x04, 0xFF, 0x00, 0x00, 0x01};
        golden_tbs(tbs);
        memcpy(pkg, tbs, TBS_LEN);
        memcpy(pkg + TBS_LEN, bad_der, sizeof(bad_der));
        verify_expect(pkg, TBS_LEN + sizeof(bad_der), APP_ADMISSION_ERR_BAD_PACKAGE,
                      "r_negative_encoding_refused");
    }

    {
        uint8_t bad_der[9] = {0x30, 0x81, 0x08, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
        golden_tbs(tbs);
        memcpy(pkg, tbs, TBS_LEN);
        memcpy(pkg + TBS_LEN, bad_der, sizeof(bad_der));
        verify_expect(pkg, TBS_LEN + sizeof(bad_der), APP_ADMISSION_ERR_BAD_PACKAGE,
                      "nonminimal_seq_len_refused");
    }

    {
        uint8_t bad_der[9] = {0x30, 0x80, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x00};
        golden_tbs(tbs);
        memcpy(pkg, tbs, TBS_LEN);
        memcpy(pkg + TBS_LEN, bad_der, sizeof(bad_der));
        verify_expect(pkg, TBS_LEN + sizeof(bad_der), APP_ADMISSION_ERR_BAD_PACKAGE,
                      "indefinite_len_refused");
    }

    {
        uint8_t pkg_trunc[320];
        golden_tbs(tbs);
        resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
        memcpy(pkg_trunc, pkg, pkg_len - 1u);
        verify_expect(pkg_trunc, pkg_len - 1u, APP_ADMISSION_ERR_BAD_PACKAGE, "truncated_der_refused");
    }
}

static void test_structural_boundary(void)
{
    uint8_t pkg[320];
    size_t len;
    uint8_t tbs[TBS_LEN];

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    pkg[0] ^= 0x01u;
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "bad_magic_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    pkg[5] = 2u;
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "future_container_version_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    pkg[6] = 1u;
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "nonzero_reserved_magic_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    pkg[8] = 0x9Fu;
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "manifest_len_159_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    pkg[8] = 0xA1u;
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "manifest_len_161_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    memset(pkg + 8, 0, 4);
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "manifest_len_zero_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    memset(pkg + 12, 0, 4);
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "image_len_zero_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    pkg[12] = 31u;
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "image_len_31_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    memset(pkg + 12, 0xFF, 4);
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "image_len_overflow_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 12, APP_ADMISSION_MAX_NATIVE_FILE_LEN + 1u);
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "image_len_over_cap_refused");

    memcpy(pkg, P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 12, 0x00200001u);
    verify_expect(pkg, P2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE, "image_len_over_default_cap_refused");

    verify_expect(P2_GOLDEN_PACKAGE, APP_ADMISSION_CONTAINER_HEADER_SIZE - 1u,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "shorter_than_container_header_refused");

    verify_expect(P2_GOLDEN_PACKAGE, APP_ADMISSION_IMAGE_OFFSET - 1u,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "shorter_than_manifest_refused");

    golden_tbs(tbs);
    memcpy(pkg, tbs, TBS_LEN);
    len = TBS_LEN + 4u;
    pkg[TBS_LEN] = 0x30u;
    pkg[TBS_LEN + 1u] = 0x02u;
    pkg[TBS_LEN + 2u] = 0x02u;
    pkg[TBS_LEN + 3u] = 0x01u;
    verify_expect(pkg, len, APP_ADMISSION_ERR_BAD_PACKAGE, "truncated_signature_refused");

    {
        app_admission_policy_t policy;
        device_policy(&policy);
        policy.max_package_len = 100u;
        app_admission_trust_view_t trust;
        app_admission_verdict_t verdict;
        fixture_trust(&trust);
        expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
               APP_ADMISSION_ERR_BAD_PACKAGE, "policy_package_cap_refused");
        policy.max_package_len = 0u;
        policy.exec_region_size = 33u;
        expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
               APP_ADMISSION_ERR_RESOURCE_LIMIT, "tiny_exec_region_refused");
    }
}

static void test_manifest_encoding_negatives(void)
{
    uint8_t tbs[TBS_LEN];
    uint8_t pkg[320];
    size_t pkg_len;

    golden_tbs(tbs);
    tbs[36] = (uint8_t)'1';
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_leading_digit");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "app_id_leading_digit_refused");

    golden_tbs(tbs);
    tbs[36] = (uint8_t)'a';
    tbs[37] = 0x00u;
    tbs[38] = (uint8_t)'b';
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_embedded_nul");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "app_id_embedded_nul_refused");

    golden_tbs(tbs);
    tbs[34] = 0x20u;
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_nonzero_padding");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "publisher_nonzero_padding_refused");

    golden_tbs(tbs);
    tbs[20] = (uint8_t)'1';
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_publisher_digit");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "publisher_leading_digit_refused");

    golden_tbs(tbs);
    tbs[16 + 0] = 'X';
    tbs[16 + 1] = 'M';
    tbs[16 + 2] = 'F';
    tbs[16 + 3] = '1';
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_manifest_magic");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "manifest_magic_refused");

    golden_tbs(tbs);
    tbs[74] = 1u;
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_reserved_nonzero");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "manifest_reserved_nonzero_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 88, 0x00000007u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_unknown_caps");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "unknown_capability_bit_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 88, 0x00000000u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_zero_caps");
    verify_expect(pkg, pkg_len, APP_ADMISSION_OK, "zero_capability_mask_accepted");

    golden_tbs(tbs);
    tbs[144] = 0x5Du;
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_wrong_fingerprint");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED, "wrong_fingerprint_refused");

    golden_tbs(tbs);
    memcpy(tbs + 20, "other-publisher", 15);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_other_publisher");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED, "unknown_publisher_refused");
}

static void test_native_header_v1_negatives(void)
{
    uint8_t tbs[TBS_LEN];
    uint8_t pkg[320];
    size_t pkg_len;

    golden_tbs(tbs);
    patch_u16(tbs, 180, 33u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_hdr33");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "native_header_size_33_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 184, 0x00020000u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_hdr_abi");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "header_abi_manifest_mismatch_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 188, POLICY_BASE + 0x1000u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_hdr_target");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "header_target_manifest_mismatch_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 192, 5u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_hdr_imgsize");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "header_image_size_mismatch_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 196, 3u);
    patch_u32(tbs, 104, 3u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_odd_entry");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "odd_entry_offset_refused");

    golden_tbs(tbs);
    tbs[204] ^= 0xFFu;
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_crc");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "native_crc_corrupt_refused");

    golden_tbs(tbs);
    tbs[177] = 'E';
    tbs[178] = 'A';
    tbs[179] = '2';
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_native_magic");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "native_magic_wrong_refused");

    golden_tbs(tbs);
    patch_u16(tbs, 182, 2u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_native_fmt");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "native_format_v2_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 96, 37u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_manifest_len");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "manifest_native_len_mismatch_refused");

    golden_tbs(tbs);
    tbs[112] ^= 0xFFu;
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_digest");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE, "native_digest_mismatch_refused");
}

static void test_policy_matrix(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    uint8_t tbs[TBS_LEN];
    uint8_t pkg[320];
    size_t pkg_len;

    device_policy(&policy);
    fixture_trust(&trust);

    policy.psram_mib = 32u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_TARGET_INCOMPATIBLE, "psram_32_refused");
    device_policy(&policy);

    policy.target_board_id = 0x00003011u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_TARGET_INCOMPATIBLE, "wrong_board_policy_refused");
    device_policy(&policy);

    policy.abi_version = 0x00020000u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "abi_policy_mismatch_refused");
    device_policy(&policy);

    policy.supported_caps_mask = APP_ADMISSION_CAP_LOG;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "caps_not_supported_refused");
    device_policy(&policy);

    policy.exec_region_base = POLICY_BASE + 0x1000u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_TARGET_INCOMPATIBLE, "exec_region_base_mismatch_refused");
    device_policy(&policy);

    golden_tbs(tbs);
    patch_u32(tbs, 92, 3u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_exec_small");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_RESOURCE_LIMIT, "exec_region_below_image_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 92, POLICY_REGION + 1u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_exec_big");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_RESOURCE_LIMIT, "exec_region_over_region_refused");

    golden_tbs(tbs);
    patch_u32(tbs, 92, POLICY_REGION);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_exec_exact");
    verify_expect(pkg, pkg_len, APP_ADMISSION_OK, "exec_region_exact_ok");

    golden_tbs(tbs);
    patch_u32(tbs, 92, 4u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_golden_exec");
    verify_expect(pkg, pkg_len, APP_ADMISSION_OK, "resigned_golden_policy_ok");

    golden_tbs(tbs);
    patch_u16(tbs, 68, 2u);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "resign_version");
    {
        app_admission_policy_t pol2;
        app_admission_trust_view_t trust2;
        app_admission_verdict_t verdict2;
        device_policy(&pol2);
        fixture_trust(&trust2);
        expect(app_admission_verify_package(pkg, pkg_len, &pol2, &trust2, &verdict2) == APP_ADMISSION_OK,
               "version_bump_accepted");
        expect(verdict2.version_major == 2u, "version_major_extracted");
    }
}

static void test_param_and_trust_failures(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;

    device_policy(&policy);
    fixture_trust(&trust);

    expect(app_admission_verify_package(NULL, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_PARAM, "null_package_param");
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, NULL, &trust, &verdict) ==
           APP_ADMISSION_ERR_PARAM, "null_policy_param");
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, NULL, &verdict) ==
           APP_ADMISSION_ERR_PARAM, "null_trust_param");
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, NULL) ==
           APP_ADMISSION_ERR_PARAM, "null_verdict_param");

    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = "test-publisher";
    trust.publisher_id_len = 14u;
    trust.spki_der = NULL;
    trust.spki_der_len = 0u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_TRUST_UNPROVEN, "missing_key_fail_closed");

    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = "test-publisher";
    trust.publisher_id_len = 14u;
    trust.spki_der = P2_D1_TEST_SPKI_DER;
    trust.spki_der_len = 0u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_TRUST_UNPROVEN, "empty_key_fail_closed");

    memset(&trust, 0, sizeof(trust));
    trust.spki_der = P2_D1_TEST_SPKI_DER;
    trust.spki_der_len = P2_D1_TEST_SPKI_DER_LEN;
    trust.publisher_id = NULL;
    trust.publisher_id_len = 0u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_TRUST_UNPROVEN, "no_publisher_binding_fail_closed");

    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = "test-publisher";
    trust.publisher_id_len = 14u;
    trust.spki_der = P2_D1_TEST_SPKI_DER;
    trust.spki_der_len = P2_D1_TEST_SPKI_DER_LEN - 1u;
    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED, "short_key_digest_mismatch");

    expect(strcmp(app_admission_result_str(APP_ADMISSION_OK), "trusted_compatible") == 0, "str_ok");
    expect(strcmp(app_admission_result_str((app_admission_result_t)99), "UNKNOWN") == 0, "str_unknown");
}

static void test_device_trust_anchor(void)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    uint8_t computed[32];
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    uint8_t tbs[TBS_LEN];
    uint8_t pkg[320];
    size_t pkg_len;

    expect(anchor != NULL, "anchor_present");
    expect(anchor->format_version == APP_TRUST_ANCHOR_FORMAT_VERSION, "anchor_format_v1");
    expect(app_trust_anchor_status() == APP_TRUST_ANCHOR_PROVISIONED, "anchor_provisioned");
    expect(strcmp(app_trust_anchor_status_str(APP_TRUST_ANCHOR_PROVISIONED), "provisioned") == 0, "anchor_str");

    mbedtls_sha256(anchor->spki_der, anchor->spki_der_len, computed, 0);
    expect_hex(computed, anchor->spki_sha256, 32, "anchor_digest_binding");

    expect(anchor->spki_der_len == 91u, "anchor_spki_len");
    expect(strcmp(anchor->publisher_id, "ne301-exp-pub") == 0, "anchor_publisher_named");
    expect(strcmp(anchor->publisher_id, "test-publisher") != 0, "anchor_not_d1_publisher");
    expect_hex_diff(anchor->spki_sha256, P2_D1_TEST_SPKI_SHA256, 32, "anchor_key_not_d1_fixture");

    device_policy(&policy);
    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;

    expect(app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN, &policy, &trust, &verdict) ==
           APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED, "d1_golden_refused_by_device_anchor");

    golden_tbs(tbs);
    memcpy(tbs + 20, anchor->publisher_id, anchor->publisher_id_len);
    resign_tbs(tbs, TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "device_id_resign_ok");
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED,
                  "d1_signature_device_identity_refused");
}

static void anchor_trust(app_admission_trust_view_t *trust)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();

    memset(trust, 0, sizeof(*trust));
    trust->publisher_id = anchor->publisher_id;
    trust->publisher_id_len = anchor->publisher_id_len;
    trust->spki_der = anchor->spki_der;
    trust->spki_der_len = anchor->spki_der_len;
}

static void verify_anchor_expect(const uint8_t *pkg, size_t pkg_len, app_admission_result_t want,
                                 const char *name)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    device_policy(&policy);
    anchor_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(pkg, pkg_len, &policy, &trust, &verdict);
    g_total++;
    if (rc != want) {
        g_fail++;
        printf("FAIL %s: got %s want %d\n", name, app_admission_result_str(rc), (int)want);
    }
}

static void test_compiled_anchor_pairing(void)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    uint8_t pkg[ANCHOR_POSITIVE_PACKAGE_LEN];
    uint8_t recomputed[32];

    expect(anchor != NULL, "anchor_pairing_anchor_present");
    expect(app_trust_anchor_status() == APP_TRUST_ANCHOR_PROVISIONED, "anchor_pairing_provisioned");

    expect(anchor->spki_der_len == ANCHOR_VECTOR_SPKI_DER_LEN, "anchor_pairing_spki_len");
    expect(memcmp(anchor->spki_der, ANCHOR_VECTOR_SPKI_DER, ANCHOR_VECTOR_SPKI_DER_LEN) == 0,
           "anchor_pairing_spki_bytes_equal");
    expect(memcmp(anchor->spki_sha256, ANCHOR_VECTOR_SPKI_SHA256, ANCHOR_VECTOR_SPKI_SHA256_LEN) == 0,
           "anchor_pairing_pinned_digest_equal");

    mbedtls_sha256(anchor->spki_der, anchor->spki_der_len, recomputed, 0);
    expect_hex(recomputed, ANCHOR_VECTOR_SPKI_SHA256, 32, "anchor_pairing_recomputed_digest");
    mbedtls_sha256(ANCHOR_VECTOR_SPKI_DER, ANCHOR_VECTOR_SPKI_DER_LEN, recomputed, 0);
    expect_hex(recomputed, ANCHOR_VECTOR_SPKI_SHA256, 32, "anchor_pairing_vector_digest");

    expect(memcmp(anchor->spki_der, ANCHOR_SUPERSEDED_SPKI_DER, ANCHOR_VECTOR_SPKI_DER_LEN) != 0,
           "anchor_superseded_key_distinct");
    expect(memcmp(anchor->spki_sha256, P2_D1_TEST_SPKI_SHA256, 32) != 0, "anchor_pairing_not_d1");

    device_policy(&policy);
    anchor_trust(&trust);
    memset(&verdict, 0x5A, sizeof(verdict));
    expect(app_admission_verify_package(ANCHOR_POSITIVE_PACKAGE, ANCHOR_POSITIVE_PACKAGE_LEN,
                                        &policy, &trust, &verdict) == APP_ADMISSION_OK,
           "anchor_positive_trusted_compatible");
    expect(strcmp(app_admission_result_str(APP_ADMISSION_OK), "trusted_compatible") == 0,
           "anchor_positive_result_str");
    expect(strcmp(verdict.publisher_id, "ne301-exp-pub") == 0, "anchor_positive_publisher");
    expect(strcmp(verdict.app_id, "anchor-fixture") == 0, "anchor_positive_app_id");
    expect(verdict.version_major == 1u && verdict.version_minor == 0u && verdict.version_patch == 0u,
           "anchor_positive_version");
    expect(verdict.tbs_len == ANCHOR_POSITIVE_SIG_OFFSET, "anchor_positive_tbs_len");
    expect_hex(verdict.publisher_key_sha256, ANCHOR_VECTOR_SPKI_SHA256, 32,
               "anchor_positive_key_fingerprint");

    verify_anchor_expect(ANCHOR_POSITIVE_PACKAGE, ANCHOR_POSITIVE_PACKAGE_LEN - 1u,
                         APP_ADMISSION_ERR_BAD_PACKAGE, "anchor_truncated_signature_refused");

    memcpy(pkg, ANCHOR_SUPERSEDED_PACKAGE, ANCHOR_SUPERSEDED_PACKAGE_LEN);
    verify_anchor_expect(pkg, ANCHOR_SUPERSEDED_PACKAGE_LEN,
                         APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED, "anchor_superseded_version_refused");

    memcpy(pkg, ANCHOR_FOREIGN_PACKAGE, ANCHOR_FOREIGN_PACKAGE_LEN);
    verify_anchor_expect(pkg, ANCHOR_FOREIGN_PACKAGE_LEN,
                         APP_ADMISSION_ERR_SIGNATURE_INVALID, "anchor_foreign_signature_refused");

    memcpy(pkg, ANCHOR_POSITIVE_PACKAGE, ANCHOR_POSITIVE_PACKAGE_LEN);
    pkg[ANCHOR_POSITIVE_TAMPER_OFFSET] ^= 0xFFu;
    verify_anchor_expect(pkg, ANCHOR_POSITIVE_PACKAGE_LEN,
                         APP_ADMISSION_ERR_SIGNATURE_INVALID, "anchor_tampered_payload_refused");

    memcpy(pkg, ANCHOR_POSITIVE_PACKAGE, ANCHOR_POSITIVE_PACKAGE_LEN);
    pkg[ANCHOR_POSITIVE_SIG_OFFSET + 8u] ^= 0x01u;
    verify_anchor_expect(pkg, ANCHOR_POSITIVE_PACKAGE_LEN,
                         APP_ADMISSION_ERR_SIGNATURE_INVALID, "anchor_tampered_signature_refused");
}

int main(void)
{
    test_identifier_charset();
    test_golden_positive();
    test_fixed_signed_vectors();
    test_signature_boundary();
    test_structural_boundary();
    test_manifest_encoding_negatives();
    test_native_header_v1_negatives();
    test_policy_matrix();
    test_param_and_trust_failures();
    test_device_trust_anchor();
    test_compiled_anchor_pairing();

    printf("app_admission: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
