#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "app_installer.h"

#include "storage.h"
#include "../../Hal/mem.h"
#include "cmsis_os2.h"
#include "debug.h"
#include "aicam_types.h"

#define APP_INSTALL_TX_READ_TIMEOUT_TICKS 1500u

static osMutexId_t g_install_tx_mutex;
static aicam_bool_t g_install_dirs_ready;

static void app_install_ensure_dirs(void)
{
    if (g_install_dirs_ready == AICAM_TRUE) {
        return;
    }
    if (flash_lfs_mkdir(APP_INSTALL_DIR) != 0) {
        LOG_SVC_WARN("app install: mkdir %s failed (may already exist)", APP_INSTALL_DIR);
    }
    if (flash_lfs_mkdir(APP_INSTALL_PKG_DIR) != 0) {
        LOG_SVC_WARN("app install: mkdir %s failed (may already exist)", APP_INSTALL_PKG_DIR);
    }
    g_install_dirs_ready = AICAM_TRUE;
}

static app_install_io_result_t app_install_dev_file_write(void *ctx, const char *path,
                                                          const uint8_t *data, size_t len)
{
    void *fd;
    int written;
    (void)ctx;

    app_install_ensure_dirs();
    fd = flash_lfs_fopen(path, "w");
    if (fd == NULL) {
        return APP_INSTALL_IO_ERROR;
    }
    written = flash_lfs_fwrite(fd, data, len);
    if (flash_lfs_fclose(fd) != 0) {
        return APP_INSTALL_IO_ERROR;
    }
    if (written != (int)len) {
        return APP_INSTALL_IO_ERROR;
    }
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_file_read(void *ctx, const char *path,
                                                         uint8_t *buf, size_t cap,
                                                         size_t *len)
{
    void *fd;
    int got;
    int src;
    struct stat st;
    (void)ctx;

    src = flash_lfs_stat(path, &st);
    if (src == -ENOENT) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (src != 0) {
        return APP_INSTALL_IO_ERROR;
    }
    if ((size_t)st.st_size > cap) {
        return APP_INSTALL_IO_ERROR;
    }
    fd = flash_lfs_fopen(path, "r");
    if (fd == NULL) {
        return APP_INSTALL_IO_ERROR;
    }
    got = flash_lfs_fread(fd, buf, cap);
    flash_lfs_fclose(fd);
    if (got < 0 || (size_t)got != (size_t)st.st_size) {
        return APP_INSTALL_IO_ERROR;
    }
    *len = (size_t)got;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_file_remove(void *ctx, const char *path)
{
    int rc;
    (void)ctx;
    rc = flash_lfs_remove(path);
    if (rc == -ENOENT) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (rc != 0) {
        return APP_INSTALL_IO_ERROR;
    }
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_file_stat(void *ctx, const char *path,
                                                         size_t *len)
{
    struct stat st;
    int rc;
    (void)ctx;
    rc = flash_lfs_stat(path, &st);
    if (rc == -ENOENT) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (rc != 0) {
        return APP_INSTALL_IO_ERROR;
    }
    *len = (size_t)st.st_size;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_kv_write(void *ctx, const char *key,
                                                        const uint8_t *data, size_t len)
{
    (void)ctx;
    if (storage_nvs_write(NVS_USER, key, data, len) < 0) {
        return APP_INSTALL_IO_ERROR;
    }
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_kv_read(void *ctx, const char *key,
                                                       uint8_t *buf, size_t cap,
                                                       size_t *len)
{
    int rc;
    (void)ctx;
    rc = storage_nvs_read(NVS_USER, key, buf, cap);
    if (rc == -ENOENT) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (rc <= 0 || (size_t)rc > cap || (size_t)rc != cap) {
        return APP_INSTALL_IO_ERROR;
    }
    *len = (size_t)rc;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_kv_delete(void *ctx, const char *key)
{
    int rc;
    (void)ctx;
    rc = storage_nvs_delete(NVS_USER, key);
    if (rc != 0 && rc != -ENOENT) {
        return APP_INSTALL_IO_ERROR;
    }
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t app_install_dev_free_bytes(void *ctx, uint64_t *out)
{
    storage_disk_info_t info;
    (void)ctx;
    if (storage_get_disk_info(&info) != 0) {
        return APP_INSTALL_IO_ERROR;
    }
    *out = (uint64_t)info.free_KBytes * 1024u;
    return APP_INSTALL_IO_OK;
}

static aicam_bool_t app_install_dev_tx_begin(void *ctx)
{
    (void)ctx;
    if (g_install_tx_mutex == NULL) {
        return AICAM_FALSE;
    }
    return (osMutexAcquire(g_install_tx_mutex, 0) == osOK) ? AICAM_TRUE : AICAM_FALSE;
}

static aicam_bool_t app_install_dev_tx_begin_read(void *ctx)
{
    (void)ctx;
    if (g_install_tx_mutex == NULL) {
        return AICAM_FALSE;
    }
    return (osMutexAcquire(g_install_tx_mutex, APP_INSTALL_TX_READ_TIMEOUT_TICKS) ==
            osOK) ? AICAM_TRUE : AICAM_FALSE;
}

static void app_install_dev_tx_end(void *ctx)
{
    (void)ctx;
    if (g_install_tx_mutex != NULL) {
        osMutexRelease(g_install_tx_mutex);
    }
}

static void *app_install_dev_big_alloc(void *ctx, size_t len)
{
    (void)ctx;
    return hal_mem_alloc(len, MEM_LARGE);
}

static void app_install_dev_big_free(void *ctx, void *ptr)
{
    (void)ctx;
    hal_mem_free(ptr);
}

static const app_install_io_t g_app_install_device_io = {
    NULL,
    app_install_dev_file_write,
    app_install_dev_file_read,
    app_install_dev_file_remove,
    app_install_dev_file_stat,
    app_install_dev_kv_write,
    app_install_dev_kv_read,
    app_install_dev_kv_delete,
    app_install_dev_free_bytes,
    app_install_dev_tx_begin,
    app_install_dev_tx_end,
    app_install_dev_tx_begin_read,
    app_install_dev_big_alloc,
    app_install_dev_big_free
};

const app_install_io_t *app_installer_device_io(void)
{
    return &g_app_install_device_io;
}

aicam_result_t app_installer_storage_init(void)
{
    if (g_install_tx_mutex == NULL) {
        g_install_tx_mutex = osMutexNew(NULL);
        if (g_install_tx_mutex == NULL) {
            LOG_SVC_ERROR("app install: tx mutex create failed");
            return AICAM_ERROR;
        }
    }
    app_install_ensure_dirs();
    return AICAM_OK;
}
