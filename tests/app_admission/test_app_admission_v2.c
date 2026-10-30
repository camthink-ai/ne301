#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

#include "app_admission.h"
#include "v2_vectors.h"
#include "p2_vectors.h"

#define POLICY_REGION (2u * 1024u * 1024u)
#define POLICY_BASE 0x93E00000u
#define V2_TBS_LEN 244u
#define V2_IMAGE_OFFSET_V2 208u
#define V2_NATIVE_OFF (16u + 192u)
#define D1_SCALAR 1u
#define D2_SCALAR 2u

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

static void v2_policy(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = APP_ADMISSION_ABI_VERSION_V2;
    policy->supported_caps_mask = APP_ADMISSION_V2_KNOWN_CAPS_MASK;
    policy->exec_region_base = POLICY_BASE;
    policy->exec_region_size = POLICY_REGION;
    policy->event_capacity = 2048u;
    policy->report_capacity = 6144u;
    policy->state_quota_capacity = 4096u;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V2;
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
    trust->spki_der = V2_D1_SPKI_DER;
    trust->spki_der_len = V2_D1_SPKI_DER_LEN;
}

static void resign_tbs_key(const uint8_t *tbs, size_t tbs_len, long scalar,
                           uint8_t *pkg, size_t pkg_cap, size_t *pkg_len)
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
        rc = mbedtls_mpi_lset(&ctx.d, scalar);
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

static void resign_tbs(const uint8_t *tbs, size_t tbs_len,
                       uint8_t *pkg, size_t pkg_cap, size_t *pkg_len)
{
    resign_tbs_key(tbs, tbs_len, (long)D1_SCALAR, pkg, pkg_cap, pkg_len);
}

static void patch_u32(uint8_t *buf, size_t off, uint32_t v)
{
    buf[off] = (uint8_t)(v & 0xFFu);
    buf[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    buf[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    buf[off + 3u] = (uint8_t)((v >> 24) & 0xFFu);
}

static void refresh_native_digest(uint8_t *tbs)
{
    uint8_t digest[32];
    mbedtls_sha256(tbs + V2_IMAGE_OFFSET_V2, 36u, digest, 0);
    memcpy(tbs + 112u, digest, 32u);
}

static void verify_expect(const uint8_t *pkg, size_t pkg_len, app_admission_result_t want,
                          const char *name)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    v2_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(pkg, pkg_len, &policy, &trust, &verdict);
    g_total++;
    if (rc != want) {
        g_fail++;
        printf("FAIL %s: got %s want %s\n", name, app_admission_result_str(rc),
               app_admission_result_str(want));
    }
}

static void verify_with_policy_expect(const app_admission_policy_t *policy,
                                      const app_admission_trust_view_t *trust,
                                      const uint8_t *pkg, size_t pkg_len,
                                      app_admission_result_t want, const char *name)
{
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(pkg, pkg_len, policy, trust, &verdict);
    g_total++;
    if (rc != want) {
        g_fail++;
        printf("FAIL %s: got %s want %s\n", name, app_admission_result_str(rc),
               app_admission_result_str(want));
    }
}

static void test_golden_positive(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    v2_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0x5A, sizeof(verdict));
    rc = app_admission_verify_package(V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                                      &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_OK, "v2_golden_accepted");
    expect(strcmp(verdict.publisher_id, "test-publisher") == 0, "v2_golden_publisher");
    expect(strcmp(verdict.app_id, "hello-app") == 0, "v2_golden_app_id");
    expect(verdict.version_major == 1u && verdict.version_minor == 0u &&
           verdict.version_patch == 0u, "v2_golden_version");
    expect(verdict.required_host_abi == APP_ADMISSION_ABI_VERSION_V2, "v2_golden_abi");
    expect(verdict.required_host_caps == 0x3Fu, "v2_golden_caps");
    expect(verdict.run_profile == 1u, "v2_golden_run_profile");
    expect(verdict.event_max == 2048u, "v2_golden_event_max");
    expect(verdict.state_quota == 4096u, "v2_golden_state_quota");
    expect(verdict.report_max == 6144u, "v2_golden_report_max");
    expect(verdict.tbs_len == V2_TBS_LEN, "v2_golden_tbs_len");
    expect(verdict.signature_len == 70u, "v2_golden_sig_len");
    expect(verdict.native_image_size == 4u, "v2_golden_native_image_size");
    expect(verdict.native_entry_offset == 0u, "v2_golden_entry_offset");
    expect(verdict.native_target_addr == POLICY_BASE, "v2_golden_target_addr");
    expect(verdict.required_exec_region_bytes == 4u, "v2_golden_exec_region_decl");
    expect_hex(verdict.tbs_sha256, V2_GOLDEN_TBS_SHA256, 32, "v2_golden_tbs_sha");
    expect_hex(verdict.image_sha256, V2_GOLDEN_PACKAGE + 112u, 32,
               "v2_golden_image_sha_equals_manifest");
    expect_hex(verdict.publisher_key_sha256, V2_D1_SPKI_SHA256, 32, "v2_golden_publisher_fp");
}

