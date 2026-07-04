/**
 * @file mqtt_service.c
 * @brief MQTT Service Implementation
 * @details MQTT service standard interface implementation, integrate MQTT/MQTTS connection management and message handling
 */

#include "mqtt_service.h"
#include "aicam_types.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "ms_mqtt_client.h"
#include "si91x_mqtt_client.h"
#include "web_server.h"
#include "web_api.h"
#include "system_service.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "generic_file.h"
#include "drtc.h"
#include "cmsis_os2.h"
#include "service_init.h"
#include "common_utils.h"
#include "device_service.h"
/* ==================== MQTT Service Context ==================== */

#define MQTT_SERVICE_VERSION "1.0.0"
#define MAX_EVENT_CALLBACKS 8

/* ==================== MQTT Event Flags ==================== */

// MQTT event flag bit definitions (each event_id corresponds to one bit)
// Note: event_id ranges from -1 to 11, needs to be mapped to bit positions 0-31
#define MQTT_EVENT_FLAG_ERROR        (1UL << 0)   // MQTT_EVENT_ERROR (0)
#define MQTT_EVENT_FLAG_STARTED      (1UL << 1)   // MQTT_EVENT_STARTED (1)
#define MQTT_EVENT_FLAG_STOPPED      (1UL << 2)   // MQTT_EVENT_STOPPED (2)
#define MQTT_EVENT_FLAG_CONNECTED    (1UL << 3)   // MQTT_EVENT_CONNECTED (3)
#define MQTT_EVENT_FLAG_DISCONNECTED (1UL << 4)   // MQTT_EVENT_DISCONNECTED (4)
#define MQTT_EVENT_FLAG_SUBSCRIBED   (1UL << 5)   // MQTT_EVENT_SUBSCRIBED (5)
#define MQTT_EVENT_FLAG_UNSUBSCRIBED (1UL << 6)   // MQTT_EVENT_UNSUBSCRIBED (6)
#define MQTT_EVENT_FLAG_PUBLISHED    (1UL << 7)   // MQTT_EVENT_PUBLISHED (7)
#define MQTT_EVENT_FLAG_DATA         (1UL << 8)   // MQTT_EVENT_DATA (8)
#define MQTT_EVENT_FLAG_BEFORE_CONNECT (1UL << 9) // MQTT_EVENT_BEFORE_CONNECT (9)
#define MQTT_EVENT_FLAG_DELETED      (1UL << 10)  // MQTT_EVENT_DELETED (10)
#define MQTT_EVENT_FLAG_USER         (1UL << 11)  // MQTT_EVENT_USER (11)

/**
 * @brief Map event_id to event flag bit
 */
static uint32_t event_id_to_flag(ms_mqtt_event_id_t event_id)
{
    switch (event_id) {
        case MQTT_EVENT_ERROR:        return MQTT_EVENT_FLAG_ERROR;
        case MQTT_EVENT_STARTED:      return MQTT_EVENT_FLAG_STARTED;
        case MQTT_EVENT_STOPPED:      return MQTT_EVENT_FLAG_STOPPED;
        case MQTT_EVENT_CONNECTED:    return MQTT_EVENT_FLAG_CONNECTED;
        case MQTT_EVENT_DISCONNECTED: return MQTT_EVENT_FLAG_DISCONNECTED;
        case MQTT_EVENT_SUBSCRIBED:   return MQTT_EVENT_FLAG_SUBSCRIBED;
        case MQTT_EVENT_UNSUBSCRIBED: return MQTT_EVENT_FLAG_UNSUBSCRIBED;
        case MQTT_EVENT_PUBLISHED:    return MQTT_EVENT_FLAG_PUBLISHED;
        case MQTT_EVENT_DATA:         return MQTT_EVENT_FLAG_DATA;
        case MQTT_EVENT_BEFORE_CONNECT: return MQTT_EVENT_FLAG_BEFORE_CONNECT;
        case MQTT_EVENT_DELETED:      return MQTT_EVENT_FLAG_DELETED;
        case MQTT_EVENT_USER:         return MQTT_EVENT_FLAG_USER;
        default:                      return 0;
    }
}

typedef struct {
    ms_mqtt_config_t base_config;              // MQTT configuration
    
    // Topic configuration
    char data_receive_topic[MAX_TOPIC_LENGTH];    // Data receive topic
    char data_report_topic[MAX_TOPIC_LENGTH];     // Data report topic
    char status_topic[MAX_TOPIC_LENGTH];          // Status topic
    char command_topic[MAX_TOPIC_LENGTH];         // Command topic
    
    // QoS configuration
    uint8_t data_receive_qos;                    // Data receive QoS (0-2)
    uint8_t data_report_qos;                     // Data report QoS (0-2)
    uint8_t status_qos;                          // Status QoS (0-2)
    uint8_t command_qos;                         // Command QoS (0-2)
    
    // Auto subscription
    aicam_bool_t auto_subscribe_receive;         // Auto subscribe to receive topic
    aicam_bool_t auto_subscribe_command;         // Auto subscribe to command topic
    
    // Message configuration
    aicam_bool_t enable_status_report;           // Enable status reporting
    uint32_t status_report_interval_ms;          // Status report interval (ms)
    aicam_bool_t enable_heartbeat;               // Enable heartbeat
    uint32_t heartbeat_interval_ms;              // Heartbeat interval (ms)
    uint8_t report_content;                      // Report content mode (mqtt_report_content_t)

    // Continuous AI telemetry
    aicam_bool_t telemetry_enabled;              // Publish AI results continuously
    char telemetry_topic[MAX_TOPIC_LENGTH];      // Telemetry topic
    uint8_t telemetry_qos;                       // Telemetry QoS (0-2)
} mqtt_service_extended_config_t;

typedef struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    mqtt_service_extended_config_t config;
    mqtt_service_stats_t stats;
    
    // API type and client handle (union to support different API types)
    mqtt_api_type_t api_type;
    union {
        ms_mqtt_client_handle_t ms_client;     // For MS API
        void *si91x_client;                    // For SI91X API (opaque pointer)
    } mqtt_client;
    
    // Event callbacks
    mqtt_service_event_callback_t event_callbacks[MAX_EVENT_CALLBACKS];
    void *event_user_data[MAX_EVENT_CALLBACKS];
    uint32_t callback_count;
    
    // Event flags for waiting on specific events
    osEventFlagsId_t event_flags;
    
    // Auto subscription status
    aicam_bool_t receive_topic_subscribed;
    aicam_bool_t command_topic_subscribed;
    
    // MQTT connection task
    osThreadId_t connect_task_handle;
    aicam_bool_t connect_task_running;
} mqtt_service_context_t;

static mqtt_service_context_t g_mqtt_service = {0};
static uint8_t mqtt_connect_task_stack[1024 * 4] ALIGN_32 IN_PSRAM;
static int mqtt_status_cmd(int argc, char* argv[]);
static void auto_subscribe_topics(void);
static void mqtt_control_cmd_handle_message(ms_mqtt_event_data_t *event_data);
static void mqtt_connect_task(void *argument);
static ms_mqtt_state_t mqtt_client_get_state_si91x(void);


/* ==================== API Type Management ==================== */

