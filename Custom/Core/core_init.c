/**
 * @file core_init.c
 * @brief L2 Core System Service Layer Initialization Implementation
 */

#include "core_init.h"
#include "version.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>

/* ==================== Private Variables ==================== */

static core_system_info_t g_core_system_info = {0};
static void (*g_error_handler)(aicam_result_t error) = NULL;

/* ==================== Private Function Declarations ==================== */

static aicam_result_t init_stage_with_timeout(aicam_result_t (*init_func)(void), 
                                              const char* stage_name,
                                              uint32_t timeout_ms);
static void update_module_status(const char* module_name, bool status);
static aicam_result_t validate_system_integrity(void);

/* ==================== Public Function Implementations ==================== */

aicam_result_t core_system_init(void)
{
    if (g_core_system_info.state != CORE_STATE_UNINITIALIZED) {
        return AICAM_ERROR_BUSY;
    }
    
    g_core_system_info.state = CORE_STATE_INITIALIZING;
    g_core_system_info.init_time = osKernelGetTickCount();
    
    // printf("[CORE] Starting L2 Core System initialization...\r\n");
    
    // Stage 1: Basic stage (Log system)
    aicam_result_t status = core_init_basic_stage();
    if (status != AICAM_OK) {
        g_core_system_info.init_failures++;
        g_core_system_info.state = CORE_STATE_ERROR;
        return status;
    }
    
    // Stage 2: Configuration stage (Configuration management)
    status = core_init_config_stage();
    if (status != AICAM_OK) {
        g_core_system_info.init_failures++;
        g_core_system_info.state = CORE_STATE_ERROR;
        return status;
    }
    
    // Stage 3: Memory stage (Buffer management)
    status = core_init_memory_stage();
    if (status != AICAM_OK) {
        g_core_system_info.init_failures++;
        g_core_system_info.state = CORE_STATE_ERROR;
        return status;
    }
    
    // Stage 4: Communication stage (Event bus)
    // status = core_init_communication_stage();
    // if (status != AICAM_OK) {
    //     g_core_system_info.init_failures++;
    //     g_core_system_info.state = CORE_STATE_ERROR;
    //     return status;
    // }
    
    // Stage 5: Services stage (Timer management)
    status = core_init_services_stage();
    if (status != AICAM_OK) {
        g_core_system_info.init_failures++;
        g_core_system_info.state = CORE_STATE_ERROR;
        return status;
    }

    // Stage 6: Security stage (Authentication manager)
    status = core_init_security_stage();
    if (status != AICAM_OK) {
        g_core_system_info.init_failures++;
        g_core_system_info.state = CORE_STATE_ERROR;
        return status;
    }
    
    // Validate system integrity
    // status = validate_system_integrity();
    // if (status != AICAM_OK) {
    //     g_core_system_info.init_failures++;
    //     g_core_system_info.state = CORE_STATE_ERROR;
    //     return status;
    // }
    
    g_core_system_info.state = CORE_STATE_INITIALIZED;
    // printf("[CORE] L2 Core System initialization completed successfully\r\n");
    
    return AICAM_OK;
}

aicam_result_t core_system_deinit(void)
{
    if (g_core_system_info.state == CORE_STATE_UNINITIALIZED) {
        return AICAM_OK;
    }
    
    g_core_system_info.state = CORE_STATE_SHUTTING_DOWN;
    
    printf("[CORE] Starting L2 Core System deinitialization...\r\n");
    
    // Deinitialize in reverse order
    timer_mgr_deinit();
    event_bus_deinit();
    buffer_mgr_deinit();
    json_config_mgr_deinit();
    debug_system_deinit();
    
    // Reset all module status
    g_core_system_info.event_bus_ready = false;
    g_core_system_info.config_mgr_ready = false;
    g_core_system_info.timer_mgr_ready = false;
    g_core_system_info.buffer_mgr_ready = false;
    g_core_system_info.debug_system_ready = false;
    
    g_core_system_info.state = CORE_STATE_UNINITIALIZED;
    
    printf("[CORE] L2 Core System deinitialization completed\r\n");
    
    return AICAM_OK;
}

aicam_result_t core_system_start(void)
{
    if (g_core_system_info.state != CORE_STATE_INITIALIZED) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    printf("[CORE] Starting L2 Core System services...\r\n");
    
    g_core_system_info.state = CORE_STATE_RUNNING;
    
    // Wait for all services to be ready
    osDelay(100); // Wait for event processing to complete
    
    printf("[CORE] L2 Core System is now running\r\n");
    
    return AICAM_OK;
}

