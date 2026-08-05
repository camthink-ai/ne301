/**
 * @file system_service.h
 * @brief Simplified System Management Service
 * @details Provides core system management: power modes, work modes, and capture triggers
 */

#ifndef SYSTEM_SERVICE_H
#define SYSTEM_SERVICE_H

#include "aicam_types.h"
#include "json_config_mgr.h"
#include "drtc.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== System Service Types ==================== */

/**
 * @brief System states
 */
typedef enum {
    SYSTEM_STATE_INIT = 0,               // Initializing
    SYSTEM_STATE_ACTIVE,                 // Active operation
    SYSTEM_STATE_SLEEP,                  // Sleep mode
    SYSTEM_STATE_SHUTDOWN,               // Shutdown mode
    SYSTEM_STATE_ERROR,                  // Error state
    SYSTEM_STATE_MAX
} system_state_t;

/**
 * @brief Power modes
 */
typedef enum {
    POWER_MODE_LOW_POWER = 0,            // Low power mode (default)
    POWER_MODE_FULL_SPEED,               // Full speed mode
    POWER_MODE_MAX
} power_mode_t;

/**
 * @brief Power mode trigger types
 */
typedef enum {
    POWER_TRIGGER_MANUAL = 0,            // WebUI manual switch
    POWER_TRIGGER_AUTO_WAKEUP,           // Auto switch when device is woken up
    POWER_TRIGGER_TIMEOUT,               // Timeout auto switch
    POWER_TRIGGER_MAX
} power_trigger_type_t;

/**
 * @brief Wakeup source types
 */
typedef enum {
    WAKEUP_SOURCE_IO = 0,                // IO trigger wakeup
    WAKEUP_SOURCE_RTC,                   // RTC timer wakeup
    WAKEUP_SOURCE_PIR,                   // PIR sensor wakeup
    WAKEUP_SOURCE_BUTTON,                // Button short press wakeup (take photo)
    WAKEUP_SOURCE_BUTTON_LONG,           // Button long press wakeup 2s (enable AP)
    WAKEUP_SOURCE_BUTTON_SUPER_LONG,     // Button super long press 10s (factory reset)
    WAKEUP_SOURCE_REMOTE,                // Remote wakeup (MQTT/Network)
    WAKEUP_SOURCE_WUFI,                  // WUFI wakeup
    WAKEUP_SOURCE_OTHER,                 // Other wakeup
    WAKEUP_SOURCE_MAX
} wakeup_source_type_t;

/**
 * @brief Wakeup source configuration
 */
typedef struct {
    aicam_bool_t enabled;                // Wakeup source enabled
    aicam_bool_t low_power_supported;    // Supported in low power mode
    aicam_bool_t full_speed_supported;   // Supported in full speed mode
    uint32_t debounce_ms;                // Debounce time in milliseconds
    void *config_data;                   // Source-specific configuration
} wakeup_source_config_t;

/**
 * @brief Power mode support features (deprecated - use wakeup source config instead)
 */
typedef struct {
    aicam_bool_t rtc_trigger;            // RTC trigger (for timed wakeup)
    aicam_bool_t io_trigger;             // IO trigger (for sensor wakeup)
    aicam_bool_t bluetooth_trigger;      // Bluetooth trigger
    aicam_bool_t mqtt_remote_trigger;    // MQTT remote trigger
    aicam_bool_t pir_trigger;            // PIR trigger
} power_mode_features_t;

/**
 * @brief Capture trigger types (mapped to wakeup sources)
 */
typedef enum {
    CAPTURE_TRIGGER_IO = WAKEUP_SOURCE_IO,              // IO wakeup
    CAPTURE_TRIGGER_RTC_WAKEUP = WAKEUP_SOURCE_RTC,     // RTC wakeup
    CAPTURE_TRIGGER_PIR = WAKEUP_SOURCE_PIR,            // PIR wakeup
    CAPTURE_TRIGGER_BUTTON = WAKEUP_SOURCE_BUTTON,      // Button wakeup
    CAPTURE_TRIGGER_REMOTE = WAKEUP_SOURCE_REMOTE,      // Remote wakeup
    CAPTURE_TRIGGER_WUFI = WAKEUP_SOURCE_WUFI,          // WUFI wakeup
    CAPTURE_TRIGGER_RTC,                                // RTC trigger
    CAPTURE_TRIGGER_MAX
} capture_trigger_type_t;

