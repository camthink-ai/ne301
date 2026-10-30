#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "app_installer.h"
#include "app_trust_anchor.h"
#include "app_sidecar_lifecycle.h"
#include "app_sidecar_verify.h"
#include "app_pair_history.h"
#include "web_assets.h"
#include "sidecar_vectors.h"
#include "mbedtls/sha256.h"

#define SC_NATIVE_PACKAGE_LEN 4096u

static int g_total;
static int g_fail;

static void check_impl(int cond, int line)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("FAIL %d: line=%d\n", g_total, line);
    }
}

#define CHECK(cond) check_impl((cond), __LINE__)

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../app_sidecar_lifecycle/mem_sidecar_backend.h"

static const app_trust_anchor_t g_anchor = {
    APP_TRUST_ANCHOR_FORMAT_VERSION,
    SC_VEC_PUBLISHER_ID,
    SC_VEC_PUBLISHER_ID_LEN,
    SC_VEC_SPKI_DER,
    SC_VEC_SPKI_LEN,
    NULL
};

static int g_anchor_missing;

const app_trust_anchor_t *app_trust_anchor_get(void)
{
    return &g_anchor;
}

app_trust_anchor_status_t app_trust_anchor_status(void)
{
    return (g_anchor_missing != 0) ? APP_TRUST_ANCHOR_MISSING : APP_TRUST_ANCHOR_PROVISIONED;
}

const char *app_trust_anchor_status_str(app_trust_anchor_status_t status)
{
    (void)status;
    return "anchor";
}

static app_install_io_t g_test_io;

const app_install_io_t *app_installer_device_io(void)
{
    mem_io_init(&g_test_io);
    return &g_test_io;
}

static int fetch_storage_path(const char *logical, char *out, size_t cap)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
    uint16_t i;
    aicam_bool_t present = AICAM_FALSE;

    if (app_install_query_committed(io, &native, &present) != APP_INSTALL_OK ||
        present != AICAM_TRUE) {
        return 0;
    }
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = native.publisher_id;
    trust.publisher_id_len = strlen(native.publisher_id);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.native_tbs_sha256 = native.tbs_sha256;
    memset(&descriptor, 0, sizeof(descriptor));
    if (app_sidecar_lifecycle_query(io, &trust, &native, &descriptor, &state) !=
        APP_SIDECAR_LIFECYCLE_OK) {
        return 0;
    }
    for (i = 0u; i < descriptor.file_count; i++) {
        if (strcmp(descriptor.files[i].path, logical) == 0) {
            snprintf(out, cap, "%s", descriptor.files[i].storage_path);
            return 1;
        }
    }
    return 0;
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

static void make_native_record(app_install_record_t *record)
{
    uint8_t encoded[APP_INSTALL_RECORD_BIN_SIZE];
    uint8_t package[SC_NATIVE_PACKAGE_LEN];
    size_t i;
    const app_install_io_t *io = app_installer_device_io();

    memset(record, 0, sizeof(*record));
    snprintf(record->publisher_id, sizeof(record->publisher_id), "%s", APP_ADMISSION_ADMIN_PUBLISHER_ID);
    snprintf(record->app_id, sizeof(record->app_id), "%s", SC_VEC_APP_ID);
    record->version_major = 1u;
    record->version_minor = 0u;
    record->version_patch = 0u;
    for (i = 0u; i < sizeof(package); i++) {
        package[i] = (uint8_t)(0x41u + (i % 0x1Bu));
    }
    record->package_len = (uint32_t)sizeof(package);
    mbedtls_sha256(package, sizeof(package), record->package_sha256, 0);
    memcpy(record->tbs_sha256, SC_VEC_NATIVE_TBS, sizeof(record->tbs_sha256));
    memset(record->publisher_key_sha256, 0, sizeof(record->publisher_key_sha256));
    memset(record->image_sha256, 0x27, sizeof(record->image_sha256));
    set_package_path(record);
    CHECK(app_install_record_encode(record, encoded, sizeof(encoded)) == AICAM_TRUE);
    CHECK(io->kv_write(io->ctx, APP_INSTALL_KEY_RECORD, encoded, sizeof(encoded)) ==
          APP_INSTALL_IO_OK);
    CHECK(io->file_write(io->ctx, record->package_path, package, sizeof(package)) ==
          APP_INSTALL_IO_OK);
}

