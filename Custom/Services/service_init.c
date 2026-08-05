/**
 * @file service_init.c
 * @brief Service Layer Initialization Management
 * @details manage the initialization, start, stop and cleanup of all service modules
 */

#include "service_init.h"
#include "aicam_types.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include "system_service.h"
#include "web_service.h"
#include "ai_service.h"
#include "communication_service.h"
#include "mqtt_service.h"
#include "device_service.h"
#include "ota_service.h"
#include "rtmp_service.h"
#include "rtsp_service.h"
#include "webhook_service.h"
#include "upload_coordinator.h"
#include "Services/Video/video_stream_hub.h"
#include "cmsis_os2.h"

// Get power mode from system_service.h
extern power_mode_t system_service_get_current_power_mode(void);
#define SERVICE_MUTEX_CREATE()       osMutexNew(NULL)
#define SERVICE_MUTEX_DESTROY(mutex) osMutexDelete(mutex)
#define SERVICE_MUTEX_LOCK(mutex)     osMutexAcquire(mutex, osWaitForever)
#define SERVICE_MUTEX_UNLOCK(mutex)   osMutexRelease(mutex)
#define SERVICE_GET_TIMESTAMP()       osKernelGetTickCount()
typedef osMutexId_t service_mutex_t;

/* ==================== Service Ready Event Flags ==================== */

// Event flag group
static osEventFlagsId_t g_service_ready_flags = NULL;

/* Set by service_start(): true if communication/mqtt/webhook were skipped
 * this wake (LOW_POWER + upload mode doesn't need network). Read by
 * system_service at sleep time to decide whether to wait for MQTT. */
static aicam_bool_t g_network_services_skipped = AICAM_FALSE;

/* ==================== Service Module Registry ==================== */

/**
 * @brief Service module registration structure
 */
typedef struct {
    const char *name;                    // Service name
    service_state_t state;               // Current state
    uint32_t init_time;                  // Initialization time
    uint32_t start_time;                 // Start time
    uint32_t error_count;                // Error count
    aicam_result_t last_error;           // Last error code
    
    // Service interface functions
    aicam_result_t (*init_func)(void *config);
    aicam_result_t (*start_func)(void);
    aicam_result_t (*stop_func)(void);
    aicam_result_t (*deinit_func)(void);
    service_state_t (*get_state_func)(void);
    
    void *config;                        // Service configuration
    aicam_bool_t auto_start;             // Auto start flag
    uint32_t init_priority;              // Initialization priority (lower = higher priority)
    
    // Power mode control
    aicam_bool_t required_in_low_power;  // Required in low power mode
    /* Skip init() on a low-power essential-only wake (RTC/PIR/Button-short/...).
     * Use for services whose init is heavy AND unused on a capture-only wake.
     * Distinct from required_in_low_power (which gates *start*): a service may
     * need init in low power (e.g. device_service registers file ops used by
     * upload_coordinator) but not start. Only set for leaf services that init
     * AFTER system_service (which loads power_mode from NVS). */
    aicam_bool_t skip_init_in_low_power;
    
    // Service dependencies
    const char *depends_on[4];           // Services this depends on (max 4)
    uint8_t depends_count;               // Number of dependencies
} service_module_t;

/* ==================== Global Service Manager Context ==================== */

typedef struct {
    aicam_bool_t initialized;
    service_mutex_t mutex;
    service_module_t modules[SERVICE_MAX_MODULES];
    uint32_t module_count;
    uint32_t active_modules;
    uint32_t failed_modules;
    
    // Statistics
    uint32_t total_init_time;
    uint32_t total_start_time;
    uint32_t total_errors;
} service_manager_t;

static service_manager_t g_service_mgr = {0};
static service_module_t* find_service_module(const char *name);

/* ==================== Service Module Registry ==================== */

