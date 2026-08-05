/**
 * @file service_init.h
 * @brief Service Layer Initialization Management Header
 * @details service layer initialization management header
 */

#ifndef SERVICE_INIT_H
#define SERVICE_INIT_H

#include "aicam_types.h"
#include "service_interfaces.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Configuration Constants ==================== */

#define SERVICE_MAX_MODULES        16      // maximum number of service modules

/* ==================== Data Structures ==================== */

/**
 * @brief Service layer information structure
 */
typedef struct {
    uint32_t total_modules;              // total number of service modules
    uint32_t active_modules;             // active number of service modules
    uint32_t failed_modules;             // failed number of service modules
    uint32_t total_errors;               // total number of errors
} service_info_t;

/* ==================== Public API Function Declarations ==================== */

/**
 * @brief Initialize service layer
 * @details Initialize all registered service modules in priority order
 * @return aicam_result_t Operation result
 */
aicam_result_t service_init(void);

/**
 * @brief Start service layer
 * @details Start all initialized services that have auto_start enabled
 * @return aicam_result_t Operation result
 */
aicam_result_t service_start(void);

/**
 * @brief Stop service layer
 * @details Stop all running services
 * @return aicam_result_t Operation result
 */
aicam_result_t service_stop(void);

/**
 * @brief Deinitialize service layer
 * @details Deinitialize all services in reverse order
 * @return aicam_result_t Operation result
 */
aicam_result_t service_deinit(void);

/**
 * @brief Start a specific service module
 * @param name Service module name
 * @return aicam_result_t Operation result
 */
aicam_result_t service_start_module(const char *name);

/**
 * @brief Stop a specific service module
 * @param name Service module name
 * @return aicam_result_t Operation result
 */
aicam_result_t service_stop_module(const char *name);

/**
 * @brief Get service module state
 * @param name Service module name
 * @param state Service state (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t service_get_module_state(const char *name, service_state_t *state);

/**
 * @brief Get service layer information
 * @param info Service information structure (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t service_get_info(service_info_t *info);

/**
 * @brief Set service module configuration
 * @param name Service module name
 * @param config Configuration pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t service_set_module_config(const char *name, void *config);

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
                                      uint32_t init_priority);

/**
 * @brief Unregister a service module
 * @param name Service name
 * @return aicam_result_t Operation result
 */
aicam_result_t service_unregister_module(const char *name);

/**
 * @brief Get list of all registered service names
 * @param names Array to store service names
 * @param max_count Maximum number of names to store
 * @param actual_count Actual number of services (output parameter)
 * @return aicam_result_t Operation result
 */
aicam_result_t service_get_registered_modules(const char **names, uint32_t max_count, uint32_t *actual_count);

/* ==================== Service Ready Wait API ==================== */

// Service ready flag definitions (each service occupies one bit)
#define SERVICE_READY_AI               (1UL << 0)   // AI service ready
#define SERVICE_READY_SYSTEM           (1UL << 1)   // System service ready
#define SERVICE_READY_DEVICE           (1UL << 2)   // Device service ready
#define SERVICE_READY_COMMUNICATION    (1UL << 3)   // Communication service ready
#define SERVICE_READY_WEB              (1UL << 4)   // Web service ready
#define SERVICE_READY_MQTT             (1UL << 5)   // MQTT service ready
#define SERVICE_READY_OTA              (1UL << 6)   // OTA service ready
#define SERVICE_READY_AP               (1UL << 7)   // AP service ready
#define SERVICE_READY_STA              (1UL << 8)   // STA service ready
#define MQTT_NET_CONNECTED             (1UL << 9)   // MQTT network connected
#define SERVICE_READY_RTMP             (1UL << 10)  // RTMP service ready
#define SERVICE_READY_VIDEO_HUB        (1UL << 11)  // Video Hub ready
#define SERVICE_READY_RTSP             (1UL << 12)  // RTSP service ready
#define SERVICE_READY_WEBHOOK          (1UL << 13)  // Webhook service ready
#define SERVICE_READY_NETIF_ALL_FAILED (1UL << 14)  // All attempted netifs failed (wake-capture fast-fail signal)


// Combined flags
#define SERVICE_READY_ALL              (0x3FFUL)    // All services ready (10 services)
#define SERVICE_READY_NETWORK          (SERVICE_READY_COMMUNICATION | SERVICE_READY_WEB | SERVICE_READY_MQTT | SERVICE_READY_AP | SERVICE_READY_STA)
#define SERVICE_READY_LOW_POWER        (SERVICE_READY_SYSTEM | SERVICE_READY_DEVICE | SERVICE_READY_COMMUNICATION | SERVICE_READY_MQTT | SERVICE_READY_STA)
#define SERVICE_READY_WIFI             (SERVICE_READY_AP | SERVICE_READY_STA)  // WiFi service (AP + STA)

/**
 * @brief Wait for service(s) to be ready
 * @param flags Service ready flags to wait for (can combine multiple with OR)
 * @param wait_all If true, wait for all specified services; if false, wait for any one
 * @param timeout_ms Timeout in milliseconds (osWaitForever for infinite wait)
 * @return AICAM_OK if services are ready, AICAM_ERROR_TIMEOUT if timeout, AICAM_ERROR otherwise
 */
aicam_result_t service_wait_for_ready(uint32_t flags, aicam_bool_t wait_all, uint32_t timeout_ms);

/**
 * @brief Check if service(s) are ready (non-blocking)
 * @param flags Service ready flags to check
 * @param check_all If true, check all specified services; if false, check any one
 * @return AICAM_TRUE if ready, AICAM_FALSE otherwise
 */
aicam_bool_t service_is_ready(uint32_t flags, aicam_bool_t check_all);

/**
 * @brief Get current service ready flags
 * @return Current ready flags (bit mask)
 */
uint32_t service_get_ready_flags(void);

/**
 * @brief Whether service_start() skipped communication/mqtt/webhook this wake
 * @return AICAM_TRUE if network services were skipped (LOW_POWER + upload mode
 *         doesn't need network this wake). system_service checks this at sleep
 *         time to avoid waiting 30s for an MQTT that was never started.
 */
aicam_bool_t service_network_services_skipped(void);

/**
 * @brief Set AP service ready state (for manual control)
 * @param ready TRUE to set ready, FALSE to clear
 * @return AICAM_OK on success, AICAM_ERROR_NOT_INITIALIZED if flags not initialized
 */
aicam_result_t service_set_ap_ready(aicam_bool_t ready);

/**
 * @brief Set STA service ready state (for manual control)
 * @param ready TRUE to set ready, FALSE to clear
 * @return AICAM_OK on success, AICAM_ERROR_NOT_INITIALIZED if flags not initialized
 */
aicam_result_t service_set_sta_ready(aicam_bool_t ready);

/**
 * @brief Set MQTT network connected state (for manual control)
 * @param connected TRUE to set connected, FALSE to clear
 * @return AICAM_OK on success, AICAM_ERROR_NOT_INITIALIZED if flags not initialized
 */
aicam_result_t service_set_mqtt_net_connected(aicam_bool_t connected);

/**
 * @brief Signal that all attempted netifs failed to come up.
 *        Set by communication_service when the startup decision resolves to
 *        COMM_TYPE_NONE. Lets the upload-coordinator's network wait break out
 *        early (via SERVICE_READY_NETIF_ALL_FAILED) instead of timing out.
 *        Flag is per-boot (event flags object is recreated each boot).
 */
aicam_result_t service_set_netif_all_failed(aicam_bool_t failed);

/**
 * @brief Register debug commands for service layer
 * @return void
 */
void service_debug_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_INIT_H */
