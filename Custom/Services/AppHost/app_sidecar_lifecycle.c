#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_sidecar_lifecycle.h"
#include "app_state_store.h"
#include "app_native_vnext_floor.h"
#include "generic_math.h"
#include "mbedtls/sha256.h"

#define SC_STORE_MAGIC 0x31534341u
#define SC_STORE_VERSION 1u
#define SC_STORE_PAIR_FLOOR_MAGIC 0x31465041u
#define SC_STORE_PAIR_FLOOR_VERSION 1u
#define SC_STORE_PAIR_FLOOR_SIZE 216u
#define SC_STORE_PAIR_FLOOR_PUB_OFF 16u
#define SC_STORE_PAIR_FLOOR_APP_OFF 32u
#define SC_STORE_PAIR_FLOOR_KEY_OFF 64u
#define SC_STORE_PAIR_FLOOR_NATIVE_OFF 96u
#define SC_STORE_PAIR_FLOOR_UI_OFF 128u
#define SC_STORE_PAIR_FLOOR_PAIR_OFF 160u
#define SC_STORE_PAIR_FLOOR_SEQ_OFF 192u
#define SC_STORE_PAIR_FLOOR_NATIVE_VERSION_OFF 200u
#define SC_STORE_PAIR_FLOOR_UI_VERSION_OFF 206u
#define SC_STORE_PAIR_FLOOR_CRC_OFF 212u
#define SC_STORE_ACTIVE 1u
#define SC_STORE_REVOKED 2u
#define SC_STORE_PHASE_STAGED 1u
#define SC_STORE_PHASE_VERIFIED 2u
#define SC_STORE_PHASE_COMMITTING 3u
#define SC_STORE_REC_STATE_OFF 8u
#define SC_STORE_REC_GEN_OFF 12u
#define SC_STORE_REC_PUB_OFF 20u
#define SC_STORE_REC_APP_OFF 36u
#define SC_STORE_REC_KEY_OFF 68u
#define SC_STORE_REC_NATIVE_OFF 100u
#define SC_STORE_REC_UI_OFF 132u
#define SC_STORE_REC_VERSION_OFF 164u
#define SC_STORE_REC_ABI_OFF 170u
#define SC_STORE_REC_COUNT_OFF 174u
#define SC_STORE_REC_MANIFEST_OFF 176u
#define SC_STORE_REC_PAYLOAD_OFF 180u
#define SC_STORE_REC_BUNDLE_LEN_OFF 184u
#define SC_STORE_REC_BUNDLE_SHA_OFF 188u
#define SC_STORE_REC_PAIR_TBS_OFF 220u
#define SC_STORE_REC_NATIVE_VERSION_OFF 252u
#define SC_STORE_REC_RELEASE_SEQ_OFF 258u
#define SC_STORE_REC_CRC_OFF 268u
#define SC_STORE_STAGE_PHASE_OFF 8u
#define SC_STORE_STAGE_TARGET_OFF 9u
#define SC_STORE_STAGE_HAS_DISPLACED_OFF 10u
#define SC_STORE_STAGE_CANDIDATE_OFF 12u
#define SC_STORE_STAGE_DISPLACED_OFF 284u
#define SC_STORE_STAGE_CRC_OFF 556u
#define SC_STORE_SLOT_MISSING 0
#define SC_STORE_SLOT_VALID 1
#define SC_STORE_SLOT_CORRUPT 2
#define SC_STORE_SLOT_IO_ERROR 3
#define SC_STORE_BUNDLE_PATH_MAX 96u
#define SC_STORE_IDENTITY_SEED_LEN 144u
#define SC_UNINSTALL_MAGIC 0x31554941u
#define SC_UNINSTALL_VERSION 1u
#define SC_UNINSTALL_STATE_PENDING 1u
#define SC_UNINSTALL_STATE_OFF 8u
#define SC_UNINSTALL_RECORD_OFF 12u
#define SC_UNINSTALL_CRC_OFF 308u
#define SC_VNEXT_FLOOR_MAGIC 0x3446564Eu
#define SC_VNEXT_FLOOR_VERSION 1u
#define SC_VNEXT_FLOOR_ACTIVE 1u
#define SC_VNEXT_FLOOR_RETIRE_READY 2u
#define SC_VNEXT_EPOCH_MAGIC 0x3445504Eu
#define SC_VNEXT_EPOCH_VERSION 1u
#define SC_VNEXT_EPOCH_SIZE 16u
#define SC_VNEXT_FLOOR_SIZE 192u
#define SC_VNEXT_FLOOR_STATE_OFF 8u
#define SC_VNEXT_FLOOR_MAJOR_OFF 12u
#define SC_VNEXT_FLOOR_GENERATION_OFF 16u
#define SC_VNEXT_FLOOR_SESSION_OFF 24u
#define SC_VNEXT_FLOOR_BOOT_OFF 28u
#define SC_VNEXT_FLOOR_REQUEST_OFF 32u
#define SC_VNEXT_FLOOR_CONFIG_OFF 36u
#define SC_VNEXT_FLOOR_STATE_REV_OFF 40u
#define SC_VNEXT_FLOOR_PUB_OFF 44u
#define SC_VNEXT_FLOOR_APP_OFF 60u
#define SC_VNEXT_FLOOR_KEY_OFF 92u
#define SC_VNEXT_FLOOR_TBS_OFF 124u
#define SC_VNEXT_FLOOR_PACKAGE_OFF 156u
#define SC_VNEXT_FLOOR_CRC_OFF 188u

typedef struct {
    uint8_t state;
    uint64_t generation;
    char publisher_id[APP_SIDECAR_PUBLISHER_ID_SLOT + 1u];
    char app_id[APP_SIDECAR_APP_ID_SLOT + 1u];
    uint8_t publisher_key_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t native_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t ui_tbs_sha256[APP_SIDECAR_SHA256_LEN];
    uint8_t pair_tbs_sha256[APP_PAIR_TBS_SHA256_LEN];
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint32_t host_ui_abi;
    uint16_t native_version[3];
    uint64_t release_seq;
    uint16_t file_count;
    uint32_t manifest_len;
    uint32_t asset_payload_len;
    uint32_t bundle_len;
    uint8_t bundle_sha256[APP_SIDECAR_SHA256_LEN];
} sc_store_record_t;

typedef struct {
    uint64_t generation;
    app_pair_history_t history;
} sc_store_pair_floor_t;

typedef struct {
    uint8_t phase;
    uint8_t target_slot;
    aicam_bool_t has_displaced;
    sc_store_record_t candidate;
    sc_store_record_t displaced;
} sc_store_stage_t;

typedef struct {
    app_sidecar_file_desc_t files[APP_SIDECAR_FILE_COUNT_MAX];
    uint16_t count;
    aicam_bool_t overflow;
} sc_store_file_set_t;

typedef struct {
    uint8_t state;
    app_install_record_t record;
} sc_uninstall_intent_t;

typedef enum {
    SC_STORE_VERIFY_OK = 0,
    SC_STORE_VERIFY_INVALID = 1,
    SC_STORE_VERIFY_IO = 2
} sc_store_verify_result_t;

static aicam_bool_t sc_store_pair_floor_matches_record(
    const sc_store_pair_floor_t *floor,
    const sc_store_record_t *record);

static aicam_bool_t g_sidecar_boot_resolved;

static aicam_bool_t sc_verify_cache_hit(const sc_store_record_t *record,
                                        const app_install_record_t *native);
static void sc_verify_cache_store(const sc_store_record_t *record,
                                  const app_install_record_t *native,
                                  const app_sidecar_descriptor_t *descriptor);
static aicam_bool_t g_verify_cache_valid;
static sc_store_record_t g_verify_cache_record;
static app_install_record_t g_verify_cache_native;
static app_sidecar_descriptor_t g_verify_cache_descriptor;

static void sc_store_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t sc_store_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void sc_store_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t sc_store_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void sc_store_put_u64(uint8_t *p, uint64_t v)
{
    sc_store_put_u32(p, (uint32_t)v);
    sc_store_put_u32(p + 4u, (uint32_t)(v >> 32));
}

static uint64_t sc_store_get_u64(const uint8_t *p)
{
    return (uint64_t)sc_store_get_u32(p) |
           ((uint64_t)sc_store_get_u32(p + 4u) << 32);
}

static void sc_store_copy_padded(uint8_t *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    memset(dst, 0, size);
    if (len > size) {
        len = size;
    }
    memcpy(dst, src, len);
}

