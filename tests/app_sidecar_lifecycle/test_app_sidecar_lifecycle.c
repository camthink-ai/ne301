#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_sidecar_lifecycle.h"
#include "app_pair_installer.h"
#include "app_installer.h"
#include "app_state_store.h"
#include "app_sidecar_verify.h"
#include "app_trust_anchor.h"
#include "app_host_abi.h"
#include "app_host_control.h"
#include "app_host_live.h"
#include "generic_math.h"
#include "mbedtls/sha256.h"
#include "mem_sidecar_backend.h"
#include "sidecar_vectors.h"

static int g_total;
static int g_fail;

#define SC_NATIVE_PACKAGE_LEN 100u

typedef struct {
    char paths[MEM_MAX_FILES][128];
    uint8_t *data[MEM_MAX_FILES];
    size_t lengths[MEM_MAX_FILES];
    size_t count;
} file_snapshot_t;

typedef struct {
    app_uninstall_purge_result_t result;
    int calls;
} cleanup_ctx_t;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void make_pair_history(app_pair_history_t *history, uint64_t seq,
                              uint16_t native_patch, uint16_t ui_patch,
                              uint8_t native_tag, uint8_t ui_tag, uint8_t pair_tag)
{
    memset(history, 0, sizeof(*history));
    snprintf(history->publisher_id, sizeof(history->publisher_id), "%s", "ne301-exp-pub");
    snprintf(history->app_id, sizeof(history->app_id), "%s", "line-crossing");
    memset(history->publisher_key_sha256, 0x06, sizeof(history->publisher_key_sha256));
    memset(history->native_tbs_sha256, native_tag, sizeof(history->native_tbs_sha256));
    memset(history->ui_tbs_sha256, ui_tag, sizeof(history->ui_tbs_sha256));
    memset(history->pair_tbs_sha256, pair_tag, sizeof(history->pair_tbs_sha256));
    history->native_version[0] = 4u;
    history->native_version[2] = native_patch;
    history->ui_version[0] = 1u;
    history->ui_version[2] = ui_patch;
    history->release_seq = seq;
}

static void test_pair_history_policy(void)
{
    app_pair_history_t previous;
    app_pair_history_t candidate;

    make_pair_history(&previous, 3u, 3u, 3u, 0x11u, 0x22u, 0x33u);
    make_pair_history(&candidate, 3u, 3u, 3u, 0x11u, 0x22u, 0x33u);
    CHECK(app_pair_history_check(NULL, &candidate) == APP_PAIR_HISTORY_FRESH);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_IDEMPOTENT);
    candidate.release_seq = 2u;
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_DOWNGRADE);
    candidate.release_seq = 3u;
    candidate.pair_tbs_sha256[0] ^= 1u;
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT);
    make_pair_history(&candidate, 4u, 2u, 4u, 0x44u, 0x55u, 0x66u);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_DOWNGRADE);
    make_pair_history(&candidate, 4u, 4u, 2u, 0x44u, 0x55u, 0x66u);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_DOWNGRADE);
    make_pair_history(&candidate, 4u, 3u, 4u, 0x11u, 0x44u, 0x66u);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_UPGRADE);
    make_pair_history(&candidate, 4u, 4u, 3u, 0x44u, 0x55u, 0x66u);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_UPGRADE);
    make_pair_history(&candidate, 4u, 3u, 3u, 0x44u, 0x22u, 0x66u);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT);
    make_pair_history(&candidate, 4u, 3u, 3u, 0x11u, 0x44u, 0x66u);
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT);
    make_pair_history(&candidate, 4u, 4u, 4u, 0x44u, 0x55u, 0x66u);
    candidate.publisher_key_sha256[0] ^= 1u;
    CHECK(app_pair_history_check(&previous, &candidate) == APP_PAIR_HISTORY_ERR_IDENTITY);
}

typedef struct {
    app_admission_policy_t policy;
    app_admission_trust_view_t trust;
} pair_test_verify_ctx_t;

static app_admission_result_t pair_test_native_verify(const uint8_t *data,
                                                       size_t len,
                                                       app_admission_verdict_t *out,
                                                       void *ctx)
{
    pair_test_verify_ctx_t *vctx = (pair_test_verify_ctx_t *)ctx;
    app_admission_result_t rc = app_admission_verify_package(
        data, len, &vctx->policy, &vctx->trust, out);
    return rc;
}

static app_uninstall_purge_result_t pair_test_purge_confirmed(
    void *ctx,
    const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_CONFIRMED;
}

