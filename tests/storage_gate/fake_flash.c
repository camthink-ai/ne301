#include "fake_flash.h"
#include <string.h>

static fake_flash_t *g_ff;

void ff_init(fake_flash_t *ff, uint8_t *mem, size_t size)
{
    ff->mem = mem;
    ff->size = size;
    ff->read_calls = 0;
    ff->write_calls = 0;
    ff->erase_calls = 0;
    ff->fail_next_reads = 0;
    ff->fail_next_writes = 0;
    ff->fail_next_erases = 0;
    memset(mem, 0xFF, size);
    g_ff = ff;
}

static int range_ok(uint32_t offset, size_t len)
{
    if (!g_ff) {
        return 0;
    }
    return ((size_t)offset < g_ff->size) && ((size_t)offset + len <= g_ff->size);
}

int ff_nvs_read(uint32_t offset, void *data, size_t len)
{
    if (!g_ff || !range_ok(offset, len)) {
        return -1;
    }
    g_ff->read_calls++;
    if (g_ff->fail_next_reads > 0) {
        g_ff->fail_next_reads--;
        return -5;
    }
    memcpy(data, g_ff->mem + offset, len);
    return 0;
}

int ff_nvs_write(uint32_t offset, void *data, size_t len)
{
    if (!g_ff || !range_ok(offset, len)) {
        return -1;
    }
    g_ff->write_calls++;
    if (g_ff->fail_next_writes > 0) {
        g_ff->fail_next_writes--;
        return -5;
    }
    for (size_t i = 0; i < len; i++) {
        if ((g_ff->mem[offset + i] & ((const uint8_t *)data)[i]) != ((const uint8_t *)data)[i]) {
            return -2;
        }
    }
    memcpy(g_ff->mem + offset, data, len);
    return 0;
}

int ff_nvs_erase(uint32_t offset, size_t size)
{
    if (!g_ff || !range_ok(offset, size)) {
        return -1;
    }
    g_ff->erase_calls++;
    if (g_ff->fail_next_erases > 0) {
        g_ff->fail_next_erases--;
        return -5;
    }
    memset(g_ff->mem + offset, 0xFF, size);
    return 0;
}

static fake_flash_t *ff_from_cfg(const struct lfs_config *c)
{
    return (fake_flash_t *)c->context;
}

int ff_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                void *data, lfs_size_t size)
{
    fake_flash_t *ff = ff_from_cfg(c);
    uint32_t offset = block * c->block_size + off;
    if (!range_ok(offset, size)) {
        return LFS_ERR_IO;
    }
    ff->read_calls++;
    if (ff->fail_next_reads > 0) {
        ff->fail_next_reads--;
        return LFS_ERR_IO;
    }
    memcpy(data, ff->mem + offset, size);
    return LFS_ERR_OK;
}

int ff_lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                const void *data, lfs_size_t size)
{
    fake_flash_t *ff = ff_from_cfg(c);
    uint32_t offset = block * c->block_size + off;
    if (!range_ok(offset, size)) {
        return LFS_ERR_IO;
    }
    ff->write_calls++;
    if (ff->fail_next_writes > 0) {
        ff->fail_next_writes--;
        return LFS_ERR_IO;
    }
    for (lfs_size_t i = 0; i < size; i++) {
        if ((ff->mem[offset + i] & ((const uint8_t *)data)[i]) != ((const uint8_t *)data)[i]) {
            return LFS_ERR_CORRUPT;
        }
    }
    memcpy(ff->mem + offset, data, size);
    return LFS_ERR_OK;
}

int ff_lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
    fake_flash_t *ff = ff_from_cfg(c);
    uint32_t offset = block * c->block_size;
    if (!range_ok(offset, c->block_size)) {
        return LFS_ERR_IO;
    }
    ff->erase_calls++;
    if (ff->fail_next_erases > 0) {
        ff->fail_next_erases--;
        return LFS_ERR_IO;
    }
    memset(ff->mem + offset, 0xFF, c->block_size);
    return LFS_ERR_OK;
}

int ff_lfs_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}
