/**
 * @file ota_service.h
 * @brief OTA Service Interface Header - A/B Partition Streaming Upgrade
 * @details A/B partition streaming upgrade OTA service interface definition
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "upgrade_manager.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants and Enums ==================== */

// Type aliases for compatibility
typedef SlotStatus ota_slot_status_t;
typedef slot_info_t ota_slot_info_t;

/* ==================== Core OTA Interface Functions ==================== */

/**
 * @brief Get active partition offset for specified firmware type
 * @param type Firmware type
 * @return Partition offset address, 0 if failed
 */
uint32_t ota_get_active_partition(FirmwareType type);

/**
 * @brief Get update partition offset for specified firmware type
 * @param type Firmware type
 * @return Partition offset address, 0 if failed
 */
uint32_t ota_get_update_partition(FirmwareType type);

/**
 * @brief Begin firmware upgrade process
 * @param handle Upgrade handle pointer
 * @param type Firmware type
 * @param header Firmware header information
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_begin(upgrade_handle_t *handle, FirmwareType type, firmware_header_t *header);

/**
 * @brief Begin firmware upgrade at an absolute flash address (bundle direct mode)
 * @param handle Upgrade handle pointer
 * @param type Firmware type
 * @param header Firmware header information
 * @param flash_addr Absolute flash address (e.g. APP1_BASE), 4K-aligned
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_begin_direct(upgrade_handle_t *handle, FirmwareType type, firmware_header_t *header, uint32_t flash_addr);

/**
 * @brief Write firmware data chunk to update partition
 * @param handle Upgrade handle pointer
 * @param chunk_data Data chunk pointer
 * @param chunk_size Data chunk size
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_write_chunk(upgrade_handle_t *handle, const void *chunk_data, size_t chunk_size);

/**
 * @brief Finish firmware upgrade process
 * @param handle Upgrade handle pointer
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_finish(upgrade_handle_t *handle);

/**
 * @brief Begin firmware read process
 * @param handle Upgrade handle pointer
 * @param type Firmware type
 * @param slot_idx Slot index (SLOT_A or SLOT_B)
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_read_begin(upgrade_handle_t *handle, FirmwareType type, int slot_idx);

/**
 * @brief Read firmware data chunk from specified partition
 * @param handle Upgrade handle pointer
 * @param buffer Read buffer pointer
 * @param read_size Read size
 * @return Actual bytes read, 0 on failure or end of read
 */
uint32_t ota_upgrade_read_chunk(upgrade_handle_t *handle, void *buffer, size_t read_size);

/**
 * @brief Get slot boot try count for specified firmware type
 * @param type Firmware type
 * @return Try count, 0 on failure
 */
uint32_t ota_get_slot_try_count(FirmwareType type);

/**
 * @brief Set slot boot success flag
 * @param type Firmware type
 * @param success Boot success flag
 */
void ota_set_slot_boot_success(FirmwareType type, bool success);

/**
 * @brief Switch active slot for specified firmware type
 * @param type Firmware type
 * @return 0 on success, -1 on failure
 */
int ota_switch_active_slot(FirmwareType type);

/**
 * @brief Check and select boot slot (from upgrade_manager)
 * @param type Firmware type
 * @return 0 on success, -1 on failure
 */
int ota_check_and_select_boot_slot(FirmwareType type);

/* ==================== System State Management ==================== */

/**
 * @brief Initialize system state with flash operations
 * @param read Flash read function
 * @param write Flash write function
 * @param erase Flash erase function
 */
void ota_init_system_state(upgrade_flash_read read, upgrade_flash_write write, upgrade_flash_erase erase);

/**
 * @brief Save system state to storage
 */
void ota_save_system_state(void);

/**
 * @brief Get current system state
 * @return System state pointer, NULL on failure
 */
SystemState* ota_get_system_state(void);

/**
 * @brief Get slot information
 * @param type Firmware type
 * @param slot_idx Slot index
 * @return Slot info pointer, NULL on failure
 */
const slot_info_t* ota_get_slot_info(FirmwareType type, int slot_idx);

