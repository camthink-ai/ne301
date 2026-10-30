/*  NVS: non volatile storage in flash
 *
 * Copyright (c) 2018 Laczen
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_FS_NVS_H_
#define ZEPHYR_INCLUDE_FS_NVS_H_

#include "stdbool.h"
#ifdef __cplusplus
extern "C" {
#endif

/*
 * MASKS AND SHIFT FOR ADDRESSES
 * an address in nvs is an uint32_t where:
 *   high 2 bytes represent the sector number
 *   low 2 bytes represent the offset in a sector
 */
#define ADDR_SECT_MASK 0xFFFF0000
#define ADDR_SECT_SHIFT 16
#define ADDR_OFFS_MASK 0x0000FFFF

/*
 * Status return values
 */
#define NVS_STATUS_NOSPACE 1


#define NVS_BLOCK_SIZE 32
#define NVS_KEY_SIZE 24

#define NVS_STARTUP_TEAR_SEEN        0x01
#define NVS_STARTUP_RECOVERY_DEFERRED 0x02

/* Allocation Table Entry */
struct nvs_ate {
	char key[NVS_KEY_SIZE];	/* data key */
	uint16_t offset;	/* data offset within sector */
	uint16_t len;	/* data len within sector */
	uint8_t part;	/* part of a multipart data - future extension */
	uint8_t crc8;	/* crc8 check of the entry */
} __packed;

struct flash_parameter {
	size_t write_block_size;
	uint8_t erase_value;
};

typedef struct {
    void (*lock)(void *mutex);
    void (*unlock)(void *mutex);
} nvs_mutex_ops_t;

typedef struct {
    int  (*flash_read)(uint32_t offset, void *data, size_t len);
    int  (*flash_write)(uint32_t offset, void *data, size_t len);
    int  (*flash_erase)(uint32_t offset, size_t size);
    int  (*flash_write_protection_set)(bool enable);
} nvs_flash_ops_t;

/**
 * @brief Non-volatile Storage File system structure
 *
 * @param offset File system offset in flash
 * @param ate_wra: Allocation table entry write address. Addresses are stored
 * as uint32_t: high 2 bytes are sector, low 2 bytes are offset in sector,
 * @param data_wra: Data write address.
 * @param sector_size File system is divided into sectors each sector should be
 * multiple of pagesize
 * @param sector_count Amount of sectors in the file systems
 * @param write_block_size Alignment size
 * @param nvs_lock Mutex
 * @param flash_ops Flash operations
 * @param flash_parameters Flash parameters
 * @param mutex_ops Mutex operations
 */

typedef struct nvs_fs {
	int offset;		/* filesystem offset in flash */
	uint32_t ate_wra;		/* next alloc table entry write address */
	uint32_t data_wra;		/* next data write address */
	uint16_t sector_size;	/* filesystem is divided into sectors,
				 * sector size should be multiple of pagesize
				 */
	uint16_t sector_count;	/* amount of sectors in the filesystem */
	bool ready;		/* is the filesystem initialized ? */
	uint8_t startup_flags;
    nvs_flash_ops_t flash_ops;
	struct flash_parameter flash_parameters;
    nvs_mutex_ops_t mutex_ops;
    void *mutex;
}nvs_fs_t;


typedef struct {
    nvs_fs_t *fs;
    uint32_t curr_addr;
    struct nvs_ate curr_ate;
    int finished;
    char dumped_keys[256][NVS_KEY_SIZE+1];
    int key_count;
} nvs_iterator_t;

/**
 * @}
 */

/**
 * @brief Non-volatile Storage APIs
 * @defgroup nvs_high_level_api Non-volatile Storage APIs
 * @ingroup nvs
 * @{
 */

/**
 * @brief nvs_init
 *
 * Initializes a NVS file system in flash.
 *
 * @param fs Pointer to file system
 * @retval 0 Success
 * @retval -ERRNO errno code if error
 */
int nvs_init(nvs_fs_t *fs);

