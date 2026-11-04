#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "app_state_store.h"
#include "debug.h"
#include "app_admission.h"

#include "generic_math.h"
#include "mbedtls/sha256.h"

#define APP_STATE_META_MAGIC 0x31535441u
#define APP_STATE_MARKER_MAGIC 0x31505441u
#define APP_STATE_FORMAT_VERSION 1u
#define APP_STATE_SPKI_OFF 8u
#define APP_STATE_PUB_OFF (APP_STATE_SPKI_OFF + APP_STATE_SHA256_LEN)
#define APP_STATE_APP_OFF (APP_STATE_PUB_OFF + (APP_STATE_PUB_SLOT + 1u))
#define APP_STATE_REV_OFF (APP_STATE_APP_OFF + (APP_STATE_APP_SLOT + 1u))
#define APP_STATE_LEN_OFF (APP_STATE_REV_OFF + 4u)
#define APP_STATE_SHA_OFF (APP_STATE_LEN_OFF + 4u)
#define APP_STATE_META_CRC_OFF (APP_STATE_SHA_OFF + APP_STATE_SHA256_LEN)
#define APP_STATE_META_SIZE (APP_STATE_META_CRC_OFF + 4u)
#define APP_STATE_MARKER_PREV_OFF 8u
#define APP_STATE_MARKER_NEW_OFF (APP_STATE_MARKER_PREV_OFF + APP_STATE_META_SIZE)
#define APP_STATE_MARKER_CRC_OFF (APP_STATE_MARKER_NEW_OFF + APP_STATE_META_SIZE)
#define APP_STATE_MARKER_SIZE (APP_STATE_MARKER_CRC_OFF + 4u)
#define APP_STATE_SLOT_HEX_LEN 10u
#define APP_STATE_KEY_PREFIX "app_state_"
#define APP_STATE_MARKER_KEY_PREFIX "app_state_p_"
#define APP_STATE_BLOB_DIR "/apps/"
#define APP_STATE_BLOB_PREFIX "st_"
#define APP_STATE_BLOB_SUFFIX ".bin"
#define APP_STATE_BLOB_SHA_HEX 32u
#define APP_STATE_BLOB_PATH_CAP 96u
#define APP_STATE_KEY_CAP 48u
#define APP_STATE_MIN_FREE_MULTIPLIER 2u
#define APP_STATE_REVISION_LAST 0xFFFFFFFFu
#define APP_STATE_BLOB_LEN_MAX 0x10000000u

_Static_assert(APP_STATE_META_SIZE == 134, "state meta layout");
_Static_assert(APP_STATE_MARKER_SIZE == 280, "state marker layout");

typedef struct {
    uint8_t spki_sha256[APP_STATE_SHA256_LEN];
    char publisher_id[APP_STATE_PUB_SLOT + 1];
    char app_id[APP_STATE_APP_SLOT + 1];
    uint32_t revision;
    uint32_t blob_len;
    uint8_t blob_sha256[APP_STATE_SHA256_LEN];
} st_meta_t;

static void st_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t st_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void st_copy_padded(uint8_t *dst, const char *src, size_t slot)
{
    size_t len = strlen(src);
    memset(dst, 0, slot);
    if (len > slot) {
        len = slot;
    }
    memcpy(dst, src, len);
}

static void st_sha256(const uint8_t *data, size_t len, uint8_t out[APP_STATE_SHA256_LEN])
{
    mbedtls_sha256(data, len, out, 0);
}

static aicam_bool_t st_identity_valid(const app_state_identity_t *ident)
{
    size_t pub_len;
    size_t app_len;
    size_t i;
    int all_zero = 1;

    if (ident == NULL) {
        return AICAM_FALSE;
    }
    pub_len = strlen(ident->publisher_id);
    app_len = strlen(ident->app_id);
    if (pub_len == 0u || pub_len > APP_STATE_PUB_SLOT ||
        app_len == 0u || app_len > APP_STATE_APP_SLOT) {
        return AICAM_FALSE;
    }
    for (i = 0; i < APP_STATE_SHA256_LEN; i++) {
        if (ident->spki_sha256[i] != 0x00u) {
            all_zero = 0;
            break;
        }
    }
    return (all_zero == 1 &&
            strcmp(ident->publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) != 0)
               ? AICAM_FALSE : AICAM_TRUE;
}

