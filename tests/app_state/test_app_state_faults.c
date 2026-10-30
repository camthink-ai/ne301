#include "state_test_util.h"

#define QUOTA_A 4096u

static app_state_identity_t g_id;

static mem_kv_t *state_find_marker_kv(void)
{
    size_t i;
    for (i = 0; i < MEM_MAX_KEYS; i++) {
        if (g_io.keys[i].present == AICAM_TRUE &&
            strncmp(g_io.keys[i].key, "app_state_p_", 12) == 0) {
            return &g_io.keys[i];
        }
    }
    return NULL;
}

static void state_fail_marker_reads(void)
{
    mem_kv_t *marker = state_find_marker_kv();
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s", marker->key);
    g_io.fail_kv_read_key_skip = 0;
}

static void state_reset_installed(void)
{
    state_io_init();
    state_make_identity(&g_id, STATE_PUB_A, STATE_APP_A, 1u);
    state_install_identity(&g_id, 1u);
}

static void state_fail_meta_writes(void)
{
    mem_kv_t *meta = state_find_meta_kv();
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s", meta->key);
    g_io.fail_kv_write_key_skip = 0;
}

static mem_file_t *state_find_blob_with_data(const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0; i < MEM_MAX_FILES; i++) {
        if (g_io.files[i].present == AICAM_TRUE &&
            g_io.files[i].len == len &&
            memcmp(g_io.files[i].data, data, len) == 0) {
            return &g_io.files[i];
        }
    }
    return NULL;
}

static void test_clean_recover_no_marker(void)
{
    int32_t rc;
    state_reset_installed();
    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    CHECK(rc == APP_STATE_OK);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_meta_keys() == 0);
    CHECK(state_count_blob_files() == 0);
}

static void test_marker_write_fail(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 3u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    g_io.fail_next_kv_writes = 1;
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_IO_ERROR);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);
}

static void test_marker_write_fail_unprovable_absence(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    char marker_key[48];
    mem_kv_t *meta;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 4u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    meta = state_find_meta_kv();
    CHECK(meta != NULL);
    snprintf(marker_key, sizeof(marker_key), "app_state_p_%s", meta->key + 10);
    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s", marker_key);
    g_io.fail_kv_write_key_skip = 0;
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s", marker_key);
    g_io.fail_kv_read_key_skip = 1;

    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 0);
    g_io.fail_kv_read_key[0] = '\0';
    g_io.fail_kv_write_key[0] = '\0';

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 2u);
}

static void test_blob_write_fail_over_existing(void)
{
    app_state_session_t s;
    uint8_t blob_a[64];
    uint8_t blob_b[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob_a, sizeof(blob_a), 7u);
    state_fill_blob(blob_b, sizeof(blob_b), 8u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob_a, sizeof(blob_a), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    g_io.fail_next_file_writes = 1;
    rc = app_state_commit(&s, blob_b, sizeof(blob_b), 1u, &new_rev);
    CHECK(rc == APP_STATE_IO_ERROR);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob_a, sizeof(blob_a)) == 0);
}

static void test_blob_readback_fail_first_commit(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 9u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    g_io.fail_next_file_reads = 1;
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_IO_ERROR);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 0);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(rev == 0u);

    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
}

static void test_meta_write_fail_restore_prev(void)
{
    app_state_session_t s;
    uint8_t blob_a[64];
    uint8_t blob_b[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob_a, sizeof(blob_a), 11u);
    state_fill_blob(blob_b, sizeof(blob_b), 12u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob_a, sizeof(blob_a), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    state_fail_meta_writes();
    rc = app_state_commit(&s, blob_b, sizeof(blob_b), 1u, &new_rev);
    CHECK(rc == APP_STATE_IO_ERROR);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob_a, sizeof(blob_a)) == 0);
}

static void test_meta_write_fail_first_commit_restore_absent(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    char meta_key[48];
    mem_kv_t *meta;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 13u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    meta = state_find_meta_kv();
    CHECK(meta != NULL);
    snprintf(meta_key, sizeof(meta_key), "%s", meta->key);
    mem_kv_delete(&g_io, meta_key);
    {
        mem_file_t *file = state_find_blob_file();
        mem_file_remove(&g_io, file->path);
    }
    CHECK(state_count_meta_keys() == 0);
    CHECK(state_count_blob_files() == 0);
    CHECK(state_count_marker_keys() == 0);

    snprintf(g_io.fail_kv_write_key, sizeof(g_io.fail_kv_write_key), "%s", meta_key);
    g_io.fail_kv_write_key_skip = 0;
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_IO_ERROR);
    g_io.fail_kv_write_key[0] = '\0';
    CHECK(state_count_meta_keys() == 0);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 0);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(rev == 0u);

    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
}