static const service_module_t g_service_registry[] = {
    {
        .name = "communication_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = communication_service_init,
        .start_func = communication_service_start,
        .stop_func = communication_service_stop,
        .deinit_func = communication_service_deinit,
        .get_state_func = communication_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 1,
        .required_in_low_power = AICAM_TRUE,   // Communication service (STA) is required
        .depends_on = {},
        .depends_count = 0
    },
    {
        .name = "ai_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = ai_service_init,
        .start_func = ai_service_start,
        .stop_func = ai_service_stop,
        .deinit_func = ai_service_deinit,
        .get_state_func = ai_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 2,
        .required_in_low_power = AICAM_FALSE,  
        .depends_on = {},
        .depends_count = 0
    },
    {
        .name = "device_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = device_service_init,
        .start_func = device_service_start,
        .stop_func = device_service_stop,
        .deinit_func = device_service_deinit,
        .get_state_func = device_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 3,
        .required_in_low_power = AICAM_FALSE,   // Device service is always required
        .depends_on = {},
        .depends_count = 0
    },
    {
        .name = "video_hub",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = (aicam_result_t (*)(void *))video_hub_init,
        .start_func = NULL,
        .stop_func = NULL,                       // no stop function
        .deinit_func = video_hub_deinit,
        .get_state_func = NULL,
        .config = NULL,
        .auto_start = AICAM_TRUE,                // auto start when initialized
        .init_priority = 4,
        .required_in_low_power = AICAM_FALSE,
        .depends_on = {},
        .depends_count = 0
    },
    {
        .name = "mqtt_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = mqtt_service_init,
        .start_func = mqtt_service_start,
        .stop_func = mqtt_service_stop,
        .deinit_func = mqtt_service_deinit,
        .get_state_func = mqtt_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 4,
        .required_in_low_power = AICAM_TRUE,   // MQTT needs to be in low power mode
        .depends_on = {"communication_service"},  // Depends on communication service (STA)
        .depends_count = 1
    },
    {
        .name = "system_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = system_service_init,
        .start_func = system_service_start,
        .stop_func = system_service_stop,
        .deinit_func = system_service_deinit,
        .get_state_func = NULL,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 5,
        .required_in_low_power = AICAM_TRUE,   // System service is always required
        .depends_on = {},
        .depends_count = 0
    },
    {
        .name = "web_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = web_service_init,
        .start_func = web_service_start,
        .stop_func = web_service_stop,
        .deinit_func = web_service_deinit,
        .get_state_func = web_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 6,
        .required_in_low_power = AICAM_FALSE,  // Web does not need to be in low power mode
        .skip_init_in_low_power = AICAM_TRUE, // init reads ~88KB web asset.bin from flash into RAM — wasted on a capture-only wake (start is gated too)
        .depends_on = {"communication_service"},  // Depends on communication service
        .depends_count = 1
    },
    {
        .name = "ota_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = ota_service_init,
        .start_func = ota_service_start,
        .stop_func = ota_service_stop,
        .deinit_func = ota_service_deinit,
        .get_state_func = ota_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 7,
        .required_in_low_power = AICAM_FALSE,  // OTA does not need to be in low power mode
        .depends_on = {"communication_service"},  // Depends on communication service
        .depends_count = 1
    },
    {
        .name = "rtmp_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = rtmp_service_init,
        .start_func = rtmp_service_start,
        .stop_func = rtmp_service_stop,
        .deinit_func = rtmp_service_deinit,
        .get_state_func = rtmp_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,             // Auto init, manual stream start
        .init_priority = 8,
        .required_in_low_power = AICAM_FALSE,  // RTMP streaming not needed in low power mode
        .depends_on = {"communication_service"},
        .depends_count = 1
    },
    {
        .name = "rtsp_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = rtsp_service_init,
        .start_func = rtsp_service_start,
        .stop_func = rtsp_service_stop,
        .deinit_func = rtsp_service_deinit,
        .get_state_func = rtsp_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 9,
        .required_in_low_power = AICAM_FALSE,
        .depends_on = {"communication_service"},
        .depends_count = 1
    },
    {
        .name = "webhook_service",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = webhook_service_init,
        .start_func = webhook_service_start,
        .stop_func = webhook_service_stop,
        .deinit_func = webhook_service_deinit,
        .get_state_func = webhook_service_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 10,
        .required_in_low_power = AICAM_TRUE,
        .depends_on = {"communication_service"},
        .depends_count = 1
    },
    {
        .name = "upload_coordinator",
        .state = SERVICE_STATE_UNINITIALIZED,
        .init_func = upload_coordinator_init,
        .start_func = upload_coordinator_start,
        .stop_func = upload_coordinator_stop,
        .deinit_func = upload_coordinator_deinit,
        .get_state_func = upload_coordinator_get_state,
        .config = NULL,
        .auto_start = AICAM_TRUE,
        .init_priority = 11,
        .required_in_low_power = AICAM_TRUE,
        /* No hard service dependency: init only needs NVS (available pre-service)
         * and file ops (registered during device_service init, not start).
         * Runtime channel/sd probes gracefully tolerate "not ready" → false. */
        .depends_on = {},
        .depends_count = 0
    }
};

#define SERVICE_REGISTRY_COUNT (sizeof(g_service_registry) / sizeof(service_module_t))

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Map service name to ready flag bit
 */
static uint32_t get_service_ready_flag(const char *name)
{
    if (!name) return 0;
    
    if (strcmp(name, "ai_service") == 0) {
        return SERVICE_READY_AI;
    } else if (strcmp(name, "system_service") == 0) {
        return SERVICE_READY_SYSTEM;
    } else if (strcmp(name, "device_service") == 0) {
        return SERVICE_READY_DEVICE;
    } else if (strcmp(name, "communication_service") == 0) {
        return SERVICE_READY_COMMUNICATION;
    } else if (strcmp(name, "web_service") == 0) {
        return SERVICE_READY_WEB;
    } else if (strcmp(name, "mqtt_service") == 0) {
        return SERVICE_READY_MQTT;
    } else if (strcmp(name, "ota_service") == 0) {
        return SERVICE_READY_OTA;
    } else if (strcmp(name, "ap_service") == 0) {
        return SERVICE_READY_AP;
    } else if (strcmp(name, "sta_service") == 0) {
        return SERVICE_READY_STA;
    } else if (strcmp(name, "rtmp_service") == 0) {
        return SERVICE_READY_RTMP;
    } else if (strcmp(name, "video_hub") == 0) {
        return SERVICE_READY_VIDEO_HUB;
    } else if (strcmp(name, "rtsp_service") == 0) {
        return SERVICE_READY_RTSP;
    } else if (strcmp(name, "webhook_service") == 0) {
        return SERVICE_READY_WEBHOOK;
    }

    return 0;
}