/**
 * @brief Get slot version as string
 * @param type Firmware type
 * @param slot_idx Slot index
 * @param buf Output buffer (at least 20 bytes)
 * @param buf_size Buffer size
 * @return 0 on success, -1 on failure
 */
int ota_get_slot_version_string(FirmwareType type, int slot_idx, char *buf, size_t buf_size);

/**
 * @brief Compare slot version with another version
 * @param type Firmware type
 * @param slot_idx Slot index
 * @param ver Version to compare (8 bytes: [major, minor, patch, build_low, build_high, ...])
 * @return >0 if slot > ver, <0 if slot < ver, 0 if equal, -999 on error
 */
int ota_compare_slot_version(FirmwareType type, int slot_idx, const uint8_t *ver);

/* ==================== Utility Functions ==================== */

/**
 * @brief Calculate CRC32 for data (using upgrade_manager implementation)
 * @param data Data pointer
 * @param size Data size
 * @return CRC32 value
 */
uint32_t ota_calculate_crc32(uint32_t *data, size_t size);

/* ==================== Standard Service Interface ==================== */

/**
 * @brief Initialize OTA service
 * @param config Service configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t ota_service_init(void *config);

/**
 * @brief Start OTA service
 * @return aicam_result_t Operation result
 */
aicam_result_t ota_service_start(void);

/**
 * @brief Stop OTA service
 * @return aicam_result_t Operation result
 */
aicam_result_t ota_service_stop(void);

/**
 * @brief Deinitialize OTA service
 * @return aicam_result_t Operation result
 */
aicam_result_t ota_service_deinit(void);

/**
 * @brief Get OTA service state
 * @return service_state_t Service state
 */
service_state_t ota_service_get_state(void);

/* ==================== Pre-upgrade Validation ==================== */

/**
 * @brief Validation result enumeration
 */
typedef enum {
    OTA_VALIDATION_OK = 0,
    OTA_VALIDATION_ERROR_INVALID_PARAMS,
    OTA_VALIDATION_ERROR_FILE_NOT_FOUND,
    OTA_VALIDATION_ERROR_FILE_SIZE,
    OTA_VALIDATION_ERROR_HEADER_INVALID,
    OTA_VALIDATION_ERROR_CRC32_MISMATCH,
    OTA_VALIDATION_ERROR_VERSION_INVALID,
    OTA_VALIDATION_ERROR_PARTITION_FULL,
    OTA_VALIDATION_ERROR_SYSTEM_STATE,
    OTA_VALIDATION_ERROR_SIGNATURE_INVALID,
    OTA_VALIDATION_ERROR_HARDWARE_INCOMPATIBLE
} ota_validation_result_t;

/**
 * @brief Validation options structure
 */
typedef struct {
    aicam_bool_t validate_crc32;           // Validate CRC32 checksum
    aicam_bool_t validate_signature;       // Validate digital signature
    aicam_bool_t validate_version;         // Validate version compatibility
    aicam_bool_t validate_hardware;        // Validate hardware compatibility
    aicam_bool_t validate_partition_size;  // Validate partition size
    aicam_bool_t allow_downgrade;          // Allow downgrade to older version
    uint32_t min_version;                  // Minimum allowed version
    uint32_t max_version;                  // Maximum allowed version
} ota_validation_options_t;

/**
 * @brief Validate firmware file before upgrade
 * @param fw_type Firmware type
 * @param filename File path
 * @param options Validation options
 * @return Validation result
 */
ota_validation_result_t ota_validate_firmware_file(FirmwareType fw_type, const char *filename, const ota_validation_options_t *options);

/**
 * @brief Validate firmware header
 * @param header Firmware header
 * @param fw_type Firmware type
 * @param options Validation options
 * @return Validation result
 */
ota_validation_result_t ota_validate_firmware_header(const firmware_header_t *header, FirmwareType fw_type, const ota_validation_options_t *options);

/**
 * @brief Validate system state before upgrade
 * @param fw_type Firmware type
 * @return Validation result
 */
ota_validation_result_t ota_validate_system_state(FirmwareType fw_type);

/**
 * @brief Validate partition availability
 * @param fw_type Firmware type
 * @param required_size Required partition size
 * @return Validation result
 */