static void test_torn_final_marker_delete_roll_forward(void)
{
    app_state_session_t s;
    uint8_t old_blob[64];
    uint8_t new_blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(old_blob, sizeof(old_blob), 17u);
    state_fill_blob(new_blob, sizeof(new_blob), 18u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, old_blob, sizeof(old_blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    g_io.fail_next_kv_deletes = 1;
    rc = app_state_commit(&s, new_blob, sizeof(new_blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);
    CHECK(state_count_blob_files() == 2);

    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    CHECK(rc == APP_STATE_OK);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(len == sizeof(new_blob));
    CHECK(memcmp(out, new_blob, sizeof(new_blob)) == 0);
    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(state_count_blob_files() == 1);
    CHECK(memcmp(out, new_blob, sizeof(new_blob)) == 0);
}

static void test_torn_new_blob_untrusted_preserves_previous(int remove_new,
                                                            int fail_recovery)
{
    app_state_session_t s;
    uint8_t old_blob[64];
    uint8_t new_blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    mem_file_t *old_file;
    mem_file_t *new_file;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(old_blob, sizeof(old_blob), 31u);
    state_fill_blob(new_blob, sizeof(new_blob), 32u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, old_blob, sizeof(old_blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);

    g_io.fail_next_kv_deletes = 1;
    rc = app_state_commit(&s, new_blob, sizeof(new_blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);
    CHECK(state_count_blob_files() == 2);
    old_file = state_find_blob_with_data(old_blob, sizeof(old_blob));
    new_file = state_find_blob_with_data(new_blob, sizeof(new_blob));
    CHECK(old_file != NULL);
    CHECK(new_file != NULL);
    if (new_file != NULL) {
        if (remove_new != 0) {
            CHECK(mem_file_remove(&g_io, new_file->path) == APP_INSTALL_IO_OK);
        } else {
            new_file->data[0] ^= 0xFFu;
        }
    }
    if (fail_recovery == 1) {
        state_fail_meta_writes();
    } else if (fail_recovery == 2) {
        g_io.fail_next_kv_deletes = 1;
    }

    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    if (fail_recovery != 0) {
        CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
        CHECK(state_count_marker_keys() == 1);
        CHECK(old_file != NULL && old_file->present == AICAM_TRUE);
        if (fail_recovery == 1) {
            g_io.fail_kv_write_key[0] = '\0';
        }
        rc = app_state_store_recover(&g_io_installer_io, &g_id);
    }
    CHECK(rc == APP_STATE_OK);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);
    CHECK(old_file != NULL && old_file->present == AICAM_TRUE);
    CHECK(old_file != NULL && old_file->len == sizeof(old_blob));
    CHECK(old_file != NULL && memcmp(old_file->data, old_blob, sizeof(old_blob)) == 0);

    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    CHECK(rc == APP_STATE_OK);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(len == sizeof(old_blob));
    CHECK(rev == 1u);
    CHECK(memcmp(out, old_blob, sizeof(old_blob)) == 0);
    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, old_blob, sizeof(old_blob)) == 0);
}

static void test_torn_rollback_prev_restored(void)
{
    app_state_session_t s;
    uint8_t blob_a[64];
    uint8_t blob_b[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob_a, sizeof(blob_a), 19u);
    state_fill_blob(blob_b, sizeof(blob_b), 20u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob_a, sizeof(blob_a), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    state_fail_meta_writes();
    g_io.fail_next_kv_deletes = 1;
    rc = app_state_commit(&s, blob_b, sizeof(blob_b), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);
    CHECK(state_count_blob_files() == 2);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
    CHECK(memcmp(out, blob_a, sizeof(blob_a)) == 0);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);

    rc = app_state_commit(&s, blob_b, sizeof(blob_b), 1u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 2u);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(memcmp(out, blob_b, sizeof(blob_b)) == 0);
}

static void test_torn_blob_write_first_commit_not_found(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 21u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);

    g_io.fail_next_file_writes = 1;
    g_io.fail_next_kv_deletes = 1;
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_NOT_FOUND);
    CHECK(rev == 0u);
    CHECK(len == 0u);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 0);

    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(new_rev == 1u);
}

