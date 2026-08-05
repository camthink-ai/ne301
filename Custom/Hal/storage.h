#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "lfs.h"
#include "generic_file.h"
#include "nvs.h"
#include "mem_map.h"
#include "fsbl_app_common.h"

#define FLASH_BLOCK_SIZE        4096
#define FS_BASE_MEM_START       FLASH_BASE

#define FS_FLASH_BLK            FLASH_BLOCK_SIZE
#define FS_FLASH_OFFSET         (LITTLEFS_BASE - FLASH_BASE)
#define FS_FLASH_SIZE           LITTLEFS_SIZE
#define FS_BLK_OFFSET           (FS_FLASH_OFFSET / FS_FLASH_BLK)

#define FS_LFS_CACHE_SIZE       1024
#define FS_LFS_LOOKAHEAD_SIZE   256
 
#define NVS_FLASH_BLK               FLASH_BLOCK_SIZE
#define NVS_FLASH_WRITE_BLOCK_SIZE 	4	/** Choose TYPEPROGAM from HAL. */
#define NVS_FLASH_ERASE_VALUE		0xFF

#define NVS_FACT_FLASH_OFFSET   (NVS_BASE - FLASH_BASE)
#define NVS_FACT_FLASH_SIZE     (32 * 1024)
#define NVS_FACT_BLK_SIZE       (NVS_FACT_FLASH_SIZE / NVS_FLASH_BLK)
#define NVS_USER_FLASH_OFFSET   (NVS_FACT_FLASH_OFFSET + NVS_FACT_FLASH_SIZE)
#define NVS_USER_FLASH_SIZE     (NVS_SIZE - NVS_FACT_FLASH_SIZE)
#define NVS_USER_BLK_SIZE       (NVS_USER_FLASH_SIZE / NVS_FLASH_BLK)


typedef void (*storage_lock_func_t)(void);
typedef void (*storage_unlock_func_t)(void);

typedef enum
{
    ERASE_4K = 0,                 /*!< 4K size Sector erase                          */
    ERASE_64K,                    /*!< 64K size Block erase                          */
    ERASE_BULK                    /*!< Whole bulk erase                              */
} STORAGE_Erase_t;
typedef enum
{
    NVS_FACTORY = 0,            /*!< Factory NVS storage                           */
    NVS_USER,                   /*!< User NVS storage                              */
} NVS_Type_t;

typedef struct {
    bool mounted;
    uint32_t total_KBytes;
    uint32_t free_KBytes;
    char fs_type[8]; // "littlefs"
} storage_disk_info_t;

typedef struct {
    uint32_t start_addr;
    size_t size;
    size_t block_size;
    size_t block_count;
    uint32_t *erase_counts;
    uint32_t max_erase;
} mem_block_dev_t;

typedef struct {
    lfs_t lfs;
    mem_block_dev_t mem_dev;
    struct lfs_config config;
    bool mounted;

    storage_lock_func_t lock;
    storage_unlock_func_t unlock;
    bool thread_safe;
} lfs_mem_system_t;

typedef struct {
    lfs_t *lfs;
    lfs_file_t file;
    bool is_open;
} lfs_file_handle_t;

typedef struct {
    lfs_t *lfs;
    lfs_dir_t dir;
    bool is_open;
} lfs_dir_handle_t;

typedef struct {
    bool is_init;
    device_t *dev;
    lfs_mem_system_t lfs_sys;
    nvs_fs_t nvs_fact;
    nvs_fs_t nvs_user;
    osMutexId_t mtx_id;
    osMutexId_t lfs_mtx_id;
    osSemaphoreId_t sem_id;
    osSemaphoreId_t lfs_sem_id;
    osThreadId_t storage_processId;
    int file_ops_handle;
} storage_t;

void *flash_lfs_fopen(const char *path, const char *mode);
int flash_lfs_fclose(void *fd);
int flash_lfs_fwrite(void *fd, const void *buf, size_t size);
int flash_lfs_fread(void *fd, void *buf, size_t size);
int flash_lfs_remove(const char *path);
int flash_lfs_rename(const char *oldpath, const char *newpath);
long flash_lfs_ftell(void *fd);
int flash_lfs_fseek(void *fd, long offset, int whence);
int flash_lfs_fflush(void *fd);
void *flash_lfs_opendir(const char *path);
int flash_lfs_readdir(void *dd, char *info);
int flash_lfs_closedir(void *dd);
int flash_lfs_stat(const char *filename, struct stat *st);

int flash_lfs_mkdir(const char *path);

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data, size_t len);
int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len);
int storage_nvs_delete(NVS_Type_t type, const char *key);
int storage_nvs_clear(NVS_Type_t type);
void storage_nvs_dump(NVS_Type_t type);

int storage_flash_write(uint32_t offset, void *data, size_t size);
int storage_flash_read(uint32_t offset, void *data, size_t size);
int storage_flash_erase(uint32_t offset, size_t num_blk);
int storage_get_disk_info(storage_disk_info_t *info);
/* Lightweight mounted check — just reads the RAM flag, NO lfs_fs_size traverse.
 * Use this instead of storage_get_disk_info() when you only need to know if the
 * littlefs volume is mounted (e.g. readiness probes in hot paths). */
bool storage_is_lfs_mounted(void);
void storage_lock(void);
void storage_unlock(void);
void storage_format(void);
int storage_file_ops_switch(void);
void storage_register(void);

#endif