/**
 ******************************************************************************
 * @file    mem_slab.h
 * @author  Application Team
 * @brief   HAL memory pool with slab allocator header file
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

#ifndef __MEM_SLAB_H
#define __MEM_SLAB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Configuration parameters */
#define MEM_INTERNAL_SIZE   (512 * 1024)        /* 512KB internal pool */
#if defined(BOARD_PSRAM_SIZE) && BOARD_PSRAM_SIZE == 64
/* External PSRAM pool: keep below APP_EXT (56MB) to leave room for other IN_PSRAM data */
#define MEM_EXTERNAL_SIZE   (52 * 1024 * 1024)  /* 52MB external pool */
#else
#define MEM_EXTERNAL_SIZE   (24 * 1024 * 1024)  /* 24MB external pool */
#endif
#define MEM_SLAB_SMALL_THRESHOLD    4096    /* Small allocation threshold */

/* Memory allocation preferences */
typedef enum {
    MEM_FAST = 0,       /* Prefer fast internal RAM */
    MEM_LARGE,          /* Prefer large external PSRAM */
    MEM_ANY             /* Any available memory */
} mem_type_t;

/* Error codes */
#define MEM_OK              0
#define MEM_ERROR          -1
#define MEM_NO_MEMORY      -2
#define MEM_INVALID_PARAM  -3

/* Core API Functions */

/**
 * @brief Initialize HAL memory pool system
 * @param internal_base Base address for internal RAM pool (NULL for auto)
 * @param external_base Base address for external PSRAM pool (NULL for auto)
 * @return MEM_OK on success, error code otherwise
 */
int32_t hal_mem_init(void *internal_base, void *external_base);

/**
 * @brief Deinitialize HAL memory pool system
 */
void hal_mem_deinit(void);

/**
 * @brief Allocate memory with preference
 * @param size Size to allocate
 * @param type Memory type preference
 * @return Pointer to allocated memory, NULL on failure
 */
void *hal_mem_alloc(size_t size, mem_type_t type);

/**
 * @brief Allocate zero-initialized memory
 * @param nmemb Number of elements
 * @param size Size of each element
 * @param type Memory type preference
 * @return Pointer to allocated memory, NULL on failure
 */
void *hal_mem_calloc(size_t nmemb, size_t size, mem_type_t type);

/**
 * @brief Allocate aligned memory
 * @param size Size to allocate
 * @param alignment Alignment
 * @param type Memory type preference
 * @return Pointer to allocated memory, NULL on failure
 */
void *hal_mem_alloc_aligned(size_t size, uint32_t alignment, mem_type_t type);
/**
 * @brief Free memory (auto-detects pool)
 * @param ptr Pointer to memory to free
 */
void hal_mem_free(void *ptr);


void *hal_mem_realloc(void *ptr, size_t size, mem_type_t type);

/* Statistics and Information */

/**
 * @brief Get memory information
 * @param total_size Total size of the memory pool
 * @param used_size Used size of the memory pool
 * @param type Memory type preference
 * @return MEM_OK on success, error code otherwise
 */
int32_t hal_mem_get_info(uint32_t *total_size, uint32_t *used_size, mem_type_t type);

/**
 * @brief Get memory usage statistics
 * @return MEM_OK on success, error code otherwise
 */
int32_t hal_mem_get_stats(void);

/**
 * @brief Check if pointer is from internal RAM pool
 * @param ptr Pointer to check
 * @return true if from internal pool, false otherwise
 */
bool hal_mem_is_internal(void *ptr);

/**
 * @brief Check if pointer is from external PSRAM pool
 * @param ptr Pointer to check
 * @return true if from external pool, false otherwise
 */
bool hal_mem_is_external(void *ptr);

/* Convenience macros for common allocations */
/**
 * @brief Allocate fast memory (internal RAM)
 * @param size Size to allocate
 * @return Pointer to allocated memory, NULL on failure
 */
#define hal_mem_alloc_fast(size) \
    hal_mem_alloc((size), MEM_FAST)

/**
 * @brief Allocate large memory (external PSRAM)
 * @param size Size to allocate
 * @return Pointer to allocated memory, NULL on failure
 */
#define hal_mem_alloc_large(size) \
    hal_mem_alloc((size), MEM_LARGE)


#define hal_mem_realloc_large(ptr, size) \
    hal_mem_realloc(ptr, size, MEM_LARGE)

/**
 * @brief Allocate any available memory
 * @param size Size to allocate
 * @return Pointer to allocated memory, NULL on failure
 */
#define hal_mem_alloc_any(size) \
    hal_mem_alloc((size), MEM_ANY)

/**
 * @brief Allocate fast zero-initialized memory
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, NULL on failure
 */
#define hal_mem_calloc_fast(nmemb, size) \
    hal_mem_calloc((nmemb), (size), MEM_FAST)

/**
 * @brief Allocate large zero-initialized memory
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return Pointer to allocated memory, NULL on failure
 */
#define hal_mem_calloc_large(nmemb, size) \
    hal_mem_calloc((nmemb), (size), MEM_LARGE)

/* Module registration functions */

/**
 * @brief Register memory module with device manager
 */
void hal_mem_register(void);

/**
 * @brief Unregister memory module from device manager
 */
void hal_mem_unregister(void);

#ifdef __cplusplus
}
#endif

#endif /* __MEM_SLAB_H */
