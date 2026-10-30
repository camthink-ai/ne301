#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "app_sidecar_verify.h"
#include "sidecar_vectors.h"

#define BUILD_DIR "build/"
#define CB_MAX 16u

typedef struct {
    const char *path;
    uint32_t len;
    const char *sha_hex;
} sc_counting_vec_t;

static const sc_counting_vec_t SC_COUNTING[] = { SC_VEC_COUNTING_FILES };

static int g_total;
static int g_fail;

static app_sidecar_file_desc_t g_files[CB_MAX];
static unsigned g_file_n;
static unsigned g_file_overflow;

static void expect(int cond, const char *name)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL %s\n", name);
    }
}

static void expect_hex(const uint8_t *got, const char *want_hex, size_t len, const char *name)
{
    uint8_t want[64];
    size_t i;

    g_total++;
    if (len > 64u) {
        g_fail++;
        printf("FAIL %s\n", name);
        return;
    }
    for (i = 0u; i < len; i++) {
        unsigned hi;
        unsigned lo;
        char c0 = want_hex[i * 2u];
        char c1 = want_hex[i * 2u + 1u];
        hi = (unsigned)((c0 >= '0' && c0 <= '9') ? (c0 - '0') : (c0 - 'a' + 10));
        lo = (unsigned)((c1 >= '0' && c1 <= '9') ? (c1 - '0') : (c1 - 'a' + 10));
        want[i] = (uint8_t)((hi << 4) | lo);
    }
    if (memcmp(got, want, len) != 0) {
        g_fail++;
        printf("FAIL %s\n", name);
    }
}

static void file_cb(void *user, const app_sidecar_file_desc_t *desc)
{
    (void)user;
    if (g_file_n < CB_MAX) {
        g_files[g_file_n] = *desc;
        g_file_n++;
    } else {
        g_file_overflow++;
    }
}

static void file_cb_reset(void)
{
    memset(g_files, 0, sizeof(g_files));
    g_file_n = 0u;
    g_file_overflow = 0u;
}

static void test_trust(app_sidecar_trust_t *t)
{
    memset(t, 0, sizeof(*t));
    t->publisher_id = SC_VEC_PUBLISHER_ID;
    t->publisher_id_len = SC_VEC_PUBLISHER_ID_LEN;
    t->app_id = SC_VEC_APP_ID;
    t->app_id_len = SC_VEC_APP_ID_LEN;
    t->spki_der = SC_VEC_SPKI_DER;
    t->spki_der_len = SC_VEC_SPKI_LEN;
    t->native_tbs_sha256 = SC_VEC_NATIVE_TBS;
}

