/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @ingroup MMHAL
 * @defgroup MMHAL_FLASH Morse Micro Flash Hardware Abstraction Layer (mmhal_flash) API
 *
 * This API provides abstraction from the underlying flash hardware.
 *
 * @note This API is not used by morselib.
 * @{
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * This is the value erased flash bytes are set to. This shall be @c 0xFF as this is the
 * value that hardware flash erases to.
 */
#define MMHAL_FLASH_ERASE_VALUE 0xFF

/** LittleFS configuration structure. Include @c lfs.h for definition. */
struct lfs_config;

/**
 * Underlying integer type for @ref mmhal_flash_addr_t. Defaults to
 * @c uint32_t. May be overridden by the build system (e.g. by defining
 * @c MMHAL_FLASH_ADDR_T on the compiler command line) if a platform requires
 * a wider integer type to represent a flash address.
 */
#ifndef MMHAL_FLASH_ADDR_T
#define MMHAL_FLASH_ADDR_T uint32_t
#endif

/** Address type used by the mmhal flash API. */
typedef MMHAL_FLASH_ADDR_T mmhal_flash_addr_t;

/**
 * Flash partition configuration structure.
 *
 * This should be initialized using @c MMHAL_FLASH_PARTITION_CONFIG_DEFAULT. For example:
 *
 * @code{.c}
 * struct mmhal_flash_partition_config partition = MMHAL_FLASH_PARTITION_CONFIG_DEFAULT;
 * @endcode
 */
struct mmhal_flash_partition_config
{
    /**
     * The start address of the partition. If @c not_memory_mapped is false, then this must
     * be a directly readable address within the CPU's memory map. If @c not_memory_mapped
     * is true, then this address is implementation defined.
     *
     * Regardless of the value @c not_memory_mapped, the partition address range defined by
     * @c partition_start and @c partition_size must be understood by @c mmhal_flash_erase(),
     * @c mmhal_flash_getblocksize(), @c mmhal_flash_read(), and @c mmhal_flash_write().
     */
    mmhal_flash_addr_t partition_start;

    /** The size of the partition (in bytes). */
    uint32_t partition_size;

    /**
     * If @c false, then the partition address range (`partition_start` to
     * `partition_start + partition_size`) must be directly readable within the CPU's memory map.
     * If @c true, then the partition is not memory mapped and its contents can only be read using
     * @ref mmhal_flash_read().
     */
    bool not_memory_mapped;
};

/** Initial values for @ref mmhal_flash_partition_config. */
#define MMHAL_FLASH_PARTITION_CONFIG_DEFAULT { 0, 0, false }

/**
 * Get MMCONFIG flash partition configuration.
 *
 * MMCONFIG initialization is done by @c mmconfig_init() in @c mmconfig.c,
 * which in turn calls this function to fetch the partition configuration for config store
 * from the HAL layer. If config store is not supported by the platform then we just
 * return NULL. This function returns a static pointer to
 * @c struct @c mmhal_flash_partition_config.
 *
 * Because primary/secondary partitions are used in the current implementation,
 * the available config storage is half of the provided size.
 *
 * @return A static pointer to the partition config for MMCONFIG, or NULL if not supported.
 */
const struct mmhal_flash_partition_config *mmhal_get_mmconfig_partition(void);

/**
 * Get optional factory default MMCONFIG flash partition configuration.
 *
 * This partition uses the same on-flash format as a single MMCONFIG image, but is treated as
 * read-only by @c mmconfig. If present and valid, values in this partition are used as fall-backs
 * when a key is absent from the writable MMCONFIG partition. Platforms that do not provide a
 * factory default partition may omit this function; a weak default implementation returns NULL.
 *
 * Note that unlike @c mmhal_get_mmconfig_partition(), the size here is used only for a
 * single partition. Therefore it would be normal to define a factory partition that is
 * half the size (or less) of the normal config partition, and it is not necessary to
 * align it to a flash sector as it is not erased.
 *
 * @return A static pointer to the factory MMCONFIG partition config, or NULL if not supported.
 */
const struct mmhal_flash_partition_config *mmhal_get_factory_mmconfig_partition(void);

/**
 * Erases a specified block of flash.
 *
 * The given @p block_address may be anywhere within the block to erase -- the entire block will
 * be erased. Once erased all bytes in the block shall be @c MMHAL_FLASH_ERASE_VALUE.
 *
 * @param block_address The address of the block of flash to erase. Addresses are implementation
 *                      dependent, but should be within a range defined by a given
 *                      @c mmhal_flash_partition_config.
 * @return              0 on success, negative number on failure
 */
int mmhal_flash_erase(mmhal_flash_addr_t block_address);

/**
 * Returns the size of the flash block at the specified address.
 *
 * @param block_address The address of the flash block. Addresses are implementation
 *                      dependent, but should be within a range defined by a given
 *                      @c mmhal_flash_partition_config.
 * @return              The size of the Flash block in bytes.
 *                      Returns 0 if an invalid address is specified.
 */
uint32_t mmhal_flash_getblocksize(mmhal_flash_addr_t block_address);

/**
 * Read a block of data from the specified Flash address into the buffer.
 *
 * @param read_address  The address to read from. Addresses are implementation
 *                      dependent, but should be within a range defined by a given
 *                      @c mmhal_flash_partition_config.
 * @param buf           The buffer to read into.
 * @param size          The number of bytes to read.
 * @return              0 on success, or a negative number on failure.
 */
int mmhal_flash_read(mmhal_flash_addr_t read_address, uint8_t *buf, size_t size);

/**
 * Write a block of data to the specified Flash address.
 *
 * There is no alignment or minimum size requirement.  This function will
 * take care of aligning the data and merging with existing Flash contents.
 * The Flash block is not erased, it is up to the application to determine
 * if the block needs to be erased before programming.
 *
 * @param write_address The address to write to. Addresses are implementation
 *                      dependent, but should be within a range defined by a given
 *                      @c mmhal_flash_partition_config.
 * @param data          A pointer to the block of data to write.
 * @param size          The number of bytes to write.
 * @return              0 on success, or a negative number on failure.
 */
int mmhal_flash_write(mmhal_flash_addr_t write_address, const uint8_t *data, size_t size);

/**
 * Get LittleFS configuration.
 *
 * LittleFS initialization is done by @c littlefs_init() in @c mmosal_shim_fileio.c.
 * which in turn calls this function to fetch the hardware configuration for LittleFS
 * from the HAL layer. The LittleFS configuration will vary from platform to platform.
 * If LittleFS is not supported by the platform then we just return NULL. This function
 * returns a static pointer to @c struct @c lfs_config which is defined in @c lfs.h.
 *
 * See @c mmhal_littlefs.c for the full HAL layer implementation for your platform.
 * See @c mmosal_shim_fileio.c for the @c libc shims for LittleFS.
 * See @c README.md in the @c src/littlefs folder for detailed information on LittleFS.
 *
 * @return A static pointer to the LittleFS config structure, or NULL if not supported.
 */
const struct lfs_config *mmhal_get_littlefs_config(void);

#ifdef __cplusplus
}
#endif

/** @} */
