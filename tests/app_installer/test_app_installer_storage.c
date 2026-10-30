#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

#include "app_installer.h"
#include "app_host_control.h"
#include "app_host_live.h"
#include "app_pair_installer.h"
#include "app_trust_anchor.h"
#include "installer_vectors.h"
#include "storage.h"
#include "cmsis_os2.h"

#define FAKE_MAX_FILES 8
#define FAKE_MAX_FILE 8192
#define FAKE_MAX_KEYS 16

typedef struct {
    char path[128];
    uint8_t data[FAKE_MAX_FILE];
    size_t len;
    int present;
} fake_file_t;

typedef struct {
    char key[48];
    uint8_t data[1024];
    size_t len;
    int present;
} fake_kv_t;

static fake_file_t g_fs_files[FAKE_MAX_FILES];
static fake_kv_t g_fs_kv[FAKE_MAX_KEYS];
static int g_fs_ready = 1;
static int g_fs_stat_generic_fail;
static int g_fs_remove_generic_fail;
static int g_fs_fopen_fail;
static int g_disk_info_fail;
static uint32_t g_disk_free_kbytes = 65536u;

static int g_total;
static int g_fail;

#define CHECK(cond) do { \
    g_total++; \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL storage:%d: %s\n", __LINE__, #cond); \
    } \
} while (0)

static fake_file_t *fs_find(const char *path)
{
    size_t i;
    for (i = 0; i < FAKE_MAX_FILES; i++) {
        if (g_fs_files[i].present && strcmp(g_fs_files[i].path, path) == 0) {
            return &g_fs_files[i];
        }
    }
    return NULL;
}

static fake_file_t *fs_slot(void)
{
    size_t i;
    for (i = 0; i < FAKE_MAX_FILES; i++) {
        if (!g_fs_files[i].present) {
            return &g_fs_files[i];
        }
    }
    return NULL;
}

#include "../../../Custom/Hal/mem.h"

void *hal_mem_alloc(size_t size, mem_type_t type)
{
    (void)type;
    return malloc(size);
}

void hal_mem_free(void *ptr)
{
    free(ptr);
}

void *flash_lfs_fopen(const char *path, const char *mode)
{
    fake_file_t *f;

    if (!g_fs_ready || g_fs_fopen_fail) {
        return NULL;
    }
    if (mode[0] == 'w') {
        f = fs_find(path);
        if (f == NULL) {
            f = fs_slot();
            if (f == NULL) {
                return NULL;
            }
            memset(f, 0, sizeof(*f));
            snprintf(f->path, sizeof(f->path), "%s", path);
            f->present = 1;
        }
        f->len = 0;
        return f;
    }
    f = fs_find(path);
    return (f != NULL) ? f : NULL;
}

int flash_lfs_fclose(void *fd)
{
    return (fd != NULL) ? 0 : -1;
}

int flash_lfs_fwrite(void *fd, const void *buf, size_t size)
{
    fake_file_t *f = (fake_file_t *)fd;

    if (f == NULL || !f->present) {
        return -1;
    }
    if (f->len + size > FAKE_MAX_FILE) {
        return -1;
    }
    memcpy(f->data + f->len, buf, size);
    f->len += size;
    return (int)size;
}

int flash_lfs_fread(void *fd, void *buf, size_t size)
{
    fake_file_t *f = (fake_file_t *)fd;
    size_t n;

    if (f == NULL || !f->present) {
        return -1;
    }
    n = (size < f->len) ? size : f->len;
    memcpy(buf, f->data, n);
    return (int)n;
}

int flash_lfs_remove(const char *path)
{
    fake_file_t *f;

    if (!g_fs_ready) {
        return -1;
    }
    if (g_fs_remove_generic_fail) {
        return -1;
    }
    f = fs_find(path);
    if (f == NULL) {
        return -ENOENT;
    }
    f->present = 0;
    return 0;
}

