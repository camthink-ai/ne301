#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_installer.h"
#include "app_sidecar_lifecycle.h"
#include "app_native_vnext_floor.h"

#include "generic_math.h"
#include "mbedtls/sha256.h"

#define APP_INSTALL_RECORD_MAGIC 0x31524941u
#define APP_INSTALL_RECORD_VERSION 1u
#define APP_INSTALL_PENDING_MAGIC 0x31504941u
#define APP_INSTALL_REC_HDR_SIZE 8u
#define APP_INSTALL_REC_PUB_OFF 8u
#define APP_INSTALL_REC_APP_OFF 25u
#define APP_INSTALL_REC_VER_OFF 58u
#define APP_INSTALL_REC_LEN_OFF 64u
#define APP_INSTALL_REC_SHAS_OFF 68u
#define APP_INSTALL_REC_PATH_OFF 196u
#define APP_INSTALL_REC_PATH_SLOT APP_INSTALL_PATH_SLOT
#define APP_INSTALL_REC_CRC_OFF (APP_INSTALL_REC_PATH_OFF + APP_INSTALL_REC_PATH_SLOT)
#define APP_INSTALL_RECORD_BLOB_SIZE (APP_INSTALL_REC_CRC_OFF + 4u)
#define APP_INSTALL_PEND_ACT_OFF 4u
#define APP_INSTALL_PEND_HAS_PREV_OFF 8u
#define APP_INSTALL_PEND_PREV_OFF 12u
#define APP_INSTALL_PEND_CRC_OFF (APP_INSTALL_PEND_PREV_OFF + APP_INSTALL_RECORD_BLOB_SIZE)
#define APP_INSTALL_PENDING_BLOB_SIZE (APP_INSTALL_PEND_CRC_OFF + 4u)
#define APP_INSTALL_ACTION_INSTALL 0x49u
#define APP_INSTALL_ACTION_UNINSTALL 0x55u

#if APP_INSTALL_RECORD_BIN_SIZE != APP_INSTALL_RECORD_BLOB_SIZE
#error "record blob size mismatch"
#endif
#if APP_INSTALL_PENDING_BIN_SIZE != APP_INSTALL_PENDING_BLOB_SIZE
#error "pending blob size mismatch"
#endif

static aicam_bool_t g_boot_resolved;
static aicam_bool_t g_state_unknown;
static aicam_bool_t g_app_running;
static aicam_bool_t g_enable_guard_cache_valid;
static aicam_bool_t g_enable_guard_cache_active;
static app_install_record_t g_enable_guard_cache_record;

static app_install_result_t inst_sidecar_revoke_for_native(
    const app_install_io_t *io,
    const app_install_record_t *native);
static app_install_result_t inst_uninstall_intent_blocks_boot(
    const app_install_io_t *io);

static void inst_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t inst_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void inst_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t inst_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void inst_copy_padded(uint8_t *dst, const char *src, size_t slot)
{
    size_t len = strlen(src);
    memset(dst, 0, slot);
    if (len > slot) {
        len = slot;
    }
    memcpy(dst, src, len);
}

static void inst_sha256(const uint8_t *data, size_t len, uint8_t out[APP_INSTALL_SHA256_LEN])
{
    mbedtls_sha256(data, len, out, 0);
}

static void inst_pkg_path(const uint8_t sha[APP_INSTALL_SHA256_LEN],
                          char *path, size_t cap)
{
    size_t i;
    size_t pos = 0;
    int n = snprintf(path, cap, "%s/", APP_INSTALL_PKG_DIR);
    pos = (size_t)n;
    for (i = 0; i < APP_INSTALL_SHA256_LEN; i++) {
        snprintf(path + pos, cap - pos, "%02x", sha[i]);
        pos += 2u;
    }
    snprintf(path + pos, cap - pos, "%s", ".neapp");
}

