/**
 * @file device_service.h
 * @brief Device Service Interface Header
 * @details Device service standard interface definition
 */

#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "misc.h"
#include "json_config_mgr.h"
#include "nn.h"
#include "jpegc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Device Information Types ==================== */

/**
 * @brief Storage management structure
 */
typedef struct {
    // SD card storage
    aicam_bool_t sd_card_connected;          // SD card connected
    uint64_t total_capacity_mb;              // Total capacity (MB) - primary storage
    uint64_t available_capacity_mb;          // Available capacity (MB)
    uint64_t used_capacity_mb;               // Used capacity (MB)
    float usage_percent;                     // Usage percentage
    aicam_bool_t cyclic_overwrite_enabled;   // Cyclic overwrite enabled
    uint32_t overwrite_threshold_percent;    // Overwrite threshold percentage

    // Internal Flash (LittleFS) storage
    aicam_bool_t flash_fs_mounted;           // Internal flash filesystem mounted
    aicam_bool_t flash_fs_error;             // Flash FS error (corrupt/unusable) — needs format
    char flash_error[24];                    // Error detail ("not_mounted"/"corrupt"/"")
    uint64_t flash_total_capacity_mb;        // Flash total capacity (MB)
    uint64_t flash_available_capacity_mb;    // Flash available capacity (MB)
    uint64_t flash_used_capacity_mb;         // Flash used capacity (MB)
    float flash_usage_percent;               // Flash usage percentage
    char flash_fs_type[8];                   // Filesystem type ("littlefs")
} storage_info_t;

/**
 * @brief LED control configuration
 */
typedef struct {
    aicam_bool_t connected;                    // LED connected
    aicam_bool_t enabled;                      // LED enabled
    uint32_t blink_times;                      // Blink times (0 for continuous)
    uint32_t interval_ms;                      // Blink interval (ms)
} led_config_t;

/**
 * @brief System indicator LED states
 * @details Defines the visual indicator states for system status
 */
typedef enum {
    SYSTEM_INDICATOR_SLEEP = 0,              // LED off - low power/power off mode
    SYSTEM_INDICATOR_RUNNING_AP_OFF,         // Slow blink 1Hz - system running, AP closed
    SYSTEM_INDICATOR_RUNNING_AP_ON,          // LED solid on - system running, AP open
    SYSTEM_INDICATOR_FACTORY_RESET,          // Fast blink 5Hz - factory reset in progress
    SYSTEM_INDICATOR_MAX
} system_indicator_state_t;

/**
 * @brief Camera configuration structure
 */
typedef struct {
    aicam_bool_t enabled;                    // Camera enabled
    uint32_t width;                          // Image width
    uint32_t height;                         // Image height
    uint32_t fps;                            // Frame rate
    uint32_t quality;                        // Image quality
    image_config_t image_config;             // Image management configuration
} camera_config_t;

/**
 * @brief Sensor data structure
 */
typedef struct {
    float temperature;                       // Temperature sensor data
    float humidity;                          // Humidity sensor data
    aicam_bool_t pir_detected;              // PIR sensor detection status
    uint32_t light_level;                    // Light sensor data
} sensor_data_t;

/**
 * @brief GPIO pin configuration
 */
typedef struct {
    uint32_t pin_number;                     // GPIO pin number
    aicam_bool_t is_input;                   // Is input mode
    aicam_bool_t pull_up;                    // Pull up enable
    aicam_bool_t pull_down;                  // Pull down enable
} gpio_config_t;

/**
 * @brief Button event types
 */
typedef enum {
    BUTTON_EVENT_PRESS = 0,                  // Button press
    BUTTON_EVENT_RELEASE,                    // Button release
    BUTTON_EVENT_SHORT_PRESS,                // Short press
    BUTTON_EVENT_LONG_PRESS,                 // Long press
    BUTTON_EVENT_DOUBLE_CLICK,               // Double click
    BUTTON_EVENT_SUPER_LONG_PRESS,           // Super long press
    BUTTON_EVENT_MAX
} button_event_t;

/**
 * @brief Button callback function type
 */
typedef void (*button_callback_t)(void *user_data);

/* ==================== Device Service Interface Functions ==================== */

