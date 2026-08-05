/**
 * @file ota_service.c
 * @brief OTA Service Implementation - A/B Partition Streaming Upgrade
 * @details A/B partition streaming upgrade OTA service implementation, based on upgrade_manager
 */

#include "ota_service.h"
#include "aicam_types.h"
#include "debug.h"
#include "generic_file.h"
#include "ota_header.h"
#include "storage.h"
#include "version.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ==================== OTA Service Context ==================== */

typedef struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t state;
} ota_service_context_t;

static ota_service_context_t g_ota_service = {0};

/* ==================== OTA Interface Wrappers ==================== */

uint32_t ota_get_active_partition(FirmwareType type)
{
    return get_active_partition(type);
}

uint32_t ota_get_update_partition(FirmwareType type)
{
    return get_update_partition(type);
}

int ota_upgrade_begin(upgrade_handle_t *handle, FirmwareType type, firmware_header_t *header)
{
    return upgrade_begin(handle, type, header);
}

int ota_upgrade_write_chunk(upgrade_handle_t *handle, const void *chunk_data, size_t chunk_size)
{
    return upgrade_write_chunk(handle, chunk_data, chunk_size);
}

int ota_upgrade_finish(upgrade_handle_t *handle)
{
    return upgrade_finish(handle);
}

int ota_upgrade_read_begin(upgrade_handle_t *handle, FirmwareType type, int slot_idx)
{
    return upgrade_read_begin(handle, type, slot_idx);
}

uint32_t ota_upgrade_read_chunk(upgrade_handle_t *handle, void *buffer, size_t read_size)
{
    return upgrade_read_chunk(handle, buffer, read_size);
}

uint32_t ota_get_slot_try_count(FirmwareType type)
{
    return get_slot_try_count(type);
}

void ota_set_slot_boot_success(FirmwareType type, bool success)
{
    set_slot_boot_success(type, success);
}

int ota_switch_active_slot(FirmwareType type)
{
    return switch_active_slot(type);
}

int ota_check_and_select_boot_slot(FirmwareType type)
{
    return check_and_select_boot_slot(type);
}

/* ==================== System State Management ==================== */

void ota_init_system_state(upgrade_flash_read read, upgrade_flash_write write, upgrade_flash_erase erase)
{
    init_system_state(read, write, erase);
}

void ota_save_system_state(void)
{
    save_system_state();
}

SystemState* ota_get_system_state(void)
{
    return get_system_state();
}

const slot_info_t* ota_get_slot_info(FirmwareType type, int slot_idx)
{
    SystemState *state = get_system_state();
    if (!state || type >= FIRMWARE_TYPE_COUNT || slot_idx >= SLOT_COUNT) {
        return NULL;
    }
    return &state->slot[type][slot_idx];
}

int ota_get_slot_version_string(FirmwareType type, int slot_idx, char *buf, size_t buf_size)
{
    if (!buf || buf_size < 16) {
        return -1;
    }
    
    const slot_info_t *slot = ota_get_slot_info(type, slot_idx);
    if (!slot) {
        return -1;
    }
    
    // Use version utility from ota_header.h
    ota_version_to_string(slot->version, buf, buf_size);
    return 0;
}

int ota_compare_slot_version(FirmwareType type, int slot_idx, const uint8_t *ver)
{
    if (!ver) {
        return -999;
    }
    
    const slot_info_t *slot = ota_get_slot_info(type, slot_idx);
    if (!slot) {
        return -999;
    }
    
    // Use version comparison from ota_header.h
    return ota_version_compare(slot->version, ver);
}

/* ==================== Utility Functions ==================== */

