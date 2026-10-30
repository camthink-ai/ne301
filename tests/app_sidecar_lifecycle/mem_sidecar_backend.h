#ifndef MEM_SIDECAR_BACKEND_H
#define MEM_SIDECAR_BACKEND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "app_installer.h"

#define MEM_MAX_FILES 80
#define MEM_MAX_KEYS 12
#define MEM_MAX_VALUE 1024

typedef struct {
    char path[128];
    uint8_t *data;
    size_t len;
    aicam_bool_t present;
    int corrupt_reads;
} mem_file_t;

typedef struct {
    char key[48];
    uint8_t data[MEM_MAX_VALUE];
    size_t len;
    aicam_bool_t present;
} mem_kv_t;

typedef struct {
    mem_file_t files[MEM_MAX_FILES];
    mem_kv_t keys[MEM_MAX_KEYS];
    int tx_held;
    int fail_next_file_writes;
    int fail_next_file_writes_skip;
    int partial_next_file_writes;
    int file_write_calls;
    int fail_next_file_reads;
    int fail_next_file_removes;
    int fail_next_file_removes_skip;
    int fail_next_file_stats;
    int fail_next_kv_writes;
    int fail_next_kv_reads;
    int fail_next_kv_deletes;
    char fail_kv_read_key[48];
    int fail_kv_read_key_skip;
    char fail_kv_write_key[48];
    int fail_kv_write_key_skip;
    uint64_t free_bytes_value;
    aicam_bool_t free_bytes_enabled;
} mem_io_t;

static mem_io_t g_io;

static void mem_reset(void)
{
    size_t i;

    for (i = 0u; i < MEM_MAX_FILES; i++) {
        free(g_io.files[i].data);
    }
    memset(&g_io, 0, sizeof(g_io));
    g_io.free_bytes_value = 64u * 1024u * 1024u;
    g_io.free_bytes_enabled = AICAM_TRUE;
}

static mem_file_t *mem_find_file(const char *path)
{
    size_t i;

    for (i = 0u; i < MEM_MAX_FILES; i++) {
        if (g_io.files[i].present == AICAM_TRUE &&
            strcmp(g_io.files[i].path, path) == 0) {
            return &g_io.files[i];
        }
    }
    return NULL;
}

static mem_kv_t *mem_find_key(const char *key)
{
    size_t i;

    for (i = 0u; i < MEM_MAX_KEYS; i++) {
        if (g_io.keys[i].present == AICAM_TRUE &&
            strcmp(g_io.keys[i].key, key) == 0) {
            return &g_io.keys[i];
        }
    }
    return NULL;
}

static int count_files(void)
{
    int count = 0;
    size_t i;

    for (i = 0u; i < MEM_MAX_FILES; i++) {
        if (g_io.files[i].present == AICAM_TRUE) {
            count++;
        }
    }
    return count;
}

static app_install_io_result_t mem_file_write(void *ctx, const char *path,
                                              const uint8_t *data, size_t len)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_file_t *file;
    size_t stored_len = len;
    size_t i;

    io->file_write_calls++;
    if (io->fail_next_file_writes > 0) {
        if (io->fail_next_file_writes_skip > 0) {
            io->fail_next_file_writes_skip--;
        } else {
            io->fail_next_file_writes--;
            return APP_INSTALL_IO_ERROR;
        }
    }
    file = mem_find_file(path);
    if (file == NULL) {
        for (i = 0u; i < MEM_MAX_FILES; i++) {
            if (g_io.files[i].present != AICAM_TRUE) {
                file = &g_io.files[i];
                break;
            }
        }
    }
    if (file == NULL) {
        return APP_INSTALL_IO_ERROR;
    }
    if (io->partial_next_file_writes > 0) {
        io->partial_next_file_writes--;
        stored_len = len / 2u;
    }
    {
        uint8_t *copy = (uint8_t *)malloc(stored_len == 0u ? 1u : stored_len);
        if (copy == NULL) {
            return APP_INSTALL_IO_ERROR;
        }
        if (stored_len > 0u) {
            memcpy(copy, data, stored_len);
        }
        free(file->data);
        file->data = copy;
    }
    memset(file->path, 0, sizeof(file->path));
    snprintf(file->path, sizeof(file->path), "%s", path);
    file->len = stored_len;
    file->present = AICAM_TRUE;
    file->corrupt_reads = 0;
    return (stored_len == len) ? APP_INSTALL_IO_OK : APP_INSTALL_IO_ERROR;
}

