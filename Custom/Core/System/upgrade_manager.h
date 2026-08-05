#ifndef UPGRADE_MANAGER_H
#define UPGRADE_MANAGER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "mem_map.h"
#include "ota_header.h"

#define SLOT_A 0
#define SLOT_B 1

#define SLOT_COUNT 2
#define MAX_BOOT_TRY 3

#define SYS_MAGIC 0x5A5A5A5A
#define FLASH_BLK_SIZE 4096

#define APP_MAGIC 0x41505000 //APP
#define WEB_MAGIC 0x57454200 //WEB
#define AI_MAGIC  0x41490000 //AI
#define WIFI_MAGIC 0x57494649 //WIFI

typedef int (*upgrade_flash_read)(uint32_t offset, void *data, size_t size);
typedef int (*upgrade_flash_write)(uint32_t offset, void *data, size_t size);
typedef int (*upgrade_flash_erase)(uint32_t offset, size_t num_blk);

typedef enum {
    FIRMWARE_FSBL = 0,
    FIRMWARE_APP,
    FIRMWARE_WEB,
    FIRMWARE_AI_1,
    FIRMWARE_AI_2,
    FIRMWARE_WIFI,      // WiFi (SiWG917) firmware — reuses the former RESERVED1 slot
                        // index to keep the NVS-persisted active_slot[] layout stable.
    FIRMWARE_RESERVED2,
    FIRMWARE_TYPE_COUNT
} FirmwareType;

typedef enum {
    IDLE = 0,
    PENDING_VERIFICATION = 1,
    ACTIVE = 2,
    UNBOOTABLE = 3
} SlotStatus;

typedef struct {
    SlotStatus status;      // Slot status: IDLE/PENDING_VERIFICATION/ACTIVE/UNBOOTABLE
    uint32_t boot_success;   // Boot success flag
    uint32_t try_count;      // Boot attempt count
    uint8_t version[16];    // Firmware version
    uint32_t firmware_size; // Firmware size
    uint32_t crc32;         // Firmware CRC32 checksum
} slot_info_t;

typedef struct{
    uint32_t magic;
    uint8_t active_slot[FIRMWARE_TYPE_COUNT]; // Current active slot
    slot_info_t slot[FIRMWARE_TYPE_COUNT][SLOT_COUNT]; // Status of each slot for each firmware type
    uint8_t reserved[64];
    uint8_t flag;
    uint32_t crc32; 
} SystemState;

typedef struct {
    FirmwareType type;
    uint32_t magic;
    uint32_t offset_A;
    uint32_t offset_B;
    uint32_t default_size;
} firmware_partition_t;

// Used for upgrade judgment
typedef struct {
    uint32_t magic;          
    uint32_t file_size;      // Total firmware size
    uint32_t crc32;          // Complete firmware CRC32
    uint8_t version[16];        // Firmware version
} firmware_header_t;

typedef struct {
    FirmwareType type;
    firmware_header_t *header;
    uint32_t base_offset;
    uint32_t current_offset;
    uint32_t total_size;
    uint32_t crc32;
} upgrade_handle_t;

void save_system_state(void);
SystemState *get_system_state(void);
void init_system_state(upgrade_flash_read read, upgrade_flash_write write, upgrade_flash_erase erase);
uint32_t get_active_partition(FirmwareType type);
int upgrade_begin(upgrade_handle_t *handle, FirmwareType type, firmware_header_t *header);
int upgrade_write_chunk(upgrade_handle_t *handle, const void *chunk_data, size_t chunk_size);
int upgrade_finish(upgrade_handle_t *handle);
int upgrade_read_begin(upgrade_handle_t *handle, FirmwareType type, int slot_idx);
uint32_t upgrade_read_chunk(upgrade_handle_t *handle, void *buffer, size_t read_size);
uint32_t get_slot_try_count(FirmwareType type);
uint32_t get_update_partition(FirmwareType type);
void set_slot_boot_success(FirmwareType type, bool success);
int switch_active_slot(FirmwareType type);
int check_and_select_boot_slot(FirmwareType type);
void clean_system_state(void);
void verify_fsbl_version(const char* version);
#endif