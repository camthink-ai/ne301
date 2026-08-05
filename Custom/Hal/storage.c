#include "storage.h"
#include "debug.h"
#include "../FSBL/Core/Inc/xspim.h"
#include "common_utils.h"
#include "upgrade_manager.h"
#include "crc.h"
#include "iwdg.h"
#include "tx_api.h"

#define LFS_LOCK(sys)   do{ if ((sys)->thread_safe && (sys)->lock) (sys)->lock(); }while(0)
#define LFS_UNLOCK(sys) do{ if ((sys)->thread_safe && (sys)->unlock) (sys)->unlock(); }while(0)

static storage_t g_storage = {0};
static uint8_t old_data[4096] ALIGN_32 = {0};
/* Static buffers for littlefs caches/lookahead — provided via lfs_config so
 * littlefs doesn't allocate on the stack (which risks overflow with larger
 * cache sizes). cache_size=1024 → 1KB read+prog caches; lookahead_size=256
 * → 256-block free-block bitmap (32 bytes). The old config used cache/lookahead
 * =16 with NULL buffers, causing a full-FS traverse (lfs_fs_traverse) every 16
 * block allocations → seconds of hang with thousands of files. */
static uint8_t s_lfs_read_buffer[FS_LFS_CACHE_SIZE] ALIGN_32;
static uint8_t s_lfs_prog_buffer[FS_LFS_CACHE_SIZE] ALIGN_32;
static uint8_t s_lfs_lookahead_buffer[FS_LFS_LOOKAHEAD_SIZE / 8];
static uint8_t storage_tread_stack[1024 * 8] ALIGN_32;
const osThreadAttr_t storageTask_attributes = {
    .name = "storageTask",
    .priority = (osPriority_t) osPriorityBelowNormal,
    .stack_mem = storage_tread_stack,
    .stack_size = sizeof(storage_tread_stack),
};

/* Block device operations simulating Flash characteristics */
// Check if programming operation is allowed (conforms to Flash characteristics)
static bool is_programmable(const uint8_t *dst, const uint8_t *src, size_t size) 
{
    for (size_t i = 0; i < size; i++) {
        if ((dst[i] & src[i]) != src[i]) {
            return false; // Attempting to change bit from 0 to 1
        }
    }
    return true;
}

static int mem_block_read(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t off, void *buffer, lfs_size_t size) 
{
    mem_block_dev_t *dev = (mem_block_dev_t *)cfg->context;
    uint32_t addr = dev->start_addr + block * dev->block_size + off;
    storage_lock();
    XSPI_NOR_DisableMemoryMappedMode();
    if (XSPI_NOR_Read((uint8_t *)buffer, addr, size) != 0) {
        XSPI_NOR_EnableMemoryMappedMode();
        return LFS_ERR_IO;
    }
    XSPI_NOR_EnableMemoryMappedMode();
    // uint8_t *ptr_addr = (uint8_t *)(addr + FLASH_BASE);
    // memcpy(buffer, ptr_addr, size);
    storage_unlock();
    return LFS_ERR_OK;
}

// 2. Write
static int mem_block_prog(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size)
{
    mem_block_dev_t *dev = (mem_block_dev_t *)cfg->context;
    uint32_t addr = dev->start_addr + block * dev->block_size + off;
    int ret = LFS_ERR_OK;

    if (size > sizeof(old_data)) {
        return LFS_ERR_IO;
    }

    storage_lock();
    XSPI_NOR_DisableMemoryMappedMode();
    if (XSPI_NOR_Read(old_data, addr, size) != 0) {
        ret = LFS_ERR_IO;
        goto out;
    }
    if (!is_programmable(old_data, buffer, size)) {
        ret = LFS_ERR_CORRUPT;
        goto out;
    }
    if (XSPI_NOR_Write((const uint8_t *)buffer, addr, size) != 0) {
        ret = LFS_ERR_IO;
        goto out;
    }

out:
    XSPI_NOR_EnableMemoryMappedMode();
    storage_unlock();
    return ret;
}

// 3. Erase
static int mem_block_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    mem_block_dev_t *dev = (mem_block_dev_t *)cfg->context;
    int ret = LFS_ERR_OK;

    if (dev->erase_counts[block] >= dev->max_erase) {
        return LFS_ERR_IO;
    }

    storage_lock();
    XSPI_NOR_DisableMemoryMappedMode();
    uint32_t block_addr = dev->start_addr + block * dev->block_size;
    if (XSPI_NOR_Erase4K(block_addr) != 0) {
        ret = LFS_ERR_IO;
        goto out;
    }
    dev->erase_counts[block]++;

out:
    XSPI_NOR_EnableMemoryMappedMode();
    storage_unlock();
    return ret;
}

