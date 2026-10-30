#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_admission.h"
#include "app_trust_anchor.h"
#include "installer_vectors.h"

static int g_total;
static int g_fail;

static void check(int condition, const char *name)
{
    g_total++;
    if (!condition) {
        g_fail++;
        printf("FAIL %s\n", name);
    }
}

static void fill_policy(app_admission_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
    policy->target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    policy->psram_mib = 64u;
    policy->abi_version = 0x00010000u;
    policy->supported_caps_mask = APP_ADMISSION_KNOWN_CAPS_MASK;
    policy->max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN;
    policy->exec_region_base = 0x93E00000u;
    policy->exec_region_size = 2u * 1024u * 1024u;
}

int main(int argc, char **argv)
{
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
    app_admission_verdict_t verdict;
    app_admission_result_t result;
    int expected_status;
    int expected_test_package_acceptance;

    if (argc != 7) {
        return 2;
    }
    expected_status = atoi(argv[1]);
    expected_test_package_acceptance = atoi(argv[6]);

    check((int)app_trust_anchor_status() == expected_status,
          "anchor_status_matches_profile_case");
    check(strcmp(app_trust_anchor_profile_str(), argv[2]) == 0,
          "compiled_profile_identity_matches");
    check(strcmp(app_trust_anchor_provenance_str(), argv[5]) == 0,
          "compiled_provenance_matches");
    check(strcmp(app_trust_anchor_spki_sha256_hex(), argv[4]) == 0,
          "compiled_spki_digest_matches");
    check(anchor != NULL, "production_anchor_descriptor_exists");
    if (anchor != NULL) {
        check(strcmp(anchor->publisher_id, argv[3]) == 0,
              "compiled_publisher_matches");
        if (expected_status == APP_TRUST_ANCHOR_UNPROVISIONED) {
            check(anchor->spki_der == NULL && anchor->spki_der_len == 0u,
                  "unprovisioned_anchor_has_no_spki");
        }
    }
    if (expected_status == APP_TRUST_ANCHOR_PROVISIONED) {
        fill_policy(&policy);
        memset(&trust, 0, sizeof(trust));
        trust.publisher_id = anchor->publisher_id;
        trust.publisher_id_len = anchor->publisher_id_len;
        trust.spki_der = anchor->spki_der;
        trust.spki_der_len = anchor->spki_der_len;
        result = app_admission_verify_package(HELLO_V1_PACKAGE,
                                              HELLO_V1_PACKAGE_LEN,
                                              &policy, &trust, &verdict);
        if (expected_test_package_acceptance != 0) {
            check(result == APP_ADMISSION_OK,
                  "test_package_is_valid_under_test_profile");
        } else {
            check(result != APP_ADMISSION_OK,
                  "test_package_is_rejected_under_production_profile");
        }
    } else {
        check(expected_test_package_acceptance == 0,
              "unprovisioned_or_malformed_profile_refuses_test_package");
    }
    printf("profile=%s publisher=%s spki_sha256=%s provenance=%s status=%s\n",
           app_trust_anchor_profile_str(),
           (anchor != NULL && anchor->publisher_id != NULL) ? anchor->publisher_id : "null",
           app_trust_anchor_spki_sha256_hex(),
           app_trust_anchor_provenance_str(),
           app_trust_anchor_status_str(app_trust_anchor_status()));
    printf("test_app_trust_profile: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