aicam_result_t mqtt_service_set_api_type(mqtt_api_type_t api_type)
{
    if (g_mqtt_service.initialized && g_mqtt_service.running) {
        LOG_SVC_ERROR("Cannot change API type while service is running");
        return AICAM_ERROR;
    }
    
    if (api_type != MQTT_API_TYPE_MS && api_type != MQTT_API_TYPE_SI91X) {
        LOG_SVC_ERROR("Invalid API type: %d", api_type);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    g_mqtt_service.api_type = api_type;
    LOG_SVC_INFO("MQTT API type set to: %s", 
                api_type == MQTT_API_TYPE_MS ? "MS" : "SI91X");
    
    return AICAM_OK;
}

mqtt_api_type_t mqtt_service_get_api_type(void)
{
    return g_mqtt_service.api_type;
}

/* ==================== API Adapter Functions ==================== */

/**
 * @brief Initialize client using MS API
 */
static aicam_result_t mqtt_client_init_ms(const ms_mqtt_config_t *config)
{
    g_mqtt_service.mqtt_client.ms_client = ms_mqtt_client_init(config);
    if (!g_mqtt_service.mqtt_client.ms_client) {
        LOG_SVC_ERROR("Failed to initialize MS MQTT client");
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Initialize client using SI91X API
 */
static aicam_result_t mqtt_client_init_si91x(const ms_mqtt_config_t *config)
{
    aicam_result_t result = service_wait_for_ready(SERVICE_READY_STA, AICAM_TRUE, osWaitForever);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to wait for STA service to be ready: %d", result);
        return AICAM_ERROR;
    }
    result = si91x_mqtt_client_init(config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize SI91X MQTT client: %d", result);
        return AICAM_ERROR;
    }
    // SI91X client is managed internally, we just mark it as initialized
    g_mqtt_service.mqtt_client.si91x_client = (void*)0x1; // Non-NULL marker
    return AICAM_OK;
}

/**
 * @brief Start client using MS API
 */
static aicam_result_t mqtt_client_start_ms(void)
{
    int result = ms_mqtt_client_start(g_mqtt_service.mqtt_client.ms_client);
    printf("mqtt start time: %lu ms\r\n", (unsigned long)rtc_get_uptime_ms());
    if (result != 0) {
        LOG_SVC_ERROR("Failed to start MS MQTT client: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Start client using SI91X API
 */
static aicam_result_t mqtt_client_start_si91x(void)
{
    const int MAX_CONNECT_RETRIES = 3;
    const int CONNECT_RETRY_DELAY_MS = 1000;
    const int CONNECT_TIMEOUT_MS = 10000;

    int result = 0;
    ms_mqtt_state_t state;

    LOG_SVC_INFO("SI91X MQTT client connecting...");

    // Retry connect
    for (int retry = 0; retry < MAX_CONNECT_RETRIES; retry++) {
        if (retry > 0) {
            LOG_SVC_WARN("SI91X MQTT connect retry %d/%d after %dms delay",
                        retry + 1, MAX_CONNECT_RETRIES, CONNECT_RETRY_DELAY_MS);
            osDelay(CONNECT_RETRY_DELAY_MS);
        }

        result = si91x_mqtt_client_connnect_sync(CONNECT_TIMEOUT_MS);
        state = mqtt_client_get_state_si91x();

        LOG_SVC_INFO("SI91X MQTT connect result: %d (0x%X), state: %d", result, result, state);

        if (result == 0 && state == MQTT_STATE_CONNECTED) {
            LOG_SVC_INFO("SI91X MQTT client connected successfully");
            break;
        }

        LOG_SVC_ERROR("SI91X MQTT connect failed: result=%d, state=%d (expected %d)",
                     result, state, MQTT_STATE_CONNECTED);
    }

    // Final state check
    state = mqtt_client_get_state_si91x();
    if (state != MQTT_STATE_CONNECTED) {
        LOG_SVC_ERROR("SI91X MQTT client not connected after %d retries, final state: %d",
                     MAX_CONNECT_RETRIES, state);
        return AICAM_ERROR;
    }

    // Subscribe with retry
    const int MAX_SUBSCRIBE_RETRIES = 3;
    const int SUBSCRIBE_RETRY_DELAY_MS = 500;

    for (int retry = 0; retry < MAX_SUBSCRIBE_RETRIES; retry++) {
        if (retry > 0) {
            LOG_SVC_WARN("SI91X MQTT subscribe retry %d/%d", retry + 1, MAX_SUBSCRIBE_RETRIES);
            osDelay(SUBSCRIBE_RETRY_DELAY_MS);

            // Re-check state before retry
            state = mqtt_client_get_state_si91x();
            if (state != MQTT_STATE_CONNECTED) {
                LOG_SVC_ERROR("SI91X MQTT state changed to %d before subscribe, aborting", state);
                return AICAM_ERROR;
            }
        }

        auto_subscribe_topics();

        // Check if subscribe succeeded by checking the flag
        if (g_mqtt_service.receive_topic_subscribed) {
            LOG_SVC_INFO("SI91X MQTT client started and subscribed successfully");
            return AICAM_OK;
        }
    }

    // Subscribe failed but connect succeeded, still return OK
    LOG_SVC_WARN("SI91X MQTT client started but subscribe failed after %d retries",
                MAX_SUBSCRIBE_RETRIES);
    return AICAM_OK;
}

/**
 * @brief Register event handler using MS API
 */
static aicam_result_t mqtt_client_register_event_ms(ms_mqtt_client_event_handler_t handler, void *user_arg)
{
    int result = ms_mqtt_client_register_event(g_mqtt_service.mqtt_client.ms_client, handler, user_arg);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to register MS MQTT event handler: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Register event handler using SI91X API
 */
static aicam_result_t mqtt_client_register_event_si91x(ms_mqtt_client_event_handler_t handler, void *user_arg)
{
    int result = si91x_mqtt_client_register_event(handler, user_arg);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to register SI91X MQTT event handler: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Disconnect client using MS API
 */
static aicam_result_t mqtt_client_disconnect_ms(void)
{
    int result = ms_mqtt_client_disconnect(g_mqtt_service.mqtt_client.ms_client);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to disconnect MS MQTT client: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Disconnect client using SI91X API
 */
static aicam_result_t mqtt_client_disconnect_si91x(void)
{
    int result = si91x_mqtt_client_disconnect();
    if (result != 0) {
        LOG_SVC_ERROR("Failed to disconnect SI91X MQTT client: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Stop client using MS API
 */
static aicam_result_t mqtt_client_stop_ms(void)
{
    int result = ms_mqtt_client_stop(g_mqtt_service.mqtt_client.ms_client);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to stop MS MQTT client: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Stop client using SI91X API
 */
static aicam_result_t mqtt_client_stop_si91x(void)
{
    // SI91X doesn't have separate stop, use disconnect
    return mqtt_client_disconnect_si91x();
}

/**
 * @brief Destroy client using MS API
 */
static aicam_result_t mqtt_client_destroy_ms(void)
{
    int result = ms_mqtt_client_destroy(g_mqtt_service.mqtt_client.ms_client);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to destroy MS MQTT client: %d", result);
        return AICAM_ERROR;
    }
    g_mqtt_service.mqtt_client.ms_client = NULL;
    return AICAM_OK;
}

/**
 * @brief Destroy client using SI91X API
 */
static aicam_result_t mqtt_client_destroy_si91x(void)
{
    int result = si91x_mqtt_client_deinit();
    if (result != 0) {
        LOG_SVC_ERROR("Failed to deinit SI91X MQTT client: %d", result);
        return AICAM_ERROR;
    }
    g_mqtt_service.mqtt_client.si91x_client = NULL;
    return AICAM_OK;
}

/**
 * @brief Get state using MS API
 */
static ms_mqtt_state_t mqtt_client_get_state_ms(void)
{
    if (!g_mqtt_service.mqtt_client.ms_client) {
        return MQTT_STATE_STOPPED;
    }
    return ms_mqtt_client_get_state(g_mqtt_service.mqtt_client.ms_client);
}

/**
 * @brief Get state using SI91X API
 */
static ms_mqtt_state_t mqtt_client_get_state_si91x(void)
{
    return si91x_mqtt_client_get_state();
}

/**
 * @brief Reconnect client using MS API
 */
static aicam_result_t mqtt_client_reconnect_ms(void)
{
    int result = ms_mqtt_client_reconnect(g_mqtt_service.mqtt_client.ms_client);
    if (result != 0) {
        LOG_SVC_ERROR("Failed to reconnect MS MQTT client: %d", result);
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

/**
 * @brief Reconnect client using SI91X API
 */
static aicam_result_t mqtt_client_reconnect_si91x(void)
{
    // SI91X doesn't have separate reconnect, use disconnect + connect
    mqtt_client_disconnect_si91x();
    return mqtt_client_start_si91x();
}

/**
 * @brief Publish using MS API
 */
static int mqtt_client_publish_ms(const char *topic, const uint8_t *data, int len, int qos, int retain)
{
    return ms_mqtt_client_publish(g_mqtt_service.mqtt_client.ms_client, 
                                (char*)topic, (uint8_t*)data, len, qos, retain);
}

/**
 * @brief Publish using SI91X API
 */
static int mqtt_client_publish_si91x(const char *topic, const uint8_t *data, int len, int qos, int retain)
{
    return si91x_mqtt_client_publish(topic, (const char*)data, len, qos, retain);
}

/**
 * @brief Subscribe using MS API
 */
static int mqtt_client_subscribe_ms(const char *topic, int qos)
{
    return ms_mqtt_client_subscribe_single(g_mqtt_service.mqtt_client.ms_client, 
                                        (char*)topic, qos);
}

/**
 * @brief Subscribe using SI91X API
 */
static int mqtt_client_subscribe_si91x(const char *topic, int qos)
{
    return si91x_mqtt_client_subscribe_sync(topic, qos, 5000);
}

/**
 * @brief Unsubscribe using MS API
 */
static int mqtt_client_unsubscribe_ms(const char *topic)
{
    return ms_mqtt_client_unsubscribe(g_mqtt_service.mqtt_client.ms_client, (char*)topic);
}

/**
 * @brief Unsubscribe using SI91X API
 */
static int mqtt_client_unsubscribe_si91x(const char *topic)
{
    return si91x_mqtt_client_unsubscribe(topic);
}

/**
 * @brief Get outbox size using MS API
 */
static int mqtt_client_get_outbox_size_ms(void)
{
    if (!g_mqtt_service.mqtt_client.ms_client) {
        return 0;
    }
    return ms_mqtt_client_get_outbox_size(g_mqtt_service.mqtt_client.ms_client);
}

/**
 * @brief Get outbox size using SI91X API
 */
static int mqtt_client_get_outbox_size_si91x(void)
{
    // SI91X doesn't expose outbox size, return 0
    return 0;
}



/* ==================== Helper Functions ==================== */

/**
 * @brief Duplicate a string
 * @param s The string to duplicate
 * @return The duplicated string
 */
static char *my_strdup(const char *s) {
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s) + 1;    
    char *copy = (char *)buffer_calloc(1, len);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, s, len); 
    return copy;
}

/**
 * @brief Duplicate a string for MQTT service
 * @param s The string to duplicate
 * @return The duplicated string
 */
char *mqtt_service_strdup(const char *s) {
    return my_strdup(s);
}

/**
 * @brief Send MQTT service event
 */
static void send_mqtt_service_event(ms_mqtt_event_data_t *event_data)
{
    for (uint32_t i = 0; i < g_mqtt_service.callback_count; i++) {
        if (g_mqtt_service.event_callbacks[i]) {
            g_mqtt_service.event_callbacks[i](event_data, g_mqtt_service.event_user_data[i]);
        }
    }
}

/**
 * @brief Auto subscribe to configured topics
 */
static void auto_subscribe_topics(void)
{
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) return;
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) return;
    }
    
    // Subscribe to data receive topic
    if (g_mqtt_service.config.auto_subscribe_receive && 
        strlen(g_mqtt_service.config.data_receive_topic) > 0) {
        int result;
        if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
            result = mqtt_client_subscribe_ms(g_mqtt_service.config.data_receive_topic,
                                                    g_mqtt_service.config.data_receive_qos);
        } else {
            result = mqtt_client_subscribe_si91x(g_mqtt_service.config.data_receive_topic,
                                                g_mqtt_service.config.data_receive_qos);
        }
        if (result >= 0) {
            g_mqtt_service.receive_topic_subscribed = AICAM_TRUE;
            LOG_SVC_DEBUG("Auto subscribed to data receive topic: %s", 
                        g_mqtt_service.config.data_receive_topic);
        } else {
            LOG_SVC_ERROR("Failed to auto subscribe to data receive topic: %d", result);
        }
    }
    
    // Subscribe to command topic
    // if (g_mqtt_service.config.auto_subscribe_command && 
    //     strlen(g_mqtt_service.config.command_topic) > 0) {
    //     int result = ms_mqtt_client_subscribe_single(g_mqtt_service.mqtt_client,
    //                                                 g_mqtt_service.config.command_topic,
    //                                                 g_mqtt_service.config.command_qos);
    //     if (result >= 0) {
    //         g_mqtt_service.command_topic_subscribed = AICAM_TRUE;
    //         LOG_SVC_DEBUG("Auto subscribed to command topic: %s", 
    //                      g_mqtt_service.config.command_topic);
    //     } else {
    //         LOG_SVC_ERROR("Failed to auto subscribe to command topic: %d", result);
    //     }
    // }
}

/**
 * @brief MQTT client event handler
 */
static void mqtt_client_event_handler(ms_mqtt_event_data_t *event_data, void *user_args)
{
    (void)user_args;
    
    if (!event_data) return;
    
    // Update statistics
    switch (event_data->event_id) {
        case MQTT_EVENT_CONNECTED:
            g_mqtt_service.stats.successful_connections++;
            g_mqtt_service.stats.current_connections = 1;
            LOG_SVC_DEBUG("MQTT connected to broker");

            printf("mqtt connected time: %lu ms\r\n", (unsigned long)rtc_get_uptime_ms());
            
            // Set MQTT network connected flag
            service_set_mqtt_net_connected(AICAM_TRUE);
            
            // Auto subscribe to configured topics (skip in time-optimized mode to save time)
            wakeup_source_type_t wakeup_source = system_service_get_wakeup_source_type();
            aicam_bool_t requires_time_optimized = system_service_requires_time_optimized_mode(wakeup_source);
            if (!requires_time_optimized) {
                auto_subscribe_topics();
            }
            
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            g_mqtt_service.stats.disconnections++;
            g_mqtt_service.stats.current_connections = 0;
            g_mqtt_service.receive_topic_subscribed = AICAM_FALSE;
            g_mqtt_service.command_topic_subscribed = AICAM_FALSE;
            LOG_SVC_DEBUG("MQTT disconnected from broker");
            
            // Clear MQTT network connected flag
            service_set_mqtt_net_connected(AICAM_FALSE);
            break;
            
        case MQTT_EVENT_DATA:
            g_mqtt_service.stats.messages_received++;
            LOG_SVC_DEBUG("MQTT message received: topic=%s, len=%d", 
                        event_data->topic ? (char*)event_data->topic : "unknown", 
                        event_data->data_len);
            
            // Handle MQTT message: try control command first
            if (event_data->topic && event_data->data) {
                // try as control command
                mqtt_control_cmd_handle_message(event_data);
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            g_mqtt_service.stats.messages_published++;
            LOG_SVC_DEBUG("MQTT message published: msg_id=%d", event_data->msg_id);
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            g_mqtt_service.stats.subscriptions++;
            LOG_SVC_DEBUG("MQTT topic subscribed: %s", 
                        event_data->topic ? (char*)event_data->topic : "unknown");
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            g_mqtt_service.stats.unsubscriptions++;
            LOG_SVC_DEBUG("MQTT topic unsubscribed: %s", 
                        event_data->topic ? (char*)event_data->topic : "unknown");
            break;
            
        case MQTT_EVENT_ERROR:
            g_mqtt_service.stats.failed_connections++;
            g_mqtt_service.stats.last_error_code = event_data->error_code;
            LOG_SVC_ERROR("MQTT error: %d", event_data->error_code);
            break;
            
        default:
            break;
    }
    
    // Set event flag for this event (if event flags is initialized)
    if (g_mqtt_service.event_flags) {
        uint32_t flag = event_id_to_flag(event_data->event_id);
        if (flag != 0) {
            osEventFlagsSet(g_mqtt_service.event_flags, flag);
            LOG_SVC_DEBUG("Event flag set: event_id=%d, flag=0x%08X", event_data->event_id, flag);
        }
    }
    
    // Forward event to registered callbacks
    send_mqtt_service_event(event_data);
}


/* ==================== Helper Functions ==================== */
/**
 * @brief Free all allocated string memory in MQTT configuration
 * @param config MQTT configuration to free
 */
static void free_mqtt_config_strings(ms_mqtt_config_t *config)
{
    if (!config) return;
    
    if (config->base.hostname) {
        buffer_free(config->base.hostname);
        config->base.hostname = NULL;
    }
    if (config->base.client_id) {
        buffer_free(config->base.client_id);
        config->base.client_id = NULL;
    }
    if (config->authentication.username) {
        buffer_free(config->authentication.username);
        config->authentication.username = NULL;
    }
    if (config->authentication.password) {
        buffer_free(config->authentication.password);
        config->authentication.password = NULL;
    }
    if (config->authentication.ca_path) {
        buffer_free(config->authentication.ca_path);
        config->authentication.ca_path = NULL;
    }
    if (config->authentication.client_cert_path) {
        buffer_free(config->authentication.client_cert_path);
        config->authentication.client_cert_path = NULL;
    }
    if (config->authentication.client_key_path) {
        buffer_free(config->authentication.client_key_path);
        config->authentication.client_key_path = NULL;
    }
    if (config->authentication.ca_data) {
        buffer_free(config->authentication.ca_data);
        config->authentication.ca_data = NULL;
    }
    if (config->authentication.client_cert_data) {
        buffer_free(config->authentication.client_cert_data);
        config->authentication.client_cert_data = NULL;
    }
    if (config->authentication.client_key_data) {
        buffer_free(config->authentication.client_key_data);
        config->authentication.client_key_data = NULL;
    }
    if (config->last_will.topic) {
        buffer_free(config->last_will.topic);
        config->last_will.topic = NULL;
    }
    if (config->last_will.msg) {
        buffer_free(config->last_will.msg);
        config->last_will.msg = NULL;
    }
}

/**
 * @brief Convert persistable MQTT config to runtime ms_mqtt_config_t
 */
static aicam_result_t mqtt_base_config_persistent_to_runtime(
    const mqtt_base_config_t *persistent,
    ms_mqtt_config_t *runtime
)
{
    if (!persistent || !runtime) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Clear runtime config
    memset(runtime, 0, sizeof(ms_mqtt_config_t));
    
    // Base configuration
    runtime->base.protocol_ver = persistent->protocol_ver;
    runtime->base.port = persistent->port;
    runtime->base.clean_session = persistent->clean_session;
    runtime->base.keepalive = persistent->keepalive;
    
    // Allocate and copy hostname
    if (persistent->hostname[0] != '\0') {
        runtime->base.hostname = (char*)buffer_calloc(1, strlen(persistent->hostname) + 1);
        if (!runtime->base.hostname) {
            LOG_CORE_ERROR("Failed to allocate memory for hostname");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->base.hostname, persistent->hostname);
    }
    
    // Allocate and copy client_id
    if (persistent->client_id[0] != '\0') {
        runtime->base.client_id = (char*)buffer_calloc(1, strlen(persistent->client_id) + 1);
        if (!runtime->base.client_id) {
            LOG_CORE_ERROR("Failed to allocate memory for client_id");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->base.client_id, persistent->client_id);
    }
    
    // Authentication
    if (persistent->username[0] != '\0') {
        runtime->authentication.username = (char*)buffer_calloc(1, strlen(persistent->username) + 1);
        if (!runtime->authentication.username) {
            LOG_CORE_ERROR("Failed to allocate memory for username");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->authentication.username, persistent->username);
    }
    
    if (persistent->password[0] != '\0') {
        runtime->authentication.password = (char*)buffer_calloc(1, strlen(persistent->password) + 1);
        if (!runtime->authentication.password) {
            LOG_CORE_ERROR("Failed to allocate memory for password");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->authentication.password, persistent->password);
    }
    
    // SSL/TLS - CA certificate
    if (persistent->ca_cert_path[0] != '\0') {
        runtime->authentication.ca_path = (char*)buffer_calloc(1, strlen(persistent->ca_cert_path) + 1);
        if (!runtime->authentication.ca_path) {
            LOG_CORE_ERROR("Failed to allocate memory for ca_path");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->authentication.ca_path, persistent->ca_cert_path);
    }
    
    if (persistent->ca_cert_len > 0) {
        runtime->authentication.ca_data = (char*)buffer_calloc(1, persistent->ca_cert_len + 1);
        if (!runtime->authentication.ca_data) {
            LOG_CORE_ERROR("Failed to allocate memory for ca_data");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        //get data from file
        void *fd = disk_file_fopen(FS_FLASH, persistent->ca_cert_path, "r");
        if (fd == NULL) {
            LOG_CORE_ERROR("Failed to open ca file: %s", persistent->ca_cert_path);
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        disk_file_fread(FS_FLASH, fd, runtime->authentication.ca_data, persistent->ca_cert_len);
        disk_file_fclose(FS_FLASH, fd);
        runtime->authentication.ca_len = persistent->ca_cert_len;
    }
    
    // SSL/TLS - Client certificate
    if (persistent->client_cert_path[0] != '\0') {
        runtime->authentication.client_cert_path = (char*)buffer_calloc(1, strlen(persistent->client_cert_path) + 1);
        if (!runtime->authentication.client_cert_path) {
            LOG_CORE_ERROR("Failed to allocate memory for client_cert_path");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->authentication.client_cert_path, persistent->client_cert_path);
    }
    
    if (persistent->client_cert_len > 0) {
        runtime->authentication.client_cert_data = (char*)buffer_calloc(1, persistent->client_cert_len + 1);
        if (!runtime->authentication.client_cert_data) {
            LOG_CORE_ERROR("Failed to allocate memory for client_cert_data");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        //get data from file
        void *fd = disk_file_fopen(FS_FLASH, persistent->client_cert_path, "r");
        if (fd == NULL) {
            LOG_CORE_ERROR("Failed to open client cert file: %s", persistent->client_cert_path);
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        disk_file_fread(FS_FLASH, fd, runtime->authentication.client_cert_data, persistent->client_cert_len);
        disk_file_fclose(FS_FLASH, fd);
        runtime->authentication.client_cert_len = persistent->client_cert_len;
    }
    
    // SSL/TLS - Client key
    if (persistent->client_key_path[0] != '\0') {
        runtime->authentication.client_key_path = (char*)buffer_calloc(1, strlen(persistent->client_key_path) + 1);
        if (!runtime->authentication.client_key_path) {
            LOG_CORE_ERROR("Failed to allocate memory for client_key_path");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->authentication.client_key_path, persistent->client_key_path);
    }
    
    if (persistent->client_key_len > 0) {
        runtime->authentication.client_key_data = (char*)buffer_calloc(1, persistent->client_key_len + 1);
        if (!runtime->authentication.client_key_data) {
            LOG_CORE_ERROR("Failed to allocate memory for client_key_data");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        //get data from file
        void *fd = disk_file_fopen(FS_FLASH, persistent->client_key_path, "r");
        if (fd == NULL) {
            LOG_CORE_ERROR("Failed to open client key file: %s", persistent->client_key_path);
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        disk_file_fread(FS_FLASH, fd, runtime->authentication.client_key_data, persistent->client_key_len);
        disk_file_fclose(FS_FLASH, fd);
        runtime->authentication.client_key_len = persistent->client_key_len;
    }
    
    runtime->authentication.is_verify_hostname = persistent->verify_hostname;
    
    // Last Will and Testament
    if (persistent->lwt_topic[0] != '\0') {
        runtime->last_will.topic = (char*)buffer_calloc(1, strlen(persistent->lwt_topic) + 1);
        if (!runtime->last_will.topic) {
            LOG_CORE_ERROR("Failed to allocate memory for lwt_topic");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->last_will.topic, persistent->lwt_topic);
    }
    
    if (persistent->lwt_message[0] != '\0') {
        runtime->last_will.msg = (char*)buffer_calloc(1, strlen(persistent->lwt_message) + 1);
        if (!runtime->last_will.msg) {
            LOG_CORE_ERROR("Failed to allocate memory for lwt_message");
            free_mqtt_config_strings(runtime);
            return AICAM_ERROR_NO_MEMORY;
        }
        strcpy(runtime->last_will.msg, persistent->lwt_message);
    }
    
    runtime->last_will.msg_len = persistent->lwt_msg_len;
    runtime->last_will.qos = persistent->lwt_qos;
    runtime->last_will.retain = persistent->lwt_retain;
    
    // Task parameters
    runtime->task.priority = persistent->task_priority;
    runtime->task.stack_size = persistent->task_stack_size;
    
    // Network parameters
    runtime->network.disable_auto_reconnect = persistent->disable_auto_reconnect;
    runtime->network.outbox_limit = persistent->outbox_limit;
    runtime->network.outbox_resend_interval_ms = persistent->outbox_resend_interval_ms;
    runtime->network.outbox_expired_timeout = persistent->outbox_expired_timeout_ms;
    runtime->network.reconnect_interval_ms = persistent->reconnect_interval_ms;
    runtime->network.timeout_ms = persistent->timeout_ms;
    runtime->network.buffer_size = persistent->buffer_size;
    runtime->network.tx_buf_size = persistent->tx_buf_size;
    runtime->network.rx_buf_size = persistent->rx_buf_size;
    
    LOG_CORE_DEBUG("Converted persistent MQTT config to runtime");
    return AICAM_OK;
}


/**
 * @brief Convert runtime ms_mqtt_config_t to persistable format
 */
static aicam_result_t mqtt_base_config_runtime_to_persistent(
    const ms_mqtt_config_t *runtime,
    mqtt_base_config_t *persistent
)
{
    if (!runtime || !persistent) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Clear persistent config
    memset(persistent, 0, sizeof(mqtt_base_config_t));
    
    // Base configuration
    persistent->protocol_ver = runtime->base.protocol_ver;
    persistent->port = runtime->base.port;
    persistent->clean_session = runtime->base.clean_session;
    persistent->keepalive = runtime->base.keepalive;
    
    // Copy hostname
    if (runtime->base.hostname) {
        strncpy(persistent->hostname, runtime->base.hostname, sizeof(persistent->hostname) - 1);
        persistent->hostname[sizeof(persistent->hostname) - 1] = '\0';
    }
    
    // Copy client_id
    if (runtime->base.client_id) {
        strncpy(persistent->client_id, runtime->base.client_id, sizeof(persistent->client_id) - 1);
        persistent->client_id[sizeof(persistent->client_id) - 1] = '\0';
    }
    
    // Authentication
    if (runtime->authentication.username) {
        strncpy(persistent->username, runtime->authentication.username, sizeof(persistent->username) - 1);
        persistent->username[sizeof(persistent->username) - 1] = '\0';
    }
    
    if (runtime->authentication.password) {
        strncpy(persistent->password, runtime->authentication.password, sizeof(persistent->password) - 1);
        persistent->password[sizeof(persistent->password) - 1] = '\0';
    }
    
    // SSL/TLS - CA certificate
    if (runtime->authentication.ca_path) {
        strncpy(persistent->ca_cert_path, runtime->authentication.ca_path, sizeof(persistent->ca_cert_path) - 1);
        persistent->ca_cert_path[sizeof(persistent->ca_cert_path) - 1] = '\0';
    }
    
    //to avoid memory allocation, use the length and data directly
    persistent->ca_cert_len = runtime->authentication.ca_len;
    persistent->ca_cert_data[0] = '\0';
    
    // SSL/TLS - Client certificate
    if (runtime->authentication.client_cert_path) {
        strncpy(persistent->client_cert_path, runtime->authentication.client_cert_path, sizeof(persistent->client_cert_path) - 1);
        persistent->client_cert_path[sizeof(persistent->client_cert_path) - 1] = '\0';
    }
    
    //to avoid memory allocation, use the length and data directly
    persistent->client_cert_len = runtime->authentication.client_cert_len;
    persistent->client_cert_data[0] = '\0';
    
    // SSL/TLS - Client key
    if (runtime->authentication.client_key_path) {
        strncpy(persistent->client_key_path, runtime->authentication.client_key_path, sizeof(persistent->client_key_path) - 1);
        persistent->client_key_path[sizeof(persistent->client_key_path) - 1] = '\0';
    }
    
    persistent->client_key_len = runtime->authentication.client_key_len;
    persistent->client_key_data[0] = '\0';
    
    persistent->verify_hostname = runtime->authentication.is_verify_hostname;
    
    // Last Will and Testament
    if (runtime->last_will.topic) {
        strncpy(persistent->lwt_topic, runtime->last_will.topic, sizeof(persistent->lwt_topic) - 1);
        persistent->lwt_topic[sizeof(persistent->lwt_topic) - 1] = '\0';
    }
    
    if (runtime->last_will.msg) {
        size_t msg_len = runtime->last_will.msg_len > 0 ? runtime->last_will.msg_len : strlen(runtime->last_will.msg);
        if (msg_len > sizeof(persistent->lwt_message) - 1) {
            msg_len = sizeof(persistent->lwt_message) - 1;
        }
        memcpy(persistent->lwt_message, runtime->last_will.msg, msg_len);
        persistent->lwt_message[msg_len] = '\0';
    }
    
    persistent->lwt_msg_len = runtime->last_will.msg_len;
    persistent->lwt_qos = runtime->last_will.qos;
    persistent->lwt_retain = runtime->last_will.retain;
    
    // Task parameters
    persistent->task_priority = runtime->task.priority;
    persistent->task_stack_size = runtime->task.stack_size;
    
    // Network parameters
    persistent->disable_auto_reconnect = runtime->network.disable_auto_reconnect;
    persistent->outbox_limit = runtime->network.outbox_limit;
    persistent->outbox_resend_interval_ms = runtime->network.outbox_resend_interval_ms;
    persistent->outbox_expired_timeout_ms = runtime->network.outbox_expired_timeout;
    persistent->reconnect_interval_ms = runtime->network.reconnect_interval_ms;
    persistent->timeout_ms = runtime->network.timeout_ms;
    persistent->buffer_size = runtime->network.buffer_size;
    persistent->tx_buf_size = runtime->network.tx_buf_size;
    persistent->rx_buf_size = runtime->network.rx_buf_size;
    
    LOG_CORE_DEBUG("Converted runtime MQTT config to persistent");
    return AICAM_OK;
}


static aicam_result_t mqtt_config_persistent_to_runtime(const mqtt_service_config_t *persistent, mqtt_service_extended_config_t *runtime)
{
    if (!persistent || !runtime) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    //convert mqtt_service_config_t to mqtt_service_extended_config_t
    aicam_result_t result = mqtt_base_config_persistent_to_runtime(&persistent->base_config, &runtime->base_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to convert MQTT service configuration: %d", result);
        return result;
    }

    //copy topic configuration
    strcpy(runtime->data_receive_topic, persistent->data_receive_topic);
    strcpy(runtime->data_report_topic, persistent->data_report_topic);
    strcpy(runtime->status_topic, persistent->status_topic);
    strcpy(runtime->command_topic, persistent->command_topic);

    //copy QoS configuration
    runtime->data_receive_qos = persistent->data_receive_qos;
    runtime->data_report_qos = persistent->data_report_qos;
    runtime->status_qos = persistent->status_qos;
    runtime->command_qos = persistent->command_qos;

    //copy auto subscription configuration
    runtime->auto_subscribe_receive = persistent->auto_subscribe_receive;
    runtime->auto_subscribe_command = persistent->auto_subscribe_command;

    //copy message configuration
    runtime->enable_status_report = persistent->enable_status_report;
    runtime->status_report_interval_ms = persistent->status_report_interval_ms;
    runtime->enable_heartbeat = persistent->enable_heartbeat;
    runtime->heartbeat_interval_ms = persistent->heartbeat_interval_ms;
    runtime->report_content = persistent->report_content;

    //copy telemetry configuration
    runtime->telemetry_enabled = persistent->telemetry_enabled;
    strcpy(runtime->telemetry_topic, persistent->telemetry_topic);
    runtime->telemetry_qos = persistent->telemetry_qos;

    return AICAM_OK;
}

static aicam_result_t mqtt_config_runtime_to_persistent(const mqtt_service_extended_config_t *runtime, mqtt_service_config_t *persistent)
{
    if (!runtime || !persistent) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    //convert mqtt_service_extended_config_t to mqtt_service_config_t
    aicam_result_t result = mqtt_base_config_runtime_to_persistent(&runtime->base_config, &persistent->base_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to convert MQTT service configuration: %d", result);
        return result;
    }

    //copy topic configuration
    strcpy(persistent->data_receive_topic, runtime->data_receive_topic);
    strcpy(persistent->data_report_topic, runtime->data_report_topic);
    strcpy(persistent->status_topic, runtime->status_topic);
    strcpy(persistent->command_topic, runtime->command_topic);

    //copy QoS configuration
    persistent->data_receive_qos = runtime->data_receive_qos;
    persistent->data_report_qos = runtime->data_report_qos;
    persistent->status_qos = runtime->status_qos;
    persistent->command_qos = runtime->command_qos;

    //copy auto subscription configuration
    persistent->auto_subscribe_receive = runtime->auto_subscribe_receive;
    persistent->auto_subscribe_command = runtime->auto_subscribe_command;

    //copy message configuration
    persistent->enable_status_report = runtime->enable_status_report;
    persistent->status_report_interval_ms = runtime->status_report_interval_ms;
    persistent->enable_heartbeat = runtime->enable_heartbeat;
    persistent->heartbeat_interval_ms = runtime->heartbeat_interval_ms;
    persistent->report_content = runtime->report_content;

    //copy telemetry configuration
    persistent->telemetry_enabled = runtime->telemetry_enabled;
    strcpy(persistent->telemetry_topic, runtime->telemetry_topic);
    persistent->telemetry_qos = runtime->telemetry_qos;

    return AICAM_OK;
}

/* ==================== MQTT Service Implementation ==================== */

aicam_result_t mqtt_service_init(void *config)
{
    if (g_mqtt_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing MQTT Service...");
    
    // Initialize context
    memset(&g_mqtt_service, 0, sizeof(mqtt_service_context_t));


    mqtt_service_config_t* mqtt_config = (mqtt_service_config_t*)buffer_calloc(1, sizeof(mqtt_service_config_t));
    if (!mqtt_config) {
        LOG_SVC_ERROR("Failed to allocate memory for MQTT service configuration");
        return AICAM_ERROR_NO_MEMORY;
    }

    aicam_result_t result = json_config_get_mqtt_service_config(mqtt_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get MQTT service configuration: %d", result);
        buffer_free(mqtt_config);
        return result;
    }

    //convert mqtt_service_config_t to mqtt_service_extended_config_t
    result = mqtt_config_persistent_to_runtime(mqtt_config, &g_mqtt_service.config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to convert MQTT service configuration: %d", result);
        buffer_free(mqtt_config);
        return result;
    }

    
    // Initialize statistics
    memset(&g_mqtt_service.stats, 0, sizeof(mqtt_service_stats_t));
    
    // Initialize API type (default to MS)
    g_mqtt_service.api_type = MQTT_API_TYPE_MS;
    g_mqtt_service.mqtt_client.ms_client = NULL;
    g_mqtt_service.mqtt_client.si91x_client = NULL;
    
    // Create event flags for waiting on specific events
    g_mqtt_service.event_flags = osEventFlagsNew(NULL);
    if (!g_mqtt_service.event_flags) {
        LOG_SVC_ERROR("Failed to create MQTT event flags");
        buffer_free(mqtt_config);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    g_mqtt_service.initialized = AICAM_TRUE;
    
    LOG_SVC_INFO("MQTT Service initialized successfully");
    LOG_SVC_INFO("Data receive topic: %s (QoS: %d)", 
                g_mqtt_service.config.data_receive_topic, 
                g_mqtt_service.config.data_receive_qos);
    LOG_SVC_INFO("Data report topic: %s (QoS: %d)", 
                g_mqtt_service.config.data_report_topic, 
                g_mqtt_service.config.data_report_qos);
    LOG_SVC_INFO("Command topic: %s (QoS: %d)", 
                g_mqtt_service.config.command_topic, 
                g_mqtt_service.config.command_qos);
    
    buffer_free(mqtt_config);
    return AICAM_OK;
}

aicam_result_t mqtt_service_start(void)
{
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_mqtt_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting MQTT Service...");
    g_mqtt_service.initialized = AICAM_TRUE;

    aicam_result_t result = AICAM_OK;
    
    // Initialize MQTT client based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        result = mqtt_client_init_ms(&g_mqtt_service.config.base_config);
    } else {
        result = mqtt_client_init_si91x(&g_mqtt_service.config.base_config);
    }
    
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize MQTT client");
        return result;
    }
    
    // Register event handler based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        result = mqtt_client_register_event_ms(mqtt_client_event_handler, NULL);
    } else {
        result = mqtt_client_register_event_si91x(mqtt_client_event_handler, NULL);
    }
    
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to register MQTT event handler: %d", result);
        // Cleanup client
        if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
            mqtt_client_destroy_ms();
        } else {
            mqtt_client_destroy_si91x();
        }
        return result;
    }

    g_mqtt_service.running = AICAM_TRUE;

    // Create MQTT connection task to monitor STA connection and auto-connect
    if(g_mqtt_service.api_type == MQTT_API_TYPE_SI91X) {
        // Use timeout instead of infinite wait to avoid blocking forever
        // when network is not available
        #define STA_READY_TIMEOUT_MS  30000
        aicam_result_t result = service_wait_for_ready(SERVICE_READY_STA, AICAM_TRUE, STA_READY_TIMEOUT_MS);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("STA service not ready within %d ms: %d", STA_READY_TIMEOUT_MS, result);
            return result;
        }
        result = mqtt_service_connect();
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to connect to MQTT broker: %d", result);
            return result;
        }
        return AICAM_OK;
    }

    if (g_mqtt_service.connect_task_handle == NULL) {
        const osThreadAttr_t mqtt_connect_task_attributes = {
            .name = "MQTTConnect",
            .priority = osPriorityNormal,
            .stack_mem = mqtt_connect_task_stack,
            .stack_size = sizeof(mqtt_connect_task_stack),
        };
        g_mqtt_service.connect_task_handle = osThreadNew(mqtt_connect_task, NULL, &mqtt_connect_task_attributes);
        if (g_mqtt_service.connect_task_handle == NULL) {
            LOG_SVC_ERROR("Failed to create MQTT connection task");
            g_mqtt_service.running = AICAM_FALSE;
            return AICAM_ERROR_NO_MEMORY;
        }
        g_mqtt_service.connect_task_running = AICAM_TRUE;
        LOG_SVC_INFO("MQTT connection task created");
    }

    LOG_SVC_INFO("MQTT Service started successfully (connection task will handle auto-connect)");
    
    return AICAM_OK;
}

aicam_result_t mqtt_service_stop(void)
{
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_mqtt_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping MQTT Service...");
    
    // Stop connection task
    if (g_mqtt_service.connect_task_handle != NULL) {
        g_mqtt_service.connect_task_running = AICAM_FALSE;
        osThreadTerminate(g_mqtt_service.connect_task_handle);
        g_mqtt_service.connect_task_handle = NULL;
        LOG_SVC_INFO("MQTT connection task stopped");
    }
    
    // Disconnect and stop based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (g_mqtt_service.mqtt_client.ms_client) {
            mqtt_client_disconnect_ms();
            mqtt_client_stop_ms();
            mqtt_client_destroy_ms();
        }
    } else {
        if (g_mqtt_service.mqtt_client.si91x_client) {
            mqtt_client_disconnect_si91x();
            mqtt_client_stop_si91x();
            mqtt_client_destroy_si91x();
        }
    }
    
    g_mqtt_service.running = AICAM_FALSE;
    
    LOG_SVC_INFO("MQTT Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t mqtt_service_restart(void)
{
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (g_mqtt_service.mqtt_client.ms_client) {
        mqtt_service_stop();
        }
    } else {
        if (g_mqtt_service.mqtt_client.si91x_client) {
            mqtt_service_stop();
        }
    }

    g_mqtt_service.initialized = AICAM_TRUE;
    g_mqtt_service.running = AICAM_FALSE;

    return mqtt_service_start();
    
}

aicam_result_t mqtt_service_deinit(void)
{
    if (!g_mqtt_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_mqtt_service.running) {
        mqtt_service_stop();
    }
    
    LOG_SVC_INFO("Deinitializing MQTT Service...");
    
    // Free allocated string memory
    free_mqtt_config_strings(&g_mqtt_service.config.base_config);
    
    // Delete event flags
    if (g_mqtt_service.event_flags) {
        osEventFlagsDelete(g_mqtt_service.event_flags);
        g_mqtt_service.event_flags = NULL;
    }
    
    // Clear event callbacks
    g_mqtt_service.callback_count = 0;
    
    // Reset context
    memset(&g_mqtt_service, 0, sizeof(mqtt_service_context_t));
    
    LOG_SVC_INFO("MQTT Service deinitialized successfully");
    
    return AICAM_OK;
}

ms_mqtt_client_handle_t mqtt_service_get_client(void)
{
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        return g_mqtt_service.mqtt_client.ms_client;
    } else {
        LOG_SVC_WARN("mqtt_service_get_client() only works with MS API");
        return NULL;
    }
}

service_state_t mqtt_service_get_state(void)
{
    ms_mqtt_state_t state = MQTT_STATE_MAX;
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        state = mqtt_client_get_state_ms();
    } else {
        state = mqtt_client_get_state_si91x();
    }

    switch (state) {
        case MQTT_STATE_CONNECTED:
            return SERVICE_STATE_CONNECTED;
        case MQTT_STATE_DISCONNECTED:
            return SERVICE_STATE_DISCONNECTED;
        case MQTT_STATE_STARTING:
            return SERVICE_STATE_INITIALIZING;
        case MQTT_STATE_STOPPED:
            return SERVICE_STATE_SHUTDOWN;
        case MQTT_STATE_WAIT_RECONNECT:
            return SERVICE_STATE_WAIT_RECONNECT;
        default:
            return SERVICE_STATE_UNINITIALIZED;
    }
    
    return SERVICE_STATE_UNINITIALIZED;
}

aicam_result_t mqtt_service_connect(void)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        LOG_SVC_ERROR("MQTT service is not initialized or running");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Check client initialization based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
        LOG_SVC_ERROR("MQTT client is not initialized");
        mqtt_service_restart();
        if (!g_mqtt_service.mqtt_client.ms_client) {
        LOG_SVC_ERROR("MQTT client is still not initialized");
        return AICAM_ERROR;
        }
        }
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            LOG_SVC_ERROR("MQTT client is not initialized");
            mqtt_service_restart();
            if (!g_mqtt_service.mqtt_client.si91x_client) {
                LOG_SVC_ERROR("MQTT client is still not initialized");
                return AICAM_ERROR;
            }
        }
    }
    
    LOG_SVC_INFO("Connecting to MQTT broker...");
    
    g_mqtt_service.stats.total_connections++;
    
    aicam_result_t result;
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        result = mqtt_client_start_ms();
        if (result != AICAM_OK) {
            int mqtt_result = ms_mqtt_client_get_state(g_mqtt_service.mqtt_client.ms_client);
            if (mqtt_result == MQTT_STATE_DISCONNECTED) {
                LOG_SVC_ERROR("MQTT client is in invalid state, reconnecting...");
                return mqtt_client_reconnect_ms();
            }
        g_mqtt_service.stats.failed_connections++;
        g_mqtt_service.stats.last_error_code = result;
            return result;
        }
    } else {
        result = mqtt_client_start_si91x();
        if (result != AICAM_OK) {
            g_mqtt_service.stats.failed_connections++;
            g_mqtt_service.stats.last_error_code = result;
            return result;
        }
    }
    
    return AICAM_OK;
}

aicam_result_t mqtt_service_disconnect(void)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_SVC_INFO("Disconnecting from MQTT broker...");
    
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
        return AICAM_ERROR;
    }
        return mqtt_client_disconnect_ms();
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            return AICAM_ERROR;
        }
        return mqtt_client_disconnect_si91x();
    }
}