static int mem_block_sync(const struct lfs_config *cfg) 
{
    return LFS_ERR_OK;
}

static uint8_t storage_lfs_isready(void)
{
    if (!g_storage.lfs_sys.mounted) osSemaphoreAcquire(g_storage.lfs_sem_id, 3000);
    return g_storage.lfs_sys.mounted;
}

static void *storage_lfs_opendir(void *context, const char *path)
{
    lfs_mem_system_t *sys = (lfs_mem_system_t *)context;
    if (!sys || !storage_lfs_isready()) return NULL;
    LFS_LOCK(sys);

    lfs_dir_handle_t *dh = hal_mem_alloc_fast(sizeof(lfs_dir_handle_t));
    if (!dh) {
        LFS_UNLOCK(sys);
        return NULL;
    }

    int err = lfs_dir_open(&sys->lfs, &dh->dir, path);
    if (err) {
        hal_mem_free(dh);
        LFS_UNLOCK(sys);
        return NULL;
    }
    dh->lfs = &sys->lfs;
    dh->is_open = true;
    LFS_UNLOCK(sys);
    return (void *)dh;
}

static int storage_lfs_readdir(void *context, void *dd, char *info)
{
    lfs_dir_handle_t *dh = (lfs_dir_handle_t *)dd;
    if (!dh || !dh->is_open) return -1;

    lfs_mem_system_t *sys = (lfs_mem_system_t *)dh->lfs;
    LFS_LOCK(sys);

    int ret = lfs_dir_read(dh->lfs, &dh->dir, (struct lfs_info *)info);
    LFS_UNLOCK(sys);
    return ret;
}

static int storage_lfs_closedir(void *context, void *dd)
{
    lfs_dir_handle_t *dh = (lfs_dir_handle_t *)dd;
    if (!dh || !dh->is_open) return -1;

    lfs_mem_system_t *sys = (lfs_mem_system_t *)dh->lfs;
    LFS_LOCK(sys);

    int err = lfs_dir_close(dh->lfs, &dh->dir);
    dh->is_open = false;
    hal_mem_free(dh);

    LFS_UNLOCK(sys);
    return err == LFS_ERR_OK ? 0 : -1;
}

/* Complete file operation interface implementation */
static void * storage_lfs_fopen(void *context, const char *path, const char *mode) 
{
    lfs_mem_system_t *sys = (lfs_mem_system_t *)context;
    if (!sys || !storage_lfs_isready()) return NULL;
    LFS_LOCK(sys);

    lfs_file_handle_t *fh = hal_mem_alloc_fast(sizeof(lfs_file_handle_t));
    if (!fh) {
        LFS_UNLOCK(sys); 
        return NULL;
    }

    int flags = 0;

    // Handle standard modes
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        flags = LFS_O_RDONLY;
    } else if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 || strcmp(mode, "r+b") == 0) {
        flags = LFS_O_RDWR;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    } else if (strcmp(mode, "w+") == 0 || strcmp(mode, "wb+") == 0 || strcmp(mode, "w+b") == 0) {
        flags = LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
    } else if (strcmp(mode, "a+") == 0 || strcmp(mode, "ab+") == 0 || strcmp(mode, "a+b") == 0) {
        flags = LFS_O_RDWR | LFS_O_CREAT | LFS_O_APPEND;
    } else {
        // Unsupported mode
        hal_mem_free(fh);
        LFS_UNLOCK(sys);
        return NULL;
    }

    int err = lfs_file_open(&sys->lfs, &fh->file, path, flags);
    if (err) {
        hal_mem_free(fh);
        LFS_UNLOCK(sys);
        return NULL;
    }

    fh->lfs = &sys->lfs;
    fh->is_open = true;
    LFS_UNLOCK(sys);
    return (void*)fh;
}

static int storage_lfs_fclose(void *context, void *fd) 
{
    lfs_file_handle_t *fh = (lfs_file_handle_t *)fd;
    if (!fh || !fh->is_open) return -1;
    
    lfs_mem_system_t *sys = (lfs_mem_system_t *)fh->lfs;
    LFS_LOCK(sys);

    int err = lfs_file_close(fh->lfs, &fh->file);
    fh->is_open = false;
    hal_mem_free(fh);
    LFS_UNLOCK(sys);
    return err == LFS_ERR_OK ? 0 : -1;
}

static int storage_lfs_fread(void *context, void *fd, void *buf, size_t size) 
{
    lfs_file_handle_t *fh = (lfs_file_handle_t *)fd;
    if (!fh || !fh->is_open) return -1;
    lfs_mem_system_t *sys = (lfs_mem_system_t *)fh->lfs;
    LFS_LOCK(sys);
    lfs_ssize_t res = lfs_file_read(fh->lfs, &fh->file, buf, size);
    LFS_UNLOCK(sys);
    return (int)res;
}

