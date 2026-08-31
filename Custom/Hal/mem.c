/**
 ******************************************************************************
 * @file    mem_slab.c
 * @author  Application Team
 * @brief   HAL memory pool with slab allocator implementation
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "mem.h"
#include "debug.h"
#include "common_utils.h"
#include "dev_manager.h"
#include "pwr.h"
#include "cmsis_os2.h"
#include "mpool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Static memory pool buffers */
static uint8_t internal_slab_buffer[MEM_INTERNAL_SIZE] ALIGN_32 SRAM_POOL;
static uint8_t external_slab_buffer[MEM_EXTERNAL_SIZE] ALIGN_32 IN_PSRAM;

typedef struct {
    osMutexId_t mtx_id;
    uint32_t  pageSize;
    void    *pool;
    void    *addr;
    const char    *name;
} mem_handle_s;

typedef mem_handle_s *mem_handle_t;

/* Slab pool handles */
static mem_handle_t g_internal_mem_handle = NULL;
static mem_handle_t g_external_mem_handle = NULL;
static bool g_slab_pools_initialized = false;

/* Memory module structure */
typedef struct {
    device_t *dev;
    bool is_init;
    osMutexId_t mtx_id;
    PowerHandle pwr_handle;
} mem_module_t;

static mem_module_t g_mem_module = {0};

#define MEM_LOCK(handle) osMutexAcquire(handle->mtx_id, osWaitForever)
#define MEM_UNLOCK(handle) osMutexRelease(handle->mtx_id)

static mem_handle_t mem_pool_create(void *base_addr, uint32_t size, const char *name)
{
    mem_handle_t handle = malloc(sizeof(mem_handle_s));
    if (handle == NULL) {
        return NULL;
    }
    handle->mtx_id = osMutexNew(NULL);
    handle->addr = base_addr;

    ngx_slab_pool_t *sp = (ngx_slab_pool_t *)base_addr;
    sp->addr = base_addr;
    sp->min_shift = 3;
    sp->end = base_addr + size;
    ngx_slab_init(sp);
    handle->pool = sp;
    handle->pageSize = sp->page_size;
    handle->name = name;
    return handle;
}

static void mem_pool_destroy(mem_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    osMutexDelete(handle->mtx_id);
}

static void *mem_pool_alloc(mem_handle_t handle, size_t size)
{
    if (handle == NULL) {
        return NULL;
    }
    void *p = NULL;
    MEM_LOCK(handle);
    p = ngx_slab_alloc(handle->pool, size);
    MEM_UNLOCK(handle);

    return p;
}

static void *mem_pool_alloc_aligned(mem_handle_t handle, size_t size, size_t alignment)
{
    if (handle == NULL) {
        return NULL;
    }
    void *p = NULL;
    MEM_LOCK(handle);
    p = ngx_slab_alloc_aligned(handle->pool, size, alignment);
    MEM_UNLOCK(handle);

    return p;
}

static int32_t mem_pool_free(mem_handle_t handle, void *p)
{
    if (handle == NULL) {
        return -1;
    }
    int32_t ret = 0;
    MEM_LOCK(handle);
    ret = ngx_slab_free(handle->pool, p);
    MEM_UNLOCK(handle);

    return ret;
}

static void mem_pool_stat(mem_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    MEM_LOCK(handle);
    ngx_slab_stat(handle->pool, NULL);
    MEM_UNLOCK(handle);
}

static bool mem_pool_contains(mem_handle_t handle, void *ptr)
{
    if (handle == NULL) {
        return false;
    }
    return ngx_slab_contains(handle->pool, ptr);
}

static bool  mem_pool_validate(mem_handle_t handle)
{
    if (handle == NULL) {
        return false;
    }
    return ngx_slab_validate(handle->pool);
}

static void mem_pool_status(mem_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    printf("Pool status: ----------------%s----------------\r\n", handle->name);
    ngx_slab_stat(handle->pool, NULL);
}

/* Public API Implementation */