aicam_result_t mqtt_service_reconnect(void)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_SVC_INFO("Reconnecting to MQTT broker...");
    
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
        return AICAM_ERROR;
    }
        return mqtt_client_reconnect_ms();
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            return AICAM_ERROR;
        }
        return mqtt_client_reconnect_si91x();
    }
}

aicam_bool_t mqtt_service_is_connected(void)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return AICAM_FALSE;
    }
    
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
            return AICAM_FALSE;
        }
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            return AICAM_FALSE;
        }
    }
    
    ms_mqtt_state_t state = mqtt_service_get_state();
    return (state == MQTT_STATE_CONNECTED) ? AICAM_TRUE : AICAM_FALSE;
}

/* ==================== Message Publishing ==================== */

int mqtt_service_publish(const char *topic, 
                        const uint8_t *payload, 
                        int payload_len, 
                        int qos, 
                        int retain)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    if (!topic || !payload) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    // Check client based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
            return MQTT_ERR_INVALID_STATE;
        }
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            return MQTT_ERR_INVALID_STATE;
        }
    }
    
    if (!mqtt_service_is_connected()) {
        return MQTT_ERR_CONN;
    }
    
    int result;
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        result = mqtt_client_publish_ms(topic, payload, payload_len, qos, retain);
    } else {
        result = mqtt_client_publish_si91x(topic, payload, payload_len, qos, retain);
    }
    
    if (result < 0) {
        LOG_SVC_ERROR("Failed to publish message: %d", result);
        g_mqtt_service.stats.messages_failed++;
        g_mqtt_service.stats.last_error_code = result;
    }
    
    return result;
}