static int storage_lfs_fwrite(void *context, void *fd, const void *buf, size_t size) 
{
    lfs_file_handle_t *fh = (lfs_file_handle_t *)fd;
    if (!fh || !fh->is_open) return -1;
    lfs_mem_system_t *sys = (lfs_mem_system_t *)fh->lfs;
    LFS_LOCK(sys);
    lfs_ssize_t res = lfs_file_write(fh->lfs, &fh->file, buf, size);
    LFS_UNLOCK(sys);
    return (int)res;
}

static int storage_lfs_remove(void *context, const char *path) 
{
    lfs_mem_system_t *sys = (lfs_mem_system_t *)context;
    if (!sys || !storage_lfs_isready()) return -1;
    LFS_LOCK(sys);
    int res = lfs_remove(&sys->lfs, path);
    LFS_UNLOCK(sys);
    return res == LFS_ERR_OK ? 0 : -1;
}

static int storage_lfs_rename(void *context, const char *oldpath, const char *newpath) 
{
    lfs_mem_system_t *sys = (lfs_mem_system_t *)context;
    if (!sys || !storage_lfs_isready()) return -1;
    LFS_LOCK(sys);
    int res = lfs_rename(&sys->lfs, oldpath, newpath);
    LFS_UNLOCK(sys);
    return res == LFS_ERR_OK ? 0 : -1;
}

static int storage_lfs_fflush(void *context, void *fd) 
{
    lfs_file_handle_t *fh = (lfs_file_handle_t *)fd;
    if (!fh || !fh->is_open) return -1;
    lfs_mem_system_t *sys = (lfs_mem_system_t *)fh->lfs;
    LFS_LOCK(sys);
    int res = lfs_file_sync(fh->lfs, &fh->file);
    LFS_UNLOCK(sys);
    return res == LFS_ERR_OK ? 0 : -1;
}

static long storage_lfs_ftell(void *context, void *fd) 
{
    lfs_file_handle_t *fh = (lfs_file_handle_t *)fd;
    if (!fh || !fh->is_open) return -1;
    lfs_mem_system_t *sys = (lfs_mem_system_t *)fh->lfs;
    LFS_LOCK(sys);
    lfs_soff_t pos = lfs_file_tell(fh->lfs, &fh->file);
    LFS_UNLOCK(sys);
    return (long)pos;
}

static int storage_lfs_fseek(void *context, void *fd, long offset, int whence) 
{
    lfs_file_handle_t *fh = (lfs_file_handle_t *)fd;
    if (!fh || !fh->is_open) return -1;
    lfs_mem_system_t *sys = (lfs_mem_system_t *)fh->lfs;
    LFS_LOCK(sys);
    int res = lfs_file_seek(fh->lfs, &fh->file, offset, whence);
    LFS_UNLOCK(sys);
    return (res < 0) ? -1 : 0;
}

static int storage_lfs_stat(void *context, const char *filename, struct stat *st)
{
    lfs_mem_system_t *sys = (lfs_mem_system_t *)context;
    if (!sys || !storage_lfs_isready()) return -1;
    LFS_LOCK(sys);

    struct lfs_info info;
    int res = lfs_stat(&sys->lfs, filename, &info);

    LFS_UNLOCK(sys);

    if (res == LFS_ERR_OK) {
        if (st) {
            memset(st, 0, sizeof(struct stat));
            st->st_size = info.size; // Fill file size
        }
        return 0; // File exists
    }
    return -1; // File does not exist
}

static int storage_lfs_mkdir(void *context, const char *path)
{
    lfs_mem_system_t *sys = (lfs_mem_system_t *)context;
    if (!sys || !storage_lfs_isready()) return -1;
    LFS_LOCK(sys);

    int res = lfs_mkdir(&sys->lfs, path);

    LFS_UNLOCK(sys);
    return (res == LFS_ERR_OK) ? 0 : -1;
}

void *flash_lfs_fopen(const char *path, const char *mode)
{
    return storage_lfs_fopen(&g_storage.lfs_sys, path, mode);
}

int flash_lfs_fclose(void *fd)
{
    return storage_lfs_fclose(&g_storage.lfs_sys, fd);
}

int flash_lfs_fwrite(void *fd, const void *buf, size_t size)
{
    return storage_lfs_fwrite(&g_storage.lfs_sys, fd, buf, size);
}

int flash_lfs_fread(void *fd, void *buf, size_t size)
{
    return storage_lfs_fread(&g_storage.lfs_sys, fd, buf, size);
}

int flash_lfs_remove(const char *path)
{
    return storage_lfs_remove(&g_storage.lfs_sys, path);
}

int flash_lfs_rename(const char *oldpath, const char *newpath)
{
    return storage_lfs_rename(&g_storage.lfs_sys, oldpath, newpath);
}

