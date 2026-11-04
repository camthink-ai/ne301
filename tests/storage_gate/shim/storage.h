#ifndef SHIM_STORAGE_H
#define SHIM_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "storage_media_gate.h"
#include "nvs.h"

#define FLASH_BLOCK_SIZE 4096

typedef enum {
    NVS_FACTORY = 0,
    NVS_USER = 1
} NVS_Type_t;

int storage_nvs_write(NVS_Type_t type, const char *key, const void *data, size_t len);
int storage_nvs_read(NVS_Type_t type, const char *key, void *data, size_t len);
int storage_nvs_delete(NVS_Type_t type, const char *key);
int storage_nvs_clear(NVS_Type_t type);
storage_media_state_t storage_nvs_media_state(NVS_Type_t type);

int storage_format(void);
int storage_flash_read(uint32_t offset, void *data, size_t size);
void storage_lock_ext(void);
void storage_unlock_ext(void);

typedef struct {
    bool mounted;
    uint32_t total_KBytes;
    uint32_t free_KBytes;
    char fs_type[8];
} storage_disk_info_t;

int storage_get_disk_info(storage_disk_info_t *info);

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

#endif
