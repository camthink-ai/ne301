/**
 * @file mqtt_service.h
 * @brief MQTT Service Interface Header
 * @details MQTT service standard interface definition, support MQTT/MQTTS connection management and message handling
 */

#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "aicam_types.h"
#include "ms_mqtt_client.h"
#include <stdint.h>
#include <stdbool.h>
#include "json_config_mgr.h"
#include "nn.h"
#include "cJSON.h"
#include "service_interfaces.h"

/* Forward declaration */
struct si91x_mqtt_client;

#ifdef __cplusplus
extern "C" {
#endif


/* ==================== MQTT Service Types ==================== */

/**
 * @brief MQTT client API type
 */
typedef enum {
    MQTT_API_TYPE_MS = 0,      ///< Use ms_mqtt_client API (default)
    MQTT_API_TYPE_SI91X = 1,   ///< Use si91x_mqtt_client API
} mqtt_api_type_t;

/**
 * @brief MQTT service statistics
 */
typedef struct {
    uint64_t total_connections;              // Total connection attempts
    uint64_t successful_connections;         // Successful connections
    uint64_t failed_connections;             // Failed connections
    uint64_t disconnections;                 // Total disconnections
    uint64_t messages_published;             // Messages published
    uint64_t messages_received;              // Messages received
    uint64_t messages_failed;                // Messages failed to send
    uint64_t subscriptions;                  // Total subscriptions
    uint64_t unsubscriptions;                // Total unsubscriptions
    uint32_t current_connections;            // Current active connections
    uint32_t outbox_size;                    // Current outbox size
    uint32_t last_error_code;                // Last error code
} mqtt_service_stats_t;

/**
 * @brief MQTT service event callback
 */
typedef void (*mqtt_service_event_callback_t)(ms_mqtt_event_data_t *event_data, void *user_data);

/**
 * @brief MQTT service topic configuration
 */
typedef struct {
    char data_receive_topic[128];    // Data receive topic
    char data_report_topic[128];     // Data report topic
    char status_topic[128];          // Status topic
    char command_topic[128];         // Command topic
    
    int data_receive_qos;            // Data receive QoS
    int data_report_qos;             // Data report QoS
    int status_qos;                  // Status QoS
    int command_qos;                 // Command QoS
    
    aicam_bool_t auto_subscribe_receive; // Auto subscribe to receive topic
    aicam_bool_t auto_subscribe_command; // Auto subscribe to command topic
    
    aicam_bool_t enable_status_report;   // Enable status reporting
    int status_report_interval_ms;       // Status report interval
    aicam_bool_t enable_heartbeat;       // Enable heartbeat
    int heartbeat_interval_ms;           // Heartbeat interval

    uint8_t report_content;              // Report content mode (mqtt_report_content_t)
} mqtt_service_topic_config_t;

/* ==================== MQTT Service Interface Functions ==================== */

/**
 * @brief Duplicate a string for MQTT service
 * @param s The string to duplicate
 * @return The duplicated string
 */
char *mqtt_service_strdup(const char *s);

/**
 * @brief Initialize MQTT service
 * @param config MQTT configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_init(void *config);

/**
 * @brief Start MQTT service
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_start(void);

/**
 * @brief Stop MQTT service
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_stop(void);

/**
 * @brief Restart MQTT service
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_restart(void);

/**
 * @brief Deinitialize MQTT service
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_deinit(void);

/**
 * @brief Set MQTT client API type
 * @param api_type API type to use
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_set_api_type(mqtt_api_type_t api_type);

/**
 * @brief Get current MQTT client API type
 * @return mqtt_api_type_t Current API type
 */
mqtt_api_type_t mqtt_service_get_api_type(void);

/**
 * @brief Get MQTT client handle (for MS API only)
 * @return ms_mqtt_client_handle_t MQTT client handle or NULL
 */
ms_mqtt_client_handle_t mqtt_service_get_client(void);

/**
 * @brief Get MQTT client state
 * @return service_state_t MQTT client state
 */
service_state_t mqtt_service_get_state(void);

/**
 * @brief Connect to MQTT broker
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_connect(void);

/**
 * @brief Disconnect from MQTT broker
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_disconnect(void);

/**
 * @brief Reconnect to MQTT broker
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_reconnect(void);

/**
 * @brief Check if connected to broker
 * @return aicam_bool_t Connection status
 */
aicam_bool_t mqtt_service_is_connected(void);

/**
 * @brief Check if MQTT service is initialized
 * @return aicam_bool_t Service initialized status
 */
aicam_bool_t mqtt_service_is_initialized(void);

/* ==================== Message Publishing ==================== */

/**
 * @brief Publish message to topic
 * @param topic Topic to publish to
 * @param payload Message payload
 * @param payload_len Payload length
 * @param qos Quality of Service (0, 1, 2)
 * @param retain Retain flag
 * @return int Message ID or error code
 */
int mqtt_service_publish(const char *topic, 
                        const uint8_t *payload, 
                        int payload_len, 
                        int qos, 
                        int retain);

/**
 * @brief Publish string message to topic
 * @param topic Topic to publish to
 * @param message String message
 * @param qos Quality of Service (0, 1, 2)
 * @param retain Retain flag
 * @return int Message ID or error code
 */
int mqtt_service_publish_string(const char *topic, 
                               const char *message, 
                               int qos, 
                               int retain);

/**
 * @brief Publish JSON message to topic
 * @param topic Topic to publish to
 * @param json_data JSON data
 * @param qos Quality of Service (0, 1, 2)
 * @param retain Retain flag
 * @return int Message ID or error code
 */
int mqtt_service_publish_json(const char *topic, 
                             const char *json_data, 
                             int qos, 
                             int retain);

/**
 * @brief Publish data to configured data report topic
 * @param data Data to publish
 * @param data_len Data length
 * @return int Message ID or error code
 */
int mqtt_service_publish_data(const uint8_t *data, int data_len);

/**
 * @brief Publish status to configured status topic
 * @param status Status message
 * @return int Message ID or error code
 */
int mqtt_service_publish_status(const char *status);

/**
 * @brief Publish JSON data to configured data report topic
 * @param json_data JSON data to publish
 * @return int Message ID or error code
 */
int mqtt_service_publish_data_json(const char *json_data);

/* ==================== Message Subscription ==================== */

/**
 * @brief Subscribe to topic
 * @param topic_filter Topic filter to subscribe to
 * @param qos Maximum Quality of Service
 * @return int Message ID or error code
 */
int mqtt_service_subscribe(const char *topic_filter, int qos);

/**
 * @brief Unsubscribe from topic
 * @param topic_filter Topic filter to unsubscribe from
 * @return int Message ID or error code
 */
int mqtt_service_unsubscribe(const char *topic_filter);

/* ==================== Configuration Management ==================== */

/**
 * @brief Get MQTT service configuration
 * @param config Configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_get_config(ms_mqtt_config_t *config);

/**
 * @brief Set MQTT service configuration
 * @param config Configuration structure
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_set_config(const ms_mqtt_config_t *config);

/**
 * @brief Get MQTT service topic configuration
 * @param config Topic configuration output
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_get_topic_config(mqtt_service_topic_config_t *config);

/**
 * @brief Set MQTT service topic configuration
 * @param config Topic configuration input
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_set_topic_config(const mqtt_service_topic_config_t *config);

/**
 * @brief Get the configured data report content mode
 * @return mqtt_report_content_t Report content mode (full when unset or service not initialized)
 */
mqtt_report_content_t mqtt_service_get_report_content(void);

/* ==================== Event Management ==================== */

/**
 * @brief Register event callback
 * @param callback Event callback function
 * @param user_data User data pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_register_event_callback(mqtt_service_event_callback_t callback, 
                                                   void *user_data);

/**
 * @brief Unregister event callback
 * @param callback Event callback function
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_unregister_event_callback(mqtt_service_event_callback_t callback);

/* ==================== Event Wait API ==================== */

/**
 * @brief Wait for specific MQTT event(s)
 * @param event_id Event ID to wait for
 * @param wait_all If true, wait for all specified events; if false, wait for any one
 * @param timeout_ms Timeout in milliseconds (osWaitForever for infinite wait)
 * @return AICAM_OK if event received, AICAM_ERROR_TIMEOUT if timeout, AICAM_ERROR otherwise
 * 
 * @note This function blocks until the specified event occurs or timeout
 * @note After the event is received, the flag is automatically cleared
 * @note Example: Wait for MQTT_EVENT_PUBLISHED to know when a message is published
 */
aicam_result_t mqtt_service_wait_for_event(ms_mqtt_event_id_t event_id, aicam_bool_t wait_all, uint32_t timeout_ms);

/**
 * @brief Clear event flag for specific event
 * @param event_id Event ID to clear
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_clear_event_flag(ms_mqtt_event_id_t event_id);

/**
 * @brief Check if event flag is set (non-blocking)
 * @param event_id Event ID to check
 * @return AICAM_TRUE if set, AICAM_FALSE otherwise
 */
aicam_bool_t mqtt_service_is_event_set(ms_mqtt_event_id_t event_id);

/* ==================== Statistics and Monitoring ==================== */

/**
 * @brief Get MQTT service statistics
 * @param stats Statistics structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_get_stats(mqtt_service_stats_t *stats);

/**
 * @brief Reset MQTT service statistics
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_reset_stats(void);

/**
 * @brief Get outbox size (pending messages)
 * @return int Number of pending messages
 */
int mqtt_service_get_outbox_size(void);

/**
 * @brief Get MQTT service version
 * @return const char* Version string
 */
const char* mqtt_service_get_version(void);

/**
 * @brief Check if MQTT service is running
 * @return aicam_bool_t Service running status
 */
aicam_bool_t mqtt_service_is_running(void);

/* ==================== Image Upload with AI Results ==================== */

/**
 * @brief Image format enumeration
 */
typedef enum {
    MQTT_IMAGE_FORMAT_JPEG = 0,
    MQTT_IMAGE_FORMAT_PNG,
    MQTT_IMAGE_FORMAT_BMP,
    MQTT_IMAGE_FORMAT_RAW
} mqtt_image_format_t;

/**
 * @brief Image metadata
 */
typedef struct {
    char image_id[64];               // Unique image ID (timestamp-based or UUID)
    uint64_t timestamp;              // Capture timestamp (Unix epoch)
    mqtt_image_format_t format;      // Image format
    uint32_t width;                  // Image width
    uint32_t height;                 // Image height
    uint32_t size;                   // Image data size in bytes
    uint8_t quality;                 // JPEG quality (1-100)
    aicam_capture_trigger_t trigger_type; // Capture trigger source
} mqtt_image_metadata_t;

/**
 * @brief AI inference result metadata for MQTT
 */
typedef struct {
    char model_name[64];             // AI model name
    char model_version[32];          // Model version
    uint32_t inference_time_ms;      // Inference time in milliseconds
    nn_result_t ai_result;           // AI inference result
    float confidence_threshold;      // Confidence threshold used
    float nms_threshold;             // NMS threshold used
} mqtt_ai_result_t;

/**
 * @brief Upload image with AI results (JSON + Base64 format)
 * @param topic MQTT topic to publish to (NULL = use default data report topic)
 * @param image_data Raw image data buffer
 * @param image_size Image data size in bytes
 * @param metadata Image metadata
 * @param ai_result AI inference result (can be NULL if no AI processing)
 * @return int Message ID or error code
 * 
 * @note This function is suitable for small images (< 100KB recommended)
 *       The image will be Base64 encoded (increases size by ~33%)
 *       Total MQTT message should not exceed broker's max message size
 */
int mqtt_service_publish_image_with_ai(const char *topic,
                                       const uint8_t *image_data,
                                       uint32_t image_size,
                                       const mqtt_image_metadata_t *metadata,
                                       const mqtt_ai_result_t *ai_result);

/**
 * @brief Upload image metadata and AI results only (no image data)
 * @param topic MQTT topic to publish to (NULL = use default data report topic)
 * @param metadata Image metadata
 * @param ai_result AI inference result (can be NULL if no AI processing)
 * @param qos Quality of Service (0, 1, 2), -1 = use default
 * @return int Message ID or error code
 * 
 * @note Use this when image is uploaded separately (e.g., via HTTP)
 *       Only metadata and AI results are sent as JSON
 */
int mqtt_service_publish_ai_result(const char *topic,
                                   const mqtt_image_metadata_t *metadata,
                                   const mqtt_ai_result_t *ai_result,
                                   int qos);

/**
 * @brief Upload image in chunks (for large images)
 * @param topic MQTT topic to publish to (NULL = use default data report topic)
 * @param image_data Raw image data buffer
 * @param image_size Image data size in bytes
 * @param metadata Image metadata
 * @param ai_result AI inference result (can be NULL)
 * @param chunk_size Size of each chunk in bytes (recommended: 1024-4096)
 * @return int Number of chunks sent or error code
 * 
 * @note First message contains metadata and AI result
 *       Following messages contain image chunks
 *       Each chunk has sequence number for reassembly
 */
int mqtt_service_publish_image_chunked(const char *topic,
                                       const uint8_t *image_data,
                                       uint32_t image_size,
                                       const mqtt_image_metadata_t *metadata,
                                       const mqtt_ai_result_t *ai_result,
                                       uint32_t chunk_size);

/**
 * @brief Helper: Initialize mqtt_ai_result_t structure
 * @param mqtt_result Output MQTT AI result structure
 * @param nn_result AI inference result from nn module
 * @param model_name Model name string
 * @param model_version Model version string (can be NULL)
 * @param inference_time_ms Inference time in milliseconds
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_init_ai_result(mqtt_ai_result_t *mqtt_result,
                                           const nn_result_t *nn_result,
                                           const char *model_name,
                                           const char *model_version,
                                           uint32_t inference_time_ms);

/**
 * @brief Helper: Generate unique image ID based on timestamp
 * @param image_id Output buffer for image ID (min 64 bytes)
 * @param prefix Optional prefix for image ID
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_generate_image_id(char *image_id, const char *prefix);

/**
 * @brief Update MQTT client ID and topic
 */
void mqtt_service_update_client_id_and_topic(void);

/* ==================== MQTT Control Command Protocol ==================== */

/**
 * @brief MQTT control command types
 */
typedef enum {
    MQTT_CMD_CAPTURE = 0,           ///< Capture image command
    MQTT_CMD_SLEEP,                 ///< Enter sleep mode command
    MQTT_CMD_TASK_COMPLETED,        ///< Mark task as completed
    MQTT_CMD_MAX
} mqtt_control_cmd_type_t;

/**
 * @brief MQTT control command structure
 */
typedef struct {
    mqtt_control_cmd_type_t cmd_type;  ///< Command type
    char request_id[64];               ///< Request ID for response matching
    union {
        struct {
            aicam_bool_t enable_ai;     ///< Enable AI inference
            uint32_t chunk_size;        ///< Chunk size (0 = auto)
            aicam_bool_t store_to_sd;   ///< Store to SD card
        } capture;
        struct {
            uint32_t duration_sec;      ///< Sleep duration in seconds (0 = use timer config)
        } sleep;
    } params;
} mqtt_control_cmd_t;

/**
 * @brief Parse MQTT control command from JSON message
 * @param json_message JSON message string
 * @param json_len Message length
 * @param cmd Output command structure
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_parse_control_cmd(const char *json_message, size_t json_len, mqtt_control_cmd_t *cmd);

/**
 * @brief Execute MQTT control command
 * @param cmd Control command to execute
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_execute_control_cmd(const mqtt_control_cmd_t *cmd);

/* ==================== CLI Commands ==================== */

/**
 * @brief Register MQTT CLI commands
 */
void mqtt_cmd_register(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_SERVICE_H */
