#include "state_test_util.h"

#define QUOTA_A 4096u
#define BLOB_SMALL 152u
#define BLOB_V3 160u
#define BLOB_MID 512u

static void state_uninstall_all(void)
{
    mem_kv_delete(&g_io, APP_INSTALL_KEY_RECORD);
    mem_kv_delete(&g_io, APP_INSTALL_KEY_PENDING);
}

static void state_bootstrap_a(void)
{
    app_state_identity_t id;
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_install_identity(&id, 1u);
}

static void test_bind_param_gates(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    const app_install_io_t *io = &g_io_installer_io;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);

    rc = app_state_session_bind(NULL, io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_session_bind(&s, NULL, &id, QUOTA_A);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_session_bind(&s, io, NULL, QUOTA_A);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_session_bind(&s, io, &id, 0u);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    {
        app_state_identity_t bad = id;
        bad.publisher_id[0] = '\0';
        rc = app_state_session_bind(&s, io, &bad, QUOTA_A);
        CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    }
    {
        app_install_io_t broken = *io;
        broken.kv_write = NULL;
        rc = app_state_session_bind(&s, &broken, &id, QUOTA_A);
        CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    }
    {
        app_state_identity_t bad = id;
        memset(bad.spki_sha256, 0, sizeof(bad.spki_sha256));
        rc = app_state_session_bind(&s, io, &bad, QUOTA_A);
        CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    }
    CHECK(state_count_meta_keys() == 0);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 0);
}

static void test_bind_requires_committed_identity(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    app_state_identity_t other;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_make_identity(&other, STATE_PUB_B, STATE_APP_B, 2u);

    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_UNAUTHORIZED);

    state_install_identity(&id, 1u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &other, QUOTA_A);
    CHECK(rc == APP_STATE_UNAUTHORIZED);

    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    CHECK(s.authorized == AICAM_TRUE);
    CHECK(s.quota == QUOTA_A);
    CHECK(state_count_meta_keys() == 0);
}

static void test_first_read_not_found(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t out[64];
    uint32_t len = 99u;
    uint32_t rev = 99u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(len == 0u);
    CHECK(rev == APP_STATE_REVISION_NONE);

    rc = app_state_read(&s, NULL, 0u, &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(len == 0u);
    CHECK(rev == 0u);

    rc = app_state_read(&s, out, sizeof(out), NULL, &rev);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_read(&s, out, sizeof(out), &len, NULL);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_read(&s, NULL, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
}

static void test_commit_read_roundtrip(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[BLOB_MID];
    uint8_t out[BLOB_MID + 32u];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob, sizeof(blob), 3u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
    CHECK(state_count_meta_keys() == 1);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(len == sizeof(blob));
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);

    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_REVISION_CONFLICT);
    rc = app_state_commit(&s, blob, sizeof(blob), 2u, &new_rev);
    CHECK(rc == APP_STATE_REVISION_CONFLICT);
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 2u);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(state_count_blob_files() == 1);
}

static void test_commit_params_and_quota(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[QUOTA_A + 8u];
    uint8_t out[64];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob, sizeof(blob), 5u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    rc = app_state_commit(&s, NULL, 10u, 0u, &new_rev);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_commit(&s, blob, 0u, 0u, &new_rev);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_commit(&s, blob, 10u, 0u, NULL);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
    rc = app_state_commit(&s, blob, QUOTA_A + 1u, 0u, &new_rev);
    CHECK(rc == APP_STATE_QUOTA_EXCEEDED);
    CHECK(state_count_meta_keys() == 0);
    CHECK(state_count_blob_files() == 0);

    rc = app_state_commit(&s, blob, QUOTA_A, 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_BUFFER_TOO_SMALL);
    CHECK(len == QUOTA_A);
    CHECK(rev == 1u);

    rc = app_state_read(&s, NULL, 0u, &len, &rev);
    CHECK(rc == APP_STATE_BUFFER_TOO_SMALL);
    CHECK(len == QUOTA_A);
}

static void test_buffer_too_small_hint(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[BLOB_V3];
    uint8_t out[BLOB_V3];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob, sizeof(blob), 9u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, BLOB_V3, 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    rc = app_state_read(&s, out, BLOB_V3 - 1u, &len, &rev);
    CHECK(rc == APP_STATE_BUFFER_TOO_SMALL);
    CHECK(len == BLOB_V3);
    CHECK(rev == 1u);
    rc = app_state_read(&s, out, BLOB_V3, &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(memcmp(out, blob, BLOB_V3) == 0);
}

static void test_session_lifecycle(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[32];
    uint8_t out[64];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob, sizeof(blob), 11u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    app_state_session_end(&s);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);
    app_state_session_end(&s);
    CHECK(s.authorized == AICAM_FALSE);

    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);
}