static void test_domain_isolation(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    device_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                                      &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "v2_golden_refused_by_current_host_policy");
    expect(verdict.required_host_caps == 0u, "v2_refused_verdict_no_caps");

    v2_policy(&policy);
    policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                                      &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "v2_golden_refused_when_host_lacks_caps");

    device_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN,
                                      &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_OK, "v1_golden_still_accepted_on_current_host");
    expect(verdict.required_host_caps == APP_ADMISSION_KNOWN_CAPS_MASK,
           "v1_golden_caps_stay_v1_domain");
    expect(verdict.run_profile == 0u, "v1_verdict_profile_untouched");
    expect(verdict.event_max == 0u && verdict.report_max == 0u && verdict.state_quota == 0u,
           "v1_verdict_quotas_untouched");

    v2_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(P2_GOLDEN_PACKAGE, P2_GOLDEN_PACKAGE_LEN,
                                      &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "v1_golden_refused_by_v2_only_policy");
}

static void test_capacity_gate(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;

    v2_policy(&policy);
    fixture_trust(&trust);
    policy.event_capacity = 1024u;
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_RESOURCE_LIMIT, "event_capacity_exceeded_refused");

    v2_policy(&policy);
    policy.report_capacity = 4096u;
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_RESOURCE_LIMIT, "report_capacity_exceeded_refused");

    v2_policy(&policy);
    policy.state_quota_capacity = 1024u;
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_RESOURCE_LIMIT, "state_capacity_exceeded_refused");

    verify_expect(V2_ARCHIVE_PACKAGE, V2_ARCHIVE_PACKAGE_LEN,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "archive_1024_4096_refused");

    v2_policy(&policy);
    policy.exec_region_size = 35u;
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_RESOURCE_LIMIT, "exec_region_too_small_refused");
}

static void test_signed_variants(void)
{
    uint8_t tbs[V2_TBS_LEN];
    uint8_t pkg[400];
    size_t pkg_len;
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t rc;

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "v2_resign_produces_package");
    verify_expect(pkg, pkg_len, APP_ADMISSION_OK, "v2_fresh_resign_accepted");

    v2_policy(&policy);
    fixture_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(pkg, pkg_len, &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_OK, "v2_resign_verdict_ok");
    expect_hex(verdict.tbs_sha256, V2_GOLDEN_TBS_SHA256, 32, "v2_resign_same_tbs_identity");

    verify_expect(V2_HIGH_S_PACKAGE, V2_HIGH_S_PACKAGE_LEN, APP_ADMISSION_OK,
                  "v2_high_s_variant_accepted");
    memset(&verdict, 0, sizeof(verdict));
    rc = app_admission_verify_package(V2_HIGH_S_PACKAGE, V2_HIGH_S_PACKAGE_LEN,
                                      &policy, &trust, &verdict);
    expect(rc == APP_ADMISSION_OK, "v2_high_s_verdict_ok");
    expect_hex(verdict.tbs_sha256, V2_GOLDEN_TBS_SHA256, 32,
               "v2_high_s_same_tbs_identity");

    pkg[V2_TBS_LEN + 10u] ^= 0x01u;
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_SIGNATURE_INVALID, "v2_corrupt_der_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    pkg[100] ^= 0x01u;
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_SIGNATURE_INVALID, "v2_tampered_tbs_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    pkg[pkg_len - 1u] ^= 0x80u;
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_SIGNATURE_INVALID, "v2_flipped_sig_byte_refused");
}