aicam_result_t core_system_stop(void)
{
    if (g_core_system_info.state != CORE_STATE_RUNNING) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    printf("[CORE] Stopping L2 Core System services...\r\n");
    
    // Stop all timers
    timer_mgr_stop_all();
    
    // Flush all pending events
    event_bus_flush();
    
    g_core_system_info.state = CORE_STATE_INITIALIZED;
    
    printf("[CORE] L2 Core System services stopped\r\n");
    
    return AICAM_OK;
}

aicam_result_t core_system_restart(void)
{
    printf("[CORE] Restarting L2 Core System...\r\n");
    
    // Stop system
    aicam_result_t status = core_system_stop();
    if (status != AICAM_OK) {
        return status;
    }
    
    // Deinitialize system
    status = core_system_deinit();
    if (status != AICAM_OK) {
        return status;
    }
    
    // Wait before restart
    osDelay(CORE_RESTART_DELAY_MS);
    
    // Reinitialize system
    status = core_system_init();
    if (status != AICAM_OK) {
        return status;
    }
    
    // Start system
    status = core_system_start();
    if (status != AICAM_OK) {
        return status;
    }
    
    g_core_system_info.restart_count++;
    
    printf("[CORE] L2 Core System restart completed\r\n");
    
    return AICAM_OK;
}

core_state_e core_system_get_state(void)
{
    return g_core_system_info.state;
}