/**
 * @brief Check if service dependencies are satisfied
 */
static aicam_bool_t check_service_dependencies(const service_module_t *module)
{
    if (!module || module->depends_count == 0) {
        return AICAM_TRUE;  // No dependencies
    }
    
    for (uint8_t i = 0; i < module->depends_count; i++) {
        const char *dep_name = module->depends_on[i];
        service_module_t *dep_module = find_service_module(dep_name);
        
        if (!dep_module) {
            LOG_SVC_WARN("Dependency '%s' not found for '%s'", dep_name, module->name);
            return AICAM_FALSE;
        }
        
        if (dep_module->state != SERVICE_STATE_RUNNING) {
            LOG_SVC_DEBUG("Dependency '%s' not running (state: %d) for '%s'", 
                         dep_name, dep_module->state, module->name);
            return AICAM_FALSE;
        }
    }
    
    return AICAM_TRUE;
}

/**
 * @brief Sort service modules by initialization priority
 */
static void sort_services_by_priority(void)
{
    // Simple bubble sort for small number of services
    for (uint32_t i = 0; i < g_service_mgr.module_count - 1; i++) {
        for (uint32_t j = 0; j < g_service_mgr.module_count - i - 1; j++) {
            if (g_service_mgr.modules[j].init_priority > g_service_mgr.modules[j + 1].init_priority) {
                service_module_t temp = g_service_mgr.modules[j];
                g_service_mgr.modules[j] = g_service_mgr.modules[j + 1];
                g_service_mgr.modules[j + 1] = temp;
            }
        }
    }
}

/**
 * @brief Find service module by name
 */
static service_module_t* find_service_module(const char *name)
{
    if (!name) return NULL;
    
    for (uint32_t i = 0; i < g_service_mgr.module_count; i++) {
        if (strcmp(g_service_mgr.modules[i].name, name) == 0) {
            return &g_service_mgr.modules[i];
        }
    }
    return NULL;
}

/**
 * @brief Initialize a single service module
 */