int flash_lfs_stat(const char *filename, struct stat *st)
{
    fake_file_t *f;

    if (!g_fs_ready) {
        return -1;
    }
    if (g_fs_stat_generic_fail) {
        return -1;
    }
    f = fs_find(filename);
    if (f == NULL) {
        return -ENOENT;
    }
    if (st != NULL) {
        memset(st, 0, sizeof(*st));
        st->st_size = (off_t)f->len;
        st->st_mode = 0100644;
    }
    return 0;
}

int flash_lfs_mkdir(const char *path)
{
    (void)path;
    return 0;
}

int flash_lfs_rename(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    return -1;
}

long flash_lfs_ftell(void *fd)
{
    (void)fd;
    return 0;
}

int flash_lfs_fseek(void *fd, long offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

int flash_lfs_fflush(void *fd)
{
    (void)fd;
    return 0;
}

void *flash_lfs_opendir(const char *path)
{
    (void)path;
    return NULL;
}

int flash_lfs_readdir(void *dd, char *info)
{
    (void)dd;
    (void)info;
    return -1;
}

int flash_lfs_closedir(void *dd)
{
    (void)dd;
    return -1;
}

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data, size_t len)
{
    fake_kv_t *kv;
    size_t i;

    (void)type;
    if (len > sizeof(g_fs_kv[0].data)) {
        return -1;
    }
    kv = NULL;
    for (i = 0; i < FAKE_MAX_KEYS; i++) {
        if (g_fs_kv[i].present && strcmp(g_fs_kv[i].key, key) == 0) {
            kv = &g_fs_kv[i];
            break;
        }
    }
    if (kv == NULL) {
        for (i = 0; i < FAKE_MAX_KEYS; i++) {
            if (!g_fs_kv[i].present) {
                kv = &g_fs_kv[i];
                break;
            }
        }
    }
    if (kv == NULL) {
        return -1;
    }
    memset(kv, 0, sizeof(*kv));
    snprintf(kv->key, sizeof(kv->key), "%s", key);
    memcpy(kv->data, data, len);
    kv->len = len;
    kv->present = 1;
    return 0;
}

int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len)
{
    size_t i;

    (void)type;
    for (i = 0; i < FAKE_MAX_KEYS; i++) {
        if (g_fs_kv[i].present && strcmp(g_fs_kv[i].key, key) == 0) {
            if (len < g_fs_kv[i].len) {
                return -1;
            }
            memcpy(data, g_fs_kv[i].data, g_fs_kv[i].len);
            return (int)g_fs_kv[i].len;
        }
    }
    return -ENOENT;
}

int storage_nvs_delete(NVS_Type_t type, const char *key)
{
    size_t i;

    (void)type;
    for (i = 0; i < FAKE_MAX_KEYS; i++) {
        if (g_fs_kv[i].present && strcmp(g_fs_kv[i].key, key) == 0) {
            g_fs_kv[i].present = 0;
            return 0;
        }
    }
    return -ENOENT;
}

int storage_get_disk_info(storage_disk_info_t *info)
{
    if (g_disk_info_fail) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->mounted = (g_fs_ready != 0);
    info->total_KBytes = 131072u;
    info->free_KBytes = g_disk_free_kbytes;
    snprintf(info->fs_type, sizeof(info->fs_type), "%s", "lfs");
    return 0;
}

static int g_mutex;
static int g_tx_zero_timeout_failures;
static int g_tx_any_timeout_failures;

osMutexId_t osMutexNew(const void *attribute)
{
    (void)attribute;
    return (osMutexId_t)&g_mutex;
}

int32_t osMutexDelete(osMutexId_t mutex)
{
    (void)mutex;
    return osOK;
}

int32_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout)
{
    (void)mutex;
    if (g_tx_any_timeout_failures > 0) {
        g_tx_any_timeout_failures--;
        return osOK + 1;
    }
    if (timeout == 0u && g_tx_zero_timeout_failures > 0) {
        g_tx_zero_timeout_failures--;
        return osOK + 1;
    }
    return osOK;
}