/**
 * @brief Initialize device service
 * @param config Service configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_init(void *config);

/**
 * @brief Start device service
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_start(void);

/**
 * @brief Stop device service
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_stop(void);

/**
 * @brief Deinitialize device service
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_deinit(void);

/**
 * @brief Get device service state
 * @return service_state_t Service state
 */
service_state_t device_service_get_state(void);

/* ==================== Device Information Management ==================== */

/**
 * @brief Get device information
 * @param info Pointer to device_info_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_get_info(device_info_config_t *info);

/**
 * @brief Get cached device info without storage scan (fast, hot-path safe).
 * Only updates battery (HAL read) and returns the in-memory copy.
 * Does NOT call update_storage_info() — use device_service_get_info() when
 * storage capacity/usage data is actually needed.
 * @param info Pointer to device_info_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_get_cached_info(device_info_config_t *info);

/**
 * @brief Update device information
 * @param info Pointer to device_info_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_update_info(const device_info_config_t *info);

/* ==================== Storage Management ==================== */

/**
 * @brief Check SD card connection status
 * @return aicam_bool_t TRUE if connected, FALSE otherwise
 */
aicam_bool_t device_service_storage_is_sd_connected(void);

/**
 * @brief Get storage information
 * @param info Pointer to storage_info_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_storage_get_info(storage_info_t *info);

/**
 * @brief Set cyclic overwrite policy
 * @param enabled Enable cyclic overwrite
 * @param threshold_percent Overwrite threshold percentage (0-100)
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_storage_set_cyclic_overwrite(aicam_bool_t enabled, uint32_t threshold_percent);

/**
 * @brief Write file to storage
 * @param buffer Pointer to buffer
 * @param size Buffer size
 * @param filename Filename
 * @return aicam_result_t Operation result
 */
aicam_result_t sd_write_file(const uint8_t *buffer, uint32_t size, const char *filename);

/**
 * @brief Update device MAC address
 * @return aicam_result_t Operation result
 */
void device_service_update_device_mac_address(void);

/**
 * @brief Update communication type
 * @return aicam_result_t Operation result
 */
void device_service_update_communication_type(void);



/* ==================== Hardware Management ==================== */

/**
 * @brief Get image configuration
 * @param config Pointer to image_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_image_get_config(image_config_t *config);

/**
 * @brief Set image configuration
 * @param config Pointer to image_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_image_set_config(const image_config_t *config);

/**
 * @brief Reload ISP tuning block from JSON/NVS into the device service cache (e.g. after HTTP import).
 */
aicam_result_t device_service_reload_isp_config_from_storage(void);

/**
 * @brief Get light configuration
 * @param config Pointer to light_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_light_get_config(light_config_t *config);

/**
 * @brief Set light configuration
 * @param config Pointer to light_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_light_set_config(const light_config_t *config);

/**
 * @brief Check light connection status
 * @return aicam_bool_t TRUE if connected, FALSE otherwise
 */
aicam_bool_t device_service_light_is_connected(void);

/**
 * @brief Control light manually (for testing)
 * @param enable Enable/disable light
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_light_control(aicam_bool_t enable);

/**
 * @brief Set light brightness level
 * @param brightness_level Brightness level (0-100)
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_light_set_brightness(uint32_t brightness_level);

/**
 * @brief Set light blink mode
 * @param blink_times Number of blinks (0 for continuous)
 * @param interval_ms Blink interval in milliseconds
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_light_blink(uint32_t blink_times, uint32_t interval_ms);

/* ==================== Camera Interface ==================== */