uint32_t ota_calculate_crc32(uint32_t *data, size_t size)
{
    // Use the existing CRC32 implementation from upgrade_manager
    // This is a placeholder - the actual implementation should be in upgrade_manager.c
    // For now, we'll implement a simple version
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ==================== Pre-upgrade Validation ==================== */

const char* ota_get_validation_result_string(ota_validation_result_t result)
{
    switch (result) {
        case OTA_VALIDATION_OK:
            return "Validation OK";
        case OTA_VALIDATION_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case OTA_VALIDATION_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case OTA_VALIDATION_ERROR_FILE_SIZE:
            return "Invalid file size";
        case OTA_VALIDATION_ERROR_HEADER_INVALID:
            return "Invalid firmware header";
        case OTA_VALIDATION_ERROR_CRC32_MISMATCH:
            return "CRC32 checksum mismatch";
        case OTA_VALIDATION_ERROR_VERSION_INVALID:
            return "Invalid firmware version";
        case OTA_VALIDATION_ERROR_PARTITION_FULL:
            return "Partition size insufficient";
        case OTA_VALIDATION_ERROR_SYSTEM_STATE:
            return "Invalid system state";
        case OTA_VALIDATION_ERROR_SIGNATURE_INVALID:
            return "Invalid digital signature";
        case OTA_VALIDATION_ERROR_HARDWARE_INCOMPATIBLE:
            return "Hardware incompatible";
        default:
            return "Unknown validation error";
    }
}

ota_validation_result_t ota_validate_system_state(FirmwareType fw_type)
{
    if (fw_type >= FIRMWARE_TYPE_COUNT) {
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    
    SystemState *state = get_system_state();
    if (!state) {
        LOG_SVC_ERROR("System state not available");
        return OTA_VALIDATION_ERROR_SYSTEM_STATE;
    }
    
    // Check if system state is valid
    if (state->magic != SYS_MAGIC) {
        LOG_SVC_ERROR("Invalid system state magic: 0x%08X", state->magic);
        return OTA_VALIDATION_ERROR_SYSTEM_STATE;
    }
    
    // Check if current active slot is valid
    uint8_t active_slot = state->active_slot[fw_type];
    if (active_slot >= SLOT_COUNT) {
        LOG_SVC_ERROR("Invalid active slot for firmware type %d: %d", fw_type, active_slot);
        return OTA_VALIDATION_ERROR_SYSTEM_STATE;
    }
    
    // Check if update slot is available
    uint8_t update_slot = (active_slot == SLOT_A) ? SLOT_B : SLOT_A;
    slot_info_t *update_slot_info = &state->slot[fw_type][update_slot];
    
    if (update_slot_info->status == ACTIVE) {
        LOG_SVC_WARN("Update slot %d for firmware type %d is already active", update_slot, fw_type);
        // This might be acceptable in some cases, but log as warning
    }
    
    LOG_SVC_INFO("System state validation passed for firmware type %d", fw_type);
    return OTA_VALIDATION_OK;
}

ota_validation_result_t ota_validate_partition_availability(FirmwareType fw_type, uint32_t required_size)
{
    if (fw_type >= FIRMWARE_TYPE_COUNT) {
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    
    // Get partition size from upgrade_manager partition table
    // This would need to be implemented based on the actual partition configuration
    uint32_t partition_size = 0;
    
    // TODO: Get actual partition size from partition table
    // For now, use hardcoded values based on upgrade_manager.c
    switch (fw_type) {
        case FIRMWARE_FSBL:
            partition_size = 0x100000;  // 1MB
            break;
        case FIRMWARE_APP:
            partition_size = 0x800000;  // 8MB
            break;
        case FIRMWARE_WEB:
            partition_size = 0x200000;  // 2MB
            break;
        case FIRMWARE_AI_1:
            partition_size = 0x1000000; // 16MB
            break;
        case FIRMWARE_AI_2:
            partition_size = 0x1000000; // 16MB
            break;
        case FIRMWARE_WIFI:
            partition_size = WIFI_FW_SIZE; // 3MB
            break;
        default:
            partition_size = 0x100000;  // 1MB default
            break;
    }
    
    if (required_size > partition_size) {
        LOG_SVC_ERROR("Required size %u exceeds partition size %u for firmware type %d", 
                      required_size, partition_size, fw_type);
        return OTA_VALIDATION_ERROR_PARTITION_FULL;
    }
    
    LOG_SVC_INFO("Partition size validation passed for firmware type %d: %u/%u bytes", 
                 fw_type, required_size, partition_size);
    return OTA_VALIDATION_OK;
}

ota_validation_result_t ota_validate_firmware_header(const firmware_header_t *header, FirmwareType fw_type, const ota_validation_options_t *options)
{
    if (!header || !options || fw_type >= FIRMWARE_TYPE_COUNT) {
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    
    // Validate file size
    if (header->file_size == 0 || header->file_size > 0x10000000) { // Max 256MB
        LOG_SVC_ERROR("Invalid file size: %u", header->file_size);
        return OTA_VALIDATION_ERROR_FILE_SIZE;
    }
    
    // Validate version if required
    if (options->validate_version) {
        // Log the incoming firmware version using new format
        char ver_str[20];
        ota_version_to_string(header->version, ver_str, sizeof(ver_str));
        LOG_SVC_INFO("Validating firmware version: %s", ver_str);
        
        // Check against current running version if downgrade not allowed
        if (!options->allow_downgrade) {
            uint8_t current_ver[8] = {FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH, 
                                      FW_VERSION_BUILD & 0xFF, (FW_VERSION_BUILD >> 8) & 0xFF, 0, 0, 0};
            if (ota_version_compare(header->version, current_ver) < 0) {
                char cur_ver_str[20];
                ota_version_to_string(current_ver, cur_ver_str, sizeof(cur_ver_str));
                LOG_SVC_ERROR("Downgrade not allowed: %s < %s", ver_str, cur_ver_str);
                return OTA_VALIDATION_ERROR_VERSION_INVALID;
            }
        }
        
        // Check min/max version (MAJOR version check for backward compatibility)
        if (options->min_version > 0 && header->version[0] < options->min_version) {
            LOG_SVC_ERROR("Version too old: MAJOR %u < %u", header->version[0], options->min_version);
            return OTA_VALIDATION_ERROR_VERSION_INVALID;
        }
        
        if (options->max_version > 0 && header->version[0] > options->max_version) {
            LOG_SVC_ERROR("Version too new: MAJOR %u > %u", header->version[0], options->max_version);
            return OTA_VALIDATION_ERROR_VERSION_INVALID;
        }
    }
    
    // Validate partition size if required
    if (options->validate_partition_size) {
        ota_validation_result_t result = ota_validate_partition_availability(fw_type, header->file_size);
        if (result != OTA_VALIDATION_OK) {
            return result;
        }
    }
    
    LOG_SVC_INFO("Firmware header validation passed for firmware type %d", fw_type);
    return OTA_VALIDATION_OK;
}

int ota_calculate_file_crc32(const char *filename, uint32_t *crc32)
{
    if (!filename || !crc32) {
        return -1;
    }
    
    LOG_SVC_INFO("Calculating CRC32 for file: %s", filename);
    
    // Open file (using flash file system)
    void *fd = disk_file_fopen(FS_FLASH, filename, "rb");
    if (!fd) {
        LOG_SVC_ERROR("Cannot open file for CRC32 calculation: %s", filename);
        return -1;
    }

    //skip the first 1024 bytes for header
    disk_file_fseek(FS_FLASH, fd, 1024, SEEK_SET);
    
    // Initialize CRC32
    uint32_t calculated_crc = 0xFFFFFFFF;
    char buffer[1024];
    int bytes_read;
    
    // Read file in chunks and calculate CRC32
    while ((bytes_read = disk_file_fread(FS_FLASH, fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            calculated_crc ^= (uint8_t)buffer[i];
            for (int j = 0; j < 8; j++) {
                if (calculated_crc & 1) {
                    calculated_crc = (calculated_crc >> 1) ^ 0xEDB88320;
                } else {
                    calculated_crc >>= 1;
                }
            }
        }
    }
    
    disk_file_fclose(FS_FLASH, fd);
    
    // Finalize CRC32
    *crc32 = calculated_crc ^ 0xFFFFFFFF;
    
    LOG_SVC_INFO("CRC32 calculation completed: 0x%08X", *crc32);
    return 0;
}

ota_validation_result_t ota_validate_firmware_file(FirmwareType fw_type, const char *filename, const ota_validation_options_t *options)
{

    if (!filename) {
        printf("filename is NULL\r\n");
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    if (!options) {
        printf("options is NULL\r\n");
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    if (fw_type >= FIRMWARE_TYPE_COUNT) {
        printf("fw_type is invalid\r\n");
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    if (!filename || !options || fw_type >= FIRMWARE_TYPE_COUNT) {
        return OTA_VALIDATION_ERROR_INVALID_PARAMS;
    }
    
    LOG_SVC_INFO("Validating firmware file: %s (type: %d)", filename, fw_type);
    
    // Step 1: Check if file exists and get file size (using flash file system)
    struct stat file_stat_t;
    if (disk_file_stat(FS_FLASH, filename, &file_stat_t) != 0) {
        LOG_SVC_ERROR("File not found: %s", filename);
        return OTA_VALIDATION_ERROR_FILE_NOT_FOUND;
    }
    
    if (file_stat_t.st_size == 0) {
        LOG_SVC_ERROR("File is empty: %s", filename);
        return OTA_VALIDATION_ERROR_FILE_SIZE;
    }
    
    LOG_SVC_INFO("File size: %ld bytes", file_stat_t.st_size);
    
    // Step 2: Open file and read firmware header
    void *fd = disk_file_fopen(FS_FLASH, filename, "rb");
    if (!fd) {
        LOG_SVC_ERROR("Cannot open file: %s", filename);
        return OTA_VALIDATION_ERROR_FILE_NOT_FOUND;
    }
    
    // Step 3: Read firmware header and verify (assuming header is at the beginning)
    ota_header_t header = {0};
    int bytes_read = disk_file_fread(FS_FLASH, fd, &header, sizeof(ota_header_t));
    if (bytes_read != sizeof(ota_header_t)) {
        LOG_SVC_ERROR("Failed to read firmware header from: %s", filename);
        disk_file_fclose(FS_FLASH, fd);
        return OTA_VALIDATION_ERROR_HEADER_INVALID;
    }

    if (ota_header_verify(&header) != AICAM_OK) {
        LOG_SVC_ERROR("Invalid firmware header: %s", filename);
        disk_file_fclose(FS_FLASH, fd);
        return OTA_VALIDATION_ERROR_HEADER_INVALID;
    }
    
    // Step 4: Validate file size matches header
    if (header.total_package_size != file_stat_t.st_size) {
        LOG_SVC_ERROR("File size mismatch: header=%u, actual=%ld", header.total_package_size, file_stat_t.st_size);
        disk_file_fclose(FS_FLASH, fd);
        return OTA_VALIDATION_ERROR_FILE_SIZE;
    }
    
    // Step 5: Validate header
    // Convert packed OTA header to aligned firmware_header_t to avoid unaligned access
    firmware_header_t fw_header = {0};
    fw_header.file_size = header.total_package_size;
    memcpy(fw_header.version, header.fw_ver, sizeof(fw_header.version));
    fw_header.crc32 = header.fw_crc32;

    ota_validation_result_t result = ota_validate_firmware_header(&fw_header, fw_type, options);
    if (result != OTA_VALIDATION_OK) {
        disk_file_fclose(FS_FLASH, fd);
        return result;
    }
    
    // Step 6: Calculate and validate CRC32 if required
    if (options->validate_crc32) {
        uint32_t calculated_crc32;
        if (ota_calculate_file_crc32(filename, &calculated_crc32) != 0) {
            LOG_SVC_ERROR("Failed to calculate CRC32 for: %s", filename);
            disk_file_fclose(FS_FLASH, fd);
            return OTA_VALIDATION_ERROR_CRC32_MISMATCH;
        }
        
        if (calculated_crc32 != header.fw_crc32) {
            LOG_SVC_ERROR("CRC32 mismatch: calculated=0x%08X, header=0x%08X", calculated_crc32, header.fw_crc32);
            disk_file_fclose(FS_FLASH, fd);
            return OTA_VALIDATION_ERROR_CRC32_MISMATCH;
        }
        
        LOG_SVC_INFO("CRC32 validation passed: 0x%08X", calculated_crc32);
    }
    
    // Step 7: Validate system state
    result = ota_validate_system_state(fw_type);
    if (result != OTA_VALIDATION_OK) {
        disk_file_fclose(FS_FLASH, fd);
        return result;
    }
    
    disk_file_fclose(FS_FLASH, fd);
    
    LOG_SVC_INFO("Firmware file validation passed: %s", filename);
    return OTA_VALIDATION_OK;
}

/* ==================== File-based Upgrade ==================== */

int ota_upgrade_from_file(int fw_type, const char *filename, const ota_validation_options_t *options)
{
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT || !filename || !options) {
        LOG_SVC_ERROR("Invalid parameters for upgrade from file");
        return -1;
    }

    LOG_SVC_INFO("Starting upgrade from file: %s (type: %d)", filename, fw_type);
    
    // Step 1: Validate firmware file
    ota_validation_result_t validation_result = ota_validate_firmware_file(fw_type, filename, options);
    if (validation_result != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("Firmware validation failed: %s", ota_get_validation_result_string(validation_result));
        return -1;
    }
    
    LOG_SVC_INFO("Firmware validation passed, proceeding with upgrade...");
    
    // Step 2: Open firmware file (using flash file system)
    void *fd = disk_file_fopen(FS_FLASH, filename, "rb");
    if (!fd) {
        LOG_SVC_ERROR("Cannot open firmware file: %s", filename);
        return -1;
    }
    
    // Step 3: Get file size and create header
    struct stat file_stat_t;
    if (disk_file_stat(FS_FLASH, filename, &file_stat_t) != 0) {
        LOG_SVC_ERROR("Cannot get file size: %s", filename);
        disk_file_fclose(FS_FLASH, fd);
        return -1;
    }
    
    firmware_header_t header = {0};
    header.file_size = file_stat_t.st_size;
    
    // Extract version from filename (simple implementation)
    // In real implementation, this should be read from firmware header
    // const char *basename = strrchr(filename, '/');
    // if (basename) {
    //     basename++; // Skip '/'
    // } else {
    //     basename = filename;
    // }
    strncpy((char*)header.version, "1.0.0.0", sizeof(header.version) - 1);
    
    // Step 4: Begin upgrade
    upgrade_handle_t handle = {0};
    if (ota_upgrade_begin(&handle, fw_type, &header) != 0) {
        LOG_SVC_ERROR("upgrade_begin failed");
        disk_file_fclose(FS_FLASH, fd);
        return -1;
    }
    
    LOG_SVC_INFO("Firmware size: %u, upgrade address: 0x%x", header.file_size, handle.base_offset);
    
    // Step 5: Read and write firmware in chunks
    // for fsbl, skip the first 1024 bytes
 
    char buffer[1024];
    uint32_t remaining = header.file_size;
    uint32_t total_written = 0;

    if (fw_type == FIRMWARE_FSBL) {
        disk_file_fseek(FS_FLASH, fd, 1024, SEEK_SET);
        remaining -= 1024;
    }
    
    while (remaining > 0) {
        size_t chunk_size = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        int bytes_read = disk_file_fread(FS_FLASH, fd, buffer, chunk_size);
        
        if (bytes_read <= 0) {
            LOG_SVC_ERROR("Failed to read firmware chunk at offset %u", total_written);
            disk_file_fclose(FS_FLASH, fd);
            return -1;
        }
        
        if (ota_upgrade_write_chunk(&handle, buffer, bytes_read) != 0) {
            LOG_SVC_ERROR("upgrade_write_chunk failed at offset %u", total_written);
            disk_file_fclose(FS_FLASH, fd);
            return -1;
        }
        
        remaining -= bytes_read;
        total_written += bytes_read;
        
        // Log progress every 10%
        if (total_written % (header.file_size / 10) == 0) {
            uint32_t progress = (total_written * 100) / header.file_size;
            LOG_SVC_INFO("Upgrade progress: %u%% (%u/%u bytes)", progress, total_written, header.file_size);
        }
    }
    
    disk_file_fclose(FS_FLASH, fd);
    
    // Step 6: Verify all data was written
    if (remaining != 0) {
        LOG_SVC_ERROR("Firmware file size mismatch: %u bytes remaining", remaining);
        return -1;
    }
    
    // Step 7: Finish upgrade
    if (ota_upgrade_finish(&handle) != 0) {
        LOG_SVC_ERROR("upgrade_finish failed");
        return -1;
    }


    // update json config
    if(fw_type == FIRMWARE_AI_2) {
        json_config_set_ai_1_active(AICAM_TRUE);
    }
    
    LOG_SVC_INFO("Upgrade from file completed successfully: %s", filename);
    return 0;
}

int ota_dump_firmware(int fw_type, int slot_idx, const char *filename)
{
    if ((fw_type < 0) ||
        (fw_type >= FIRMWARE_TYPE_COUNT) ||
        ((slot_idx != SLOT_A && slot_idx != SLOT_B)) ||
        (!filename)) 
    {
        LOG_SVC_ERROR("Invalid parameters for firmware dump");
        return -1;
    }

    LOG_SVC_INFO("Dumping firmware type %d slot %d to file: %s", fw_type, slot_idx, filename);
    
    // Step 1: Open output file (using flash file system)
    void *fd = disk_file_fopen(FS_FLASH, filename, "wb");
    if (!fd) {
        LOG_SVC_ERROR("Cannot open output file for write: %s", filename);
        return -1;
    }
    
    // Step 2: Begin read from specified slot
    upgrade_handle_t handle = {0};
    firmware_header_t header = {0};
    handle.header = &header;
    
    if (ota_upgrade_read_begin(&handle, fw_type, slot_idx) != 0) {
        LOG_SVC_ERROR("upgrade_read_begin failed for firmware type %d slot %d", fw_type, slot_idx);
        disk_file_fclose(FS_FLASH, fd);
        return -1;
    }
    
    LOG_SVC_INFO("Reading firmware from slot %d, total size: %u bytes", slot_idx, handle.total_size);
    
    // Step 3: Read and write firmware in chunks
    char buffer[1024];
    uint32_t remaining = handle.total_size;
    uint32_t total_read = 0;
    
    while (remaining > 0) {
        size_t chunk_size = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        uint32_t bytes_read = ota_upgrade_read_chunk(&handle, buffer, chunk_size);
        
        if (bytes_read == 0) {
            LOG_SVC_ERROR("Failed to read firmware chunk at offset %u", total_read);
            disk_file_fclose(FS_FLASH, fd);
            return -1;
        }
        
        if (disk_file_fwrite(FS_FLASH, fd, buffer, bytes_read) != bytes_read) {
            LOG_SVC_ERROR("Failed to write firmware chunk to file at offset %u", total_read);
            disk_file_fclose(FS_FLASH, fd);
            return -1;
        }
        
        remaining -= bytes_read;
        total_read += bytes_read;
        
        // Log progress every 10%
        if (total_read % (handle.total_size / 10) == 0) {
            uint32_t progress = (total_read * 100) / handle.total_size;
            LOG_SVC_INFO("Dump progress: %u%% (%u/%u bytes)", progress, total_read, handle.total_size);
        }
    }
    
    disk_file_fclose(FS_FLASH, fd);
    
    // Step 4: Verify all data was read and written
    if (remaining != 0) {
        LOG_SVC_ERROR("Firmware dump failed: %u bytes remaining", remaining);
        return -1;
    }
    
    LOG_SVC_INFO("Firmware dumped successfully to %s, size: %u bytes", filename, handle.total_size);
    return 0;
}

/* ==================== Memory-based Upgrade ==================== */

int ota_upgrade_from_memory(int fw_type, const void *firmware_data, size_t firmware_size, const ota_validation_options_t *options)
{
    if (fw_type < 0 || fw_type >= FIRMWARE_TYPE_COUNT || !firmware_data || firmware_size == 0 || !options) {
        LOG_SVC_ERROR("Invalid parameters for upgrade from memory");
        return -1;
    }

    LOG_SVC_INFO("Starting upgrade from memory: size=%u bytes (type: %d)", firmware_size, fw_type);
    
    // Step 1: Validate firmware size
    if (firmware_size < sizeof(ota_header_t)) {
        LOG_SVC_ERROR("Firmware size too small: %u < %u", firmware_size, sizeof(ota_header_t));
        return -1;
    }
    
    // Step 2: Read and verify OTA header
    ota_header_t *header = (ota_header_t *)firmware_data;
    if (ota_header_verify(header) != 0) {
        LOG_SVC_ERROR("Invalid firmware header in memory buffer");
        return -1;
    }
    
    // Step 3: Validate header size matches buffer size
    if (header->total_package_size != firmware_size) {
        LOG_SVC_ERROR("Firmware size mismatch: header=%u, buffer=%u", header->total_package_size, firmware_size);
        return -1;
    }
    
    // Step 4: Validate firmware header with options
    firmware_header_t fw_header = {0};
    fw_header.file_size = header->total_package_size;
    memcpy(fw_header.version, header->fw_ver, sizeof(fw_header.version));
    fw_header.crc32 = header->fw_crc32;
    
    ota_validation_result_t validation_result = ota_validate_firmware_header((const firmware_header_t *)&fw_header, fw_type, options);
    if (validation_result != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("Firmware header validation failed: %s", ota_get_validation_result_string(validation_result));
        return -1;
    }
    
    // Step 5: Validate system state
    validation_result = ota_validate_system_state(fw_type);
    if (validation_result != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("System state validation failed: %s", ota_get_validation_result_string(validation_result));
        return -1;
    }
    
    // Step 6: Calculate and validate CRC32 if required
    if (options->validate_crc32) {
        uint32_t calculated_crc = 0xFFFFFFFF;
        const uint8_t *data = (const uint8_t *)firmware_data;
        
        // Skip header (1024 bytes) for CRC32 calculation
        size_t data_size = firmware_size - sizeof(ota_header_t);
        const uint8_t *data_start = data + sizeof(ota_header_t);
        
        for (size_t i = 0; i < data_size; i++) {
            calculated_crc ^= data_start[i];
            for (int j = 0; j < 8; j++) {
                if (calculated_crc & 1) {
                    calculated_crc = (calculated_crc >> 1) ^ 0xEDB88320;
                } else {
                    calculated_crc >>= 1;
                }
            }
        }
        calculated_crc ^= 0xFFFFFFFF;
        
        if (calculated_crc != header->fw_crc32) {
            LOG_SVC_ERROR("CRC32 mismatch: calculated=0x%08X, header=0x%08X", calculated_crc, header->fw_crc32);
            return -1;
        }
        
        LOG_SVC_INFO("CRC32 validation passed: 0x%08X", calculated_crc);
    }
    
    LOG_SVC_INFO("Firmware validation passed, proceeding with upgrade...");
    
    // Step 7: Begin upgrade
    upgrade_handle_t handle = {0};
    if (ota_upgrade_begin(&handle, fw_type, &fw_header) != 0) {
        LOG_SVC_ERROR("upgrade_begin failed");
        return -1;
    }
    
    LOG_SVC_INFO("Firmware size: %u, upgrade address: 0x%x", fw_header.file_size, handle.base_offset);
    
    // Step 8: Write firmware data in chunks
    const uint8_t *data_ptr = (const uint8_t *)firmware_data;
    uint32_t remaining = firmware_size;
    uint32_t total_written = 0;
    size_t header_size = sizeof(ota_header_t);
    
    // For FSBL, skip the first 1024 bytes (header)
    if (fw_type == FIRMWARE_FSBL) {
        data_ptr += header_size;
        remaining -= header_size;
    }
    
    char buffer[1024];
    while (remaining > 0) {
        size_t chunk_size = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        memcpy(buffer, data_ptr, chunk_size);
        
        if (ota_upgrade_write_chunk(&handle, buffer, chunk_size) != 0) {
            LOG_SVC_ERROR("upgrade_write_chunk failed at offset %u", total_written);
            return -1;
        }
        
        data_ptr += chunk_size;
        remaining -= chunk_size;
        total_written += chunk_size;
        
        // Log progress every 10%
        if (total_written % ((fw_header.file_size - header_size) / 10) == 0) {
            uint32_t progress = (total_written * 100) / (fw_header.file_size - header_size);
            LOG_SVC_INFO("Upgrade progress: %u%% (%u/%u bytes)", progress, total_written, fw_header.file_size - header_size);
        }
    }
    
    // Step 9: Verify all data was written
    if (remaining != 0) {
        LOG_SVC_ERROR("Firmware size mismatch: %u bytes remaining", remaining);
        return -1;
    }
    
    // Step 10: Finish upgrade
    if (ota_upgrade_finish(&handle) != 0) {
        LOG_SVC_ERROR("upgrade_finish failed");
        return -1;
    }

    // Step 11: Update json config if needed
    if (fw_type == FIRMWARE_AI_2) {
        json_config_set_ai_1_active(AICAM_TRUE);
    }
    
    LOG_SVC_INFO("Upgrade from memory completed successfully, size: %u bytes", firmware_size);
    return 0;
}

int ota_dump_firmware_to_memory(int fw_type, int slot_idx, void *buffer, size_t buffer_size, size_t *actual_size)
{
    if ((fw_type < 0) ||
        (fw_type >= FIRMWARE_TYPE_COUNT) ||
        ((slot_idx != SLOT_A && slot_idx != SLOT_B)) ||
        (!buffer) ||
        (buffer_size == 0) ||
        (!actual_size)) 
    {
        LOG_SVC_ERROR("Invalid parameters for firmware dump to memory");
        return -1;
    }

    LOG_SVC_INFO("Dumping firmware type %d slot %d to memory buffer (size: %u)", fw_type, slot_idx, buffer_size);
    
    // Step 1: Begin read from specified slot
    upgrade_handle_t handle = {0};
    firmware_header_t header = {0};
    handle.header = &header;
    
    if (ota_upgrade_read_begin(&handle, fw_type, slot_idx) != 0) {
        LOG_SVC_ERROR("upgrade_read_begin failed for firmware type %d slot %d", fw_type, slot_idx);
        return -1;
    }
    
    // Check if buffer is large enough
    // We need space for OTA header (1024 bytes) + firmware data
    size_t required_size = sizeof(ota_header_t) + handle.total_size;
    if (buffer_size < required_size) {
        LOG_SVC_ERROR("Buffer too small: required=%u, provided=%u", required_size, buffer_size);
        *actual_size = required_size;
        return -1;
    }
    
    LOG_SVC_INFO("Reading firmware from slot %d, total size: %u bytes", slot_idx, handle.total_size);
    
    // Step 2: Read OTA header from flash partition
    // upgrade_read_begin already set handle.base_offset to the correct partition offset
    uint32_t partition_offset = handle.base_offset;
    
    if (partition_offset == 0) {
        LOG_SVC_ERROR("Invalid partition offset for firmware type %d slot %d", fw_type, slot_idx);
        return -1;
    }
    
    // Read header from flash (header is at the start of partition)
    ota_header_t *header_ptr = (ota_header_t *)buffer;
    if (storage_flash_read(partition_offset, header_ptr, sizeof(ota_header_t)) != 0) {
        LOG_SVC_ERROR("Failed to read OTA header from flash");
        return -1;
    }
    
    // Step 3: Read firmware data in chunks
    uint8_t *data_ptr = (uint8_t *)buffer + sizeof(ota_header_t);
    uint32_t remaining = handle.total_size;
    uint32_t total_read = 0;
    char read_buffer[1024];
    
    while (remaining > 0) {
        size_t chunk_size = (remaining > sizeof(read_buffer)) ? sizeof(read_buffer) : remaining;
        uint32_t bytes_read = ota_upgrade_read_chunk(&handle, read_buffer, chunk_size);
        
        if (bytes_read == 0) {
            LOG_SVC_ERROR("Failed to read firmware chunk at offset %u", total_read);
            return -1;
        }
        
        memcpy(data_ptr, read_buffer, bytes_read);
        data_ptr += bytes_read;
        remaining -= bytes_read;
        total_read += bytes_read;
        
        // Log progress every 10%
        if (total_read % (handle.total_size / 10) == 0) {
            uint32_t progress = (total_read * 100) / handle.total_size;
            LOG_SVC_INFO("Dump progress: %u%% (%u/%u bytes)", progress, total_read, handle.total_size);
        }
    }
    
    // Step 4: Verify all data was read
    if (remaining != 0) {
        LOG_SVC_ERROR("Firmware dump failed: %u bytes remaining", remaining);
        return -1;
    }
    
    *actual_size = sizeof(ota_header_t) + total_read;
    LOG_SVC_INFO("Firmware dumped successfully to memory, total size: %u bytes (header: %u + data: %u)", 
                 *actual_size, sizeof(ota_header_t), total_read);
    return 0;
}

/* ==================== Web Download Upgrade Implementation ==================== */

int ota_download_start(ota_download_handle_t *handle, const ota_download_config_t *config)
{
    if (!handle || !config) {
        return -1;
    }
    
    LOG_SVC_INFO("Starting download from: %s", config->url);
    
    // Initialize handle
    memset(handle, 0, sizeof(ota_download_handle_t));
    memcpy(&handle->config, config, sizeof(ota_download_config_t));
    handle->status = OTA_DOWNLOAD_STATUS_CONNECTING;
    
    // TODO: Implement HTTP download
    // 1. Initialize HTTP client
    // 2. Set up connection
    // 3. Get file size
    // 4. Start download
    
    // Placeholder implementation
    handle->status = OTA_DOWNLOAD_STATUS_DOWNLOADING;
    handle->total_bytes = 1024 * 1024; // 1MB placeholder
    
    // Call status callback
    if (config->status_cb) {
        config->status_cb(handle->status, 0, config->user_data);
    }
    
    return 0;
}

int ota_download_pause(ota_download_handle_t *handle)
{
    if (!handle) {
        return -1;
    }
    
    if (handle->status != OTA_DOWNLOAD_STATUS_DOWNLOADING) {
        return -1;
    }
    
    LOG_SVC_INFO("Pausing download");
    handle->status = OTA_DOWNLOAD_STATUS_PAUSED;
    
    // TODO: Implement pause logic
    // 1. Pause HTTP transfer
    // 2. Save current position
    
    // Call status callback
    if (handle->config.status_cb) {
        handle->config.status_cb(handle->status, 0, handle->config.user_data);
    }
    
    return 0;
}

int ota_download_resume(ota_download_handle_t *handle)
{
    if (!handle) {
        return -1;
    }
    
    if (handle->status != OTA_DOWNLOAD_STATUS_PAUSED) {
        return -1;
    }
    
    LOG_SVC_INFO("Resuming download");
    handle->status = OTA_DOWNLOAD_STATUS_DOWNLOADING;
    
    // TODO: Implement resume logic
    // 1. Resume HTTP transfer from saved position
    // 2. Continue download
    
    // Call status callback
    if (handle->config.status_cb) {
        handle->config.status_cb(handle->status, 0, handle->config.user_data);
    }
    
    return 0;
}

int ota_download_cancel(ota_download_handle_t *handle)
{
    if (!handle) {
        return -1;
    }
    
    LOG_SVC_INFO("Cancelling download");
    handle->status = OTA_DOWNLOAD_STATUS_CANCELLED;
    
    // TODO: Implement cancel logic
    // 1. Stop HTTP transfer
    // 2. Close file handles
    // 3. Clean up resources
    
    // Call status callback
    if (handle->config.status_cb) {
        handle->config.status_cb(handle->status, 0, handle->config.user_data);
    }
    
    return 0;
}

ota_download_status_t ota_download_get_status(const ota_download_handle_t *handle)
{
    if (!handle) {
        return OTA_DOWNLOAD_STATUS_IDLE;
    }
    return handle->status;
}

int ota_download_get_progress(const ota_download_handle_t *handle, uint64_t *downloaded_bytes, uint64_t *total_bytes)
{
    if (!handle || !downloaded_bytes || !total_bytes) {
        return -1;
    }
    
    *downloaded_bytes = handle->downloaded_bytes;
    *total_bytes = handle->total_bytes;
    
    return 0;
}

int ota_web_upgrade_start(ota_web_upgrade_handle_t *handle, FirmwareType fw_type, const ota_web_upgrade_config_t *config)
{
    if (!handle || !config || fw_type >= FIRMWARE_TYPE_COUNT) {
        return -1;
    }
    
    LOG_SVC_INFO("Starting web upgrade for firmware type %d", fw_type);
    
    // Initialize handle
    memset(handle, 0, sizeof(ota_web_upgrade_handle_t));
    handle->fw_type = fw_type;
    memcpy(&handle->config, config, sizeof(ota_web_upgrade_config_t));
    handle->status = OTA_WEB_UPGRADE_STATUS_DOWNLOADING;
    
    // Step 1: Start download
    int result = ota_download_start(&handle->download_handle, &config->download_config);
    if (result != 0) {
        handle->status = OTA_WEB_UPGRADE_STATUS_FAILED;
        strcpy(handle->last_error, "Download start failed");
        return -1;
    }
    
    // TODO: Implement complete web upgrade flow
    // This is a placeholder implementation
    
    return 0;
}

int ota_web_upgrade_cancel(ota_web_upgrade_handle_t *handle)
{
    if (!handle) {
        return -1;
    }
    
    LOG_SVC_INFO("Cancelling web upgrade");
    handle->status = OTA_WEB_UPGRADE_STATUS_CANCELLED;
    
    // Cancel download if in progress
    if (handle->status == OTA_WEB_UPGRADE_STATUS_DOWNLOADING) {
        ota_download_cancel(&handle->download_handle);
    }
    
    // TODO: Implement cancel logic
    // 1. Cancel download
    // 2. Clean up temp files
    // 3. Reset state
    
    return 0;
}

ota_web_upgrade_status_t ota_web_upgrade_get_status(const ota_web_upgrade_handle_t *handle)
{
    if (!handle) {
        return OTA_WEB_UPGRADE_STATUS_IDLE;
    }
    return handle->status;
}

int ota_web_upgrade_get_progress(const ota_web_upgrade_handle_t *handle, uint32_t *progress)
{
    if (!handle || !progress) {
        return -1;
    }
    
    *progress = 0;
    
    switch (handle->status) {
        case OTA_WEB_UPGRADE_STATUS_DOWNLOADING: {
            uint64_t downloaded, total;
            if (ota_download_get_progress(&handle->download_handle, &downloaded, &total) == 0 && total > 0) {
                *progress = (uint32_t)((downloaded * 50) / total); // Download is 50% of total progress
            }
            break;
        }
        case OTA_WEB_UPGRADE_STATUS_VALIDATING:
            *progress = 50; // Validation is 50-70% of total progress
            break;
        case OTA_WEB_UPGRADE_STATUS_UPGRADING:
            *progress = 70; // Upgrade is 70-100% of total progress
            break;
        case OTA_WEB_UPGRADE_STATUS_COMPLETED:
            *progress = 100;
            break;
        default:
            *progress = 0;
            break;
    }
    
    return 0;
}

const char* ota_web_upgrade_get_error(const ota_web_upgrade_handle_t *handle)
{
    if (!handle) {
        return "Invalid handle";
    }
    return handle->last_error;
}

/* ==================== Standard Service Interface ==================== */

aicam_result_t ota_service_init(void *config)
{
    if (g_ota_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing OTA Service...");
    
    // TODO: Initialize flash operations
    // The actual flash operations should be provided by the caller
    // For now, we'll just mark as initialized
    
    g_ota_service.initialized = AICAM_TRUE;
    g_ota_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_SVC_INFO("OTA Service initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t ota_service_start(void)
{
    if (!g_ota_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_ota_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting OTA Service...");
    
    // TODO: Start OTA-specific components
    // - Check for pending updates
    // - Start update monitoring
    
    g_ota_service.running = AICAM_TRUE;
    g_ota_service.state = SERVICE_STATE_RUNNING;
    
    LOG_SVC_INFO("OTA Service started successfully");
    
    return AICAM_OK;
}

aicam_result_t ota_service_stop(void)
{
    if (!g_ota_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_ota_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping OTA Service...");
    
    // TODO: Stop OTA-specific components
    // - Stop update monitoring
    // - Cancel ongoing updates
    
    g_ota_service.running = AICAM_FALSE;
    g_ota_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_SVC_INFO("OTA Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t ota_service_deinit(void)
{
    if (!g_ota_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_ota_service.running) {
        ota_service_stop();
    }
    
    LOG_SVC_INFO("Deinitializing OTA Service...");
    
    // TODO: Deinitialize OTA-specific components
    
    // Reset context
    memset(&g_ota_service, 0, sizeof(ota_service_context_t));
    
    LOG_SVC_INFO("OTA Service deinitialized successfully");
    
    return AICAM_OK;
}

service_state_t ota_service_get_state(void)
{
    return g_ota_service.state;
}