/* ==================== System Controller ==================== */

/**
 * @brief System controller handle
 */
typedef struct system_controller_s system_controller_t;

/**
 * @brief System event callback
 */
typedef void (*system_event_callback_t)(system_state_t old_state, 
                                       system_state_t new_state, 
                                       void *user_data);

/**
 * @brief Power mode change callback
 */
typedef void (*power_mode_change_callback_t)(power_mode_t old_mode,
                                            power_mode_t new_mode,
                                            power_trigger_type_t trigger_type,
                                            void *user_data);

/**
 * @brief Work mode change callback
 */
typedef void (*work_mode_change_callback_t)(aicam_work_mode_t old_mode,
                                           aicam_work_mode_t new_mode,
                                           void *user_data);

/**
 * @brief Capture trigger callback
 */
typedef void (*capture_trigger_callback_t)(capture_trigger_type_t trigger_type, void *user_data);
typedef void (*timer_trigger_callback_t)(void *user_data);

/**
 * @brief Create system controller
 * @return System controller handle or NULL on failure
 */
system_controller_t* system_controller_create(void);

/**
 * @brief Destroy system controller
 * @param controller System controller handle
 */
void system_controller_destroy(system_controller_t *controller);

/**
 * @brief Initialize system controller
 * @param controller System controller handle
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_init(system_controller_t *controller);

/**
 * @brief Deinitialize system controller
 * @param controller System controller handle
 */
void system_controller_deinit(system_controller_t *controller);

/**
 * @brief Get current system state
 * @param controller System controller handle
 * @return Current system state
 */
system_state_t system_controller_get_state(system_controller_t *controller);

/**
 * @brief Set system state
 * @param controller System controller handle
 * @param new_state New system state
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_set_state(system_controller_t *controller, 
                                          system_state_t new_state);

/**
 * @brief Register system event callback
 * @param controller System controller handle
 * @param callback Event callback function
 * @param user_data User data pointer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_register_callback(system_controller_t *controller,
                                                  system_event_callback_t callback,
                                                  void *user_data);

/* ==================== Power Mode Management ==================== */

/**
 * @brief Get current power mode
 * @param controller System controller handle
 * @return Current power mode
 */
power_mode_t system_controller_get_power_mode(system_controller_t *controller);

/**
 * @brief Set power mode
 * @param controller System controller handle
 * @param mode New power mode
 * @param trigger_type Trigger type for this change
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_set_power_mode(system_controller_t *controller,
                                               power_mode_t mode,
                                               power_trigger_type_t trigger_type);

/**
 * @brief Get power mode configuration
 * @param controller System controller handle
 * @param config Configuration output buffer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_get_power_config(system_controller_t *controller,
                                                 power_mode_config_t *config);

/**
 * @brief Set power mode configuration
 * @param controller System controller handle
 * @param config Configuration to set
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_set_power_config(system_controller_t *controller,
                                                 const power_mode_config_t *config);

/**
 * @brief Register power mode change callback
 * @param controller System controller handle
 * @param callback Power mode change callback
 * @param user_data User data pointer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_register_power_callback(system_controller_t *controller,
                                                        power_mode_change_callback_t callback,
                                                        void *user_data);

/**
 * @brief Update last activity time (for timeout management)
 * @param controller System controller handle
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_update_activity(system_controller_t *controller);

/**
 * @brief Check and handle power mode timeout
 * @param controller System controller handle
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_check_power_timeout(system_controller_t *controller);

// system_controller_set_power_feature function removed - use wakeup source configuration instead

/* ==================== Work Mode Management ==================== */

/**
 * @brief Get current work mode
 * @param controller System controller handle
 * @return Current work mode
 */
aicam_work_mode_t system_controller_get_work_mode(system_controller_t *controller);