static void test_identity_isolation(void)
{
    app_state_session_t sa;
    app_state_session_t sb;
    app_state_identity_t ida;
    app_state_identity_t idb;
    uint8_t blob_a[64];
    uint8_t blob_b[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&ida, STATE_PUB_A, STATE_APP_A, 1u);
    state_make_identity(&idb, STATE_PUB_B, STATE_APP_B, 2u);
    state_fill_blob(blob_a, sizeof(blob_a), 21u);
    state_fill_blob(blob_b, sizeof(blob_b), 22u);

    state_install_identity(&ida, 1u);
    rc = app_state_session_bind(&sa, &g_io_installer_io, &ida, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&sa, blob_a, sizeof(blob_a), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    state_uninstall_all();
    rc = app_state_read(&sa, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);

    state_install_identity(&idb, 1u);
    rc = app_state_session_bind(&sb, &g_io_installer_io, &idb, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&sb, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(rev == 0u);
    CHECK(len == 0u);
    rc = app_state_commit(&sb, blob_b, sizeof(blob_b), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
    CHECK(state_count_meta_keys() == 2);
    CHECK(state_count_blob_files() == 2);

    state_uninstall_all();
    state_install_identity(&ida, 1u);
    rc = app_state_session_bind(&sa, &g_io_installer_io, &ida, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&sa, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(len == sizeof(blob_a));
    CHECK(memcmp(out, blob_a, sizeof(blob_a)) == 0);
    rc = app_state_read(&sb, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);
}

static void test_upgrade_downgrade_preservation(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[96];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_fill_blob(blob, sizeof(blob), 31u);
    state_install_identity(&id, 1u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    state_install_identity(&id, 2u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(len == sizeof(blob));
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);

    state_install_identity(&id, 1u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);

    state_uninstall_all();
    CHECK(state_count_meta_keys() == 1);
    CHECK(state_count_blob_files() == 1);
    state_install_identity(&id, 2u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);
}

static void test_key_rotation_isolation(void)
{
    app_state_session_t s;
    app_state_identity_t old_id;
    app_state_identity_t new_id;
    uint8_t blob[48];
    uint8_t out[64];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&old_id, STATE_PUB_A, STATE_APP_A, 1u);
    state_make_identity(&new_id, STATE_PUB_A, STATE_APP_A, 40u);
    state_fill_blob(blob, sizeof(blob), 41u);
    state_install_identity(&old_id, 1u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &old_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    state_install_identity(&new_id, 1u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &new_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(rev == 0u);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
    CHECK(state_count_meta_keys() == 2);
    CHECK(state_count_blob_files() == 2);
}

static void test_opaque_blob_migration(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t v2blob[BLOB_SMALL];
    uint8_t v3blob[BLOB_V3];
    uint8_t out[BLOB_V3 + 16u];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    mem_kv_t *meta;
    mem_file_t *file;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(v2blob, sizeof(v2blob), 51u);
    state_fill_blob(v3blob, sizeof(v3blob), 52u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, v2blob, BLOB_SMALL, 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    rc = app_state_commit(&s, v3blob, BLOB_V3, 1u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 2u);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(len == BLOB_V3);
    CHECK(rev == 2u);
    CHECK(memcmp(out, v3blob, BLOB_V3) == 0);

    meta = state_find_meta_kv();
    file = state_find_blob_file();
    CHECK(meta != NULL);
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(file->len == BLOB_V3);
        CHECK(memcmp(file->data, v3blob, BLOB_V3) == 0);
    }

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(len == BLOB_V3);
    CHECK(rev == 2u);
    if (file != NULL) {
        CHECK(file->len == BLOB_V3);
        CHECK(memcmp(file->data, v3blob, BLOB_V3) == 0);
    }
    CHECK(state_count_blob_files() == 1);
    (void)meta;
}

static void test_corrupt_blob_fail_closed(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    mem_file_t *file;
    uint8_t saved[64];
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob, sizeof(blob), 61u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    file = state_find_blob_file();
    CHECK(file != NULL);
    memcpy(saved, file->data, sizeof(saved));
    file->data[7] ^= 0xFFu;
    saved[7] ^= 0xFFu;

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(len == 0u);
    CHECK(rev == 0u);
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(file != NULL && file->len == sizeof(saved));
    CHECK(memcmp(file->data, saved, sizeof(saved)) == 0);
    CHECK(state_count_marker_keys() == 0);
}

static void test_corrupt_meta_fail_closed(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    app_state_identity_t other;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    mem_kv_t *meta;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_make_identity(&other, STATE_PUB_B, STATE_APP_B, 2u);
    state_fill_blob(blob, sizeof(blob), 71u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    meta = state_find_meta_kv();
    CHECK(meta != NULL);
    meta->data[20] ^= 0xFFu;

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(len == 0u);
    CHECK(rev == 0u);
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_blob_files() == 1);
}

static void test_busy_and_free_space(void)
{
    app_state_session_t s;
    app_state_identity_t id;
    uint8_t blob[64];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob, sizeof(blob), 81u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    g_io.tx_held = 1;
    rc = app_state_read(&s, blob, sizeof(blob), &len, &rev);
    CHECK(rc == APP_STATE_BUSY);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_BUSY);
    rc = app_state_store_recover(&g_io_installer_io, &id);
    CHECK(rc == APP_STATE_BUSY);
    g_io.tx_held = 0;

    g_io.free_bytes_value = 32u;
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_IO_ERROR);
    g_io.free_bytes_enabled = AICAM_FALSE;
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    g_io.free_bytes_enabled = AICAM_TRUE;
    g_io.free_bytes_value = 64u * 1024u * 1024u;

    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
}

static void test_concurrent_cas_two_sessions(void)
{
    app_state_session_t s1;
    app_state_session_t s2;
    app_state_identity_t id;
    uint8_t blob_a[32];
    uint8_t blob_b[32];
    uint8_t out[64];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_fill_blob(blob_a, sizeof(blob_a), 91u);
    state_fill_blob(blob_b, sizeof(blob_b), 92u);
    rc = app_state_session_bind(&s1, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_session_bind(&s2, &g_io_installer_io, &id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    rc = app_state_commit(&s1, blob_a, sizeof(blob_a), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    rc = app_state_commit(&s2, blob_b, sizeof(blob_b), 0u, &new_rev);
    CHECK(rc == APP_STATE_REVISION_CONFLICT);
    rc = app_state_read(&s2, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob_a, sizeof(blob_a)) == 0);

    rc = app_state_commit(&s2, blob_b, sizeof(blob_b), 1u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 2u);
    rc = app_state_read(&s1, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(memcmp(out, blob_b, sizeof(blob_b)) == 0);
}

static void test_unbound_and_null_sessions(void)
{
    app_state_identity_t id;
    app_state_session_t s;
    uint8_t out[16];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    mem_reset();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);
    state_bootstrap_a();
    state_make_identity(&id, STATE_PUB_A, STATE_APP_A, 1u);

    rc = app_state_read(NULL, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);
    rc = app_state_commit(NULL, out, 1u, 0u, &new_rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);

    memset(&s, 0, sizeof(s));
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);
    rc = app_state_commit(&s, out, 1u, 0u, &new_rev);
    CHECK(rc == APP_STATE_UNAUTHORIZED);

    rc = app_state_store_recover(&g_io_installer_io, &id);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_store_recover(&g_io_installer_io, NULL);
    CHECK(rc == APP_STATE_INVALID_ARGUMENT);
}

static void test_result_str_and_wire_shape(void)
{
    typedef int32_t (*state_read_fn_t)(void *out, uint32_t cap, uint32_t *actual_len,
                                       uint32_t *out_revision);
    typedef int32_t (*state_commit_fn_t)(const void *blob, uint32_t len,
                                         uint32_t expected_revision,
                                         uint32_t *new_revision);
    _Static_assert(sizeof(state_read_fn_t) == 4u || sizeof(state_read_fn_t) == 8u,
                   "fn ptr width");
    CHECK(app_state_result_str(APP_STATE_OK)[0] == 'O');
    CHECK(app_state_result_str(APP_STATE_NOT_FOUND)[0] == 'N');
    CHECK(app_state_result_str(APP_STATE_REVISION_CONFLICT)[0] == 'R');
    CHECK(app_state_result_str(APP_STATE_STORAGE_UNKNOWN)[0] == 'S');
    CHECK(app_state_result_str(1234)[0] == 'U');
    (void)sizeof(state_commit_fn_t);
}

int main(void)
{
    state_io_init();
    test_bind_param_gates();
    test_bind_requires_committed_identity();
    test_first_read_not_found();
    test_commit_read_roundtrip();
    test_commit_params_and_quota();
    test_buffer_too_small_hint();
    test_session_lifecycle();
    test_identity_isolation();
    test_upgrade_downgrade_preservation();
    test_key_rotation_isolation();
    test_opaque_blob_migration();
    test_corrupt_blob_fail_closed();
    test_corrupt_meta_fail_closed();
    test_busy_and_free_space();
    test_concurrent_cas_two_sessions();
    test_unbound_and_null_sessions();
    test_result_str_and_wire_shape();

    printf("app_state_store: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