aicam_result_t core_system_get_info(core_system_info_t* info)
{
    if (info == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Update uptime
    g_core_system_info.uptime = osKernelGetTickCount() - g_core_system_info.init_time;
    
    memcpy(info, &g_core_system_info, sizeof(core_system_info_t));
    
    return AICAM_OK;
}

aicam_result_t core_system_health_check(void)
{
    // Check if all critical modules are ready
    if (!g_core_system_info.event_bus_ready ||
        !g_core_system_info.config_mgr_ready ||
        !g_core_system_info.debug_system_ready ||
        !g_core_system_info.buffer_mgr_ready ||
        !g_core_system_info.timer_mgr_ready) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Check system state
    if (g_core_system_info.state != CORE_STATE_RUNNING &&
        g_core_system_info.state != CORE_STATE_INITIALIZED) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Additional health checks can be added here
    // For example: memory usage, queue depths, error counts, etc.
    
    return AICAM_OK;
}

aicam_result_t core_system_get_version(char* version, size_t size)
{
    if (version == NULL || size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    snprintf(version, size, FW_VERSION_STRING);
    
    return AICAM_OK;
}

aicam_result_t core_system_register_error_handler(void (*error_handler)(aicam_result_t error))
{
    if (error_handler == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    g_error_handler = error_handler;
    
    return AICAM_OK;
}

void core_system_handle_error(aicam_result_t error, const char* source, const char* message)
{
    g_core_system_info.error_count++;
    
    printf("[CORE ERROR] Source: %s, Error: %d, Message: %s\r\n", 
           source ? source : "Unknown", error, message ? message : "No message");
    
    if (g_error_handler != NULL) {
        g_error_handler(error);
    }
    
    // Auto-restart on critical errors if error count is below threshold
    if (g_core_system_info.error_count < CORE_MAX_ERROR_COUNT) {
        if (error == AICAM_ERROR_HARDWARE || 
            error == AICAM_ERROR_FIRMWARE || 
            error == AICAM_ERROR_CORRUPTED) {
            printf("[CORE] Critical error detected, initiating system restart...\r\n");
            core_system_restart();
        }
    } else {
        printf("[CORE] Too many errors, system halted\r\n");
        g_core_system_info.state = CORE_STATE_ERROR;
    }
}

/* ==================== Module-Specific Initialization Functions ==================== */

aicam_result_t core_init_basic_stage(void)
{
    // printf("[CORE] Initializing basic stage...\r\n");
    
    // Initialize log system first 
    aicam_result_t status = init_stage_with_timeout(debug_system_init, "Debug System", CORE_INIT_TIMEOUT_MS);
    if (status != AICAM_OK) {
        return status;
    }
    update_module_status("debug_system", true);
    
    return AICAM_OK;
}

aicam_result_t core_init_config_stage(void)
{
    // printf("[CORE] Initializing configuration stage...\r\n");

    aicam_result_t status = init_stage_with_timeout(json_config_mgr_init, "Configuration Manager", CORE_INIT_TIMEOUT_MS);
    if (status != AICAM_OK) {
        return status;
    }
    update_module_status("config_mgr", true);
    
    // printf("[CORE] Configuration stage initialization completed\r\n");
    
    return AICAM_OK;
}

aicam_result_t core_init_memory_stage(void)
{
    // printf("[CORE] Initializing memory stage...\r\n");
    
    aicam_result_t status = init_stage_with_timeout(buffer_mgr_init, "Buffer Manager", CORE_INIT_TIMEOUT_MS);
    
    if (status == AICAM_OK) {
        update_module_status("buffer_mgr", true);
        // printf("[CORE] Memory stage initialization completed\r\n");
    }
    
    return AICAM_OK;
}

aicam_result_t core_init_communication_stage(void)
{
    // printf("[CORE] Initializing communication stage...\r\n");
    
    aicam_result_t status = init_stage_with_timeout(event_bus_init, "Event Bus", CORE_INIT_TIMEOUT_MS);
    
    if (status == AICAM_OK) {
        update_module_status("event_bus", true);
        // printf("[CORE] Communication stage initialization completed\r\n");
    }
    
    return status;
}

aicam_result_t core_init_services_stage(void)
{
    // printf("[CORE] Initializing services stage...\r\n");
    
    //TODO: init timer manager
    
    // aicam_result_t status = init_stage_with_timeout(timer_mgr_init, "Timer Manager", CORE_INIT_TIMEOUT_MS);

    update_module_status("timer_mgr", true);

    return AICAM_OK;
}

aicam_result_t core_init_security_stage(void)
{
    // printf("[CORE] Initializing security stage...\r\n");
    
    aicam_result_t status = init_stage_with_timeout(auth_mgr_init, "Authentication Manager", CORE_INIT_TIMEOUT_MS);
    
    if (status == AICAM_OK) {
        update_module_status("auth_mgr", true);
        // printf("[CORE] Security stage initialization completed\r\n");
    }
    
    return status;
}

/* ==================== Private Function Implementations ==================== */

static aicam_result_t init_stage_with_timeout(aicam_result_t (*init_func)(void), 
                                              const char* stage_name,
                                              uint32_t timeout_ms)
{
    if (init_func == NULL || stage_name == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // printf("[CORE] Initializing %s...\r\n", stage_name);
    
    uint32_t start_time = osKernelGetTickCount();
    
    aicam_result_t status = init_func();
    
    uint32_t elapsed_time = osKernelGetTickCount() - start_time;
    
    if (status == AICAM_OK) {
        // printf("[CORE] %s initialized successfully (took %lu ms)\r\n", stage_name, elapsed_time);
    } else {
        printf("[CORE] %s initialization failed: %d (took %lu ms)\r\n", stage_name, status, elapsed_time);
        
        if (elapsed_time >= timeout_ms) {
            printf("[CORE] %s initialization timeout\r\n", stage_name);
            return AICAM_ERROR_TIMEOUT;
        }
    }
    
    return status;
}

static void update_module_status(const char* module_name, bool status)
{
    if (module_name == NULL) {
        return;
    }
    
    if (strcmp(module_name, "event_bus") == 0) {
        g_core_system_info.event_bus_ready = status;
    } else if (strcmp(module_name, "config_mgr") == 0) {
        g_core_system_info.config_mgr_ready = status;
    } else if (strcmp(module_name, "debug_system") == 0) {
        g_core_system_info.debug_system_ready = status;
    } else if (strcmp(module_name, "timer_mgr") == 0) {
        g_core_system_info.timer_mgr_ready = status;
    } else if (strcmp(module_name, "buffer_mgr") == 0) {
        g_core_system_info.buffer_mgr_ready = status;
    } else if (strcmp(module_name, "auth_mgr") == 0) {
        g_core_system_info.auth_mgr_ready = status;
    }
}

__attribute__((unused)) static aicam_result_t validate_system_integrity(void)
{
    // Check if all required modules are initialized
    if (!g_core_system_info.event_bus_ready ||
        !g_core_system_info.config_mgr_ready ||
        !g_core_system_info.debug_system_ready ||
        !g_core_system_info.timer_mgr_ready ||
        !g_core_system_info.buffer_mgr_ready ||
        !g_core_system_info.auth_mgr_ready ) 
    {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Additional integrity checks can be added here
    
    return AICAM_OK;
} 