/**
 * @brief Set work mode (image mode and video stream mode)
 * @param controller System controller handle
 * @param mode New work mode
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_set_work_mode(system_controller_t *controller,
                                              aicam_work_mode_t mode);

/**
 * @brief Get work mode configuration
 * @param controller System controller handle
 * @param config Configuration output buffer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_get_work_config(system_controller_t *controller,
                                                work_mode_config_t *config);

/**
 * @brief Set work mode configuration
 * @param controller System controller handle
 * @param config Configuration to set
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_set_work_config(system_controller_t *controller,
                                                const work_mode_config_t *config);

/**
 * @brief Register work mode change callback
 * @param controller System controller handle
 * @param callback Mode change callback function
 * @param user_data User data pointer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_register_work_callback(system_controller_t *controller,
                                                       work_mode_change_callback_t callback,
                                                       void *user_data);

/* ==================== Capture Trigger Manager ==================== */

/**
 * @brief Register IO trigger for capture
 * @param controller System controller handle
 * @param io_pin IO pin number
 * @param trigger_mode Trigger mode (rising, falling, both)
 * @param callback Trigger callback function
 * @param user_data User data pointer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_register_io_trigger(system_controller_t *controller,
                                                    int io_pin,
                                                    int trigger_mode,
                                                    capture_trigger_callback_t callback,
                                                    void *user_data);

/**
 * @brief Register RTC trigger for scheduled capture
 * @param controller System controller handle
 * @param name Schedule name
 * @param trigger_sec Trigger time in seconds from midnight
 * @param day_offset Day offset for cross-day scheduling
 * @param weekdays Weekday mask for weekly scheduling
 * @param repeat Repeat type
 * @param callback Trigger callback function
 * @param user_data User data pointer
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_register_rtc_trigger(system_controller_t *controller,
                                                     wakeup_type_t type,
                                                     const char *name,
                                                     uint64_t trigger_sec,
                                                     int16_t day_offset,
                                                     uint8_t weekdays,
                                                     repeat_type_t repeat,
                                                     timer_trigger_callback_t callback,
                                                     void *user_data);

/**
 * @brief Unregister capture trigger by name
 * @param controller System controller handle
 * @param name Trigger name to unregister
 * @return AICAM_OK on success
 */
aicam_result_t system_controller_unregister_trigger(system_controller_t *controller,
                                                   const char *name);

/* ==================== Simplified System Service ==================== */

aicam_result_t system_controller_get_next_capture_at(
    system_controller_t *controller, uint64_t *next_capture_at);

/**
 * @brief System service context handle
 */
typedef struct system_service_context_s system_service_context_t;

/**
 * @brief Initialize system service
 * @param config Initialization configuration (optional)
 * @return AICAM_OK on success
 */
aicam_result_t system_service_init(void* config);

/**
 * @brief Deinitialize system service
 * @return AICAM_OK on success
 */
aicam_result_t system_service_deinit(void);

/**
 * @brief start system service
 *
 */
aicam_result_t system_service_start(void);

/**
 * @brief stop system service
 * 
 */
aicam_result_t system_service_stop(void);

/**
 * @brief Get system service context
 * @return System service context or NULL if not initialized
 */
system_service_context_t* system_service_get_context(void);

/**
 * @brief Get system service status
 * @return AICAM_OK if service is properly initialized
 */
aicam_result_t system_service_get_status(void);

/**
 * @brief Get system controller from service context
 * @return System controller handle or NULL if not available
 */
system_controller_t* system_service_get_controller(void);

/* ==================== Timer Trigger Management API ==================== */

/**
 * @brief Start timer trigger with current configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t system_service_start_timer_trigger(void);

/**
 * @brief Stop timer trigger
 * @return aicam_result_t Operation result
 */
aicam_result_t system_service_stop_timer_trigger(void);

/**
 * @brief Get timer trigger status
 * @param active Pointer to store active status
 * @param task_count Pointer to store task execution count
 * @return aicam_result_t Operation result
 */
aicam_result_t system_service_get_timer_trigger_status(aicam_bool_t *active, uint32_t *task_count);

/**
 * @brief Apply timer trigger configuration changes
 * @return aicam_result_t Operation result
 */
aicam_result_t system_service_apply_timer_trigger_config(void);

/**
 * @brief Get system service status information
 * @param is_started Pointer to store started status
 * @param timer_configured Pointer to store timer configuration status
 * @param timer_active Pointer to store timer active status
 * @return aicam_result_t Operation result
 */