static uint8_t *load_bundle(const char *name, size_t *len)
{
    char path[256];
    FILE *stream;
    uint8_t *data;
    long size;

    (void)snprintf(path, sizeof(path), "build/%s", name);
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return NULL;
    }
    fseek(stream, 0, SEEK_END);
    size = ftell(stream);
    fseek(stream, 0, SEEK_SET);
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(stream);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, stream) != (size_t)size) {
        free(data);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *len = (size_t)size;
    return data;
}

static void install_active_sidecar(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t bundle_len;

    make_native_record(&native);
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = native.publisher_id;
    trust.publisher_id_len = strlen(native.publisher_id);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.native_tbs_sha256 = native.tbs_sha256;
    bundle = load_bundle("serving_small.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = app_sidecar_lifecycle_install_pair(io, &trust, &native, bundle,
                                                bundle_len, NULL,
                                                &descriptor, &state);
    free(bundle);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK);
    CHECK(state == APP_SIDECAR_LIFECYCLE_ACTIVE);
}

static void reset_all(void)
{
    mem_reset();
    app_install_boot_resolve_reset();
    app_sidecar_lifecycle_reset();
    web_sidecar_projection_reset();
    g_anchor_missing = 0;
}

static void test_active_projection(void)
{
    reset_all();
    install_active_sidecar();
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    CHECK(web_sidecar_projection_valid() == AICAM_TRUE);
    CHECK(web_sidecar_projection_generation() > 0u);
    CHECK(web_sidecar_projection_file_count() == 3u);
}