long flash_lfs_ftell(void *fd)
{
    return storage_lfs_ftell(&g_storage.lfs_sys, fd);
}

int flash_lfs_fseek(void *fd, long offset, int whence)
{
    return storage_lfs_fseek(&g_storage.lfs_sys, fd, offset, whence);
}

int flash_lfs_fflush(void *fd)
{
    return storage_lfs_fflush(&g_storage.lfs_sys, fd);
}

void *flash_lfs_opendir(const char *path)
{
    return storage_lfs_opendir(&g_storage.lfs_sys, path);
}

int flash_lfs_readdir(void *dd, char *info)
{
    return storage_lfs_readdir(&g_storage.lfs_sys, dd, info);
}

int flash_lfs_closedir(void *dd)
{
    return storage_lfs_closedir(&g_storage.lfs_sys, dd);
}

int flash_lfs_stat(const char *filename, struct stat *st)
{
    return storage_lfs_stat(&g_storage.lfs_sys, filename, st);
}

int flash_lfs_mkdir(const char *path)
{
    return storage_lfs_mkdir(&g_storage.lfs_sys, path);
}

// Complete file operation interface table


static file_ops_t lfs_file_ops = {
    .fopen   = storage_lfs_fopen,
    .fclose  = storage_lfs_fclose,
    .fwrite  = storage_lfs_fwrite,
    .fread   = storage_lfs_fread,
    .remove  = storage_lfs_remove,
    .rename  = storage_lfs_rename,
    .mkdir   = storage_lfs_mkdir,
    .ftell   = storage_lfs_ftell,
    .fseek   = storage_lfs_fseek,
    .fflush  = storage_lfs_fflush,
    .opendir = storage_lfs_opendir,
    .readdir = storage_lfs_readdir,
    .closedir= storage_lfs_closedir,
    .stat = storage_lfs_stat,
};


/* Filesystem initialization */
static int lfs_mem_init(lfs_mem_system_t *sys, 
                uint32_t mem_start, 
                size_t mem_size,
                size_t block_size,
                uint32_t max_erase_cycles,
                storage_lock_func_t lock, storage_unlock_func_t unlock) 
{
    if (!sys || !mem_start || mem_size < block_size * 2) {
        return LFS_ERR_INVAL;
    }

    // Configure memory block device
    sys->mem_dev = (mem_block_dev_t){
        .start_addr = mem_start,
        .size = mem_size,
        .block_size = block_size,
        .block_count = mem_size / block_size,
        .max_erase = max_erase_cycles
    };

    // Allocate erase count statistics array
    sys->mem_dev.erase_counts = hal_mem_calloc_large(sys->mem_dev.block_count, sizeof(uint32_t));
    if (!sys->mem_dev.erase_counts) {
        return LFS_ERR_NOMEM;
    }

    // Configure littlefs
    sys->config = (struct lfs_config){
        .context = &sys->mem_dev,
        .read  = mem_block_read,
        .prog  = mem_block_prog,
        .erase = mem_block_erase,
        .sync  = mem_block_sync,

        .read_size = FS_LFS_CACHE_SIZE,
        .prog_size = FS_LFS_CACHE_SIZE,
        .block_size = block_size,
        .block_count = sys->mem_dev.block_count,
        .cache_size = FS_LFS_CACHE_SIZE,
        .lookahead_size = FS_LFS_LOOKAHEAD_SIZE,
        .read_buffer = s_lfs_read_buffer,
        .prog_buffer = s_lfs_prog_buffer,
        .lookahead_buffer = s_lfs_lookahead_buffer,
        .block_cycles = max_erase_cycles
    };

    // Thread safety
    if (lock && unlock) {
        sys->lock = lock;
        sys->unlock = unlock;
        sys->thread_safe = true;
    } else {
        sys->thread_safe = false;
    }

    // Mount filesystem
    LFS_LOCK(sys);
    int err = lfs_mount(&sys->lfs, &sys->config);
    if (err) {
        lfs_format(&sys->lfs, &sys->config);
        err = lfs_mount(&sys->lfs, &sys->config);
    }
    LFS_UNLOCK(sys);

    sys->mounted = (err == LFS_ERR_OK);
    return err;
}

int storage_file_ops_switch(void)
{
    if(g_storage.file_ops_handle != -1){
        return file_ops_switch(g_storage.file_ops_handle);
    }
    return -1;
}

int storage_flash_write(uint32_t offset, void *data, size_t size)
{
    storage_lock();
    XSPI_NOR_DisableMemoryMappedMode();
    if (XSPI_NOR_Write((uint8_t *)data, offset, size) != 0) {
        XSPI_NOR_EnableMemoryMappedMode();
        storage_unlock();
        return -1; 
    }
    XSPI_NOR_EnableMemoryMappedMode();
    storage_unlock();
    return 0;
}