int32_t hal_mem_init(void *internal_base, void *external_base)
{
    if (g_slab_pools_initialized) {
        hal_mem_deinit();
    }

    void *internal_addr = internal_base ? internal_base : internal_slab_buffer;
    void *external_addr = external_base ? external_base : external_slab_buffer;

    /* Initialize internal slab pool */
    g_internal_mem_handle = mem_pool_create(internal_addr, MEM_INTERNAL_SIZE, "internal");
    if (g_internal_mem_handle == NULL) {
        LOG_DRV_ERROR("Failed to initialize internal slab pool\r\n");
        return MEM_ERROR;
    }

    /* Initialize external slab pool */
    g_external_mem_handle = mem_pool_create(external_addr, MEM_EXTERNAL_SIZE, "external");
    if (g_external_mem_handle == NULL) {
        LOG_DRV_ERROR("Failed to initialize external slab pool\r\n");
        mem_pool_destroy(g_internal_mem_handle);
        g_internal_mem_handle = NULL;
        return MEM_ERROR;
    }

    g_slab_pools_initialized = true;
    // printf("Slab memory pools initialized successfully\r\n");

    return MEM_OK;
}

void hal_mem_deinit(void)
{
    if (!g_slab_pools_initialized) {
        return;
    }

    mem_pool_destroy(g_internal_mem_handle);
    g_internal_mem_handle = NULL;
    mem_pool_destroy(g_external_mem_handle);
    g_external_mem_handle = NULL;

    g_slab_pools_initialized = false;

    printf("Slab memory pools deinitialized\r\n");
}

void *hal_mem_alloc(size_t size, mem_type_t type)
{
    if (!g_slab_pools_initialized || size == 0) {
        return NULL;
    }

    void *ptr = NULL;

    switch (type) {
        case MEM_FAST:
            if (g_internal_mem_handle) {
                ptr = mem_pool_alloc(g_internal_mem_handle, size);
            }
            break;

        case MEM_LARGE:
            if (g_external_mem_handle) {
                ptr = mem_pool_alloc(g_external_mem_handle, size);
            }
            break;

        case MEM_ANY:
        default:
            /* Choose based on size - small allocations go to internal */
            if (size <= MEM_SLAB_SMALL_THRESHOLD && g_internal_mem_handle) {
                ptr = mem_pool_alloc(g_internal_mem_handle, size);
                if (!ptr && g_external_mem_handle) {
                    ptr = mem_pool_alloc(g_external_mem_handle, size);
                }
            } else if (g_external_mem_handle) {
                ptr = mem_pool_alloc(g_external_mem_handle, size);
                if (!ptr && g_internal_mem_handle) {
                    ptr = mem_pool_alloc(g_internal_mem_handle, size);
                }
            }
            break;
    }

    return ptr;
}

void *hal_mem_calloc(size_t nmemb, size_t size, mem_type_t type)
{
    size_t total_size = nmemb * size;

    /* Check for overflow */
    if (nmemb != 0 && total_size / nmemb != size) {
        return NULL;
    }

    void *ptr = hal_mem_alloc(total_size, type);
    if (ptr) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

void *hal_mem_alloc_aligned(size_t size, uint32_t alignment, mem_type_t type)
{
    if (!g_slab_pools_initialized || size == 0) {
        return NULL;
    }
    switch (type) {
        case MEM_FAST:
            if (g_internal_mem_handle) {
                return mem_pool_alloc_aligned(g_internal_mem_handle, size, alignment);
            }
            break;
        case MEM_LARGE:
            if (g_external_mem_handle) {
                return mem_pool_alloc_aligned(g_external_mem_handle, size, alignment);
            }
            break;
        case MEM_ANY:
        default:
            /* Choose based on size - small allocations go to internal */
            if (size <= MEM_SLAB_SMALL_THRESHOLD && g_internal_mem_handle) {
                return mem_pool_alloc_aligned(g_internal_mem_handle, size, alignment);
            } else if (g_external_mem_handle) {
                return mem_pool_alloc_aligned(g_external_mem_handle, size, alignment);
            }
            break;
    }
    return NULL;
}

void hal_mem_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    if (g_external_mem_handle && mem_pool_contains(g_external_mem_handle, ptr)) {
        mem_pool_free(g_external_mem_handle, ptr);
    } else if (g_internal_mem_handle && mem_pool_contains(g_internal_mem_handle, ptr)) {
        mem_pool_free(g_internal_mem_handle, ptr);
    } else {
        free(ptr);
    }
}