int mqtt_service_publish_string(const char *topic, 
                            const char *message, 
                            int qos, 
                            int retain)
{
    if (!message) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    return mqtt_service_publish(topic, (const uint8_t*)message, strlen(message), qos, retain);
}

int mqtt_service_publish_json(const char *topic, 
                            const char *json_data, 
                            int qos, 
                            int retain)
{
    if (!json_data) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    return mqtt_service_publish(topic, (const uint8_t*)json_data, strlen(json_data), qos, retain);
}

/**
 * @brief Publish data to configured data report topic
 */
int mqtt_service_publish_data(const uint8_t *data, int data_len)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    if (!data || data_len <= 0) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    return mqtt_service_publish(g_mqtt_service.config.data_report_topic, 
                            data, 
                            data_len, 
                            g_mqtt_service.config.data_report_qos, 
                            0);
}

/**
 * @brief Publish status to configured status topic
 */
int mqtt_service_publish_status(const char *status)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    if (!status) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    return mqtt_service_publish_string(g_mqtt_service.config.status_topic, 
                                    status, 
                                    g_mqtt_service.config.status_qos, 
                                    1);  // Retain status messages
}

/**
 * @brief Publish JSON data to configured data report topic
 */
int mqtt_service_publish_data_json(const char *json_data)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    if (!json_data) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    return mqtt_service_publish_json(g_mqtt_service.config.data_report_topic, 
                                    json_data, 
                                    g_mqtt_service.config.data_report_qos, 
                                    0);
}

/* ==================== Message Subscription ==================== */

int mqtt_service_subscribe(const char *topic_filter, int qos)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    if (!topic_filter) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    // Check client based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
            return MQTT_ERR_INVALID_STATE;
        }
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            return MQTT_ERR_INVALID_STATE;
        }
    }
    
    if (!mqtt_service_is_connected()) {
        return MQTT_ERR_CONN;
    }
    
    int result;
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        result = mqtt_client_subscribe_ms(topic_filter, qos);
    } else {
        result = mqtt_client_subscribe_si91x(topic_filter, qos);
    }
    
    if (result < 0) {
        LOG_SVC_ERROR("Failed to subscribe to topic: %d", result);
        g_mqtt_service.stats.last_error_code = result;
    }
    
    return result;
}

int mqtt_service_unsubscribe(const char *topic_filter)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    if (!topic_filter) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    // Check client based on API type
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        if (!g_mqtt_service.mqtt_client.ms_client) {
            return MQTT_ERR_INVALID_STATE;
        }
    } else {
        if (!g_mqtt_service.mqtt_client.si91x_client) {
            return MQTT_ERR_INVALID_STATE;
        }
    }
    
    if (!mqtt_service_is_connected()) {
        return MQTT_ERR_CONN;
    }
    
    int result;
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        result = mqtt_client_unsubscribe_ms(topic_filter);
    } else {
        result = mqtt_client_unsubscribe_si91x(topic_filter);
    }
    
    if (result < 0) {
        LOG_SVC_ERROR("Failed to unsubscribe from topic: %d", result);
        g_mqtt_service.stats.last_error_code = result;
    }
    
    return result;
}

/* ==================== Configuration Management ==================== */