int storage_flash_read(uint32_t offset, void *data, size_t size)
{
    storage_lock();
    memcpy(data, (const void *)(FS_BASE_MEM_START + offset), size);
    // To reduce power consumption, switch back to XSPI memory-mapped mode after reading.
    // Disable task scheduling during the switch to avoid timing issues caused by task preemption.
    // XSPI_NOR_DisableMemoryMappedMode();
    // XSPI_NOR_EnableMemoryMappedMode();
    storage_unlock();
    return 0;
}

int storage_flash_erase(uint32_t offset, size_t num_blk)
{
    if (offset % FLASH_BLOCK_SIZE != 0) {
        return -1;
    }

    LOG_DRV_DEBUG("storage_flash_erase offset %u, num_blk %u\r\n", offset, num_blk);

    // For large OTA erases, protect I-cache by locking preemption during MM-disabled phase.
    // Code runs from XSPI flash (XIP). When memory-mapped mode is disabled for erase,
    // only I-cache serves instruction fetches. If other tasks run and evict the erase loop,
    // the CPU faults when trying to fetch from flash with MM disabled.
    // Strategy: lock preemption during erase batches (MM disabled), briefly unlock between
    // batches (MM re-enabled) to let network stack send TCP ACKs and prevent connection timeout.
    const size_t PREEMPTION_LOCK_THRESHOLD = 128;
    aicam_bool_t lock_preemption = (num_blk >= PREEMPTION_LOCK_THRESHOLD);

    TX_THREAD *self = NULL;
    UINT old_threshold = 0;
    if (lock_preemption) {
        self = tx_thread_identify();
        if (self) {
            tx_thread_preemption_change(self, 0, &old_threshold);
        }
    }

    storage_lock();
    XSPI_NOR_DisableMemoryMappedMode();

    const size_t YIELD_INTERVAL = 16;

    for (size_t i = 0; i < num_blk; i++) {
        if (XSPI_NOR_Erase4K(offset + i * FLASH_BLOCK_SIZE) != 0) {
            XSPI_NOR_EnableMemoryMappedMode();
            storage_unlock();
            if (lock_preemption && self) {
                tx_thread_preemption_change(self, old_threshold, &old_threshold);
            }
            LOG_DRV_ERROR("storage_flash_erase failed at block %u\r\n", i);
            return -1;
        }

        if ((i + 1) % YIELD_INTERVAL == 0 || i == num_blk - 1) {
            // Re-enable MM — flash access is now safe for other tasks
            XSPI_NOR_EnableMemoryMappedMode();
            storage_unlock();

            HAL_IWDG_Refresh(&hiwdg);

            if (lock_preemption && self) {
                // Briefly allow network stack to process TCP ACKs (prevent connection timeout).
                // MM is enabled so other tasks can safely access flash.
                // Keep window short (1ms) to minimize I-cache pressure on the erase loop.
                tx_thread_preemption_change(self, old_threshold, &old_threshold);
                osDelay(1);
                tx_thread_preemption_change(self, 0, &old_threshold);
            } else {
                osDelay(5);
            }

            if (num_blk > 64) {
                LOG_DRV_DEBUG("storage_flash_erase progress: %u/%u blocks (%.1f%%)\r\n",
                       i + 1, num_blk, ((float)(i + 1) * 100.0f / num_blk));
            }

            storage_lock();
            XSPI_NOR_DisableMemoryMappedMode();
        }
    }

    XSPI_NOR_EnableMemoryMappedMode();
    storage_unlock();

    if (lock_preemption && self) {
        tx_thread_preemption_change(self, old_threshold, &old_threshold);
    }

    LOG_DRV_DEBUG("storage_flash_erase end\r\n");
    return 0;
}

/* lfs_fs_size traverses the whole FS (~120ms on 8192 blocks). Boot calls
 * storage_get_disk_info several times (log registration, ensure_dirs, status
 * polls). Cache the used-block count for a short window so repeat calls within
 * the window are free. Stale by up to TTL ms is acceptable: space checks use
 * large margins (≥512 KB), and total capacity never changes. */
#define FS_SIZE_CACHE_TTL_MS  2000U
static struct {
    lfs_ssize_t used_blocks;
    uint32_t    ts_ms;
    bool        valid;
} g_fs_size_cache;