static void test_exact_path_match(void)
{
    uint32_t index = 0u;

    CHECK(web_sidecar_asset_resolve("index.html", &index) == AICAM_TRUE);
    CHECK(web_sidecar_asset_resolve("assets/app.min.js", &index) == AICAM_TRUE);
    CHECK(web_sidecar_asset_resolve("libs/lib-a.min.js", &index) == AICAM_TRUE);
    CHECK(web_sidecar_asset_resolve("missing.js", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/app.min.js.extra", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/app.min", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("INDEX.HTML", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/app.min.js/", &index) == AICAM_FALSE);
}

static void test_path_escapes(void)
{
    uint32_t index = 0u;

    CHECK(web_sidecar_asset_resolve("../index.html", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/../index.html", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/./app.min.js", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("/index.html", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets//app.min.js", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets\\app.min.js", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/%2e%2e/app.min.js", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/app.min.js:secret", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("", &index) == AICAM_FALSE);
    CHECK(web_sidecar_asset_resolve("assets/app min.js", &index) == AICAM_FALSE);
}

static void test_route_parse(void)
{
    uint64_t generation = 0u;
    char path[APP_SIDECAR_PATH_MAX + 2u];
    char route[256];
    uint32_t index = 0u;

    CHECK(web_sidecar_route_parse("/appsite/g42/index.html", &generation, path, sizeof(path)) ==
          AICAM_TRUE);
    CHECK(generation == 42u);
    CHECK(strcmp(path, "index.html") == 0);
    (void)snprintf(route, sizeof(route), "/appsite/g%llu/assets/app.min.js",
                   (unsigned long long)web_sidecar_projection_generation());
    CHECK(web_sidecar_route_parse(route, &generation, path, sizeof(path)) == AICAM_TRUE);
    CHECK(generation == web_sidecar_projection_generation());
    CHECK(strcmp(path, "assets/app.min.js") == 0);
    CHECK(web_sidecar_route_parse("/appsite/g0/index.html", &generation, path, sizeof(path)) ==
          AICAM_FALSE);
    CHECK(web_sidecar_route_parse("/appsite/g/index.html", &generation, path, sizeof(path)) ==
          AICAM_FALSE);
    CHECK(web_sidecar_route_parse("/appsite/g42/", &generation, path, sizeof(path)) ==
          AICAM_FALSE);
    CHECK(web_sidecar_route_parse("/appsite/g42", &generation, path, sizeof(path)) ==
          AICAM_FALSE);
    CHECK(web_sidecar_route_parse("/appsite/x42/index.html", &generation, path, sizeof(path)) ==
          AICAM_FALSE);
    CHECK(web_sidecar_route_parse("/appsite/g42/%2e%2e/index.html", &generation, path,
                                  sizeof(path)) == AICAM_TRUE);
    CHECK(web_sidecar_asset_resolve(path, &index) == AICAM_FALSE);
    CHECK(web_sidecar_route_parse("/other/g42/index.html", &generation, path, sizeof(path)) ==
          AICAM_FALSE);
}

static void test_load_verifies_content(void)
{
    uint32_t index = 0u;
    uint8_t buf[512];
    size_t len = 0u;
    mem_file_t *file;

    CHECK(web_sidecar_asset_resolve("index.html", &index) == AICAM_TRUE);
    CHECK(web_sidecar_asset_load(index, buf, sizeof(buf), &len) == AICAM_TRUE);
    CHECK(len == 128u);
    char stored[128];
    CHECK(fetch_storage_path("index.html", stored, sizeof(stored)) == 1);
    file = mem_find_file(stored);
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(len == file->len);
        CHECK(memcmp(buf, file->data, len) == 0);
        file->data[0] ^= 0x01u;
        CHECK(web_sidecar_asset_load(index, buf, sizeof(buf), &len) == AICAM_FALSE);
        file->data[0] ^= 0x01u;
        CHECK(web_sidecar_asset_load(index, buf, sizeof(buf), &len) == AICAM_TRUE);
    }
}

static void test_missing_storage_file_rejected(void)
{
    uint32_t index = 0u;
    uint8_t buf[512];
    size_t len = 0u;
    char stored[128];

    CHECK(web_sidecar_asset_resolve("libs/lib-a.min.js", &index) == AICAM_TRUE);
    CHECK(fetch_storage_path("libs/lib-a.min.js", stored, sizeof(stored)) == 1);
    CHECK(g_test_io.file_remove(g_test_io.ctx, stored) == APP_INSTALL_IO_OK);
    CHECK(web_sidecar_asset_load(index, buf, sizeof(buf), &len) == AICAM_FALSE);
}

static void test_refresh_rejects_without_native(void)
{
    reset_all();
    CHECK(web_sidecar_projection_refresh() == AICAM_FALSE);
    CHECK(web_sidecar_projection_valid() == AICAM_FALSE);
}

static void test_refresh_rejects_stale_generation_after_reinstall(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t native;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t descriptor;
    app_sidecar_lifecycle_state_t state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
    app_sidecar_lifecycle_result_t result;
    uint8_t *bundle;
    size_t bundle_len;
    uint64_t first_generation;

    reset_all();
    install_active_sidecar();
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    first_generation = web_sidecar_projection_generation();
    CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A) != NULL ||
          mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B) != NULL);
    make_native_record(&native);
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = native.publisher_id;
    trust.publisher_id_len = strlen(native.publisher_id);
    trust.app_id = native.app_id;
    trust.app_id_len = strlen(native.app_id);
    trust.native_tbs_sha256 = native.tbs_sha256;
    bundle = load_bundle("serving_upgrade.bin", &bundle_len);
    CHECK(bundle != NULL);
    if (bundle == NULL) {
        return;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = app_sidecar_lifecycle_install_pair(io, &trust, &native, bundle,
                                                bundle_len, NULL,
                                                &descriptor, &state);
    free(bundle);
    CHECK(result == APP_SIDECAR_LIFECYCLE_OK || result == APP_SIDECAR_LIFECYCLE_ALREADY);
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    CHECK(web_sidecar_projection_generation() != first_generation);
}

static void test_refresh_rejects_after_sidecar_removal(void)
{
    const app_install_io_t *io = app_installer_device_io();

    reset_all();
    install_active_sidecar();
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    {
        app_install_io_result_t ra = io->kv_delete(io->ctx, APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A);
        app_install_io_result_t rb = io->kv_delete(io->ctx, APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B);
        CHECK(ra == APP_INSTALL_IO_OK || rb == APP_INSTALL_IO_OK);
        CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A) == NULL);
        CHECK(mem_find_key(APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B) == NULL);
    }
    CHECK(web_sidecar_projection_refresh() == AICAM_FALSE);
    CHECK(web_sidecar_projection_valid() == AICAM_FALSE);
    {
        uint32_t index = 0u;
        CHECK(web_sidecar_asset_resolve("index.html", &index) == AICAM_FALSE);
    }
}

static void test_unsigned_admin_does_not_depend_on_trust_anchor(void)
{
    reset_all();
    install_active_sidecar();
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    g_anchor_missing = 1;
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
    CHECK(web_sidecar_projection_valid() == AICAM_TRUE);
    g_anchor_missing = 0;
    CHECK(web_sidecar_projection_refresh() == AICAM_TRUE);
}

int main(void)
{
    test_active_projection();
    test_exact_path_match();
    test_path_escapes();
    test_route_parse();
    test_load_verifies_content();
    test_missing_storage_file_rejected();
    test_refresh_rejects_without_native();
    test_refresh_rejects_stale_generation_after_reinstall();
    test_refresh_rejects_after_sidecar_removal();
    test_unsigned_admin_does_not_depend_on_trust_anchor();
    printf("web_sidecar_assets: total=%d fail=%d\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