static void test_recover_api_and_fresh_boot(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 23u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    g_io.fail_next_kv_deletes = 1;
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);

    rc = app_state_store_recover(&g_io_installer_io, &g_id);
    CHECK(rc == APP_STATE_OK);
    CHECK(state_count_marker_keys() == 0);

    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(memcmp(out, blob, sizeof(blob)) == 0);
}

static void test_marker_read_fail_closed(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 25u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    g_io.fail_next_kv_deletes = 1;
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);

    state_fail_marker_reads();
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);
    CHECK(state_count_blob_files() == 1);
    state_fail_marker_reads();
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 1);
    g_io.fail_kv_read_key[0] = '\0';

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 2u);
    CHECK(state_count_marker_keys() == 0);
}

static void test_meta_read_fail_closed_no_destructive_write(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint8_t out[128];
    uint32_t len = 0u;
    uint32_t rev = 0u;
    uint32_t new_rev = 0u;
    mem_file_t *file;
    mem_kv_t *meta;
    uint8_t saved[64];
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 27u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    file = state_find_blob_file();
    memcpy(saved, file->data, sizeof(saved));
    meta = state_find_meta_kv();
    CHECK(meta != NULL);
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s", meta->key);
    g_io.fail_kv_read_key_skip = 0;

    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(len == 0u);
    CHECK(rev == 0u);
    snprintf(g_io.fail_kv_read_key, sizeof(g_io.fail_kv_read_key), "%s", meta->key);
    g_io.fail_kv_read_key_skip = 0;
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    g_io.fail_kv_read_key[0] = '\0';

    CHECK(file->len == sizeof(saved));
    CHECK(memcmp(file->data, saved, sizeof(saved)) == 0);
    CHECK(state_count_marker_keys() == 0);
    rc = app_state_read(&s, out, sizeof(out), &len, &rev);
    CHECK(rc == APP_STATE_OK);
    CHECK(rev == 1u);
}

static void test_current_blob_read_fail_refuses_commit(void)
{
    app_state_session_t s;
    uint8_t blob[64];
    uint32_t new_rev = 0u;
    int32_t rc;

    state_reset_installed();
    state_fill_blob(blob, sizeof(blob), 29u);
    rc = app_state_session_bind(&s, &g_io_installer_io, &g_id, QUOTA_A);
    CHECK(rc == APP_STATE_OK);
    rc = app_state_commit(&s, blob, sizeof(blob), 0u, &new_rev);
    CHECK(rc == APP_STATE_OK);

    {
        mem_file_t *file = state_find_blob_file();
        file->corrupt_reads = 1;
    }
    rc = app_state_commit(&s, blob, sizeof(blob), 1u, &new_rev);
    CHECK(rc == APP_STATE_STORAGE_UNKNOWN);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 1);
}

static void test_repeated_recovery_idempotent(void)
{
    int32_t rc;
    int i;

    state_reset_installed();
    for (i = 0; i < 3; i++) {
        rc = app_state_store_recover(&g_io_installer_io, &g_id);
        CHECK(rc == APP_STATE_OK);
    }
    CHECK(state_count_meta_keys() == 0);
    CHECK(state_count_marker_keys() == 0);
    CHECK(state_count_blob_files() == 0);
}

int main(void)
{
    state_io_init();
    test_clean_recover_no_marker();
    test_marker_write_fail();
    test_marker_write_fail_unprovable_absence();
    test_blob_write_fail_over_existing();
    test_blob_readback_fail_first_commit();
    test_meta_write_fail_restore_prev();
    test_meta_write_fail_first_commit_restore_absent();
    test_torn_final_marker_delete_roll_forward();
    test_torn_rollback_prev_restored();
    test_torn_blob_write_first_commit_not_found();
    test_recover_api_and_fresh_boot();
    test_marker_read_fail_closed();
    test_meta_read_fail_closed_no_destructive_write();
    test_current_blob_read_fail_refuses_commit();
    test_repeated_recovery_idempotent();
    test_torn_new_blob_untrusted_preserves_previous(0, 0);
    test_torn_new_blob_untrusted_preserves_previous(1, 0);
    test_torn_new_blob_untrusted_preserves_previous(0, 1);
    test_torn_new_blob_untrusted_preserves_previous(0, 2);

    printf("app_state_faults: %d checks, %d failures\n", g_total, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
