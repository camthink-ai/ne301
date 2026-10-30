#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_admission.h"
#include "app_host_abi.h"
#include "app_trust_anchor.h"
#include "v3_vectors.h"

#define POLICY_REGION (2u * 1024u * 1024u)
#define POLICY_BASE 0x93E00000u

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

static void v3_policy(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = 0x00030000u;
    policy->abi_table_size = APP_ADMISSION_V3_ABI_TABLE_SIZE;
    policy->supported_caps_mask = 0x000000ffu;
    policy->exec_region_base = POLICY_BASE;
    policy->exec_region_size = POLICY_REGION;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    policy->event_capacity = 8192u;
    policy->report_capacity = 8192u;
    policy->state_quota_capacity = 8192u;
    policy->control_capacity = 1024u;
    policy->snapshot_capacity = 4096u;
}

static void fixture_trust(app_admission_trust_view_t *trust)
{
    memset(trust, 0, sizeof(*trust));
    trust->publisher_id = "ne301-exp-pub";
    trust->publisher_id_len = strlen(trust->publisher_id);
    trust->spki_der = V3_TEST_SPKI_DER;
    trust->spki_der_len = V3_TEST_SPKI_DER_LEN;
}

static app_admission_result_t verify_package(const uint8_t *package, size_t package_len,
                                             const app_admission_policy_t *policy,
                                             const app_admission_trust_view_t *trust)
{
    app_admission_verdict_t verdict;
    return app_admission_verify_package(package, package_len, policy, trust, &verdict);
}

static void expect_result(const uint8_t *package, size_t package_len,
                          const app_admission_policy_t *policy,
                          app_admission_result_t expected, const char *name)
{
    app_admission_trust_view_t trust;
    fixture_trust(&trust);
    expect(verify_package(package, package_len, policy, &trust) == expected, name);
}

static void test_generic_profile_accepts_v3_minimal_caps(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;

    v3_policy(&policy);
    fixture_trust(&trust);
    expect(app_admission_verify_package(V3_GENERIC_MINIMAL_PACKAGE,
                                        V3_GENERIC_MINIMAL_PACKAGE_LEN,
                                        &policy, &trust, &verdict) == APP_ADMISSION_OK,
           "generic_v3_profile_accepts_bits_0_1_5_6_without_business_bits");
    expect(verdict.required_host_abi == APP_ADMISSION_ABI_VERSION_V3 &&
           verdict.required_host_caps == 0x00000063u &&
           verdict.run_profile == APP_ADMISSION_V3_RUN_PROFILE_HOT_COMMAND_SESSION &&
           verdict.control_max == APP_ADMISSION_V3_CONTROL_WIRE_MIN &&
           verdict.snapshot_max == 0u,
           "generic_v3_minimal_verdict_keeps_declared_abi_caps_and_inclusive_quotas");
}