aicam_result_t system_service_get_status_info(aicam_bool_t *is_started, 
                                            aicam_bool_t *timer_configured, 
                                            aicam_bool_t *timer_active);

/* ==================== Wakeup Source Management API ==================== */

/**
 * @brief Get wakeup source type
 * @return Wakeup source type
 */
wakeup_source_type_t system_service_get_wakeup_source_type(void);

/**
 * @brief Check if wakeup source requires time-optimized mode (skip time-consuming operations)
 * @param source Wakeup source type
 * @return AICAM_TRUE if time-optimized mode is required, AICAM_FALSE otherwise
 * @details Time-optimized mode is used for wakeup sources that need quick response,
 *          such as RTC timer wakeup and button short press. In this mode, time-consuming
 *          operations (like network scan, auto-subscribe) should be skipped to save time.
 *          Wakeup sources like button long press (AP enable) or other sources don't need this optimization.
 */
aicam_bool_t system_service_requires_time_optimized_mode(wakeup_source_type_t source);

/**
 * @brief Check if wakeup source requires only essential services in low power mode
 * @param source Wakeup source type
 * @return AICAM_TRUE if only essential services should be started, AICAM_FALSE otherwise
 * @details In low power mode, some wakeup sources (like RTC, button short press, PIR, IO)
 *          only need essential services to be started for quick response.
 *          Other wakeup sources (like button long press for AP enable) require all services.
 */
aicam_bool_t system_service_requires_only_essential_services(wakeup_source_type_t source);

/**
 * @brief Configure wakeup source
 * @param source Wakeup source type
 * @param config Wakeup source configuration
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_configure_wakeup_source(wakeup_source_type_t source, const wakeup_source_config_t *config);

/**
 * @brief Get wakeup source configuration
 * @param source Wakeup source type
 * @param config Pointer to store wakeup source configuration
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_get_wakeup_source_config(wakeup_source_type_t source, wakeup_source_config_t *config);

/**
 * @brief Check if wakeup source is supported in current power mode
 * @param source Wakeup source type
 * @return AICAM_TRUE if supported, AICAM_FALSE otherwise
 */
aicam_bool_t system_service_is_wakeup_source_supported(wakeup_source_type_t source);

/**
 * @brief Handle wakeup event from external source
 * @param source Wakeup source type
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_handle_wakeup_event(wakeup_source_type_t source);

/**
 * @brief Update system activity (for power management)
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_update_activity(void);

/**
 * @brief Get system activity counter
 * @param counter Pointer to store activity counter
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_get_activity_counter(uint32_t *counter);

/**
 * @brief Force save configuration to persistent storage
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_save_config(void);

/**
 * @brief Force load configuration from persistent storage
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_load_config(void);

/* ==================== Power Mode Configuration API ==================== */

/**
 * @brief Get power mode configuration
 * @param config Pointer to store power mode configuration
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_get_power_mode_config(power_mode_config_t *config);

/**
 * @brief Set power mode configuration
 * @param config Power mode configuration to set
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_set_power_mode_config(const power_mode_config_t *config);

/**
 * @brief Get current power mode
 * @return Current power mode (POWER_MODE_LOW_POWER or POWER_MODE_FULL_SPEED)
 */
power_mode_t system_service_get_current_power_mode(void);

/**
 * @brief Set current power mode
 * @param mode Power mode to set
 * @param trigger_type Trigger type for this change
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_set_current_power_mode(power_mode_t mode, power_trigger_type_t trigger_type);

/* ==================== Capture Callback Management API ==================== */

/**
 * @brief Register capture callback function
 * @param callback Capture callback function (NULL to use default framework)
 * @param user_data User data to pass to callback
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_register_capture_callback(capture_trigger_callback_t callback, void *user_data);

/**
 * @brief Unregister capture callback function
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_unregister_capture_callback(void);

/**
 * @brief Trigger capture manually
 * @param trigger_type Trigger type for this capture
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_trigger_capture(capture_trigger_type_t trigger_type);

/* ==================== Sleep Management API (U0 Integration) ==================== */