int32_t osMutexRelease(osMutexId_t mutex)
{
    (void)mutex;
    return osOK;
}

static app_admission_policy_t g_policy;

static uint8_t *read_fixture(const char *path, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *data;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return NULL;
    }
    size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL || fread(data, 1u, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_len = (size_t)size;
    return data;
}

static app_admission_result_t test_verify(const uint8_t *data, size_t len,
                                          app_admission_verdict_t *out, void *ctx)
{
    app_admission_policy_t *policy = (app_admission_policy_t *)ctx;
    const app_trust_anchor_t *anchor = app_trust_anchor_get();
    app_admission_trust_view_t trust;

    trust.publisher_id = anchor->publisher_id;
    trust.publisher_id_len = anchor->publisher_id_len;
    trust.spki_der = anchor->spki_der;
    trust.spki_der_len = anchor->spki_der_len;
    return app_admission_verify_package(data, len, policy, &trust, out);
}

static void fill_policy(void)
{
    memset(&g_policy, 0, sizeof(g_policy));
    g_policy.target_board_id = APP_ADMISSION_EXPERIMENT_BOARD_ID;
    g_policy.psram_mib = 64u;
    g_policy.abi_version = APP_ADMISSION_ABI_VERSION_V4;
    g_policy.supported_caps_mask = APP_ADMISSION_V3_KNOWN_CAPS_MASK |
                                   APP_ADMISSION_V4_CAP_ENABLE_CONTROL |
                                   APP_ADMISSION_V4_CAP_LIVE_PUBLISH;
    g_policy.max_package_len = APP_ADMISSION_MAX_PACKAGE_LEN_V5;
    g_policy.event_capacity = 16384u;
    g_policy.report_capacity = 16384u;
    g_policy.state_quota_capacity = 8192u;
    g_policy.control_capacity = APP_CONTROL_MSG_RAM_MAX;
    g_policy.snapshot_capacity = APP_CONTROL_SNAPSHOT_RAM_MAX;
    g_policy.abi_table_size = APP_ADMISSION_V4_ABI_TABLE_SIZE;
    g_policy.v3_business_profile = APP_ADMISSION_V3_BUSINESS_GENERIC_FULL;
    g_policy.live_publish_capacity = APP_HOST_LIVE_PUBLISH_CAPACITY;
    g_policy.live_track_capacity = APP_HOST_LIVE_TRACK_CAPACITY;
    g_policy.exec_region_base = 0x93E00000u;
    g_policy.exec_region_size = 2u * 1024u * 1024u;
}

static void reset_all(void)
{
    memset(g_fs_files, 0, sizeof(g_fs_files));
    memset(g_fs_kv, 0, sizeof(g_fs_kv));
    g_fs_ready = 1;
    g_fs_stat_generic_fail = 0;
    g_fs_remove_generic_fail = 0;
    g_fs_fopen_fail = 0;
    g_disk_info_fail = 0;
    g_disk_free_kbytes = 65536u;
    g_tx_zero_timeout_failures = 0;
    g_tx_any_timeout_failures = 0;
    app_install_boot_resolve_reset();
    app_install_set_app_running(AICAM_FALSE);
}

