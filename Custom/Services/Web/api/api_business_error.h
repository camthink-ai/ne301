/**
 * @file api_business_error.h
 * @brief Business Error Code Definitions for API Responses
 * @details Simple business error code definitions and conversion functions
 */

#ifndef API_BUSINESS_ERROR_H
#define API_BUSINESS_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Business Error Code Definitions ==================== */

/**
 * @brief Business error code enumeration
 * @note Error codes start from 1000 to avoid conflicts with system error codes
 */
typedef enum {
    API_BUSINESS_ERROR_NONE = 0,              ///< No error, operation successful
    
    /* Authentication & Authorization Errors (1001-1099) */
    API_BUSINESS_ERROR_INVALID_PASSWORD = 1001,       ///< Invalid password
    API_BUSINESS_ERROR_INVALID_CREDENTIALS = 1002,    ///< Invalid username or password
    API_BUSINESS_ERROR_UNAUTHORIZED = 1003,           ///< Unauthorized access
    API_BUSINESS_ERROR_SESSION_EXPIRED = 1004,        ///< Session expired
    API_BUSINESS_ERROR_TOKEN_INVALID = 1005,          ///< Invalid token
    API_BUSINESS_ERROR_PERMISSION_DENIED = 1007,      ///< Permission denied
    
    /* Parameter Validation Errors (1101-1199) */
    API_BUSINESS_ERROR_INVALID_PARAM = 1101,          ///< Invalid parameter
    API_BUSINESS_ERROR_MISSING_PARAM = 1102,          ///< Missing required parameter
    API_BUSINESS_ERROR_PARAM_OUT_OF_RANGE = 1103,     ///< Parameter out of valid range
    API_BUSINESS_ERROR_INVALID_FORMAT = 1104,         ///< Invalid data format
    
    /* Device & Hardware Errors (1201-1299) */
    API_BUSINESS_ERROR_DEVICE_OFFLINE = 1201,         ///< Device offline
    API_BUSINESS_ERROR_DEVICE_BUSY = 1202,            ///< Device busy
    API_BUSINESS_ERROR_CAMERA_ERROR = 1204,           ///< Camera error
    API_BUSINESS_ERROR_HARDWARE_ERROR = 1206,         ///< Hardware error
    
    /* Network & Communication Errors (1301-1399) */
    API_BUSINESS_ERROR_NETWORK_ERROR = 1301,          ///< Network error
    API_BUSINESS_ERROR_NETWORK_TIMEOUT = 1302,        ///< Network timeout
    API_BUSINESS_ERROR_MQTT_NOT_CONNECTED = 1304,     ///< MQTT not connected
    API_BUSINESS_ERROR_WIFI_NOT_CONNECTED = 1305,     ///< WiFi not connected
    
    /* Storage Errors (1401-1499) */
    API_BUSINESS_ERROR_STORAGE_FULL = 1401,           ///< Storage full
    API_BUSINESS_ERROR_FILE_NOT_FOUND = 1403,         ///< File not found
    API_BUSINESS_ERROR_INSUFFICIENT_SPACE = 1407,     ///< Insufficient storage space
    API_BUSINESS_ERROR_DIR_NOT_EMPTY = 1408,          ///< Directory not empty (cannot delete)
    
    /* AI & Model Errors (1501-1599) */
    API_BUSINESS_ERROR_MODEL_NOT_LOADED = 1501,       ///< AI model not loaded
    API_BUSINESS_ERROR_MODEL_INVALID = 1502,          ///< Invalid AI model
    API_BUSINESS_ERROR_MODEL_RELOAD_FAILED = 1503,    ///< Model reload failed
    API_BUSINESS_ERROR_INFERENCE_TIMEOUT = 1505,      ///< Inference timeout
    
    /* Configuration Errors (1601-1699) */
    API_BUSINESS_ERROR_CONFIG_INVALID = 1601,         ///< Invalid configuration
    API_BUSINESS_ERROR_CONFIG_NOT_FOUND = 1602,       ///< Configuration not found
    API_BUSINESS_ERROR_CONFIG_UPDATE_FAILED = 1604,   ///< Configuration update failed
    
    /* Operation Errors (1701-1799) */
    API_BUSINESS_ERROR_OPERATION_TIMEOUT = 1701,      ///< Operation timeout
    API_BUSINESS_ERROR_OPERATION_FAILED = 1702,       ///< Operation failed
    API_BUSINESS_ERROR_OPERATION_IN_PROGRESS = 1705,  ///< Operation already in progress
    API_BUSINESS_ERROR_AT_COMMAND_FAILED = 1706,      ///< AT command failed
    
    /* OTA & Firmware Errors (1801-1899) */
    API_BUSINESS_ERROR_FIRMWARE_INVALID = 1801,       ///< Invalid firmware
    API_BUSINESS_ERROR_OTA_IN_PROGRESS = 1804,        ///< OTA upgrade in progress
    API_BUSINESS_ERROR_OTA_FAILED = 1805,             ///< OTA upgrade failed
    API_BUSINESS_ERROR_OTA_HEADER_VALIDATION_FAILED = 1806, ///< OTA header validation failed
    
    /* Resource Errors (1901-1999) */
    API_BUSINESS_ERROR_RESOURCE_NOT_FOUND = 1901,     ///< Resource not found
    API_BUSINESS_ERROR_RESOURCE_BUSY = 1902,          ///< Resource busy
    
    /* Unknown Error */
    API_BUSINESS_ERROR_UNKNOWN = 9999                 ///< Unknown error
} api_business_error_code_t;

/* ==================== API Functions ==================== */

/**
 * @brief Convert error code number to string
 * @param error_code Business error code number
 * @return const char* Error code string, or "UNKNOWN" if not found
 */
const char* api_business_error_code_to_string(int32_t error_code);

/**
 * @brief Convert error code string to number
 * @param error_str Error code string (e.g., "INVALID_PASSWORD")
 * @return int32_t Error code number, or -1 if not found
 */
int32_t api_business_error_string_to_code(const char* error_str);

#ifdef __cplusplus
}
#endif

#endif /* API_BUSINESS_ERROR_H */