void *hal_mem_realloc(void *ptr, size_t size, mem_type_t type)
{
    void *new_ptr = NULL;
    if (ptr == NULL) {
        return hal_mem_alloc(size, type);
    }
    if (size == 0) {
        hal_mem_free(ptr);
        return NULL;
    }
    new_ptr = hal_mem_alloc(size, type);
    if (new_ptr == NULL) {
        return NULL;
    }
    hal_mem_free(ptr);
    return new_ptr;
}

int32_t hal_mem_get_info(uint32_t *total_size, uint32_t *used_size, mem_type_t type)
{
    ngx_slab_info_t stat_info;
    if (!g_slab_pools_initialized || total_size == NULL || used_size == NULL) {
        return MEM_INVALID_PARAM;
    }
    
    switch (type) {
        case MEM_FAST:
            if (g_internal_mem_handle) {
                MEM_LOCK(g_internal_mem_handle);
                ngx_slab_stat(g_internal_mem_handle->pool, &stat_info);
                *total_size = stat_info.pool_size;
                *used_size = stat_info.used_size;
                MEM_UNLOCK(g_internal_mem_handle);
            }
            break;

        case MEM_LARGE:
            if (g_external_mem_handle) {
                MEM_LOCK(g_external_mem_handle);
                ngx_slab_stat(g_external_mem_handle->pool, &stat_info);
                *total_size = stat_info.pool_size;
                *used_size = stat_info.used_size;
                MEM_UNLOCK(g_external_mem_handle);
            }
            break;
        case MEM_ANY:
        default:
            *total_size = 0;
            *used_size = 0;
            if (g_internal_mem_handle) {
                MEM_LOCK(g_internal_mem_handle);
                ngx_slab_stat(g_internal_mem_handle->pool, &stat_info);
                *total_size += stat_info.pool_size;
                *used_size += stat_info.used_size;
                MEM_UNLOCK(g_internal_mem_handle);
            }
            if (g_external_mem_handle) {
                MEM_LOCK(g_external_mem_handle);
                ngx_slab_stat(g_external_mem_handle->pool, &stat_info);
                *total_size += stat_info.pool_size;
                *used_size += stat_info.used_size;
                MEM_UNLOCK(g_external_mem_handle);
            }
            break;
    }
    return MEM_OK;
}

int32_t hal_mem_get_stats(void)
{
    if (!g_slab_pools_initialized) {
        return MEM_INVALID_PARAM;
    }
    mem_pool_stat(g_internal_mem_handle);
    mem_pool_stat(g_external_mem_handle);
    return MEM_OK;
}

bool hal_mem_is_internal(void *ptr)
{
    if (!g_slab_pools_initialized || ptr == NULL || !g_internal_mem_handle) {
        return false;
    }

    return mem_pool_contains(g_internal_mem_handle, ptr);
}

bool hal_mem_is_external(void *ptr)
{
    if (!g_slab_pools_initialized || ptr == NULL || !g_external_mem_handle) {
        return false;
    }

    return mem_pool_contains(g_external_mem_handle, ptr);
}


bool hal_mem_is_initialized(void)
{
    return g_slab_pools_initialized;
}

/* Command handler for slab memory pool testing */
static int hal_mem_slab_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: slab <status|stats|validate|test|reset|info|optimal>\r\n");
        LOG_SIMPLE("  status   - Print slab memory pool status\r\n");
        LOG_SIMPLE("  validate - Validate slab pool integrity\r\n");
        LOG_SIMPLE("  aligned    - Allocate aligned memory test\r\n");
        return -1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "status") == 0) {
        mem_pool_status(g_internal_mem_handle);
        mem_pool_status(g_external_mem_handle);
    } else if (strcmp(cmd, "validate") == 0) {
        LOG_SIMPLE("Internal pool validate: %d\r\n", mem_pool_validate(g_internal_mem_handle));
        LOG_SIMPLE("External pool validate: %d\r\n", mem_pool_validate(g_external_mem_handle));
    } else if (strcmp(cmd, "aligned") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("Usage: aligned <size> <alignment>\r\n");
            return -1;
        }
        size_t size = strtoul(argv[2], NULL, 10);
        size_t alignment = strtoul(argv[3], NULL, 10);
        void *p1 = hal_mem_alloc_aligned(size, alignment, MEM_ANY);
        void *p2 = hal_mem_alloc_aligned(size, alignment, MEM_FAST);
        void *p3 = hal_mem_alloc_aligned(size, alignment, MEM_LARGE);
        LOG_SIMPLE("Allocated aligned memory: %p\r\n", p1);
        LOG_SIMPLE("Allocated aligned memory: %p\r\n", p2);
        LOG_SIMPLE("Allocated aligned memory: %p\r\n", p3);
        LOG_SIMPLE("After allocation:\r\n");
        hal_mem_get_stats();
        hal_mem_free(p1);
        hal_mem_free(p2);
        hal_mem_free(p3);
        LOG_SIMPLE("After free:\r\n");
        hal_mem_get_stats();
    } else {
        LOG_SIMPLE("Usage: aligned <size> <alignment>\r\n");
    }
    return 0;
}

