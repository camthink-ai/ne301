/**
 * @file netif_init_manager.c
 * @brief Network Interface Initialization Manager Implementation
 */

#include "netif_init_manager.h"
#include "netif_manager.h"
#include "debug.h"
#include "drtc.h"
#include "common_utils.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"

/* ==================== Configuration ==================== */

#define MAX_NETIF_COUNT       6
#define NETIF_INIT_STACK_SIZE (4096 * 4)

/* One fixed stack per registration slot; stack_slot in entry survives priority sort. */
static uint8_t s_netif_init_stacks[MAX_NETIF_COUNT][NETIF_INIT_STACK_SIZE] ALIGN_32 IN_PSRAM;

/* ==================== Internal Types ==================== */

typedef struct {
    netif_init_config_t config;
    osThreadId_t task_id;
    osSemaphoreId_t ready_semaphore;
    uint8_t stack_slot;
} netif_init_entry_t;

typedef struct {
    aicam_bool_t initialized;
    osMutexId_t mutex;
    
    netif_init_entry_t entries[MAX_NETIF_COUNT];
    uint32_t entry_count;
} netif_init_manager_t;

/* ==================== Global Context ==================== */

static netif_init_manager_t g_netif_init_mgr = {0};

static void *netif_init_stack_for_slot(uint8_t stack_slot)
{
    if (stack_slot >= MAX_NETIF_COUNT) {
        return NULL;
    }
    return s_netif_init_stacks[stack_slot];
}

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Find entry by interface name
 */