int storage_get_disk_info(storage_disk_info_t *info)
{
    if (info == NULL) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    strcpy(info->fs_type, "littlefs");
    info->mounted = g_storage.lfs_sys.mounted;

    if (!g_storage.is_init || !g_storage.lfs_sys.mounted) {
        return 0;
    }

    lfs_mem_system_t *sys = &g_storage.lfs_sys;
    LFS_LOCK(sys);

    /* Use cached used-block count if fresh — avoids the ~120ms full-FS traverse
     * on repeat calls (boot hits this 2-3x, web status polls hit it regularly). */
    lfs_ssize_t used_blocks;
    uint32_t now = HAL_GetTick();
    if (g_fs_size_cache.valid && (now - g_fs_size_cache.ts_ms) < FS_SIZE_CACHE_TTL_MS) {
        used_blocks = g_fs_size_cache.used_blocks;
    } else {
        used_blocks = lfs_fs_size(&sys->lfs);
        if (used_blocks < 0) {
            LFS_UNLOCK(sys);
            return -2;
        }
        g_fs_size_cache.used_blocks = used_blocks;
        g_fs_size_cache.ts_ms = now;
        g_fs_size_cache.valid = true;
    }

    size_t total_bytes = sys->mem_dev.block_count * sys->mem_dev.block_size;
    size_t used_bytes = (size_t)used_blocks * sys->mem_dev.block_size;
    size_t free_bytes = (used_bytes < total_bytes) ? (total_bytes - used_bytes) : 0U;

    LFS_UNLOCK(sys);

    info->total_KBytes = (uint32_t)(total_bytes / 1024U);
    info->free_KBytes = (uint32_t)(free_bytes / 1024U);

    return 0;
}

bool storage_is_lfs_mounted(void)
{
    return (g_storage.is_init && g_storage.lfs_sys.mounted);
}

static int storage_flash_erase4K(uint32_t offset, size_t size)
{
    (void)size; //NVS and LFS only use 4K erase
    
    storage_lock();
    XSPI_NOR_DisableMemoryMappedMode();
    if(offset % FS_FLASH_BLK != 0) {
        XSPI_NOR_EnableMemoryMappedMode();
        storage_unlock();
        return -1; 
    }

    if (XSPI_NOR_Erase4K(offset) != 0) {
        XSPI_NOR_EnableMemoryMappedMode();
        storage_unlock();
        return -1; 
    }
    XSPI_NOR_EnableMemoryMappedMode();
    storage_unlock();
    return 0;
}

static void nvs_lock(void *mutex_id)
{
    osMutexAcquire(mutex_id, osWaitForever);
}

static void nvs_unlock(void *mutex_id)
{
    osMutexRelease(mutex_id);
}

static int sysclk_nor_flash_read(uint32_t address, void *data, size_t size)
{
    /*
     * SYS_CLK_CONFIG_SAVE_FLASH_BASE (and other persisted slots) live in the
     * memory-mapped flash window (e.g. 0x7000_0000). Reading via XSPI command
     * mode can block for seconds due to peripheral/busy timeouts and also
     * disrupt other users of the memory-mapped region. Prefer a plain memcpy.
     */
    if (data == (void *)0 || size == 0U)
        return -1;

    storage_lock();
    memcpy(data, (const void *)address, size);
    storage_unlock();
    
    return 0;
}

static int sysclk_nor_flash_write(uint32_t address, void *data, size_t size)
{
    /* Reuse proven storage layer write (handles lock + XSPI mode switch) */
    if (data == (void *)0 || size == 0U)
        return -1;
    if (address < FLASH_BASE)
        return -1;
    uint32_t offset = address - FLASH_BASE;
    return storage_flash_write(offset, data, size);
}

static int sysclk_nor_flash_erase(uint32_t address, size_t size)
{
    /* Reuse storage layer erase (yields during long erase loops) */
    if (size == 0U)
        return -1;
    if (address < FLASH_BASE)
        return -1;
    if ((address - FLASH_BASE) % FLASH_BLOCK_SIZE != 0U) 
        return -1;
    if (size % FLASH_BLOCK_SIZE != 0U)
        return -1;
    uint32_t offset = address - FLASH_BASE;
    size_t num_blk = size / FLASH_BLOCK_SIZE;
    return storage_flash_erase(offset, num_blk);
}

static uint32_t sysclk_hal_crc32(void *data, size_t size)
{
    return CRC_Calculate(data, (uint32_t)size);
}

static int storage_nvs_init(nvs_fs_t *nvs, uint32_t flash_offset, size_t sector_size, size_t sector_count) 
{
    int ret = 0;
    if (!nvs) {
        return -1;
    }

    nvs->offset = flash_offset;
    nvs->ate_wra = flash_offset;
    nvs->data_wra = flash_offset + sizeof(struct nvs_ate);
    nvs->sector_size = sector_size;
    nvs->sector_count = sector_count;
    nvs->flash_parameters.write_block_size = NVS_FLASH_WRITE_BLOCK_SIZE;
    nvs->flash_parameters.erase_value = NVS_FLASH_ERASE_VALUE;

    nvs->flash_ops.flash_write = storage_flash_write;
    nvs->flash_ops.flash_read = storage_flash_read;
    nvs->flash_ops.flash_erase = storage_flash_erase4K;
    nvs->flash_ops.flash_write_protection_set = NULL;

    nvs->mutex_ops.lock = nvs_lock;
    nvs->mutex_ops.unlock = nvs_unlock;
    nvs->mutex = osMutexNew(NULL);

    ret = nvs_init(nvs);
    if (ret != 0) osMutexDelete(nvs->mutex);
    return ret;
}