static aicam_result_t init_service_module(service_module_t *module)
{
    if (!module || !module->init_func) {
        LOG_SVC_ERROR("Service '%s' initialization function is NULL", module->name);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (module->state != SERVICE_STATE_UNINITIALIZED) {
        LOG_SVC_WARN("Service '%s' already initialized (state: %d)", 
                      module->name, module->state);
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    module->state = SERVICE_STATE_INITIALIZING;
    module->init_time = SERVICE_GET_TIMESTAMP();

    LOG_SVC_INFO("Initializing service: %s", module->name);

    aicam_result_t result = module->init_func(module->config);
    
    if (result == AICAM_OK) {
        module->state = SERVICE_STATE_INITIALIZED;
        g_service_mgr.active_modules++;
        LOG_SVC_INFO("Service '%s' initialized successfully", module->name);
    } else {
        module->state = SERVICE_STATE_ERROR;
        module->last_error = result;
        module->error_count++;
        g_service_mgr.failed_modules++;
        g_service_mgr.total_errors++;
        LOG_SVC_ERROR("Service '%s' initialization failed: %d", module->name, result);
    }
    
    return result;
}

/**
 * @brief Start a single service module
 */
static aicam_result_t start_service_module(service_module_t *module)
{
    if (!module || !module->start_func) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (module->state != SERVICE_STATE_INITIALIZED) {
        LOG_SVC_WARN("Service '%s' not ready to start (state: %d)", 
                      module->name, module->state);
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    // Check dependencies
    if (!check_service_dependencies(module)) {
        LOG_SVC_WARN("Service '%s' dependencies not satisfied", module->name);
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    module->start_time = SERVICE_GET_TIMESTAMP();
    
    LOG_SVC_INFO("Starting service: %s", module->name);
    
    aicam_result_t result = module->start_func();
    
    if (result == AICAM_OK) {
        module->state = SERVICE_STATE_RUNNING;
        LOG_SVC_INFO("Service '%s' started successfully", module->name);
        
        // Set service ready flag
        if (g_service_ready_flags) {
            uint32_t flag = get_service_ready_flag(module->name);
            if (flag != 0) {
                osEventFlagsSet(g_service_ready_flags, flag);
                LOG_SVC_DEBUG("Service '%s' ready flag set (0x%08X)", module->name, flag);
            }
        }
    } else {
        module->state = SERVICE_STATE_ERROR;
        module->last_error = result;
        module->error_count++;
        g_service_mgr.total_errors++;
        LOG_SVC_ERROR("Service '%s' start failed: %d", module->name, result);
    }
    
    return result;
}

/**
 * @brief Stop a single service module
 */
static aicam_result_t stop_service_module(service_module_t *module)
{
    if (!module || !module->stop_func) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (module->state != SERVICE_STATE_RUNNING) {
        LOG_SVC_WARN("Service '%s' not running (state: %d)", 
                      module->name, module->state);
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping service: %s", module->name);
    
    aicam_result_t result = module->stop_func();
    
    if (result == AICAM_OK) {
        module->state = SERVICE_STATE_INITIALIZED;
        LOG_SVC_INFO("Service '%s' stopped successfully", module->name);
        
        // Clear service ready flag
        if (g_service_ready_flags) {
            uint32_t flag = get_service_ready_flag(module->name);
            if (flag != 0) {
                osEventFlagsClear(g_service_ready_flags, flag);
                LOG_SVC_DEBUG("Service '%s' ready flag cleared (0x%08X)", module->name, flag);
            }
        }
    } else {
        module->last_error = result;
        module->error_count++;
        g_service_mgr.total_errors++;
        LOG_SVC_ERROR("Service '%s' stop failed: %d", module->name, result);
    }
    
    return result;
}

/**
 * @brief Deinitialize a single service module
 */
static aicam_result_t deinit_service_module(service_module_t *module)
{
    if (!module || !module->deinit_func) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (module->state == SERVICE_STATE_UNINITIALIZED) {
        return AICAM_OK;  // Already deinitialized
    }
    
    // Stop if running
    if (module->state == SERVICE_STATE_RUNNING) {
        stop_service_module(module);
    }
    
    LOG_SVC_INFO("Deinitializing service: %s", module->name);
    
    aicam_result_t result = module->deinit_func();
    
    if (result == AICAM_OK) {
        if (module->state == SERVICE_STATE_INITIALIZED) {
            g_service_mgr.active_modules--;
        }
        module->state = SERVICE_STATE_UNINITIALIZED;
        LOG_SVC_INFO("Service '%s' deinitialized successfully", module->name);
    } else {
        module->last_error = result;
        module->error_count++;
        g_service_mgr.total_errors++;
        LOG_SVC_ERROR("Service '%s' deinitialization failed: %d", module->name, result);
    }
    
    return result;
}

/* ==================== Public API Implementation ==================== */

aicam_result_t service_init(void)
{
    if (g_service_mgr.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing Service Layer...");
    
    // Initialize service manager
    memset(&g_service_mgr, 0, sizeof(service_manager_t));
    
    // Create mutex
    g_service_mgr.mutex = SERVICE_MUTEX_CREATE();
    if (!g_service_mgr.mutex) {
        LOG_SVC_ERROR("Failed to create service manager mutex");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Create service ready event flags
    g_service_ready_flags = osEventFlagsNew(NULL);
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Failed to create service ready event flags");
        SERVICE_MUTEX_DESTROY(g_service_mgr.mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    // Copy service registry to manager
    if (SERVICE_REGISTRY_COUNT > SERVICE_MAX_MODULES) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        LOG_SVC_ERROR("Too many services in registry: %d > %d", 
                      (int)SERVICE_REGISTRY_COUNT, SERVICE_MAX_MODULES);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    memcpy(g_service_mgr.modules, g_service_registry, 
           sizeof(service_module_t) * SERVICE_REGISTRY_COUNT);
    g_service_mgr.module_count = SERVICE_REGISTRY_COUNT;
    
    // Sort by initialization priority
    sort_services_by_priority();
    
    // Initialize all services in priority order
    for (uint32_t i = 0; i < g_service_mgr.module_count; i++) {
        service_module_t *module = &g_service_mgr.modules[i];

        /* Low-power essential-only wake (RTC/PIR/Button-short/...): skip the
         * init of services flagged skip_init_in_low_power. web_service is the
         * main one — its init reads the ~88KB web asset.bin from flash into
         * RAM, pure waste on a capture-only wake (start is already gated, so
         * those assets would never be served). The && short-circuits, so the
         * power-mode/wakeup getters are only evaluated for flagged modules —
         * which (by contract) init after system_service, so the getters return
         * real values by then. */
        if (module->skip_init_in_low_power
            && system_service_get_current_power_mode() == POWER_MODE_LOW_POWER
            && system_service_requires_only_essential_services(system_service_get_wakeup_source_type())) {
            LOG_SVC_INFO("Skipping init '%s' in low power essential-only wake", module->name);
            continue;
        }

        aicam_result_t result = init_service_module(module);
        if (result != AICAM_OK) {
            LOG_SVC_WARN("Service '%s' initialization failed, continuing with others",
                          module->name);
        }
    }
    
    g_service_mgr.initialized = AICAM_TRUE;
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    LOG_SVC_INFO("Service Layer initialized: %d/%d services active, %d failed",
                  g_service_mgr.active_modules, g_service_mgr.module_count, 
                  g_service_mgr.failed_modules);

    LOG_SVC_INFO("Starting Service Layer...");
    
    service_start();
    
    return AICAM_OK;
}

aicam_result_t service_start(void)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    // Get current power mode from system_service
    power_mode_t current_power_mode = system_service_get_current_power_mode();
    LOG_SVC_INFO("Starting Service Layer (power mode: %s)...",
                current_power_mode == POWER_MODE_LOW_POWER ? "LOW_POWER" : "FULL_SPEED");

    // Get current wakeup source from system_service
    wakeup_source_type_t current_wakeup_source = system_service_get_wakeup_source_type();
    LOG_SVC_INFO("Starting Service Layer (wakeup source: %s)...",
                current_wakeup_source == WAKEUP_SOURCE_RTC ? "RTC" : current_wakeup_source == WAKEUP_SOURCE_BUTTON ? "BUTTON" : current_wakeup_source == WAKEUP_SOURCE_PIR ? "PIR" : "OTHER");

    /* Ask upload_coordinator whether this wake cycle actually needs the
     * network stack. Only honored in LOW_POWER mode AND for wake sources
     * that only need essential services (RTC/PIR/Button short/IO). For
     * OTHER / BUTTON_LONG (AP enable) the full service stack - including
     * web - must come up, so we never skip communication there. */
    aicam_bool_t need_network = upload_coordinator_needs_network();
    aicam_bool_t essential_only = system_service_requires_only_essential_services(current_wakeup_source);
    aicam_bool_t skip_network_services =
        (current_power_mode == POWER_MODE_LOW_POWER) && essential_only && !need_network;
    g_network_services_skipped = skip_network_services;   /* read by system_service at sleep time */
    LOG_SVC_INFO("Starting Service Layer (network needed: %s, essential_only: %d, skip: %s)...",
                need_network ? "YES" : "NO",
                (int)essential_only,
                skip_network_services ? "YES" : "NO");
    if (skip_network_services) {
        printf("[BOOT] Skipping network services (capture mode doesn't need upload this wake)\r\n");
    }

    /* Wake-capture netif restriction: on the low-power wake-capture path (not
     * skipping network, essential-only), if the capture-upload config names a
     * specific upload netif, bring up only that one. For "default" upload
     * network (req == NONE) we keep the full path: ALL auto_start_* netifs init
     * - only after init do we know which are actually present/plugged in, and
     * the connection decision picks among them. Full-speed boots and skip-
     * network wakes also leave it at default (all). */
    if (!skip_network_services && essential_only &&
        current_power_mode == POWER_MODE_LOW_POWER) {
        communication_type_t req = upload_coordinator_get_required_comm_type();
        if (req != COMM_TYPE_NONE) {
            communication_service_set_required_type(req);
            // printf("[BOOT] Wake-capture using single netif: %s\r\n",
            //        communication_type_to_string(req));
        }
    }

    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);

    // Start services based on power mode and auto_start flag
    for (uint32_t i = 0; i < g_service_mgr.module_count; i++) {
        service_module_t *module = &g_service_mgr.modules[i];

        if (module->state != SERVICE_STATE_INITIALIZED || !module->auto_start || !module->start_func) {
            continue;
        }

        // Low power mode: only start essential services unless wakeup source requires all services
        if (current_power_mode == POWER_MODE_LOW_POWER && system_service_requires_only_essential_services(current_wakeup_source)) {
            if (!module->required_in_low_power) {
                LOG_SVC_INFO("Skipping '%s' in low power mode", module->name);
                continue;
            }
        }

        /* Network bring-up skip (LOW_POWER only): when upload_coordinator
         * says this wake doesn't need to upload, skip communication/mqtt/
         * webhook (the multi-second network bring-up). upload_coordinator
         * itself stays up so it can enqueue the capture. In FULL_SPEED this
         * never triggers - web/OTA/stream need the network. */
        if (skip_network_services &&
            (strcmp(module->name, "communication_service") == 0 ||
             strcmp(module->name, "mqtt_service") == 0 ||
             strcmp(module->name, "webhook_service") == 0)) {
            LOG_SVC_INFO("Skipping '%s' (network not needed this wake)", module->name);
            /* Leave state as INITIALIZED so next wake can start it normally. */
            continue;
        }

        aicam_result_t result = start_service_module(module);
        if (result != AICAM_OK) {
            LOG_SVC_WARN("Service '%s' start failed, continuing with others", 
                          module->name);
        }
    }
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    LOG_SVC_INFO("Service Layer started");
    
    return AICAM_OK;
}

aicam_result_t service_stop(void)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_SVC_INFO("Stopping Service Layer...");
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    // Stop all running services (in reverse order)
    for (int32_t i = g_service_mgr.module_count - 1; i >= 0; i--) {
        service_module_t *module = &g_service_mgr.modules[i];
        
        if (module->state == SERVICE_STATE_RUNNING) {
            aicam_result_t result = stop_service_module(module);
            if (result != AICAM_OK) {
                LOG_SVC_WARN("Service '%s' stop failed, continuing with others", 
                              module->name);
            }
        }
    }
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    LOG_SVC_INFO("Service Layer stopped");
    
    return AICAM_OK;
}

aicam_result_t service_deinit(void)
{
    if (!g_service_mgr.initialized) {
        return AICAM_OK;
    }
    
    LOG_SVC_INFO("Deinitializing Service Layer...");
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    // Deinitialize all services (in reverse order)
    for (int32_t i = g_service_mgr.module_count - 1; i >= 0; i--) {
        service_module_t *module = &g_service_mgr.modules[i];
        
        aicam_result_t result = deinit_service_module(module);
        if (result != AICAM_OK) {
            LOG_SVC_WARN("Service '%s' deinitialization failed, continuing with others", 
                          module->name);
        }
    }
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    // Destroy mutex
    SERVICE_MUTEX_DESTROY(g_service_mgr.mutex);
    
    // Reset manager
    memset(&g_service_mgr, 0, sizeof(service_manager_t));
    
    LOG_SVC_INFO("Service Layer deinitialized");
    
    return AICAM_OK;
}

aicam_result_t service_start_module(const char *name)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    service_module_t *module = find_service_module(name);
    if (!module) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    aicam_result_t result = start_service_module(module);
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    return result;
}

aicam_result_t service_stop_module(const char *name)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    service_module_t *module = find_service_module(name);
    if (!module) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    aicam_result_t result = stop_service_module(module);
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    return result;
}

aicam_result_t service_get_module_state(const char *name, service_state_t *state)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!name || !state) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    service_module_t *module = find_service_module(name);
    if (!module) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    *state = module->state;
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    return AICAM_OK;
}

aicam_result_t service_get_info(service_info_t *info)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    info->total_modules = g_service_mgr.module_count;
    info->active_modules = g_service_mgr.active_modules;
    info->failed_modules = g_service_mgr.failed_modules;
    info->total_errors = g_service_mgr.total_errors;
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    return AICAM_OK;
}