static aicam_bool_t sc_store_text_valid(const uint8_t *field, size_t size)
{
    size_t len = 0u;
    size_t i;

    while (len < size && field[len] != 0u) {
        len++;
    }
    if (len == 0u || len == size) {
        return AICAM_FALSE;
    }
    for (i = len; i < size; i++) {
        if (field[i] != 0u) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t sc_store_record_encode(const sc_store_record_t *record,
                                           uint8_t out[APP_SIDECAR_LIFECYCLE_RECORD_SIZE])
{
    size_t pub_len;
    size_t app_len;

    if (record == NULL || out == NULL || record->generation == 0u ||
        (record->state != SC_STORE_ACTIVE && record->state != SC_STORE_REVOKED)) {
        return AICAM_FALSE;
    }
    pub_len = strlen(record->publisher_id);
    app_len = strlen(record->app_id);
    if (pub_len == 0u || pub_len > APP_SIDECAR_PUBLISHER_ID_SLOT ||
        app_len == 0u || app_len > APP_SIDECAR_APP_ID_SLOT) {
        return AICAM_FALSE;
    }
    memset(out, 0, APP_SIDECAR_LIFECYCLE_RECORD_SIZE);
    sc_store_put_u32(out, SC_STORE_MAGIC);
    sc_store_put_u32(out + 4u, SC_STORE_VERSION);
    out[SC_STORE_REC_STATE_OFF] = record->state;
    sc_store_put_u64(out + SC_STORE_REC_GEN_OFF, record->generation);
    sc_store_copy_padded(out + SC_STORE_REC_PUB_OFF, record->publisher_id,
                         APP_SIDECAR_PUBLISHER_ID_SLOT);
    sc_store_copy_padded(out + SC_STORE_REC_APP_OFF, record->app_id,
                         APP_SIDECAR_APP_ID_SLOT);
    memcpy(out + SC_STORE_REC_KEY_OFF, record->publisher_key_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out + SC_STORE_REC_NATIVE_OFF, record->native_tbs_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out + SC_STORE_REC_UI_OFF, record->ui_tbs_sha256,
           APP_SIDECAR_SHA256_LEN);
    sc_store_put_u16(out + SC_STORE_REC_VERSION_OFF, record->version_major);
    sc_store_put_u16(out + SC_STORE_REC_VERSION_OFF + 2u, record->version_minor);
    sc_store_put_u16(out + SC_STORE_REC_VERSION_OFF + 4u, record->version_patch);
    sc_store_put_u32(out + SC_STORE_REC_ABI_OFF, record->host_ui_abi);
    sc_store_put_u16(out + SC_STORE_REC_COUNT_OFF, record->file_count);
    sc_store_put_u32(out + SC_STORE_REC_MANIFEST_OFF, record->manifest_len);
    sc_store_put_u32(out + SC_STORE_REC_PAYLOAD_OFF, record->asset_payload_len);
    sc_store_put_u32(out + SC_STORE_REC_BUNDLE_LEN_OFF, record->bundle_len);
    memcpy(out + SC_STORE_REC_BUNDLE_SHA_OFF, record->bundle_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out + SC_STORE_REC_PAIR_TBS_OFF, record->pair_tbs_sha256,
           APP_PAIR_TBS_SHA256_LEN);
    sc_store_put_u16(out + SC_STORE_REC_NATIVE_VERSION_OFF,
                     record->native_version[0]);
    sc_store_put_u16(out + SC_STORE_REC_NATIVE_VERSION_OFF + 2u,
                     record->native_version[1]);
    sc_store_put_u16(out + SC_STORE_REC_NATIVE_VERSION_OFF + 4u,
                     record->native_version[2]);
    sc_store_put_u64(out + SC_STORE_REC_RELEASE_SEQ_OFF, record->release_seq);
    sc_store_put_u32(out + SC_STORE_REC_CRC_OFF,
                     generic_crc32(out, SC_STORE_REC_CRC_OFF));
    return AICAM_TRUE;
}

static aicam_bool_t sc_store_record_decode(const uint8_t *data, size_t len,
                                           sc_store_record_t *out)
{
    if (data == NULL || out == NULL || len != APP_SIDECAR_LIFECYCLE_RECORD_SIZE ||
        sc_store_get_u32(data) != SC_STORE_MAGIC ||
        sc_store_get_u32(data + 4u) != SC_STORE_VERSION ||
        sc_store_get_u32(data + SC_STORE_REC_CRC_OFF) !=
            generic_crc32(data, SC_STORE_REC_CRC_OFF) ||
        (data[SC_STORE_REC_STATE_OFF] != SC_STORE_ACTIVE &&
         data[SC_STORE_REC_STATE_OFF] != SC_STORE_REVOKED) ||
        sc_store_text_valid(data + SC_STORE_REC_PUB_OFF,
                            APP_SIDECAR_PUBLISHER_ID_SLOT) != AICAM_TRUE ||
        sc_store_text_valid(data + SC_STORE_REC_APP_OFF,
                            APP_SIDECAR_APP_ID_SLOT) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->state = data[SC_STORE_REC_STATE_OFF];
    out->generation = sc_store_get_u64(data + SC_STORE_REC_GEN_OFF);
    memcpy(out->publisher_id, data + SC_STORE_REC_PUB_OFF,
           APP_SIDECAR_PUBLISHER_ID_SLOT);
    memcpy(out->app_id, data + SC_STORE_REC_APP_OFF, APP_SIDECAR_APP_ID_SLOT);
    memcpy(out->publisher_key_sha256, data + SC_STORE_REC_KEY_OFF,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out->native_tbs_sha256, data + SC_STORE_REC_NATIVE_OFF,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out->ui_tbs_sha256, data + SC_STORE_REC_UI_OFF,
           APP_SIDECAR_SHA256_LEN);
    out->version_major = sc_store_get_u16(data + SC_STORE_REC_VERSION_OFF);
    out->version_minor = sc_store_get_u16(data + SC_STORE_REC_VERSION_OFF + 2u);
    out->version_patch = sc_store_get_u16(data + SC_STORE_REC_VERSION_OFF + 4u);
    out->host_ui_abi = sc_store_get_u32(data + SC_STORE_REC_ABI_OFF);
    out->file_count = sc_store_get_u16(data + SC_STORE_REC_COUNT_OFF);
    out->manifest_len = sc_store_get_u32(data + SC_STORE_REC_MANIFEST_OFF);
    out->asset_payload_len = sc_store_get_u32(data + SC_STORE_REC_PAYLOAD_OFF);
    out->bundle_len = sc_store_get_u32(data + SC_STORE_REC_BUNDLE_LEN_OFF);
    memcpy(out->bundle_sha256, data + SC_STORE_REC_BUNDLE_SHA_OFF,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out->pair_tbs_sha256, data + SC_STORE_REC_PAIR_TBS_OFF,
           APP_PAIR_TBS_SHA256_LEN);
    out->native_version[0] = sc_store_get_u16(data + SC_STORE_REC_NATIVE_VERSION_OFF);
    out->native_version[1] = sc_store_get_u16(data + SC_STORE_REC_NATIVE_VERSION_OFF + 2u);
    out->native_version[2] = sc_store_get_u16(data + SC_STORE_REC_NATIVE_VERSION_OFF + 4u);
    out->release_seq = sc_store_get_u64(data + SC_STORE_REC_RELEASE_SEQ_OFF);
    if (out->generation == 0u || out->file_count > APP_SIDECAR_FILE_COUNT_MAX ||
        out->bundle_len > APP_SIDECAR_LIFECYCLE_BUNDLE_MAX ||
        out->asset_payload_len > APP_SIDECAR_ASSET_PAYLOAD_MAX ||
        out->manifest_len > APP_SIDECAR_MANIFEST_LEN_MAX ||
        (out->state == SC_STORE_ACTIVE &&
         (out->file_count == 0u || out->bundle_len == 0u ||
          out->host_ui_abi != APP_SIDECAR_HOST_UI_ABI))) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void sc_store_stage_encode(const sc_store_stage_t *stage,
                                  uint8_t out[APP_SIDECAR_LIFECYCLE_STAGE_SIZE])
{
    uint8_t rec[APP_SIDECAR_LIFECYCLE_RECORD_SIZE];

    memset(out, 0, APP_SIDECAR_LIFECYCLE_STAGE_SIZE);
    sc_store_put_u32(out, SC_STORE_MAGIC);
    sc_store_put_u32(out + 4u, SC_STORE_VERSION);
    out[SC_STORE_STAGE_PHASE_OFF] = stage->phase;
    out[SC_STORE_STAGE_TARGET_OFF] = stage->target_slot;
    out[SC_STORE_STAGE_HAS_DISPLACED_OFF] =
        (stage->has_displaced == AICAM_TRUE) ? 1u : 0u;
    if (sc_store_record_encode(&stage->candidate, rec) == AICAM_TRUE) {
        memcpy(out + SC_STORE_STAGE_CANDIDATE_OFF, rec, sizeof(rec));
    }
    if (stage->has_displaced == AICAM_TRUE &&
        sc_store_record_encode(&stage->displaced, rec) == AICAM_TRUE) {
        memcpy(out + SC_STORE_STAGE_DISPLACED_OFF, rec, sizeof(rec));
    }
    sc_store_put_u32(out + SC_STORE_STAGE_CRC_OFF,
                     generic_crc32(out, SC_STORE_STAGE_CRC_OFF));
}

static aicam_bool_t sc_store_stage_decode(const uint8_t *data, size_t len,
                                          sc_store_stage_t *out)
{
    uint8_t has_displaced;

    if (data == NULL || out == NULL || len != APP_SIDECAR_LIFECYCLE_STAGE_SIZE ||
        sc_store_get_u32(data) != SC_STORE_MAGIC ||
        sc_store_get_u32(data + 4u) != SC_STORE_VERSION ||
        sc_store_get_u32(data + SC_STORE_STAGE_CRC_OFF) !=
            generic_crc32(data, SC_STORE_STAGE_CRC_OFF) ||
        data[SC_STORE_STAGE_PHASE_OFF] < SC_STORE_PHASE_STAGED ||
        data[SC_STORE_STAGE_PHASE_OFF] > SC_STORE_PHASE_COMMITTING ||
        data[SC_STORE_STAGE_TARGET_OFF] > 1u) {
        return AICAM_FALSE;
    }
    has_displaced = data[SC_STORE_STAGE_HAS_DISPLACED_OFF];
    if (has_displaced > 1u ||
        sc_store_record_decode(data + SC_STORE_STAGE_CANDIDATE_OFF,
                               APP_SIDECAR_LIFECYCLE_RECORD_SIZE,
                               &out->candidate) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    memset(&out->displaced, 0, sizeof(out->displaced));
    if (has_displaced == 1u &&
        sc_store_record_decode(data + SC_STORE_STAGE_DISPLACED_OFF,
                               APP_SIDECAR_LIFECYCLE_RECORD_SIZE,
                               &out->displaced) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (has_displaced == 0u) {
        size_t i;
        for (i = 0u; i < APP_SIDECAR_LIFECYCLE_RECORD_SIZE; i++) {
            if (data[SC_STORE_STAGE_DISPLACED_OFF + i] != 0u) {
                return AICAM_FALSE;
            }
        }
    }
    out->phase = data[SC_STORE_STAGE_PHASE_OFF];
    out->target_slot = data[SC_STORE_STAGE_TARGET_OFF];
    out->has_displaced = (has_displaced == 1u) ? AICAM_TRUE : AICAM_FALSE;
    if (out->phase >= SC_STORE_PHASE_VERIFIED &&
        out->candidate.state == SC_STORE_ACTIVE &&
        (out->candidate.file_count == 0u || out->candidate.host_ui_abi != APP_SIDECAR_HOST_UI_ABI)) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static const char *sc_store_slot_key(uint8_t slot)
{
    return (slot == 0u) ? APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_A :
                          APP_SIDECAR_LIFECYCLE_KEY_ACTIVE_B;
}

static app_install_io_result_t sc_store_read_key(const app_install_io_t *io,
                                                 const char *key,
                                                 uint8_t *out, size_t expected_len)
{
    size_t got = 0u;
    app_install_io_result_t rc = io->kv_read(io->ctx, key, out, expected_len, &got);

    if (rc == APP_INSTALL_IO_OK && got != expected_len) {
        return APP_INSTALL_IO_ERROR;
    }
    return rc;
}

static aicam_bool_t sc_store_kv_write_proven(const app_install_io_t *io,
                                             const char *key,
                                             const uint8_t *data, size_t len)
{
    uint8_t back[APP_SIDECAR_LIFECYCLE_STAGE_SIZE];
    app_install_io_result_t write_rc;
    app_install_io_result_t read_rc;

    if (len > sizeof(back)) {
        return AICAM_FALSE;
    }
    write_rc = io->kv_write(io->ctx, key, data, len);
    read_rc = sc_store_read_key(io, key, back, len);
    if (read_rc == APP_INSTALL_IO_OK && memcmp(back, data, len) == 0) {
        return AICAM_TRUE;
    }
    if (write_rc == APP_INSTALL_IO_ERROR && read_rc == APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_FALSE;
    }
    return AICAM_FALSE;
}

static aicam_bool_t sc_store_pair_floor_encode(
    const sc_store_pair_floor_t *floor,
    uint8_t out[SC_STORE_PAIR_FLOOR_SIZE])
{
    const app_pair_history_t *history;
    size_t publisher_len;
    size_t app_len;
    size_t i;

    if (floor == NULL || out == NULL || floor->generation == 0u ||
        floor->history.release_seq == 0u) {
        return AICAM_FALSE;
    }
    history = &floor->history;
    publisher_len = strlen(history->publisher_id);
    app_len = strlen(history->app_id);
    if (publisher_len == 0u || publisher_len >= APP_INSTALL_PUBLISHER_SLOT ||
        app_len == 0u || app_len >= APP_INSTALL_APP_SLOT) {
        return AICAM_FALSE;
    }
    memset(out, 0, SC_STORE_PAIR_FLOOR_SIZE);
    sc_store_put_u32(out, SC_STORE_PAIR_FLOOR_MAGIC);
    sc_store_put_u32(out + 4u, SC_STORE_PAIR_FLOOR_VERSION);
    sc_store_put_u64(out + 8u, floor->generation);
    sc_store_copy_padded(out + SC_STORE_PAIR_FLOOR_PUB_OFF,
                         history->publisher_id, APP_INSTALL_PUBLISHER_SLOT);
    sc_store_copy_padded(out + SC_STORE_PAIR_FLOOR_APP_OFF,
                         history->app_id, APP_INSTALL_APP_SLOT);
    memcpy(out + SC_STORE_PAIR_FLOOR_KEY_OFF, history->publisher_key_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(out + SC_STORE_PAIR_FLOOR_NATIVE_OFF, history->native_tbs_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(out + SC_STORE_PAIR_FLOOR_UI_OFF, history->ui_tbs_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(out + SC_STORE_PAIR_FLOOR_PAIR_OFF, history->pair_tbs_sha256,
           APP_PAIR_TBS_SHA256_LEN);
    sc_store_put_u64(out + SC_STORE_PAIR_FLOOR_SEQ_OFF, history->release_seq);
    for (i = 0u; i < 3u; i++) {
        sc_store_put_u16(out + SC_STORE_PAIR_FLOOR_NATIVE_VERSION_OFF + (2u * i),
                         history->native_version[i]);
        sc_store_put_u16(out + SC_STORE_PAIR_FLOOR_UI_VERSION_OFF + (2u * i),
                         history->ui_version[i]);
    }
    sc_store_put_u32(out + SC_STORE_PAIR_FLOOR_CRC_OFF,
                     generic_crc32(out, SC_STORE_PAIR_FLOOR_CRC_OFF));
    return AICAM_TRUE;
}

static aicam_bool_t sc_store_pair_floor_decode(
    const uint8_t *data,
    size_t len,
    sc_store_pair_floor_t *out)
{
    size_t i;

    if (data == NULL || out == NULL || len != SC_STORE_PAIR_FLOOR_SIZE ||
        sc_store_get_u32(data) != SC_STORE_PAIR_FLOOR_MAGIC ||
        sc_store_get_u32(data + 4u) != SC_STORE_PAIR_FLOOR_VERSION ||
        sc_store_get_u32(data + SC_STORE_PAIR_FLOOR_CRC_OFF) !=
            generic_crc32(data, SC_STORE_PAIR_FLOOR_CRC_OFF) ||
        sc_store_text_valid(data + SC_STORE_PAIR_FLOOR_PUB_OFF,
                            APP_INSTALL_PUBLISHER_SLOT) != AICAM_TRUE ||
        sc_store_text_valid(data + SC_STORE_PAIR_FLOOR_APP_OFF,
                            APP_INSTALL_APP_SLOT) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->generation = sc_store_get_u64(data + 8u);
    memcpy(out->history.publisher_id, data + SC_STORE_PAIR_FLOOR_PUB_OFF,
           APP_INSTALL_PUBLISHER_SLOT);
    memcpy(out->history.app_id, data + SC_STORE_PAIR_FLOOR_APP_OFF,
           APP_INSTALL_APP_SLOT);
    memcpy(out->history.publisher_key_sha256, data + SC_STORE_PAIR_FLOOR_KEY_OFF,
           APP_INSTALL_SHA256_LEN);
    memcpy(out->history.native_tbs_sha256, data + SC_STORE_PAIR_FLOOR_NATIVE_OFF,
           APP_INSTALL_SHA256_LEN);
    memcpy(out->history.ui_tbs_sha256, data + SC_STORE_PAIR_FLOOR_UI_OFF,
           APP_INSTALL_SHA256_LEN);
    memcpy(out->history.pair_tbs_sha256, data + SC_STORE_PAIR_FLOOR_PAIR_OFF,
           APP_PAIR_TBS_SHA256_LEN);
    out->history.release_seq = sc_store_get_u64(data + SC_STORE_PAIR_FLOOR_SEQ_OFF);
    for (i = 0u; i < 3u; i++) {
        out->history.native_version[i] = sc_store_get_u16(
            data + SC_STORE_PAIR_FLOOR_NATIVE_VERSION_OFF + (2u * i));
        out->history.ui_version[i] = sc_store_get_u16(
            data + SC_STORE_PAIR_FLOOR_UI_VERSION_OFF + (2u * i));
    }
    return (out->generation != 0u && out->history.release_seq != 0u) ?
           AICAM_TRUE : AICAM_FALSE;
}

static int sc_store_pair_floor_load(const app_install_io_t *io,
                                   sc_store_pair_floor_t *out)
{
    uint8_t data[SC_STORE_PAIR_FLOOR_SIZE];
    app_install_io_result_t rc;

    rc = sc_store_read_key(io, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                           data, sizeof(data));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return SC_STORE_SLOT_MISSING;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return SC_STORE_SLOT_IO_ERROR;
    }
    if (sc_store_pair_floor_decode(data, sizeof(data), out) != AICAM_TRUE) {
        return SC_STORE_SLOT_CORRUPT;
    }
    return SC_STORE_SLOT_VALID;
}

static void *sc_store_big_alloc(const app_install_io_t *io, size_t len)
{
    if (io != NULL && io->big_alloc != NULL) {
        return io->big_alloc(io->ctx, len);
    }
    return malloc(len);
}

static void sc_store_big_free(const app_install_io_t *io, void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    if (io != NULL && io->big_free != NULL) {
        io->big_free(io->ctx, ptr);
        return;
    }
    free(ptr);
}

static aicam_bool_t sc_store_kv_delete_proven(const app_install_io_t *io,
                                              const char *key, size_t blob_size)
{
    uint8_t probe[APP_SIDECAR_LIFECYCLE_STAGE_SIZE];
    size_t got = 0u;
    app_install_io_result_t rc = io->kv_read(io->ctx, key, probe, blob_size, &got);

    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_TRUE;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    rc = io->kv_delete(io->ctx, key);
    if (rc != APP_INSTALL_IO_OK && rc != APP_INSTALL_IO_NOT_FOUND) {
        rc = io->kv_read(io->ctx, key, probe, blob_size, &got);
        return (rc == APP_INSTALL_IO_NOT_FOUND) ? AICAM_TRUE : AICAM_FALSE;
    }
    rc = io->kv_read(io->ctx, key, probe, blob_size, &got);
    return (rc == APP_INSTALL_IO_NOT_FOUND) ? AICAM_TRUE : AICAM_FALSE;
}

static void sc_store_hex(const uint8_t *data, size_t len, char *out)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        (void)snprintf(out + (i * 2u), 3u, "%02x", data[i]);
    }
    out[len * 2u] = '\0';
}

static void sc_store_bundle_path(const uint8_t sha[APP_SIDECAR_SHA256_LEN],
                                 char *out, size_t cap)
{
    char hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];

    sc_store_hex(sha, APP_SIDECAR_SHA256_LEN, hex);
    (void)snprintf(out, cap, "/apps/ui-b_%s.neui", hex);
}

static void sc_store_asset_path(const sc_store_record_t *record, uint16_t index,
                                char *out, size_t cap)
{
    uint8_t seed[SC_STORE_IDENTITY_SEED_LEN];
    uint8_t root[APP_SIDECAR_SHA256_LEN];
    char hex[APP_SIDECAR_SHA256_LEN * 2u + 1u];
    size_t pos = 0u;

    memset(seed, 0, sizeof(seed));
    sc_store_copy_padded(seed, record->publisher_id, APP_SIDECAR_PUBLISHER_ID_SLOT);
    sc_store_copy_padded(seed + APP_SIDECAR_PUBLISHER_ID_SLOT, record->app_id,
                         APP_SIDECAR_APP_ID_SLOT);
    pos = APP_SIDECAR_PUBLISHER_ID_SLOT + APP_SIDECAR_APP_ID_SLOT;
    memcpy(seed + pos, record->publisher_key_sha256, APP_SIDECAR_SHA256_LEN);
    pos += APP_SIDECAR_SHA256_LEN;
    memcpy(seed + pos, record->native_tbs_sha256, APP_SIDECAR_SHA256_LEN);
    pos += APP_SIDECAR_SHA256_LEN;
    memcpy(seed + pos, record->ui_tbs_sha256, APP_SIDECAR_SHA256_LEN);
    mbedtls_sha256(seed, sizeof(seed), root, 0);
    sc_store_hex(root, sizeof(root), hex);
    (void)snprintf(out, cap, "/apps/ui-a_%s_%02u.bin", hex, (unsigned int)index);
}

static void sc_store_file_collect(void *user, const app_sidecar_file_desc_t *desc)
{
    sc_store_file_set_t *set = (sc_store_file_set_t *)user;

    if (set->count >= APP_SIDECAR_FILE_COUNT_MAX) {
        set->overflow = AICAM_TRUE;
        return;
    }
    memcpy(&set->files[set->count], desc, sizeof(*desc));
    set->count++;
}

static aicam_bool_t sc_store_native_equal(const app_install_record_t *a,
                                          const app_install_record_t *b)
{
    return (strcmp(a->publisher_id, b->publisher_id) == 0 &&
            strcmp(a->app_id, b->app_id) == 0 &&
            a->version_major == b->version_major &&
            a->version_minor == b->version_minor &&
            a->version_patch == b->version_patch &&
            a->package_len == b->package_len &&
            memcmp(a->package_sha256, b->package_sha256, APP_INSTALL_SHA256_LEN) == 0 &&
            memcmp(a->tbs_sha256, b->tbs_sha256, APP_INSTALL_SHA256_LEN) == 0 &&
            memcmp(a->publisher_key_sha256, b->publisher_key_sha256,
                   APP_INSTALL_SHA256_LEN) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_native_is_unsigned(const app_install_record_t *native)
{
    return (native != NULL &&
            strcmp(native->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0 &&
            memcmp(native->publisher_key_sha256,
                   (const uint8_t[APP_SIDECAR_SHA256_LEN]){0},
                   APP_SIDECAR_SHA256_LEN) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_record_is_unsigned(const sc_store_record_t *record)
{
    return (record != NULL &&
            strcmp(record->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0 &&
            memcmp(record->publisher_key_sha256,
                   (const uint8_t[APP_SIDECAR_SHA256_LEN]){0},
                   APP_SIDECAR_SHA256_LEN) == 0 && record->release_seq == 0u)
               ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_record_is_retired(const sc_store_record_t *record)
{
    size_t i;

    if (record == NULL || record->state != SC_STORE_REVOKED ||
        record->file_count != 0u || record->bundle_len != 0u ||
        record->manifest_len != 0u || record->asset_payload_len != 0u ||
        record->host_ui_abi != 0u) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < APP_SIDECAR_SHA256_LEN; i++) {
        if (record->bundle_sha256[i] != 0u) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t sc_store_record_admits_unsigned(const sc_store_record_t *record)
{
    return (sc_store_record_is_unsigned(record) == AICAM_TRUE ||
            sc_store_record_is_retired(record) == AICAM_TRUE) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_record_native_equal(const sc_store_record_t *record,
                                                  const app_install_record_t *native)
{
    return (strcmp(record->publisher_id, native->publisher_id) == 0 &&
            strcmp(record->app_id, native->app_id) == 0 &&
            memcmp(record->publisher_key_sha256, native->publisher_key_sha256,
                   APP_SIDECAR_SHA256_LEN) == 0 &&
            memcmp(record->native_tbs_sha256, native->tbs_sha256,
                   APP_SIDECAR_SHA256_LEN) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_trust_matches(const app_sidecar_trust_t *trust,
                                           const app_install_record_t *native)
{
    if (trust == NULL || native == NULL ||
        sc_store_native_is_unsigned(native) != AICAM_TRUE ||
        trust->publisher_id == NULL || trust->app_id == NULL ||
        trust->native_tbs_sha256 == NULL ||
        strcmp(trust->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
        strcmp(native->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
        trust->publisher_id_len != strlen(native->publisher_id) ||
        trust->app_id_len != strlen(native->app_id) ||
        strcmp(trust->app_id, native->app_id) != 0 ||
        memcmp(trust->native_tbs_sha256, native->tbs_sha256,
               APP_SIDECAR_SHA256_LEN) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void sc_store_record_from_native(sc_store_record_t *record,
                                        const app_install_record_t *native)
{
    memset(record, 0, sizeof(*record));
    record->state = SC_STORE_ACTIVE;
    (void)snprintf(record->publisher_id, sizeof(record->publisher_id), "%s",
                   native->publisher_id);
    (void)snprintf(record->app_id, sizeof(record->app_id), "%s", native->app_id);
    memcpy(record->publisher_key_sha256, native->publisher_key_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(record->native_tbs_sha256, native->tbs_sha256, APP_SIDECAR_SHA256_LEN);
}

static int sc_store_slot_load(const app_install_io_t *io, uint8_t slot,
                              sc_store_record_t *record)
{
    uint8_t data[APP_SIDECAR_LIFECYCLE_RECORD_SIZE];
    app_install_io_result_t rc;

    rc = sc_store_read_key(io, sc_store_slot_key(slot), data, sizeof(data));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return SC_STORE_SLOT_MISSING;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return SC_STORE_SLOT_IO_ERROR;
    }
    if (sc_store_record_decode(data, sizeof(data), record) != AICAM_TRUE) {
        return SC_STORE_SLOT_CORRUPT;
    }
    return SC_STORE_SLOT_VALID;
}

static aicam_bool_t sc_store_slot_write(const app_install_io_t *io, uint8_t slot,
                                        const sc_store_record_t *record)
{
    uint8_t data[APP_SIDECAR_LIFECYCLE_RECORD_SIZE];

    if (sc_store_record_encode(record, data) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    return sc_store_kv_write_proven(io, sc_store_slot_key(slot), data, sizeof(data));
}

static int sc_store_stage_load(const app_install_io_t *io, sc_store_stage_t *stage)
{
    uint8_t data[APP_SIDECAR_LIFECYCLE_STAGE_SIZE];
    app_install_io_result_t rc;

    rc = sc_store_read_key(io, APP_SIDECAR_LIFECYCLE_KEY_STAGING,
                           data, sizeof(data));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return SC_STORE_SLOT_MISSING;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return SC_STORE_SLOT_IO_ERROR;
    }
    if (sc_store_stage_decode(data, sizeof(data), stage) != AICAM_TRUE) {
        return SC_STORE_SLOT_CORRUPT;
    }
    return SC_STORE_SLOT_VALID;
}

static aicam_bool_t sc_store_stage_write(const app_install_io_t *io,
                                         const sc_store_stage_t *stage)
{
    uint8_t data[APP_SIDECAR_LIFECYCLE_STAGE_SIZE];

    sc_store_stage_encode(stage, data);
    return sc_store_kv_write_proven(io, APP_SIDECAR_LIFECYCLE_KEY_STAGING,
                                    data, sizeof(data));
}

static aicam_bool_t sc_store_file_write_proven(const app_install_io_t *io,
                                               const char *path,
                                               const uint8_t *data, size_t len)
{
    uint8_t *back;
    size_t stat_len = 0u;
    size_t read_len = 0u;
    app_install_io_result_t write_rc;
    app_install_io_result_t stat_rc;
    app_install_io_result_t read_rc;
    aicam_bool_t result = AICAM_FALSE;

    if (len == 0u || data == NULL) {
        return AICAM_FALSE;
    }
    stat_rc = io->file_stat(io->ctx, path, &stat_len);
    if (stat_rc == APP_INSTALL_IO_OK) {
        if (stat_len != len) {
            return AICAM_FALSE;
        }
        back = (uint8_t *)sc_store_big_alloc(io, len);
        if (back == NULL) {
            return AICAM_FALSE;
        }
        read_rc = io->file_read(io->ctx, path, back, len, &read_len);
        if (read_rc == APP_INSTALL_IO_OK && read_len == len &&
            memcmp(back, data, len) == 0) {
            result = AICAM_TRUE;
        }
        sc_store_big_free(io, back);
        return result;
    }
    if (stat_rc != APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_FALSE;
    }
    write_rc = io->file_write(io->ctx, path, data, len);
    stat_rc = io->file_stat(io->ctx, path, &stat_len);
    if (stat_rc != APP_INSTALL_IO_OK || stat_len != len) {
        return AICAM_FALSE;
    }
    back = (uint8_t *)sc_store_big_alloc(io, len);
    if (back == NULL) {
        return AICAM_FALSE;
    }
    read_rc = io->file_read(io->ctx, path, back, len, &read_len);
    if (read_rc == APP_INSTALL_IO_OK && read_len == len &&
        memcmp(back, data, len) == 0) {
        result = AICAM_TRUE;
    }
    sc_store_big_free(io, back);
    if (write_rc == APP_INSTALL_IO_OK || result == AICAM_TRUE) {
        return result;
    }
    return AICAM_FALSE;
}

static aicam_bool_t sc_store_file_remove_proven(const app_install_io_t *io,
                                                const char *path)
{
    size_t len = 0u;
    app_install_io_result_t rc = io->file_remove(io->ctx, path);

    if (rc != APP_INSTALL_IO_OK && rc != APP_INSTALL_IO_NOT_FOUND) {
        rc = io->file_stat(io->ctx, path, &len);
        return (rc == APP_INSTALL_IO_NOT_FOUND) ? AICAM_TRUE : AICAM_FALSE;
    }
    rc = io->file_stat(io->ctx, path, &len);
    return (rc == APP_INSTALL_IO_NOT_FOUND) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_record_same_content(const sc_store_record_t *a,
                                                const sc_store_record_t *b)
{
    return (strcmp(a->publisher_id, b->publisher_id) == 0 &&
            strcmp(a->app_id, b->app_id) == 0 &&
            memcmp(a->publisher_key_sha256, b->publisher_key_sha256,
                   APP_SIDECAR_SHA256_LEN) == 0 &&
            memcmp(a->native_tbs_sha256, b->native_tbs_sha256,
                   APP_SIDECAR_SHA256_LEN) == 0 &&
            memcmp(a->ui_tbs_sha256, b->ui_tbs_sha256,
                   APP_SIDECAR_SHA256_LEN) == 0 &&
            memcmp(a->pair_tbs_sha256, b->pair_tbs_sha256,
                   APP_PAIR_TBS_SHA256_LEN) == 0 &&
            memcmp(a->native_version, b->native_version,
                   sizeof(a->native_version)) == 0 &&
            a->release_seq == b->release_seq) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_record_referenced(const sc_store_record_t *record,
                                               const sc_store_record_t *a,
                                               aicam_bool_t have_a,
                                               const sc_store_record_t *b,
                                               aicam_bool_t have_b)
{
    return ((have_a == AICAM_TRUE && sc_store_record_same_content(record, a) == AICAM_TRUE) ||
            (have_b == AICAM_TRUE && sc_store_record_same_content(record, b) == AICAM_TRUE)) ?
           AICAM_TRUE : AICAM_FALSE;
}

static sc_store_verify_result_t sc_store_verify_record(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native,
    const sc_store_record_t *record,
    app_sidecar_descriptor_t *out)
{
    char bundle_path[APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX];
    uint8_t *bundle = NULL;
    uint8_t sha[APP_SIDECAR_SHA256_LEN];
    size_t stat_len = 0u;
    size_t read_len = 0u;
    size_t payload_offset;
    sc_store_file_set_t files;
    app_sidecar_verdict_t verdict;
    app_sidecar_result_t verify_rc;
    app_install_io_result_t io_rc;
    uint16_t i;
    sc_store_verify_result_t result = SC_STORE_VERIFY_INVALID;

    if (record->state != SC_STORE_ACTIVE ||
        sc_store_record_native_equal(record, native) != AICAM_TRUE ||
        sc_store_trust_matches(trust, native) != AICAM_TRUE) {
        return SC_STORE_VERIFY_INVALID;
    }
    sc_store_bundle_path(record->bundle_sha256, bundle_path, sizeof(bundle_path));
    io_rc = io->file_stat(io->ctx, bundle_path, &stat_len);
    if (io_rc == APP_INSTALL_IO_ERROR) {
        return SC_STORE_VERIFY_IO;
    }
    if (io_rc != APP_INSTALL_IO_OK || stat_len != record->bundle_len) {
        return SC_STORE_VERIFY_INVALID;
    }
    bundle = (uint8_t *)sc_store_big_alloc(io, record->bundle_len);
    if (bundle == NULL) {
        return SC_STORE_VERIFY_IO;
    }
    io_rc = io->file_read(io->ctx, bundle_path, bundle, record->bundle_len, &read_len);
    if (io_rc == APP_INSTALL_IO_ERROR) {
        result = SC_STORE_VERIFY_IO;
        goto cleanup;
    }
    if (io_rc != APP_INSTALL_IO_OK || read_len != record->bundle_len) {
        goto cleanup;
    }
    inst_sha256(bundle, read_len, sha);
    if (memcmp(sha, record->bundle_sha256, sizeof(sha)) != 0) {
        goto cleanup;
    }
    memset(&files, 0, sizeof(files));
    verify_rc = app_sidecar_verify_bundle(bundle, read_len, trust,
                                          sc_store_file_collect, &files, &verdict);
    if (verify_rc != APP_SIDECAR_OK || files.overflow == AICAM_TRUE ||
        files.count != record->file_count ||
        memcmp(verdict.ui_tbs_sha256, record->ui_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) != 0 ||
        verdict.version_major != record->version_major ||
        verdict.version_minor != record->version_minor ||
        verdict.version_patch != record->version_patch ||
        verdict.host_ui_abi != record->host_ui_abi ||
        verdict.manifest_len != record->manifest_len ||
        verdict.asset_payload_len != record->asset_payload_len) {
        goto cleanup;
    }
    payload_offset = APP_SIDECAR_HEADER_SIZE + verdict.manifest_len;
    memset(out, 0, sizeof(*out));
    out->state = APP_SIDECAR_LIFECYCLE_ACTIVE;
    (void)snprintf(out->publisher_id, sizeof(out->publisher_id), "%s",
                   record->publisher_id);
    (void)snprintf(out->app_id, sizeof(out->app_id), "%s", record->app_id);
    memcpy(out->publisher_key_sha256, record->publisher_key_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out->native_tbs_sha256, record->native_tbs_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(out->ui_tbs_sha256, record->ui_tbs_sha256,
           APP_SIDECAR_SHA256_LEN);
    out->version_major = record->version_major;
    out->version_minor = record->version_minor;
    out->version_patch = record->version_patch;
    out->host_ui_abi = record->host_ui_abi;
    out->service_generation = record->generation;
    out->bundle_len = record->bundle_len;
    out->manifest_len = record->manifest_len;
    out->asset_payload_len = record->asset_payload_len;
    out->file_count = record->file_count;
    for (i = 0u; i < files.count; i++) {
        size_t asset_len = files.files[i].file_len;
        uint8_t *asset = NULL;
        size_t asset_read_len = 0u;
        char asset_path[APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX];

        if (asset_len == 0u || payload_offset > read_len || asset_len > read_len - payload_offset) {
            goto cleanup;
        }
        sc_store_asset_path(record, i, asset_path, sizeof(asset_path));
        io_rc = io->file_stat(io->ctx, asset_path, &stat_len);
        if (io_rc == APP_INSTALL_IO_ERROR) {
            result = SC_STORE_VERIFY_IO;
            goto cleanup;
        }
        if (io_rc != APP_INSTALL_IO_OK || stat_len != asset_len) {
            goto cleanup;
        }
        asset = (uint8_t *)malloc(asset_len);
        if (asset == NULL) {
            result = SC_STORE_VERIFY_IO;
            goto cleanup;
        }
        io_rc = io->file_read(io->ctx, asset_path, asset, asset_len, &asset_read_len);
        if (io_rc == APP_INSTALL_IO_ERROR) {
            free(asset);
            result = SC_STORE_VERIFY_IO;
            goto cleanup;
        }
        if (io_rc != APP_INSTALL_IO_OK || asset_read_len != asset_len ||
            memcmp(asset, bundle + payload_offset, asset_len) != 0) {
            free(asset);
            goto cleanup;
        }
        free(asset);
        (void)snprintf(out->files[i].path, sizeof(out->files[i].path), "%s",
                       files.files[i].path);
        sc_store_asset_path(record, i, out->files[i].storage_path,
                            sizeof(out->files[i].storage_path));
        out->files[i].file_len = files.files[i].file_len;
        memcpy(out->files[i].sha256, files.files[i].sha256,
               APP_SIDECAR_SHA256_LEN);
        payload_offset += asset_len;
    }
    if (payload_offset != APP_SIDECAR_HEADER_SIZE + verdict.manifest_len +
                              verdict.asset_payload_len) {
        goto cleanup;
    }
    result = SC_STORE_VERIFY_OK;

cleanup:
    sc_store_big_free(io, bundle);
    return result;
}

static int sc_store_highest(const sc_store_record_t recs[2], const int slots[2])
{
    if (slots[0] != SC_STORE_SLOT_VALID && slots[1] != SC_STORE_SLOT_VALID) {
        return -1;
    }
    if (slots[0] == SC_STORE_SLOT_VALID && slots[1] == SC_STORE_SLOT_VALID) {
        return (recs[0].generation >= recs[1].generation) ? 0 : 1;
    }
    return (slots[0] == SC_STORE_SLOT_VALID) ? 0 : 1;
}

static uint64_t sc_store_next_generation(const sc_store_record_t recs[2],
                                         const int slots[2])
{
    uint64_t highest = 0u;
    int i;

    for (i = 0; i < 2; i++) {
        if (slots[i] == SC_STORE_SLOT_VALID && recs[i].generation > highest) {
            highest = recs[i].generation;
        }
    }
    return (highest == UINT64_MAX) ? 0u : highest + 1u;
}

static aicam_bool_t sc_store_load_slots(const app_install_io_t *io,
                                        sc_store_record_t recs[2], int slots[2])
{
    uint8_t i;

    for (i = 0u; i < 2u; i++) {
        slots[i] = sc_store_slot_load(io, i, &recs[i]);
        if (slots[i] == SC_STORE_SLOT_IO_ERROR) {
            return AICAM_FALSE;
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t sc_store_stage_delete(const app_install_io_t *io)
{
    return sc_store_kv_delete_proven(io, APP_SIDECAR_LIFECYCLE_KEY_STAGING,
                            APP_SIDECAR_LIFECYCLE_STAGE_SIZE);
}

static aicam_bool_t sc_store_record_resources_remove(const app_install_io_t *io,
                                                     const sc_store_record_t *record,
                                                     const sc_store_record_t *keep_a,
                                                     aicam_bool_t have_a,
                                                     const sc_store_record_t *keep_b,
                                                     aicam_bool_t have_b)
{
    char path[APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX];
    uint16_t i;
    aicam_bool_t shared_assets = sc_store_record_referenced(record, keep_a, have_a,
                                                             keep_b, have_b);

    if (record->file_count > APP_SIDECAR_FILE_COUNT_MAX) {
        return AICAM_FALSE;
    }
    if (shared_assets != AICAM_TRUE) {
        for (i = 0u; i < record->file_count; i++) {
            sc_store_asset_path(record, i, path, sizeof(path));
            if (sc_store_file_remove_proven(io, path) != AICAM_TRUE) {
                return AICAM_FALSE;
            }
        }
    }
    if (record->bundle_len > 0u) {
        aicam_bool_t shared_bundle =
            (have_a == AICAM_TRUE && memcmp(keep_a->bundle_sha256, record->bundle_sha256,
                                            APP_SIDECAR_SHA256_LEN) == 0) ||
            (have_b == AICAM_TRUE && memcmp(keep_b->bundle_sha256, record->bundle_sha256,
                                            APP_SIDECAR_SHA256_LEN) == 0);
        if (shared_bundle != AICAM_TRUE) {
            sc_store_bundle_path(record->bundle_sha256, path, sizeof(path));
            if (sc_store_file_remove_proven(io, path) != AICAM_TRUE) {
                return AICAM_FALSE;
            }
        }
    }
    return AICAM_TRUE;
}

static aicam_bool_t sc_store_candidate_cleanup(const app_install_io_t *io,
                                               const sc_store_record_t *candidate,
                                               const sc_store_record_t recs[2],
                                               const int slots[2],
                                               int target_slot,
                                               aicam_bool_t clean_assets)
{
    aicam_bool_t have_a = (slots[0] == SC_STORE_SLOT_VALID && target_slot != 0) ?
                          AICAM_TRUE : AICAM_FALSE;
    aicam_bool_t have_b = (slots[1] == SC_STORE_SLOT_VALID && target_slot != 1) ?
                          AICAM_TRUE : AICAM_FALSE;

    if (clean_assets == AICAM_TRUE && candidate->file_count > APP_SIDECAR_FILE_COUNT_MAX) {
        return AICAM_FALSE;
    }
    return sc_store_record_resources_remove(io, candidate,
                                            (have_a == AICAM_TRUE) ? &recs[0] : NULL,
                                            have_a,
                                            (have_b == AICAM_TRUE) ? &recs[1] : NULL,
                                            have_b);
}

static aicam_bool_t sc_store_record_slot_matches(const sc_store_record_t *record,
                                                 const sc_store_record_t *candidate)
{
    return (record->state == candidate->state &&
            record->generation == candidate->generation &&
            sc_store_record_same_content(record, candidate) == AICAM_TRUE &&
            memcmp(record->bundle_sha256, candidate->bundle_sha256,
                   APP_SIDECAR_SHA256_LEN) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static app_sidecar_lifecycle_result_t sc_store_stage_recover_locked(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native)
{
    sc_store_stage_t stage;
    sc_store_record_t recs[2];
    sc_store_pair_floor_t floor;
    int slots[2];
    int stage_state = sc_store_stage_load(io, &stage);
    int floor_state;
    int target;
    aicam_bool_t committed = AICAM_FALSE;
    app_sidecar_descriptor_t descriptor;
    sc_store_verify_result_t verify_rc;

    if (stage_state == SC_STORE_SLOT_MISSING) {
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (stage_state != SC_STORE_SLOT_VALID ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (stage.phase >= SC_STORE_PHASE_COMMITTING && stage.candidate.release_seq > 0u) {
        floor_state = sc_store_pair_floor_load(io, &floor);
        if (floor_state != SC_STORE_SLOT_VALID ||
            sc_store_pair_floor_matches_record(&floor, &stage.candidate) != AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
    }
    target = stage.target_slot;
    if (slots[target] == SC_STORE_SLOT_VALID && stage.phase >= SC_STORE_PHASE_VERIFIED &&
        sc_store_record_slot_matches(&recs[target], &stage.candidate) == AICAM_TRUE) {
        if (recs[target].state == SC_STORE_REVOKED) {
            committed = AICAM_TRUE;
        } else {
            verify_rc = sc_store_verify_record(io, trust, native, &recs[target], &descriptor);
            if (verify_rc == SC_STORE_VERIFY_IO) {
                return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
            }
            if (verify_rc == SC_STORE_VERIFY_OK) {
                committed = AICAM_TRUE;
            }
        }
    }
    if (committed == AICAM_TRUE) {
        uint8_t other = (uint8_t)(1 - target);
        aicam_bool_t have_other = (slots[other] == SC_STORE_SLOT_VALID) ? AICAM_TRUE : AICAM_FALSE;
        if (stage.has_displaced == AICAM_TRUE &&
            sc_store_record_resources_remove(io, &stage.displaced,
                                             &recs[target], AICAM_TRUE,
                                             (have_other == AICAM_TRUE) ? &recs[other] : NULL,
                                             have_other) != AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        if (sc_store_stage_delete(io) != AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (sc_store_candidate_cleanup(io, &stage.candidate, recs, slots, target,
                                   (stage.phase >= SC_STORE_PHASE_VERIFIED) ?
                                       AICAM_TRUE : AICAM_FALSE) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (slots[target] == SC_STORE_SLOT_CORRUPT ||
        (slots[target] == SC_STORE_SLOT_VALID &&
         sc_store_record_slot_matches(&recs[target], &stage.candidate) == AICAM_TRUE)) {
        if (sc_store_kv_delete_proven(io, sc_store_slot_key((uint8_t)target),
                                APP_SIDECAR_LIFECYCLE_RECORD_SIZE) != AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
    }
    if (sc_store_stage_delete(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    return APP_SIDECAR_LIFECYCLE_OK;
}

static int sc_store_version_cmp(const sc_store_record_t *record,
                                const app_sidecar_verdict_t *verdict)
{
    if (record->version_major != verdict->version_major) {
        return (record->version_major < verdict->version_major) ? -1 : 1;
    }
    if (record->version_minor != verdict->version_minor) {
        return (record->version_minor < verdict->version_minor) ? -1 : 1;
    }
    if (record->version_patch != verdict->version_patch) {
        return (record->version_patch < verdict->version_patch) ? -1 : 1;
    }
    return 0;
}

static void sc_store_record_update_verdict(sc_store_record_t *record,
                                           const app_sidecar_verdict_t *verdict,
                                           const uint8_t bundle_sha[APP_SIDECAR_SHA256_LEN],
                                           uint32_t bundle_len,
                                           const app_install_record_t *native,
                                           const app_pair_history_t *pair_history)
{
    record->state = SC_STORE_ACTIVE;
    record->file_count = verdict->file_count;
    record->version_major = verdict->version_major;
    record->version_minor = verdict->version_minor;
    record->version_patch = verdict->version_patch;
    record->host_ui_abi = verdict->host_ui_abi;
    record->manifest_len = verdict->manifest_len;
    record->asset_payload_len = verdict->asset_payload_len;
    record->bundle_len = bundle_len;
    memcpy(record->ui_tbs_sha256, verdict->ui_tbs_sha256,
           APP_SIDECAR_SHA256_LEN);
    memcpy(record->bundle_sha256, bundle_sha, APP_SIDECAR_SHA256_LEN);
    if (native != NULL) {
        record->native_version[0] = native->version_major;
        record->native_version[1] = native->version_minor;
        record->native_version[2] = native->version_patch;
    }
    if (pair_history != NULL) {
        memcpy(record->pair_tbs_sha256, pair_history->pair_tbs_sha256,
               APP_PAIR_TBS_SHA256_LEN);
        record->release_seq = pair_history->release_seq;
        memcpy(record->native_version, pair_history->native_version,
               sizeof(record->native_version));
    }
}

static aicam_bool_t sc_store_pair_matches_native(
    const app_pair_history_t *pair_history,
    const app_install_record_t *native)
{
    return (pair_history != NULL && pair_history->release_seq > 0u &&
            strcmp(pair_history->publisher_id, native->publisher_id) == 0 &&
            strcmp(pair_history->app_id, native->app_id) == 0 &&
            memcmp(pair_history->publisher_key_sha256, native->publisher_key_sha256,
                   APP_INSTALL_SHA256_LEN) == 0 &&
            memcmp(pair_history->native_tbs_sha256, native->tbs_sha256,
                   APP_INSTALL_SHA256_LEN) == 0 &&
            pair_history->native_version[0] == native->version_major &&
            pair_history->native_version[1] == native->version_minor &&
            pair_history->native_version[2] == native->version_patch)
               ? AICAM_TRUE : AICAM_FALSE;
}

static app_sidecar_lifecycle_result_t sc_store_pair_history_result(
    app_pair_history_result_t result)
{
    switch (result) {
    case APP_PAIR_HISTORY_FRESH:
    case APP_PAIR_HISTORY_IDEMPOTENT:
    case APP_PAIR_HISTORY_UPGRADE:
        return APP_SIDECAR_LIFECYCLE_OK;
    case APP_PAIR_HISTORY_ERR_IDENTITY:
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    case APP_PAIR_HISTORY_ERR_CONTENT_CONFLICT:
        return APP_SIDECAR_LIFECYCLE_ERR_VERSION_CONFLICT;
    case APP_PAIR_HISTORY_ERR_DOWNGRADE:
        return APP_SIDECAR_LIFECYCLE_ERR_DOWNGRADE;
    default:
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
}

static aicam_bool_t sc_store_pair_floor_matches_record(
    const sc_store_pair_floor_t *floor,
    const sc_store_record_t *record)
{
    const app_pair_history_t *history;

    if (floor == NULL || record == NULL || record->release_seq == 0u) {
        return AICAM_FALSE;
    }
    history = &floor->history;
    return (strcmp(record->publisher_id, history->publisher_id) == 0 &&
            strcmp(record->app_id, history->app_id) == 0 &&
            memcmp(record->publisher_key_sha256, history->publisher_key_sha256,
                   APP_INSTALL_SHA256_LEN) == 0 &&
            memcmp(record->native_tbs_sha256, history->native_tbs_sha256,
                   APP_INSTALL_SHA256_LEN) == 0 &&
            memcmp(record->ui_tbs_sha256, history->ui_tbs_sha256,
                   APP_INSTALL_SHA256_LEN) == 0 &&
            memcmp(record->pair_tbs_sha256, history->pair_tbs_sha256,
                   APP_PAIR_TBS_SHA256_LEN) == 0 &&
            record->release_seq == history->release_seq &&
            memcmp(record->native_version, history->native_version,
                   sizeof(history->native_version)) == 0 &&
            record->version_major == history->ui_version[0] &&
            record->version_minor == history->ui_version[1] &&
            record->version_patch == history->ui_version[2]) ?
           AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_pair_record_exists(const sc_store_record_t recs[2],
                                                const int slots[2])
{
    return ((slots[0] == SC_STORE_SLOT_VALID && recs[0].release_seq > 0u &&
             sc_store_record_is_retired(&recs[0]) != AICAM_TRUE) ||
            (slots[1] == SC_STORE_SLOT_VALID && recs[1].release_seq > 0u &&
             sc_store_record_is_retired(&recs[1]) != AICAM_TRUE)) ?
           AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_store_pair_floor_lineage_retired(
    const sc_store_pair_floor_t *floor,
    const sc_store_record_t recs[2],
    const int slots[2])
{
    int i;

    for (i = 0; i < 2; i++) {
        if (slots[i] == SC_STORE_SLOT_VALID &&
            sc_store_record_is_retired(&recs[i]) == AICAM_TRUE &&
            sc_store_pair_floor_matches_record(floor, &recs[i]) == AICAM_TRUE) {
            return AICAM_TRUE;
        }
    }
    return AICAM_FALSE;
}

static app_sidecar_lifecycle_result_t sc_store_pair_floor_check_locked(
    const app_install_io_t *io,
    const app_pair_history_t *candidate,
    const sc_store_record_t recs[2],
    const int slots[2],
    sc_store_pair_floor_t *out_floor,
    app_pair_history_result_t *out_history_result)
{
    sc_store_pair_floor_t floor;
    int state;

    if (out_history_result != NULL) {
        *out_history_result = APP_PAIR_HISTORY_ERR_PARAM;
    }
    state = sc_store_pair_floor_load(io, &floor);
    if (state == SC_STORE_SLOT_MISSING) {
        if (sc_store_pair_record_exists(recs, slots) == AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        if (out_history_result != NULL) {
            *out_history_result = app_pair_history_check(NULL, candidate);
            return sc_store_pair_history_result(*out_history_result);
        }
        return sc_store_pair_history_result(app_pair_history_check(NULL, candidate));
    }
    if (state != SC_STORE_SLOT_VALID) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (out_floor != NULL) {
        *out_floor = floor;
    }
    if (out_history_result != NULL) {
        *out_history_result = app_pair_history_check(&floor.history, candidate);
        return sc_store_pair_history_result(*out_history_result);
    }
    return sc_store_pair_history_result(app_pair_history_check(&floor.history, candidate));
}

static app_sidecar_lifecycle_result_t sc_store_pair_floor_advance_locked(
    const app_install_io_t *io,
    const app_pair_history_t *candidate,
    const sc_store_record_t recs[2],
    const int slots[2])
{
    sc_store_pair_floor_t current;
    sc_store_pair_floor_t next;
    app_pair_history_result_t history_result;
    app_sidecar_lifecycle_result_t result;
    uint8_t data[SC_STORE_PAIR_FLOOR_SIZE];
    int state;

    state = sc_store_pair_floor_load(io, &current);
    if (state == SC_STORE_SLOT_MISSING) {
        if (sc_store_pair_record_exists(recs, slots) == AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        memset(&current, 0, sizeof(current));
        history_result = APP_PAIR_HISTORY_FRESH;
    } else if (state == SC_STORE_SLOT_VALID) {
        history_result = app_pair_history_check(&current.history, candidate);
    } else {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    result = sc_store_pair_history_result(history_result);
    if (result != APP_SIDECAR_LIFECYCLE_OK) {
        return result;
    }
    if (history_result == APP_PAIR_HISTORY_IDEMPOTENT) {
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (current.generation == UINT64_MAX) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    memset(&next, 0, sizeof(next));
    next.generation = current.generation + 1u;
    next.history = *candidate;
    if (sc_store_pair_floor_encode(&next, data) != AICAM_TRUE ||
        sc_store_kv_write_proven(io, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                                 data, sizeof(data)) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    return APP_SIDECAR_LIFECYCLE_OK;
}

static aicam_bool_t sc_store_assets_write(const app_install_io_t *io,
                                          const sc_store_record_t *record,
                                          const uint8_t *bundle,
                                          const app_sidecar_verdict_t *verdict,
                                          const sc_store_file_set_t *files)
{
    size_t pos = APP_SIDECAR_HEADER_SIZE + verdict->manifest_len;
    uint16_t i;

    if (files->overflow == AICAM_TRUE || files->count != verdict->file_count) {
        return AICAM_FALSE;
    }
    for (i = 0u; i < files->count; i++) {
        char path[APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX];
        size_t len = files->files[i].file_len;
        if (len == 0u || pos > record->bundle_len || len > record->bundle_len - pos) {
            return AICAM_FALSE;
        }
        sc_store_asset_path(record, i, path, sizeof(path));
        if (sc_store_file_write_proven(io, path, bundle + pos, len) != AICAM_TRUE) {
            return AICAM_FALSE;
        }
        pos += len;
    }
    return (pos == APP_SIDECAR_HEADER_SIZE + verdict->manifest_len +
                    verdict->asset_payload_len) ? AICAM_TRUE : AICAM_FALSE;
}

static void sc_store_fill_descriptor(const sc_store_record_t *record,
                                     const app_sidecar_descriptor_t *verified,
                                     app_sidecar_descriptor_t *out)
{
    memcpy(out, verified, sizeof(*out));
    out->service_generation = record->generation;
    memcpy(out->pair_tbs_sha256, record->pair_tbs_sha256,
           APP_PAIR_TBS_SHA256_LEN);
    memcpy(out->native_version, record->native_version,
           sizeof(out->native_version));
    out->release_seq = record->release_seq;
}

static app_sidecar_lifecycle_result_t sc_store_write_generation_copy_locked(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native,
    const sc_store_record_t *source,
    uint8_t target,
    const sc_store_record_t *displaced,
    aicam_bool_t have_displaced,
    app_sidecar_descriptor_t *out)
{
    sc_store_record_t recs[2];
    int slots[2];
    sc_store_stage_t stage;
    sc_store_record_t fresh = *source;
    sc_store_pair_floor_t floor;
    uint64_t next_gen;
    sc_store_verify_result_t verify_rc;
    app_sidecar_descriptor_t verified;
    int floor_state;

    if (sc_store_load_slots(io, recs, slots) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    floor_state = sc_store_pair_floor_load(io, &floor);
    if (floor_state == SC_STORE_SLOT_IO_ERROR || floor_state == SC_STORE_SLOT_CORRUPT ||
        (floor_state == SC_STORE_SLOT_MISSING && source->release_seq > 0u) ||
        (floor_state == SC_STORE_SLOT_VALID &&
         sc_store_pair_record_exists(recs, slots) == AICAM_TRUE &&
         sc_store_pair_floor_matches_record(&floor, source) != AICAM_TRUE) ||
        (floor_state == SC_STORE_SLOT_VALID && source->release_seq == 0u &&
         sc_store_pair_record_exists(recs, slots) == AICAM_TRUE)) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    next_gen = sc_store_next_generation(recs, slots);
    if (next_gen == 0u) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    fresh.state = SC_STORE_ACTIVE;
    fresh.generation = next_gen;
    memset(&stage, 0, sizeof(stage));
    stage.phase = SC_STORE_PHASE_COMMITTING;
    stage.target_slot = target;
    stage.has_displaced = have_displaced;
    stage.candidate = fresh;
    if (have_displaced == AICAM_TRUE) {
        stage.displaced = *displaced;
    }
    if (sc_store_stage_write(io, &stage) != AICAM_TRUE ||
        sc_store_slot_write(io, target, &fresh) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    verify_rc = sc_store_verify_record(io, trust, native, &fresh, &verified);
    if (verify_rc != SC_STORE_VERIFY_OK) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (have_displaced == AICAM_TRUE &&
        sc_store_record_resources_remove(io, displaced, &fresh, AICAM_TRUE,
                                         source, AICAM_TRUE) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (sc_store_stage_delete(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    sc_store_fill_descriptor(&fresh, &verified, out);
    return APP_SIDECAR_LIFECYCLE_OK;
}

static app_sidecar_lifecycle_result_t sc_store_boot_resolve_locked(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state)
{
    sc_store_record_t recs[2];
    sc_store_pair_floor_t floor;
    int slots[2];
    int highest;
    int selected;
    int other;
    int target;
    int floor_state;
    sc_store_verify_result_t verify_rc;
    app_sidecar_descriptor_t verified;
    app_sidecar_lifecycle_result_t rc;

    *out_state = APP_SIDECAR_LIFECYCLE_NONE;
    memset(out, 0, sizeof(*out));
    if (sc_store_stage_recover_locked(io, trust, native) != APP_SIDECAR_LIFECYCLE_OK ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    floor_state = sc_store_pair_floor_load(io, &floor);
    if (floor_state == SC_STORE_SLOT_IO_ERROR || floor_state == SC_STORE_SLOT_CORRUPT ||
        (floor_state == SC_STORE_SLOT_MISSING &&
         sc_store_pair_record_exists(recs, slots) == AICAM_TRUE)) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    highest = sc_store_highest(recs, slots);
    if (highest < 0) {
        if (floor_state == SC_STORE_SLOT_VALID) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        }
        g_sidecar_boot_resolved = AICAM_TRUE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    selected = highest;
    if (floor_state == SC_STORE_SLOT_VALID &&
        sc_store_pair_floor_matches_record(&floor, &recs[selected]) != AICAM_TRUE) {
        other = 1 - highest;
        if (slots[other] == SC_STORE_SLOT_VALID &&
            sc_store_record_is_retired(&recs[other]) != AICAM_TRUE &&
            sc_store_pair_floor_matches_record(&floor, &recs[other]) == AICAM_TRUE) {
            selected = other;
        } else if (sc_store_pair_floor_lineage_retired(&floor, recs, slots) != AICAM_TRUE) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
    }
    if (recs[selected].state == SC_STORE_REVOKED) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        g_sidecar_boot_resolved = AICAM_TRUE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (sc_store_record_native_equal(&recs[selected], native) != AICAM_TRUE ||
        sc_store_trust_matches(trust, native) != AICAM_TRUE) {
        sc_store_record_t revoked = recs[selected];
        target = 1 - selected;
        if (revoked.generation == UINT64_MAX) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        revoked.state = SC_STORE_REVOKED;
        revoked.generation++;
        if (sc_store_slot_write(io, (uint8_t)target, &revoked) != AICAM_TRUE) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        g_sidecar_boot_resolved = AICAM_TRUE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    verify_rc = sc_store_verify_record(io, trust, native, &recs[selected], &verified);
    if (verify_rc == SC_STORE_VERIFY_IO) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (verify_rc != SC_STORE_VERIFY_OK) {
        other = 1 - selected;
        if (slots[other] != SC_STORE_SLOT_VALID ||
            recs[other].state != SC_STORE_ACTIVE ||
            sc_store_record_native_equal(&recs[other], native) != AICAM_TRUE ||
            (floor_state == SC_STORE_SLOT_VALID &&
             sc_store_pair_floor_matches_record(&floor, &recs[other]) != AICAM_TRUE) ||
            (floor_state == SC_STORE_SLOT_MISSING && recs[other].release_seq > 0u)) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        verify_rc = sc_store_verify_record(io, trust, native, &recs[other], &verified);
        if (verify_rc != SC_STORE_VERIFY_OK) {
            *out_state = (verify_rc == SC_STORE_VERIFY_IO) ?
                         APP_SIDECAR_LIFECYCLE_UNKNOWN : APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
            return (verify_rc == SC_STORE_VERIFY_IO) ?
                   APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN : APP_SIDECAR_LIFECYCLE_ERR_VERIFY;
        }
        rc = sc_store_write_generation_copy_locked(io, trust, native, &recs[other],
                                                    (uint8_t)selected, &recs[selected],
                                                    AICAM_TRUE, out);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return rc;
        }
        *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
        g_sidecar_boot_resolved = AICAM_TRUE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (selected != highest) {
        target = highest;
        rc = sc_store_write_generation_copy_locked(
            io, trust, native, &recs[selected], (uint8_t)target, &recs[target],
            AICAM_TRUE, out);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return rc;
        }
        *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
        g_sidecar_boot_resolved = AICAM_TRUE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    target = 1 - selected;
    other = target;
    rc = sc_store_write_generation_copy_locked(
        io, trust, native, &recs[selected], (uint8_t)target,
        (slots[other] == SC_STORE_SLOT_VALID) ? &recs[other] : NULL,
        (slots[other] == SC_STORE_SLOT_VALID) ? AICAM_TRUE : AICAM_FALSE, out);
    if (rc != APP_SIDECAR_LIFECYCLE_OK) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return rc;
    }
    *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
    g_sidecar_boot_resolved = AICAM_TRUE;
    return APP_SIDECAR_LIFECYCLE_OK;
}

static app_sidecar_lifecycle_result_t sc_store_active_locked(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state,
    int *out_slot)
{
    sc_store_record_t recs[2];
    int slots[2];
    int highest;
    sc_store_verify_result_t verify_rc;
    app_sidecar_descriptor_t verified;

    if (sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    highest = sc_store_highest(recs, slots);
    if (highest < 0) {
        *out_state = APP_SIDECAR_LIFECYCLE_NONE;
        *out_slot = -1;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    *out_slot = highest;
    if (recs[highest].state == SC_STORE_REVOKED ||
        sc_store_record_native_equal(&recs[highest], native) != AICAM_TRUE) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (sc_verify_cache_hit(&recs[highest], native) == AICAM_TRUE) {
        sc_store_fill_descriptor(&recs[highest], &g_verify_cache_descriptor, out);
        *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    verify_rc = sc_store_verify_record(io, trust, native, &recs[highest], &verified);
    if (verify_rc == SC_STORE_VERIFY_IO) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (verify_rc != SC_STORE_VERIFY_OK) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_SIDECAR_LIFECYCLE_ERR_VERIFY;
    }
    sc_verify_cache_store(&recs[highest], native, &verified);
    sc_store_fill_descriptor(&recs[highest], &verified, out);
    *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
    return APP_SIDECAR_LIFECYCLE_OK;
}

static aicam_bool_t sc_verify_cache_hit(const sc_store_record_t *record,
                                        const app_install_record_t *native)
{
    if (g_verify_cache_valid != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (memcmp(&g_verify_cache_record, record, sizeof(*record)) != 0 ||
        memcmp(&g_verify_cache_native, native, sizeof(*native)) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void sc_verify_cache_store(const sc_store_record_t *record,
                                  const app_install_record_t *native,
                                  const app_sidecar_descriptor_t *descriptor)
{
    g_verify_cache_record = *record;
    g_verify_cache_native = *native;
    g_verify_cache_descriptor = *descriptor;
    g_verify_cache_valid = AICAM_TRUE;
}

static aicam_bool_t sc_store_io_valid(const app_install_io_t *io)
{
    return (io != NULL && io->file_write != NULL && io->file_read != NULL &&
            io->file_remove != NULL && io->file_stat != NULL && io->kv_write != NULL &&
            io->kv_read != NULL && io->kv_delete != NULL && io->free_bytes != NULL &&
            io->tx_begin != NULL && io->tx_end != NULL) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_tx_begin_read(const app_install_io_t *io)
{
    if (io->tx_begin_read != NULL) {
        return io->tx_begin_read(io->ctx);
    }
    return io->tx_begin(io->ctx);
}

static app_sidecar_lifecycle_result_t sc_store_native_package_proven(
    const app_install_io_t *io,
    const app_install_record_t *native)
{
    uint8_t *package;
    uint8_t sha[APP_INSTALL_SHA256_LEN];
    size_t stat_len = 0u;
    size_t read_len = 0u;
    app_install_io_result_t st_rc;
    app_install_io_result_t rd_rc;

    if (native->package_len == 0u || native->package_path[0] == '\0') {
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    st_rc = io->file_stat(io->ctx, native->package_path, &stat_len);
    if (st_rc == APP_INSTALL_IO_ERROR) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (st_rc != APP_INSTALL_IO_OK || stat_len != native->package_len) {
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    package = (uint8_t *)malloc(native->package_len);
    if (package == NULL) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    rd_rc = io->file_read(io->ctx, native->package_path, package,
                          native->package_len, &read_len);
    if (rd_rc == APP_INSTALL_IO_ERROR) {
        free(package);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (rd_rc != APP_INSTALL_IO_OK || read_len != native->package_len) {
        free(package);
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    inst_sha256(package, read_len, sha);
    free(package);
    if (memcmp(sha, native->package_sha256, sizeof(sha)) != 0) {
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    return APP_SIDECAR_LIFECYCLE_OK;
}

static app_sidecar_lifecycle_result_t sc_store_identity_check_locked(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native)
{
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;

    if (sc_store_trust_matches(trust, native) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    if (inst_record_load(io, &current, &present) != AICAM_OK || present != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (sc_store_native_equal(&current, native) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    return sc_store_native_package_proven(io, &current);
}

static app_sidecar_lifecycle_result_t sc_store_revoke_locked(
    const app_install_io_t *io,
    const app_install_record_t *native)
{
    sc_store_record_t recs[2];
    int slots[2];
    int highest;
    int target;
    sc_store_stage_t pending_stage;
    int pending_state;
    sc_store_stage_t stage;
    sc_store_record_t revoked;

    if (sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    pending_state = sc_store_stage_load(io, &pending_stage);
    if (pending_state != SC_STORE_SLOT_MISSING && pending_state != SC_STORE_SLOT_VALID) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (pending_state == SC_STORE_SLOT_VALID &&
        (sc_store_candidate_cleanup(io, &pending_stage.candidate, recs, slots,
                                   pending_stage.target_slot, AICAM_TRUE) != AICAM_TRUE ||
         sc_store_stage_delete(io) != AICAM_TRUE)) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    highest = sc_store_highest(recs, slots);
    if (highest < 0) {
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (sc_store_record_native_equal(&recs[highest], native) == AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    if (recs[highest].state == SC_STORE_REVOKED &&
        recs[highest].generation == UINT64_MAX) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    target = 1 - highest;
    revoked = recs[highest];
    revoked.state = SC_STORE_REVOKED;
    revoked.generation++;
    memset(&stage, 0, sizeof(stage));
    stage.phase = SC_STORE_PHASE_COMMITTING;
    stage.target_slot = (uint8_t)target;
    stage.has_displaced = (slots[target] == SC_STORE_SLOT_VALID) ? AICAM_TRUE : AICAM_FALSE;
    stage.candidate = revoked;
    if (stage.has_displaced == AICAM_TRUE) {
        stage.displaced = recs[target];
    }
    if (sc_store_stage_write(io, &stage) != AICAM_TRUE ||
        sc_store_slot_write(io, (uint8_t)target, &revoked) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (stage.has_displaced == AICAM_TRUE &&
        sc_store_record_resources_remove(io, &stage.displaced, &revoked, AICAM_TRUE,
                                         &recs[highest], AICAM_TRUE) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (sc_store_stage_delete(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    g_sidecar_boot_resolved = AICAM_FALSE;
    return APP_SIDECAR_LIFECYCLE_OK;
}

static aicam_bool_t sc_uninstall_intent_encode(const sc_uninstall_intent_t *intent,
                                               uint8_t out[APP_INSTALL_UNINSTALL_BIN_SIZE])
{
    if (intent == NULL || out == NULL || intent->state != SC_UNINSTALL_STATE_PENDING) {
        return AICAM_FALSE;
    }
    memset(out, 0, APP_INSTALL_UNINSTALL_BIN_SIZE);
    sc_store_put_u32(out, SC_UNINSTALL_MAGIC);
    sc_store_put_u32(out + 4u, SC_UNINSTALL_VERSION);
    out[SC_UNINSTALL_STATE_OFF] = intent->state;
    if (app_install_record_encode(&intent->record, out + SC_UNINSTALL_RECORD_OFF,
                                  APP_INSTALL_RECORD_BIN_SIZE) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    sc_store_put_u32(out + SC_UNINSTALL_CRC_OFF,
                     generic_crc32(out, SC_UNINSTALL_CRC_OFF));
    return AICAM_TRUE;
}

static aicam_bool_t sc_uninstall_intent_decode(
    const uint8_t data[APP_INSTALL_UNINSTALL_BIN_SIZE],
    sc_uninstall_intent_t *out)
{
    size_t i;

    if (data == NULL || out == NULL || sc_store_get_u32(data) != SC_UNINSTALL_MAGIC ||
        sc_store_get_u32(data + 4u) != SC_UNINSTALL_VERSION ||
        data[SC_UNINSTALL_STATE_OFF] != SC_UNINSTALL_STATE_PENDING ||
        sc_store_get_u32(data + SC_UNINSTALL_CRC_OFF) !=
            generic_crc32(data, SC_UNINSTALL_CRC_OFF)) {
        return AICAM_FALSE;
    }
    for (i = 9u; i < SC_UNINSTALL_RECORD_OFF; i++) {
        if (data[i] != 0u) {
            return AICAM_FALSE;
        }
    }
    memset(out, 0, sizeof(*out));
    out->state = data[SC_UNINSTALL_STATE_OFF];
    return app_install_record_decode(data + SC_UNINSTALL_RECORD_OFF,
                                     APP_INSTALL_RECORD_BIN_SIZE,
                                     &out->record);
}

static int sc_uninstall_intent_load(const app_install_io_t *io,
                                    sc_uninstall_intent_t *intent)
{
    uint8_t data[APP_INSTALL_UNINSTALL_BIN_SIZE];
    app_install_io_result_t rc = sc_store_read_key(io, APP_INSTALL_KEY_UNINSTALL,
                                                   data, sizeof(data));

    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return SC_STORE_SLOT_MISSING;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return SC_STORE_SLOT_IO_ERROR;
    }
    if (sc_uninstall_intent_decode(data, intent) != AICAM_TRUE) {
        return SC_STORE_SLOT_CORRUPT;
    }
    return SC_STORE_SLOT_VALID;
}

static app_install_result_t inst_uninstall_intent_blocks_boot(
    const app_install_io_t *io)
{
    sc_uninstall_intent_t intent;
    int state = sc_uninstall_intent_load(io, &intent);

    return (state == SC_STORE_SLOT_MISSING) ? APP_INSTALL_OK :
           APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
}

static aicam_bool_t sc_uninstall_identity_from_record(
    const app_install_record_t *record,
    app_state_identity_t *identity)
{
    if (record == NULL || identity == NULL) {
        return AICAM_FALSE;
    }
    memset(identity, 0, sizeof(*identity));
    memcpy(identity->spki_sha256, record->publisher_key_sha256,
           APP_STATE_SHA256_LEN);
    (void)snprintf(identity->publisher_id, sizeof(identity->publisher_id), "%s",
                   record->publisher_id);
    (void)snprintf(identity->app_id, sizeof(identity->app_id), "%s", record->app_id);
    return AICAM_TRUE;
}

static app_sidecar_lifecycle_result_t sc_store_uninstall_all_locked(
    const app_install_io_t *io,
    const app_install_record_t *native)
{
    sc_store_record_t recs[2];
    int slots[2];
    sc_store_stage_t old_stage;
    sc_store_stage_t stage;
    sc_store_record_t tombstone;
    sc_store_record_t displaced;
    int stage_state;
    int highest;
    int target;
    int other;
    uint64_t generation;
    aicam_bool_t has_displaced;

    stage_state = sc_store_stage_load(io, &old_stage);
    if (stage_state != SC_STORE_SLOT_MISSING && stage_state != SC_STORE_SLOT_VALID) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (stage_state == SC_STORE_SLOT_VALID) {
        if (sc_store_candidate_cleanup(io, &old_stage.candidate, recs, slots,
                                       old_stage.target_slot, AICAM_TRUE) != AICAM_TRUE ||
            sc_store_stage_delete(io) != AICAM_TRUE) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
    }
    if (sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    highest = sc_store_highest(recs, slots);
    if (highest >= 0 && recs[highest].state == SC_STORE_REVOKED &&
        sc_store_record_native_equal(&recs[highest], native) == AICAM_TRUE &&
        recs[highest].file_count == 0u && recs[highest].bundle_len == 0u &&
        slots[1 - highest] == SC_STORE_SLOT_MISSING) {
        return APP_SIDECAR_LIFECYCLE_OK;
    }
    generation = sc_store_next_generation(recs, slots);
    if (generation == 0u) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    target = (highest < 0) ? 0 : 1 - highest;
    other = 1 - target;
    has_displaced = (slots[target] == SC_STORE_SLOT_VALID) ? AICAM_TRUE : AICAM_FALSE;
    if (has_displaced == AICAM_TRUE) {
        displaced = recs[target];
    } else {
        memset(&displaced, 0, sizeof(displaced));
    }
    sc_store_record_from_native(&tombstone, native);
    tombstone.state = SC_STORE_REVOKED;
    tombstone.generation = generation;
    if (highest >= 0 && recs[highest].release_seq > 0u) {
        (void)snprintf(tombstone.publisher_id, sizeof(tombstone.publisher_id), "%s",
                       recs[highest].publisher_id);
        (void)snprintf(tombstone.app_id, sizeof(tombstone.app_id), "%s",
                       recs[highest].app_id);
        memcpy(tombstone.publisher_key_sha256, recs[highest].publisher_key_sha256,
               APP_SIDECAR_SHA256_LEN);
        memcpy(tombstone.native_tbs_sha256, recs[highest].native_tbs_sha256,
               APP_SIDECAR_SHA256_LEN);
        memcpy(tombstone.ui_tbs_sha256, recs[highest].ui_tbs_sha256,
               APP_SIDECAR_SHA256_LEN);
        memcpy(tombstone.pair_tbs_sha256, recs[highest].pair_tbs_sha256,
               APP_PAIR_TBS_SHA256_LEN);
        memcpy(tombstone.native_version, recs[highest].native_version,
               sizeof(tombstone.native_version));
        tombstone.version_major = recs[highest].version_major;
        tombstone.version_minor = recs[highest].version_minor;
        tombstone.version_patch = recs[highest].version_patch;
        tombstone.release_seq = recs[highest].release_seq;
    } else {
        memset(tombstone.ui_tbs_sha256, 0, sizeof(tombstone.ui_tbs_sha256));
        memset(tombstone.pair_tbs_sha256, 0, sizeof(tombstone.pair_tbs_sha256));
        tombstone.version_major = 0u;
        tombstone.version_minor = 0u;
        tombstone.version_patch = 0u;
        tombstone.release_seq = 0u;
    }
    tombstone.host_ui_abi = 0u;
    tombstone.file_count = 0u;
    tombstone.manifest_len = 0u;
    tombstone.asset_payload_len = 0u;
    tombstone.bundle_len = 0u;
    memset(tombstone.bundle_sha256, 0, sizeof(tombstone.bundle_sha256));
    memset(&stage, 0, sizeof(stage));
    stage.phase = SC_STORE_PHASE_COMMITTING;
    stage.target_slot = (uint8_t)target;
    stage.has_displaced = has_displaced;
    stage.candidate = tombstone;
    stage.displaced = displaced;
    if (sc_store_stage_write(io, &stage) != AICAM_TRUE ||
        sc_store_slot_write(io, (uint8_t)target, &tombstone) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (has_displaced == AICAM_TRUE &&
        sc_store_record_resources_remove(io, &displaced, &tombstone, AICAM_TRUE,
                                         (slots[other] == SC_STORE_SLOT_VALID) ?
                                             &recs[other] : NULL,
                                         (slots[other] == SC_STORE_SLOT_VALID) ?
                                             AICAM_TRUE : AICAM_FALSE) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (slots[other] == SC_STORE_SLOT_VALID &&
        sc_store_record_resources_remove(io, &recs[other], &tombstone, AICAM_TRUE,
                                         NULL, AICAM_FALSE) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (slots[other] != SC_STORE_SLOT_MISSING &&
        sc_store_kv_delete_proven(io, sc_store_slot_key((uint8_t)other),
                             APP_SIDECAR_LIFECYCLE_RECORD_SIZE) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (sc_store_stage_delete(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (has_displaced == AICAM_TRUE &&
        sc_store_record_is_retired(&displaced) == AICAM_TRUE &&
        sc_store_native_is_unsigned(native) == AICAM_TRUE) {
        sc_store_pair_floor_t displaced_floor;
        int displaced_floor_state = sc_store_pair_floor_load(io, &displaced_floor);
        if (displaced_floor_state == SC_STORE_SLOT_IO_ERROR ||
            displaced_floor_state == SC_STORE_SLOT_CORRUPT ||
            (displaced_floor_state == SC_STORE_SLOT_VALID &&
             sc_store_pair_floor_matches_record(&displaced_floor, &displaced) ==
                 AICAM_TRUE &&
             sc_store_kv_delete_proven(io, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                                    SC_STORE_PAIR_FLOOR_SIZE) !=
                 AICAM_TRUE)) {
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
    }
    g_sidecar_boot_resolved = AICAM_FALSE;
    return APP_SIDECAR_LIFECYCLE_OK;
}

static aicam_bool_t sc_uninstall_local_cleanup_locked(
    const app_install_io_t *io,
    const app_install_record_t *record)
{
    app_state_identity_t identity;
    app_sidecar_lifecycle_result_t sidecar_rc;

    if (inst_record_delete_proven(io) != AICAM_TRUE ||
        inst_kv_delete_proven(io, APP_INSTALL_KEY_PENDING,
                             APP_INSTALL_PENDING_BLOB_SIZE) != AICAM_TRUE ||
        sc_store_file_remove_proven(io, record->package_path) != AICAM_TRUE ||
        sc_uninstall_identity_from_record(record, &identity) != AICAM_TRUE ||
        app_state_store_purge_locked(io, &identity) != APP_STATE_OK) {
        return AICAM_FALSE;
    }
    sidecar_rc = sc_store_uninstall_all_locked(io, record);
    return (sidecar_rc == APP_SIDECAR_LIFECYCLE_OK) ? AICAM_TRUE : AICAM_FALSE;
}

typedef struct {
    uint32_t state;
    uint32_t major;
    uint64_t generation;
    uint32_t session_generation;
    uint32_t boot_id;
    uint32_t request_id;
    uint32_t config_generation;
    uint32_t state_revision;
    char publisher_id[APP_INSTALL_PUBLISHER_SLOT + 1u];
    char app_id[APP_INSTALL_APP_SLOT + 1u];
    uint8_t key_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t tbs_sha256[APP_INSTALL_SHA256_LEN];
    uint8_t package_sha256[APP_INSTALL_SHA256_LEN];
} sc_vnext_floor_t;

static int sc_vnext_floor_load(const app_install_io_t *io, sc_vnext_floor_t *floor)
{
    uint8_t data[SC_VNEXT_FLOOR_SIZE];
    size_t got = 0u;
    app_install_io_result_t rc;

    if (io == NULL || io->kv_read == NULL || floor == NULL) {
        return SC_STORE_SLOT_IO_ERROR;
    }
    rc = io->kv_read(io->ctx, APP_NATIVE_VNEXT_FLOOR_KEY, data, sizeof(data), &got);
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return SC_STORE_SLOT_MISSING;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return SC_STORE_SLOT_IO_ERROR;
    }
    if (got != sizeof(data) || sc_store_get_u32(data) != SC_VNEXT_FLOOR_MAGIC ||
        sc_store_get_u32(data + 4u) != SC_VNEXT_FLOOR_VERSION ||
        sc_store_get_u32(data + SC_VNEXT_FLOOR_CRC_OFF) !=
            generic_crc32(data, SC_VNEXT_FLOOR_CRC_OFF) ||
        (sc_store_get_u32(data + SC_VNEXT_FLOOR_STATE_OFF) != SC_VNEXT_FLOOR_ACTIVE &&
         sc_store_get_u32(data + SC_VNEXT_FLOOR_STATE_OFF) != SC_VNEXT_FLOOR_RETIRE_READY) ||
        sc_store_get_u32(data + SC_VNEXT_FLOOR_MAJOR_OFF) < APP_NATIVE_VNEXT_MAJOR ||
        sc_store_get_u64(data + SC_VNEXT_FLOOR_GENERATION_OFF) == 0u ||
        sc_store_text_valid(data + SC_VNEXT_FLOOR_PUB_OFF,
                            APP_INSTALL_PUBLISHER_SLOT) != AICAM_TRUE ||
        sc_store_text_valid(data + SC_VNEXT_FLOOR_APP_OFF,
                            APP_INSTALL_APP_SLOT) != AICAM_TRUE) {
        return SC_STORE_SLOT_CORRUPT;
    }
    memset(floor, 0, sizeof(*floor));
    floor->state = sc_store_get_u32(data + SC_VNEXT_FLOOR_STATE_OFF);
    floor->major = sc_store_get_u32(data + SC_VNEXT_FLOOR_MAJOR_OFF);
    floor->generation = sc_store_get_u64(data + SC_VNEXT_FLOOR_GENERATION_OFF);
    floor->session_generation = sc_store_get_u32(data + SC_VNEXT_FLOOR_SESSION_OFF);
    floor->boot_id = sc_store_get_u32(data + SC_VNEXT_FLOOR_BOOT_OFF);
    floor->request_id = sc_store_get_u32(data + SC_VNEXT_FLOOR_REQUEST_OFF);
    floor->config_generation = sc_store_get_u32(data + SC_VNEXT_FLOOR_CONFIG_OFF);
    floor->state_revision = sc_store_get_u32(data + SC_VNEXT_FLOOR_STATE_REV_OFF);
    memcpy(floor->publisher_id, data + SC_VNEXT_FLOOR_PUB_OFF,
           APP_INSTALL_PUBLISHER_SLOT);
    memcpy(floor->app_id, data + SC_VNEXT_FLOOR_APP_OFF,
           APP_INSTALL_APP_SLOT);
    memcpy(floor->key_sha256, data + SC_VNEXT_FLOOR_KEY_OFF,
           APP_INSTALL_SHA256_LEN);
    memcpy(floor->tbs_sha256, data + SC_VNEXT_FLOOR_TBS_OFF,
           APP_INSTALL_SHA256_LEN);
    memcpy(floor->package_sha256, data + SC_VNEXT_FLOOR_PACKAGE_OFF,
           APP_INSTALL_SHA256_LEN);
    return SC_STORE_SLOT_VALID;
}

static aicam_bool_t sc_vnext_floor_encode(const sc_vnext_floor_t *floor,
                                          uint8_t data[SC_VNEXT_FLOOR_SIZE])
{
    if (floor == NULL || data == NULL || floor->generation == 0u ||
        floor->major < APP_NATIVE_VNEXT_MAJOR ||
        (floor->state != SC_VNEXT_FLOOR_ACTIVE &&
         floor->state != SC_VNEXT_FLOOR_RETIRE_READY)) {
        return AICAM_FALSE;
    }
    memset(data, 0, SC_VNEXT_FLOOR_SIZE);
    sc_store_put_u32(data, SC_VNEXT_FLOOR_MAGIC);
    sc_store_put_u32(data + 4u, SC_VNEXT_FLOOR_VERSION);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_STATE_OFF, floor->state);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_MAJOR_OFF, floor->major);
    sc_store_put_u64(data + SC_VNEXT_FLOOR_GENERATION_OFF, floor->generation);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_SESSION_OFF, floor->session_generation);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_BOOT_OFF, floor->boot_id);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_REQUEST_OFF, floor->request_id);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_CONFIG_OFF, floor->config_generation);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_STATE_REV_OFF, floor->state_revision);
    sc_store_copy_padded(data + SC_VNEXT_FLOOR_PUB_OFF,
                         floor->publisher_id, APP_INSTALL_PUBLISHER_SLOT);
    sc_store_copy_padded(data + SC_VNEXT_FLOOR_APP_OFF,
                         floor->app_id, APP_INSTALL_APP_SLOT);
    memcpy(data + SC_VNEXT_FLOOR_KEY_OFF, floor->key_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(data + SC_VNEXT_FLOOR_TBS_OFF, floor->tbs_sha256,
           APP_INSTALL_SHA256_LEN);
    memcpy(data + SC_VNEXT_FLOOR_PACKAGE_OFF, floor->package_sha256,
           APP_INSTALL_SHA256_LEN);
    sc_store_put_u32(data + SC_VNEXT_FLOOR_CRC_OFF,
                     generic_crc32(data, SC_VNEXT_FLOOR_CRC_OFF));
    return AICAM_TRUE;
}

static aicam_bool_t sc_vnext_floor_owner_equal(const sc_vnext_floor_t *floor,
                                               const app_install_record_t *record)
{
    return (strcmp(floor->publisher_id, record->publisher_id) == 0 &&
            strcmp(floor->app_id, record->app_id) == 0)
               ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t sc_vnext_floor_is_unsigned(const sc_vnext_floor_t *floor)
{
    static const uint8_t zero[APP_INSTALL_SHA256_LEN] = {0};

    return (floor != NULL &&
            strcmp(floor->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0 &&
            memcmp(floor->key_sha256, zero, sizeof(zero)) == 0)
               ? AICAM_TRUE : AICAM_FALSE;
}

static app_install_result_t sc_vnext_floor_write(const app_install_io_t *io,
                                                 const sc_vnext_floor_t *floor)
{
    uint8_t data[SC_VNEXT_FLOOR_SIZE];
    if (io == NULL || io->kv_read == NULL || io->kv_write == NULL ||
        sc_vnext_floor_encode(floor, data) != AICAM_TRUE ||
        sc_store_kv_write_proven(io, APP_NATIVE_VNEXT_FLOOR_KEY,
                                 data, sizeof(data)) != AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    return APP_INSTALL_OK;
}

app_install_result_t app_native_vnext_floor_epoch_advance(
    const app_install_io_t *io, uint32_t *out_epoch)
{
    uint8_t data[SC_VNEXT_EPOCH_SIZE];
    uint8_t verify[SC_VNEXT_EPOCH_SIZE];
    size_t got = 0u;
    uint32_t epoch = 0u;
    app_install_io_result_t rc;

    if (out_epoch == NULL || io == NULL || io->kv_read == NULL ||
        io->kv_write == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    rc = io->kv_read(io->ctx, APP_NATIVE_VNEXT_EPOCH_KEY, data, sizeof(data), &got);
    if (rc == APP_INSTALL_IO_OK) {
        if (got != sizeof(data) || sc_store_get_u32(data) != SC_VNEXT_EPOCH_MAGIC ||
            sc_store_get_u32(data + 4u) != SC_VNEXT_EPOCH_VERSION ||
            sc_store_get_u32(data + 12u) != generic_crc32(data, 12u)) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
        }
        epoch = sc_store_get_u32(data + 8u);
    } else if (rc != APP_INSTALL_IO_NOT_FOUND) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (epoch == UINT32_MAX) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    epoch++;
    memset(data, 0, sizeof(data));
    sc_store_put_u32(data, SC_VNEXT_EPOCH_MAGIC);
    sc_store_put_u32(data + 4u, SC_VNEXT_EPOCH_VERSION);
    sc_store_put_u32(data + 8u, epoch);
    sc_store_put_u32(data + 12u, generic_crc32(data, 12u));
    if (io->kv_write(io->ctx, APP_NATIVE_VNEXT_EPOCH_KEY,
                     data, sizeof(data)) != APP_INSTALL_IO_OK) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    got = 0u;
    rc = io->kv_read(io->ctx, APP_NATIVE_VNEXT_EPOCH_KEY,
                     verify, sizeof(verify), &got);
    io->tx_end(io->ctx);
    if (rc != APP_INSTALL_IO_OK || got != sizeof(verify) ||
        memcmp(data, verify, sizeof(data)) != 0) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    out_epoch[0] = epoch;
    return APP_INSTALL_OK;
}

app_install_result_t app_native_vnext_floor_validate(const app_install_io_t *io)
{
    sc_vnext_floor_t floor;
    int state = sc_vnext_floor_load(io, &floor);
    return (state == SC_STORE_SLOT_MISSING || state == SC_STORE_SLOT_VALID)
               ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
}

app_install_result_t app_native_vnext_floor_check(const app_install_io_t *io,
                                                   const app_install_record_t *record,
                                                   uint32_t container_major)
{
    sc_vnext_floor_t floor;
    int state;
    if (record == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    state = sc_vnext_floor_load(io, &floor);
    if (state == SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_OK;
    }
    if (state != SC_STORE_SLOT_VALID || floor.state != SC_VNEXT_FLOOR_ACTIVE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (container_major == 5u &&
        (sc_vnext_floor_is_unsigned(&floor) != AICAM_TRUE ||
         strcmp(record->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0 ||
         memcmp(record->publisher_key_sha256,
                (const uint8_t[APP_INSTALL_SHA256_LEN]){0},
                APP_INSTALL_SHA256_LEN) != 0)) {
        return APP_INSTALL_ERR_LEGACY_STATE;
    }
    if (sc_vnext_floor_owner_equal(&floor, record) != AICAM_TRUE) {
        return APP_INSTALL_ERR_IDENTITY_CONFLICT;
    }
    if (container_major != 0u && container_major < floor.major) {
        return APP_INSTALL_ERR_DOWNGRADE_REFUSED;
    }
    return APP_INSTALL_OK;
}

app_install_result_t app_native_vnext_floor_raise(const app_install_io_t *io,
                                                   const app_install_record_t *record,
                                                   uint32_t session_generation,
                                                   uint32_t boot_id,
                                                   uint32_t request_id,
                                                   uint32_t config_generation,
                                                   uint32_t expected_state_revision)
{
    sc_vnext_floor_t floor;
    app_install_record_t committed;
    aicam_bool_t present = AICAM_FALSE;
    int state;
    if (io == NULL || record == NULL || io->kv_read == NULL ||
        io->kv_write == NULL || io->tx_begin == NULL || io->tx_end == NULL ||
        session_generation == 0u || boot_id == 0u || request_id == 0u ||
        config_generation == 0u || app_install_app_running() != AICAM_TRUE ||
        io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (inst_record_load(io, &committed, &present) != AICAM_OK ||
        present != AICAM_TRUE ||
        sc_store_native_equal(&committed, record) != AICAM_TRUE ||
        memcmp(committed.package_sha256, record->package_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    state = sc_vnext_floor_load(io, &floor);
    if (state != SC_STORE_SLOT_MISSING && state != SC_STORE_SLOT_VALID) {
        io->tx_end(io->ctx);
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (state == SC_STORE_SLOT_VALID) {
        if (floor.state != SC_VNEXT_FLOOR_ACTIVE ||
            sc_vnext_floor_owner_equal(&floor, record) != AICAM_TRUE ||
            floor.generation == UINT64_MAX) {
            io->tx_end(io->ctx);
            return APP_INSTALL_ERR_IDENTITY_CONFLICT;
        }
        if (floor.session_generation == session_generation &&
            floor.boot_id == boot_id && floor.request_id == request_id) {
            aicam_bool_t same_evidence =
                floor.config_generation == config_generation &&
                floor.state_revision == expected_state_revision &&
                memcmp(floor.key_sha256, record->publisher_key_sha256,
                       APP_INSTALL_SHA256_LEN) == 0 &&
                memcmp(floor.tbs_sha256, record->tbs_sha256,
                       APP_INSTALL_SHA256_LEN) == 0 &&
                memcmp(floor.package_sha256, record->package_sha256,
                       APP_INSTALL_SHA256_LEN) == 0;
            io->tx_end(io->ctx);
            return (same_evidence == AICAM_TRUE) ? APP_INSTALL_OK :
                                                   APP_INSTALL_ERR_IDENTITY_CONFLICT;
        }
        floor.generation++;
        if (floor.major < APP_NATIVE_VNEXT_MAJOR) {
            floor.major = APP_NATIVE_VNEXT_MAJOR;
        }
    } else {
        memset(&floor, 0, sizeof(floor));
        floor.generation = 1u;
        floor.major = APP_NATIVE_VNEXT_MAJOR;
        memcpy(floor.publisher_id, record->publisher_id, sizeof(floor.publisher_id));
        memcpy(floor.app_id, record->app_id, sizeof(floor.app_id));
    }
    floor.state = SC_VNEXT_FLOOR_ACTIVE;
    floor.session_generation = session_generation;
    floor.boot_id = boot_id;
    floor.request_id = request_id;
    floor.config_generation = config_generation;
    floor.state_revision = expected_state_revision;
    memcpy(floor.key_sha256, record->publisher_key_sha256, APP_INSTALL_SHA256_LEN);
    memcpy(floor.tbs_sha256, record->tbs_sha256, APP_INSTALL_SHA256_LEN);
    memcpy(floor.package_sha256, record->package_sha256, APP_INSTALL_SHA256_LEN);
    {
        app_install_result_t rc = sc_vnext_floor_write(io, &floor);
        io->tx_end(io->ctx);
        return rc;
    }
}

app_install_result_t app_native_vnext_floor_verify(
    const app_install_io_t *io,
    const app_install_record_t *record,
    uint32_t session_generation,
    uint32_t boot_id,
    uint32_t request_id,
    uint32_t config_generation,
    uint32_t expected_state_revision)
{
    sc_vnext_floor_t floor;
    int state;

    if (io == NULL || io->kv_read == NULL) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (record == NULL || session_generation == 0u || boot_id == 0u ||
        request_id == 0u || config_generation == 0u) {
        return APP_INSTALL_ERR_PARAM;
    }
    state = sc_vnext_floor_load(io, &floor);
    if (state != SC_STORE_SLOT_VALID || floor.state != SC_VNEXT_FLOOR_ACTIVE ||
        floor.major < APP_NATIVE_VNEXT_MAJOR ||
        floor.session_generation != session_generation || floor.boot_id != boot_id ||
        floor.request_id != request_id || floor.config_generation != config_generation ||
        floor.state_revision != expected_state_revision ||
        sc_vnext_floor_owner_equal(&floor, record) != AICAM_TRUE ||
        memcmp(floor.key_sha256, record->publisher_key_sha256,
               APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(floor.tbs_sha256, record->tbs_sha256,
               APP_INSTALL_SHA256_LEN) != 0 ||
        memcmp(floor.package_sha256, record->package_sha256,
               APP_INSTALL_SHA256_LEN) != 0) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    return APP_INSTALL_OK;
}

app_install_result_t app_native_vnext_floor_retire_prepare(
    const app_install_io_t *io,
    const app_install_record_t *record)
{
    sc_vnext_floor_t floor;
    if (io == NULL || io->kv_read == NULL || io->kv_write == NULL || record == NULL) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    int state = sc_vnext_floor_load(io, &floor);
    if (state == SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_OK;
    }
    if (state != SC_STORE_SLOT_VALID || floor.state != SC_VNEXT_FLOOR_ACTIVE ||
        record == NULL || sc_vnext_floor_owner_equal(&floor, record) != AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    floor.state = SC_VNEXT_FLOOR_RETIRE_READY;
    memcpy(floor.package_sha256, record->package_sha256, APP_INSTALL_SHA256_LEN);
    return sc_vnext_floor_write(io, &floor);
}

app_install_result_t app_native_vnext_floor_retire_finish(
    const app_install_io_t *io,
    const app_install_record_t *record)
{
    sc_vnext_floor_t floor;
    sc_uninstall_intent_t intent;
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;
    aicam_result_t record_rc;
    if (io == NULL || io->kv_read == NULL || io->kv_delete == NULL ||
        record == NULL) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    int state = sc_vnext_floor_load(io, &floor);
    if (state == SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_OK;
    }
    if (state != SC_STORE_SLOT_VALID || floor.state != SC_VNEXT_FLOOR_RETIRE_READY ||
        record == NULL || sc_vnext_floor_owner_equal(&floor, record) != AICAM_TRUE ||
        memcmp(floor.package_sha256, record->package_sha256,
               APP_INSTALL_SHA256_LEN) != 0 ||
        sc_uninstall_intent_load(io, &intent) != SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    record_rc = inst_record_load(io, &current, &present);
    if (record_rc != AICAM_OK || present == AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    return sc_store_kv_delete_proven(io, APP_NATIVE_VNEXT_FLOOR_KEY, SC_VNEXT_FLOOR_SIZE) == AICAM_TRUE
               ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
}

app_install_result_t app_native_vnext_floor_recover(const app_install_io_t *io)
{
    sc_vnext_floor_t floor;
    sc_uninstall_intent_t intent;
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;
    aicam_result_t record_rc;
    if (io == NULL || io->kv_read == NULL || io->kv_delete == NULL) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    int state = sc_vnext_floor_load(io, &floor);
    if (state == SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_OK;
    }
    if (state != SC_STORE_SLOT_VALID) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    if (floor.state == SC_VNEXT_FLOOR_ACTIVE) {
        return APP_INSTALL_OK;
    }
    if (sc_uninstall_intent_load(io, &intent) != SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    record_rc = inst_record_load(io, &current, &present);
    if (record_rc != AICAM_OK || present == AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    return sc_store_kv_delete_proven(io, APP_NATIVE_VNEXT_FLOOR_KEY, SC_VNEXT_FLOOR_SIZE) == AICAM_TRUE
               ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
}

static app_uninstall_purge_result_t sc_uninstall_device_purge_unknown(
    void *ctx,
    const app_install_record_t *record)
{
    (void)ctx;
    (void)record;
    return APP_UNINSTALL_PURGE_UNKNOWN;
}

const app_uninstall_cleanup_ops_t *app_installer_device_cleanup_ops(void)
{
    static const app_uninstall_cleanup_ops_t ops = {
        NULL,
        sc_uninstall_device_purge_unknown
    };
    return &ops;
}

app_install_result_t app_install_uninstall_status(
    const app_install_io_t *io,
    app_uninstall_state_t *out_state,
    app_install_record_t *out_record)
{
    sc_uninstall_intent_t intent;
    int state;

    if (out_state != NULL) {
        *out_state = APP_UNINSTALL_STATE_NONE;
    }
    if (out_record != NULL) {
        memset(out_record, 0, sizeof(*out_record));
    }
    if (sc_store_io_valid(io) != AICAM_TRUE || out_state == NULL) {
        return APP_INSTALL_ERR_PARAM;
    }
    if (sc_tx_begin_read(io) != AICAM_TRUE) {
        return APP_INSTALL_ERR_BUSY;
    }
    state = sc_uninstall_intent_load(io, &intent);
    io->tx_end(io->ctx);
    if (state == SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_OK;
    }
    if (state != SC_STORE_SLOT_VALID) {
        *out_state = APP_UNINSTALL_STATE_UNKNOWN;
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    *out_state = APP_UNINSTALL_STATE_CLEANUP_PENDING;
    if (out_record != NULL) {
        memcpy(out_record, &intent.record, sizeof(*out_record));
    }
    return APP_INSTALL_OK;
}

static app_install_result_t sc_uninstall_pair_floor_finalize(
    const app_install_io_t *io,
    const app_install_record_t *record)
{
    sc_store_record_t recs[2];
    sc_store_pair_floor_t floor;
    int slots[2];
    int i;
    int state;
    aicam_bool_t referenced = AICAM_FALSE;
    aicam_bool_t eligible = AICAM_FALSE;

    state = sc_store_pair_floor_load(io, &floor);
    if (state == SC_STORE_SLOT_MISSING) {
        return APP_INSTALL_OK;
    }
    if (state != SC_STORE_SLOT_VALID || record == NULL ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE) {
        return APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
    }
    for (i = 0; i < 2; i++) {
        if (slots[i] != SC_STORE_SLOT_VALID ||
            sc_store_record_native_equal(&recs[i], record) != AICAM_TRUE) {
            continue;
        }
        if (sc_store_pair_floor_matches_record(&floor, &recs[i]) != AICAM_TRUE) {
            return APP_INSTALL_OK;
        }
        eligible = AICAM_TRUE;
    }
    if (eligible != AICAM_TRUE) {
        return APP_INSTALL_OK;
    }
    for (i = 0; i < 2; i++) {
        if (slots[i] == SC_STORE_SLOT_VALID &&
            sc_store_pair_floor_matches_record(&floor, &recs[i]) == AICAM_TRUE) {
            referenced = AICAM_TRUE;
        }
    }
    if (referenced == AICAM_TRUE) {
        return APP_INSTALL_OK;
    }
    return sc_store_kv_delete_proven(io, APP_SIDECAR_LIFECYCLE_KEY_PAIR_FLOOR,
                                    SC_STORE_PAIR_FLOOR_SIZE) ==
           AICAM_TRUE ? APP_INSTALL_OK : APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
}

app_uninstall_result_t app_install_uninstall_confirmed(
    const app_install_io_t *io,
    aicam_bool_t confirmed,
    const app_uninstall_cleanup_ops_t *cleanup_ops,
    app_install_record_t *out_removed,
    app_uninstall_state_t *out_state)
{
    const app_uninstall_cleanup_ops_t *ops = cleanup_ops;
    sc_uninstall_intent_t intent;
    app_install_record_t record;
    app_install_record_t current;
    aicam_bool_t present = AICAM_FALSE;
    uint8_t encoded[APP_INSTALL_UNINSTALL_BIN_SIZE];
    int intent_state;
    app_install_result_t load_rc;
    aicam_result_t record_rc;

    if (out_state != NULL) {
        *out_state = APP_UNINSTALL_STATE_NONE;
    }
    if (confirmed != AICAM_TRUE) {
        return APP_UNINSTALL_CANCELLED;
    }
    if (sc_store_io_valid(io) != AICAM_TRUE || out_removed == NULL ||
        out_state == NULL) {
        return APP_UNINSTALL_ERR_PARAM;
    }
    if (ops == NULL) {
        ops = app_installer_device_cleanup_ops();
    }
    if (ops->purge == NULL) {
        return APP_UNINSTALL_ERR_PARAM;
    }
    if (app_install_app_running() == AICAM_TRUE) {
        return APP_UNINSTALL_ERR_BUSY;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_UNINSTALL_ERR_BUSY;
    }
    if (app_install_app_running() == AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_UNINSTALL_ERR_BUSY;
    }
    intent_state = sc_uninstall_intent_load(io, &intent);
    if (intent_state == SC_STORE_SLOT_IO_ERROR || intent_state == SC_STORE_SLOT_CORRUPT) {
        io->tx_end(io->ctx);
        *out_state = APP_UNINSTALL_STATE_UNKNOWN;
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    if (intent_state == SC_STORE_SLOT_MISSING) {
        if (g_boot_resolved != AICAM_TRUE) {
            load_rc = app_install_boot_resolve(io);
            if (load_rc != APP_INSTALL_OK) {
                io->tx_end(io->ctx);
                *out_state = APP_UNINSTALL_STATE_UNKNOWN;
                return APP_UNINSTALL_CLEANUP_PENDING;
            }
        }
        record_rc = inst_record_load(io, &current, &present);
        if (record_rc != AICAM_OK) {
            io->tx_end(io->ctx);
            *out_state = APP_UNINSTALL_STATE_UNKNOWN;
            return APP_UNINSTALL_CLEANUP_PENDING;
        }
        if (present != AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_UNINSTALL_ERR_NOT_FOUND;
        }
        memset(&intent, 0, sizeof(intent));
        intent.state = SC_UNINSTALL_STATE_PENDING;
        memcpy(&intent.record, &current, sizeof(current));
        if (sc_uninstall_intent_encode(&intent, encoded) != AICAM_TRUE ||
            sc_store_kv_write_proven(io, APP_INSTALL_KEY_UNINSTALL,
                                     encoded, sizeof(encoded)) != AICAM_TRUE) {
            io->tx_end(io->ctx);
            *out_state = APP_UNINSTALL_STATE_UNKNOWN;
            return APP_UNINSTALL_CLEANUP_PENDING;
        }
    }
    memcpy(&record, &intent.record, sizeof(record));
    memcpy(out_removed, &record, sizeof(*out_removed));
    *out_state = APP_UNINSTALL_STATE_CLEANUP_PENDING;
    if (sc_uninstall_local_cleanup_locked(io, &record) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    if (sc_uninstall_pair_floor_finalize(io, &record) != APP_INSTALL_OK) {
        io->tx_end(io->ctx);
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    io->tx_end(io->ctx);

    if (ops->purge(ops->ctx, &record) != APP_UNINSTALL_PURGE_CONFIRMED) {
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    if (app_install_app_running() == AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_UNINSTALL_STATE_CLEANUP_PENDING;
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    intent_state = sc_uninstall_intent_load(io, &intent);
    if (intent_state != SC_STORE_SLOT_VALID ||
        sc_store_native_equal(&intent.record, &record) != AICAM_TRUE ||
        sc_uninstall_local_cleanup_locked(io, &record) != AICAM_TRUE ||
        app_native_vnext_floor_retire_prepare(io, &record) != APP_INSTALL_OK ||
        sc_store_kv_delete_proven(io, APP_INSTALL_KEY_UNINSTALL,
                             APP_INSTALL_UNINSTALL_BIN_SIZE) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        (*out_state) = APP_UNINSTALL_STATE_CLEANUP_PENDING;
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    if (app_native_vnext_floor_retire_finish(io, &record) != APP_INSTALL_OK) {
        io->tx_end(io->ctx);
        *out_state = APP_UNINSTALL_STATE_CLEANUP_PENDING;
        return APP_UNINSTALL_CLEANUP_PENDING;
    }
    io->tx_end(io->ctx);
    g_state_unknown = AICAM_FALSE;
    g_boot_resolved = AICAM_FALSE;
    app_sidecar_lifecycle_reset();
    *out_state = APP_UNINSTALL_STATE_NONE;
    return APP_UNINSTALL_OK;
}

static app_install_result_t inst_sidecar_revoke_for_native(
    const app_install_io_t *io,
    const app_install_record_t *native)
{
    app_sidecar_lifecycle_result_t rc = sc_store_revoke_locked(io, native);

    return (rc == APP_SIDECAR_LIFECYCLE_OK) ? APP_INSTALL_OK :
           APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN;
}

static app_sidecar_lifecycle_result_t sc_sidecar_lifecycle_install_internal(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    const uint8_t *bundle,
    size_t bundle_len,
    const app_pair_history_t *pair_history,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state)
{
    uint32_t manifest_len;
    uint32_t payload_len;
    uint64_t free_bytes = 0u;
    uint64_t candidate_bytes;
    uint64_t required_bytes;
    uint8_t bundle_sha[APP_SIDECAR_SHA256_LEN];
    char bundle_path[APP_SIDECAR_LIFECYCLE_ASSET_PATH_MAX];
    uint8_t *stored_bundle = NULL;
    size_t read_len = 0u;
    sc_store_file_set_t files;
    app_sidecar_verdict_t verdict;
    app_sidecar_result_t verify_rc;
    sc_store_record_t recs[2];
    int slots[2];
    int active_slot = -1;
    int highest;
    int target;
    sc_store_stage_t stage;
    sc_store_record_t candidate;
    app_sidecar_descriptor_t active;
    app_sidecar_lifecycle_state_t active_state;
    app_sidecar_lifecycle_result_t rc;
    aicam_bool_t current_active = AICAM_FALSE;
    aicam_bool_t cleanup_ok;
    sc_store_verify_result_t content_rc;
    app_sidecar_descriptor_t verified;

    if (out_state != NULL) {
        *out_state = APP_SIDECAR_LIFECYCLE_NONE;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (sc_store_io_valid(io) != AICAM_TRUE || trust == NULL || native_record == NULL ||
        bundle == NULL || out == NULL || out_state == NULL) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    if (bundle_len < APP_SIDECAR_HEADER_SIZE + APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE ||
        bundle_len > APP_SIDECAR_LIFECYCLE_BUNDLE_MAX ||
        (sc_store_native_is_unsigned(native_record) == AICAM_TRUE
             ? memcmp(bundle, "NEUI\002\000\000\000", APP_SIDECAR_MAGIC_SIZE) != 0
             : memcmp(bundle, "NEUI\001\000\000\000", APP_SIDECAR_MAGIC_SIZE) != 0)) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_SIDECAR_LIFECYCLE_ERR_VERIFY;
    }
    manifest_len = sc_store_get_u32(bundle + 8u);
    payload_len = sc_store_get_u32(bundle + 12u);
    if (manifest_len < (sc_store_native_is_unsigned(native_record) == AICAM_TRUE
                            ? APP_SIDECAR_UNSIGNED_MANIFEST_FIXED_SIZE
                            : APP_SIDECAR_MANIFEST_FIXED_SIZE) ||
        manifest_len > APP_SIDECAR_MANIFEST_LEN_MAX || payload_len == 0u ||
        payload_len > APP_SIDECAR_ASSET_PAYLOAD_MAX ||
        (sc_store_native_is_unsigned(native_record) == AICAM_TRUE
             ? bundle_len != APP_SIDECAR_HEADER_SIZE + (size_t)manifest_len + payload_len
             : (bundle_len < APP_SIDECAR_HEADER_SIZE + (size_t)manifest_len + payload_len +
                                    APP_SIDECAR_DER_LEN_MIN ||
                bundle_len > APP_SIDECAR_HEADER_SIZE + (size_t)manifest_len + payload_len +
                                    APP_SIDECAR_DER_LEN_MAX))) {
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_SIDECAR_LIFECYCLE_ERR_VERIFY;
    }
    if (app_install_app_running() == AICAM_TRUE || io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_BUSY;
    }
    if (sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) !=
        SC_STORE_SLOT_MISSING) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    rc = sc_store_identity_check_locked(io, trust, native_record);
    if (rc != APP_SIDECAR_LIFECYCLE_OK) {
        io->tx_end(io->ctx);
        *out_state = (rc == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED) ?
                     APP_SIDECAR_LIFECYCLE_UNAVAILABLE : APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return rc;
    }
    if (g_sidecar_boot_resolved != AICAM_TRUE) {
        rc = sc_store_boot_resolve_locked(io, trust, native_record, &active, &active_state);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            io->tx_end(io->ctx);
            *out_state = active_state;
            return rc;
        }
    } else {
        rc = sc_store_stage_recover_locked(io, trust, native_record);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return rc;
        }
    }
    if (sc_store_stage_load(io, &stage) != SC_STORE_SLOT_MISSING ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    highest = sc_store_highest(recs, slots);
    if (pair_history != NULL) {
        if (sc_store_pair_matches_native(pair_history, native_record) != AICAM_TRUE) {
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
            return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
        }
        rc = sc_store_pair_floor_check_locked(io, pair_history, recs, slots,
                                               NULL, NULL);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
            return rc;
        }
    } else if (highest >= 0 && recs[highest].release_seq > 0u &&
               sc_store_record_is_retired(&recs[highest]) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    rc = sc_store_active_locked(io, trust, native_record, &active, &active_state,
                                &active_slot);
    if (rc == APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN) {
        io->tx_end(io->ctx);
        *out_state = active_state;
        return rc;
    }
    current_active = (active_state == APP_SIDECAR_LIFECYCLE_ACTIVE) ? AICAM_TRUE : AICAM_FALSE;
    candidate_bytes = (uint64_t)bundle_len + payload_len;
    if (candidate_bytes > (UINT64_MAX - APP_SIDECAR_LIFECYCLE_GC_RESERVE -
                           (2u * APP_SIDECAR_LIFECYCLE_RECORD_SIZE)) /
                              APP_SIDECAR_LIFECYCLE_FREE_MULTIPLIER) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_FULL;
    }
    required_bytes = candidate_bytes * APP_SIDECAR_LIFECYCLE_FREE_MULTIPLIER +
                     (2u * APP_SIDECAR_LIFECYCLE_RECORD_SIZE) +
                     APP_SIDECAR_LIFECYCLE_GC_RESERVE;
    if (io->free_bytes(io->ctx, &free_bytes) != APP_INSTALL_IO_OK) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (free_bytes < required_bytes) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_FULL;
    }
    inst_sha256(bundle, bundle_len, bundle_sha);
    sc_store_bundle_path(bundle_sha, bundle_path, sizeof(bundle_path));
    highest = sc_store_highest(recs, slots);
    target = (highest < 0) ? 0 : 1 - highest;
    memset(&candidate, 0, sizeof(candidate));
    sc_store_record_from_native(&candidate, native_record);
    candidate.native_version[0] = native_record->version_major;
    candidate.native_version[1] = native_record->version_minor;
    candidate.native_version[2] = native_record->version_patch;
    candidate.state = SC_STORE_REVOKED;
    candidate.generation = sc_store_next_generation(recs, slots);
    candidate.bundle_len = (uint32_t)bundle_len;
    memcpy(candidate.bundle_sha256, bundle_sha, sizeof(bundle_sha));
    if (candidate.generation == 0u) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    memset(&stage, 0, sizeof(stage));
    stage.phase = SC_STORE_PHASE_STAGED;
    stage.target_slot = (uint8_t)target;
    stage.has_displaced = (slots[target] == SC_STORE_SLOT_VALID) ? AICAM_TRUE : AICAM_FALSE;
    stage.candidate = candidate;
    if (stage.has_displaced == AICAM_TRUE) {
        stage.displaced = recs[target];
    }
    if (sc_store_stage_write(io, &stage) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    *out_state = APP_SIDECAR_LIFECYCLE_CANDIDATE_STAGED;
    if (sc_store_file_write_proven(io, bundle_path, bundle, bundle_len) != AICAM_TRUE) {
        cleanup_ok = sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                                 AICAM_FALSE);
        if (cleanup_ok == AICAM_TRUE && sc_store_stage_delete(io) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_SIDECAR_LIFECYCLE_ERR_IO;
        }
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    stored_bundle = (uint8_t *)sc_store_big_alloc(io, bundle_len);
    if (stored_bundle == NULL) {
        (void)sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                         AICAM_FALSE);
        (void)sc_store_stage_delete(io);
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (io->file_read(io->ctx, bundle_path, stored_bundle, bundle_len, &read_len) !=
            APP_INSTALL_IO_OK || read_len != bundle_len) {
        sc_store_big_free(io, stored_bundle);
        (void)sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                         AICAM_FALSE);
        (void)sc_store_stage_delete(io);
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    memset(&files, 0, sizeof(files));
    verify_rc = app_sidecar_verify_bundle(stored_bundle, read_len, trust,
                                          sc_store_file_collect, &files, &verdict);
    if (verify_rc != APP_SIDECAR_OK || files.overflow == AICAM_TRUE ||
        files.count != verdict.file_count ||
        sc_store_record_native_equal(&candidate, native_record) != AICAM_TRUE) {
        sc_store_big_free(io, stored_bundle);
        if (sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                       AICAM_FALSE) == AICAM_TRUE &&
            sc_store_stage_delete(io) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
            return APP_SIDECAR_LIFECYCLE_ERR_VERIFY;
        }
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (pair_history != NULL &&
        (strcmp(pair_history->publisher_id, verdict.publisher_id) != 0 ||
         strcmp(pair_history->app_id, verdict.app_id) != 0 ||
         memcmp(pair_history->ui_tbs_sha256, verdict.ui_tbs_sha256,
                APP_INSTALL_SHA256_LEN) != 0 ||
         pair_history->ui_version[0] != verdict.version_major ||
         pair_history->ui_version[1] != verdict.version_minor ||
         pair_history->ui_version[2] != verdict.version_patch)) {
        sc_store_big_free(io, stored_bundle);
        if (sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                       AICAM_FALSE) == AICAM_TRUE &&
            sc_store_stage_delete(io) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
            return APP_SIDECAR_LIFECYCLE_ERR_VERIFY;
        }
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    sc_store_record_update_verdict(&candidate, &verdict, bundle_sha,
                                   (uint32_t)bundle_len, native_record,
                                   pair_history);
    if (current_active == AICAM_TRUE &&
        sc_store_record_native_equal(&recs[active_slot], native_record) == AICAM_TRUE) {
        int cmp = sc_store_version_cmp(&recs[active_slot], &verdict);
        if (cmp == 0 && memcmp(recs[active_slot].ui_tbs_sha256,
                               verdict.ui_tbs_sha256, APP_SIDECAR_SHA256_LEN) != 0) {
            sc_store_big_free(io, stored_bundle);
            if (sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                           AICAM_FALSE) == AICAM_TRUE &&
                sc_store_stage_delete(io) == AICAM_TRUE) {
                io->tx_end(io->ctx);
                *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
                return APP_SIDECAR_LIFECYCLE_ERR_VERSION_CONFLICT;
            }
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        if (cmp == 0 && memcmp(recs[active_slot].ui_tbs_sha256,
                               verdict.ui_tbs_sha256, APP_SIDECAR_SHA256_LEN) == 0 &&
            (pair_history == NULL ||
             (recs[active_slot].release_seq == pair_history->release_seq &&
              memcmp(recs[active_slot].pair_tbs_sha256,
                     pair_history->pair_tbs_sha256,
                     APP_PAIR_TBS_SHA256_LEN) == 0))) {
            memcpy(out, &active, sizeof(*out));
            sc_store_big_free(io, stored_bundle);
            if (sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                           AICAM_FALSE) == AICAM_TRUE &&
                sc_store_stage_delete(io) == AICAM_TRUE) {
                out->state = APP_SIDECAR_LIFECYCLE_ACTIVE;
                *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
                io->tx_end(io->ctx);
                return APP_SIDECAR_LIFECYCLE_ALREADY;
            }
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
        if (cmp > 0) {
            sc_store_big_free(io, stored_bundle);
            if (sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                           AICAM_FALSE) == AICAM_TRUE &&
                sc_store_stage_delete(io) == AICAM_TRUE) {
                io->tx_end(io->ctx);
                *out_state = APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
                return APP_SIDECAR_LIFECYCLE_ERR_DOWNGRADE;
            }
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
        }
    }
    sc_store_big_free(io, stored_bundle);
    stage.candidate = candidate;
    stage.phase = SC_STORE_PHASE_VERIFIED;
    if (sc_store_stage_write(io, &stage) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    *out_state = APP_SIDECAR_LIFECYCLE_VERIFIED;
    if (sc_store_assets_write(io, &candidate, bundle, &verdict, &files) != AICAM_TRUE) {
        if (sc_store_candidate_cleanup(io, &candidate, recs, slots, target,
                                       AICAM_TRUE) == AICAM_TRUE &&
            sc_store_stage_delete(io) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_SIDECAR_LIFECYCLE_ERR_IO;
        }
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (pair_history != NULL &&
        sc_store_pair_floor_advance_locked(io, pair_history, recs, slots) !=
            APP_SIDECAR_LIFECYCLE_OK) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    stage.phase = SC_STORE_PHASE_COMMITTING;
    if (sc_store_stage_write(io, &stage) != AICAM_TRUE ||
        sc_store_slot_write(io, (uint8_t)target, &candidate) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    content_rc = sc_store_verify_record(io, trust, native_record, &candidate,
                                        &verified);
    if (content_rc != SC_STORE_VERIFY_OK) {
        io->tx_end(io->ctx);
        *out_state = (content_rc == SC_STORE_VERIFY_IO) ?
                     APP_SIDECAR_LIFECYCLE_UNKNOWN : APP_SIDECAR_LIFECYCLE_UNAVAILABLE;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    sc_store_fill_descriptor(&candidate, &verified, out);
    if (stage.has_displaced == AICAM_TRUE &&
        sc_store_record_resources_remove(io, &stage.displaced, &candidate, AICAM_TRUE,
                                         &recs[highest],
                                         (highest >= 0 && slots[highest] == SC_STORE_SLOT_VALID) ?
                                             AICAM_TRUE : AICAM_FALSE) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (sc_store_stage_delete(io) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    out->state = APP_SIDECAR_LIFECYCLE_ACTIVE;
    *out_state = APP_SIDECAR_LIFECYCLE_ACTIVE;
    io->tx_end(io->ctx);
    return APP_SIDECAR_LIFECYCLE_OK;
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_install(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    const uint8_t *bundle,
    size_t bundle_len,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state)
{
    return sc_sidecar_lifecycle_install_internal(io, trust, native_record,
                                                  bundle, bundle_len, NULL,
                                                  out, out_state);
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_install_pair(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    const uint8_t *bundle,
    size_t bundle_len,
    const app_pair_history_t *pair_history,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state)
{
    if (pair_history == NULL &&
        sc_store_native_is_unsigned(native_record) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    return sc_sidecar_lifecycle_install_internal(io, trust, native_record,
                                                  bundle, bundle_len,
                                                  pair_history, out,
                                                  out_state);
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_query(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state)
{
    app_sidecar_lifecycle_result_t rc;
    app_sidecar_descriptor_t recovered;
    app_sidecar_lifecycle_state_t recovered_state;
    int slot = -1;

    if (out_state != NULL) {
        *out_state = APP_SIDECAR_LIFECYCLE_NONE;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (sc_store_io_valid(io) != AICAM_TRUE || trust == NULL || native_record == NULL ||
        out == NULL || out_state == NULL) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    if (sc_tx_begin_read(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_BUSY;
    }
    if (sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) !=
        SC_STORE_SLOT_MISSING) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    rc = sc_store_identity_check_locked(io, trust, native_record);
    if (rc != APP_SIDECAR_LIFECYCLE_OK) {
        io->tx_end(io->ctx);
        *out_state = (rc == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED) ?
                     APP_SIDECAR_LIFECYCLE_UNAVAILABLE : APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return rc;
    }
    if (g_sidecar_boot_resolved != AICAM_TRUE) {
        rc = sc_store_boot_resolve_locked(io, trust, native_record,
                                          &recovered, &recovered_state);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            io->tx_end(io->ctx);
            *out_state = recovered_state;
            return rc;
        }
    } else {
        rc = sc_store_stage_recover_locked(io, trust, native_record);
        if (rc != APP_SIDECAR_LIFECYCLE_OK) {
            io->tx_end(io->ctx);
            *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
            return rc;
        }
    }
    rc = sc_store_active_locked(io, trust, native_record, out, out_state, &slot);
    io->tx_end(io->ctx);
    return rc;
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_boot_resolve(
    const app_install_io_t *io,
    const app_sidecar_trust_t *trust,
    const app_install_record_t *native_record,
    app_sidecar_descriptor_t *out,
    app_sidecar_lifecycle_state_t *out_state)
{
    app_sidecar_lifecycle_result_t rc;

    if (out_state != NULL) {
        *out_state = APP_SIDECAR_LIFECYCLE_NONE;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (sc_store_io_valid(io) != AICAM_TRUE || trust == NULL || native_record == NULL ||
        out == NULL || out_state == NULL) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    if (sc_tx_begin_read(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_BUSY;
    }
    if (sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) !=
        SC_STORE_SLOT_MISSING) {
        io->tx_end(io->ctx);
        *out_state = APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    rc = sc_store_identity_check_locked(io, trust, native_record);
    if (rc != APP_SIDECAR_LIFECYCLE_OK) {
        io->tx_end(io->ctx);
        *out_state = (rc == APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED) ?
                     APP_SIDECAR_LIFECYCLE_UNAVAILABLE : APP_SIDECAR_LIFECYCLE_UNKNOWN;
        return rc;
    }
    rc = sc_store_boot_resolve_locked(io, trust, native_record, out, out_state);
    io->tx_end(io->ctx);
    return rc;
}

void app_sidecar_lifecycle_reset(void)
{
    g_sidecar_boot_resolved = AICAM_FALSE;
    g_verify_cache_valid = AICAM_FALSE;
}

const char *app_sidecar_lifecycle_result_str(app_sidecar_lifecycle_result_t result)
{
    switch (result) {
    case APP_SIDECAR_LIFECYCLE_OK:
        return "OK";
    case APP_SIDECAR_LIFECYCLE_ALREADY:
        return "ALREADY_ACTIVE";
    case APP_SIDECAR_LIFECYCLE_CLEANUP_PENDING:
        return "CLEANUP_PENDING";
    case APP_SIDECAR_LIFECYCLE_ERR_PARAM:
        return "PARAM";
    case APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED:
        return "UNTRUSTED_NATIVE";
    case APP_SIDECAR_LIFECYCLE_ERR_VERIFY:
        return "VERIFY_FAILED";
    case APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN:
        return "STORAGE_UNKNOWN";
    case APP_SIDECAR_LIFECYCLE_ERR_STORAGE_FULL:
        return "STORAGE_FULL";
    case APP_SIDECAR_LIFECYCLE_ERR_VERSION_CONFLICT:
        return "VERSION_CONFLICT";
    case APP_SIDECAR_LIFECYCLE_ERR_DOWNGRADE:
        return "DOWNGRADE_REFUSED";
    case APP_SIDECAR_LIFECYCLE_ERR_BUSY:
        return "BUSY";
    case APP_SIDECAR_LIFECYCLE_ERR_IO:
        return "IO_FAILED";
    default:
        return "UNKNOWN";
    }
}

const char *app_sidecar_lifecycle_state_str(app_sidecar_lifecycle_state_t state)
{
    switch (state) {
    case APP_SIDECAR_LIFECYCLE_NONE:
        return "none";
    case APP_SIDECAR_LIFECYCLE_CANDIDATE_STAGED:
        return "candidate_staged";
    case APP_SIDECAR_LIFECYCLE_VERIFIED:
        return "verified";
    case APP_SIDECAR_LIFECYCLE_ACTIVE:
        return "active";
    case APP_SIDECAR_LIFECYCLE_UNAVAILABLE:
        return "unavailable";
    case APP_SIDECAR_LIFECYCLE_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_pair_history_check(
    const app_install_io_t *io,
    const app_pair_history_t *candidate)
{
    sc_store_record_t recs[2];
    int slots[2];
    int stage_state;
    app_sidecar_lifecycle_result_t result = APP_SIDECAR_LIFECYCLE_OK;

    if (sc_store_io_valid(io) != AICAM_TRUE || candidate == NULL) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    if (sc_tx_begin_read(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_BUSY;
    }
    stage_state = sc_store_stage_load(io, &(sc_store_stage_t){0});
    if (stage_state != SC_STORE_SLOT_MISSING ||
        sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) !=
            SC_STORE_SLOT_MISSING ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        result = APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    } else {
        result = sc_store_pair_floor_check_locked(io, candidate, recs, slots,
                                                  NULL, NULL);
    }
    io->tx_end(io->ctx);
    return result;
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_pair_history_query(
    const app_install_io_t *io,
    app_pair_history_t *out,
    aicam_bool_t *present)
{
    sc_store_record_t recs[2];
    sc_store_pair_floor_t floor;
    int slots[2];
    int stage_state;
    int floor_state;

    if (present != NULL) {
        *present = AICAM_FALSE;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (sc_store_io_valid(io) != AICAM_TRUE || out == NULL || present == NULL) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    if (sc_tx_begin_read(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_BUSY;
    }
    stage_state = sc_store_stage_load(io, &(sc_store_stage_t){0});
    if (stage_state == SC_STORE_SLOT_IO_ERROR || stage_state == SC_STORE_SLOT_CORRUPT ||
        sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) !=
            SC_STORE_SLOT_MISSING ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (stage_state != SC_STORE_SLOT_MISSING) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    floor_state = sc_store_pair_floor_load(io, &floor);
    if (floor_state == SC_STORE_SLOT_IO_ERROR || floor_state == SC_STORE_SLOT_CORRUPT ||
        (floor_state == SC_STORE_SLOT_MISSING &&
         sc_store_pair_record_exists(recs, slots) == AICAM_TRUE)) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (floor_state == SC_STORE_SLOT_MISSING) {
        int i;
        for (i = 0; i < 2; i++) {
            if (slots[i] == SC_STORE_SLOT_VALID &&
                sc_store_record_admits_unsigned(&recs[i]) != AICAM_TRUE) {
                io->tx_end(io->ctx);
                return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
            }
        }
    }
    if (floor_state == SC_STORE_SLOT_VALID &&
        sc_store_pair_floor_lineage_retired(&floor, recs, slots) != AICAM_TRUE) {
        *out = floor.history;
        *present = AICAM_TRUE;
    }
    io->tx_end(io->ctx);
    return APP_SIDECAR_LIFECYCLE_OK;
}

aicam_bool_t app_sidecar_lifecycle_pair_authorized(
    const app_install_io_t *io,
    const app_install_record_t *native_record,
    const app_sidecar_descriptor_t *descriptor)
{
    sc_store_record_t recs[2];
    sc_store_pair_floor_t floor;
    int slots[2];
    int highest;
    int stage_state;
    int floor_state;
    aicam_bool_t authorized = AICAM_FALSE;

    if (sc_store_io_valid(io) != AICAM_TRUE || native_record == NULL ||
        descriptor == NULL || sc_tx_begin_read(io) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    stage_state = sc_store_stage_load(io, &(sc_store_stage_t){0});
    if (stage_state != SC_STORE_SLOT_MISSING ||
        sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) !=
            SC_STORE_SLOT_MISSING ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        io->tx_end(io->ctx);
        return AICAM_FALSE;
    }
    floor_state = sc_store_pair_floor_load(io, &floor);
    highest = sc_store_highest(recs, slots);
    if (sc_store_native_is_unsigned(native_record) == AICAM_TRUE &&
        (floor_state != SC_STORE_SLOT_VALID ||
         sc_store_pair_record_exists(recs, slots) != AICAM_TRUE) && highest >= 0 &&
        recs[highest].state == SC_STORE_ACTIVE &&
        sc_store_record_is_unsigned(&recs[highest]) == AICAM_TRUE &&
        sc_store_record_native_equal(&recs[highest], native_record) == AICAM_TRUE &&
        strcmp(recs[highest].app_id, descriptor->app_id) == 0 &&
        memcmp(recs[highest].native_tbs_sha256, descriptor->native_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) == 0 &&
        memcmp(recs[highest].ui_tbs_sha256, descriptor->ui_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) == 0 &&
        recs[highest].version_major == descriptor->version_major &&
        recs[highest].version_minor == descriptor->version_minor &&
        recs[highest].version_patch == descriptor->version_patch &&
        recs[highest].generation == descriptor->service_generation) {
        authorized = AICAM_TRUE;
    }
    if (floor_state != SC_STORE_SLOT_VALID) {
        io->tx_end(io->ctx);
        return authorized;
    }
    if (highest >= 0 && recs[highest].state == SC_STORE_ACTIVE &&
        recs[highest].release_seq > 0u &&
        sc_store_pair_floor_matches_record(&floor, &recs[highest]) == AICAM_TRUE &&
        sc_store_record_native_equal(&recs[highest], native_record) == AICAM_TRUE &&
        strcmp(recs[highest].publisher_id, descriptor->publisher_id) == 0 &&
        strcmp(recs[highest].app_id, descriptor->app_id) == 0 &&
        memcmp(recs[highest].publisher_key_sha256,
               descriptor->publisher_key_sha256, APP_SIDECAR_SHA256_LEN) == 0 &&
        memcmp(recs[highest].native_tbs_sha256, descriptor->native_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) == 0 &&
        memcmp(recs[highest].ui_tbs_sha256, descriptor->ui_tbs_sha256,
               APP_SIDECAR_SHA256_LEN) == 0 &&
        memcmp(recs[highest].pair_tbs_sha256, descriptor->pair_tbs_sha256,
               APP_PAIR_TBS_SHA256_LEN) == 0 &&
        recs[highest].release_seq == descriptor->release_seq &&
        recs[highest].native_version[0] == native_record->version_major &&
        recs[highest].native_version[1] == native_record->version_minor &&
        recs[highest].native_version[2] == native_record->version_patch &&
        recs[highest].version_major == descriptor->version_major &&
        recs[highest].version_minor == descriptor->version_minor &&
        recs[highest].version_patch == descriptor->version_patch &&
        recs[highest].generation == descriptor->service_generation) {
        authorized = AICAM_TRUE;
    }
    io->tx_end(io->ctx);
    return authorized;
}

app_sidecar_lifecycle_result_t app_sidecar_lifecycle_unsigned_preflight(
    const app_install_io_t *io,
    const app_install_record_t *native_record)
{
    sc_store_record_t recs[2];
    sc_store_pair_floor_t floor;
    int slots[2];
    int stage_state;
    int floor_state;
    int i;

    if (sc_store_io_valid(io) != AICAM_TRUE ||
        (native_record != NULL && sc_store_native_is_unsigned(native_record) != AICAM_TRUE)) {
        return APP_SIDECAR_LIFECYCLE_ERR_PARAM;
    }
    if (sc_tx_begin_read(io) != AICAM_TRUE) {
        return APP_SIDECAR_LIFECYCLE_ERR_BUSY;
    }
    stage_state = sc_store_stage_load(io, &(sc_store_stage_t){0});
    if (stage_state != SC_STORE_SLOT_MISSING ||
        sc_uninstall_intent_load(io, &(sc_uninstall_intent_t){0}) != SC_STORE_SLOT_MISSING ||
        sc_store_load_slots(io, recs, slots) != AICAM_TRUE ||
        slots[0] == SC_STORE_SLOT_CORRUPT || slots[1] == SC_STORE_SLOT_CORRUPT) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    floor_state = sc_store_pair_floor_load(io, &floor);
    if (floor_state == SC_STORE_SLOT_IO_ERROR || floor_state == SC_STORE_SLOT_CORRUPT) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_STORAGE_UNKNOWN;
    }
    if (floor_state == SC_STORE_SLOT_VALID &&
        sc_store_pair_floor_lineage_retired(&floor, recs, slots) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
    }
    for (i = 0; i < 2; i++) {
        if (slots[i] == SC_STORE_SLOT_VALID &&
            sc_store_record_admits_unsigned(&recs[i]) != AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_SIDECAR_LIFECYCLE_ERR_UNTRUSTED;
        }
    }
    io->tx_end(io->ctx);
    return APP_SIDECAR_LIFECYCLE_OK;
}