debug_cmd_reg_t mem_cmd_table[] = {
    {"mpool", "Memory pool management", hal_mem_slab_cmd},
};

// Command registration callback
static void hal_mem_cmd_register(void)
{
    debug_cmdline_register(mem_cmd_table, sizeof(mem_cmd_table) / sizeof(mem_cmd_table[0]));
}

/* Device operations for slab memory module */
static int hal_mem_module_ioctl(void *priv, unsigned int cmd, unsigned char *ubuf, unsigned long arg)
{
    mem_module_t *mem_mod = (mem_module_t *)priv;
    if (!mem_mod->is_init) {
        return -1;
    }

    osMutexAcquire(mem_mod->mtx_id, osWaitForever);

    // Handle slab memory-specific ioctl commands here if needed
    switch (cmd) {
        default:
            break;
    }

    osMutexRelease(mem_mod->mtx_id);
    return 0;
}

static int hal_mem_module_init(void *priv)
{
    LOG_DRV_DEBUG("mem_module_init\r\n");
    mem_module_t *mem_mod = (mem_module_t *)priv;

    mem_mod->mtx_id = osMutexNew(NULL);
    if (mem_mod->mtx_id == NULL) {
        LOG_DRV_ERROR("Failed to create slab mem mutex\r\n");
        return -1;
    }

    /* Initialize slab memory pools */
    if (hal_mem_init(NULL, NULL) != MEM_OK) {
        LOG_DRV_ERROR("Slab memory pool initialization failed\r\n");
        osMutexDelete(mem_mod->mtx_id);
        mem_mod->mtx_id = NULL;
        return -1;
    }

    mem_mod->is_init = true;
    // printf("Slab memory pools initialized successfully\r\n");
    return 0;
}

static int hal_mem_module_deinit(void *priv)
{
    mem_module_t *mem_mod = (mem_module_t *)priv;

    mem_mod->is_init = false;

    /* Deinitialize slab memory pools */
    hal_mem_deinit();

    if (mem_mod->mtx_id != NULL) {
        osMutexDelete(mem_mod->mtx_id);
        mem_mod->mtx_id = NULL;
    }

    LOG_DRV_DEBUG("Slab memory module deinitialized\r\n");
    return 0;
}

void hal_mem_register(void)
{
    static dev_ops_t mem_ops = {
        .init = hal_mem_module_init,
        .deinit = hal_mem_module_deinit,
        .ioctl = hal_mem_module_ioctl
    };

    device_t *dev = malloc(sizeof(device_t));
    if (dev == NULL) {
        LOG_DRV_ERROR("Failed to allocate memory device\r\n");
        return;
    }

    g_mem_module.dev = dev;
    strcpy(dev->name, "mpool");
    dev->type = DEV_TYPE_MISC;
    dev->ops = &mem_ops;
    dev->priv_data = &g_mem_module;

    device_register(g_mem_module.dev);
    driver_cmd_register_callback("mpool", hal_mem_cmd_register);

    // printf("Memory module registered\r\n");
}

void hal_mem_unregister(void)
{
    if (g_mem_module.dev != NULL) {
        device_unregister(g_mem_module.dev);
        free(g_mem_module.dev);
        g_mem_module.dev = NULL;
    }
    // LOG_DRV_DEBUG("Memory module unregistered\r\n");
}