aicam_result_t service_set_module_config(const char *name, void *config)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    service_module_t *module = find_service_module(name);
    if (!module) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    module->config = config;
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    return AICAM_OK;
}

/* ==================== Dynamic Service Registration API ==================== */

/**
 * @brief Register a new service module dynamically
 * @param name Service name
 * @param init_func Initialization function
 * @param start_func Start function
 * @param stop_func Stop function
 * @param deinit_func Deinitialization function
 * @param get_state_func Get state function
 * @param config Service configuration
 * @param auto_start Auto start flag
 * @param init_priority Initialization priority
 * @return aicam_result_t Operation result
 */
aicam_result_t service_register_module(const char *name,
                                      aicam_result_t (*init_func)(void *config),
                                      aicam_result_t (*start_func)(void),
                                      aicam_result_t (*stop_func)(void),
                                      aicam_result_t (*deinit_func)(void),
                                      service_state_t (*get_state_func)(void),
                                      void *config,
                                      aicam_bool_t auto_start,
                                      uint32_t init_priority)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!name || !init_func || !start_func || !stop_func || !deinit_func || !get_state_func) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    // Check if service already exists
    if (find_service_module(name) != NULL) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    // Check if we have space for more services
    if (g_service_mgr.module_count >= SERVICE_MAX_MODULES) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Add new service module
    service_module_t *module = &g_service_mgr.modules[g_service_mgr.module_count];
    module->name = name;
    module->state = SERVICE_STATE_UNINITIALIZED;
    module->init_func = init_func;
    module->start_func = start_func;
    module->stop_func = stop_func;
    module->deinit_func = deinit_func;
    module->get_state_func = get_state_func;
    module->config = config;
    module->auto_start = auto_start;
    module->init_priority = init_priority;
    module->init_time = 0;
    module->start_time = 0;
    module->error_count = 0;
    module->last_error = AICAM_OK;
    
    g_service_mgr.module_count++;
    
    // Re-sort by priority
    sort_services_by_priority();
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    LOG_SVC_INFO("Service '%s' registered successfully", name);
    
    return AICAM_OK;
}

