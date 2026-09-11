/**
 * @file core_init.h
 * @brief L2 Core System Service Layer Initialization Header File
 */

#ifndef CORE_INIT_H
#define CORE_INIT_H

#include "aicam_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Core Module Header File Includes ==================== */

// System core modules
#include "event_bus.h"
#include "json_config_mgr.h"
#include "debug.h"
#include "timer_mgr.h"
#include "auth_mgr.h"

// Data core modules
#include "buffer_mgr.h"

/* ==================== Initialization Order Definitions ==================== */

/**
 * @brief Core module initialization order enumeration
 */
typedef enum {
    CORE_INIT_STAGE_BASIC = 0,      // Basic stage: log system
    CORE_INIT_STAGE_STORAGE,        // Storage stage: file system, configuration management
    CORE_INIT_STAGE_MEMORY,         // Memory stage: buffer management
    CORE_INIT_STAGE_COMMUNICATION,  // Communication stage: event bus
    CORE_INIT_STAGE_SERVICES,       // Services stage: timer management
    CORE_INIT_STAGE_MAX
} core_init_stage_e;

/**
 * @brief Core system state enumeration
 */
typedef enum {
    CORE_STATE_UNINITIALIZED = 0,  // Uninitialized
    CORE_STATE_INITIALIZING,       // Initializing
    CORE_STATE_INITIALIZED,        // Initialized
    CORE_STATE_RUNNING,            // Running
    CORE_STATE_SHUTTING_DOWN,      // Shutting down
    CORE_STATE_ERROR               // Error state
} core_state_e;

/**
 * @brief Core system statistics
 */
typedef struct {
    core_state_e state;             // System state
    uint32_t init_time;             // Initialization time
    uint32_t uptime;                // Uptime
    uint32_t init_failures;         // Initialization failure count
    uint32_t restart_count;         // Restart count
    uint32_t error_count;           // Error count
    
    // Module states
    bool event_bus_ready;           // Event bus ready
    bool config_mgr_ready;          // Configuration manager ready
    bool debug_system_ready;        // Debug system ready
    bool timer_mgr_ready;           // Timer manager ready
    bool buffer_mgr_ready;          // Buffer manager ready
    bool auth_mgr_ready;            // Authentication manager ready
} core_system_info_t;

/* ==================== Interface Function Declarations ==================== */

/**
 * @brief Initialize L2 core system service layer
 * @details Initialize all core modules in predefined order
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_init(void);

/**
 * @brief Deinitialize L2 core system service layer
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_deinit(void);

/**
 * @brief Start core system services
 * @details Called after all modules are initialized to start system services
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_start(void);

/**
 * @brief Stop core system services
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_stop(void);

/**
 * @brief Restart core system
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_restart(void);

/**
 * @brief Get core system state
 * @return core_state_e System state
 */
core_state_e core_system_get_state(void);

/**
 * @brief Get core system information
 * @param info System information structure pointer (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_get_info(core_system_info_t* info);

/**
 * @brief Check core system health status
 * @return aicam_result_t Health status result
 */
aicam_result_t core_system_health_check(void);


/**
 * @brief Get core system version information
 * @param version Version string buffer
 * @param size Buffer size
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_get_version(char* version, size_t size);

/**
 * @brief Register core system error handler
 * @param error_handler Error handler callback function
 * @return aicam_result_t Operation result
 */
aicam_result_t core_system_register_error_handler(void (*error_handler)(aicam_result_t error));

/**
 * @brief Trigger core system error handling
 * @param error Error code
 * @param source Error source module name
 * @param message Error message
 */
void core_system_handle_error(aicam_result_t error, const char* source, const char* message);

/* ==================== Module-Specific Initialization Functions ==================== */

/**
 * @brief Initialize basic stage modules
 * @return aicam_result_t Operation result
 */
aicam_result_t core_init_basic_stage(void);

/**
 * @brief Initialize storage stage modules
 * @return aicam_result_t Operation result
 */
aicam_result_t core_init_config_stage(void);

/**
 * @brief Initialize memory stage modules
 * @return aicam_result_t Operation result
 */
aicam_result_t core_init_memory_stage(void);

/**
 * @brief Initialize communication stage modules
 * @return aicam_result_t Operation result
 */
aicam_result_t core_init_communication_stage(void);

/**
 * @brief Initialize services stage modules
 * @return aicam_result_t Operation result
 */
aicam_result_t core_init_services_stage(void);

/**
 * @brief Initialize security stage modules
 * @return aicam_result_t Operation result
 */
aicam_result_t core_init_security_stage(void);


/* ==================== Configuration Macro Definitions ==================== */

#ifndef CORE_INIT_TIMEOUT_MS
#define CORE_INIT_TIMEOUT_MS 5000
#endif

#ifndef CORE_HEALTH_CHECK_INTERVAL_MS
#define CORE_HEALTH_CHECK_INTERVAL_MS 10000
#endif

#ifndef CORE_MAX_ERROR_COUNT
#define CORE_MAX_ERROR_COUNT 10
#endif

#ifndef CORE_RESTART_DELAY_MS
#define CORE_RESTART_DELAY_MS 1000
#endif

/* ==================== Debug and Diagnostic Macros ==================== */

#ifdef DEBUG
#define CORE_DEBUG_PRINT(fmt, ...) printf("[CORE] " fmt "\n", ##__VA_ARGS__)
#else
#define CORE_DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

#define CORE_ERROR_PRINT(fmt, ...) printf("[CORE ERROR] " fmt "\n", ##__VA_ARGS__)

/* ==================== Version Information ==================== */
/* Version macros (FW_VERSION_*) come from version.h (auto-generated by build system).
   Include "version.h" directly in .c files that need version info. */

#ifdef __cplusplus
}
#endif

#endif // CORE_INIT_H 