static void test_structural_negatives(void)
{
    uint8_t pkg[400];

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    pkg[5] = 0x03u;
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "unknown_container_magic_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 8, 160u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "manifest_len_160_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 8, 193u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "manifest_len_193_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    pkg[16] = 'N';
    pkg[17] = 'M';
    pkg[18] = 'F';
    pkg[19] = '1';
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v1_manifest_magic_in_v2_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 84, 0x00010000u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "manifest_abi_v1_in_v2_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    pkg[74] = 0x01u;
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "manifest_reserved58_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    pkg[192] = 0x01u;
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v2_reserved176_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 176, 2u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "unknown_run_profile_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 88, 0x0000001Fu);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "profile1_without_session_bit_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 88, 0x0000007Fu);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "unknown_cap_bit6_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 180, 0u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "ai_events_without_event_max_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 188, 0u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "report_submit_without_report_max_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 184, 0u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "app_state_without_state_quota_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 88, 0x0000003Bu);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "event_max_without_ai_events_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 88, 0x00000037u);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "report_max_without_report_submit_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 88, 0x0000002Fu);
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "state_quota_without_app_state_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    pkg[V2_GOLDEN_PACKAGE_LEN] = 0xFFu;
    verify_expect(pkg, V2_GOLDEN_PACKAGE_LEN + 1u, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "der_trailing_byte_refused");

    verify_expect(V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN - 1u, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "der_truncated_refused");

    memcpy(pkg, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
    patch_u32(pkg, 12, 31u);
    verify_expect(pkg, 16u + 192u + 31u + 70u, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "image_len_below_native_header_refused");

    verify_expect(V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN - 8u, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "package_truncated_refused");

    verify_expect(NULL, 0, APP_ADMISSION_ERR_PARAM, "null_package_param_refused");
    verify_expect(V2_GOLDEN_PACKAGE, 8u, APP_ADMISSION_ERR_BAD_PACKAGE, "short_package_refused");
}

static void test_resent_policy_negatives(void)
{
    uint8_t tbs[V2_TBS_LEN];
    uint8_t pkg[400];
    size_t pkg_len;
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, V2_NATIVE_OFF + 8u, 0x00010000u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "native_abi_mismatch_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, V2_NATIVE_OFF + 24u, 0x00000001u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "native_reserved0_nonzero_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, 76, 0x00003011u);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_TARGET_INCOMPATIBLE,
                  "wrong_board_signed_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, 80, 128u);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_TARGET_INCOMPATIBLE,
                  "psram_over_board_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, 108, 0x93E00001u);
    patch_u32(tbs, V2_NATIVE_OFF + 12u, 0x93E00001u);
    refresh_native_digest(tbs);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_TARGET_INCOMPATIBLE,
                  "foreign_target_addr_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, 92, 40u);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    v2_policy(&policy);
    policy.exec_region_size = 36u;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, pkg, pkg_len,
                              APP_ADMISSION_ERR_RESOURCE_LIMIT,
                              "exec_region_decl_over_region_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    patch_u32(tbs, 92, 3u);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_RESOURCE_LIMIT,
                  "exec_region_decl_below_image_refused");
}

