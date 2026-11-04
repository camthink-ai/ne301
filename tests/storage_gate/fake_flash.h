#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include "lfs.h"

typedef struct {
    uint8_t *mem;
    size_t size;
    int read_calls;
    int write_calls;
    int erase_calls;
    int fail_next_reads;
    int fail_next_writes;
    int fail_next_erases;
} fake_flash_t;

void ff_init(fake_flash_t *ff, uint8_t *mem, size_t size);
int ff_nvs_read(uint32_t offset, void *data, size_t len);
int ff_nvs_write(uint32_t offset, void *data, size_t len);
int ff_nvs_erase(uint32_t offset, size_t size);
int ff_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                void *data, lfs_size_t size);
int ff_lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                const void *data, lfs_size_t size);
int ff_lfs_erase(const struct lfs_config *c, lfs_block_t block);
int ff_lfs_sync(const struct lfs_config *c);

#endif