int nvs_blank_check(nvs_fs_t *fs, int *is_blank);

/**
 * @brief nvs_clear
 *
 * Clears the NVS file system from flash.
 * @param fs Pointer to file system
 * @retval 0 Success
 * @retval -ERRNO errno code if error
 */
int nvs_clear(nvs_fs_t *fs);

/**
 * @brief nvs_write
 *
 * Write an entry to the file system.
 *
 * @param fs Pointer to file system
 * @param id Id of the entry to be written
 * @param data Pointer to the data to be written
 * @param len Number of bytes to be written
 *
 * @return Number of bytes written. On success, it will be equal to the number
 * of bytes requested to be written. On error returns -ERRNO code.
 */

size_t nvs_write(nvs_fs_t *fs, const char *key, const void *data, size_t len);

/**
 * @brief nvs_delete
 *
 * Delete an entry from the file system
 *
 * @param fs Pointer to file system
 * @param id Id of the entry to be deleted
 * @retval 0 Success
 * @retval -ERRNO errno code if error
 */
int nvs_delete(nvs_fs_t *fs, const char *key);

/**
 * @brief nvs_read
 *
 * Read an entry from the file system.
 *
 * @param fs Pointer to file system
 * @param id Id of the entry to be read
 * @param data Pointer to data buffer
 * @param len Number of bytes to be read
 *
 * @return Number of bytes read. On success, it will be equal to the number
 * of bytes requested to be read. When the return value is larger than the
 * number of bytes requested to read this indicates not all bytes were read,
 * and more data is available. On error returns -ERRNO code.
 */
size_t nvs_read(nvs_fs_t *fs, const char *key, void *data, size_t len);

/**
 * @brief nvs_read_hist
 *
 * Read a history entry from the file system.
 *
 * @param fs Pointer to file system
 * @param id Id of the entry to be read
 * @param data Pointer to data buffer
 * @param len Number of bytes to be read
 * @param cnt History counter: 0: latest entry, 1:one before latest ...
 *
 * @return Number of bytes read. On success, it will be equal to the number
 * of bytes requested to be read. When the return value is larger than the
 * number of bytes requested to read this indicates not all bytes were read,
 * and more data is available. On error returns -ERRNO code.
 */
size_t nvs_read_hist(nvs_fs_t *fs, const char *key, void *data, size_t len,
              uint16_t cnt);

/**
 * @brief nvs_calc_free_space
 *
 * Calculate the available free space in the file system.
 *
 * @param fs Pointer to file system
 *
 * @return Number of bytes free. On success, it will be equal to the number
 * of bytes that can still be written to the file system. Calculating the
 * free space is a time consuming operation, especially on spi flash.
 * On error returns -ERRNO code.
 */
size_t nvs_calc_free_space(nvs_fs_t *fs);

/**
 * @brief nvs_entry_find
 *
 * Find an entry in the file system.
 *
 * @param fs Pointer to file system
 * @param it Pointer to iterator structure
 *
 * @return 0 on success, -EACCES code on error.
 */
int nvs_entry_find(nvs_fs_t *fs, nvs_iterator_t *it);

/**
 * @brief nvs_entry_info
 *
 * Get information about the current entry in the iterator.
 *
 * @param it Pointer to iterator structure
 * @param info Pointer to struct nvs_ate to fill with information
 *
 * @return 0 on success, -ENOENT code on error.
 */
int nvs_entry_info(nvs_iterator_t *it, struct nvs_ate *info);

/**
 * @brief nvs_entry_next
 *
 * Move to the next entry in the iterator.
 *
 * @param it Pointer to iterator structure
 *
 * @return 0 on success, -ERRNO code if no more entries are available.
 */
int nvs_entry_next(nvs_iterator_t *it);

/**
 * @brief nvs_release_iterator
 *
 * Release the iterator.
 *
 * @param it Pointer to iterator structure
 */
void nvs_release_iterator(nvs_iterator_t *it);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_FS_NVS_H_ */