static uint8_t *load_file(const char *name, size_t *len_out)
{
    char path[256];
    FILE *f;
    uint8_t *buf;
    size_t cap = 2200000u;
    size_t n;

    (void)snprintf(path, sizeof(path), "%s%s", BUILD_DIR, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    buf = malloc(cap);
    if (buf == NULL) {
        (void)fclose(f);
        return NULL;
    }
    n = fread(buf, 1u, cap, f);
    (void)fclose(f);
    *len_out = n;
    return buf;
}

static int run_case(const char *name, int expected, const app_sidecar_trust_t *trust,
                    size_t chunk, app_sidecar_verdict_t *vout)
{
    uint8_t *data;
    size_t len = 0u;
    app_sidecar_verifier_t v;
    app_sidecar_result_t rc;
    size_t pos = 0u;
    int ok;

    data = load_file(name, &len);
    if (data == NULL) {
        g_total++;
        g_fail++;
        printf("FAIL load %s\n", name);
        return 0;
    }
    file_cb_reset();
    rc = app_sidecar_verifier_init(&v, trust, file_cb, NULL);
    if (rc != APP_SIDECAR_OK) {
        ok = (rc == (app_sidecar_result_t)expected);
    } else {
        ok = 1;
        while (pos < len) {
            size_t take = len - pos;
            if (take > chunk) {
                take = chunk;
            }
            rc = app_sidecar_verify_consume(&v, data + pos, take);
            if (rc != APP_SIDECAR_OK) {
                break;
            }
            pos += take;
        }
        if (rc == APP_SIDECAR_OK) {
            rc = app_sidecar_verify_finish(&v, vout);
        }
        ok = (rc == (app_sidecar_result_t)expected);
    }
    g_total++;
    if (!ok) {
        g_fail++;
        printf("FAIL case %s rc=%d want=%d\n", name, (int)rc, expected);
    }
    free(data);
    return ok;
}

static int fixtures_share_tbs(const char *positive_name, const char *negative_name)
{
    uint8_t *positive;
    uint8_t *negative;
    size_t positive_len = 0u;
    size_t negative_len = 0u;
    size_t tbs_len = APP_SIDECAR_HEADER_SIZE + SC_VEC_SMALL_MANIFEST_LEN +
                     SC_VEC_SMALL_PAYLOAD_LEN;
    int matches;

    positive = load_file(positive_name, &positive_len);
    negative = load_file(negative_name, &negative_len);
    if ((positive == NULL) || (negative == NULL)) {
        free(positive);
        free(negative);
        return 0;
    }
    matches = (positive_len > tbs_len) && (negative_len > tbs_len) &&
              (memcmp(positive, negative, tbs_len) == 0);
    free(positive);
    free(negative);
    return matches;
}

static void test_negatives(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;
    size_t i;

    test_trust(&trust);
    for (i = 0u; i < (sizeof(SC_VEC_NEG_CASES) / sizeof(SC_VEC_NEG_CASES[0])); i++) {
        const char *name = SC_VEC_NEG_CASES[i].file;
        int expected = SC_VEC_NEG_CASES[i].expected;
        size_t chunk = (expected == APP_SIDECAR_OK) ? 4096u : 2200000u;
        (void)run_case(name, expected, &trust, chunk, &verdict);
    }
}

static void test_signed_strict_der(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;

    test_trust(&trust);
    if (run_case("positive_strict_der_r_msb.bin", APP_SIDECAR_OK, &trust, 4096u, &verdict)) {
        expect(memcmp(verdict.ui_tbs_sha256, SC_VEC_UI_TBS_SMALL, 32u) == 0,
               "strict_der_r_positive_tbs");
    }
    (void)run_case("negative_strict_der_r_msb.bin", APP_SIDECAR_ERR_BAD_FORMAT,
                   &trust, 2200000u, &verdict);
    expect(fixtures_share_tbs("positive_strict_der_r_msb.bin",
                              "negative_strict_der_r_msb.bin"),
           "strict_der_r_same_tbs");
    if (run_case("positive_strict_der_s_msb.bin", APP_SIDECAR_OK, &trust, 4096u, &verdict)) {
        expect(memcmp(verdict.ui_tbs_sha256, SC_VEC_UI_TBS_SMALL, 32u) == 0,
               "strict_der_s_positive_tbs");
    }
    (void)run_case("negative_strict_der_s_msb.bin", APP_SIDECAR_ERR_BAD_FORMAT,
                   &trust, 2200000u, &verdict);
    expect(fixtures_share_tbs("positive_strict_der_s_msb.bin",
                              "negative_strict_der_s_msb.bin"),
           "strict_der_s_same_tbs");
}

static void test_callback_auth_boundary(void)
{
    static const char *const cases[] = {
        "neg_sig_lowbit_flip.bin",
        "neg_sig_foreign_key.bin"
    };
    static const char *const callback_names[] = {
        "invalid_signature_zero_trusted_callbacks",
        "wrong_key_zero_trusted_callbacks"
    };
    static const char *const output_names[] = {
        "invalid_signature_zero_trusted_output",
        "wrong_key_zero_trusted_output"
    };
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;
    uint8_t sentinel[sizeof(verdict)];
    uint8_t *data;
    size_t len = 0u;
    size_t tbs_len = APP_SIDECAR_HEADER_SIZE + SC_VEC_SMALL_MANIFEST_LEN +
                     SC_VEC_SMALL_PAYLOAD_LEN;
    app_sidecar_verifier_t v;
    app_sidecar_result_t rc;
    size_t i;

    test_trust(&trust);
    for (i = 0u; i < (sizeof(cases) / sizeof(cases[0])); i++) {
        (void)memset(&verdict, 0xA5, sizeof(verdict));
        (void)memset(sentinel, 0xA5, sizeof(sentinel));
        (void)run_case(cases[i], APP_SIDECAR_ERR_SIGNATURE_INVALID,
                       &trust, 2200000u, &verdict);
        expect(g_file_n == 0u, callback_names[i]);
        expect(memcmp(&verdict, sentinel, sizeof(verdict)) == 0, output_names[i]);
    }

    data = load_file("positive_small.bin", &len);
    expect(data != NULL, "callback_boundary_load");
    if (data == NULL) {
        return;
    }
    file_cb_reset();
    rc = app_sidecar_verifier_init(&v, &trust, file_cb, NULL);
    expect(rc == APP_SIDECAR_OK, "callback_boundary_init");
    if (rc == APP_SIDECAR_OK) {
        rc = app_sidecar_verify_consume(&v, data, tbs_len);
        expect(rc == APP_SIDECAR_OK, "callback_boundary_assets_consume");
        expect(g_file_n == 0u, "callback_boundary_before_signature");
        if (rc == APP_SIDECAR_OK) {
            rc = app_sidecar_verify_consume(&v, data + tbs_len, len - tbs_len);
            expect(rc == APP_SIDECAR_OK, "callback_boundary_signature_consume");
            expect(g_file_n == 0u, "callback_boundary_before_finish");
            if (rc == APP_SIDECAR_OK) {
                rc = app_sidecar_verify_finish(&v, &verdict);
                expect(rc == APP_SIDECAR_OK, "callback_boundary_finish");
                expect(g_file_n == 3u, "callback_boundary_after_finish");
            }
        }
    }
    free(data);
}

static void test_positive_small_fields(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;

    test_trust(&trust);
    memset(&verdict, 0xAA, sizeof(verdict));
    if (run_case("positive_small.bin", APP_SIDECAR_OK, &trust, 1u, &verdict)) {
        expect(strcmp(verdict.publisher_id, SC_VEC_PUBLISHER_ID) == 0, "small_verdict_publisher");
        expect(strcmp(verdict.app_id, SC_VEC_APP_ID) == 0, "small_verdict_app");
        expect(verdict.version_major == 1u, "small_verdict_major");
        expect(verdict.version_minor == 4u, "small_verdict_minor");
        expect(verdict.version_patch == 2u, "small_verdict_patch");
        expect(verdict.host_ui_abi == 1u, "small_verdict_abi");
        expect(verdict.file_count == 3u, "small_verdict_file_count");
        expect(verdict.manifest_len == SC_VEC_SMALL_MANIFEST_LEN, "small_verdict_manifest_len");
        expect(verdict.asset_payload_len == SC_VEC_SMALL_PAYLOAD_LEN, "small_verdict_payload_len");
        expect((verdict.signature_len >= APP_SIDECAR_DER_LEN_MIN) &&
               (verdict.signature_len <= APP_SIDECAR_DER_LEN_MAX), "small_verdict_sig_len");
        expect(memcmp(verdict.ui_tbs_sha256, SC_VEC_UI_TBS_SMALL, 32u) == 0,
               "small_verdict_ui_tbs");
        expect(g_file_n == 3u, "small_cb_count");
        expect(g_file_overflow == 0u, "small_cb_overflow");
        expect(strcmp(g_files[0u].path, "assets/app.min.js") == 0, "small_cb_path0");
        expect(g_files[0u].file_len == 512u, "small_cb_len0");
        expect(strcmp(g_files[1u].path, "index.html") == 0, "small_cb_path1");
        expect(g_files[1u].file_len == 128u, "small_cb_len1");
        expect(strcmp(g_files[2u].path, "libs/lib-a.min.js") == 0, "small_cb_path2");
        expect(g_files[2u].file_len == 256u, "small_cb_len2");
    }
}

static void test_positive_counting_fields(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;
    unsigned i;

    test_trust(&trust);
    memset(&verdict, 0, sizeof(verdict));
    if (run_case("positive_counting.bin", APP_SIDECAR_OK, &trust, 4096u, &verdict)) {
        expect(verdict.version_major == 2u, "counting_verdict_major");
        expect(verdict.version_minor == 0u, "counting_verdict_minor");
        expect(verdict.version_patch == 1u, "counting_verdict_patch");
        expect(verdict.file_count == SC_VEC_COUNTING_FILE_COUNT, "counting_verdict_count");
        expect(verdict.manifest_len == SC_VEC_COUNTING_MANIFEST_LEN, "counting_verdict_manifest");
        expect(verdict.asset_payload_len == SC_VEC_COUNTING_PAYLOAD_LEN,
               "counting_verdict_payload");
        expect(verdict.asset_payload_len == 417036u, "counting_real_magnitude");
        expect(memcmp(verdict.ui_tbs_sha256, SC_VEC_UI_TBS_COUNTING, 32u) == 0,
               "counting_verdict_ui_tbs");
        expect(g_file_n == SC_VEC_COUNTING_FILE_COUNT, "counting_cb_count");
        expect(g_file_overflow == 0u, "counting_cb_overflow");
        for (i = 0u; i < SC_VEC_COUNTING_FILE_COUNT; i++) {
            expect(strcmp(g_files[i].path, SC_COUNTING[i].path) == 0, "counting_cb_path");
            expect(g_files[i].file_len == SC_COUNTING[i].len, "counting_cb_len");
            expect_hex(g_files[i].sha256, SC_COUNTING[i].sha_hex, 32u, "counting_cb_sha");
        }
    }
}

static void test_resign_idempotent_identity(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t v1;
    app_sidecar_verdict_t v2;

    test_trust(&trust);
    (void)run_case("positive_small.bin", APP_SIDECAR_OK, &trust, 4096u, &v1);
    (void)run_case("positive_resign_same_tbs.bin", APP_SIDECAR_OK, &trust, 4096u, &v2);
    expect(memcmp(v1.ui_tbs_sha256, v2.ui_tbs_sha256, 32u) == 0, "resign_same_ui_tbs");
    expect(v1.version_major == v2.version_major, "resign_same_version");
    expect(v1.asset_payload_len == v2.asset_payload_len, "resign_same_payload");
    expect(v1.signature_len == v2.signature_len, "resign_der_len_distinct_content_id");
}

static void test_trust_faults(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verifier_t v;
    app_sidecar_verdict_t verdict;
    app_sidecar_result_t rc;
    uint8_t byte_buf = 0u;

    test_trust(&trust);
    rc = app_sidecar_verifier_init(NULL, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_PARAM, "init_null_verifier");
    rc = app_sidecar_verifier_init(&v, NULL, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_PARAM, "init_null_trust");

    test_trust(&trust);
    trust.native_tbs_sha256 = NULL;
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_NO_TRUST, "init_no_native_tbs");

    test_trust(&trust);
    trust.spki_der = NULL;
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_NO_TRUST, "init_no_spki");

    test_trust(&trust);
    trust.spki_der_len = SC_VEC_SPKI_LEN - 1u;
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_NO_TRUST, "init_short_spki");

    test_trust(&trust);
    trust.spki_der = SC_VEC_SPKI_BAD_POINT;
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_NO_TRUST, "init_offcurve_spki");

    test_trust(&trust);
    trust.publisher_id_len = 0u;
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_ERR_NO_TRUST, "init_empty_publisher");

    test_trust(&trust);
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_OK, "init_ok");
    rc = app_sidecar_verify_finish(&v, &verdict);
    expect(rc == APP_SIDECAR_ERR_BAD_FORMAT, "finish_without_consume");
    rc = app_sidecar_verify_consume(&v, NULL, 1u);
    expect(rc == APP_SIDECAR_ERR_PARAM, "consume_null_data");
    rc = app_sidecar_verify_consume(&v, &byte_buf, 0u);
    expect(rc == APP_SIDECAR_OK, "consume_zero_len_noop");
    (void)memset(&verdict, 0, sizeof(verdict));
    rc = app_sidecar_verify_finish(&v, &verdict);
    expect(rc == APP_SIDECAR_ERR_BAD_FORMAT, "finish_header_state");
}