ota_validation_result_t ota_validate_partition_availability(FirmwareType fw_type, uint32_t required_size);

/**
 * @brief Calculate file CRC32
 * @param filename File path
 * @param crc32 Calculated CRC32 (output)
 * @return 0 on success, -1 on failure
 */
int ota_calculate_file_crc32(const char *filename, uint32_t *crc32);

/**
 * @brief Get validation result string
 * @param result Validation result
 * @return Result description string
 */
const char* ota_get_validation_result_string(ota_validation_result_t result);

/* ==================== Web Download Upgrade ==================== */

/**
 * @brief Download progress callback function type
 * @param downloaded_bytes Bytes downloaded so far
 * @param total_bytes Total bytes to download
 * @param user_data User data pointer
 */
typedef void (*ota_download_progress_callback_t)(uint64_t downloaded_bytes, uint64_t total_bytes, void *user_data);

/**
 * @brief Download status callback function type
 * @param status Download status
 * @param error_code Error code if failed
 * @param user_data User data pointer
 */
typedef void (*ota_download_status_callback_t)(int status, int error_code, void *user_data);

/**
 * @brief Download configuration structure
 */
typedef struct {
    char url[512];                                    // Download URL
    char temp_filename[256];                          // Temporary file path
    char final_filename[256];                         // Final file path
    uint32_t timeout_ms;                              // Download timeout
    uint32_t retry_count;                             // Retry count
    uint32_t chunk_size;                              // Download chunk size
    aicam_bool_t resume_download;                     // Enable resume download
    ota_download_progress_callback_t progress_cb;     // Progress callback
    ota_download_status_callback_t status_cb;         // Status callback
    void *user_data;                                  // User data
} ota_download_config_t;

/**
 * @brief Download status enumeration
 */
typedef enum {
    OTA_DOWNLOAD_STATUS_IDLE = 0,
    OTA_DOWNLOAD_STATUS_CONNECTING,
    OTA_DOWNLOAD_STATUS_DOWNLOADING,
    OTA_DOWNLOAD_STATUS_PAUSED,
    OTA_DOWNLOAD_STATUS_COMPLETED,
    OTA_DOWNLOAD_STATUS_FAILED,
    OTA_DOWNLOAD_STATUS_CANCELLED
} ota_download_status_t;

/**
 * @brief Download handle structure
 */
typedef struct {
    ota_download_config_t config;
    ota_download_status_t status;
    uint64_t downloaded_bytes;
    uint64_t total_bytes;
    uint32_t retry_count;
    uint32_t error_code;
    char last_error[128];
    void *http_handle;                                // HTTP client handle
    void *file_handle;                                // File handle
} ota_download_handle_t;

/**
 * @brief Web upgrade configuration structure
 */
typedef struct {
    ota_download_config_t download_config;
    ota_validation_options_t validation_options;
    aicam_bool_t auto_upgrade;                        // Auto upgrade after download
    aicam_bool_t delete_temp_file;                    // Delete temp file after upgrade
    aicam_bool_t backup_current_firmware;             // Backup current firmware
} ota_web_upgrade_config_t;

/**
 * @brief Web upgrade status enumeration
 */
typedef enum {
    OTA_WEB_UPGRADE_STATUS_IDLE = 0,
    OTA_WEB_UPGRADE_STATUS_DOWNLOADING,
    OTA_WEB_UPGRADE_STATUS_VALIDATING,
    OTA_WEB_UPGRADE_STATUS_UPGRADING,
    OTA_WEB_UPGRADE_STATUS_COMPLETED,
    OTA_WEB_UPGRADE_STATUS_FAILED,
    OTA_WEB_UPGRADE_STATUS_CANCELLED
} ota_web_upgrade_status_t;

/**
 * @brief Web upgrade handle structure
 */
typedef struct {
    FirmwareType fw_type;
    ota_web_upgrade_config_t config;
    ota_web_upgrade_status_t status;
    ota_download_handle_t download_handle;
    upgrade_handle_t upgrade_handle;
    uint32_t error_code;
    char last_error[128];
} ota_web_upgrade_handle_t;