/**
 * @brief Initialize camera
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_init(void);

/**
 * @brief Start camera
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_start(void);

/**
 * @brief Stop camera
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_stop(void);

/**
 * @brief Get camera configuration
 * @param config Pointer to camera_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_get_config(camera_config_t *config);

/**
 * @brief Set camera configuration
 * @param config Pointer to camera_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_set_config(const camera_config_t *config);

/**
 * @brief Capture image
 * @param buffer Pointer to image buffer
 * @param out_len Pointer to actual captured size
 * @param need_ai_inference Whether AI inference is needed
 * @param nn_result Pointer to AI inference result (output, can be NULL if need_ai_inference is false)
 * @param frame_id Pointer to frame ID (output, optional, can be NULL)
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_capture(uint8_t **buffer, int *out_len,  aicam_bool_t need_ai_inference, nn_result_t *nn_result, uint32_t *frame_id);

/**
 * @brief Fast capture image for low-power RTC wakeup
 * @details This API is consistent with device_service_camera_capture but includes device initialization.
 *          Automatically initializes camera/JPEG/light devices, loads AI model, sets pipe2 parameters.
 *          Designed for fast startup scenarios where device service may not be fully started.
 * @param buffer Pointer to image buffer (output)
 * @param out_len Pointer to actual captured size (output)
 * @param need_ai_inference Whether AI inference is needed
 * @param nn_result Pointer to AI inference result (output, can be NULL if need_ai_inference is false)
 * @param frame_id Pointer to frame ID (output, optional, can be NULL)
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_capture_fast(uint8_t **buffer, int *out_len, 
                                                  aicam_bool_t need_ai_inference, nn_result_t *nn_result, uint32_t *frame_id);

/**
 * @brief Get JPEG parameters
 * @param jpeg_params Pointer to jpegc_params_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_get_jpeg_params(jpegc_params_t *jpeg_params);

/**
 * @brief Get JPEG encode parameters
 * @param jpeg_enc_param Pointer to jpegc_params_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_get_jpeg_enc_param(jpegc_params_t *jpeg_enc_param);

/**
 * @brief Free JPEG buffer
 * @param buffer Pointer to JPEG buffer
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_camera_free_jpeg_buffer(uint8_t *buffer);

/* ==================== LED Interface ==================== */

/**
 * @brief Get LED configuration
 * @param config Pointer to led_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_led_get_config(led_config_t *config);

/**
 * @brief Set LED configuration
 * @param config Pointer to led_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_led_set_config(const led_config_t *config);

/**
 * @brief Check if LED is connected
 * @return aicam_bool_t LED connection status
 */
aicam_bool_t device_service_led_is_connected(void);

/**
 * @brief Turn LED on
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_led_on(void);

/**
 * @brief Turn LED off
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_led_off(void);

/**
 * @brief Set LED blink mode
 * @param blink_times Number of blinks (0 for continuous)
 * @param interval_ms Blink interval in milliseconds
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_led_blink(uint32_t blink_times, uint32_t interval_ms);

/**
 * @brief Set system indicator LED state
 * @details Sets the LED according to system status:
 *          - SYSTEM_INDICATOR_SLEEP: LED off
 *          - SYSTEM_INDICATOR_RUNNING_AP_OFF: Slow blink 1Hz (500ms interval)
 *          - SYSTEM_INDICATOR_RUNNING_AP_ON: LED solid on
 *          - SYSTEM_INDICATOR_FACTORY_RESET: Fast blink 5Hz (100ms interval)
 * @param state System indicator state
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_set_indicator_state(system_indicator_state_t state);

/**
 * @brief Get current system indicator LED state
 * @return system_indicator_state_t Current indicator state
 */
system_indicator_state_t device_service_get_indicator_state(void);

/* ==================== Sensor Interface ==================== */

/**
 * @brief Initialize sensors
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_sensor_init(void);

/**
 * @brief Read sensor data
 * @param data Pointer to sensor_data_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_sensor_read(sensor_data_t *data);

/**
 * @brief Enable/disable PIR sensor
 * @param enable Enable flag
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_sensor_pir_enable(aicam_bool_t enable);

/* ==================== GPIO Interface ==================== */

/**
 * @brief Configure GPIO pin
 * @param config Pointer to gpio_config_t structure
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_gpio_config(const gpio_config_t *config);

/**
 * @brief Set GPIO pin state
 * @param pin_number GPIO pin number
 * @param state Pin state (0 or 1)
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_gpio_set(uint32_t pin_number, aicam_bool_t state);

/**
 * @brief Get GPIO pin state
 * @param pin_number GPIO pin number
 * @param state Pointer to store pin state
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_gpio_get(uint32_t pin_number, aicam_bool_t *state);

/* ==================== System Management ==================== */

/**
 * @brief Reset device to factory defaults
 * @details This function will:
 *   1. Reset all configuration to default values
 *   2. Clear AI model slots
 *   3. Restart the system
 * @return aicam_result_t Operation result
 */
aicam_result_t device_service_reset_to_factory_defaults(void);


#ifdef __cplusplus
}
#endif

#endif /* DEVICE_SERVICE_H */