static void st_slot_hex(const app_state_identity_t *ident, char *out, size_t cap)
{
    uint8_t seed[APP_STATE_SHA256_LEN + APP_STATE_PUB_SLOT + APP_STATE_APP_SLOT];
    uint8_t hash[APP_STATE_SHA256_LEN];
    uint8_t pub_pad[APP_STATE_PUB_SLOT];
    uint8_t app_pad[APP_STATE_APP_SLOT];
    size_t pos = 0;
    size_t i;

    st_copy_padded(pub_pad, ident->publisher_id, APP_STATE_PUB_SLOT);
    st_copy_padded(app_pad, ident->app_id, APP_STATE_APP_SLOT);
    memcpy(seed, ident->spki_sha256, APP_STATE_SHA256_LEN);
    memcpy(seed + APP_STATE_SHA256_LEN, pub_pad, APP_STATE_PUB_SLOT);
    memcpy(seed + APP_STATE_SHA256_LEN + APP_STATE_PUB_SLOT, app_pad, APP_STATE_APP_SLOT);
    st_sha256(seed, sizeof(seed), hash);
    for (i = 0; i < APP_STATE_SLOT_HEX_LEN / 2u; i++) {
        snprintf(out + pos, cap - pos, "%02x", hash[i]);
        pos += 2u;
    }
    out[pos] = '\0';
}

static void st_meta_key(const app_state_identity_t *ident, char *out, size_t cap)
{
    char slot[APP_STATE_SLOT_HEX_LEN + 1u];
    st_slot_hex(ident, slot, sizeof(slot));
    snprintf(out, cap, "%s%s", APP_STATE_KEY_PREFIX, slot);
}

static void st_marker_key(const app_state_identity_t *ident, char *out, size_t cap)
{
    char slot[APP_STATE_SLOT_HEX_LEN + 1u];
    st_slot_hex(ident, slot, sizeof(slot));
    snprintf(out, cap, "%s%s", APP_STATE_MARKER_KEY_PREFIX, slot);
}

static void st_blob_path(const app_state_identity_t *ident,
                         const uint8_t sha[APP_STATE_SHA256_LEN],
                         char *out, size_t cap)
{
    char slot[APP_STATE_SLOT_HEX_LEN + 1u];
    char sha_hex[APP_STATE_BLOB_SHA_HEX + 1u];
    size_t pos = 0;
    size_t i;

    st_slot_hex(ident, slot, sizeof(slot));
    for (i = 0; i < APP_STATE_BLOB_SHA_HEX / 2u; i++) {
        snprintf(sha_hex + pos, sizeof(sha_hex) - pos, "%02x", sha[i]);
        pos += 2u;
    }
    sha_hex[pos] = '\0';
    snprintf(out, cap, "%s%s%s_%s%s", APP_STATE_BLOB_DIR, APP_STATE_BLOB_PREFIX,
             slot, sha_hex, APP_STATE_BLOB_SUFFIX);
}

static void st_meta_encode(const st_meta_t *meta, uint8_t *out)
{
    memset(out, 0, APP_STATE_META_SIZE);
    st_put_u32(out, APP_STATE_META_MAGIC);
    st_put_u32(out + 4, APP_STATE_FORMAT_VERSION);
    memcpy(out + APP_STATE_SPKI_OFF, meta->spki_sha256, APP_STATE_SHA256_LEN);
    st_copy_padded(out + APP_STATE_PUB_OFF, meta->publisher_id, APP_STATE_PUB_SLOT + 1u);
    st_copy_padded(out + APP_STATE_APP_OFF, meta->app_id, APP_STATE_APP_SLOT + 1u);
    st_put_u32(out + APP_STATE_REV_OFF, meta->revision);
    st_put_u32(out + APP_STATE_LEN_OFF, meta->blob_len);
    memcpy(out + APP_STATE_SHA_OFF, meta->blob_sha256, APP_STATE_SHA256_LEN);
    st_put_u32(out + APP_STATE_META_CRC_OFF, generic_crc32(out, APP_STATE_META_CRC_OFF));
}

static aicam_bool_t st_meta_decode(const uint8_t *data, size_t len, st_meta_t *out)
{
    if (data == NULL || out == NULL || len != APP_STATE_META_SIZE) {
        return AICAM_FALSE;
    }
    if (st_get_u32(data) != APP_STATE_META_MAGIC ||
        st_get_u32(data + 4) != APP_STATE_FORMAT_VERSION) {
        return AICAM_FALSE;
    }
    if (st_get_u32(data + APP_STATE_META_CRC_OFF) !=
        generic_crc32(data, APP_STATE_META_CRC_OFF)) {
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->spki_sha256, data + APP_STATE_SPKI_OFF, APP_STATE_SHA256_LEN);
    memcpy(out->publisher_id, data + APP_STATE_PUB_OFF, APP_STATE_PUB_SLOT + 1u);
    out->publisher_id[APP_STATE_PUB_SLOT] = '\0';
    memcpy(out->app_id, data + APP_STATE_APP_OFF, APP_STATE_APP_SLOT + 1u);
    out->app_id[APP_STATE_APP_SLOT] = '\0';
    out->revision = st_get_u32(data + APP_STATE_REV_OFF);
    out->blob_len = st_get_u32(data + APP_STATE_LEN_OFF);
    memcpy(out->blob_sha256, data + APP_STATE_SHA_OFF, APP_STATE_SHA256_LEN);
    if (out->revision == APP_STATE_REVISION_NONE || out->blob_len == 0u ||
        out->blob_len > APP_STATE_BLOB_LEN_MAX) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static void st_meta_build(const app_state_identity_t *ident, uint32_t revision,
                          const uint8_t *blob, uint32_t len, st_meta_t *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->spki_sha256, ident->spki_sha256, APP_STATE_SHA256_LEN);
    snprintf(out->publisher_id, sizeof(out->publisher_id), "%s", ident->publisher_id);
    snprintf(out->app_id, sizeof(out->app_id), "%s", ident->app_id);
    out->revision = revision;
    out->blob_len = len;
    if (blob != NULL) {
        st_sha256(blob, len, out->blob_sha256);
    }
}