/**
 * @brief Unregister a service module
 * @param name Service name
 * @return aicam_result_t Operation result
 */
aicam_result_t service_unregister_module(const char *name)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!name) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    // Find the service module
    service_module_t *module = find_service_module(name);
    if (!module) {
        SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Deinitialize if needed
    if (module->state != SERVICE_STATE_UNINITIALIZED) {
        deinit_service_module(module);
    }
    
    // Remove from array by shifting remaining modules
    uint32_t module_index = module - g_service_mgr.modules;
    for (uint32_t i = module_index; i < g_service_mgr.module_count - 1; i++) {
        g_service_mgr.modules[i] = g_service_mgr.modules[i + 1];
    }
    
    g_service_mgr.module_count--;
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    LOG_SVC_INFO("Service '%s' unregistered successfully", name);
    
    return AICAM_OK;
}

/**
 * @brief Get list of all registered service names
 * @param names Array to store service names
 * @param max_count Maximum number of names to store
 * @param actual_count Actual number of services (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t service_get_registered_modules(const char **names, uint32_t max_count, uint32_t *actual_count)
{
    if (!g_service_mgr.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!names || !actual_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    SERVICE_MUTEX_LOCK(g_service_mgr.mutex);
    
    uint32_t count = (g_service_mgr.module_count < max_count) ? g_service_mgr.module_count : max_count;
    
    for (uint32_t i = 0; i < count; i++) {
        names[i] = g_service_mgr.modules[i].name;
    }
    
    *actual_count = g_service_mgr.module_count;
    
    SERVICE_MUTEX_UNLOCK(g_service_mgr.mutex);
    
    return AICAM_OK;
}

/* ==================== Service Ready Wait API ==================== */