void lfs_lock(void)
{
    osMutexAcquire(g_storage.lfs_mtx_id, osWaitForever);
}

void lfs_unlock(void)
{
    osMutexRelease(g_storage.lfs_mtx_id);
}

static void storageProcess(void *argument)
{
    storage_t *storage = (storage_t *)argument;

    int ret = lfs_mem_init(&storage->lfs_sys, FS_FLASH_OFFSET, FS_FLASH_SIZE , FS_FLASH_BLK, 10000, lfs_lock, lfs_unlock);
    if (ret != 0) printf("lfs_mem_init failed(ret = %d)...\r\n", ret);
    osSemaphoreRelease(storage->lfs_sem_id);
    
    while (storage->is_init) {
        if (osSemaphoreAcquire(storage->sem_id, osWaitForever) == osOK) {
        
        }
    }
    osThreadExit();
}

int storage_init(void *priv)
{
    int ret;
    storage_t *storage = (storage_t *)priv;
    storage->mtx_id = osMutexNew(NULL);
    storage->lfs_mtx_id = osMutexNew(NULL);
    storage->sem_id = osSemaphoreNew(1, 0, NULL);
    storage->lfs_sem_id = osSemaphoreNew(1, 0, NULL);
    storage->file_ops_handle = -1;
    
    common_flash_ops_t ops = {
        .flash_read = sysclk_nor_flash_read,
        .flash_write = sysclk_nor_flash_write,
        .flash_erase = sysclk_nor_flash_erase,
        .crc32 = sysclk_hal_crc32,
    };
    ret = fsbl_app_common_init(&ops);
    if(ret != 0){
        printf("fsbl_app_common_init failed...\r\n");
        return ret;
    }

    init_system_state(storage_flash_read, storage_flash_write, storage_flash_erase);

    ret = storage_nvs_init(&storage->nvs_fact, NVS_FACT_FLASH_OFFSET, NVS_FLASH_BLK, NVS_FACT_BLK_SIZE);
    if (ret != 0) {  //Try again after erasing
        printf("nvs_fact init failed(ret = %d), erasing and reboot...\r\n", ret);
        storage_flash_erase(NVS_FACT_FLASH_OFFSET, NVS_FACT_BLK_SIZE);
        osDelay(1000);
#if ENABLE_U0_MODULE
        u0_module_clear_wakeup_flag();
        u0_module_reset_chip_n6();
#endif
        HAL_NVIC_SystemReset();
        return ret;
    }

    ret = storage_nvs_init(&storage->nvs_user, NVS_USER_FLASH_OFFSET, NVS_FLASH_BLK, NVS_USER_BLK_SIZE);
    if (ret != 0) {  //Try again after erasing
        printf("nvs_user init failed(ret = %d), erasing and reboot...\r\n", ret);
        storage_flash_erase(NVS_USER_FLASH_OFFSET, NVS_USER_BLK_SIZE);
        osDelay(1000);
#if ENABLE_U0_MODULE
        u0_module_clear_wakeup_flag();
        u0_module_reset_chip_n6();
#endif
        HAL_NVIC_SystemReset();
        return ret;
    }

    storage->file_ops_handle = file_ops_register(FS_FLASH, &lfs_file_ops, &storage->lfs_sys);
    if (storage->file_ops_handle != -1) file_ops_switch(storage->file_ops_handle);

    storage->is_init = true;
    storage->storage_processId = osThreadNew(storageProcess, storage, &storageTask_attributes);
    return 0;
}

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data, size_t len)
{
    nvs_fs_t *nvs;
    if (!g_storage.is_init) {
        return -1;
    }

    if(type == NVS_FACTORY) {
        nvs = &g_storage.nvs_fact;
    }else if(type == NVS_USER) {
        nvs = &g_storage.nvs_user;
    }else {
        return -1;
    }

    if (nvs->ready != true) {
        return -1;
    }

    return nvs_write(nvs, key, data, len);
}

int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len)
{
    nvs_fs_t *nvs;
    if (!g_storage.is_init) {
        return -1;
    }

    if(type == NVS_FACTORY) {
        nvs = &g_storage.nvs_fact;
    }else if(type == NVS_USER) {
        nvs = &g_storage.nvs_user;
    }else {
        return -1;
    }

    return nvs_read(nvs, key, data, len);
}