static void st_marker_encode(const st_meta_t *prev, aicam_bool_t has_prev,
                             const st_meta_t *next, uint8_t *out)
{
    memset(out, 0, APP_STATE_MARKER_SIZE);
    st_put_u32(out, APP_STATE_MARKER_MAGIC);
    st_put_u32(out + 4, APP_STATE_FORMAT_VERSION);
    if (has_prev == AICAM_TRUE) {
        st_meta_encode(prev, out + APP_STATE_MARKER_PREV_OFF);
    }
    st_meta_encode(next, out + APP_STATE_MARKER_NEW_OFF);
    st_put_u32(out + APP_STATE_MARKER_CRC_OFF,
               generic_crc32(out, APP_STATE_MARKER_CRC_OFF));
}

static aicam_bool_t st_marker_decode(const uint8_t *data, size_t len,
                                     st_meta_t *prev, aicam_bool_t *has_prev,
                                     st_meta_t *next)
{
    if (data == NULL || len != APP_STATE_MARKER_SIZE) {
        return AICAM_FALSE;
    }
    if (st_get_u32(data) != APP_STATE_MARKER_MAGIC ||
        st_get_u32(data + 4) != APP_STATE_FORMAT_VERSION) {
        return AICAM_FALSE;
    }
    if (st_get_u32(data + APP_STATE_MARKER_CRC_OFF) !=
        generic_crc32(data, APP_STATE_MARKER_CRC_OFF)) {
        return AICAM_FALSE;
    }
    if (st_meta_decode(data + APP_STATE_MARKER_NEW_OFF, APP_STATE_META_SIZE,
                       next) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (st_meta_decode(data + APP_STATE_MARKER_PREV_OFF, APP_STATE_META_SIZE,
                       prev) == AICAM_TRUE) {
        *has_prev = AICAM_TRUE;
        return AICAM_TRUE;
    }
    {
        uint8_t zeros[APP_STATE_META_SIZE];
        memset(zeros, 0, sizeof(zeros));
        if (memcmp(data + APP_STATE_MARKER_PREV_OFF, zeros, APP_STATE_META_SIZE) != 0) {
            return AICAM_FALSE;
        }
    }
    *has_prev = AICAM_FALSE;
    return AICAM_TRUE;
}

static aicam_bool_t st_meta_equal(const st_meta_t *a, const st_meta_t *b)
{
    uint8_t ea[APP_STATE_META_SIZE];
    uint8_t eb[APP_STATE_META_SIZE];
    st_meta_encode(a, ea);
    st_meta_encode(b, eb);
    return (memcmp(ea, eb, APP_STATE_META_SIZE) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t st_meta_owner_matches(const st_meta_t *meta,
                                          const app_state_identity_t *ident)
{
    st_meta_t want;
    st_meta_build(ident, meta->revision, NULL, 1u, &want);
    if (memcmp(meta->spki_sha256, want.spki_sha256, APP_STATE_SHA256_LEN) != 0 ||
        memcmp(meta->publisher_id, want.publisher_id, APP_STATE_PUB_SLOT + 1u) != 0 ||
        memcmp(meta->app_id, want.app_id, APP_STATE_APP_SLOT + 1u) != 0) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static app_install_io_result_t st_kv_read_exact(const app_install_io_t *io,
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

static aicam_bool_t st_kv_write_proven(const app_install_io_t *io, const char *key,
                                       const uint8_t *blob, size_t blob_size)
{
    uint8_t back[APP_STATE_MARKER_SIZE];
    app_install_io_result_t rc;

    if (blob_size > sizeof(back)) {
        return AICAM_FALSE;
    }
    if (io->kv_write(io->ctx, key, blob, blob_size) != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    rc = st_kv_read_exact(io, key, back, blob_size);
    if (rc != APP_INSTALL_IO_OK) {
        return AICAM_FALSE;
    }
    return (memcmp(back, blob, blob_size) == 0) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t st_kv_absent_proven(const app_install_io_t *io, const char *key,
                                        size_t blob_size)
{
    uint8_t probe[APP_STATE_MARKER_SIZE];
    size_t len = 0;
    return (io->kv_read(io->ctx, key, probe, blob_size, &len) ==
            APP_INSTALL_IO_NOT_FOUND) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t st_kv_delete_proven(const app_install_io_t *io, const char *key,
                                        size_t blob_size)
{
    uint8_t probe[APP_STATE_MARKER_SIZE];
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
    return st_kv_absent_proven(io, key, blob_size);
}

static aicam_bool_t st_file_load_verify(const app_install_io_t *io, const char *path,
                                        const uint8_t expected_sha[APP_STATE_SHA256_LEN],
                                        uint32_t expected_len)
{
    uint8_t *buf;
    uint8_t sha[APP_STATE_SHA256_LEN];
    size_t got = 0;
    aicam_bool_t ok;

    if (expected_len == 0u || expected_len > APP_STATE_BLOB_LEN_MAX) {
        return AICAM_FALSE;
    }
    buf = (uint8_t *)malloc(expected_len);
    if (buf == NULL) {
        return AICAM_FALSE;
    }
    ok = AICAM_FALSE;
    if (io->file_read(io->ctx, path, buf, expected_len, &got) == APP_INSTALL_IO_OK &&
        got == (size_t)expected_len) {
        if (expected_sha == NULL) {
            ok = AICAM_TRUE;
        } else {
            st_sha256(buf, got, sha);
            ok = (memcmp(sha, expected_sha, APP_STATE_SHA256_LEN) == 0) ? AICAM_TRUE : AICAM_FALSE;
        }
    }
    free(buf);
    return ok;
}

static aicam_result_t st_load_meta(const app_install_io_t *io,
                                   const app_state_identity_t *ident,
                                   st_meta_t *meta, aicam_bool_t *present)
{
    uint8_t blob[APP_STATE_META_SIZE];
    char key[APP_STATE_KEY_CAP];
    app_install_io_result_t rc;

    *present = AICAM_FALSE;
    st_meta_key(ident, key, sizeof(key));
    rc = st_kv_read_exact(io, key, blob, sizeof(blob));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return AICAM_OK;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return AICAM_ERROR;
    }
    if (st_meta_decode(blob, sizeof(blob), meta) != AICAM_TRUE) {
        return AICAM_ERROR;
    }
    if (st_meta_owner_matches(meta, ident) != AICAM_TRUE) {
        return AICAM_ERROR;
    }
    *present = AICAM_TRUE;
    return AICAM_OK;
}

static int32_t st_recover_locked(const app_install_io_t *io,
                                 const app_state_identity_t *ident)
{
    char mkey[APP_STATE_KEY_CAP];
    char pkey[APP_STATE_KEY_CAP];
    uint8_t marker[APP_STATE_MARKER_SIZE];
    uint8_t meta_blob[APP_STATE_META_SIZE];
    st_meta_t prev;
    st_meta_t next;
    st_meta_t cur;
    aicam_bool_t has_prev = AICAM_FALSE;
    aicam_bool_t have_cur = AICAM_FALSE;
    char new_path[APP_STATE_BLOB_PATH_CAP];
    char old_path[APP_STATE_BLOB_PATH_CAP];
    aicam_result_t lrc;
    app_install_io_result_t rc;

    st_meta_key(ident, mkey, sizeof(mkey));
    st_marker_key(ident, pkey, sizeof(pkey));
    rc = st_kv_read_exact(io, pkey, marker, sizeof(marker));
    if (rc == APP_INSTALL_IO_NOT_FOUND) {
        return APP_STATE_OK;
    }
    if (rc != APP_INSTALL_IO_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (st_marker_decode(marker, sizeof(marker), &prev, &has_prev, &next) != AICAM_TRUE) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    lrc = st_load_meta(io, ident, &cur, &have_cur);
    if (lrc != AICAM_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    st_blob_path(ident, next.blob_sha256, new_path, sizeof(new_path));

    if (have_cur == AICAM_TRUE) {
        if (st_meta_equal(&cur, &next) == AICAM_TRUE) {
            if (st_file_load_verify(io, new_path, next.blob_sha256,
                                    next.blob_len) != AICAM_TRUE) {
                if (has_prev != AICAM_TRUE) {
                    return APP_STATE_STORAGE_UNKNOWN;
                }
                st_blob_path(ident, prev.blob_sha256, old_path, sizeof(old_path));
                if (st_file_load_verify(io, old_path, prev.blob_sha256,
                                        prev.blob_len) != AICAM_TRUE) {
                    return APP_STATE_STORAGE_UNKNOWN;
                }
                st_meta_encode(&prev, meta_blob);
                if (st_kv_write_proven(io, mkey, meta_blob,
                                       sizeof(meta_blob)) != AICAM_TRUE) {
                    return APP_STATE_STORAGE_UNKNOWN;
                }
                if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) != AICAM_TRUE) {
                    return APP_STATE_STORAGE_UNKNOWN;
                }
                if (strcmp(old_path, new_path) != 0) {
                    (void)io->file_remove(io->ctx, new_path);
                }
                return APP_STATE_OK;
            }
            if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) != AICAM_TRUE) {
                return APP_STATE_STORAGE_UNKNOWN;
            }
            if (has_prev == AICAM_TRUE) {
                st_blob_path(ident, prev.blob_sha256, old_path, sizeof(old_path));
                if (strcmp(old_path, new_path) != 0) {
                    (void)io->file_remove(io->ctx, old_path);
                }
            }
            return APP_STATE_OK;
        }
        if (has_prev != AICAM_TRUE || st_meta_equal(&cur, &prev) != AICAM_TRUE) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        st_blob_path(ident, prev.blob_sha256, old_path, sizeof(old_path));
        if (st_file_load_verify(io, old_path, prev.blob_sha256, prev.blob_len) != AICAM_TRUE) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        if (strcmp(old_path, new_path) != 0) {
            (void)io->file_remove(io->ctx, new_path);
        }
        if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) != AICAM_TRUE) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        return APP_STATE_OK;
    }

    if (has_prev == AICAM_TRUE) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (st_file_load_verify(io, new_path, next.blob_sha256, next.blob_len) == AICAM_TRUE) {
        st_meta_encode(&next, meta_blob);
        if (st_kv_write_proven(io, mkey, meta_blob, sizeof(meta_blob)) != AICAM_TRUE) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) != AICAM_TRUE) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        return APP_STATE_OK;
    }
    (void)io->file_remove(io->ctx, new_path);
    if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) != AICAM_TRUE) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    return APP_STATE_OK;
}