/**
 * @brief Mark task as completed and check if should enter sleep
 * @details In low power mode, this will set sleep pending flag
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_task_completed(void);

/**
 * @brief Enter sleep mode immediately
 * @param sleep_duration_sec Sleep duration in seconds (0 = use timer config)
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_enter_sleep(uint32_t sleep_duration_sec);

/**
 * @brief Check if sleep is pending
 * @param pending Pointer to store pending status
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_is_sleep_pending(aicam_bool_t *pending);

/**
 * @brief Execute pending sleep if applicable
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_execute_pending_sleep(void);

/**
 * @brief Request async sleep (for use in callbacks to avoid deadlock)
 * @param duration_sec Sleep duration in seconds (0 = use timer config)
 * @return AICAM_OK on success, error code otherwise
 * @note This sets sleep_pending flag, actual sleep is executed by main loop
 */
aicam_result_t system_service_request_sleep(uint32_t duration_sec);

/**
 * @brief Get last wakeup flag from U0 module
 * @param wakeup_flag Pointer to store wakeup flag
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_get_last_wakeup_flag(uint32_t *wakeup_flag);

/**
 * @brief Force update RTC time to U0 module
 * @details Should be called after setting system time
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_update_rtc_to_u0(void);

/**
 * @brief Process stored wakeup event (call after all services are started)
 * @details This should be called by application after service_manager_start_all()
 *          to process wakeup events that triggered system boot.
 *          During system_service_init(), wakeup flag is only stored, not processed.
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_process_wakeup_event(void);

/**
 * @brief Poll for a scheduled upload-flush node due now (while awake).
 *        Call ~every 15s from the main loop. Handles FULL_SPEED mode and
 *        LOW_POWER awake periods spanning a node without a dedicated wake.
 */
aicam_result_t system_service_poll_scheduled_flush(void);

/**
 * @brief Wait for an in-progress upload flush to finish before sleeping.
 *        Does NOT start a new flush. Call before execute_pending_sleep().
 */
aicam_result_t system_service_wait_upload_before_sleep(uint32_t timeout_ms);

/* ==================== Image Capture and Upload API ==================== */

typedef struct {
    aicam_bool_t enable_ai;
    uint32_t chunk_size;
    aicam_bool_t store_to_sd;
    aicam_bool_t fast_fail_mqtt;              // AICAM_TRUE: quick fail if MQTT not connected
    aicam_capture_trigger_t trigger_type;     // Capture trigger source (default: UNKNOWN)
} system_capture_request_t;

typedef struct {
    aicam_result_t result;
    uint64_t duration_ms;
} system_capture_response_t;

/**
 * @brief Capture image with AI inference and upload to MQTT
 * @details This function performs the complete workflow:
 *          1. Capture image using device service
 *          2. Get JPEG parameters and generate metadata
 *          3. Prepare AI inference results
 *          4. Check MQTT connection and upload (with auto-reconnect)
 *          5. Choose single or chunked upload based on image size
 * @param enable_ai Enable AI inference (AICAM_TRUE/AICAM_FALSE)
 * @param chunk_size Chunk size for large images (0 = auto: 10KB)
 * @param store_to_sd Store image to SD card (AICAM_TRUE/AICAM_FALSE)
 * @param qos MQTT QoS level (0, 1, or 2)
 * @return AICAM_OK on success, error code otherwise
 */
aicam_result_t system_service_capture_and_upload_mqtt(aicam_bool_t enable_ai, 
                                                     uint32_t chunk_size,
                                                     aicam_bool_t store_to_sd,
                                                     aicam_capture_trigger_t trigger_type);

/**
 * @brief Unified capture request entry with defaults and concurrency guard
 */
aicam_result_t system_service_capture_request(const system_capture_request_t *request,
                                              system_capture_response_t *response);

/**
 * @brief Check if a capture operation is currently in progress
 * @return AICAM_TRUE if capture is running, AICAM_FALSE otherwise
 */
aicam_bool_t system_service_capture_in_progress(void);

/**
 * @brief Register PIR debug commands
 * @details Register CLI commands for PIR sensor debugging:
 *   - pir_status: Show PIR sensor status and configuration
 *   - pir_test: Test PIR sensor reading
 *   - pir_cfg: Configure PIR sensor parameters
 *   - pir_wakeup_test: Test PIR wakeup configuration
 */
void system_service_pir_debug_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_SERVICE_H