static aicam_bool_t inst_record_path_consistent(const app_install_record_t *record)
{
    char expected[APP_INSTALL_PATH_MAX];

    inst_pkg_path(record->package_sha256, expected, sizeof(expected));
    return (strcmp(record->package_path, expected) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_bool_t app_install_record_encode(const app_install_record_t *record,
                                       uint8_t *out, size_t cap)
{
    size_t pub_len;
    size_t app_len;
    size_t path_len;

    if (record == NULL || out == NULL || cap < APP_INSTALL_RECORD_BIN_SIZE) {
        return AICAM_FALSE;
    }
    pub_len = strlen(record->publisher_id);
    app_len = strlen(record->app_id);
    path_len = strlen(record->package_path);
    if (pub_len == 0u || pub_len > APP_INSTALL_PUBLISHER_SLOT ||
        app_len == 0u || app_len > APP_INSTALL_APP_SLOT ||
        path_len == 0u || path_len >= APP_INSTALL_PATH_MAX) {
        return AICAM_FALSE;
    }
    if (inst_record_path_consistent(record) != AICAM_TRUE) {
        return AICAM_FALSE;
    }

    memset(out, 0, APP_INSTALL_RECORD_BIN_SIZE);
    inst_put_u32(out, APP_INSTALL_RECORD_MAGIC);
    inst_put_u32(out + 4, APP_INSTALL_RECORD_VERSION);
    inst_copy_padded(out + APP_INSTALL_REC_PUB_OFF, record->publisher_id,
                     APP_INSTALL_PUBLISHER_SLOT);
    inst_copy_padded(out + APP_INSTALL_REC_APP_OFF, record->app_id,
                     APP_INSTALL_APP_SLOT);
    inst_put_u16(out + APP_INSTALL_REC_VER_OFF, record->version_major);
    inst_put_u16(out + APP_INSTALL_REC_VER_OFF + 2u, record->version_minor);
    inst_put_u16(out + APP_INSTALL_REC_VER_OFF + 4u, record->version_patch);
    inst_put_u32(out + APP_INSTALL_REC_LEN_OFF, record->package_len);
    memcpy(out + APP_INSTALL_REC_SHAS_OFF, record->package_sha256, APP_INSTALL_SHA256_LEN);
    memcpy(out + APP_INSTALL_REC_SHAS_OFF + APP_INSTALL_SHA256_LEN, record->tbs_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(out + APP_INSTALL_REC_SHAS_OFF + 2u * APP_INSTALL_SHA256_LEN, record->image_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(out + APP_INSTALL_REC_SHAS_OFF + 3u * APP_INSTALL_SHA256_LEN,
           record->publisher_key_sha256, APP_INSTALL_SHA256_LEN);
    inst_copy_padded(out + APP_INSTALL_REC_PATH_OFF, record->package_path,
                     APP_INSTALL_REC_PATH_SLOT);
    inst_put_u32(out + APP_INSTALL_REC_CRC_OFF,
                 generic_crc32(out, APP_INSTALL_REC_CRC_OFF));
    return AICAM_TRUE;
}

static aicam_bool_t inst_padded_identifier_valid(const uint8_t *field, size_t slot)
{
    size_t text_len = 0;
    size_t i;

    while (text_len < slot && field[text_len] != 0x00u) {
        text_len++;
    }
    for (i = text_len; i < slot; i++) {
        if (field[i] != 0x00u) {
            return AICAM_FALSE;
        }
    }
    if (text_len == 0u) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

aicam_bool_t app_install_record_decode(const uint8_t *data, size_t len,
                                       app_install_record_t *out)
{
    if (data == NULL || out == NULL || len != APP_INSTALL_RECORD_BIN_SIZE) {
        return AICAM_FALSE;
    }
    if (inst_get_u32(data) != APP_INSTALL_RECORD_MAGIC ||
        inst_get_u32(data + 4) != APP_INSTALL_RECORD_VERSION) {
        return AICAM_FALSE;
    }
    if (inst_get_u32(data + APP_INSTALL_REC_CRC_OFF) !=
        generic_crc32(data, APP_INSTALL_REC_CRC_OFF)) {
        return AICAM_FALSE;
    }
    if (!inst_padded_identifier_valid(data + APP_INSTALL_REC_PUB_OFF,
                                      APP_INSTALL_PUBLISHER_SLOT) ||
        !inst_padded_identifier_valid(data + APP_INSTALL_REC_APP_OFF,
                                      APP_INSTALL_APP_SLOT)) {
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->publisher_id, data + APP_INSTALL_REC_PUB_OFF, APP_INSTALL_PUBLISHER_SLOT);
    out->publisher_id[APP_INSTALL_PUBLISHER_SLOT] = '\0';
    memcpy(out->app_id, data + APP_INSTALL_REC_APP_OFF, APP_INSTALL_APP_SLOT);
    out->app_id[APP_INSTALL_APP_SLOT] = '\0';
    out->version_major = inst_get_u16(data + APP_INSTALL_REC_VER_OFF);
    out->version_minor = inst_get_u16(data + APP_INSTALL_REC_VER_OFF + 2u);
    out->version_patch = inst_get_u16(data + APP_INSTALL_REC_VER_OFF + 4u);
    out->package_len = inst_get_u32(data + APP_INSTALL_REC_LEN_OFF);
    memcpy(out->package_sha256, data + APP_INSTALL_REC_SHAS_OFF, APP_INSTALL_SHA256_LEN);
    memcpy(out->tbs_sha256, data + APP_INSTALL_REC_SHAS_OFF + APP_INSTALL_SHA256_LEN,
           APP_INSTALL_SHA256_LEN);
    memcpy(out->image_sha256, data + APP_INSTALL_REC_SHAS_OFF + 2u * APP_INSTALL_SHA256_LEN,
           APP_INSTALL_SHA256_LEN);
    memcpy(out->publisher_key_sha256,
           data + APP_INSTALL_REC_SHAS_OFF + 3u * APP_INSTALL_SHA256_LEN,
           APP_INSTALL_SHA256_LEN);
    memcpy(out->package_path, data + APP_INSTALL_REC_PATH_OFF, APP_INSTALL_REC_PATH_SLOT);
    out->package_path[APP_INSTALL_REC_PATH_SLOT - 1u] = '\0';
    if (out->package_len == 0u) {
        return AICAM_FALSE;
    }
    if (inst_record_path_consistent(out) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void inst_pending_encode(uint8_t *blob, uint8_t action,
                                aicam_bool_t has_prev, const app_install_record_t *prev)
{
    memset(blob, 0, APP_INSTALL_PENDING_BLOB_SIZE);
    inst_put_u32(blob, APP_INSTALL_PENDING_MAGIC);
    blob[APP_INSTALL_PEND_ACT_OFF] = action;
    inst_put_u32(blob + APP_INSTALL_PEND_HAS_PREV_OFF,
                 (has_prev == AICAM_TRUE) ? 1u : 0u);
    if (has_prev == AICAM_TRUE && prev != NULL) {
        (void)app_install_record_encode(prev, blob + APP_INSTALL_PEND_PREV_OFF,
                                        APP_INSTALL_RECORD_BIN_SIZE);
    }
    inst_put_u32(blob + APP_INSTALL_PEND_CRC_OFF,
                 generic_crc32(blob, APP_INSTALL_PEND_CRC_OFF));
}

static aicam_bool_t inst_pending_decode(const uint8_t *blob, size_t len,
                                        uint8_t *action, aicam_bool_t *has_prev,
                                        app_install_record_t *prev)
{
    if (blob == NULL || len != APP_INSTALL_PENDING_BLOB_SIZE) {
        return AICAM_FALSE;
    }
    if (inst_get_u32(blob) != APP_INSTALL_PENDING_MAGIC) {
        return AICAM_FALSE;
    }
    if (inst_get_u32(blob + APP_INSTALL_PEND_CRC_OFF) !=
        generic_crc32(blob, APP_INSTALL_PEND_CRC_OFF)) {
        return AICAM_FALSE;
    }
    if (blob[APP_INSTALL_PEND_ACT_OFF] != APP_INSTALL_ACTION_INSTALL &&
        blob[APP_INSTALL_PEND_ACT_OFF] != APP_INSTALL_ACTION_UNINSTALL) {
        return AICAM_FALSE;
    }
    *action = blob[APP_INSTALL_PEND_ACT_OFF];
    *has_prev = (inst_get_u32(blob + APP_INSTALL_PEND_HAS_PREV_OFF) != 0u) ?
                AICAM_TRUE : AICAM_FALSE;
    if (*has_prev == AICAM_TRUE) {
        if (app_install_record_decode(blob + APP_INSTALL_PEND_PREV_OFF,
                                      APP_INSTALL_RECORD_BIN_SIZE, prev) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static app_install_io_result_t inst_kv_read_blob(const app_install_io_t *io,
                                                 const char *key, uint8_t *blob,
                                                 size_t blob_size)
{
    size_t len = 0;
    app_install_io_result_t rc = io->kv_read(io->ctx, key, blob, blob_size, &len);
    if (rc != APP_INSTALL_IO_OK) {
        return rc;
    }
    if (len != blob_size) {
        return APP_INSTALL_IO_ERROR;
    }
    return APP_INSTALL_IO_OK;
}

static aicam_bool_t inst_kv_write_proven(const app_install_io_t *io, const char *key,
                                         const uint8_t *blob, size_t blob_size)
{
    uint8_t back[APP_INSTALL_PENDING_BLOB_SIZE];
    app_install_io_result_t rc;

    if (io->kv_write(io->ctx, key, blob, blob_size) != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    rc = inst_kv_read_blob(io, key, back, blob_size);
    if (rc != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    return memcmp(back, blob, blob_size) == 0 ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t inst_kv_absent_proven(const app_install_io_t *io, const char *key,
                                          size_t blob_size)
{
    uint8_t probe[APP_INSTALL_PENDING_BLOB_SIZE];
    size_t len = 0;
    return (io->kv_read(io->ctx, key, probe, blob_size, &len) ==
            APP_INSTALL_IO_NOT_FOUND) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t inst_kv_delete_proven(const app_install_io_t *io, const char *key,
                                          size_t blob_size)
{
    uint8_t probe[APP_INSTALL_PENDING_BLOB_SIZE];
    size_t len = 0;
    app_install_io_result_t rc = io->kv_read(io->ctx, key, probe, blob_size, &len);

    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_TRUE;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    rc = io->kv_delete(io->ctx, key);
    if (rc != APP_INSTALL_IO_OK && rc != APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_FALSE;
    }
    return inst_kv_absent_proven(io, key, blob_size);
}

static aicam_bool_t inst_record_write_proven(const app_install_io_t *io,
                                             const app_install_record_t *record)
{
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];
    if (app_install_record_encode(record, blob, sizeof(blob)) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return inst_kv_write_proven(io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob));
}

static aicam_bool_t inst_record_delete_proven(const app_install_io_t *io)
{
    return inst_kv_delete_proven(io, APP_INSTALL_KEY_RECORD, APP_INSTALL_RECORD_BLOB_SIZE);
}

static aicam_bool_t inst_tx_begin_read(const app_install_io_t *io)
{
    if (io->tx_begin_read != NULL) {
        return io->tx_begin_read(io->ctx);
    }
    return io->tx_begin(io->ctx);
}

static aicam_result_t inst_record_load(const app_install_io_t *io,
                                       app_install_record_t *record,
                                       aicam_bool_t *present)
{
    uint8_t blob[APP_INSTALL_RECORD_BIN_SIZE];
    app_install_io_result_t rc;

    *present = AICAM_FALSE;
    rc = inst_kv_read_blob(io, APP_INSTALL_KEY_RECORD, blob, sizeof(blob));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_OK;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return AICAM_ERROR;
    }
    if (app_install_record_decode(blob, sizeof(blob), record) != AICAM_TRUE) {
        return AICAM_ERROR;
    }
    *present = AICAM_TRUE;
    return AICAM_OK;
}

static int inst_version_cmp(const app_install_record_t *rec,
                            const app_admission_verdict_t *verdict)
{
    if (rec->version_major != verdict->version_major) {
        return (rec->version_major < verdict->version_major) ? -1 : 1;
    }
    if (rec->version_minor != verdict->version_minor) {
        return (rec->version_minor < verdict->version_minor) ? -1 : 1;
    }
    if (rec->version_patch != verdict->version_patch) {
        return (rec->version_patch < verdict->version_patch) ? -1 : 1;
    }
    return 0;
}

static app_install_result_t inst_admission_map(app_admission_result_t rc)
{
    switch (rc) {
    case APP_ADMISSION_OK:
        return APP_INSTALL_OK;
    case APP_ADMISSION_ERR_BAD_PACKAGE:
        return APP_INSTALL_ERR_BAD_PACKAGE;
    case APP_ADMISSION_ERR_SIGNATURE_INVALID:
        return APP_INSTALL_ERR_SIGNATURE_INVALID;
    case APP_ADMISSION_ERR_PUBLISHER_UNTRUSTED:
        return APP_INSTALL_ERR_PUBLISHER_UNTRUSTED;
    case APP_ADMISSION_ERR_TARGET_INCOMPATIBLE:
        return APP_INSTALL_ERR_TARGET_INCOMPATIBLE;
    case APP_ADMISSION_ERR_ABI_INCOMPATIBLE:
        return APP_INSTALL_ERR_ABI_INCOMPATIBLE;
    case APP_ADMISSION_ERR_RESOURCE_LIMIT:
        return APP_INSTALL_ERR_RESOURCE_LIMIT;
    case APP_ADMISSION_ERR_TRUST_UNPROVEN:
        return APP_INSTALL_ERR_TRUST_UNPROVEN;
    default:
        return APP_INSTALL_ERR_BAD_PACKAGE;
    }
}

static app_install_io_result_t inst_file_read_verify(const app_install_io_t *io,
                                                     const char *path,
                                                     const uint8_t expected_sha[APP_INSTALL_SHA256_LEN],
                                                     size_t len,
                                                     aicam_bool_t *match)
{
    uint8_t *buf;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    size_t read_len = 0;
    app_install_io_result_t rc;

    *match = AICAM_FALSE;
    buf = (uint8_t *)malloc(len);
    if (buf == NULL) {
        return APP_INSTALL_IO_ERROR;
    }
    rc = io->file_read(io->ctx, path, buf, len, &read_len);
    if (rc != APP_INSTALL_IO_OK) {
        free(buf);
        return rc;
    }
    if (read_len != len) {
        free(buf);
        return APP_INSTALL_IO_OK;
    }
    inst_sha256(buf, read_len, sha);
    *match = (memcmp(sha, expected_sha, APP_INSTALL_SHA256_LEN) == 0) ? AICAM_TRUE : AICAM_FALSE;
    free(buf);
    return APP_INSTALL_IO_OK;
}

static app_install_result_t inst_committed_enable_guard(
    const app_install_io_t *io,
    app_install_verify_fn verify,
    void *verify_ctx,
    const app_install_record_t *record,
    const app_admission_verdict_t *candidate,
    aicam_bool_t present,
    uint32_t candidate_major)
{
    app_admission_verdict_t verdict;
    app_install_io_result_t rc;
    uint8_t *package;
    size_t got = 0u;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    app_admission_result_t arc;
    aicam_bool_t candidate_matches_record;

    if (present != AICAM_TRUE || candidate_major >= APP_NATIVE_VNEXT_MAJOR ||
        candidate == NULL ||
        strcmp(record->publisher_id, candidate->publisher_id) != 0 ||
        strcmp(record->app_id, candidate->app_id) != 0) {
        return APP_INSTALL_OK;
    }
    if (g_enable_guard_cache_valid == AICAM_TRUE &&
        strcmp(g_enable_guard_cache_record.publisher_id, record->publisher_id) == 0 &&
        strcmp(g_enable_guard_cache_record.app_id, record->app_id) == 0 &&
        g_enable_guard_cache_record.version_major == record->version_major &&
        g_enable_guard_cache_record.version_minor == record->version_minor &&
        g_enable_guard_cache_record.version_patch == record->version_patch &&
        g_enable_guard_cache_record.package_len == record->package_len &&
        memcmp(g_enable_guard_cache_record.package_sha256, record->package_sha256,
               APP_INSTALL_SHA256_LEN) == 0 &&
        memcmp(g_enable_guard_cache_record.tbs_sha256, record->tbs_sha256,
               APP_INSTALL_SHA256_LEN) == 0 &&
        memcmp(g_enable_guard_cache_record.publisher_key_sha256,
               record->publisher_key_sha256, APP_INSTALL_SHA256_LEN) == 0) {
        return (g_enable_guard_cache_active == AICAM_TRUE)
                   ? APP_INSTALL_ERR_DOWNGRADE_REFUSED : APP_INSTALL_OK;
    }
    candidate_matches_record =
        (candidate->version_major == record->version_major &&
         candidate->version_minor == record->version_minor &&
         candidate->version_patch == record->version_patch &&
         memcmp(candidate->tbs_sha256, record->tbs_sha256,
                APP_INSTALL_SHA256_LEN) == 0 &&
         memcmp(candidate->publisher_key_sha256, record->publisher_key_sha256,
                APP_INSTALL_SHA256_LEN) == 0)
            ? AICAM_TRUE : AICAM_FALSE;
    package = (uint8_t *)malloc(record->package_len);
    if (package == NULL) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    rc = io->file_read(io->ctx, record->package_path, package,
                       record->package_len, &got);
    if (rc != APP_INSTALL_IO_OK || got != record->package_len) {
        free(package);
        return (candidate_matches_record == AICAM_TRUE)
                   ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    inst_sha256(package, got, sha);
    if (memcmp(sha, record->package_sha256, sizeof(sha)) != 0) {
        free(package);
        return (candidate_matches_record == AICAM_TRUE)
                   ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    memset(&verdict, 0, sizeof(verdict));
    arc = verify(package, got, &verdict, verify_ctx);
    free(package);
    if (arc != APP_ADMISSION_OK ||
        strcmp(record->publisher_id, verdict.publisher_id) != 0 ||
        strcmp(record->app_id, verdict.app_id) != 0 ||
        record->version_major != verdict.version_major ||
        record->version_minor != verdict.version_minor ||
        record->version_patch != verdict.version_patch ||
        memcmp(record->tbs_sha256, verdict.tbs_sha256,
               APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(record->publisher_key_sha256, verdict.publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        return (candidate_matches_record == AICAM_TRUE)
                   ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (verdict.container_major == APP_NATIVE_VNEXT_MAJOR &&
        (verdict.required_host_caps & APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u) {
        return APP_INSTALL_ERR_DOWNGRADE_REFUSED;
    }
    return APP_INSTALL_OK;
}

static app_install_result_t inst_stage_at_final(const app_install_io_t *io,
                                                const uint8_t *package, size_t len,
                                                const uint8_t pkg_sha[APP_INSTALL_SHA256_LEN],
                                                const char *final_path,
                                                app_install_verify_fn verify,
                                                void *verify_ctx,
                                                aicam_bool_t *wrote_file)
{
    size_t stat_len = 0;
    app_install_io_result_t rc;
    app_install_io_result_t mrc;
    aicam_bool_t match = AICAM_FALSE;

    *wrote_file = AICAM_FALSE;
    rc = io->file_stat(io->ctx, final_path, &stat_len);
    if (rc == APP_INSTALL_IO_ERROR) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (rc == APP_INSTALL_IO_OK && stat_len == len) {
        mrc = inst_file_read_verify(io, final_path, pkg_sha, len, &match);
        if (mrc == APP_INSTALL_IO_ERROR) {
            return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
        }
        if (match == AICAM_TRUE) {
            return APP_INSTALL_OK;
        }
    }
    if (io->file_write(io->ctx, final_path, package, len) != APP_INSTALL_IO_OK) {
        return APP_INSTALL_ERR_WRITE_FAILED;
    }
    *wrote_file = AICAM_TRUE;
    mrc = inst_file_read_verify(io, final_path, pkg_sha, len, &match);
    if (mrc != APP_INSTALL_IO_OK || match != AICAM_TRUE) {
        (void)io->file_remove(io->ctx, final_path);
        *wrote_file = AICAM_FALSE;
        return APP_INSTALL_ERR_WRITE_FAILED;
    }
    {
        app_admission_verdict_t verdict;
        app_admission_result_t arc;
        uint8_t *buf;
        buf = (uint8_t *)malloc(len);
        if (buf == NULL) {
            (void)io->file_remove(io->ctx, final_path);
            *wrote_file = AICAM_FALSE;
            return APP_INSTALL_ERR_WRITE_FAILED;
        }
        {
            size_t read_len = 0;
            app_install_io_result_t rr = io->file_read(io->ctx, final_path, buf, len, &read_len);
            if (rr != APP_INSTALL_IO_OK || read_len != len) {
                free(buf);
                (void)io->file_remove(io->ctx, final_path);
                *wrote_file = AICAM_FALSE;
                return APP_INSTALL_ERR_WRITE_FAILED;
            }
            arc = verify(buf, read_len, &verdict, verify_ctx);
        }
        free(buf);
        if (arc != APP_ADMISSION_OK) {
            (void)io->file_remove(io->ctx, final_path);
            *wrote_file = AICAM_FALSE;
            return APP_INSTALL_ERR_WRITE_FAILED;
        }
    }
    return APP_INSTALL_OK;
}

static app_install_result_t inst_capacity_verify(const app_install_io_t *io, size_t len)
{
    uint64_t free_bytes = 0;

    if (io->free_bytes(io->ctx, &free_bytes) != APP_INSTALL_IO_OK) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (free_bytes < (uint64_t)len * APP_INSTALL_MIN_FREE_MULTIPLIER) {
        return APP_INSTALL_ERR_STORAGE_FULL;
    }
    return APP_INSTALL_OK;
}

app_install_result_t app_install(const app_install_io_t *io,
                                 app_install_verify_fn verify, void *verify_ctx,
                                 const uint8_t *package, size_t len,
                                 app_install_record_t *out_record)
{
    app_admission_verdict_t verdict;
    app_admission_result_t arc;
    app_install_record_t cur;
    app_install_record_t fresh;
    app_install_record_t new_rec;
    app_install_record_t floor_candidate;
    aicam_bool_t had_cur;
    aicam_bool_t has_prev;
    aicam_bool_t wrote_file = AICAM_FALSE;
    uint8_t pending[APP_INSTALL_PENDING_BLOB_SIZE];
    char final_path[APP_INSTALL_PATH_MAX];
    int cmp;
    app_install_result_t rc;
    aicam_result_t lrc;

    if (io == NULL || verify == NULL || out_record == NULL ||
        io->file_write == NULL || io->file_read == NULL || io->file_remove == NULL ||
        io->file_stat == NULL || io->kv_write == NULL || io->kv_read == NULL ||
        io->kv_delete == NULL || io->free_bytes == NULL ||
        io->tx_begin == NULL || io->tx_end == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    if (package == NULL || len == 0u || len > APP_ADMISSION_MAX_PACKAGE_LEN_V5) {
        return APP_INSTALL_ERR_PARAM;
    }
    if (g_state_unknown == AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (g_app_running == AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    if (g_app_running == AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_BUSY;
    }
    if (inst_uninstall_intent_blocks_boot(io) != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (g_boot_resolved != AICAM_TRUE) {
        app_install_result_t rr = app_install_boot_resolve(io);
        if (rr != APP_INSTALL_OK) {
            io->tx_end(io->ctx);
            return rr;
        }
    }

    arc = verify(package, len, &verdict, verify_ctx);
    if (arc != APP_ADMISSION_OK) {
        io->tx_end(io->ctx);
        return inst_admission_map(arc);
    }

    memset(&floor_candidate, 0, sizeof(floor_candidate));
    snprintf(floor_candidate.publisher_id, sizeof(floor_candidate.publisher_id),
             "%s", verdict.publisher_id);
    snprintf(floor_candidate.app_id, sizeof(floor_candidate.app_id), "%s",
             verdict.app_id);
    inst_sha256(package, len, floor_candidate.package_sha256);
    memcpy(floor_candidate.tbs_sha256, verdict.tbs_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(floor_candidate.publisher_key_sha256, verdict.publisher_key_sha256,
           APP_INSTALL_SHA256_LEN);
    lrc = inst_record_load(io, &cur, &had_cur);
    if (lrc != AICAM_OK) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (verdict.container_major == 5u && had_cur == AICAM_TRUE &&
        (strcmp(cur.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
         memcmp(cur.publisher_key_sha256,
                (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
                APP_INSTALL_SHA256_LEN) != 0)) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_LEGACY_STATE;
    }

    rc = app_native_vnext_floor_check(io, &floor_candidate,
                                      verdict.container_major);
    if (rc != APP_INSTALL_OK) {
        io->tx_end(io->ctx);
        return rc;
    }
    has_prev = had_cur;
    if (had_cur == AICAM_TRUE) {
        if (strcmp(cur.publisher_id, verdict.publisher_id) != 0 ||
            memcmp(cur.publisher_key_sha256, verdict.publisher_key_sha256,
                   APP_INSTALL_SHA256_LEN) != 0) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_PUBLISHER_CONFLICT;
        }
        if (strcmp(cur.app_id, verdict.app_id) != 0) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_IDENTITY_CONFLICT;
        }
        rc = inst_committed_enable_guard(io, verify, verify_ctx, &cur, &verdict,
                                         had_cur, verdict.container_major);
        if (rc != APP_INSTALL_OK) {
            io->tx_end(io->ctx);
            return rc;
        }
        cmp = inst_version_cmp(&cur, &verdict);
        if (cmp == 0) {
            if (memcmp(cur.tbs_sha256, verdict.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0) {
                io->tx_end(io->ctx);
                return APP_INSTALL_ERR_VERSION_CONFLICT;
            }
            {
                size_t exist_len = 0;
                aicam_bool_t match = AICAM_FALSE;
                app_install_io_result_t st_rc;
                app_install_io_result_t mrc;

                st_rc = io->file_stat(io->ctx, cur.package_path, &exist_len);
                if (st_rc == APP_INSTALL_IO_ERROR) {
                    io->tx_end(io->ctx);
                    return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
                }
                if (st_rc == APP_INSTALL_IO_OK && exist_len == (size_t)cur.package_len) {
                    mrc = inst_file_read_verify(io, cur.package_path, cur.package_sha256,
                                                (size_t)cur.package_len, &match);
                    if (mrc == APP_INSTALL_IO_ERROR) {
                        io->tx_end(io->ctx);
                        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
                    }
                    if (match == AICAM_TRUE) {
                        memcpy(out_record, &cur, sizeof(*out_record));
                        memcpy(&g_enable_guard_cache_record, &cur, sizeof(cur));
                        g_enable_guard_cache_active =
                            (aicam_bool_t)(verdict.container_major ==
                                               APP_NATIVE_VNEXT_MAJOR &&
                                           (verdict.required_host_caps &
                                            APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u);
                        g_enable_guard_cache_valid = AICAM_TRUE;
                        io->tx_end(io->ctx);
                        return APP_INSTALL_OK_ALREADY;
                    }
                }
            }
        }
        if (cmp > 0) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_DOWNGRADE_REFUSED;
        }
    }

    rc = inst_capacity_verify(io, len);
    if (rc != APP_INSTALL_OK) {
        io->tx_end(io->ctx);
        return rc;
    }

    {
        uint8_t pkg_sha[APP_INSTALL_SHA256_LEN];
        inst_sha256(package, len, pkg_sha);
        inst_pkg_path(pkg_sha, final_path, sizeof(final_path));
        rc = inst_stage_at_final(io, package, len, pkg_sha, final_path, verify, verify_ctx,
                                 &wrote_file);
    }
    if (rc != APP_INSTALL_OK) {
        io->tx_end(io->ctx);
        return rc;
    }

    memset(&fresh, 0, sizeof(fresh));
    snprintf(fresh.publisher_id, sizeof(fresh.publisher_id), "%s", verdict.publisher_id);
    snprintf(fresh.app_id, sizeof(fresh.app_id), "%s", verdict.app_id);
    fresh.version_major = verdict.version_major;
    fresh.version_minor = verdict.version_minor;
    fresh.version_patch = verdict.version_patch;
    fresh.package_len = (uint32_t)len;
    inst_sha256(package, len, fresh.package_sha256);
    memcpy(fresh.tbs_sha256, verdict.tbs_sha256, APP_INSTALL_SHA256_LEN);
    memcpy(fresh.image_sha256, verdict.image_sha256, APP_INSTALL_SHA256_LEN);
    memcpy(fresh.publisher_key_sha256, verdict.publisher_key_sha256,
           APP_INSTALL_SHA256_LEN);
    snprintf(fresh.package_path, sizeof(fresh.package_path), "%s", final_path);
    memcpy(&new_rec, &fresh, sizeof(new_rec));

    inst_pending_encode(pending, APP_INSTALL_ACTION_INSTALL, has_prev,
                        (has_prev == AICAM_TRUE) ? &cur : NULL);
    if (inst_kv_write_proven(io, APP_INSTALL_KEY_PENDING, pending,
                             sizeof(pending)) != AICAM_TRUE) {
        if (inst_kv_absent_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) == AICAM_TRUE) {
            if (wrote_file == AICAM_TRUE &&
                (had_cur == AICAM_FALSE ||
                 strcmp(cur.package_path, new_rec.package_path) != 0)) {
                (void)io->file_remove(io->ctx, new_rec.package_path);
            }
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_WRITE_FAILED;
        }
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (inst_record_write_proven(io, &new_rec) != AICAM_TRUE) {
        if ((has_prev == AICAM_FALSE
                 ? inst_record_delete_proven(io)
                 : inst_record_write_proven(io, &cur)) == AICAM_TRUE &&
            inst_kv_delete_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_COMMIT_FAILED;
        }
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (inst_kv_delete_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) != AICAM_TRUE) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (has_prev == AICAM_TRUE &&
        (strcmp(cur.publisher_id, new_rec.publisher_id) != 0 ||
         strcmp(cur.app_id, new_rec.app_id) != 0 ||
         memcmp(cur.tbs_sha256, new_rec.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0 ||
         memcmp(cur.publisher_key_sha256, new_rec.publisher_key_sha256,
                APP_INSTALL_SHA256_LEN) != 0) &&
        inst_sidecar_revoke_for_native(io, &new_rec) != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (has_prev == AICAM_TRUE &&
        strcmp(cur.package_path, new_rec.package_path) != 0) {
        (void)io->file_remove(io->ctx, cur.package_path);
    }

    memcpy(&g_enable_guard_cache_record, &new_rec, sizeof(new_rec));
    g_enable_guard_cache_active =
        (aicam_bool_t)(verdict.container_major == APP_NATIVE_VNEXT_MAJOR &&
                       (verdict.required_host_caps &
                        APP_ADMISSION_V4_CAP_ENABLE_CONTROL) != 0u);
    g_enable_guard_cache_valid = AICAM_TRUE;
    memcpy(out_record, &new_rec, sizeof(*out_record));
    io->tx_end(io->ctx);
    return APP_INSTALL_OK;
}

app_install_result_t app_install_uninstall(const app_install_io_t *io,
                                           app_install_record_t *out_removed)
{
    app_install_record_t cur;
    aicam_bool_t had_cur;
    uint8_t pending[APP_INSTALL_PENDING_BLOB_SIZE];
    app_install_io_result_t st_rc;
    size_t pkg_len = 0;
    aicam_result_t lrc;

    if (io == NULL || out_removed == NULL ||
        io->file_stat == NULL ||
        io->kv_write == NULL || io->kv_read == NULL || io->kv_delete == NULL ||
        io->tx_begin == NULL || io->tx_end == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    if (g_state_unknown == AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (g_app_running == AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    if (g_app_running == AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_BUSY;
    }
    if (inst_uninstall_intent_blocks_boot(io) != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (g_boot_resolved != AICAM_TRUE) {
        app_install_result_t rr = app_install_boot_resolve(io);
        if (rr != APP_INSTALL_OK) {
            io->tx_end(io->ctx);
            return rr;
        }
    }

    lrc = inst_record_load(io, &cur, &had_cur);
    if (lrc != AICAM_OK) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (had_cur != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_NOT_FOUND;
    }
    st_rc = io->file_stat(io->ctx, cur.package_path, &pkg_len);
    if (st_rc == APP_INSTALL_IO_ERROR) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    inst_pending_encode(pending, APP_INSTALL_ACTION_UNINSTALL, AICAM_TRUE, &cur);
    if (inst_kv_write_proven(io, APP_INSTALL_KEY_PENDING, pending,
                             sizeof(pending)) != AICAM_TRUE) {
        if (inst_kv_absent_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_WRITE_FAILED;
        }
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (inst_record_delete_proven(io) != AICAM_TRUE) {
        if (inst_record_write_proven(io, &cur) == AICAM_TRUE &&
            inst_kv_delete_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_COMMIT_FAILED;
        }
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (inst_kv_delete_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) != AICAM_TRUE) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    (void)io->file_remove(io->ctx, cur.package_path);
    g_enable_guard_cache_valid = AICAM_FALSE;
    g_enable_guard_cache_active = AICAM_FALSE;
    memset(&g_enable_guard_cache_record, 0, sizeof(g_enable_guard_cache_record));
    memcpy(out_removed, &cur, sizeof(*out_removed));
    io->tx_end(io->ctx);
    return APP_INSTALL_OK;
}

app_install_result_t app_install_query(const app_install_io_t *io,
                                       app_install_verify_fn verify, void *verify_ctx,
                                       app_install_query_t *out)
{
    app_install_record_t cur;
    aicam_bool_t had_cur;
    aicam_result_t lrc;
    size_t stat_len = 0;
    app_install_io_result_t st_rc;
    app_install_io_result_t rd_rc;
    uint8_t *buf;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    size_t read_len = 0;
    app_admission_verdict_t verdict;
    app_admission_result_t arc;

    if (io == NULL || verify == NULL || out == NULL ||
        io->file_read == NULL || io->file_stat == NULL ||
        io->kv_read == NULL || io->tx_begin == NULL || io->tx_end == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    out->state = APP_INSTALL_STATE_NONE;

    if (g_state_unknown == AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    if (inst_uninstall_intent_blocks_boot(io) != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (g_boot_resolved != AICAM_TRUE) {
        app_install_result_t rr = app_install_boot_resolve(io);
        if (rr != APP_INSTALL_OK) {
            io->tx_end(io->ctx);
            return rr;
        }
    }

    lrc = inst_record_load(io, &cur, &had_cur);
    if (lrc != AICAM_OK) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (had_cur != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }

    memcpy(&out->record, &cur, sizeof(cur));

    st_rc = io->file_stat(io->ctx, cur.package_path, &stat_len);
    if (st_rc == APP_INSTALL_IO_ERROR) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (st_rc != APP_INSTALL_IO_OK) {
        snprintf(out->reason, sizeof(out->reason), "%s", "file_missing");
        out->state = APP_INSTALL_STATE_UNAVAILABLE;
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }

    buf = (uint8_t *)malloc(cur.package_len);
    if (buf == NULL) {
        snprintf(out->reason, sizeof(out->reason), "%s", "content_unreadable");
        out->state = APP_INSTALL_STATE_UNAVAILABLE;
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }
    rd_rc = io->file_read(io->ctx, cur.package_path, buf, cur.package_len, &read_len);
    if (rd_rc == APP_INSTALL_IO_ERROR) {
        free(buf);
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (rd_rc != APP_INSTALL_IO_OK || read_len != cur.package_len) {
        free(buf);
        snprintf(out->reason, sizeof(out->reason), "%s",
                 (rd_rc == APP_INSTALL_IO_NOT_FOUND) ? "file_missing" : "content_unreadable");
        out->state = APP_INSTALL_STATE_UNAVAILABLE;
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }
    inst_sha256(buf, read_len, sha);
    if (memcmp(sha, cur.package_sha256, APP_INSTALL_SHA256_LEN) != 0) {
        free(buf);
        snprintf(out->reason, sizeof(out->reason), "%s", "content_mismatch");
        out->state = APP_INSTALL_STATE_UNAVAILABLE;
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }
    arc = verify(buf, read_len, &verdict, verify_ctx);
    free(buf);
    if (arc != APP_ADMISSION_OK) {
        snprintf(out->reason, sizeof(out->reason), "%s", app_admission_result_str(arc));
        out->state = APP_INSTALL_STATE_UNAVAILABLE;
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }
    if (strcmp(cur.publisher_id, verdict.publisher_id) != 0 ||
        strcmp(cur.app_id, verdict.app_id) != 0 ||
        cur.version_major != verdict.version_major ||
        cur.version_minor != verdict.version_minor ||
        cur.version_patch != verdict.version_patch ||
        memcmp(cur.tbs_sha256, verdict.tbs_sha256, APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(cur.publisher_key_sha256, verdict.publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        snprintf(out->reason, sizeof(out->reason), "%s", "identity_mismatch");
        out->state = APP_INSTALL_STATE_UNAVAILABLE;
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }

    out->state = APP_INSTALL_STATE_INSTALLED;
    io->tx_end(io->ctx);
    return APP_INSTALL_OK;
}

app_install_result_t app_install_query_committed(const app_install_io_t *io,
                                                 app_install_record_t *out,
                                                 aicam_bool_t *present)
{
    app_install_record_t cur;
    aicam_bool_t had_cur;
    aicam_result_t lrc;

    if (io == NULL || out == NULL || present == NULL ||
        io->kv_read == NULL || io->tx_begin == NULL || io->tx_end == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    *present = AICAM_FALSE;
    if (g_state_unknown == AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (inst_tx_begin_read(io) != AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    if (inst_uninstall_intent_blocks_boot(io) != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (g_boot_resolved != AICAM_TRUE) {
        app_install_result_t rr = app_install_boot_resolve(io);
        if (rr != APP_INSTALL_OK) {
            io->tx_end(io->ctx);
            return rr;
        }
    }
    lrc = inst_record_load(io, &cur, &had_cur);
    if (lrc != AICAM_OK) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (had_cur != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_INSTALL_OK;
    }
    if (app_native_vnext_floor_check(io, &cur, 0u) != APP_INSTALL_OK) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    memcpy(out, &cur, sizeof(*out));
    *present = AICAM_TRUE;
    io->tx_end(io->ctx);
    return APP_INSTALL_OK;
}

app_install_result_t app_install_boot_resolve(const app_install_io_t *io)
{
    uint8_t pending[APP_INSTALL_PENDING_BLOB_SIZE];
    app_install_io_result_t rc;
    uint8_t action = 0;
    aicam_bool_t has_prev = AICAM_FALSE;
    app_install_record_t prev;
    app_install_result_t uninstall_gate;

    if (io == NULL || io->kv_read == NULL || io->kv_write == NULL ||
        io->kv_delete == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    g_enable_guard_cache_valid = AICAM_FALSE;
    g_enable_guard_cache_active = AICAM_FALSE;
    memset(&g_enable_guard_cache_record, 0, sizeof(g_enable_guard_cache_record));

    uninstall_gate = inst_uninstall_intent_blocks_boot(io);
    if (uninstall_gate != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (app_native_vnext_floor_recover(io) != APP_INSTALL_OK) {
        g_state_unknown = AICAM_TRUE;
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    rc = inst_kv_read_blob(io, APP_INSTALL_KEY_PENDING, pending, sizeof(pending));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        g_boot_resolved = AICAM_TRUE;
        return APP_INSTALL_OK;
    }
    if (rc != APP_INSTALL_IO_OK) {
        g_state_unknown = AICAM_TRUE;
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (inst_pending_decode(pending, sizeof(pending), &action, &has_prev, &prev) !=
            AICAM_TRUE) {
        g_state_unknown = AICAM_TRUE;
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    if (has_prev == AICAM_TRUE) {
        if (inst_record_write_proven(io, &prev) != AICAM_TRUE) {
            g_state_unknown = AICAM_TRUE;
            return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
        }
    } else {
        uint8_t probe[APP_INSTALL_RECORD_BIN_SIZE];
        size_t len = 0;
        app_install_io_result_t rec_rc =
            io->kv_read(io->ctx, APP_INSTALL_KEY_RECORD, probe, sizeof(probe), &len);
        if (rec_rc == APP_INSTALL_IO_OK) {
            if (inst_record_delete_proven(io) != AICAM_TRUE) {
                g_state_unknown = AICAM_TRUE;
                return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
            }
        } else if (rec_rc == APP_INSTALL_IO_ERROR) {
            g_state_unknown = AICAM_TRUE;
            return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
        }
    }

    if (inst_kv_delete_proven(io, APP_INSTALL_KEY_PENDING, APP_INSTALL_PENDING_BLOB_SIZE) != AICAM_TRUE) {
        g_state_unknown = AICAM_TRUE;
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }

    g_boot_resolved = AICAM_TRUE;
    return APP_INSTALL_OK;
}

void app_install_boot_resolve_reset(void)
{
    g_boot_resolved = AICAM_FALSE;
    g_state_unknown = AICAM_FALSE;
    g_enable_guard_cache_valid = AICAM_FALSE;
    g_enable_guard_cache_active = AICAM_FALSE;
    memset(&g_enable_guard_cache_record, 0, sizeof(g_enable_guard_cache_record));
    app_sidecar_lifecycle_reset();
}

void app_install_set_app_running(aicam_bool_t running)
{
    g_app_running = (running == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

aicam_bool_t app_install_app_running(void)
{
    return g_app_running;
}

const char *app_install_result_str(app_install_result_t result)
{
    switch (result) {
    case APP_INSTALL_OK:
        return "OK";
    case APP_INSTALL_OK_ALREADY:
        return "ALREADY_INSTALLED";
    case APP_INSTALL_ERR_PARAM:
        return "PARAM";
    case APP_INSTALL_ERR_BUSY:
        return "BUSY";
    case APP_INSTALL_ERR_BAD_PACKAGE:
        return "BAD_PACKAGE";
    case APP_INSTALL_ERR_SIGNATURE_INVALID:
    case APP_INSTALL_ERR_PUBLISHER_UNTRUSTED:
        return "LEGACY_SIGNED_FORMAT_REJECTED";
    case APP_INSTALL_ERR_TARGET_INCOMPATIBLE:
        return "TARGET_INCOMPATIBLE";
    case APP_INSTALL_ERR_ABI_INCOMPATIBLE:
        return "ABI_INCOMPATIBLE";
    case APP_INSTALL_ERR_RESOURCE_LIMIT:
        return "RESOURCE_LIMIT";
    case APP_INSTALL_ERR_TRUST_UNPROVEN:
        return "LEGACY_SIGNED_FORMAT_REJECTED";
    case APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN:
        return "STORAGE_STATE_UNKNOWN";
    case APP_INSTALL_ERR_STORAGE_FULL:
        return "STORAGE_FULL";
    case APP_INSTALL_ERR_WRITE_FAILED:
        return "WRITE_FAILED";
    case APP_INSTALL_ERR_COMMIT_FAILED:
        return "COMMIT_FAILED";
    case APP_INSTALL_ERR_VERSION_CONFLICT:
        return "VERSION_CONFLICT";
    case APP_INSTALL_ERR_DOWNGRADE_REFUSED:
        return "DOWNGRADE_REFUSED";
    case APP_INSTALL_ERR_PUBLISHER_CONFLICT:
        return "PUBLISHER_CONFLICT";
    case APP_INSTALL_ERR_IDENTITY_CONFLICT:
        return "IDENTITY_CONFLICT";
    case APP_INSTALL_ERR_NOT_FOUND:
        return "NOT_FOUND";
    case APP_INSTALL_ERR_UNAVAILABLE:
        return "UNAVAILABLE";
    case APP_INSTALL_ERR_LEGACY_STATE:
        return "LEGACY_SIGNED_STATE_PRESERVED";
    default:
        return "UNKNOWN";
    }
}

const char *app_install_state_str(app_install_state_t state)
{
    switch (state) {
    case APP_INSTALL_STATE_NONE:
        return "none";
    case APP_INSTALL_STATE_INSTALLED:
        return "installed";
    case APP_INSTALL_STATE_UNAVAILABLE:
        return "unavailable";
    default:
        return "unknown";
    }
}

#include "app_sidecar_verify.c"
#include "app_sidecar_lifecycle.c"
#include "app_pair_installer.c"