static void test_v3_signature_and_structure_rejections(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;

    v3_policy(&policy);
    expect_result(V3_TAMPERED_TBS_PACKAGE, V3_TAMPERED_TBS_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_SIGNATURE_INVALID, "v3_tbs_tamper_invalidates_signature");
    expect_result(V3_TAMPERED_SIGNATURE_PACKAGE, V3_TAMPERED_SIGNATURE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_SIGNATURE_INVALID, "v3_signature_tamper_invalidates_signature");
    expect_result(V3_FOREIGN_SIGNATURE_PACKAGE, V3_FOREIGN_SIGNATURE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_SIGNATURE_INVALID, "v3_foreign_signer_cannot_use_anchor_identity");
    expect_result(V3_BAD_MAGIC_PACKAGE, V3_BAD_MAGIC_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_unknown_major_magic_rejected");
    expect_result(V3_BAD_MANIFEST_MAGIC_PACKAGE, V3_BAD_MANIFEST_MAGIC_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_BAD_PACKAGE, "v3_manifest_magic_must_be_nmf3");
    expect_result(V3_BAD_MANIFEST_LENGTH_PACKAGE, V3_BAD_MANIFEST_LENGTH_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_manifest_length_must_be_192");
    expect_result(V3_NATIVE_FILE_OVER_LIMIT_PACKAGE, V3_NATIVE_FILE_OVER_LIMIT_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_native_file_length_must_fit_protocol_limit");
    expect_result(V3_NATIVE_FILE_TOO_SHORT_PACKAGE, V3_NATIVE_FILE_TOO_SHORT_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_native_file_must_contain_full_header");
    expect_result(V3_BAD_MANIFEST_ABI_PACKAGE, V3_BAD_MANIFEST_ABI_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_manifest_abi_must_be_00030000");
    expect_result(V3_BAD_NATIVE_ABI_PACKAGE, V3_BAD_NATIVE_ABI_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_native_header_abi_must_match_manifest");
    expect_result(V3_BAD_NATIVE_FORMAT_PACKAGE, V3_BAD_NATIVE_FORMAT_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_native_header_format_must_be_one");
    expect_result(V3_BAD_NATIVE_RESERVED_PACKAGE, V3_BAD_NATIVE_RESERVED_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_native_header_reserved_must_be_zero");
    expect_result(V3_BAD_NATIVE_IMAGE_SIZE_PACKAGE, V3_BAD_NATIVE_IMAGE_SIZE_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_native_header_image_size_must_match_manifest");
    expect_result(V3_BAD_NATIVE_ENTRY_MISMATCH_PACKAGE,
                  V3_BAD_NATIVE_ENTRY_MISMATCH_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_native_header_entry_must_match_manifest");
    expect_result(V3_BAD_NATIVE_ENTRY_RANGE_PACKAGE, V3_BAD_NATIVE_ENTRY_RANGE_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_entry_offset_must_be_even_and_inside_image");
    expect_result(V3_BAD_NATIVE_TARGET_MISMATCH_PACKAGE,
                  V3_BAD_NATIVE_TARGET_MISMATCH_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_native_header_target_must_match_manifest");
    expect_result(V3_BAD_NATIVE_CRC_PACKAGE, V3_BAD_NATIVE_CRC_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_native_payload_crc_must_match");
    expect_result(V3_BAD_NATIVE_SHA_PACKAGE, V3_BAD_NATIVE_SHA_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_native_file_sha256_must_match");
    expect_result(V3_BAD_MANIFEST_FILE_LEN_PACKAGE, V3_BAD_MANIFEST_FILE_LEN_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_BAD_PACKAGE,
                  "v3_manifest_native_file_length_must_match_container");
    expect_result(V3_BAD_MANIFEST_EXEC_SIZE_PACKAGE, V3_BAD_MANIFEST_EXEC_SIZE_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_RESOURCE_LIMIT,
                  "v3_resident_exec_budget_must_fit_host_region");
    expect_result(V3_BAD_PROFILE_PACKAGE, V3_BAD_PROFILE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_profile_1_is_not_reinterpreted_as_profile_2");
    expect_result(V3_MISSING_BIT5_PACKAGE, V3_MISSING_BIT5_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_profile_2_requires_session_lifecycle_bit5");
    expect_result(V3_MISSING_BIT6_PACKAGE, V3_MISSING_BIT6_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_profile_2_requires_control_receive_bit6");
    expect_result(V3_UNKNOWN_CAP_PACKAGE, V3_UNKNOWN_CAP_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_unknown_capability_bits_rejected");
    expect_result(V3_BAD_RESERVED_PACKAGE, V3_BAD_RESERVED_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_reserved_bytes_must_be_zero");
    expect_result(V3_NO_CONTROL_QUOTA_PACKAGE, V3_NO_CONTROL_QUOTA_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_control_bit_requires_nonzero_control_quota");
    expect_result(V3_SMALL_CONTROL_QUOTA_PACKAGE, V3_SMALL_CONTROL_QUOTA_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "v3_control_quota_includes_32_byte_wire_header");
    expect_result(V3_SMALL_CONTROL_TAMPERED_PACKAGE, V3_SMALL_CONTROL_TAMPERED_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_SIGNATURE_INVALID,
                  "v3_signature_is_checked_before_control_quota_policy");
    expect_result(V3_SNAPSHOT_WITHOUT_BIT7_PACKAGE, V3_SNAPSHOT_WITHOUT_BIT7_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_snapshot_quota_requires_bit7");
    expect_result(V3_BIT7_WITHOUT_SNAPSHOT_PACKAGE, V3_BIT7_WITHOUT_SNAPSHOT_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "v3_snapshot_bit_requires_nonzero_quota");
    expect_result(V3_TARGET_MISMATCH_PACKAGE, V3_TARGET_MISMATCH_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_TARGET_INCOMPATIBLE, "v3_wrong_board_is_target_incompatible");
    expect_result(V3_UNSUPPORTED_TARGET_ADDR_PACKAGE,
                  V3_UNSUPPORTED_TARGET_ADDR_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_TARGET_INCOMPATIBLE,
                  "v3_native_target_must_match_host_exec_base");
    expect_result(V3_UNSUPPORTED_PSRAM_PACKAGE, V3_UNSUPPORTED_PSRAM_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_TARGET_INCOMPATIBLE,
                  "v3_psram_requirement_must_fit_target_host");

    fixture_trust(&trust);
    trust.publisher_id = "other-publisher";
    trust.publisher_id_len = strlen(trust.publisher_id);
    expect(verify_package(V3_DER_TAIL_PACKAGE, V3_DER_TAIL_PACKAGE_LEN,
                          &policy, &trust) == APP_ADMISSION_ERR_BAD_PACKAGE,
           "v3_der_tail_is_structural_failure_before_trust_mapping");
    expect(verify_package(V3_DER_NONMINIMAL_PACKAGE, V3_DER_NONMINIMAL_PACKAGE_LEN,
                          &policy, &trust) == APP_ADMISSION_ERR_BAD_PACKAGE,
           "v3_nonminimal_der_is_structural_failure_before_trust_mapping");
    expect(verify_package(V3_DER_SCALAR_OUT_OF_RANGE_PACKAGE,
                          V3_DER_SCALAR_OUT_OF_RANGE_PACKAGE_LEN,
                          &policy, &trust) == APP_ADMISSION_ERR_BAD_PACKAGE,
           "v3_out_of_range_der_scalar_is_structural_failure_before_trust_mapping");
    expect_result(V3_WRONG_PUBLISHER_PACKAGE, V3_WRONG_PUBLISHER_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED, "v3_publisher_id_must_match_anchor_mapping");
}

static void test_v3_business_policy_and_capacities(void)
{
    app_admission_policy_t policy;

    v3_policy(&policy);
    expect_result(V3_GENERIC_SMALL_EVENT_PACKAGE, V3_GENERIC_SMALL_EVENT_PACKAGE_LEN,
                  &policy, APP_ADMISSION_ERR_ABI_INCOMPATIBLE,
                  "generic_host_policy_does_not_enable_ai_event_capability");
    expect_result(V3_RESOURCE_PACKAGE, V3_RESOURCE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_ABI_INCOMPATIBLE,
                  "generic_host_policy_does_not_enable_business_snapshot_capability");
    expect_result(V3_GENERIC_BUSINESS_QUOTAS_PACKAGE,
                  V3_GENERIC_BUSINESS_QUOTAS_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_ABI_INCOMPATIBLE,
                  "generic_host_policy_does_not_enable_business_caps_without_line_crossing_policy");

    policy.control_capacity = 512u;
    policy.snapshot_capacity = 2048u;
    expect_result(V3_RESOURCE_PACKAGE, V3_RESOURCE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_ABI_INCOMPATIBLE,
                  "larger_declared_quotas_do_not_bypass_missing_business_policy");

    v3_policy(&policy);
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    expect_result(V3_RESOURCE_PACKAGE, V3_RESOURCE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_OK,
                  "line_crossing_package_uses_explicit_budget_above_reference_host_values");
    policy.control_capacity = 511u;
    expect_result(V3_RESOURCE_PACKAGE, V3_RESOURCE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "v3_control_quota_over_host_capacity_is_rejected");
    policy.control_capacity = 512u;
    policy.snapshot_capacity = 2047u;
    expect_result(V3_RESOURCE_PACKAGE, V3_RESOURCE_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "v3_snapshot_quota_over_host_capacity_is_rejected");

    v3_policy(&policy);
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    expect_result(V3_GENERIC_BUSINESS_QUOTAS_PACKAGE,
                  V3_GENERIC_BUSINESS_QUOTAS_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE,
                  "line_crossing_policy_requires_business_snapshot_capability");

    v3_policy(&policy);
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    expect_result(V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_BAD_PACKAGE, "line_crossing_policy_requires_bits_2_through_7");
    expect_result(V3_LINE_CROSSING_PACKAGE, V3_LINE_CROSSING_PACKAGE_LEN, &policy,
                  APP_ADMISSION_OK, "trusted_line_crossing_policy_accepts_complete_caps_and_floors");
    expect_result(V3_LINE_CROSSING_SMALL_EVENT_PACKAGE,
                  V3_LINE_CROSSING_SMALL_EVENT_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "line_crossing_event_floor_is_1576_bytes");
    expect_result(V3_LINE_CROSSING_SMALL_REPORT_PACKAGE,
                  V3_LINE_CROSSING_SMALL_REPORT_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "line_crossing_report_floor_is_6144_bytes");
    v3_policy(&policy);
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    policy.event_capacity = 2047u;
    expect_result(V3_LINE_CROSSING_PACKAGE, V3_LINE_CROSSING_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "line_crossing_event_quota_must_fit_host_capacity");
    v3_policy(&policy);
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    policy.report_capacity = 6143u;
    expect_result(V3_LINE_CROSSING_PACKAGE, V3_LINE_CROSSING_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "line_crossing_report_quota_must_fit_host_capacity");
    v3_policy(&policy);
    policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    policy.state_quota_capacity = 4095u;
    expect_result(V3_LINE_CROSSING_PACKAGE, V3_LINE_CROSSING_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_RESOURCE_LIMIT, "line_crossing_state_quota_must_fit_host_capacity");
}

static void test_v3_host_abi_and_package_length_gates(void)
{
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    uint8_t *oversized;
    size_t oversized_len = (size_t)APP_ADMISSION_MAX_PACKAGE_LEN_V3 + 1u;

    v3_policy(&policy);
    policy.abi_table_size = 48u;
    expect_result(V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "v3_48_byte_v2_table_is_not_a_v3_table");
    v3_policy(&policy);
    policy.supported_caps_mask = 0x0000003fu;
    expect_result(V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "v3_missing_control_receive_host_capability_fails_closed");
    v3_policy(&policy);
    policy.abi_version = APP_HOST_ABI_VERSION;
    policy.abi_table_size = (uint32_t)sizeof(app_host_api_table_t);
    policy.supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    expect_result(V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN, &policy,
                  APP_ADMISSION_ERR_ABI_INCOMPATIBLE, "current_v1_host_policy_rejects_valid_v3_package");

    v3_policy(&policy);
    policy.max_package_len = V3_GENERIC_MINIMAL_PACKAGE_LEN;
    fixture_trust(&trust);
    expect(verify_package(V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                          &policy, &trust) == APP_ADMISSION_OK,
           "v3_full_package_length_at_policy_bound_is_accepted");
    policy.max_package_len = V3_GENERIC_MINIMAL_PACKAGE_LEN - 1u;
    expect(verify_package(V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN,
                          &policy, &trust) == APP_ADMISSION_ERR_BAD_PACKAGE,
           "v3_full_package_length_over_policy_bound_is_rejected");

    oversized = malloc(oversized_len);
    expect(oversized != NULL, "v3_protocol_length_probe_allocates");
    if (oversized != NULL) {
        memset(oversized, 0, oversized_len);
        memcpy(oversized, V3_GENERIC_MINIMAL_PACKAGE, V3_GENERIC_MINIMAL_PACKAGE_LEN);
        policy.max_package_len = 0u;
        expect(verify_package(oversized, oversized_len, &policy, &trust) ==
                   APP_ADMISSION_ERR_BAD_PACKAGE,
               "v3_full_length_over_protocol_cap_rejected_before_signature");
        free(oversized);
    }
}

int main(void)
{
    test_generic_profile_accepts_v3_minimal_caps();
    test_v3_signature_and_structure_rejections();
    test_v3_business_policy_and_capacities();
    test_v3_host_abi_and_package_length_gates();
    printf("total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