static void test_trust_negatives(void)
{
    uint8_t tbs[V2_TBS_LEN];
    uint8_t pkg[400];
    size_t pkg_len;
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;

    v2_policy(&policy);
    fixture_trust(&trust);
    trust.publisher_id = "other-publisher";
    trust.publisher_id_len = 15u;
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED,
                              "unknown_publisher_refused");

    fixture_trust(&trust);
    trust.spki_der = NULL;
    trust.spki_der_len = 0u;
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_TRUST_UNPROVEN, "missing_trust_refused");

    fixture_trust(&trust);
    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    memcpy(tbs + 144u, V2_D2_SPKI_SHA256, 32u);
    resign_tbs_key(tbs, V2_TBS_LEN, (long)D2_SCALAR, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED,
                  "self_declared_key_not_trusted");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    resign_tbs_key(tbs, V2_TBS_LEN, (long)D2_SCALAR, pkg, sizeof(pkg), &pkg_len);
    verify_expect(pkg, pkg_len, APP_ADMISSION_ERR_SIGNATURE_INVALID,
                  "foreign_key_signature_refused");
}

static void test_full_len_policy_gate(void)
{
    uint8_t tbs[V2_TBS_LEN];
    uint8_t pkg[400];
    uint8_t *huge;
    size_t pkg_len;
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;

    v2_policy(&policy);
    policy.max_package_len = 300u;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_BAD_PACKAGE,
                              "v2_golden_der_tail_over_policy_full_len_refused");

    v2_policy(&policy);
    policy.max_package_len = V2_GOLDEN_PACKAGE_LEN;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_OK, "v2_golden_full_len_at_policy_bound_accepted");

    v2_policy(&policy);
    policy.max_package_len = V2_GOLDEN_PACKAGE_LEN - 1u;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_ERR_BAD_PACKAGE,
                              "v2_golden_full_len_one_over_policy_refused");

    memcpy(tbs, V2_GOLDEN_TBS, V2_TBS_LEN);
    resign_tbs(tbs, V2_TBS_LEN, pkg, sizeof(pkg), &pkg_len);
    expect(pkg_len != 0, "v2_resign_len_gate_package");
    v2_policy(&policy);
    policy.max_package_len = pkg_len - 1u;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, pkg, pkg_len, APP_ADMISSION_ERR_BAD_PACKAGE,
                              "v2_resign_full_len_over_policy_refused");

    v2_policy(&policy);
    policy.max_package_len = pkg_len;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, pkg, pkg_len, APP_ADMISSION_OK,
                              "v2_resign_full_len_at_policy_bound_accepted");

    v2_policy(&policy);
    policy.max_package_len = 0u;
    fixture_trust(&trust);
    verify_with_policy_expect(&policy, &trust, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN,
                              APP_ADMISSION_OK, "v2_zero_policy_len_falls_to_protocol_cap");

    huge = malloc((size_t)APP_ADMISSION_MAX_PACKAGE_LEN_V2 + 1u);
    expect(huge != NULL, "v2_protocol_cap_probe_alloc");
    if (huge != NULL) {
        memset(huge, 0, (size_t)APP_ADMISSION_MAX_PACKAGE_LEN_V2 + 1u);
        memcpy(huge, V2_GOLDEN_PACKAGE, V2_GOLDEN_PACKAGE_LEN);
        v2_policy(&policy);
        policy.max_package_len = 0u;
        fixture_trust(&trust);
        verify_with_policy_expect(&policy, &trust, huge,
                                  (size_t)APP_ADMISSION_MAX_PACKAGE_LEN_V2 + 1u,
                                  APP_ADMISSION_ERR_BAD_PACKAGE,
                                  "v2_full_len_over_protocol_cap_refused");
        free(huge);
    }
}

int main(void)
{
    test_golden_positive();
    test_domain_isolation();
    test_capacity_gate();
    test_signed_variants();
    test_structural_negatives();
    test_resent_policy_negatives();
    test_trust_negatives();
    test_full_len_policy_gate();

    printf("total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