static void test_nepk_candidate_host_lifecycle(
    const uint8_t *package,
    size_t len,
    pair_test_verify_ctx_t *verify_ctx,
    app_sidecar_trust_t *trust)
{
    app_install_io_t io;
    app_install_record_t native;
    app_install_record_t removed;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_descriptor_t recovered;
    app_sidecar_lifecycle_state_t state;
    app_pair_history_t history;
    app_uninstall_cleanup_ops_t cleanup_ops;
    app_uninstall_state_t uninstall_state;
    app_uninstall_result_t uninstall_result;
    app_pair_install_result_t install_result;
    aicam_bool_t has_history = AICAM_FALSE;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
    install_result = app_pair_install(&io, pair_test_native_verify, verify_ctx,
                                       trust, package, len, &native,
                                       &descriptor, &state);
    CHECK(install_result == APP_PAIR_INSTALL_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(app_pair_is_authorized(&io, &native, &descriptor) == AICAM_TRUE);
    CHECK(app_sidecar_lifecycle_pair_history_query(&io, &history,
                                                   &has_history) ==
          APP_SIDECAR_LIFECYCLE_OK);
    CHECK(has_history == AICAM_TRUE && history.release_seq == 3u);
    install_result = app_pair_install(&io, pair_test_native_verify, verify_ctx,
                                       trust, package, len, &native,
                                       &descriptor, &state);
    CHECK(install_result == APP_PAIR_INSTALL_ALREADY);

    app_install_boot_resolve_reset();
    CHECK(app_install_boot_resolve(&io) == APP_INSTALL_OK);
    CHECK(app_install_query_committed(&io, &native, &has_history) == APP_INSTALL_OK);
    CHECK(has_history == AICAM_TRUE);
    trust->app_id = native.app_id;
    trust->app_id_len = strlen(native.app_id);
    trust->native_tbs_sha256 = native.tbs_sha256;
    CHECK(app_sidecar_lifecycle_boot_resolve(&io, trust, &native,
                                              &recovered, &state) ==
          APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(app_pair_is_authorized(&io, &native, &recovered) == AICAM_TRUE);

    memset(&cleanup_ops, 0, sizeof(cleanup_ops));
    cleanup_ops.purge = pair_test_purge_confirmed;
    uninstall_result = app_install_uninstall_confirmed(
        &io, AICAM_TRUE, &cleanup_ops, &removed, &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_OK);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_NONE);
    CHECK(app_sidecar_lifecycle_pair_history_query(&io, &history,
                                                   &has_history) ==
          APP_SIDECAR_LIFECYCLE_OK);
    CHECK(has_history == AICAM_TRUE && history.release_seq == 3u);

    install_result = app_pair_install(&io, pair_test_native_verify, verify_ctx,
                                       trust, package, len, &native,
                                       &descriptor, &state);
    CHECK(install_result == APP_PAIR_INSTALL_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(app_pair_is_authorized(&io, &native, &descriptor) == AICAM_TRUE);
}

static void test_nepk_ci_candidate(void)
{
    const char *path = getenv("NEPK_CI_CANDIDATE");
    const app_trust_anchor_t *anchor;
    pair_test_verify_ctx_t verify_ctx;
    app_sidecar_trust_t trust;
    app_pair_candidate_t candidate;
    app_pair_candidate_t rejected;
    app_pair_install_result_t result;
    FILE *fp;
    long file_len;
    uint8_t *package;
    uint8_t *tampered = NULL;
    size_t len;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    fp = fopen(path, "rb");
    CHECK(fp != NULL);
    if (fp == NULL) {
        return;
    }
    CHECK(fseek(fp, 0, SEEK_END) == 0);
    file_len = ftell(fp);
    CHECK(file_len > 0);
    CHECK(fseek(fp, 0, SEEK_SET) == 0);
    if (file_len <= 0) {
        fclose(fp);
        return;
    }
    len = (size_t)file_len;
    package = (uint8_t *)malloc(len + 1u);
    CHECK(package != NULL);
    if (package == NULL) {
        fclose(fp);
        return;
    }
    tampered = (uint8_t *)malloc(len + 1u);
    CHECK(tampered != NULL);
    if (tampered == NULL) {
        free(package);
        return;
    }
    CHECK(fread(package, 1u, len, fp) == len);
    fclose(fp);
    anchor = app_trust_anchor_get();
    memset(&verify_ctx, 0, sizeof(verify_ctx));
    verify_ctx.policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    verify_ctx.policy.psram_mib = 64u;
    verify_ctx.policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    verify_ctx.policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK |
                                            APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                            APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    verify_ctx.policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V4;
    verify_ctx.policy.event_capacity = 16384u;
    verify_ctx.policy.state_quota_capacity = 8192u;
    verify_ctx.policy.control_capacity = APP_CONTROL_MSG_RAM_MAX;
    verify_ctx.policy.snapshot_capacity = APP_CONTROL_SNAPSHOT_RAM_MAX;
    verify_ctx.policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    verify_ctx.policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    verify_ctx.policy.live_publish_capacity = APP_HOST_LIVE_PUBLISH_CAPACITY;
    verify_ctx.policy.live_track_capacity = APP_HOST_LIVE_TRACK_CAPACITY;
    verify_ctx.policy.exec_region_base = 0x93E00000u;
    verify_ctx.policy.exec_region_size = 2u * 1024u * 1024u;
    verify_ctx.trust.publisher_id = anchor->publisher_id;
    verify_ctx.trust.publisher_id_len = anchor->publisher_id_len;
    verify_ctx.trust.spki_der = anchor->spki_der;
    verify_ctx.trust.spki_der_len = anchor->spki_der_len;
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    result = app_pair_verify_package(package, len, pair_test_native_verify,
                                     &verify_ctx, &trust, &candidate);
    if (result != APP_PAIR_INSTALL_OK) {
        printf("NEPK candidate verifier failed: %s (%d)\n",
               app_pair_install_result_str(result), result);
    }
    CHECK(result == APP_PAIR_INSTALL_OK);
    if (result == APP_PAIR_INSTALL_OK) {
        CHECK(candidate.history.release_seq == 3u);
        CHECK(candidate.history.native_version[0] == 4u);
        CHECK(candidate.history.native_version[1] == 0u);
        CHECK(candidate.history.native_version[2] == 3u);
        CHECK(candidate.history.ui_version[0] == 1u);
        CHECK(candidate.history.ui_version[2] == 3u);
        memcpy(tampered, package, len);
        tampered[candidate.pair_tbs_len - 1u] ^= 0x80u;
        CHECK(app_pair_verify_package(tampered, len, pair_test_native_verify,
                                      &verify_ctx, &trust, &rejected) ==
              APP_PAIR_INSTALL_ERR_PAIR_SIGNATURE);
        memcpy(tampered, package, len);
        tampered[len] = 0u;
        CHECK(app_pair_verify_package(tampered, len + 1u, pair_test_native_verify,
                                      &verify_ctx, &trust, &rejected) ==
              APP_PAIR_INSTALL_ERR_PAIR_SIGNATURE);
        memcpy(tampered, package, len);
        tampered[6] = 0u;
        CHECK(app_pair_verify_package(tampered, len, pair_test_native_verify,
                                      &verify_ctx, &trust, &rejected) ==
              APP_PAIR_INSTALL_ERR_BAD_FORMAT);
        test_nepk_candidate_host_lifecycle(package, len, &verify_ctx, &trust);
    }
    free(tampered);
    free(package);
}

static uint8_t *load_fixture(const char *name, size_t *len)
{
    char path[256];
    FILE *fp;
    long size;
    uint8_t *data;

    snprintf(path, sizeof(path), "../app_sidecar/build/%s", name);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *len = (size_t)size;
    return data;
}

static uint8_t *load_limit_fixture(size_t *len)
{
    FILE *fp;
    long size;
    uint8_t *data;

    fp = fopen("build/positive_limit_64.bin", "rb");
    if (fp == NULL || fseek(fp, 0, SEEK_END) != 0) {
        if (fp != NULL) {
            fclose(fp);
        }
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *len = (size_t)size;
    return data;
}

static uint8_t *load_local_fixture(const char *name, size_t *len)
{
    char path[256];
    FILE *fp;
    long size;
    uint8_t *data;

    snprintf(path, sizeof(path), "build/%s", name);
    fp = fopen(path, "rb");
    if (fp == NULL || fseek(fp, 0, SEEK_END) != 0) {
        if (fp != NULL) {
            fclose(fp);
        }
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *len = (size_t)size;
    return data;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int bytes_match_hex(const uint8_t *data, size_t len, const char *hex)
{
    size_t i;

    if (data == NULL || hex == NULL || strlen(hex) != (len * 2u)) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        int high = hex_digit(hex[i * 2u]);
        int low = hex_digit(hex[(i * 2u) + 1u]);
        if (high < 0 || low < 0 || data[i] != (uint8_t)((high << 4) | low)) {
            return 0;
        }
    }
    return 1;
}

static void set_package_path(app_install_record_t *record)
{
    size_t i;
    size_t pos = 0u;

    pos = (size_t)snprintf(record->package_path, sizeof(record->package_path), "%s/",
                           APP_INSTALL_PKG_DIR);
    for (i = 0u; i < APP_INSTALL_SHA256_LEN; i++) {
        pos += (size_t)snprintf(record->package_path + pos,
                                sizeof(record->package_path) - pos,
                                "%02x", record->package_sha256[i]);
    }
    (void)snprintf(record->package_path + pos, sizeof(record->package_path) - pos,
                   ".neapp");
}

static void build_native_package(uint8_t package[SC_NATIVE_PACKAGE_LEN])
{
    size_t i;

    for (i = 0u; i < SC_NATIVE_PACKAGE_LEN; i++) {
        package[i] = (uint8_t)(0x41u + (i % 0x1Bu));
    }
}

static void make_native_record(app_install_io_t *io, app_install_record_t *record)
{
    uint8_t encoded[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t key_sha[APP_INSTALL_SHA256_LEN];
    uint8_t package[SC_NATIVE_PACKAGE_LEN];

    memset(record, 0, sizeof(*record));
    snprintf(record->publisher_id, sizeof(record->publisher_id), "%s", SC_VEC_PUBLISHER_ID);
    snprintf(record->app_id, sizeof(record->app_id), "%s", SC_VEC_APP_ID);
    record->version_major = 1u;
    record->version_minor = 0u;
    record->version_patch = 0u;
    build_native_package(package);
    record->package_len = (uint32_t)sizeof(package);
    mbedtls_sha256(package, sizeof(package), record->package_sha256, 0);
    memcpy(record->tbs_sha256, SC_VEC_NATIVE_TBS, sizeof(record->tbs_sha256));
    mbedtls_sha256(SC_VEC_SPKI_DER, SC_VEC_SPKI_LEN, key_sha, 0);
    memcpy(record->publisher_key_sha256, key_sha, sizeof(key_sha));
    memset(record->image_sha256, 0x27, sizeof(record->image_sha256));
    set_package_path(record);
    CHECK(app_install_record_encode(record, encoded, sizeof(encoded)) == AICAM_TRUE);
    CHECK(io->kv_write(io->ctx, APP_INSTALL_KEY_RECORD, encoded, sizeof(encoded)) ==
          APP_INSTALL_IO_OK);
    CHECK(io->file_write(io->ctx, record->package_path, package, sizeof(package)) ==
          APP_INSTALL_IO_OK);
}

static void restore_native_package(app_install_io_t *io,
                                   const app_install_record_t *record)
{
    uint8_t package[SC_NATIVE_PACKAGE_LEN];

    build_native_package(package);
    CHECK(io->file_write(io->ctx, record->package_path, package, sizeof(package)) ==
          APP_INSTALL_IO_OK);
}

static void make_trust(app_sidecar_trust_t *trust, app_install_record_t *native)
{
    trust->publisher_id = native->publisher_id;
    trust->publisher_id_len = strlen(native->publisher_id);
    trust->app_id = native->app_id;
    trust->app_id_len = strlen(native->app_id);
    trust->spki_der = SC_VEC_SPKI_DER;
    trust->spki_der_len = SC_VEC_SPKI_LEN;
    trust->native_tbs_sha256 = native->tbs_sha256;
}

static void file_snapshot_take(file_snapshot_t *snapshot)
{
    size_t i;

    memset(snapshot, 0, sizeof(*snapshot));
    for (i = 0u; i < MEM_MAX_FILES; i++) {
        mem_file_t *file = &g_io.files[i];
        if (file->present != AICAM_TRUE) {
            continue;
        }
        CHECK(snapshot->count < MEM_MAX_FILES);
        if (snapshot->count >= MEM_MAX_FILES) {
            return;
        }
        snprintf(snapshot->paths[snapshot->count], sizeof(snapshot->paths[0]), "%s",
                 file->path);
        snapshot->lengths[snapshot->count] = file->len;
        snapshot->data[snapshot->count] = (uint8_t *)malloc(file->len == 0u ? 1u : file->len);
        CHECK(snapshot->data[snapshot->count] != NULL);
        if (snapshot->data[snapshot->count] != NULL && file->len > 0u) {
            memcpy(snapshot->data[snapshot->count], file->data, file->len);
        }
        snapshot->count++;
    }
}

static void file_snapshot_check(const file_snapshot_t *snapshot)
{
    size_t i;

    CHECK(count_files() == (int)snapshot->count);
    for (i = 0u; i < snapshot->count; i++) {
        mem_file_t *file = mem_find_file(snapshot->paths[i]);
        CHECK(file != NULL);
        if (file == NULL) {
            continue;
        }
        CHECK(file->len == snapshot->lengths[i]);
        if (file->len == snapshot->lengths[i]) {
            CHECK(memcmp(file->data, snapshot->data[i], file->len) == 0);
        }
    }
}

static void file_snapshot_free(file_snapshot_t *snapshot)
{
    size_t i;

    for (i = 0u; i < snapshot->count; i++) {
        free(snapshot->data[i]);
        snapshot->data[i] = NULL;
    }
    snapshot->count = 0u;
}

static app_uninstall_purge_result_t fake_external_purge(
    void *ctx,
    const app_install_record_t *record)
{
    cleanup_ctx_t *cleanup = (cleanup_ctx_t *)ctx;
    (void)record;
    cleanup->calls++;
    return cleanup->result;
}

static void store_native_record(app_install_io_t *io,
                                const app_install_record_t *record)
{
    uint8_t encoded[APP_INSTALL_RECORD_BIN_SIZE];

    CHECK(app_install_record_encode(record, encoded, sizeof(encoded)) == AICAM_TRUE);
    CHECK(io->kv_write(io->ctx, APP_INSTALL_KEY_RECORD, encoded, sizeof(encoded)) ==
          APP_INSTALL_IO_OK);
}

static void test_active_and_boot_generation(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t active;
    app_sidecar_descriptor_t recovered;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t len = 0u;
    uint64_t old_generation;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    bundle = load_fixture("positive_small.bin", &len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, len,
                                           &active, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(active.file_count == 3u);
    CHECK(active.version_major == 1u && active.version_minor == 4u &&
          active.version_patch == 2u);
    CHECK(active.host_ui_abi == APP_SIDECAR_HOST_UI_ABI);
    CHECK(memcmp(active.ui_tbs_sha256, SC_VEC_UI_TBS_SMALL, APP_SIDECAR_SHA256_LEN) == 0);
    CHECK(active.service_generation > 0u);
    old_generation = active.service_generation;
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == old_generation);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                                &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation > old_generation);
    free(bundle);
}

static void test_same_bundle_retry_preserves_active_files(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t active;
    app_sidecar_descriptor_t recovered;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    file_snapshot_t snapshot;
    uint8_t *bundle;
    size_t bundle_len = 0u;
    uint64_t active_generation;
    int writes_before;

    memset(&snapshot, 0, sizeof(snapshot));
    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    bundle = load_fixture("positive_small.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &active, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &active, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    active_generation = active.service_generation;
    CHECK(active_generation > 0u);
    file_snapshot_take(&snapshot);
    CHECK(snapshot.count == (size_t)(active.file_count + 2u));
    writes_before = g_io.file_write_calls;

    g_io.partial_next_file_writes = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ALREADY);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == active_generation);
    CHECK(g_io.file_write_calls == writes_before);
    CHECK(g_io.partial_next_file_writes == 1);
    file_snapshot_check(&snapshot);

    g_io.fail_next_file_writes = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ALREADY);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == active_generation);
    CHECK(g_io.file_write_calls == writes_before);
    CHECK(g_io.fail_next_file_writes == 1);
    file_snapshot_check(&snapshot);

    g_io.fail_next_file_reads = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &recovered, &state);
    CHECK(result != APP_SIDECAR_LIFECYCLE_ALREADY);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(g_io.file_write_calls == writes_before);
    file_snapshot_check(&snapshot);
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == active_generation);
    file_snapshot_check(&snapshot);

    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                                &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(strcmp(recovered.publisher_id, active.publisher_id) == 0);
    CHECK(strcmp(recovered.app_id, active.app_id) == 0);
    CHECK(memcmp(recovered.native_tbs_sha256, active.native_tbs_sha256,
                 APP_SIDECAR_SHA256_LEN) == 0);
    CHECK(memcmp(recovered.ui_tbs_sha256, active.ui_tbs_sha256,
                 APP_SIDECAR_SHA256_LEN) == 0);
    file_snapshot_check(&snapshot);
    file_snapshot_free(&snapshot);
    free(bundle);
}