static void test_misuse_after_finish(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verifier_t v;
    app_sidecar_verdict_t verdict;
    app_sidecar_result_t rc;
    uint8_t *data;
    size_t len = 0u;

    test_trust(&trust);
    data = load_file("positive_small.bin", &len);
    expect(data != NULL, "misuse_load");
    if (data == NULL) {
        return;
    }
    rc = app_sidecar_verifier_init(&v, &trust, NULL, NULL);
    expect(rc == APP_SIDECAR_OK, "misuse_init");
    rc = app_sidecar_verify_consume(&v, data, len);
    expect(rc == APP_SIDECAR_OK, "misuse_consume");
    rc = app_sidecar_verify_finish(&v, &verdict);
    expect(rc == APP_SIDECAR_OK, "misuse_finish");
    rc = app_sidecar_verify_finish(&v, &verdict);
    expect(rc == APP_SIDECAR_ERR_PARAM, "finish_twice");
    rc = app_sidecar_verify_consume(&v, data, 1u);
    expect(rc == APP_SIDECAR_ERR_PARAM, "consume_after_finish");
    free(data);
}

static void test_bounded_state(void)
{
    app_sidecar_trust_t trust;
    app_sidecar_verdict_t verdict;

    expect(sizeof(app_sidecar_verifier_t) <= 20480u, "verifier_state_bounded");
    test_trust(&trust);
    (void)run_case("positive_counting.bin", APP_SIDECAR_OK, &trust, 9973u, &verdict);
    (void)run_case("positive_small.bin", APP_SIDECAR_OK, &trust, 16u, &verdict);
}

int main(void)
{
    test_negatives();
    test_signed_strict_der();
    test_callback_auth_boundary();
    test_positive_small_fields();
    test_positive_counting_fields();
    test_resign_idempotent_identity();
    test_trust_faults();
    test_misuse_after_finish();
    test_bounded_state();

    printf("total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