aicam_result_t mqtt_service_get_config(ms_mqtt_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Copy non-pointer fields
    config->base.protocol_ver = g_mqtt_service.config.base_config.base.protocol_ver;
    config->base.port = g_mqtt_service.config.base_config.base.port;
    config->base.clean_session = g_mqtt_service.config.base_config.base.clean_session;
    config->base.keepalive = g_mqtt_service.config.base_config.base.keepalive;
    
    // Deep copy string fields
    if (g_mqtt_service.config.base_config.base.hostname) {
        config->base.hostname = my_strdup(g_mqtt_service.config.base_config.base.hostname);
        if (!config->base.hostname) {
            LOG_SVC_ERROR("Failed to allocate memory for hostname");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->base.hostname = NULL;
    }
    
    if (g_mqtt_service.config.base_config.base.client_id) {
        config->base.client_id = my_strdup(g_mqtt_service.config.base_config.base.client_id);
        if (!config->base.client_id) {
            LOG_SVC_ERROR("Failed to allocate memory for client_id");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->base.client_id = NULL;
    }
    
    if (g_mqtt_service.config.base_config.authentication.username) {
        config->authentication.username = my_strdup(g_mqtt_service.config.base_config.authentication.username);
        if (!config->authentication.username) {
            LOG_SVC_ERROR("Failed to allocate memory for username");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.username = NULL;
    }
    
    if (g_mqtt_service.config.base_config.authentication.password) {
        config->authentication.password = my_strdup(g_mqtt_service.config.base_config.authentication.password);
        if (!config->authentication.password) {
            LOG_SVC_ERROR("Failed to allocate memory for password");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.password = NULL;
    }
    
    if (g_mqtt_service.config.base_config.authentication.ca_path) {
        config->authentication.ca_path = my_strdup(g_mqtt_service.config.base_config.authentication.ca_path);
        if (!config->authentication.ca_path) {
            LOG_SVC_ERROR("Failed to allocate memory for ca_path");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.ca_path = NULL;
    }
    
    if (g_mqtt_service.config.base_config.authentication.client_cert_path) {
        config->authentication.client_cert_path = my_strdup(g_mqtt_service.config.base_config.authentication.client_cert_path);
        if (!config->authentication.client_cert_path) {
            LOG_SVC_ERROR("Failed to allocate memory for client_cert_path");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.client_cert_path = NULL;
    }
    
    if (g_mqtt_service.config.base_config.authentication.client_key_path) {
        config->authentication.client_key_path = my_strdup(g_mqtt_service.config.base_config.authentication.client_key_path);
        if (!config->authentication.client_key_path) {
            LOG_SVC_ERROR("Failed to allocate memory for client_key_path");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.client_key_path = NULL;
    }
    
    if (g_mqtt_service.config.base_config.last_will.topic) {
        config->last_will.topic = my_strdup(g_mqtt_service.config.base_config.last_will.topic);
        if (!config->last_will.topic) {
            LOG_SVC_ERROR("Failed to allocate memory for last_will.topic");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->last_will.topic = NULL;
    }
    
    if (g_mqtt_service.config.base_config.last_will.msg) {
        config->last_will.msg = my_strdup(g_mqtt_service.config.base_config.last_will.msg);
        if (!config->last_will.msg) {
            LOG_SVC_ERROR("Failed to allocate memory for last_will.msg");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->last_will.msg = NULL;
    }

    if (g_mqtt_service.config.base_config.authentication.ca_data) {
        config->authentication.ca_data = my_strdup(g_mqtt_service.config.base_config.authentication.ca_data);
        if (!config->authentication.ca_data) {
            LOG_SVC_ERROR("Failed to allocate memory for ca_data");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.ca_data = NULL;
    }

    if (g_mqtt_service.config.base_config.authentication.client_cert_data) {
        config->authentication.client_cert_data = my_strdup(g_mqtt_service.config.base_config.authentication.client_cert_data);
        if (!config->authentication.client_cert_data) {
            LOG_SVC_ERROR("Failed to allocate memory for client_cert_data");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.client_cert_data = NULL;
    }

    if (g_mqtt_service.config.base_config.authentication.client_key_data) {
        config->authentication.client_key_data = my_strdup(g_mqtt_service.config.base_config.authentication.client_key_data);
        if (!config->authentication.client_key_data) {
            LOG_SVC_ERROR("Failed to allocate memory for client_key_data");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        config->authentication.client_key_data = NULL;
    }
    
    // Copy other non-pointer fields
    config->authentication.ca_len = g_mqtt_service.config.base_config.authentication.ca_len;
    config->authentication.client_cert_len = g_mqtt_service.config.base_config.authentication.client_cert_len;
    config->authentication.client_key_len = g_mqtt_service.config.base_config.authentication.client_key_len;
    config->authentication.is_verify_hostname = g_mqtt_service.config.base_config.authentication.is_verify_hostname;
    
    config->last_will.msg_len = g_mqtt_service.config.base_config.last_will.msg_len;
    config->last_will.qos = g_mqtt_service.config.base_config.last_will.qos;
    config->last_will.retain = g_mqtt_service.config.base_config.last_will.retain;
    
    config->task.priority = g_mqtt_service.config.base_config.task.priority;
    config->task.stack_size = g_mqtt_service.config.base_config.task.stack_size;
    
    config->network.disable_auto_reconnect = g_mqtt_service.config.base_config.network.disable_auto_reconnect;
    config->network.outbox_limit = g_mqtt_service.config.base_config.network.outbox_limit;
    config->network.outbox_resend_interval_ms = g_mqtt_service.config.base_config.network.outbox_resend_interval_ms;
    config->network.outbox_expired_timeout = g_mqtt_service.config.base_config.network.outbox_expired_timeout;
    config->network.reconnect_interval_ms = g_mqtt_service.config.base_config.network.reconnect_interval_ms;
    config->network.timeout_ms = g_mqtt_service.config.base_config.network.timeout_ms;
    config->network.buffer_size = g_mqtt_service.config.base_config.network.buffer_size;
    
    return AICAM_OK;
}

aicam_result_t mqtt_service_set_config(const ms_mqtt_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Free old string memory first
    free_mqtt_config_strings(&g_mqtt_service.config.base_config);
    
    // Copy non-pointer fields
    g_mqtt_service.config.base_config.base.protocol_ver = config->base.protocol_ver;
    g_mqtt_service.config.base_config.base.port = config->base.port;
    g_mqtt_service.config.base_config.base.clean_session = config->base.clean_session;
    g_mqtt_service.config.base_config.base.keepalive = config->base.keepalive;
    
    // Deep copy string fields
    if (config->base.hostname) {
        g_mqtt_service.config.base_config.base.hostname = my_strdup(config->base.hostname);
        if (!g_mqtt_service.config.base_config.base.hostname) {
            LOG_SVC_ERROR("Failed to allocate memory for hostname");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.base.hostname = NULL;
    }
    
    if (config->base.client_id) {
        g_mqtt_service.config.base_config.base.client_id = my_strdup(config->base.client_id);
        if (!g_mqtt_service.config.base_config.base.client_id) {
            LOG_SVC_ERROR("Failed to allocate memory for client_id");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.base.client_id = NULL;
    }
    
    if (config->authentication.username) {
        g_mqtt_service.config.base_config.authentication.username = my_strdup(config->authentication.username);
        if (!g_mqtt_service.config.base_config.authentication.username) {
            LOG_SVC_ERROR("Failed to allocate memory for username");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.username = NULL;
    }
    
    if (config->authentication.password) {
        g_mqtt_service.config.base_config.authentication.password = my_strdup(config->authentication.password);
        if (!g_mqtt_service.config.base_config.authentication.password) {
            LOG_SVC_ERROR("Failed to allocate memory for password");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.password = NULL;
    }
    
    if (config->authentication.ca_path) {
        g_mqtt_service.config.base_config.authentication.ca_path = my_strdup(config->authentication.ca_path);
        if (!g_mqtt_service.config.base_config.authentication.ca_path) {
            LOG_SVC_ERROR("Failed to allocate memory for ca_path");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.ca_path = NULL;
    }
    
    if (config->authentication.client_cert_path) {
        g_mqtt_service.config.base_config.authentication.client_cert_path = my_strdup(config->authentication.client_cert_path);
        if (!g_mqtt_service.config.base_config.authentication.client_cert_path) {
            LOG_SVC_ERROR("Failed to allocate memory for client_cert_path");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.client_cert_path = NULL;
    }
    
    if (config->authentication.client_key_path) {
        g_mqtt_service.config.base_config.authentication.client_key_path = my_strdup(config->authentication.client_key_path);
        if (!g_mqtt_service.config.base_config.authentication.client_key_path) {
            LOG_SVC_ERROR("Failed to allocate memory for client_key_path");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.client_key_path = NULL;
    }
    
    if (config->authentication.ca_data) {
        g_mqtt_service.config.base_config.authentication.ca_data = my_strdup(config->authentication.ca_data);
        if (!g_mqtt_service.config.base_config.authentication.ca_data) {
            LOG_SVC_ERROR("Failed to allocate memory for ca_data");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.ca_data = NULL;
    }

    if (config->authentication.client_cert_data) {
        g_mqtt_service.config.base_config.authentication.client_cert_data = my_strdup(config->authentication.client_cert_data);
        if (!g_mqtt_service.config.base_config.authentication.client_cert_data) {
            LOG_SVC_ERROR("Failed to allocate memory for client_cert_data");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.client_cert_data = NULL;
    }

    if (config->authentication.client_key_data) {
        g_mqtt_service.config.base_config.authentication.client_key_data = my_strdup(config->authentication.client_key_data);
        if (!g_mqtt_service.config.base_config.authentication.client_key_data) {
            LOG_SVC_ERROR("Failed to allocate memory for client_key_data");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.authentication.client_key_data = NULL;
    }
    
    if (config->last_will.topic) {
        g_mqtt_service.config.base_config.last_will.topic = my_strdup(config->last_will.topic);
        if (!g_mqtt_service.config.base_config.last_will.topic) {
            LOG_SVC_ERROR("Failed to allocate memory for last_will.topic");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.last_will.topic = NULL;
    }
    
    if (config->last_will.msg) {
        g_mqtt_service.config.base_config.last_will.msg = my_strdup(config->last_will.msg);
        if (!g_mqtt_service.config.base_config.last_will.msg) {
            LOG_SVC_ERROR("Failed to allocate memory for last_will.msg");
            return AICAM_ERROR_NO_MEMORY;
        }
    } else {
        g_mqtt_service.config.base_config.last_will.msg = NULL;
    }
    // Copy other non-pointer fields
    g_mqtt_service.config.base_config.authentication.ca_len = config->authentication.ca_len;
    g_mqtt_service.config.base_config.authentication.client_cert_len = config->authentication.client_cert_len;
    g_mqtt_service.config.base_config.authentication.client_key_len = config->authentication.client_key_len;
    g_mqtt_service.config.base_config.authentication.is_verify_hostname = config->authentication.is_verify_hostname;
    
    g_mqtt_service.config.base_config.last_will.msg_len = config->last_will.msg_len;
    g_mqtt_service.config.base_config.last_will.qos = config->last_will.qos;
    g_mqtt_service.config.base_config.last_will.retain = config->last_will.retain;
    
    g_mqtt_service.config.base_config.task.priority = config->task.priority;
    g_mqtt_service.config.base_config.task.stack_size = config->task.stack_size;
    
    g_mqtt_service.config.base_config.network.disable_auto_reconnect = config->network.disable_auto_reconnect;
    g_mqtt_service.config.base_config.network.outbox_limit = config->network.outbox_limit;
    g_mqtt_service.config.base_config.network.outbox_resend_interval_ms = config->network.outbox_resend_interval_ms;
    g_mqtt_service.config.base_config.network.outbox_expired_timeout = config->network.outbox_expired_timeout;
    g_mqtt_service.config.base_config.network.reconnect_interval_ms = config->network.reconnect_interval_ms;
    g_mqtt_service.config.base_config.network.timeout_ms = config->network.timeout_ms;
    g_mqtt_service.config.base_config.network.buffer_size = config->network.buffer_size;


    //cert save to flash
    if (config->authentication.ca_data) {
        if (config->authentication.ca_path) {
            void *fd = disk_file_fopen(FS_FLASH, config->authentication.ca_path, "w");
            if (fd == NULL) {
                LOG_SVC_ERROR("Failed to open ca file: %s", config->authentication.ca_path);
                return AICAM_ERROR;
            }
            disk_file_fwrite(FS_FLASH, fd, config->authentication.ca_data, config->authentication.ca_len);
            disk_file_fclose(FS_FLASH, fd);
        }
    }
    if (config->authentication.client_cert_data) {
        if (config->authentication.client_cert_path) {
            void *fd = disk_file_fopen(FS_FLASH, config->authentication.client_cert_path, "w");
            if (fd == NULL) {
                LOG_SVC_ERROR("Failed to open client cert file: %s", config->authentication.client_cert_path);
                return AICAM_ERROR;
            }
            disk_file_fwrite(FS_FLASH, fd, config->authentication.client_cert_data, config->authentication.client_cert_len);
            disk_file_fclose(FS_FLASH, fd);
        }
    }
    if (config->authentication.client_key_data) {
        if (config->authentication.client_key_path) {
            void *fd = disk_file_fopen(FS_FLASH, config->authentication.client_key_path, "w");
            if (fd == NULL) {
                LOG_SVC_ERROR("Failed to open client key file: %s", config->authentication.client_key_path);
                return AICAM_ERROR;
            }
            disk_file_fwrite(FS_FLASH, fd, config->authentication.client_key_data, config->authentication.client_key_len);
            disk_file_fclose(FS_FLASH, fd);
        }
    }

    //convert mqtt_service_extended_config_t to mqtt_service_config_t
    mqtt_service_config_t* mqtt_config = (mqtt_service_config_t*)buffer_calloc(1, sizeof(mqtt_service_config_t));
    if (mqtt_config == NULL) {
        LOG_SVC_ERROR("Failed to allocate memory for MQTT service configuration");
        return AICAM_ERROR_NO_MEMORY;
    }
    mqtt_config_runtime_to_persistent(&g_mqtt_service.config, mqtt_config);
    json_config_set_mqtt_service_config(mqtt_config);
    buffer_free(mqtt_config);
    LOG_SVC_DEBUG("MQTT service base configuration updated with deep copy");
    
    return AICAM_OK;
}

/**
 * @brief Get MQTT service topic configuration
 */
aicam_result_t mqtt_service_get_topic_config(mqtt_service_topic_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    strncpy(config->data_receive_topic, g_mqtt_service.config.data_receive_topic, sizeof(config->data_receive_topic) - 1);
    config->data_receive_topic[sizeof(config->data_receive_topic) - 1] = '\0';
    
    strncpy(config->data_report_topic, g_mqtt_service.config.data_report_topic, sizeof(config->data_report_topic) - 1);
    config->data_report_topic[sizeof(config->data_report_topic) - 1] = '\0';
    
    strncpy(config->status_topic, g_mqtt_service.config.status_topic, sizeof(config->status_topic) - 1);
    config->status_topic[sizeof(config->status_topic) - 1] = '\0';
    
    strncpy(config->command_topic, g_mqtt_service.config.command_topic, sizeof(config->command_topic) - 1);
    config->command_topic[sizeof(config->command_topic) - 1] = '\0';
    
    config->data_receive_qos = g_mqtt_service.config.data_receive_qos;
    config->data_report_qos = g_mqtt_service.config.data_report_qos;
    config->status_qos = g_mqtt_service.config.status_qos;
    config->command_qos = g_mqtt_service.config.command_qos;
    
    config->auto_subscribe_receive = g_mqtt_service.config.auto_subscribe_receive;
    config->auto_subscribe_command = g_mqtt_service.config.auto_subscribe_command;
    
    config->enable_status_report = g_mqtt_service.config.enable_status_report;
    config->status_report_interval_ms = g_mqtt_service.config.status_report_interval_ms;
    config->enable_heartbeat = g_mqtt_service.config.enable_heartbeat;
    config->heartbeat_interval_ms = g_mqtt_service.config.heartbeat_interval_ms;
    config->report_content = g_mqtt_service.config.report_content;

    config->telemetry_enabled = g_mqtt_service.config.telemetry_enabled;
    strncpy(config->telemetry_topic, g_mqtt_service.config.telemetry_topic, sizeof(config->telemetry_topic) - 1);
    config->telemetry_topic[sizeof(config->telemetry_topic) - 1] = '\0';
    config->telemetry_qos = g_mqtt_service.config.telemetry_qos;


    return AICAM_OK;
}

/**
 * @brief Set MQTT service topic configuration
 */
aicam_result_t mqtt_service_set_topic_config(const mqtt_service_topic_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    strncpy(g_mqtt_service.config.data_receive_topic, config->data_receive_topic, sizeof(g_mqtt_service.config.data_receive_topic) - 1);
    g_mqtt_service.config.data_receive_topic[sizeof(g_mqtt_service.config.data_receive_topic) - 1] = '\0';
    
    strncpy(g_mqtt_service.config.data_report_topic, config->data_report_topic, sizeof(g_mqtt_service.config.data_report_topic) - 1);
    g_mqtt_service.config.data_report_topic[sizeof(g_mqtt_service.config.data_report_topic) - 1] = '\0';
    
    strncpy(g_mqtt_service.config.status_topic, config->status_topic, sizeof(g_mqtt_service.config.status_topic) - 1);
    g_mqtt_service.config.status_topic[sizeof(g_mqtt_service.config.status_topic) - 1] = '\0';
    
    strncpy(g_mqtt_service.config.command_topic, config->command_topic, sizeof(g_mqtt_service.config.command_topic) - 1);
    g_mqtt_service.config.command_topic[sizeof(g_mqtt_service.config.command_topic) - 1] = '\0';
    
    g_mqtt_service.config.data_receive_qos = config->data_receive_qos;
    g_mqtt_service.config.data_report_qos = config->data_report_qos;
    g_mqtt_service.config.status_qos = config->status_qos;
    g_mqtt_service.config.command_qos = config->command_qos;
    
    g_mqtt_service.config.auto_subscribe_receive = config->auto_subscribe_receive;
    g_mqtt_service.config.auto_subscribe_command = config->auto_subscribe_command;
    
    g_mqtt_service.config.enable_status_report = config->enable_status_report;
    g_mqtt_service.config.status_report_interval_ms = config->status_report_interval_ms;
    g_mqtt_service.config.enable_heartbeat = config->enable_heartbeat;
    g_mqtt_service.config.heartbeat_interval_ms = config->heartbeat_interval_ms;
    g_mqtt_service.config.report_content = config->report_content;

    g_mqtt_service.config.telemetry_enabled = config->telemetry_enabled;
    strncpy(g_mqtt_service.config.telemetry_topic, config->telemetry_topic, sizeof(g_mqtt_service.config.telemetry_topic) - 1);
    g_mqtt_service.config.telemetry_topic[sizeof(g_mqtt_service.config.telemetry_topic) - 1] = '\0';
    g_mqtt_service.config.telemetry_qos = config->telemetry_qos;


    LOG_SVC_DEBUG("MQTT service topic configuration updated");

    return AICAM_OK;
}

/**
 * @brief Get the configured data report content mode
 */
mqtt_report_content_t mqtt_service_get_report_content(void)
{
    // Treat uninitialized service or an out-of-range stored value as stock behavior
    if (g_mqtt_service.initialized &&
        g_mqtt_service.config.report_content == MQTT_REPORT_CONTENT_METADATA_ONLY) {
        return MQTT_REPORT_CONTENT_METADATA_ONLY;
    }
    return MQTT_REPORT_CONTENT_FULL;
}

/**
 * @brief Get whether continuous AI telemetry publishing is enabled
 */
aicam_bool_t mqtt_service_get_telemetry_enabled(void)
{
    // Treat an uninitialized service as telemetry off (stock behavior)
    if (g_mqtt_service.initialized && g_mqtt_service.config.telemetry_enabled) {
        return AICAM_TRUE;
    }
    return AICAM_FALSE;
}

/**
 * @brief Publish a continuous AI telemetry message
 * @details Fire-and-forget on the configured telemetry topic/QoS. Fails fast
 *          while disconnected; callers treat failures as dropped beats.
 */
int mqtt_service_publish_telemetry(const char *json_str)
{
    if (!json_str) {
        return MQTT_ERR_INVALID_ARG;
    }

    if (!mqtt_service_get_telemetry_enabled()) {
        return MQTT_ERR_INVALID_STATE;
    }

    if (!mqtt_service_is_connected()) {
        return MQTT_ERR_CONN;
    }

    return mqtt_service_publish_json(g_mqtt_service.config.telemetry_topic, json_str,
                                     g_mqtt_service.config.telemetry_qos, 0);
}

/* ==================== Event Management ==================== */

aicam_result_t mqtt_service_register_event_callback(mqtt_service_event_callback_t callback, 
                                                void *user_data)
{
    if (!callback) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_mqtt_service.callback_count >= MAX_EVENT_CALLBACKS) {
        return AICAM_ERROR_NO_MEMORY;
    }
    
    g_mqtt_service.event_callbacks[g_mqtt_service.callback_count] = callback;
    g_mqtt_service.event_user_data[g_mqtt_service.callback_count] = user_data;
    g_mqtt_service.callback_count++;
    
    return AICAM_OK;
}

aicam_result_t mqtt_service_unregister_event_callback(mqtt_service_event_callback_t callback)
{
    if (!callback) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    for (uint32_t i = 0; i < g_mqtt_service.callback_count; i++) {
        if (g_mqtt_service.event_callbacks[i] == callback) {
            // Move last callback to this position
            if (i < g_mqtt_service.callback_count - 1) {
                g_mqtt_service.event_callbacks[i] = g_mqtt_service.event_callbacks[g_mqtt_service.callback_count - 1];
                g_mqtt_service.event_user_data[i] = g_mqtt_service.event_user_data[g_mqtt_service.callback_count - 1];
            }
            g_mqtt_service.callback_count--;
            return AICAM_OK;
        }
    }
    
    return AICAM_ERROR_NOT_FOUND;
}

/* ==================== Event Wait API ==================== */

/**
 * @brief Wait for specific MQTT event(s)
 * @param event_id Event ID to wait for (can be combined with OR for multiple events)
 * @param wait_all If true, wait for all specified events; if false, wait for any one
 * @param timeout_ms Timeout in milliseconds (osWaitForever for infinite wait)
 * @return AICAM_OK if event received, AICAM_ERROR_TIMEOUT if timeout, AICAM_ERROR otherwise
 */
aicam_result_t mqtt_service_wait_for_event(ms_mqtt_event_id_t event_id, aicam_bool_t wait_all, uint32_t timeout_ms)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.event_flags) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if(g_mqtt_service.config.data_report_qos == 0){
        //if qos is 0, just wait for timeout
        osDelay(2000);
        return AICAM_OK;
    }
    
    // Convert event_id(s) to flags
    uint32_t flags = 0;
    if (event_id == MQTT_EVENT_ANY) {
        // Wait for any event
        flags = 0xFFFFFFFF;  // All flags
    } else {
        // Handle single event or combined events
        // For now, we only support single event_id
        flags = event_id_to_flag(event_id);
    }
    
    if (flags == 0) {
        LOG_SVC_ERROR("Invalid event_id: %d", event_id);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_DEBUG("Waiting for MQTT event: event_id=%d, flags=0x%08X, wait_all=%d, timeout=%u ms", 
                event_id, flags, wait_all, timeout_ms);
    
    uint32_t option = wait_all ? osFlagsWaitAll : osFlagsWaitAny;
    uint32_t result = osEventFlagsWait(g_mqtt_service.event_flags, flags, option, timeout_ms);
    
    if (result & osFlagsError) {
        if (result == (uint32_t)osFlagsErrorTimeout) {
            LOG_SVC_WARN("Timeout waiting for MQTT event: event_id=%d", event_id);
            return AICAM_ERROR_TIMEOUT;
        } else {
            LOG_SVC_ERROR("Error waiting for MQTT event: event_id=%d, error=0x%08X", event_id, result);
            return AICAM_ERROR;
        }
    }
    
    // Clear the flag after waiting
    osEventFlagsClear(g_mqtt_service.event_flags, flags);
    
    LOG_SVC_DEBUG("MQTT event received: event_id=%d, result=0x%08X", event_id, result);
    return AICAM_OK;
}

/**
 * @brief Clear event flag for specific event
 * @param event_id Event ID to clear
 * @return aicam_result_t Operation result
 */
aicam_result_t mqtt_service_clear_event_flag(ms_mqtt_event_id_t event_id)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.event_flags) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t flag = event_id_to_flag(event_id);
    if (flag == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osEventFlagsClear(g_mqtt_service.event_flags, flag);
    return AICAM_OK;
}

/**
 * @brief Check if event flag is set (non-blocking)
 * @param event_id Event ID to check
 * @return AICAM_TRUE if set, AICAM_FALSE otherwise
 */
aicam_bool_t mqtt_service_is_event_set(ms_mqtt_event_id_t event_id)
{
    if (!g_mqtt_service.initialized || !g_mqtt_service.event_flags) {
        return AICAM_FALSE;
    }
    
    uint32_t flag = event_id_to_flag(event_id);
    if (flag == 0) {
        return AICAM_FALSE;
    }
    
    uint32_t current_flags = osEventFlagsGet(g_mqtt_service.event_flags);
    return ((current_flags & flag) != 0) ? AICAM_TRUE : AICAM_FALSE;
}

/* ==================== Statistics and Monitoring ==================== */

aicam_result_t mqtt_service_get_stats(mqtt_service_stats_t *stats)
{
    if (!stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    *stats = g_mqtt_service.stats;
    
    // Update current outbox size
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        stats->outbox_size = mqtt_client_get_outbox_size_ms();
    } else {
        stats->outbox_size = mqtt_client_get_outbox_size_si91x();
    }
    
    return AICAM_OK;
}

aicam_result_t mqtt_service_reset_stats(void)
{
    if (!g_mqtt_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memset(&g_mqtt_service.stats, 0, sizeof(mqtt_service_stats_t));
    
    LOG_SVC_DEBUG("MQTT service statistics reset");
    
    return AICAM_OK;
}

int mqtt_service_get_outbox_size(void)
{
    if (!g_mqtt_service.initialized) {
        return 0;
    }
    
    if (g_mqtt_service.api_type == MQTT_API_TYPE_MS) {
        return mqtt_client_get_outbox_size_ms();
    } else {
        return mqtt_client_get_outbox_size_si91x();
    }
}

const char* mqtt_service_get_version(void)
{
    return MQTT_SERVICE_VERSION;
}

aicam_bool_t mqtt_service_is_running(void)
{
    return g_mqtt_service.running;
}

aicam_bool_t mqtt_service_is_initialized(void)
{
    return g_mqtt_service.initialized;
}

/* ==================== CLI Commands ==================== */

/**
 * @brief CLI command: mqtt status
 */
static int mqtt_status_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    printf("\r\n================== MQTT SERVICE STATUS ==================\r\n");
    printf("Service State: %s\r\n", g_mqtt_service.running ? "Running" : "Stopped");
    printf("Service Version: %s\r\n", mqtt_service_get_version());
    
    printf("API Type: %s\r\n", g_mqtt_service.api_type == MQTT_API_TYPE_MS ? "MS" : "SI91X");
    
    if ((g_mqtt_service.api_type == MQTT_API_TYPE_MS && g_mqtt_service.mqtt_client.ms_client) ||
        (g_mqtt_service.api_type == MQTT_API_TYPE_SI91X && g_mqtt_service.mqtt_client.si91x_client)) {
        ms_mqtt_state_t state = mqtt_service_get_state();
        const char* state_str = "Unknown";
        switch (state) {
            case MQTT_STATE_STOPPED: state_str = "Stopped"; break;
            case MQTT_STATE_STARTING: state_str = "Starting"; break;
            case MQTT_STATE_DISCONNECTED: state_str = "Disconnected"; break;
            case MQTT_STATE_CONNECTED: state_str = "Connected"; break;
            case MQTT_STATE_WAIT_RECONNECT: state_str = "Wait Reconnect"; break;
            case MQTT_STATE_MAX: state_str = "Max"; break;
            default: state_str = "Unknown"; break;
        }
        printf("Client State: %s\r\n", state_str);
        printf("Connected: %s\r\n", mqtt_service_is_connected() ? "Yes" : "No");
        printf("Outbox Size: %d\r\n", mqtt_service_get_outbox_size());
    } else {
        printf("Client: Not initialized\r\n");
    }
    
    // Show configuration
    printf("\r\n--- Configuration ---\r\n");
    printf("Host: %s:%d\r\n", g_mqtt_service.config.base_config.base.hostname, 
        g_mqtt_service.config.base_config.base.port);
    printf("Client ID: %s\r\n", g_mqtt_service.config.base_config.base.client_id);
    printf("Username: %s\r\n", g_mqtt_service.config.base_config.authentication.username ? g_mqtt_service.config.base_config.authentication.username : "None");
    printf("Password: %s\r\n", g_mqtt_service.config.base_config.authentication.password ? g_mqtt_service.config.base_config.authentication.password : "None");
    printf("CA Cert Path: %s\r\n", g_mqtt_service.config.base_config.authentication.ca_path ? g_mqtt_service.config.base_config.authentication.ca_path : "None");
    printf("Client Cert Path: %s\r\n", g_mqtt_service.config.base_config.authentication.client_cert_path ? g_mqtt_service.config.base_config.authentication.client_cert_path : "None");
    printf("Client Key Path: %s\r\n", g_mqtt_service.config.base_config.authentication.client_key_path ? g_mqtt_service.config.base_config.authentication.client_key_path : "None");
    printf("CA Data: %s\r\n", g_mqtt_service.config.base_config.authentication.ca_data ? g_mqtt_service.config.base_config.authentication.ca_data : "None");
    printf("Client Cert Data: %s\r\n", g_mqtt_service.config.base_config.authentication.client_cert_data ? g_mqtt_service.config.base_config.authentication.client_cert_data : "None");
    printf("Client Key Data: %s\r\n", g_mqtt_service.config.base_config.authentication.client_key_data ? g_mqtt_service.config.base_config.authentication.client_key_data : "None");
    printf("SNI: %s\r\n", g_mqtt_service.config.base_config.authentication.is_verify_hostname ? "Yes" : "No");
    printf("Keepalive: %d seconds\r\n", g_mqtt_service.config.base_config.base.keepalive);
    
    printf("\r\n--- Topics ---\r\n");
    printf("Data Receive: %s (QoS: %d)\r\n", g_mqtt_service.config.data_receive_topic, 
        g_mqtt_service.config.data_receive_qos);
    printf("Data Report: %s (QoS: %d)\r\n", g_mqtt_service.config.data_report_topic, 
        g_mqtt_service.config.data_report_qos);
    printf("Status: %s (QoS: %d)\r\n", g_mqtt_service.config.status_topic, 
        g_mqtt_service.config.status_qos);
    printf("Command: %s (QoS: %d)\r\n", g_mqtt_service.config.command_topic, 
        g_mqtt_service.config.command_qos);
    
    printf("\r\n--- Auto Subscription ---\r\n");
    printf("Receive Topic: %s\r\n", g_mqtt_service.config.auto_subscribe_receive ? "Enabled" : "Disabled");
    printf("Command Topic: %s\r\n", g_mqtt_service.config.auto_subscribe_command ? "Enabled" : "Disabled");
    printf("Receive Subscribed: %s\r\n", g_mqtt_service.receive_topic_subscribed ? "Yes" : "No");
    printf("Command Subscribed: %s\r\n", g_mqtt_service.command_topic_subscribed ? "Yes" : "No");
    
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: mqtt stats
 */
static int mqtt_stats_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    mqtt_service_stats_t stats;
    aicam_result_t result = mqtt_service_get_stats(&stats);
    if (result != AICAM_OK) {
        printf("Failed to get MQTT statistics: %d\r\n", result);
        return -1;
    }
    
    printf("\r\n================== MQTT STATISTICS ==================\r\n");
    printf("Total Connections: %d\r\n", (int)stats.total_connections);
    printf("Successful Connections: %d\r\n", (int)stats.successful_connections);
    printf("Failed Connections: %d\r\n", (int)stats.failed_connections);
    printf("Disconnections: %d\r\n", (int)stats.disconnections);
    printf("Current Connections: %d\r\n", (int)stats.current_connections);
    printf("Messages Published: %d\r\n", (int)stats.messages_published);
    printf("Messages Received: %d\r\n", (int)stats.messages_received);
    printf("Messages Failed: %d\r\n", (int)stats.messages_failed);
    printf("Subscriptions: %d\r\n", (int)stats.subscriptions);
    printf("Unsubscriptions: %d\r\n", (int)stats.unsubscriptions);
    printf("Outbox Size: %d\r\n", (int)stats.outbox_size);
    printf("Last Error Code: 0x%08X\r\n", (unsigned int)stats.last_error_code);
    printf("=======================================================\r\n\r\n");
    
    return 0;
}

/**
 * @brief CLI command: mqtt connect
 */
static int mqtt_connect_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    if (mqtt_service_is_connected()) {
        printf("MQTT client already connected\r\n");
        return 0;
    }
    
    printf("Connecting to MQTT broker...\r\n");
    
    aicam_result_t result = mqtt_service_connect();
    if (result != AICAM_OK) {
        printf("Failed to connect to MQTT broker: %d\r\n", result);
        return -1;
    }
    
    printf("MQTT connection initiated successfully\r\n");
    return 0;
}

/**
 * @brief CLI command: mqtt disconnect
 */
static int mqtt_disconnect_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    if (!mqtt_service_is_connected()) {
        printf("MQTT client not connected\r\n");
        return 0;
    }
    
    printf("Disconnecting from MQTT broker...\r\n");
    
    aicam_result_t result = mqtt_service_disconnect();
    if (result != AICAM_OK) {
        printf("Failed to disconnect from MQTT broker: %d\r\n", result);
        return -1;
    }
    
    printf("MQTT disconnection initiated successfully\r\n");
    return 0;
}

/**
 * @brief CLI command: mqtt reconnect
 */
static int mqtt_reconnect_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    printf("Reconnecting to MQTT broker...\r\n");
    
    aicam_result_t result = mqtt_service_reconnect();
    if (result != AICAM_OK) {
        printf("Failed to reconnect to MQTT broker: %d\r\n", result);
        return -1;
    }
    
    printf("MQTT reconnection initiated successfully\r\n");
    return 0;
}

/**
 * @brief CLI command: mqtt publish
 */
static int mqtt_publish_cmd(int argc, char* argv[])
{
    if (argc < 4) {
        printf("Usage: mq publish <topic> <message> [qos] [retain]\r\n");
        printf("  topic   - MQTT topic to publish to\r\n");
        printf("  message - Message content to publish\r\n");
        printf("  qos     - Quality of Service (0, 1, or 2, default: 0)\r\n");
        printf("  retain  - Retain flag (0 or 1, default: 0)\r\n");
        printf("Example: mq publish \"test/topic\" \"Hello World\" 1 0\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    if (!mqtt_service_is_connected()) {
        printf("MQTT client not connected\r\n");
        return -1;
    }
    
    const char* topic = argv[2];
    const char* message = argv[3];
    int qos = 0;
    int retain = 0;
    
    if (argc >= 5) {
        qos = atoi(argv[4]);
        if (qos < 0 || qos > 2) {
            printf("Invalid QoS value. Must be 0, 1, or 2\r\n");
            return -1;
        }
    }
    
    if (argc >= 6) {
        retain = atoi(argv[5]);
        if (retain < 0 || retain > 1) {
            printf("Invalid retain value. Must be 0 or 1\r\n");
            return -1;
        }
    }
    
    printf("Publishing message to topic '%s'...\r\n", topic);
    printf("Message: %s\r\n", message);
    printf("QoS: %d, Retain: %d\r\n", qos, retain);
    
    int result = mqtt_service_publish_string(topic, message, qos, retain);
    if (result < 0) {
        printf("Failed to publish message: %d\r\n", result);
        return -1;
    }
    
    printf("Message published successfully (msg_id: %d)\r\n", result);
    return 0;
}

/**
 * @brief CLI command: mqtt subscribe
 */
static int mqtt_subscribe_cmd(int argc, char* argv[])
{
    if (argc < 4) {
        printf("Usage: mq subscribe <topic> <qos>\r\n");
        printf("  topic - MQTT topic to subscribe to\r\n");
        printf("  qos   - Quality of Service (0, 1, or 2)\r\n");
        printf("Example: mq subscribe \"test/topic\" 1\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    if (!mqtt_service_is_connected()) {
        printf("MQTT client not connected\r\n");
        return -1;
    }
    
    const char* topic = argv[2];
    int qos = atoi(argv[3]);
    
    if (qos < 0 || qos > 2) {
        printf("Invalid QoS value. Must be 0, 1, or 2\r\n");
        return -1;
    }
    
    printf("Subscribing to topic '%s' with QoS %d...\r\n", topic, qos);
    
    int result = mqtt_service_subscribe(topic, qos);
    if (result < 0) {
        printf("Failed to subscribe to topic: %d\r\n", result);
        return -1;
    }
    
    printf("Subscribed to topic successfully (msg_id: %d)\r\n", result);
    return 0;
}

/**
 * @brief CLI command: mqtt unsubscribe
 */
static int mqtt_unsubscribe_cmd(int argc, char* argv[])
{
    if (argc < 3) {
        printf("Usage: mq unsubscribe <topic>\r\n");
        printf("  topic - MQTT topic to unsubscribe from\r\n");
        printf("Example: mq unsubscribe \"test/topic\"\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    if (!mqtt_service_is_connected()) {
        printf("MQTT client not connected\r\n");
        return -1;
    }
    
    const char* topic = argv[2];
    
    printf("Unsubscribing from topic '%s'...\r\n", topic);
    
    int result = mqtt_service_unsubscribe(topic);
    if (result < 0) {
        printf("Failed to unsubscribe from topic: %d\r\n", result);
        return -1;
    }
    
    printf("Unsubscribed from topic successfully (msg_id: %d)\r\n", result);
    return 0;
}

/**
 * @brief CLI command: mqtt test
 */
static int mqtt_test_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    if (!g_mqtt_service.running) {
        printf("MQTT service not running\r\n");
        return -1;
    }
    
    if (!mqtt_service_is_connected()) {
        printf("MQTT client not connected\r\n");
        return -1;
    }
    
    printf("Running MQTT test sequence...\r\n");
    
    // Test 1: Publish to data report topic
    printf("\r\n1. Testing data report topic...\r\n");
    int result = mqtt_service_publish_data_json("{\"test\": \"data_report\", \"timestamp\": 1234567890}");
    if (result >= 0) {
        printf("   Data report published successfully (msg_id: %d)\r\n", result);
    } else {
        printf("   Failed to publish data report: %d\r\n", result);
    }
    
    // Test 2: Publish status
    printf("\r\n2. Testing status topic...\r\n");
    result = mqtt_service_publish_status("online");
    if (result >= 0) {
        printf("   Status published successfully (msg_id: %d)\r\n", result);
    } else {
        printf("   Failed to publish status: %d\r\n", result);
    }
    
    // Test 3: Subscribe to test topic
    printf("\r\n3. Testing subscription...\r\n");
    result = mqtt_service_subscribe("test/mqtt/cli", 1);
    if (result >= 0) {
        printf("   Subscribed to test topic successfully (msg_id: %d)\r\n", result);
    } else {
        printf("   Failed to subscribe to test topic: %d\r\n", result);
    }
    
    // Test 4: Publish to test topic
    printf("\r\n4. Testing test topic publish...\r\n");
    result = mqtt_service_publish_string("test/mqtt/cli", "CLI test message", 1, 0);
    if (result >= 0) {
        printf("   Test message published successfully (msg_id: %d)\r\n", result);
    } else {
        printf("   Failed to publish test message: %d\r\n", result);
    }
    
    // Test 5: Unsubscribe from test topic
    printf("\r\n5. Testing unsubscription...\r\n");
    result = mqtt_service_unsubscribe("test/mqtt/cli");
    if (result >= 0) {
        printf("   Unsubscribed from test topic successfully (msg_id: %d)\r\n", result);
    } else {
        printf("   Failed to unsubscribe from test topic: %d\r\n", result);
    }
    
    printf("\r\nMQTT test sequence completed\r\n");
    return 0;
}

/**
 * @brief CLI command: mqtt reset
 */
static int mqtt_reset_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    if (!g_mqtt_service.initialized) {
        printf("MQTT service not initialized\r\n");
        return -1;
    }
    
    printf("Resetting MQTT service statistics...\r\n");
    
    aicam_result_t result = mqtt_service_reset_stats();
    if (result != AICAM_OK) {
        printf("Failed to reset statistics: %d\r\n", result);
        return -1;
    }
    
    printf("MQTT service statistics reset successfully\r\n");
    return 0;
}

/**
 * @brief Main CLI command handler
 */
static int mqtt_cmd(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: mq <command> [args]\r\n");
        printf("Commands:\r\n");
        printf("  status     - Show MQTT service status and configuration\r\n");
        printf("  stats      - Show MQTT service statistics\r\n");
        printf("  connect    - Connect to MQTT broker\r\n");
        printf("  disconnect - Disconnect from MQTT broker\r\n");
        printf("  reconnect  - Reconnect to MQTT broker\r\n");
        printf("  publish    - Publish message to topic\r\n");
        printf("  subscribe  - Subscribe to topic\r\n");
        printf("  unsubscribe- Unsubscribe from topic\r\n");
        printf("  test       - Run MQTT test sequence\r\n");
        printf("  reset      - Reset MQTT service statistics\r\n");
        printf("\r\nExamples:\r\n");
        printf("  mq status\r\n");
        printf("  mq connect\r\n");
        printf("  mq publish \"test/topic\" \"Hello World\" 1 0\r\n");
        printf("  mq subscribe \"test/topic\" 1\r\n");
        printf("  mq test\r\n");
        return -1;
    }
    
    if (strcmp(argv[1], "status") == 0) {
        return mqtt_status_cmd(argc, argv);
    } else if (strcmp(argv[1], "stats") == 0) {
        return mqtt_stats_cmd(argc, argv);
    } else if (strcmp(argv[1], "connect") == 0) {
        return mqtt_connect_cmd(argc, argv);
    } else if (strcmp(argv[1], "disconnect") == 0) {
        return mqtt_disconnect_cmd(argc, argv);
    } else if (strcmp(argv[1], "reconnect") == 0) {
        return mqtt_reconnect_cmd(argc, argv);
    } else if (strcmp(argv[1], "publish") == 0) {
        return mqtt_publish_cmd(argc, argv);
    } else if (strcmp(argv[1], "subscribe") == 0) {
        return mqtt_subscribe_cmd(argc, argv);
    } else if (strcmp(argv[1], "unsubscribe") == 0) {
        return mqtt_unsubscribe_cmd(argc, argv);
    } else if (strcmp(argv[1], "test") == 0) {
        return mqtt_test_cmd(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        return mqtt_reset_cmd(argc, argv);
    } else {
        printf("Unknown command: %s\r\n", argv[1]);
        return -1;
    }
}

/* ==================== Image Upload with AI Results ==================== */

/**
 * @brief Base64 encoding table
 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Encode data to Base64
 * @param input Input data buffer
 * @param input_len Input data length
 * @param output Output Base64 string buffer
 * @param output_len Output buffer size
 * @return Encoded string length or -1 on error
 */
static int base64_encode(const uint8_t *input, uint32_t input_len, char *output, uint32_t output_len)
{
    if (!input || !output || input_len == 0) {
        return -1;
    }
    
    // Calculate required output size: (input_len + 2) / 3 * 4 + 1 (null terminator)
    uint32_t required_len = ((input_len + 2) / 3) * 4 + 1;
    if (output_len < required_len) {
        return -1;
    }
    
    uint32_t i = 0, j = 0;
    uint8_t a, b, c;
    
    while (i < input_len) {
        a = input[i++];
        b = (i < input_len) ? input[i++] : 0;
        c = (i < input_len) ? input[i++] : 0;
        
        output[j++] = base64_table[(a >> 2) & 0x3F];
        output[j++] = base64_table[((a << 4) & 0x30) | ((b >> 4) & 0x0F)];
        output[j++] = (i > input_len + 1) ? '=' : base64_table[((b << 2) & 0x3C) | ((c >> 6) & 0x03)];
        output[j++] = (i > input_len) ? '=' : base64_table[c & 0x3F];
    }
    
    output[j] = '\0';
    return j;
}

/**
 * @brief Encode image data to Base64 with Data URL format (optional)
 * @param input Input image data
 * @param input_len Input data length
 * @param output Output buffer
 * @param output_len Output buffer size
 * @param image_format Image format (for MIME type)
 * @param use_data_url Whether to use Data URL format
 * @return Encoded string length or -1 on error
 */
static int base64_encode_image(const uint8_t *input, uint32_t input_len,
                            char *output, uint32_t output_len,
                            mqtt_image_format_t image_format,
                            aicam_bool_t use_data_url)
{
    if (!input || !output || input_len == 0)
    {
        return -1;
    }

    uint32_t prefix_len = 0;
    char *data_start = output;

    // Add Data URL prefix if requested
    if (use_data_url)
    {
        const char *mime_type = "application/octet-stream";
        switch (image_format)
        {
        case MQTT_IMAGE_FORMAT_JPEG:
            mime_type = "image/jpeg";
            break;
        case MQTT_IMAGE_FORMAT_PNG:
            mime_type = "image/png";
            break;
        case MQTT_IMAGE_FORMAT_BMP:
            mime_type = "image/bmp";
            break;
        case MQTT_IMAGE_FORMAT_RAW:
            mime_type = "image/raw";
            break;
        }

        prefix_len = snprintf(output, output_len, "data:%s;base64,", mime_type);
        if (prefix_len >= output_len)
        {
            return -1;
        }
        data_start = output + prefix_len;
        output_len -= prefix_len;
    }

    // Calculate required output size for Base64
    uint32_t required_len = ((input_len + 2) / 3) * 4 + 1;
    if (output_len < required_len)
    {
        return -1;
    }

    // Base64 encode
    uint32_t i = 0, j = 0;
    uint8_t a, b, c;

    while (i < input_len)
    {
        a = input[i++];
        b = (i < input_len) ? input[i++] : 0;
        c = (i < input_len) ? input[i++] : 0;

        data_start[j++] = base64_table[(a >> 2) & 0x3F];
        data_start[j++] = base64_table[((a << 4) & 0x30) | ((b >> 4) & 0x0F)];
        data_start[j++] = (i > input_len + 1) ? '=' : base64_table[((b << 2) & 0x3C) | ((c >> 6) & 0x03)];
        data_start[j++] = (i > input_len) ? '=' : base64_table[c & 0x3F];
    }

    data_start[j] = '\0';
    return prefix_len + j;
}

/**
 * @brief Generate unique image ID based on timestamp
 */
aicam_result_t mqtt_service_generate_image_id(char *image_id, const char *prefix)
{
    if (!image_id) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    uint64_t timestamp = rtc_get_timeStamp();
    
    if (prefix && strlen(prefix) > 0) {
        snprintf(image_id, 64, "%s_%lu", prefix, (unsigned long)timestamp);
    } else {
        snprintf(image_id, 64, "img_%lu", (unsigned long)timestamp);
    }
    
    return AICAM_OK;
}

/**
 * @brief Initialize mqtt_ai_result_t structure
 */
aicam_result_t mqtt_service_init_ai_result(mqtt_ai_result_t *mqtt_result,
                                        const nn_result_t *nn_result,
                                        const char *model_name,
                                        const char *model_version,
                                        uint32_t inference_time_ms)
{
    if (!mqtt_result) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memset(mqtt_result, 0, sizeof(mqtt_ai_result_t));
    
    // Copy model info
    if (model_name) {
        strncpy(mqtt_result->model_name, model_name, sizeof(mqtt_result->model_name) - 1);
    } else {
        strcpy(mqtt_result->model_name, "unknown");
    }
    
    if (model_version) {
        strncpy(mqtt_result->model_version, model_version, sizeof(mqtt_result->model_version) - 1);
    } else {
        strcpy(mqtt_result->model_version, "1.0");
    }
    
    mqtt_result->inference_time_ms = inference_time_ms;
    
    // Copy AI result if provided
    if (nn_result) {
        memcpy(&mqtt_result->ai_result, nn_result, sizeof(nn_result_t));
    }
    
    // Get thresholds from nn module
    float conf_threshold = 0.0f, nms_threshold = 0.0f;
    nn_get_confidence_threshold(&conf_threshold);
    nn_get_nms_threshold(&nms_threshold);
    mqtt_result->confidence_threshold = conf_threshold;
    mqtt_result->nms_threshold = nms_threshold;
    
    return AICAM_OK;
}

/**
 * @brief Create JSON for image metadata
 */
static cJSON *create_metadata_json(const mqtt_image_metadata_t *metadata)
{
    if (!metadata) return NULL;
    
    cJSON *meta = cJSON_CreateObject();
    if (!meta) return NULL;
    
    cJSON_AddStringToObject(meta, "image_id", metadata->image_id);
    cJSON_AddNumberToObject(meta, "timestamp", metadata->timestamp);
    
    const char *format_str = "unknown";
    switch (metadata->format) {
        case MQTT_IMAGE_FORMAT_JPEG: format_str = "jpeg"; break;
        case MQTT_IMAGE_FORMAT_PNG: format_str = "png"; break;
        case MQTT_IMAGE_FORMAT_BMP: format_str = "bmp"; break;
        case MQTT_IMAGE_FORMAT_RAW: format_str = "raw"; break;
    }
    cJSON_AddStringToObject(meta, "format", format_str);
    cJSON_AddNumberToObject(meta, "width", metadata->width);
    cJSON_AddNumberToObject(meta, "height", metadata->height);
    cJSON_AddNumberToObject(meta, "size", metadata->size);
    cJSON_AddNumberToObject(meta, "quality", metadata->quality);

    const char *trigger_str = "unknown";
    switch (metadata->trigger_type) {
        case AICAM_CAPTURE_TRIGGER_RTC:      trigger_str = "rtc";      break;
        case AICAM_CAPTURE_TRIGGER_PIR:      trigger_str = "pir";      break;
        case AICAM_CAPTURE_TRIGGER_WEB:      trigger_str = "web";      break;
        case AICAM_CAPTURE_TRIGGER_REMOTE:   trigger_str = "remote";   break;
        case AICAM_CAPTURE_TRIGGER_GPIO:     trigger_str = "gpio";     break;
        case AICAM_CAPTURE_TRIGGER_BUTTON:   trigger_str = "button";   break;
        case AICAM_CAPTURE_TRIGGER_SCHEDULE: trigger_str = "schedule"; break;
        default:                             trigger_str = "unknown";  break;
    }
    cJSON_AddStringToObject(meta, "trigger_type", trigger_str);
    
    return meta;
}

/**
 * @brief Create JSON for AI result
 */
static cJSON *create_ai_result_json(const mqtt_ai_result_t *ai_result)
{
    if (!ai_result) return NULL;
    
    cJSON *ai = cJSON_CreateObject();
    if (!ai) return NULL;
    
    // Add model metadata
    cJSON_AddStringToObject(ai, "model_name", ai_result->model_name);
    cJSON_AddStringToObject(ai, "model_version", ai_result->model_version);
    cJSON_AddNumberToObject(ai, "inference_time_ms", ai_result->inference_time_ms);
    cJSON_AddNumberToObject(ai, "confidence_threshold", ai_result->confidence_threshold);
    cJSON_AddNumberToObject(ai, "nms_threshold", ai_result->nms_threshold);
    
    // Add AI result
    cJSON *ai_result_json = nn_create_ai_result_json(&ai_result->ai_result);
    if (ai_result_json) {
        cJSON_AddItemToObject(ai, "ai_result", ai_result_json);
    }
    
    return ai;
}


static cJSON *create_device_info_json(const device_info_config_t *device_info)
{
    if (!device_info) return NULL;
    
    cJSON *device = cJSON_CreateObject();
    if (!device) return NULL;
    
    cJSON_AddStringToObject(device, "device_name", device_info->device_name);
    cJSON_AddStringToObject(device, "mac_address", device_info->mac_address);
    cJSON_AddStringToObject(device, "serial_number", device_info->serial_number);
    cJSON_AddStringToObject(device, "hardware_version", device_info->hardware_version);
    cJSON_AddStringToObject(device, "software_version", device_info->software_version);
    cJSON_AddStringToObject(device, "power_supply_type", device_info->power_supply_type);
    cJSON_AddNumberToObject(device, "battery_percent", device_info->battery_percent);
    cJSON_AddStringToObject(device, "communication_type", device_info->communication_type);
    return device;
}
/**
 * @brief Upload image with AI results (JSON + Base64 format)
 */
int mqtt_service_publish_image_with_ai(const char *topic,
                                    const uint8_t *image_data,
                                    uint32_t image_size,
                                    const mqtt_image_metadata_t *metadata,
                                    const mqtt_ai_result_t *ai_result)
{

    if (!image_data || image_size == 0 || !metadata) {
        LOG_SVC_ERROR("Invalid arguments");
        return MQTT_ERR_INVALID_ARG;
    }

    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        LOG_SVC_ERROR("MQTT service not initialized or running");
        return MQTT_ERR_INVALID_STATE;
    }
    
    // Calculate Base64 encoded size
    uint32_t base64_len = ((image_size + 2) / 3) * 4 + 1;
    uint32_t prefix_len = strlen("data:image/jpeg;base64,"); // TODO: use metadata->format
    
    
    // Allocate buffer for Base64 encoding
    char *base64_buffer = (char *)buffer_calloc(1, base64_len + prefix_len);
    if (!base64_buffer) {
        LOG_SVC_ERROR("Failed to allocate Base64 buffer (%u bytes)", base64_len);
        return MQTT_ERR_MEM;
    }
    
    // Encode image to Base64
    int encoded_len = base64_encode_image(image_data, image_size, base64_buffer, base64_len + prefix_len, metadata->format, AICAM_TRUE);
    if (encoded_len < 0) {
        LOG_SVC_ERROR("Base64 encoding failed");
        buffer_free(base64_buffer);
        return MQTT_ERR_INVALID_ARG;
    }
    
    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        LOG_SVC_ERROR("Failed to create JSON object");
        buffer_free(base64_buffer);
        return MQTT_ERR_MEM;
    }
    
    // Add metadata
    cJSON *meta_json = create_metadata_json(metadata);
    if (meta_json) {
        cJSON_AddItemToObject(root, "metadata", meta_json);
    }

    //Add device info
    device_info_config_t* device_info = (device_info_config_t*)buffer_calloc(1, sizeof(device_info_config_t));
    aicam_result_t ret = device_service_get_info(device_info);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get device information: %d", ret);
        buffer_free(device_info);
        return MQTT_ERR_INVALID_ARG;
    }
    cJSON *device_json = create_device_info_json(device_info);
    if (device_json) {
        cJSON_AddItemToObject(root, "device_info", device_json);
    }
    buffer_free(device_info);
    
    // Add AI result if provided
    if (ai_result && ai_result->ai_result.is_valid) {
        cJSON *ai_json = create_ai_result_json(ai_result);
        if (ai_json) {
            cJSON_AddItemToObject(root, "ai_result", ai_json);
        }
    }
    else
    {
        cJSON_AddItemToObject(root, "ai_result", cJSON_CreateNull());
    }
    
    // Add Base64 encoded image
    cJSON_AddStringToObject(root, "image_data", base64_buffer);
    cJSON_AddStringToObject(root, "encoding", "base64");
    
    // Convert to JSON string
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    buffer_free(base64_buffer);
    
    if (!json_str) {
        LOG_SVC_ERROR("Failed to generate JSON string");
        return MQTT_ERR_MEM;
    }
    
    
    // Use default topic if not specified
    const char *publish_topic = topic ? topic : g_mqtt_service.config.data_report_topic;
    LOG_SVC_INFO("Publish topic: %s", publish_topic);
    
    LOG_SVC_INFO("Publishing image with AI result (size: %u, base64: %d, json: %u)",
                image_size, encoded_len, (uint32_t)strlen(json_str));

    
    // Publish
    int result = mqtt_service_publish_json(publish_topic, json_str, g_mqtt_service.config.data_report_qos, 0);
    
    buffer_free(json_str);
    
    return result;
}

/**
 * @brief Upload image metadata and AI results only (no image data)
 */
int mqtt_service_publish_ai_result(const char *topic,
                                const mqtt_image_metadata_t *metadata,
                                const mqtt_ai_result_t *ai_result,
                                int qos)
{
    if (!metadata) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    // Create JSON object
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        LOG_SVC_ERROR("Failed to create JSON object");
        return MQTT_ERR_MEM;
    }
    
    // Add metadata
    cJSON *meta_json = create_metadata_json(metadata);
    if (meta_json) {
        cJSON_AddItemToObject(root, "metadata", meta_json);
    }
    
    //Add device info
    device_info_config_t *device_info = (device_info_config_t *)buffer_calloc(1, sizeof(device_info_config_t));
    if (device_info) {
        if (device_service_get_info(device_info) == AICAM_OK) {
            cJSON *device_json = create_device_info_json(device_info);
            if (device_json) {
                cJSON_AddItemToObject(root, "device_info", device_json);
            }
        } else {
            LOG_SVC_WARN("Failed to get device information, reporting without it");
        }
        buffer_free(device_info);
    }

    // Add AI result if provided (explicit null on absence or serialization
    // failure keeps the payload shape stable for consumers)
    cJSON *ai_json = NULL;
    if (ai_result && ai_result->ai_result.is_valid) {
        ai_json = create_ai_result_json(ai_result);
    }
    if (ai_json) {
        cJSON_AddItemToObject(root, "ai_result", ai_json);
    }
    else
    {
        cJSON_AddItemToObject(root, "ai_result", cJSON_CreateNull());
    }
    
    // Convert to JSON string
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        LOG_SVC_ERROR("Failed to generate JSON string");
        return MQTT_ERR_MEM;
    }
    
    // Use default QoS if not specified
    if (qos < 0) {
        qos = g_mqtt_service.config.data_report_qos;
    }
    
    // Use default topic if not specified
    const char *publish_topic = topic ? topic : g_mqtt_service.config.data_report_topic;
    
    // Get detection count based on result type
    uint32_t detection_count = 0;
    if (ai_result && ai_result->ai_result.is_valid) {
        if (ai_result->ai_result.type == PP_TYPE_OD) {
            detection_count = ai_result->ai_result.od.nb_detect;
        } else if (ai_result->ai_result.type == PP_TYPE_MPE) {
            detection_count = ai_result->ai_result.mpe.nb_detect;
        } else if (ai_result->ai_result.type == PP_TYPE_SPE) {
            // SPE is detector-free single instance: one pose when keypoints exist
            detection_count = (ai_result->ai_result.spe.nb_keypoints > 0) ? 1 : 0;
        } else if (ai_result->ai_result.type == PP_TYPE_ISEG) {
            detection_count = ai_result->ai_result.iseg.nb_detect;
        }
    }
    
    LOG_SVC_INFO("Publishing AI result only (detections: %u)", detection_count);
    
    // Publish
    int result = mqtt_service_publish_json(publish_topic, json_str, qos, 0);
    
    buffer_free(json_str);
    
    return result;
}

/**
 * @brief Upload image in chunks (for large images)
 */
int mqtt_service_publish_image_chunked(const char *topic,
                                    const uint8_t *image_data,
                                    uint32_t image_size,
                                    const mqtt_image_metadata_t *metadata,
                                    const mqtt_ai_result_t *ai_result,
                                    uint32_t chunk_size)
{
    if (!image_data || image_size == 0 || !metadata || chunk_size == 0) {
        return MQTT_ERR_INVALID_ARG;
    }
    
    if (!g_mqtt_service.initialized || !g_mqtt_service.running) {
        return MQTT_ERR_INVALID_STATE;
    }
    
    
    // Use default topic if not specified
    const char *publish_topic = topic ? topic : g_mqtt_service.config.data_report_topic;
    
    // Calculate total chunks
    uint32_t total_chunks = (image_size + chunk_size - 1) / chunk_size;
    
    LOG_SVC_INFO("Publishing chunked image: size=%u, chunk_size=%u, total_chunks=%u",
                image_size, chunk_size, total_chunks);
    
    // Step 1: Send metadata and AI result
    cJSON *header = cJSON_CreateObject();
    if (!header) {
        LOG_SVC_ERROR("Failed to create header JSON");
        return MQTT_ERR_MEM;
    }
    
    cJSON_AddStringToObject(header, "type", "image_chunked_header");
    cJSON_AddStringToObject(header, "image_id", metadata->image_id);
    cJSON_AddNumberToObject(header, "total_size", image_size);
    cJSON_AddNumberToObject(header, "total_chunks", total_chunks);
    cJSON_AddNumberToObject(header, "chunk_size", chunk_size);
    
    // Add metadata
    cJSON *meta_json = create_metadata_json(metadata);
    if (meta_json) {
        cJSON_AddItemToObject(header, "metadata", meta_json);
    }
    
    // Add AI result if provided
    if (ai_result && ai_result->ai_result.is_valid) {
        cJSON *ai_json = create_ai_result_json(ai_result);
        if (ai_json) {
            cJSON_AddItemToObject(header, "ai_result", ai_json);
        }
    }
    else
    {
        cJSON_AddItemToObject(header, "ai_result", cJSON_CreateNull());
    }

    char *header_str = cJSON_PrintUnformatted(header);
    cJSON_Delete(header);
    
    if (!header_str) {
        LOG_SVC_ERROR("Failed to generate header JSON string");
        return MQTT_ERR_MEM;
    }
    
    // Publish header
    int result = mqtt_service_publish_json(publish_topic, header_str, g_mqtt_service.config.data_report_qos, 0);
    buffer_free(header_str);
    
    if (result < 0) {
        LOG_SVC_ERROR("Failed to publish chunked image header: %d", result);
        return result;
    }
    
    // Step 2: Send image chunks
    uint32_t sent_chunks = 0;
    uint32_t offset = 0;
    
    // Allocate buffer for chunk Base64 encoding
    uint32_t base64_chunk_len = ((chunk_size + 2) / 3) * 4 + 1;
    char *base64_chunk = (char *)buffer_calloc(1, base64_chunk_len);
    if (!base64_chunk) {
        LOG_SVC_ERROR("Failed to allocate Base64 chunk buffer");
        return MQTT_ERR_MEM;
    }
    
    for (uint32_t chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++) {
        uint32_t current_chunk_size = (offset + chunk_size > image_size) ? 
                                    (image_size - offset) : chunk_size;
        
        // Encode chunk to Base64
        int encoded_len = base64_encode(image_data + offset, current_chunk_size, 
                                    base64_chunk, base64_chunk_len);
        if (encoded_len < 0) {
            LOG_SVC_ERROR("Failed to encode chunk %u", chunk_idx);
            buffer_free(base64_chunk);
            return MQTT_ERR_INVALID_ARG;
        }
        
        // Create chunk JSON
        cJSON *chunk_json = cJSON_CreateObject();
        if (!chunk_json) {
            buffer_free(base64_chunk);
            return MQTT_ERR_MEM;
        }
        
        cJSON_AddStringToObject(chunk_json, "type", "image_chunk");
        cJSON_AddStringToObject(chunk_json, "image_id", metadata->image_id);
        cJSON_AddNumberToObject(chunk_json, "chunk_index", chunk_idx);
        cJSON_AddNumberToObject(chunk_json, "total_chunks", total_chunks);
        cJSON_AddNumberToObject(chunk_json, "chunk_size", current_chunk_size);
        cJSON_AddStringToObject(chunk_json, "data", base64_chunk);
        cJSON_AddStringToObject(chunk_json, "encoding", "base64");
        
        char *chunk_str = cJSON_PrintUnformatted(chunk_json);
        cJSON_Delete(chunk_json);
        
        if (!chunk_str) {
            LOG_SVC_ERROR("Failed to generate chunk JSON string");
            buffer_free(base64_chunk);
            return MQTT_ERR_MEM;
        }
        
        // Publish chunk
        result = mqtt_service_publish_json(publish_topic, chunk_str, g_mqtt_service.config.data_report_qos, 0);
        buffer_free(chunk_str);
        
        if (result < 0) {
            LOG_SVC_ERROR("Failed to publish chunk %u: %d", chunk_idx, result);
            buffer_free(base64_chunk);
            return result;
        }
        
        sent_chunks++;
        offset += current_chunk_size;
        
        // Small delay to avoid overwhelming the broker
        osDelay(10);
    }
    
    buffer_free(base64_chunk);
    
    LOG_SVC_INFO("Successfully published %u chunks", sent_chunks);
    
    return sent_chunks;
}

static aicam_bool_t mqtt_build_topics(const char *mac_str, mqtt_service_config_t *cfg)
{
    unsigned int m[6];
    aicam_bool_t changed = AICAM_FALSE;
    if (sscanf(mac_str, NETIF_MAC_STR_FMT, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
        return AICAM_FALSE; // MAC format error return

    // generate unified MAC string (no separator)
    char mac_hex[7];
    snprintf(mac_hex, sizeof(mac_hex), "%02X%02X%02X", m[3], m[4], m[5]);

    // simplify condition judgment logic
    if (cfg->data_receive_topic[0] == '\0' ||
        strcmp(cfg->data_receive_topic, "aicam/data/receive") == 0)
    {
        snprintf(cfg->data_receive_topic, sizeof(cfg->data_receive_topic),
                "ne301/%s/down/control", mac_hex);
        changed = AICAM_TRUE;
    }

    if (cfg->data_report_topic[0] == '\0' ||
        strcmp(cfg->data_report_topic, "aicam/data/report") == 0)
    {
        snprintf(cfg->data_report_topic, sizeof(cfg->data_report_topic),
                "ne301/%s/upload/report", mac_hex);
        changed = AICAM_TRUE;
    }

    if (cfg->telemetry_topic[0] == '\0' ||
        strcmp(cfg->telemetry_topic, "aicam/data/telemetry") == 0)
    {
        snprintf(cfg->telemetry_topic, sizeof(cfg->telemetry_topic),
                "ne301/%s/upload/telemetry", mac_hex);
        changed = AICAM_TRUE;
    }

    return changed;
}

/**
 * @brief Update MQTT client ID and topic
 */
void mqtt_service_update_client_id_and_topic(void)
{
    // Update MQTT client ID
    LOG_SVC_INFO("Updating MQTT client ID and topic");
    aicam_bool_t changed = AICAM_FALSE;
    mqtt_service_config_t* mqtt_config = (mqtt_service_config_t*)buffer_calloc(1, sizeof(mqtt_service_config_t));
    if (!mqtt_config) {
        LOG_SVC_ERROR("Failed to allocate memory for MQTT service configuration");
        return;
    }
    aicam_result_t result = json_config_get_mqtt_service_config(mqtt_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get MQTT config: %d", result);
        buffer_free(mqtt_config);
        return;
    }
    if (strcmp(mqtt_config->base_config.client_id, "AICAM-000000") == 0) {
        snprintf(mqtt_config->base_config.client_id, sizeof(mqtt_config->base_config.client_id), "NE301-%06X", (unsigned int)rtc_get_timeStamp());
        changed = AICAM_TRUE;
    }

    //get device mac address
    device_info_config_t* device_info = (device_info_config_t*)buffer_calloc(1, sizeof(device_info_config_t));
    if (!device_info) {
        LOG_SVC_ERROR("Failed to allocate memory for device info configuration");
        buffer_free(mqtt_config);
        return;
    }
    result = json_config_get_device_info_config(device_info);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get device MAC address: %d", result);
        buffer_free(mqtt_config);
        buffer_free(device_info);
        return;
    }
    LOG_SVC_INFO("Device MAC address: %s", device_info->mac_address);

    changed = changed | mqtt_build_topics(device_info->mac_address, mqtt_config);
    if (changed) {
        result = json_config_set_mqtt_service_config(mqtt_config);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to set MQTT config: %d", result);
        }
    }

    if (g_mqtt_service.initialized && changed) {
        aicam_bool_t need_reconnect = g_mqtt_service.running;
        
        free_mqtt_config_strings(&g_mqtt_service.config.base_config);
        
        result = mqtt_config_persistent_to_runtime(mqtt_config, &g_mqtt_service.config);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to sync runtime config: %d", result);
        } else {
            LOG_SVC_INFO("Runtime config synced successfully");
            
            if (need_reconnect) {
                LOG_SVC_INFO("Triggering MQTT service restart for new config");
                mqtt_service_restart();
            }
        }
    }

    buffer_free(mqtt_config);
    buffer_free(device_info);
}

/* ==================== MQTT Control Command Protocol ==================== */

/**
 * @brief Parse MQTT control command from JSON message
 */
aicam_result_t mqtt_service_parse_control_cmd(const char *json_message, size_t json_len, mqtt_control_cmd_t *cmd)
{
    if (!json_message || !cmd) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Parse JSON
    cJSON *json = cJSON_ParseWithLength(json_message, json_len);
    if (!json) {
        LOG_SVC_ERROR("Failed to parse control command JSON");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Initialize command structure
    memset(cmd, 0, sizeof(mqtt_control_cmd_t));
    
    // Extract command type
    cJSON *cmd_item = cJSON_GetObjectItem(json, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        LOG_SVC_ERROR("Missing or invalid 'cmd' field in control command");
        cJSON_Delete(json);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    const char *cmd_str = cmd_item->valuestring;
    if (strcmp(cmd_str, "capture") == 0) {
        cmd->cmd_type = MQTT_CMD_CAPTURE;
    } else if (strcmp(cmd_str, "sleep") == 0) {
        cmd->cmd_type = MQTT_CMD_SLEEP;
    } else if (strcmp(cmd_str, "task_completed") == 0) {
        cmd->cmd_type = MQTT_CMD_TASK_COMPLETED;
    } else {
        LOG_SVC_ERROR("Unknown command: %s", cmd_str);
        cJSON_Delete(json);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Extract request_id (optional)
    cJSON *request_id_item = cJSON_GetObjectItem(json, "request_id");
    if (request_id_item && cJSON_IsString(request_id_item)) {
        strncpy(cmd->request_id, request_id_item->valuestring, sizeof(cmd->request_id) - 1);
        cmd->request_id[sizeof(cmd->request_id) - 1] = '\0';
    }
    
    // Extract command-specific parameters
    cJSON *params_item = cJSON_GetObjectItem(json, "params");
    if (params_item && cJSON_IsObject(params_item)) {
        switch (cmd->cmd_type) {
            case MQTT_CMD_CAPTURE: {
                cJSON *enable_ai_item = cJSON_GetObjectItem(params_item, "enable_ai");
                if (enable_ai_item && cJSON_IsBool(enable_ai_item)) {
                    cmd->params.capture.enable_ai = cJSON_IsTrue(enable_ai_item) ? AICAM_TRUE : AICAM_FALSE;
                } else {
                    cmd->params.capture.enable_ai = AICAM_FALSE; // Default
                }
                
                cJSON *chunk_size_item = cJSON_GetObjectItem(params_item, "chunk_size");
                if (chunk_size_item && cJSON_IsNumber(chunk_size_item)) {
                    cmd->params.capture.chunk_size = chunk_size_item->valueint;
                } else {
                    cmd->params.capture.chunk_size = 0; // Auto
                }
                
                cJSON *store_to_sd_item = cJSON_GetObjectItem(params_item, "store_to_sd");
                if (store_to_sd_item && cJSON_IsBool(store_to_sd_item)) {
                    cmd->params.capture.store_to_sd = cJSON_IsTrue(store_to_sd_item) ? AICAM_TRUE : AICAM_FALSE;
                } else {
                    cmd->params.capture.store_to_sd = AICAM_FALSE; // Default
                }
                break;
            }
            
            case MQTT_CMD_SLEEP: {
                cJSON *duration_item = cJSON_GetObjectItem(params_item, "duration_sec");
                if (duration_item && cJSON_IsNumber(duration_item)) {
                    cmd->params.sleep.duration_sec = duration_item->valueint;
                } else {
                    cmd->params.sleep.duration_sec = 0; // Use timer config
                }
                break;
            }
            
            default:
                // No parameters needed
                break;
        }
    }
    
    cJSON_Delete(json);
    return AICAM_OK;
}

/**
 * @brief Execute MQTT control command
 */
aicam_result_t mqtt_service_execute_control_cmd(const mqtt_control_cmd_t *cmd)
{
    if (!cmd) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    aicam_result_t result = AICAM_OK;
    
    // Execute command
    switch (cmd->cmd_type) {
        case MQTT_CMD_CAPTURE: {
            LOG_SVC_INFO("Executing capture command: enable_ai=%d, chunk_size=%u, store_to_sd=%d",
                        cmd->params.capture.enable_ai,
                        cmd->params.capture.chunk_size,
                        cmd->params.capture.store_to_sd);
            
            result = system_service_capture_and_upload_mqtt(
                cmd->params.capture.enable_ai,
                cmd->params.capture.chunk_size,
                cmd->params.capture.store_to_sd,
                AICAM_CAPTURE_TRIGGER_REMOTE
            );
            
            if (result != AICAM_OK) {
                LOG_SVC_ERROR("Capture command failed: %d", result);
            }
            break;
        }
        
        case MQTT_CMD_SLEEP: {
            LOG_SVC_INFO("Executing sleep command: duration=%u seconds", cmd->params.sleep.duration_sec);

            // Use request_sleep to trigger async sleep via main loop
            // This avoids deadlock when mqtt_service_stop() is called from MQTT callback
            // The sleep will be executed by system_service_execute_pending_sleep() in main loop
            result = system_service_request_sleep(cmd->params.sleep.duration_sec);

            if (result != AICAM_OK) {
                LOG_SVC_ERROR("Sleep command failed: %d", result);
            }
            break;
        }
        
        case MQTT_CMD_TASK_COMPLETED: {
            LOG_SVC_INFO("Executing task_completed command");
            
            result = system_service_task_completed();
            
            if (result != AICAM_OK) {
                LOG_SVC_ERROR("Task completed command failed: %d", result);
            }
            break;
        }
        
        default:
            LOG_SVC_ERROR("Unknown command type: %d", cmd->cmd_type);
            return AICAM_ERROR_INVALID_PARAM;
    }
    
    return result;
}

/**
 * @brief Send MQTT control command response
 */
static aicam_result_t mqtt_control_cmd_send_response(const mqtt_control_cmd_t *cmd, aicam_result_t result, const char *response_topic)
{
    if (!cmd || !response_topic) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Create response JSON
    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Add command type
    const char *cmd_str = "unknown";
    switch (cmd->cmd_type) {
        case MQTT_CMD_CAPTURE: cmd_str = "capture"; break;
        case MQTT_CMD_SLEEP: cmd_str = "sleep"; break;
        case MQTT_CMD_TASK_COMPLETED: cmd_str = "task_completed"; break;
        default: break;
    }
    cJSON_AddStringToObject(response_json, "cmd", cmd_str);
    
    // Add request_id if present
    if (cmd->request_id[0] != '\0') {
        cJSON_AddStringToObject(response_json, "request_id", cmd->request_id);
    }
    
    // Add result
    aicam_bool_t is_success = (result == AICAM_OK);
    cJSON_AddBoolToObject(response_json, "success", is_success ? 1 : 0);
    cJSON_AddNumberToObject(response_json, "code", is_success ? 200 : 500);
    cJSON_AddNumberToObject(response_json, "result_code", result);
    
    // Add message
    const char *message = is_success ? "Command executed successfully" : "Command execution failed";
    cJSON_AddStringToObject(response_json, "message", message);
    
    // Add timestamp
    cJSON_AddNumberToObject(response_json, "timestamp", rtc_get_timeStamp());
    
    // Convert to string
    char *response_str = cJSON_PrintUnformatted(response_json);
    cJSON_Delete(response_json);
    
    if (!response_str) {
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Publish response
    int publish_result = mqtt_service_publish_string(response_topic, response_str, 
                                                    g_mqtt_service.config.data_report_qos, 0);
    
    
    buffer_free(response_str);
    
    if (publish_result < 0) {
        LOG_SVC_ERROR("Failed to publish control command response: %d", publish_result);
        return AICAM_ERROR;
    }
    
    LOG_SVC_DEBUG("Control command response published to %s", response_topic);
    return AICAM_OK;
}

/**
 * @brief Handle MQTT message as control command
 */
static void mqtt_control_cmd_handle_message(ms_mqtt_event_data_t *event_data)
{
    if (!event_data || !event_data->topic || !event_data->data || event_data->data_len == 0) {
        return;
    }
    
    char topic[256];
    if (event_data->topic_len > 0) {
        size_t topic_len = (event_data->topic_len < sizeof(topic) - 1) ? 
                        event_data->topic_len : sizeof(topic) - 1;
        memcpy(topic, event_data->topic, topic_len);
        topic[topic_len] = '\0';
    } else {
        strncpy(topic, (const char*)event_data->topic, sizeof(topic) - 1);
        topic[sizeof(topic) - 1] = '\0';
    }
    
    // Check if this is a control command topic (data_receive_topic or contains "down/control")
    aicam_bool_t is_control_topic = AICAM_FALSE;
    if (strlen(g_mqtt_service.config.data_receive_topic) > 0) {
        if (strcmp(topic, g_mqtt_service.config.data_receive_topic) == 0 ||
            strstr(topic, g_mqtt_service.config.data_receive_topic) != NULL) {
            is_control_topic = AICAM_TRUE;
        }
    }
    
    // Also check for default pattern "down/control" (control command pattern)
    if (!is_control_topic && strstr(topic, "down/control") != NULL) {
        is_control_topic = AICAM_TRUE;
    }
    
    
    if (!is_control_topic) {
        return; // Not a control command topic
    }
    
    LOG_SVC_INFO("Processing MQTT control command from topic: %s", topic);
    
    // Parse control command
    mqtt_control_cmd_t cmd;
    aicam_result_t result = mqtt_service_parse_control_cmd(
        (const char*)event_data->data,
        event_data->data_len,
        &cmd
    );
    
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to parse control command: %d", result);
        return;
    }
    
    // Execute command
    result = mqtt_service_execute_control_cmd(&cmd);
    
    // Determine response topic
    char response_topic[256];
    if (strlen(g_mqtt_service.config.data_report_topic) > 0) {
        strncpy(response_topic, g_mqtt_service.config.data_report_topic, sizeof(response_topic) - 1);
        response_topic[sizeof(response_topic) - 1] = '\0';
    } else {
        // Generate response topic from request topic (replace "down" with "up")
        strncpy(response_topic, topic, sizeof(response_topic) - 1);
        response_topic[sizeof(response_topic) - 1] = '\0';
        char *down_pos = strstr(response_topic, "down");
        if (down_pos) {
            memcpy(down_pos, "up", 2);
        }
    }
    
    // Send response
    mqtt_control_cmd_send_response(&cmd, result, response_topic);
}

/* ==================== CLI Command Registration ==================== */

debug_cmd_reg_t mqtt_cmd_table[] = {
    {"mq", "MQTT service management and testing.", mqtt_cmd},
};

void mqtt_cmd_register(void)
{
    debug_cmdline_register(mqtt_cmd_table, sizeof(mqtt_cmd_table) / sizeof(mqtt_cmd_table[0]));
}

/**
 * @brief MQTT connection task - monitors STA connection and triggers MQTT connect
 * @param argument Task argument (unused)
 */
static void mqtt_connect_task(void *argument)
{
    (void)argument;
    
    LOG_SVC_INFO("MQTT connection task started");
    
    int retry_count = 5;
    while (g_mqtt_service.connect_task_running && g_mqtt_service.running && retry_count > 0) {
        // Wait for STA to be ready (with timeout to allow periodic check)
        aicam_result_t result = service_wait_for_ready(SERVICE_READY_STA, AICAM_TRUE, osWaitForever);
        
        if (result == AICAM_OK) {
            // STA is connected, check if MQTT should connect
            if (strcmp(g_mqtt_service.config.base_config.base.hostname, "mqtt.example.com") != 0) {
                // Valid hostname configured
                if (mqtt_service_is_connected() == AICAM_FALSE) {
                    LOG_SVC_INFO("STA connected, attempting MQTT connection...");
                    aicam_result_t result = mqtt_service_connect();
                    if (result != AICAM_OK) {
                        LOG_SVC_WARN("MQTT connection failed: %d, will retry when STA reconnects", result);
                    } else {
                        LOG_SVC_INFO("MQTT connection successful");
                    }
                }
            }
            
            // Wait longer when connected to avoid busy loop
            osDelay(5000);
            retry_count--;
        } else {
            // STA not ready, wait a bit and check again
            osDelay(500);
        }
    }
    
    LOG_SVC_INFO("MQTT connection task exiting");
    osThreadExit();
}