static void test_native_package_damage_blocks_sidecar(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t active;
    app_sidecar_descriptor_t recovered;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    mem_file_t *package;
    uint8_t *bundle;
    size_t bundle_len = 0u;
    uint64_t active_generation;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    bundle = load_fixture("positive_small.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &active, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    active_generation = active.service_generation;
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == active_generation);

    package = mem_find_file(native.package_path);
    CHECK(package != NULL);
    if (package == NULL) {
        free(bundle);
        return;
    }
    package->data[0] ^= 0x80u;
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state == APP_SIDECAR_LIFECYCLE_UNAVAILABLE);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                                &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state == APP_SIDECAR_LIFECYCLE_UNAVAILABLE);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) == NULL);

    package = mem_find_file(native.package_path);
    CHECK(package != NULL);
    if (package != NULL) {
        package->data[0] ^= 0x80u;
    }
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == active_generation + 1u);
    CHECK(memcmp(recovered.ui_tbs_sha256, active.ui_tbs_sha256,
                 APP_SIDECAR_SHA256_LEN) == 0);

    CHECK(io.file_remove(io.ctx, native.package_path) == APP_INSTALL_IO_OK);
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state == APP_SIDECAR_LIFECYCLE_UNAVAILABLE);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                                &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state == APP_SIDECAR_LIFECYCLE_UNAVAILABLE);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) == NULL);

    restore_native_package(&io, &native);
    result = app_sidecar_lifecycle_query(&io, &trust, &native, &recovered, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(recovered.service_generation == active_generation + 2u);
    CHECK(memcmp(recovered.ui_tbs_sha256, active.ui_tbs_sha256,
                 APP_SIDECAR_SHA256_LEN) == 0);
    free(bundle);
}