/**
 * @brief Wait for service(s) to be ready
 * @param flags Service ready flags to wait for (can combine multiple with OR)
 * @param wait_all If true, wait for all specified services; if false, wait for any one
 * @param timeout_ms Timeout in milliseconds (osWaitForever for infinite wait)
 * @return AICAM_OK if services are ready, AICAM_ERROR_TIMEOUT if timeout, AICAM_ERROR otherwise
 */
aicam_result_t service_wait_for_ready(uint32_t flags, aicam_bool_t wait_all, uint32_t timeout_ms)
{
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Service ready flags not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (flags == 0) {
        LOG_SVC_ERROR("Invalid flags: 0");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_DEBUG("Waiting for service(s) ready: flags=0x%08X, wait_all=%d, timeout=%u ms", 
                 flags, wait_all, timeout_ms);
    
    //check flag every bit is ready. Log each not-ready bit ONCE per not-ready
    //stretch (static reported mask, cleared when the bit goes ready) —
    //service_wait_for_ready is called repeatedly (upload channel wait, direct
    //publish, netif ready, ...) and re-logging the same not-ready bits every
    //call spams the console.
    // static uint32_t s_reported_not_ready = 0;
    // for (uint32_t i = 0; i < 32; i++) {
    //     uint32_t bit = (1 << i);
    //     if (!(flags & bit)) continue;
    //     if (osEventFlagsGet(g_service_ready_flags) & bit) {
    //         s_reported_not_ready &= ~bit;   /* ready now — allow re-report if it drops later */
    //     } else if (!(s_reported_not_ready & bit)) {
    //         LOG_SVC_WARN("Service %d is not ready", i);
    //         s_reported_not_ready |= bit;
    //     }
    // }
    
    // Use osFlagsNoClear to keep flags set after wait (service ready state is persistent)
    uint32_t option = (wait_all ? osFlagsWaitAll : osFlagsWaitAny) | osFlagsNoClear;
    uint32_t result = osEventFlagsWait(g_service_ready_flags, flags, option, timeout_ms);
    
    if (result & osFlagsError) {
        if (result == (uint32_t)osFlagsErrorTimeout) {
            LOG_SVC_WARN("Timeout waiting for service(s) ready: flags=0x%08X", flags);
            return AICAM_ERROR_TIMEOUT;
        } else {
            LOG_SVC_ERROR("Error waiting for service(s) ready: flags=0x%08X, error=0x%08X", flags, result);
            return AICAM_ERROR;
        }
    }
    
    LOG_SVC_DEBUG("Service(s) ready: result=0x%08X (flags remain set)", result);
    return AICAM_OK;
}

/**
 * @brief Check if service(s) are ready (non-blocking)
 * @param flags Service ready flags to check
 * @param check_all If true, check all specified services; if false, check any one
 * @return AICAM_TRUE if ready, AICAM_FALSE otherwise
 */
aicam_bool_t service_is_ready(uint32_t flags, aicam_bool_t check_all)
{
    if (!g_service_ready_flags) {
        return AICAM_FALSE;
    }
    
    if (flags == 0) {
        return AICAM_FALSE;
    }
    
    uint32_t current_flags = osEventFlagsGet(g_service_ready_flags);
    
    if (check_all) {
        // Check if all specified services are ready
        return ((current_flags & flags) == flags) ? AICAM_TRUE : AICAM_FALSE;
    } else {
        // Check if any of the specified services is ready
        return ((current_flags & flags) != 0) ? AICAM_TRUE : AICAM_FALSE;
    }
}

/**
 * @brief Get current service ready flags
 * @return Current ready flags (bit mask)
 */
uint32_t service_get_ready_flags(void)
{
    if (!g_service_ready_flags) {
        return 0;
    }

    return osEventFlagsGet(g_service_ready_flags);
}

aicam_bool_t service_network_services_skipped(void)
{
    return g_network_services_skipped;
}

/* ==================== Service Ready Manual Control API ==================== */

/**
 * @brief Set AP service ready state
 * @param ready TRUE to set ready, FALSE to clear
 * @return AICAM_OK on success, AICAM_ERROR_NOT_INITIALIZED if flags not initialized
 */
aicam_result_t service_set_ap_ready(aicam_bool_t ready)
{
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Service ready flags not initialized, cannot set AP ready");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (ready) {
        uint32_t result = osEventFlagsSet(g_service_ready_flags, SERVICE_READY_AP);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to set AP ready flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        uint32_t current_flags = osEventFlagsGet(g_service_ready_flags);
        LOG_SVC_INFO("AP service marked as ready (flags: 0x%08X)", current_flags);
    } else {
        uint32_t result = osEventFlagsClear(g_service_ready_flags, SERVICE_READY_AP);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to clear AP ready flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        LOG_SVC_INFO("AP service marked as not ready");
    }
    
    return AICAM_OK;
}

/**
 * @brief Set STA service ready state
 * @param ready TRUE to set ready, FALSE to clear
 * @return AICAM_OK on success, AICAM_ERROR_NOT_INITIALIZED if flags not initialized
 */