static netif_init_entry_t* find_entry(const char *if_name)
{
    if (!if_name) return NULL;
    
    for (uint32_t i = 0; i < g_netif_init_mgr.entry_count; i++) {
        if (strcmp(g_netif_init_mgr.entries[i].config.if_name, if_name) == 0) {
            return &g_netif_init_mgr.entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Find entry index by interface name
 */
static int find_entry_index(const char *if_name)
{
    if (!if_name) return -1;
    
    for (uint32_t i = 0; i < g_netif_init_mgr.entry_count; i++) {
        if (strcmp(g_netif_init_mgr.entries[i].config.if_name, if_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Sort entries by priority (high priority first)
 */
static void sort_entries_by_priority(void)
{
    for (uint32_t i = 0; i < g_netif_init_mgr.entry_count - 1; i++) {
        for (uint32_t j = 0; j < g_netif_init_mgr.entry_count - i - 1; j++) {
            if (g_netif_init_mgr.entries[j].config.priority > 
                g_netif_init_mgr.entries[j + 1].config.priority) {
                netif_init_entry_t temp = g_netif_init_mgr.entries[j];
                g_netif_init_mgr.entries[j] = g_netif_init_mgr.entries[j + 1];
                g_netif_init_mgr.entries[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief Network interface initialization task
 */
static void netif_init_task(void *argument)
{
    const char *if_name = (const char *)argument;

    aicam_result_t result = AICAM_OK;
    
    // Find entry
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    netif_init_entry_t *entry = find_entry(if_name);
    if (!entry) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_ERROR("Entry not found for interface: %s", if_name);
        osThreadTerminate(osThreadGetId());
        return;
    }
    
    // Update state
    entry->config.state = NETIF_INIT_STATE_INITIALIZING;
    osMutexRelease(g_netif_init_mgr.mutex);
    
    // Record start time after releasing mutex (actual initialization starts here)
    uint64_t start_time_ms = rtc_get_uptime_ms();
    
    // Execute initialization
    LOG_DRV_INFO("Initializing interface: %s", if_name);
    int ret = netif_manager_ctrl(if_name, NETIF_CMD_INIT, NULL);
    if (ret != 0) {
        // Update state and timing on failure
        osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
        entry->config.state = NETIF_INIT_STATE_FAILED;
        entry->config.init_time_ms = (uint32_t)(rtc_get_uptime_ms() - start_time_ms);
        osMutexRelease(g_netif_init_mgr.mutex);

        LOG_DRV_INFO("Failed to initialize interface %s: %d", if_name, ret);
        
        // Release semaphore and call callback even on failure
        if (entry->ready_semaphore) {
            osSemaphoreRelease(entry->ready_semaphore);
        }
        if (entry->config.callback) {
            entry->config.callback(if_name, AICAM_ERROR);
        }
        osThreadTerminate(osThreadGetId());
        return;
    }

    if (strcmp(if_name, NETIF_NAME_WIFI_STA) == 0
#if NETIF_WIFI_HALOW_IS_ENABLE
        || strcmp(if_name, NETIF_NAME_WIFI_HALOW) == 0
#endif
    ) {
        goto end;
    }

    // Get netif config (only if INIT succeeded)
    netif_config_t netif_cfg;
    ret = nm_get_netif_cfg(if_name, &netif_cfg);
    if (ret != 0) {
        LOG_DRV_ERROR("Failed to get netif config for %s: %d", if_name, ret);
        // Continue with default config if get fails
        memset(&netif_cfg, 0, sizeof(netif_config_t));
    }
    
    // Reason: to accelerate the initialization process, we set the channel to 6
    // Only set channel for wireless interfaces
    if (strcmp(if_name, NETIF_NAME_WIFI_AP) == 0) {
        netif_cfg.wireless_cfg.channel = 6;
        
        // Set netif config (only for wireless interfaces that need channel setting)
        ret = nm_set_netif_cfg(if_name, &netif_cfg);
        if (ret != 0) {
            LOG_DRV_WARN("Failed to set netif config for %s: %d (continuing anyway)", if_name, ret);
            // Don't fail initialization if config set fails, as INIT already succeeded
        }
    }
    
    // Auto bring up if configured
    if (entry->config.auto_up) {
        LOG_DRV_INFO("Bringing up interface: %s", if_name);
        ret = netif_manager_ctrl(if_name, NETIF_CMD_UP, NULL);
        if (ret != 0) {
            LOG_DRV_ERROR("Failed to bring up interface %s: %d", if_name, ret);
            result = AICAM_ERROR;
        }
    }
    

end:
    // Calculate elapsed time
    uint64_t elapsed_time_ms = rtc_get_uptime_ms() - start_time_ms;
    
    // Update state and timing
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    entry->config.state = (result == AICAM_OK) ? NETIF_INIT_STATE_READY : NETIF_INIT_STATE_FAILED;
    entry->config.init_time_ms = (uint32_t)elapsed_time_ms;
    osMutexRelease(g_netif_init_mgr.mutex);
    
    LOG_DRV_INFO("Interface %s initialization %s (took %u ms)", 
                if_name,
                (result == AICAM_OK) ? "completed" : "failed",
                entry->config.init_time_ms);


    // Release semaphore
    if (entry->ready_semaphore) {
        osSemaphoreRelease(entry->ready_semaphore);
    }
    
    // Call callback
    if (entry->config.callback) {
        entry->config.callback(if_name, result);
    }

    osThreadTerminate(osThreadGetId());
}

/* ==================== Public API Implementation ==================== */

aicam_result_t netif_init_manager_framework_init(void)
{
    if (g_netif_init_mgr.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_DRV_INFO("Initializing network interface initialization manager");
    
    memset(&g_netif_init_mgr, 0, sizeof(netif_init_manager_t));
    
    // Create mutex
    g_netif_init_mgr.mutex = osMutexNew(NULL);
    if (!g_netif_init_mgr.mutex) {
        LOG_DRV_ERROR("Failed to create mutex");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    g_netif_init_mgr.initialized = AICAM_TRUE;
    
    LOG_DRV_INFO("Network interface initialization manager initialized");
    
    return AICAM_OK;
}

aicam_result_t netif_init_manager_deinit(void)
{
    if (!g_netif_init_mgr.initialized) {
        return AICAM_OK;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    // Clean up entries
    for (uint32_t i = 0; i < g_netif_init_mgr.entry_count; i++) {
        if (g_netif_init_mgr.entries[i].ready_semaphore) {
            osSemaphoreDelete(g_netif_init_mgr.entries[i].ready_semaphore);
        }
    }
    
    osMutexRelease(g_netif_init_mgr.mutex);
    osMutexDelete(g_netif_init_mgr.mutex);
    
    memset(&g_netif_init_mgr, 0, sizeof(netif_init_manager_t));
    
    return AICAM_OK;
}

aicam_result_t netif_init_manager_register(const netif_init_config_t *config)
{
    if (!g_netif_init_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!config || !config->if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    // Check if already registered
    if (find_entry(config->if_name) != NULL) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_WARN("Interface %s already registered", config->if_name);
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    // Check capacity
    if (g_netif_init_mgr.entry_count >= MAX_NETIF_COUNT) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_ERROR("Maximum number of interfaces reached");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Add new entry
    netif_init_entry_t *entry = &g_netif_init_mgr.entries[g_netif_init_mgr.entry_count];
    memcpy(&entry->config, config, sizeof(netif_init_config_t));
    
    // Create semaphore for wait operation
    entry->ready_semaphore = osSemaphoreNew(1, 0, NULL);
    if (!entry->ready_semaphore) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_ERROR("Failed to create semaphore for %s", config->if_name);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    entry->task_id = NULL;
    entry->stack_slot = (uint8_t)g_netif_init_mgr.entry_count;
    
    g_netif_init_mgr.entry_count++;
    
    // Sort by priority
    sort_entries_by_priority();
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    LOG_DRV_INFO("Registered interface: %s (priority: %d, async: %s, auto_up: %s)",
                config->if_name, config->priority,
                config->async ? "true" : "false",
                config->auto_up ? "true" : "false");
    
    return AICAM_OK;
}

aicam_result_t netif_init_manager_unregister(const char *if_name)
{
    if (!g_netif_init_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    int idx = find_entry_index(if_name);
    if (idx < 0) {
        osMutexRelease(g_netif_init_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Clean up semaphore
    if (g_netif_init_mgr.entries[idx].ready_semaphore) {
        osSemaphoreDelete(g_netif_init_mgr.entries[idx].ready_semaphore);
    }
    
    // Shift remaining entries
    for (uint32_t i = idx; i < g_netif_init_mgr.entry_count - 1; i++) {
        g_netif_init_mgr.entries[i] = g_netif_init_mgr.entries[i + 1];
    }
    
    g_netif_init_mgr.entry_count--;
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    LOG_DRV_INFO("Unregistered interface: %s", if_name);
    
    return AICAM_OK;
}

aicam_result_t netif_init_manager_init_async(const char *if_name)
{
    if (!g_netif_init_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    netif_init_entry_t *entry = find_entry(if_name);
    if (!entry) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_ERROR("Interface %s not registered", if_name);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Check current state
    if (entry->config.state == NETIF_INIT_STATE_INITIALIZING) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_WARN("Interface %s is already initializing", if_name);
        return AICAM_ERROR_BUSY;
    }
    
    if (entry->config.state == NETIF_INIT_STATE_READY) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_INFO("Interface %s is already ready", if_name);
        return AICAM_OK;
    }

    void *stack_mem = netif_init_stack_for_slot(entry->stack_slot);
    if (stack_mem == NULL) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_ERROR("Invalid stack slot for %s", if_name);
        return AICAM_ERROR_INVALID_PARAM;
    }

    osThreadAttr_t task_attr = {
        .name = if_name,
        .stack_mem = stack_mem,
        .stack_size = NETIF_INIT_STACK_SIZE,
        .priority = (entry->config.priority == NETIF_INIT_PRIORITY_HIGH) ?
                    osPriorityAboveNormal : osPriorityNormal
    };

    entry->task_id = NULL;
    entry->task_id = osThreadNew(netif_init_task, (void *)if_name, &task_attr);

    if (!entry->task_id) {
        osMutexRelease(g_netif_init_mgr.mutex);
        LOG_DRV_ERROR("Failed to create initialization task for %s", if_name);
        entry->task_id = NULL;
        return AICAM_ERROR;
    }
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    LOG_DRV_INFO("Started async initialization for: %s", if_name);
    
    return AICAM_OK;
}

aicam_result_t netif_init_manager_init_sync(const char *if_name, uint32_t timeout_ms)
{
    // Start async initialization
    aicam_result_t result = netif_init_manager_init_async(if_name);
    if (result != AICAM_OK && result != AICAM_ERROR_BUSY) {
        return result;
    }
    
    // Wait for completion
    return netif_init_manager_wait_ready(if_name, timeout_ms);
}

aicam_result_t netif_init_manager_init_all(aicam_bool_t async)
{
    if (!g_netif_init_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_DRV_INFO("Initializing all network interfaces (async: %s)", async ? "true" : "false");
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    // Sort by priority first
    sort_entries_by_priority();
    
    uint32_t count = g_netif_init_mgr.entry_count;
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    // Initialize each interface
    for (uint32_t i = 0; i < count; i++) {
        const char *if_name = g_netif_init_mgr.entries[i].config.if_name;
        
        if (async && g_netif_init_mgr.entries[i].config.async) {
            // Asynchronous initialization
            netif_init_manager_init_async(if_name);
        } else {
            // Synchronous initialization
            netif_init_manager_init_sync(if_name, 30000);
        }
    }
    
    LOG_DRV_INFO("All network interfaces initialization started");
    
    return AICAM_OK;
}

netif_init_state_t netif_init_manager_get_state(const char *if_name)
{
    if (!g_netif_init_mgr.initialized || !if_name) {
        return NETIF_INIT_STATE_IDLE;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    netif_init_entry_t *entry = find_entry(if_name);
    netif_init_state_t state = entry ? entry->config.state : NETIF_INIT_STATE_IDLE;
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    return state;
}

aicam_result_t netif_init_manager_wait_ready(const char *if_name, uint32_t timeout_ms)
{
    if (!g_netif_init_mgr.initialized || !if_name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    netif_init_entry_t *entry = find_entry(if_name);
    if (!entry) {
        osMutexRelease(g_netif_init_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Check if already ready
    if (entry->config.state == NETIF_INIT_STATE_READY) {
        osMutexRelease(g_netif_init_mgr.mutex);
        return AICAM_OK;
    }
    
    if (entry->config.state == NETIF_INIT_STATE_FAILED) {
        osMutexRelease(g_netif_init_mgr.mutex);
        return AICAM_ERROR;
    }
    
    osSemaphoreId_t semaphore = entry->ready_semaphore;
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    // Wait for ready semaphore
    osStatus_t status = osSemaphoreAcquire(semaphore, timeout_ms);
    
    if (status == osOK) {
        // Check final state
        netif_init_state_t final_state = netif_init_manager_get_state(if_name);
        return (final_state == NETIF_INIT_STATE_READY) ? AICAM_OK : AICAM_ERROR;
    } else if (status == osErrorTimeout) {
        LOG_DRV_WARN("Timeout waiting for %s to become ready", if_name);
        return AICAM_ERROR_TIMEOUT;
    }
    
    return AICAM_ERROR;
}

aicam_bool_t netif_init_manager_is_ready(const char *if_name)
{
    return (netif_init_manager_get_state(if_name) == NETIF_INIT_STATE_READY) ? 
           AICAM_TRUE : AICAM_FALSE;
}

uint32_t netif_init_manager_get_init_time(const char *if_name)
{
    if (!g_netif_init_mgr.initialized || !if_name) {
        return 0;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    netif_init_entry_t *entry = find_entry(if_name);
    uint32_t init_time = entry ? entry->config.init_time_ms : 0;
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    return init_time;
}

uint32_t netif_init_manager_get_count(void)
{
    if (!g_netif_init_mgr.initialized) {
        return 0;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    uint32_t count = g_netif_init_mgr.entry_count;
    osMutexRelease(g_netif_init_mgr.mutex);
    
    return count;
}

aicam_result_t netif_init_manager_get_list(const char **names, uint32_t max_count, uint32_t *actual_count)
{
    if (!g_netif_init_mgr.initialized || !names || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_netif_init_mgr.mutex, osWaitForever);
    
    uint32_t count = (g_netif_init_mgr.entry_count < max_count) ? 
                     g_netif_init_mgr.entry_count : max_count;
    
    for (uint32_t i = 0; i < count; i++) {
        names[i] = g_netif_init_mgr.entries[i].config.if_name;
    }
    
    *actual_count = g_netif_init_mgr.entry_count;
    
    osMutexRelease(g_netif_init_mgr.mutex);
    
    return AICAM_OK;
}