static void test_app26_golden_host86_interop(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t active;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_verdict_t verdict;
    app_admission_policy_t native_policy;
    app_admission_trust_view_t native_trust;
    app_admission_verdict_t native_verdict;
    app_admission_result_t native_admission;
    uint8_t *bundle;
    uint8_t *host_bundle;
    uint8_t *native_package;
    uint8_t *spki;
    size_t bundle_len = 0u;
    size_t host_bundle_len = 0u;
    size_t native_len = 0u;
    size_t spki_len = 0u;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    bundle = load_local_fixture("golden.neui", &bundle_len);
    host_bundle = load_local_fixture("host_controlled.neui", &host_bundle_len);
    native_package = load_local_fixture("golden.neapp.v3", &native_len);
    spki = load_local_fixture("test-publisher.spki.der", &spki_len);
    CHECK(bundle != NULL && host_bundle != NULL && native_package != NULL && spki != NULL);
    if (bundle == NULL || host_bundle == NULL || native_package == NULL || spki == NULL) {
        free(bundle);
        free(host_bundle);
        free(native_package);
        free(spki);
        return;
    }
    memset(&native_policy, 0, sizeof(native_policy));
    native_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    native_policy.psram_mib = 64u;
    native_policy.abi_version = APP_ADMISSION_ABI_VERSION_V3;
    native_policy.abi_table_size = APP_ADMISSION_V3_ABI_TABLE_SIZE;
    native_policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK;
    native_policy.exec_region_base = 0x93E00000u;
    native_policy.exec_region_size = 2u * 1024u * 1024u;
    native_policy.event_capacity = 8192u;
    native_policy.report_capacity = 8192u;
    native_policy.state_quota_capacity = 8192u;
    native_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V3;
    native_policy.control_capacity = 1024u;
    native_policy.snapshot_capacity = 4096u;
    native_policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_LINE_CROSSING_64;
    native_trust.publisher_id = "test-publisher";
    native_trust.publisher_id_len = strlen(native_trust.publisher_id);
    native_trust.spki_der = spki;
    native_trust.spki_der_len = spki_len;
    native_admission = app_admission_verify_package(native_package, native_len,
                                                     &native_policy, &native_trust,
                                                     &native_verdict);
    CHECK(native_admission == APP_ADMISSION_OK);
    if (native_admission != APP_ADMISSION_OK) {
        free(bundle);
        free(host_bundle);
        free(native_package);
        free(spki);
        return;
    }
    memset(&native, 0, sizeof(native));
    snprintf(native.publisher_id, sizeof(native.publisher_id), "%s",
             native_verdict.publisher_id);
    snprintf(native.app_id, sizeof(native.app_id), "%s", native_verdict.app_id);
    native.version_major = native_verdict.version_major;
    native.version_minor = native_verdict.version_minor;
    native.version_patch = native_verdict.version_patch;
    native.package_len = (uint32_t)native_len;
    mbedtls_sha256(native_package, native_len, native.package_sha256, 0);
    memcpy(native.tbs_sha256, native_verdict.tbs_sha256, sizeof(native.tbs_sha256));
    memcpy(native.image_sha256, native_verdict.image_sha256, sizeof(native.image_sha256));
    memcpy(native.publisher_key_sha256, native_verdict.publisher_key_sha256,
           sizeof(native.publisher_key_sha256));
    set_package_path(&native);
    store_native_record(&io, &native);
    CHECK(io.file_write(io.ctx, native.package_path, native_package, native_len) ==
          APP_INSTALL_IO_OK);
    trust.publisher_id = native.publisher_id;
    trust.publisher_id_len = strlen(native.publisher_id);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.spki_der = spki;
    trust.spki_der_len = spki_len;
    trust.native_tbs_sha256 = native.tbs_sha256;
    CHECK(bytes_match_hex(native.package_sha256, sizeof(native.package_sha256),
                          "c74f7e468a9b63403bbaa8d36816bfd5250ffd68f66ebffc7d994440f1e22342"));
    CHECK(bytes_match_hex(native.publisher_key_sha256, sizeof(native.publisher_key_sha256),
                          "5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3"));
    CHECK(bytes_match_hex(native.tbs_sha256, sizeof(native.tbs_sha256),
                          "d793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e"));
    CHECK(app_sidecar_verify_bundle(bundle, bundle_len, &trust, NULL, NULL, &verdict) ==
          APP_SIDECAR_OK);
    CHECK(bytes_match_hex(verdict.ui_tbs_sha256, sizeof(verdict.ui_tbs_sha256),
                          "aa8130ea20737d457bb6ace49871daa82e85338c7b2e2da656edcc9b7f3fe02f"));
    CHECK(app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                        &active, &state) == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(active.file_count == 4u);
    CHECK(active.service_generation > 0u);
    CHECK(bytes_match_hex(active.ui_tbs_sha256, sizeof(active.ui_tbs_sha256),
                          "aa8130ea20737d457bb6ace49871daa82e85338c7b2e2da656edcc9b7f3fe02f"));
    CHECK(memcmp(active.native_tbs_sha256, native.tbs_sha256,
                 sizeof(native.tbs_sha256)) == 0);
    CHECK(app_sidecar_verify_bundle(host_bundle, host_bundle_len, &trust, NULL, NULL,
                                    &verdict) == APP_SIDECAR_OK);
    CHECK(bytes_match_hex(verdict.ui_tbs_sha256, sizeof(verdict.ui_tbs_sha256),
                          "91c392f993521c11ebfd8ce566237337d8e46bebcffe9f0ede95201517f263ee"));
    CHECK(app_sidecar_lifecycle_install(&io, &trust, &native, host_bundle,
                                        host_bundle_len, &active, &state) ==
          APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(active.version_major == 1u && active.version_minor == 0u &&
          active.version_patch == 1u);
    free(bundle);
    free(host_bundle);
    free(native_package);
    free(spki);
}

static void test_native_identity_mismatch_rejected(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t active;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t len = 0u;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    native.tbs_sha256[0] ^= 0x80u;
    bundle = load_fixture("positive_small.bin", &len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, len,
                                           &active, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(count_files() == 1);
    free(bundle);
}

static void test_confirmed_uninstall_purge_gate_and_reinstall(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_install_record_t removed;
    app_install_record_t status_record;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t sidecar_state;
    app_sidecar_lifecycle_result_t sidecar_result;
    app_state_identity_t identity;
    app_state_session_t session;
    app_uninstall_cleanup_ops_t ops;
    app_uninstall_state_t uninstall_state;
    app_uninstall_result_t uninstall_result;
    uint8_t state_blob[] = {0x44u, 0x21u, 0x90u};
    uint8_t state_out[16];
    uint32_t actual_len = 0u;
    uint32_t revision = 0u;
    uint32_t new_revision = 0u;
    cleanup_ctx_t cleanup;
    uint8_t *bundle;
    size_t bundle_len = 0u;
    app_install_record_t committed;
    aicam_bool_t present = AICAM_FALSE;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    memset(&identity, 0, sizeof(identity));
    memcpy(identity.spki_sha256, native.publisher_key_sha256,
           APP_STATE_SHA256_LEN);
    snprintf(identity.publisher_id, sizeof(identity.publisher_id), "%s",
             native.publisher_id);
    snprintf(identity.app_id, sizeof(identity.app_id), "%s", native.app_id);
    CHECK(app_state_session_bind(&session, &io, &identity, 128u) == APP_STATE_OK);
    CHECK(app_state_commit(&session, state_blob, sizeof(state_blob),
                           APP_STATE_REVISION_NONE, &new_revision) == APP_STATE_OK);
    CHECK(new_revision == 1u);
    app_state_session_end(&session);
    bundle = load_fixture("positive_small.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    CHECK(app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                        &descriptor, &sidecar_state) ==
          APP_SIDECAR_LIFECYCLE_OK);
    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.result = APP_UNINSTALL_PURGE_UNKNOWN;
    ops.ctx = &cleanup;
    ops.purge = fake_external_purge;
    memset(&removed, 0, sizeof(removed));
    uninstall_result = app_install_uninstall_confirmed(
        &io, AICAM_FALSE, &ops, &removed, &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_CANCELLED);
    CHECK(cleanup.calls == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(app_install_uninstall_status(&io, &uninstall_state, NULL) == APP_INSTALL_OK);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_NONE);
    g_io.fail_next_file_removes = 1;
    uninstall_result = app_install_uninstall_confirmed(
        &io, AICAM_TRUE, &ops, &removed, &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_CLEANUP_PENDING);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    CHECK(cleanup.calls == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_UNINSTALL) != NULL);
    CHECK(mem_find_file(native.package_path) != NULL);
    app_install_boot_resolve_reset();
    CHECK(app_install_uninstall_status(&io, &uninstall_state, &status_record) ==
          APP_INSTALL_OK);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    CHECK(memcmp(status_record.tbs_sha256, native.tbs_sha256,
                 APP_INSTALL_SHA256_LEN) == 0);
    CHECK(app_install_boot_resolve(&io) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    uninstall_result = app_install_uninstall_confirmed(
        &io, AICAM_TRUE, &ops, &removed, &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_CLEANUP_PENDING);
    CHECK(cleanup.calls == 1);
    cleanup.result = APP_UNINSTALL_PURGE_CONFIRMED;
    uninstall_result = app_install_uninstall_confirmed(
        &io, AICAM_TRUE, &ops, &removed, &uninstall_state);
    CHECK(uninstall_result == APP_UNINSTALL_OK);
    CHECK(uninstall_state == APP_UNINSTALL_STATE_NONE);
    CHECK(cleanup.calls == 2);
    CHECK(mem_find_key(APP_INSTALL_KEY_UNINSTALL) == NULL);
    CHECK(count_files() == 0);
    store_native_record(&io, &native);
    restore_native_package(&io, &native);
    CHECK(app_state_session_bind(&session, &io, &identity, 128u) == APP_STATE_OK);
    CHECK(app_state_read(&session, state_out, sizeof(state_out), &actual_len,
                         &revision) == APP_STATE_NOT_FOUND);
    app_state_session_end(&session);
    sidecar_result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                                       &descriptor, &sidecar_state);
    CHECK(sidecar_result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(sidecar_state == APP_SIDECAR_LIFECYCLE_UNAVAILABLE);
    CHECK(app_install_query_committed(&io, &committed, &present) == APP_INSTALL_OK);
    CHECK(present == AICAM_TRUE);
    CHECK(app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                        &descriptor, &sidecar_state) ==
          APP_SIDECAR_LIFECYCLE_OK);
    CHECK(sidecar_state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    free(bundle);
}

static void test_limit_admission_and_budget(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t bundle_len = 0u;
    uint64_t candidate_bytes;
    uint64_t required_bytes;
    uint64_t payload_sum = 0u;
    uint32_t max_file_len = 0u;
    uint16_t i;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    bundle = load_limit_fixture(&bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    candidate_bytes = bundle_len + 2097152u;
    required_bytes = candidate_bytes * APP_SIDECAR_LIFECYCLE_FREE_MULTIPLIER +
                     2u * APP_SIDECAR_LIFECYCLE_RECORD_SIZE +
                     APP_SIDECAR_LIFECYCLE_GC_RESERVE;
    g_io.free_bytes_value = required_bytes - 1u;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_FULL);
    CHECK(count_files() == 1);
    g_io.free_bytes_value = required_bytes;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(descriptor.file_count == APP_SIDECAR_FILE_COUNT_MAX);
    CHECK(descriptor.asset_payload_len == 2097152u);
    CHECK(descriptor.manifest_len <= APP_SIDECAR_MANIFEST_LEN_MAX);
    for (i = 0u; i < descriptor.file_count; i++) {
        payload_sum += descriptor.files[i].file_len;
        if (descriptor.files[i].file_len > max_file_len) {
            max_file_len = descriptor.files[i].file_len;
        }
    }
    CHECK(payload_sum == APP_SIDECAR_ASSET_PAYLOAD_MAX);
    CHECK(max_file_len == APP_SIDECAR_FILE_LEN_MAX);
    CHECK(count_files() == APP_SIDECAR_FILE_COUNT_MAX + 2);
    free(bundle);
}

static void test_storage_faults_fail_closed_and_recover(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t bundle_len = 0u;
    mem_kv_t *slot;
    mem_file_t *asset;

    bundle = load_fixture("positive_small.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    g_io.partial_next_file_writes = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result != APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(count_files() == 1);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    g_io.fail_next_file_reads = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result != APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    g_io.fail_next_file_stats = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result != APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state != APP_SIDECAR_LIFECYCLE_ACTIVE);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    g_io.free_bytes_enabled = AICAM_FALSE;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(count_files() == 1);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    g_io.fail_next_kv_writes = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) == NULL);
    CHECK(count_files() == 1);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_STAGING);
    g_io.fail_kv_read_key_skip = 3;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) != NULL);
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                               &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_NONE);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) == NULL);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s",
             APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) != NULL);
    g_io.fail_next_file_removes = 1;
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                               &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                               &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_NONE);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    g_io.fail_next_kv_deletes = 1;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A) != NULL);
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                               &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);

    slot = mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
    CHECK(slot != NULL);
    if (slot != NULL) {
        slot->data[24] ^= 0x01u;
    }
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_query(&io, &trust, &native,
                                        &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(state == APP_SIDECAR_LIFECYCLE_UNKNOWN);

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    CHECK(app_sidecar_lifecycle_install(&io, &trust, &native, bundle, bundle_len,
                                        &descriptor, &state) == APP_SIDECAR_LIFECYCLE_OK);
    asset = mem_find_file(descriptor.files[0].storage_path);
    CHECK(asset != NULL);
    if (asset != NULL && asset->len > 0u) {
        asset->data[0] ^= 0x80u;
    }
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_query(&io, &trust, &native,
                                        &descriptor, &state);
    CHECK(result != APP_SIDECAR_LIFECYCLE_OK ||
          state != APP_SIDECAR_LIFECYCLE_ACTIVE);
    free(bundle);
}