aicam_result_t service_set_sta_ready(aicam_bool_t ready)
{
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Service ready flags not initialized, cannot set STA ready");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (ready) {
        uint32_t result = osEventFlagsSet(g_service_ready_flags, SERVICE_READY_STA);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to set STA ready flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        uint32_t current_flags = osEventFlagsGet(g_service_ready_flags);
        LOG_SVC_INFO("STA service marked as ready (flags: 0x%08X)", current_flags);
    } else {
        uint32_t result = osEventFlagsClear(g_service_ready_flags, SERVICE_READY_STA);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to clear STA ready flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        LOG_SVC_INFO("STA service marked as not ready");
    }
    
    return AICAM_OK;
}

/**
 * @brief Set MQTT network connected state
 * @param connected TRUE to set connected, FALSE to clear
 * @return AICAM_OK on success, AICAM_ERROR_NOT_INITIALIZED if flags not initialized
 */
aicam_result_t service_set_mqtt_net_connected(aicam_bool_t connected)
{
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Service ready flags not initialized, cannot set MQTT network connected");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (connected) {
        uint32_t result = osEventFlagsSet(g_service_ready_flags, MQTT_NET_CONNECTED);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to set MQTT network connected flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        uint32_t current_flags = osEventFlagsGet(g_service_ready_flags);
        LOG_SVC_INFO("MQTT network marked as connected (flags: 0x%08X)", current_flags);
    } else {
        uint32_t result = osEventFlagsClear(g_service_ready_flags, MQTT_NET_CONNECTED);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to clear MQTT network connected flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        LOG_SVC_INFO("MQTT network marked as disconnected");
    }
    
    return AICAM_OK;
}

/**
 * @brief Set the "all netifs failed" signal. See header for details.
 */
aicam_result_t service_set_netif_all_failed(aicam_bool_t failed)
{
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Service ready flags not initialized, cannot set NETIF_ALL_FAILED");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    if (failed) {
        uint32_t result = osEventFlagsSet(g_service_ready_flags, SERVICE_READY_NETIF_ALL_FAILED);
        if (result & osFlagsError) {
            LOG_SVC_ERROR("Failed to set NETIF_ALL_FAILED flag: 0x%08X", result);
            return AICAM_ERROR;
        }
        LOG_SVC_WARN("All netifs failed signal raised - upload waiters should abort");
    }
    return AICAM_OK;
}

/* ==================== Debug Helper Functions ==================== */

/**
 * @brief Debug helper: Print current service ready flags status
 */
int service_debug_print_ready_flags(int argc, char **argv)
{
    if (!g_service_ready_flags) {
        LOG_SVC_ERROR("Service ready flags not initialized!");
        return -1;
    }
    
    uint32_t flags = osEventFlagsGet(g_service_ready_flags);
    
    LOG_SVC_INFO("=== Service Ready Flags Status ===");
    LOG_SVC_INFO("All flags: 0x%08X", flags);
    LOG_SVC_INFO("  AI:            %s (0x%02X)", (flags & SERVICE_READY_AI) ? "✅" : "❌", SERVICE_READY_AI);
    LOG_SVC_INFO("  System:        %s (0x%02X)", (flags & SERVICE_READY_SYSTEM) ? "✅" : "❌", SERVICE_READY_SYSTEM);
    LOG_SVC_INFO("  Device:        %s (0x%02X)", (flags & SERVICE_READY_DEVICE) ? "✅" : "❌", SERVICE_READY_DEVICE);
    LOG_SVC_INFO("  Communication: %s (0x%02X)", (flags & SERVICE_READY_COMMUNICATION) ? "✅" : "❌", SERVICE_READY_COMMUNICATION);
    LOG_SVC_INFO("  Web:           %s (0x%02X)", (flags & SERVICE_READY_WEB) ? "✅" : "❌", SERVICE_READY_WEB);
    LOG_SVC_INFO("  MQTT:          %s (0x%02X)", (flags & SERVICE_READY_MQTT) ? "✅" : "❌", SERVICE_READY_MQTT);
    LOG_SVC_INFO("  OTA:           %s (0x%02X)", (flags & SERVICE_READY_OTA) ? "✅" : "❌", SERVICE_READY_OTA);
    LOG_SVC_INFO("  AP:            %s (0x%02X)", (flags & SERVICE_READY_AP) ? "✅" : "❌", SERVICE_READY_AP);
    LOG_SVC_INFO("  STA:           %s (0x%02X)", (flags & SERVICE_READY_STA) ? "✅" : "❌", SERVICE_READY_STA);
    LOG_SVC_INFO("  MQTT Net:      %s (0x%02X)", (flags & MQTT_NET_CONNECTED) ? "✅" : "❌", MQTT_NET_CONNECTED);
    LOG_SVC_INFO("===================================");

    return 0;
}

debug_cmd_reg_t service_debug_cmd_table[] = {
    { "flag", "Print current service ready flags status", service_debug_print_ready_flags },
};

void service_debug_register_commands(void)
{
    for (int i = 0; i < sizeof(service_debug_cmd_table) / sizeof(service_debug_cmd_table[0]); i++) {
        debug_register_commands(&service_debug_cmd_table[i], 1);
    }
}