static aicam_bool_t st_io_valid(const app_install_io_t *io)
{
    return (io != NULL && io->file_write != NULL && io->file_read != NULL &&
            io->file_remove != NULL && io->file_stat != NULL && io->kv_write != NULL &&
            io->kv_read != NULL && io->kv_delete != NULL && io->free_bytes != NULL &&
            io->tx_begin != NULL && io->tx_end != NULL) ? AICAM_TRUE : AICAM_FALSE;
}

static int32_t st_committed_identity_check(const app_install_io_t *io,
                                           const app_state_identity_t *ident)
{
    app_install_record_t rec;
    aicam_bool_t present = AICAM_FALSE;
    app_install_result_t qrc;

    qrc = app_install_query_committed(io, &rec, &present);
    if (qrc == APP_INSTALL_ERR_BUSY) {
        return APP_STATE_BUSY;
    }
    if (qrc != APP_INSTALL_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (present != AICAM_TRUE) {
        return APP_STATE_UNAUTHORIZED;
    }
    if (strcmp(rec.publisher_id, ident->publisher_id) != 0 ||
        strcmp(rec.app_id, ident->app_id) != 0 ||
        memcmp(rec.publisher_key_sha256, ident->spki_sha256, APP_STATE_SHA256_LEN) != 0) {
        return APP_STATE_UNAUTHORIZED;
    }
    return APP_STATE_OK;
}

static void st_zero_outputs(uint32_t *actual_len, uint32_t *out_revision)
{
    if (actual_len != NULL) {
        *actual_len = 0u;
    }
    if (out_revision != NULL) {
        *out_revision = 0u;
    }
}

int32_t app_state_session_bind(app_state_session_t *session,
                               const app_install_io_t *io,
                               const app_state_identity_t *identity,
                               uint32_t quota)
{
    int32_t gate;

    if (session == NULL) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    memset(session, 0, sizeof(*session));
    if (st_io_valid(io) != AICAM_TRUE || st_identity_valid(identity) != AICAM_TRUE ||
        quota == 0u) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    gate = st_committed_identity_check(io, identity);
    if (gate != APP_STATE_OK) {
        return gate;
    }
    session->io = io;
    session->identity = *identity;
    session->quota = quota;
    session->authorized = AICAM_TRUE;
    return APP_STATE_OK;
}

void app_state_session_end(app_state_session_t *session)
{
    if (session == NULL) {
        return;
    }
    session->authorized = AICAM_FALSE;
}

int32_t app_state_store_recover(const app_install_io_t *io,
                                const app_state_identity_t *identity)
{
    int32_t rc;

    if (st_io_valid(io) != AICAM_TRUE || st_identity_valid(identity) != AICAM_TRUE) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_STATE_BUSY;
    }
    rc = st_recover_locked(io, identity);
    io->tx_end(io->ctx);
    return rc;
}