static void test_upgrade_conflict_retry_and_nested_index(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_result_t result;
    app_sidecar_verdict_t verdict;
    uint8_t *small;
    uint8_t *upgrade;
    uint8_t *conflict;
    uint8_t *downgrade;
    uint8_t *nested;
    uint8_t *duplicate;
    size_t small_len = 0u;
    size_t upgrade_len = 0u;
    size_t conflict_len = 0u;
    size_t downgrade_len = 0u;
    size_t nested_len = 0u;
    size_t duplicate_len = 0u;

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    make_trust(&trust, &native);
    small = load_fixture("positive_small.bin", &small_len);
    upgrade = load_local_fixture("positive_upgrade.bin", &upgrade_len);
    conflict = load_local_fixture("positive_version_conflict.bin", &conflict_len);
    downgrade = load_local_fixture("positive_downgrade.bin", &downgrade_len);
    nested = load_local_fixture("positive_nested_index.bin", &nested_len);
    duplicate = load_local_fixture("negative_duplicate_root_index.bin", &duplicate_len);
    CHECK(small != NULL && upgrade != NULL && conflict != NULL && downgrade != NULL &&
          nested != NULL && duplicate != NULL);
    if (small == NULL || upgrade == NULL || conflict == NULL || downgrade == NULL ||
        nested == NULL || duplicate == NULL) {
        free(small);
        free(upgrade);
        free(conflict);
        free(downgrade);
        free(nested);
        free(duplicate);
        return;
    }
    CHECK(app_sidecar_verify_bundle(nested, nested_len, &trust, NULL, NULL,
                                    &verdict) == APP_SIDECAR_OK);
    CHECK(app_sidecar_verify_bundle(duplicate, duplicate_len, &trust, NULL, NULL,
                                    &verdict) == APP_SIDECAR_ERR_BAD_FORMAT);
    CHECK(app_sidecar_lifecycle_install(&io, &trust, &native, small, small_len,
                                        &descriptor, &state) == APP_SIDECAR_LIFECYCLE_OK);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, small, small_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ALREADY);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, conflict, conflict_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_VERSION_CONFLICT);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, downgrade, downgrade_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_DOWNGRADE);
    g_io.fail_next_file_writes = 1;
    g_io.fail_next_file_writes_skip = 1;
    g_io.fail_next_file_removes = 1;
    g_io.fail_next_file_removes_skip = 3;
    result = app_sidecar_lifecycle_install(&io, &trust, &native, upgrade, upgrade_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN);
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_STAGING) != NULL);
    app_install_boot_resolve_reset();
    result = app_sidecar_lifecycle_boot_resolve(&io, &trust, &native,
                                               &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(descriptor.version_major == 1u && descriptor.version_minor == 4u &&
          descriptor.version_patch == 2u);
    result = app_sidecar_lifecycle_install(&io, &trust, &native, upgrade, upgrade_len,
                                           &descriptor, &state);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
    CHECK(descriptor.version_major == 1u && descriptor.version_minor == 5u &&
          descriptor.version_patch == 0u);
    free(small);
    free(upgrade);
    free(conflict);
    free(downgrade);
    free(nested);
    free(duplicate);
}