/**
 * @brief Start web download
 * @param handle Download handle
 * @param config Download configuration
 * @return 0 on success, -1 on failure
 */
int ota_download_start(ota_download_handle_t *handle, const ota_download_config_t *config);

/**
 * @brief Pause web download
 * @param handle Download handle
 * @return 0 on success, -1 on failure
 */
int ota_download_pause(ota_download_handle_t *handle);

/**
 * @brief Resume web download
 * @param handle Download handle
 * @return 0 on success, -1 on failure
 */
int ota_download_resume(ota_download_handle_t *handle);

/**
 * @brief Cancel web download
 * @param handle Download handle
 * @return 0 on success, -1 on failure
 */
int ota_download_cancel(ota_download_handle_t *handle);

/**
 * @brief Get download status
 * @param handle Download handle
 * @return Download status
 */
ota_download_status_t ota_download_get_status(const ota_download_handle_t *handle);

/**
 * @brief Get download progress
 * @param handle Download handle
 * @param downloaded_bytes Downloaded bytes (output)
 * @param total_bytes Total bytes (output)
 * @return 0 on success, -1 on failure
 */
int ota_download_get_progress(const ota_download_handle_t *handle, uint64_t *downloaded_bytes, uint64_t *total_bytes);

/**
 * @brief Start web upgrade process
 * @param handle Web upgrade handle
 * @param fw_type Firmware type
 * @param config Web upgrade configuration
 * @return 0 on success, -1 on failure
 */
int ota_web_upgrade_start(ota_web_upgrade_handle_t *handle, FirmwareType fw_type, const ota_web_upgrade_config_t *config);

/**
 * @brief Cancel web upgrade process
 * @param handle Web upgrade handle
 * @return 0 on success, -1 on failure
 */
int ota_web_upgrade_cancel(ota_web_upgrade_handle_t *handle);

/**
 * @brief Get web upgrade status
 * @param handle Web upgrade handle
 * @return Web upgrade status
 */
ota_web_upgrade_status_t ota_web_upgrade_get_status(const ota_web_upgrade_handle_t *handle);

/**
 * @brief Get web upgrade progress
 * @param handle Web upgrade handle
 * @param progress Progress percentage (0-100)
 * @return 0 on success, -1 on failure
 */
int ota_web_upgrade_get_progress(const ota_web_upgrade_handle_t *handle, uint32_t *progress);

/**
 * @brief Get web upgrade error message
 * @param handle Web upgrade handle
 * @return Error message string
 */
const char* ota_web_upgrade_get_error(const ota_web_upgrade_handle_t *handle);

/* ==================== File-based Upgrade Examples ==================== */

/**
 * @brief Upgrade firmware from file with validation
 * @param fw_type Firmware type
 * @param filename File path
 * @param options Validation options
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_from_file(int fw_type, const char *filename, const ota_validation_options_t *options);

/**
 * @brief Dump firmware to file
 * @param fw_type Firmware type
 * @param slot_idx Slot index
 * @param filename File path
 * @return 0 on success, -1 on failure
 */
int ota_dump_firmware(int fw_type, int slot_idx, const char *filename);

/**
 * @brief Upgrade firmware from memory buffer with validation
 * @param fw_type Firmware type
 * @param firmware_data Firmware data buffer (must include OTA header)
 * @param firmware_size Firmware data size (including header)
 * @param options Validation options
 * @return 0 on success, -1 on failure
 */
int ota_upgrade_from_memory(int fw_type, const void *firmware_data, size_t firmware_size, const ota_validation_options_t *options);

/**
 * @brief Dump firmware to memory buffer
 * @param fw_type Firmware type
 * @param slot_idx Slot index
 * @param buffer Output buffer (must be large enough for header + firmware data)
 * @param buffer_size Buffer size
 * @param actual_size Actual size written (output)
 * @return 0 on success, -1 on failure
 */
int ota_dump_firmware_to_memory(int fw_type, int slot_idx, void *buffer, size_t buffer_size, size_t *actual_size);

#ifdef __cplusplus
}
#endif

#endif /* OTA_SERVICE_H */