static int32_t st_remove_blob_proven(const app_install_io_t *io,
                                     const app_state_identity_t *identity,
                                     const st_meta_t *meta)
{
    char path[APP_STATE_BLOB_PATH_CAP];
    size_t len = 0u;
    app_install_io_result_t rc;

    if (meta->blob_len == 0u) {
        return APP_STATE_OK;
    }
    st_blob_path(identity, meta->blob_sha256, path, sizeof(path));
    rc = io->file_remove(io->ctx, path);
    if (rc != APP_INSTALL_IO_OK && rc != APP_INSTALL_IO_NOT_FOUND) {
        rc = io->file_stat(io->ctx, path, &len);
        return (rc == APP_INSTALL_IO_NOT_FOUND) ? APP_STATE_OK : APP_STATE_STORAGE_UNKNOWN;
    }
    rc = io->file_stat(io->ctx, path, &len);
    return (rc == APP_INSTALL_IO_NOT_FOUND) ? APP_STATE_OK : APP_STATE_STORAGE_UNKNOWN;
}

int32_t app_state_store_purge_locked(const app_install_io_t *io,
                                     const app_state_identity_t *identity)
{
    char meta_key[APP_STATE_KEY_CAP];
    char marker_key[APP_STATE_KEY_CAP];
    uint8_t marker[APP_STATE_MARKER_SIZE];
    uint8_t meta_blob[APP_STATE_META_SIZE];
    st_meta_t prev;
    st_meta_t next;
    st_meta_t current;
    aicam_bool_t has_prev = AICAM_FALSE;
    aicam_bool_t has_current = AICAM_FALSE;
    aicam_bool_t has_marker = AICAM_FALSE;
    app_install_io_result_t rc;

    if (io == NULL || identity == NULL || io->file_remove == NULL ||
        io->file_stat == NULL || io->kv_read == NULL || io->kv_delete == NULL ||
        st_identity_valid(identity) != AICAM_TRUE) {
        return APP_STATE_INVALID_ARGUMENT;
    }

    st_meta_key(identity, meta_key, sizeof(meta_key));
    st_marker_key(identity, marker_key, sizeof(marker_key));
    rc = st_kv_read_exact(io, marker_key, marker, sizeof(marker));
    if (rc == APP_INSTALL_IO_OK) {
        if (st_marker_decode(marker, sizeof(marker), &prev, &has_prev, &next) != AICAM_TRUE ||
            st_meta_owner_matches(&next, identity) != AICAM_TRUE ||
            (has_prev == AICAM_TRUE && st_meta_owner_matches(&prev, identity) != AICAM_TRUE)) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        has_marker = AICAM_TRUE;
    } else if (rc != APP_INSTALL_IO_NOT_FOUND) {
        return APP_STATE_STORAGE_UNKNOWN;
    }

    rc = st_kv_read_exact(io, meta_key, meta_blob, sizeof(meta_blob));
    if (rc == APP_INSTALL_IO_OK) {
        if (st_meta_decode(meta_blob, sizeof(meta_blob), &current) != AICAM_TRUE ||
            st_meta_owner_matches(&current, identity) != AICAM_TRUE) {
            return APP_STATE_STORAGE_UNKNOWN;
        }
        has_current = AICAM_TRUE;
    } else if (rc != APP_INSTALL_IO_NOT_FOUND) {
        return APP_STATE_STORAGE_UNKNOWN;
    }

    if (has_current == AICAM_TRUE &&
        st_remove_blob_proven(io, identity, &current) != APP_STATE_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (has_marker == AICAM_TRUE &&
        st_remove_blob_proven(io, identity, &next) != APP_STATE_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (has_marker == AICAM_TRUE && has_prev == AICAM_TRUE &&
        st_remove_blob_proven(io, identity, &prev) != APP_STATE_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }

    if (st_kv_delete_proven(io, marker_key, APP_STATE_MARKER_SIZE) != AICAM_TRUE ||
        st_kv_delete_proven(io, meta_key, APP_STATE_META_SIZE) != AICAM_TRUE) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    return APP_STATE_OK;
}

int32_t app_state_read(app_state_session_t *session,
                       void *out, uint32_t cap,
                       uint32_t *actual_len, uint32_t *out_revision)
{
    const app_install_io_t *io;
    st_meta_t meta;
    aicam_bool_t present = AICAM_FALSE;
    uint8_t *buf;
    uint8_t sha[APP_STATE_SHA256_LEN];
    size_t got = 0;
    char path[APP_STATE_BLOB_PATH_CAP];
    int32_t gate;
    int32_t rc;

    if (actual_len == NULL || out_revision == NULL) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    st_zero_outputs(actual_len, out_revision);
    if (session == NULL || session->authorized != AICAM_TRUE ||
        st_identity_valid(&session->identity) != AICAM_TRUE) {
        return APP_STATE_UNAUTHORIZED;
    }
    if (out == NULL && cap > 0u) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    io = session->io;
    if (st_io_valid(io) != AICAM_TRUE) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    gate = st_committed_identity_check(io, &session->identity);
    if (gate != APP_STATE_OK) {
        return gate;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_STATE_BUSY;
    }
    rc = st_recover_locked(io, &session->identity);
    if (rc != APP_STATE_OK) {
        io->tx_end(io->ctx);
        return rc;
    }
    rc = st_load_meta(io, &session->identity, &meta, &present);
    if (rc != AICAM_OK) {
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (present != AICAM_TRUE) {
        *actual_len = 0u;
        *out_revision = APP_STATE_REVISION_NONE;
        io->tx_end(io->ctx);
        return APP_STATE_NOT_FOUND;
    }
    *actual_len = meta.blob_len;
    *out_revision = meta.revision;
    if (out == NULL || cap < meta.blob_len) {
        io->tx_end(io->ctx);
        return APP_STATE_BUFFER_TOO_SMALL;
    }
    st_blob_path(&session->identity, meta.blob_sha256, path, sizeof(path));
    buf = (uint8_t *)malloc(meta.blob_len);
    if (buf == NULL) {
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }
    {
        app_install_io_result_t frc = io->file_read(io->ctx, path, buf, meta.blob_len, &got);
        if (frc != APP_INSTALL_IO_OK || got != (size_t)meta.blob_len) {
            free(buf);
            st_zero_outputs(actual_len, out_revision);
            io->tx_end(io->ctx);
            return APP_STATE_STORAGE_UNKNOWN;
        }
    }
    st_sha256(buf, got, sha);
    if (memcmp(sha, meta.blob_sha256, APP_STATE_SHA256_LEN) != 0) {
        free(buf);
        st_zero_outputs(actual_len, out_revision);
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }
    memcpy(out, buf, meta.blob_len);
    free(buf);
    io->tx_end(io->ctx);
    return APP_STATE_OK;
}

int32_t app_state_commit(app_state_session_t *session,
                         const void *blob, uint32_t len,
                         uint32_t expected_revision, uint32_t *new_revision)
{
    const app_install_io_t *io;
    st_meta_t cur;
    st_meta_t prev_empty;
    st_meta_t meta_new;
    st_meta_t *prev;
    aicam_bool_t has_prev;
    aicam_bool_t present = AICAM_FALSE;
    aicam_bool_t prev_written;
    uint8_t marker[APP_STATE_MARKER_SIZE];
    uint8_t meta_blob[APP_STATE_META_SIZE];
    char pkey[APP_STATE_KEY_CAP];
    char mkey[APP_STATE_KEY_CAP];
    char new_path[APP_STATE_BLOB_PATH_CAP];
    char old_path[APP_STATE_BLOB_PATH_CAP];
    uint64_t free_bytes = 0;
    uint32_t cur_rev;
    uint32_t next_rev;
    int32_t gate;
    int32_t rc;

    memset(old_path, 0, sizeof(old_path));

    if (new_revision == NULL) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    *new_revision = 0u;
    if (session == NULL || session->authorized != AICAM_TRUE ||
        st_identity_valid(&session->identity) != AICAM_TRUE) {
        return APP_STATE_UNAUTHORIZED;
    }
    if (blob == NULL || len == 0u) {
        return APP_STATE_INVALID_ARGUMENT;
    }
    if (len > session->quota || len > APP_STATE_BLOB_LEN_MAX) {
        return APP_STATE_QUOTA_EXCEEDED;
    }
    io = session->io;
    if (st_io_valid(io) != AICAM_TRUE) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    gate = st_committed_identity_check(io, &session->identity);
    if (gate != APP_STATE_OK) {
        return gate;
    }
    if (io->free_bytes(io->ctx, &free_bytes) != APP_INSTALL_IO_OK) {
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (free_bytes < (uint64_t)len * APP_STATE_MIN_FREE_MULTIPLIER) {
        return APP_STATE_IO_ERROR;
    }
    if (io->tx_begin(io->ctx) != AICAM_TRUE) {
        return APP_STATE_BUSY;
    }
    rc = st_recover_locked(io, &session->identity);
    if (rc != APP_STATE_OK) {
        io->tx_end(io->ctx);
        return rc;
    }
    rc = st_load_meta(io, &session->identity, &cur, &present);
    if (rc != AICAM_OK) {
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }
    if (present == AICAM_TRUE) {
        st_blob_path(&session->identity, cur.blob_sha256, old_path, sizeof(old_path));
        if (st_file_load_verify(io, old_path, cur.blob_sha256, cur.blob_len) != AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_STATE_STORAGE_UNKNOWN;
        }
        cur_rev = cur.revision;
        prev = &cur;
        has_prev = AICAM_TRUE;
    } else {
        cur_rev = APP_STATE_REVISION_NONE;
        memset(&prev_empty, 0, sizeof(prev_empty));
        prev = &prev_empty;
        has_prev = AICAM_FALSE;
    }
    if (expected_revision != cur_rev) {
        LOG_SVC_WARN("app state: revision conflict expected=%u current=%u",
                     (unsigned int)expected_revision, (unsigned int)cur_rev);
        io->tx_end(io->ctx);
        return APP_STATE_REVISION_CONFLICT;
    }
    if (cur_rev == APP_STATE_REVISION_LAST) {
        io->tx_end(io->ctx);
        return APP_STATE_IO_ERROR;
    }
    next_rev = cur_rev + 1u;
    st_meta_build(&session->identity, next_rev, (const uint8_t *)blob, len, &meta_new);
    st_marker_key(&session->identity, pkey, sizeof(pkey));
    st_meta_key(&session->identity, mkey, sizeof(mkey));
    st_marker_encode(prev, has_prev, &meta_new, marker);

    if (st_kv_write_proven(io, pkey, marker, sizeof(marker)) != AICAM_TRUE) {
        if (st_kv_absent_proven(io, pkey, APP_STATE_MARKER_SIZE) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_STATE_IO_ERROR;
        }
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }

    st_blob_path(&session->identity, meta_new.blob_sha256, new_path, sizeof(new_path));
    if (io->file_write(io->ctx, new_path, (const uint8_t *)blob, len) != APP_INSTALL_IO_OK ||
        st_file_load_verify(io, new_path, meta_new.blob_sha256, len) != AICAM_TRUE) {
        if (strcmp(old_path, new_path) != 0) {
            (void)io->file_remove(io->ctx, new_path);
        }
        if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) == AICAM_TRUE) {
            io->tx_end(io->ctx);
            return APP_STATE_IO_ERROR;
        }
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }

    st_meta_encode(&meta_new, meta_blob);
    prev_written = AICAM_FALSE;
    if (st_kv_write_proven(io, mkey, meta_blob, sizeof(meta_blob)) != AICAM_TRUE) {
        if (has_prev == AICAM_TRUE) {
            uint8_t prev_blob[APP_STATE_META_SIZE];
            st_meta_encode(prev, prev_blob);
            prev_written = st_kv_write_proven(io, mkey, prev_blob, sizeof(prev_blob));
        } else {
            prev_written = st_kv_delete_proven(io, mkey, APP_STATE_META_SIZE);
        }
        if (prev_written == AICAM_TRUE && st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) == AICAM_TRUE) {
            if (strcmp(old_path, new_path) != 0) {
                (void)io->file_remove(io->ctx, new_path);
            }
            io->tx_end(io->ctx);
            return APP_STATE_IO_ERROR;
        }
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }

    if (st_kv_delete_proven(io, pkey, APP_STATE_MARKER_SIZE) != AICAM_TRUE) {
        io->tx_end(io->ctx);
        return APP_STATE_STORAGE_UNKNOWN;
    }

    if (has_prev == AICAM_TRUE && strcmp(old_path, new_path) != 0) {
        (void)io->file_remove(io->ctx, old_path);
    }
    *new_revision = next_rev;
    io->tx_end(io->ctx);
    return APP_STATE_OK;
}

const char *app_state_result_str(int32_t result)
{
    switch (result) {
    case APP_STATE_OK:
        return "OK";
    case APP_STATE_NOT_FOUND:
        return "NOT_FOUND";
    case APP_STATE_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case APP_STATE_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    case APP_STATE_UNAUTHORIZED:
        return "UNAUTHORIZED";
    case APP_STATE_QUOTA_EXCEEDED:
        return "QUOTA_EXCEEDED";
    case APP_STATE_STORAGE_UNKNOWN:
        return "STORAGE_UNKNOWN";
    case APP_STATE_BUSY:
        return "BUSY";
    case APP_STATE_REVISION_CONFLICT:
        return "REVISION_CONFLICT";
    case APP_STATE_IO_ERROR:
        return "IO_ERROR";
    default:
        return "UNKNOWN";
    }
}