static void test_uninstall_nvs_delete_fault(void)
{
    app_install_io_t io;
    app_install_record_t native;
    app_install_record_t removed;
    app_uninstall_cleanup_ops_t ops;
    app_uninstall_state_t state;
    app_uninstall_result_t result;
    cleanup_ctx_t cleanup;
    uint8_t package[] = {0x19u, 0x72u};

    mem_reset();
    mem_io_init(&io);
    app_install_boot_resolve_reset();
    make_native_record(&io, &native);
    CHECK(io.file_write(io.ctx, native.package_path, package, sizeof(package)) ==
          APP_INSTALL_IO_OK);
    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.result = APP_UNINSTALL_PURGE_CONFIRMED;
    ops.ctx = &cleanup;
    ops.purge = fake_external_purge;
    g_io.fail_next_kv_deletes = 1;
    result = app_install_uninstall_confirmed(&io, AICAM_TRUE, &ops,
                                             &removed, &state);
    CHECK(result == APP_UNINSTALL_CLEANUP_PENDING);
    CHECK(state == APP_UNINSTALL_STATE_CLEANUP_PENDING);
    CHECK(cleanup.calls == 0);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) != NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_UNINSTALL) != NULL);
    app_install_boot_resolve_reset();
    CHECK(app_install_boot_resolve(&io) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    result = app_install_uninstall_confirmed(&io, AICAM_TRUE, &ops,
                                             &removed, &state);
    CHECK(result == APP_UNINSTALL_OK);
    CHECK(state == APP_UNINSTALL_STATE_NONE);
    CHECK(cleanup.calls == 1);
    CHECK(mem_find_key(APP_INSTALL_KEY_RECORD) == NULL);
    CHECK(mem_find_key(APP_INSTALL_KEY_UNINSTALL) == NULL);
    CHECK(count_files() == 0);
}

int main(void)
{
    test_pair_history_policy();
    test_nepk_ci_candidate();
    test_active_and_boot_generation();
    test_same_bundle_retry_preserves_active_files();
    test_native_package_damage_blocks_sidecar();
    test_app26_golden_host86_interop();
    test_native_identity_mismatch_rejected();
    test_confirmed_uninstall_purge_gate_and_reinstall();
    test_limit_admission_and_budget();
    test_storage_faults_fail_closed_and_recover();
    test_upgrade_conflict_retry_and_nested_index();
    test_uninstall_nvs_delete_fault();
    printf("test_app_sidecar_lifecycle: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