int storage_nvs_delete(NVS_Type_t type, const char *key)
{
    nvs_fs_t *nvs;
    if (!g_storage.is_init) {
        return -1;
    }

    if(type == NVS_FACTORY) {
        nvs = &g_storage.nvs_fact;
    }else if(type == NVS_USER) {
        nvs = &g_storage.nvs_user;
    }else {
        return -1;
    }

    return nvs_delete(nvs, key);
}

int storage_nvs_clear(NVS_Type_t type)
{
    nvs_fs_t *nvs;
    if (!g_storage.is_init) {
        return -1;
    }

    if(type == NVS_FACTORY) {
        nvs = &g_storage.nvs_fact;
    }else if(type == NVS_USER) {
        nvs = &g_storage.nvs_user;
    }else {
        return -1;
    }

    return nvs_clear(nvs);
}

void storage_nvs_dump(NVS_Type_t type)
{
    nvs_fs_t *nvs;
    nvs_iterator_t it;
    struct nvs_ate info;
    if (!g_storage.is_init) {
        return;
    }
    
    if(type == NVS_FACTORY) {
        nvs = &g_storage.nvs_fact;
    }else if(type == NVS_USER) {
        nvs = &g_storage.nvs_user;
    }else {
        return;
    }

    if (nvs_entry_find(nvs, &it) != 0) {
        LOG_SIMPLE("No entry found\r\n");
        return;
    }
    while (nvs_entry_next(&it) == 0) {
        nvs_entry_info(&it, &info);
        if (info.len == 0) {
            continue; // Skip empty entries
        }
        char *buf = hal_mem_alloc_large(info.len + 1);
        memset(buf, 0, info.len + 1);
        int value_len = nvs_read(nvs, info.key, buf, info.len);
        if (value_len > 0)
            LOG_SIMPLE("Key: %s, Value: %s", info.key,buf);
        hal_mem_free(buf);
    }
    nvs_release_iterator(&it);
}

void storage_lock(void)
{
    osMutexAcquire(g_storage.mtx_id, osWaitForever);
}

void storage_unlock(void)
{
    osMutexRelease(g_storage.mtx_id);
}

void storage_format(void)
{
    if (g_storage.is_init != true) {
        return;
    }

    if (g_storage.lfs_sys.thread_safe && g_storage.lfs_sys.lock && g_storage.lfs_sys.unlock) {
        g_storage.lfs_sys.lock();
    }

    int err = lfs_format(&g_storage.lfs_sys.lfs, &g_storage.lfs_sys.config);
    if (err == LFS_ERR_OK) {
        // Remount after successful format
        err = lfs_mount(&g_storage.lfs_sys.lfs, &g_storage.lfs_sys.config);
        g_storage.lfs_sys.mounted = (err == LFS_ERR_OK);
    }
    /* Invalidate the free-space cache — used-block count is meaningless after
     * a format/remount. */
    g_fs_size_cache.valid = false;

    if (g_storage.lfs_sys.thread_safe && g_storage.lfs_sys.lock && g_storage.lfs_sys.unlock) {
        g_storage.lfs_sys.unlock();
    }

}

void storage_register(void)
{
    static dev_ops_t storage_ops = {
        .init = storage_init
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_storage.dev = dev;
    strcpy(dev->name, STROAGE_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &storage_ops;
    dev->priv_data = &g_storage;

    device_register(g_storage.dev);

}
#if 0
// Usage example
int main() {
    // Allocate memory space (simulating 512KB Flash)
    static uint8_t flash_mem[128 * 4096]; 
    lfs_mem_system_t fs_sys;
    
    // Initialize filesystem, set maximum erase cycles to 10000
    if (lfs_mem_init(&fs_sys, flash_mem, sizeof(flash_mem), 4096, 10000) != 0) {
        printf("Filesystem init failed\n");
        return -1;
    }

    // File operation demonstration
    void *fd = lfs_file_ops.fopen(&fs_sys, "/data.log", "w");
    if (fd < 0) {
        printf("Open file failed\n");
        return -1;
    }

    lfs_file_ops.fprintf(&fs_sys, fd, "System Start...\n");
    lfs_file_ops.fflush(&fs_sys, fd); // Ensure data is written
    
    // File rename demonstration
    if (lfs_file_ops.rename(&fs_sys, "/data.log", "/system.log") != 0) {
        printf("Rename failed\n");
    }

    lfs_file_ops.fclose(&fs_sys, fd);

    // Cleanup operations
    lfs_file_ops.remove(&fs_sys, "/system.log");
    
    // Unmount filesystem
    lfs_unmount(&fs_sys.lfs);
    hal_mem_free(fs_sys.mem_dev.erase_counts);
    return 0;
}
#endif