static void test_stat_and_remove_mapping(void)
{
    const app_install_io_t *io = app_installer_device_io();
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buf[16];
    size_t len = 0;

    reset_all();
    CHECK(app_installer_storage_init() == AICAM_OK);
    CHECK(io != NULL);
    CHECK(io->free_bytes != NULL);

    CHECK(io->file_stat(io->ctx, "/apps/pkg/none.neapp", &len) == APP_INSTALL_IO_NOT_FOUND);
    CHECK(io->file_write(io->ctx, "/apps/pkg/a.neapp", payload, sizeof(payload)) ==
          APP_INSTALL_IO_OK);
    CHECK(io->file_stat(io->ctx, "/apps/pkg/a.neapp", &len) == APP_INSTALL_IO_OK);
    CHECK(len == sizeof(payload));

    g_fs_stat_generic_fail = 1;
    CHECK(io->file_stat(io->ctx, "/apps/pkg/a.neapp", &len) == APP_INSTALL_IO_ERROR);
    CHECK(io->file_stat(io->ctx, "/apps/pkg/none.neapp", &len) == APP_INSTALL_IO_ERROR);
    CHECK(io->file_read(io->ctx, "/apps/pkg/a.neapp", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_ERROR);
    g_fs_stat_generic_fail = 0;

    CHECK(io->file_remove(io->ctx, "/apps/pkg/a.neapp") == APP_INSTALL_IO_OK);
    CHECK(io->file_remove(io->ctx, "/apps/pkg/a.neapp") == APP_INSTALL_IO_NOT_FOUND);
    CHECK(io->file_write(io->ctx, "/apps/pkg/a.neapp", payload, sizeof(payload)) ==
          APP_INSTALL_IO_OK);
    g_fs_remove_generic_fail = 1;
    CHECK(io->file_remove(io->ctx, "/apps/pkg/a.neapp") == APP_INSTALL_IO_ERROR);
    g_fs_remove_generic_fail = 0;
    CHECK(io->file_remove(io->ctx, "/apps/pkg/a.neapp") == APP_INSTALL_IO_OK);
}

static void test_read_and_write_mapping(void)
{
    const app_install_io_t *io = app_installer_device_io();
    const uint8_t payload[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x11};
    uint8_t buf[16];
    size_t len = 0;

    reset_all();
    CHECK(app_installer_storage_init() == AICAM_OK);

    CHECK(io->file_read(io->ctx, "/apps/pkg/none.neapp", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_NOT_FOUND);
    CHECK(io->file_write(io->ctx, "/apps/pkg/b.neapp", payload, sizeof(payload)) ==
          APP_INSTALL_IO_OK);
    CHECK(io->file_read(io->ctx, "/apps/pkg/b.neapp", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_OK);
    CHECK(len == sizeof(payload));
    CHECK(memcmp(buf, payload, sizeof(payload)) == 0);

    CHECK(io->file_read(io->ctx, "/apps/pkg/b.neapp", buf, 2u, &len) == APP_INSTALL_IO_ERROR);

    g_fs_fopen_fail = 1;
    CHECK(io->file_write(io->ctx, "/apps/pkg/c.neapp", payload, sizeof(payload)) ==
          APP_INSTALL_IO_ERROR);
    g_fs_fopen_fail = 0;

    g_fs_ready = 0;
    CHECK(io->file_stat(io->ctx, "/apps/pkg/b.neapp", &len) == APP_INSTALL_IO_ERROR);
    CHECK(io->file_read(io->ctx, "/apps/pkg/b.neapp", buf, sizeof(buf), &len) ==
          APP_INSTALL_IO_ERROR);
    CHECK(io->file_write(io->ctx, "/apps/pkg/b.neapp", payload, sizeof(payload)) ==
          APP_INSTALL_IO_ERROR);
    CHECK(io->file_remove(io->ctx, "/apps/pkg/b.neapp") == APP_INSTALL_IO_ERROR);
    g_fs_ready = 1;
}

static void test_free_bytes_and_kv_mapping(void)
{
    const app_install_io_t *io = app_installer_device_io();
    const uint8_t blob[] = {0x10, 0x20, 0x30};
    uint8_t buf[16];
    size_t len = 0;
    uint64_t free_bytes = 0;

    reset_all();
    CHECK(app_installer_storage_init() == AICAM_OK);

    CHECK(io->free_bytes(io->ctx, &free_bytes) == APP_INSTALL_IO_OK);
    CHECK(free_bytes == (uint64_t)g_disk_free_kbytes * 1024u);
    g_disk_info_fail = 1;
    CHECK(io->free_bytes(io->ctx, &free_bytes) == APP_INSTALL_IO_ERROR);
    g_disk_info_fail = 0;

    CHECK(io->kv_write(io->ctx, "k1", blob, sizeof(blob)) == APP_INSTALL_IO_OK);
    CHECK(io->kv_read(io->ctx, "k1", buf, sizeof(blob), &len) == APP_INSTALL_IO_OK);
    CHECK(len == sizeof(blob));
    CHECK(memcmp(buf, blob, sizeof(blob)) == 0);
    CHECK(io->kv_read(io->ctx, "missing", buf, sizeof(buf), &len) == APP_INSTALL_IO_NOT_FOUND);
    CHECK(io->kv_read(io->ctx, "k1", buf, sizeof(buf), &len) == APP_INSTALL_IO_ERROR);
    CHECK(io->kv_delete(io->ctx, "k1") == APP_INSTALL_IO_OK);
    CHECK(io->kv_delete(io->ctx, "k1") == APP_INSTALL_IO_OK);
}

static void test_device_io_install_fail_closed_when_unmounted(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t rec;
    app_install_query_t query;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t ui;
    app_sidecar_lifecycle_state_t pair_state;
    app_pair_install_result_t pair_result;
    uint8_t *initial;
    uint8_t *upgrade;
    size_t initial_len = 0u;
    size_t upgrade_len = 0u;

    reset_all();
    CHECK(app_installer_storage_init() == AICAM_OK);
    initial = read_fixture("../app_unsigned/single-file-v5.neapp", &initial_len);
    upgrade = read_fixture("../app_unsigned/single-file-v5-upgrade.neapp", &upgrade_len);
    CHECK(initial != NULL && upgrade != NULL);
    if (initial == NULL || upgrade == NULL) {
        free(initial);
        free(upgrade);
        return;
    }
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);

    pair_result = app_pair_install(io, test_verify, &g_policy, &trust,
                                   initial, initial_len, &rec, &ui, &pair_state);
    CHECK(pair_result == APP_PAIR_INSTALL_OK);

    g_fs_ready = 0;
    pair_result = app_pair_install(io, test_verify, &g_policy, &trust,
                                   upgrade, upgrade_len, &rec, &ui, &pair_state);
    CHECK(pair_result == APP_PAIR_INSTALL_ERR_STORAGE_UNKNOWN);
    CHECK(app_install_query(io, test_verify, &g_policy, &query) ==
          APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);
    CHECK(app_install_uninstall(io, &rec) == APP_INSTALL_ERR_STORAGE_STATE_UNKNOWN);

    g_fs_ready = 1;
    CHECK(app_install_query(io, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);
    CHECK(query.record.version_minor == 0u);
    CHECK(strcmp(query.record.publisher_id, APP_ADMISSION_ADMIN_PUBLISHER_ID) == 0);
    CHECK(strcmp(query.record.app_id, "line-crossing") == 0);

    pair_result = app_pair_install(io, test_verify, &g_policy, &trust,
                                   upgrade, upgrade_len, &rec, &ui, &pair_state);
    CHECK(pair_result == APP_PAIR_INSTALL_OK);
    CHECK(app_install_query(io, test_verify, &g_policy, &query) == APP_INSTALL_OK);
    CHECK(query.state == APP_INSTALL_STATE_INSTALLED);
    CHECK(query.record.version_major == 1u);
    CHECK(query.record.version_minor == 1u);
    free(initial);
    free(upgrade);
}

static void test_lifecycle_reads_survive_transient_tx_contention(void)
{
    const app_install_io_t *io = app_installer_device_io();
    app_install_record_t rec;
    app_sidecar_trust_t trust;
    app_sidecar_descriptor_t ui;
    app_sidecar_lifecycle_state_t state;
    app_sidecar_lifecycle_state_t baseline_state;
    app_sidecar_lifecycle_result_t baseline_result;
    app_sidecar_lifecycle_result_t result;
    app_pair_install_result_t pair_result;
    app_pair_history_t history;
    aicam_bool_t history_present;
    uint8_t *initial;
    size_t initial_len = 0u;

    reset_all();
    CHECK(app_installer_storage_init() == AICAM_OK);
    initial = read_fixture("../app_unsigned/single-file-v5.neapp", &initial_len);
    CHECK(initial != NULL);
    if (initial == NULL) {
        return;
    }
    memset(&trust, 0, sizeof(trust));
    trust.publisher_id = APP_ADMISSION_ADMIN_PUBLISHER_ID;
    trust.publisher_id_len = strlen(APP_ADMISSION_ADMIN_PUBLISHER_ID);

    pair_result = app_pair_install(io, test_verify, &g_policy, &trust,
                                   initial, initial_len, &rec, &ui, &state);
    CHECK(pair_result == APP_PAIR_INSTALL_OK);

    g_tx_zero_timeout_failures = 1;
    CHECK(app_install_uninstall(io, &rec) == APP_INSTALL_ERR_BUSY);
    CHECK(g_tx_zero_timeout_failures == 0);

    baseline_result = app_sidecar_lifecycle_query(io, &trust, &rec, &ui, &baseline_state);
    CHECK(baseline_result != APP_SIDECAR_LIFECYCLE_ERR_BUSY);

    g_tx_zero_timeout_failures = 1;
    result = app_sidecar_lifecycle_query(io, &trust, &rec, &ui, &state);
    CHECK(g_tx_zero_timeout_failures == 1);
    CHECK(result == baseline_result);
    CHECK(state == baseline_state);

    g_tx_zero_timeout_failures = 1;
    result = app_sidecar_lifecycle_boot_resolve(io, &trust, &rec, &ui, &state);
    CHECK(g_tx_zero_timeout_failures == 1);
    CHECK(result != APP_SIDECAR_LIFECYCLE_ERR_BUSY);

    g_tx_zero_timeout_failures = 1;
    result = app_sidecar_lifecycle_pair_history_query(io, &history, &history_present);
    CHECK(g_tx_zero_timeout_failures == 1);
    CHECK(result != APP_SIDECAR_LIFECYCLE_ERR_BUSY);

    {
        aicam_bool_t pair_baseline = app_sidecar_lifecycle_pair_authorized(io, &rec, &ui);
        g_tx_zero_timeout_failures = 1;
        CHECK(app_sidecar_lifecycle_pair_authorized(io, &rec, &ui) == pair_baseline);
        CHECK(g_tx_zero_timeout_failures == 1);
    }

    {
        app_uninstall_state_t uninstall_state = APP_UNINSTALL_STATE_UNKNOWN;
        app_install_record_t uninstall_record;
        app_install_result_t status_result;
        g_tx_zero_timeout_failures = 1;
        status_result = app_install_uninstall_status(io, &uninstall_state, &uninstall_record);
        CHECK(g_tx_zero_timeout_failures == 1);
        CHECK(status_result != APP_INSTALL_ERR_BUSY);
    }

    g_tx_zero_timeout_failures = 1;
    CHECK(app_sidecar_lifecycle_unsigned_preflight(io, &rec) !=
          APP_SIDECAR_LIFECYCLE_ERR_BUSY);
    CHECK(g_tx_zero_timeout_failures == 1);

    g_tx_any_timeout_failures = 1;
    result = app_sidecar_lifecycle_query(io, &trust, &rec, &ui, &state);
    CHECK(g_tx_any_timeout_failures == 0);
    CHECK(result == APP_SIDECAR_LIFECYCLE_ERR_BUSY);

    free(initial);
}

int main(void)
{
    fill_policy();
    test_stat_and_remove_mapping();
    test_read_and_write_mapping();
    test_free_bytes_and_kv_mapping();
    test_device_io_install_fail_closed_when_unmounted();
    test_lifecycle_reads_survive_transient_tx_contention();

    printf("test_app_installer_storage: %d checks, %d failures\n", g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