static app_install_io_result_t mem_file_read(void *ctx, const char *path,
                                             uint8_t *buf, size_t cap, size_t *len)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_file_t *file;

    if (io->fail_next_file_reads > 0) {
        io->fail_next_file_reads--;
        return APP_INSTALL_IO_ERROR;
    }
    file = mem_find_file(path);
    if (file == NULL) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (file->len > cap) {
        return APP_INSTALL_IO_ERROR;
    }
    if (file->len > 0u) {
        memcpy(buf, file->data, file->len);
    }
    if (file->corrupt_reads > 0) {
        file->corrupt_reads--;
        if (file->len > 0u) {
            buf[0] ^= 0xFFu;
        }
    }
    *len = file->len;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t mem_file_remove(void *ctx, const char *path)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_file_t *file;

    if (io->fail_next_file_removes > 0) {
        if (io->fail_next_file_removes_skip > 0) {
            io->fail_next_file_removes_skip--;
        } else {
            io->fail_next_file_removes--;
            return APP_INSTALL_IO_ERROR;
        }
    }
    file = mem_find_file(path);
    if (file == NULL) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    file->present = AICAM_FALSE;
    free(file->data);
    file->data = NULL;
    file->len = 0u;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t mem_file_stat(void *ctx, const char *path, size_t *len)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_file_t *file;

    if (io->fail_next_file_stats > 0) {
        io->fail_next_file_stats--;
        return APP_INSTALL_IO_ERROR;
    }
    file = mem_find_file(path);
    if (file == NULL) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    *len = file->len;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t mem_kv_write(void *ctx, const char *key,
                                            const uint8_t *data, size_t len)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_kv_t *kv;
    size_t i;

    if (io->fail_next_kv_writes > 0) {
        io->fail_next_kv_writes--;
        return APP_INSTALL_IO_ERROR;
    }
    if (io->fail_kv_write_key[0] != '\0' && strcmp(key, io->fail_kv_write_key) == 0) {
        if (io->fail_kv_write_key_skip > 0) {
            io->fail_kv_write_key_skip--;
        } else {
            io->fail_kv_write_key[0] = '\0';
            return APP_INSTALL_IO_ERROR;
        }
    }
    if (len > MEM_MAX_VALUE) {
        return APP_INSTALL_IO_ERROR;
    }
    kv = mem_find_key(key);
    if (kv == NULL) {
        for (i = 0u; i < MEM_MAX_KEYS; i++) {
            if (g_io.keys[i].present != AICAM_TRUE) {
                kv = &g_io.keys[i];
                break;
            }
        }
    }
    if (kv == NULL) {
        return APP_INSTALL_IO_ERROR;
    }
    memset(kv->key, 0, sizeof(kv->key));
    snprintf(kv->key, sizeof(kv->key), "%s", key);
    memcpy(kv->data, data, len);
    kv->len = len;
    kv->present = AICAM_TRUE;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t mem_kv_read(void *ctx, const char *key,
                                           uint8_t *buf, size_t cap, size_t *len)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_kv_t *kv;

    if (io->fail_next_kv_reads > 0) {
        io->fail_next_kv_reads--;
        return APP_INSTALL_IO_ERROR;
    }
    if (io->fail_kv_read_key[0] != '\0' && strcmp(key, io->fail_kv_read_key) == 0) {
        if (io->fail_kv_read_key_skip > 0) {
            io->fail_kv_read_key_skip--;
        } else {
            io->fail_kv_read_key[0] = '\0';
            return APP_INSTALL_IO_ERROR;
        }
    }
    kv = mem_find_key(key);
    if (kv == NULL) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    if (kv->len > cap) {
        return APP_INSTALL_IO_ERROR;
    }
    memcpy(buf, kv->data, kv->len);
    *len = kv->len;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t mem_kv_delete(void *ctx, const char *key)
{
    mem_io_t *io = (mem_io_t *)ctx;
    mem_kv_t *kv;

    if (io->fail_next_kv_deletes > 0) {
        io->fail_next_kv_deletes--;
        return APP_INSTALL_IO_ERROR;
    }
    kv = mem_find_key(key);
    if (kv == NULL) {
        return APP_INSTALL_IO_NOT_FOUND;
    }
    kv->present = AICAM_FALSE;
    return APP_INSTALL_IO_OK;
}

static app_install_io_result_t mem_free_bytes(void *ctx, uint64_t *out)
{
    mem_io_t *io = (mem_io_t *)ctx;

    if (io->free_bytes_enabled != AICAM_TRUE) {
        return APP_INSTALL_IO_ERROR;
    }
    *out = io->free_bytes_value;
    return APP_INSTALL_IO_OK;
}

static aicam_bool_t mem_tx_begin(void *ctx)
{
    mem_io_t *io = (mem_io_t *)ctx;

    if (io->tx_held != 0) {
        return AICAM_FALSE;
    }
    io->tx_held = 1;
    return AICAM_TRUE;
}

static void mem_tx_end(void *ctx)
{
    mem_io_t *io = (mem_io_t *)ctx;

    io->tx_held = 0;
}

static void mem_io_init(app_install_io_t *io)
{
    memset(io, 0, sizeof(*io));
    io->ctx = &g_io;
    io->file_write = mem_file_write;
    io->file_read = mem_file_read;
    io->file_remove = mem_file_remove;
    io->file_stat = mem_file_stat;
    io->kv_write = mem_kv_write;
    io->kv_read = mem_kv_read;
    io->kv_delete = mem_kv_delete;
    io->free_bytes = mem_free_bytes;
    io->tx_begin = mem_tx_begin;
    io->tx_end = mem_tx_end;
}

#endif
