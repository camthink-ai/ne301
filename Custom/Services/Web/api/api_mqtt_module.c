/**
 * @file api_mqtt_module.c
 * @brief MQTT API Module Implementation
 * @details MQTT service API module for configuration and testing
 */

#include "api_mqtt_module.h"
#include "mqtt_service.h"
#include "web_api.h"
#include "web_server.h"
#include "system_service.h"
#include "cJSON.h"
#include "buffer_mgr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "debug.h"

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
 * @brief Map a report content mode to its API string form
 */
static const char *report_content_to_str(uint8_t mode)
{
    return mode == MQTT_REPORT_CONTENT_METADATA_ONLY ? "metadata_only" : "full";
}

/**
 * @brief Parse a report content mode from its API string form
 * @return AICAM_OK on a recognized value, error otherwise
 */
static aicam_result_t parse_report_content(const cJSON *item, uint8_t *mode)
{
    if (!cJSON_IsString(item)) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (strcmp(item->valuestring, "full") == 0) {
        *mode = MQTT_REPORT_CONTENT_FULL;
        return AICAM_OK;
    }
    if (strcmp(item->valuestring, "metadata_only") == 0) {
        *mode = MQTT_REPORT_CONTENT_METADATA_ONLY;
        return AICAM_OK;
    }
    return AICAM_ERROR_INVALID_PARAM;
}

/**
 * @brief Safe string comparison, handle NULL pointers
 */
static int safe_strcmp(const char* str1, const char* str2) {
    if (str1 == NULL && str2 == NULL) return 0;
    if (str1 == NULL || str2 == NULL) return 1;
    return strcmp(str1, str2);
}

/**
 * @brief Safe string copy, automatically manage memory
 */
static aicam_result_t safe_strdup(char** dest, const char* src) {
    if (!dest) return AICAM_ERROR_INVALID_PARAM;
    
    // Free old memory
    if (*dest) {
        buffer_free(*dest);
        *dest = NULL;
    }
    
    // If source string is empty, return success
    if (!src || strlen(src) == 0) {
        return AICAM_OK;
    }
    
    // Allocate new memory and copy
    *dest = mqtt_service_strdup(src);
    if (!*dest) {
        return AICAM_ERROR_NO_MEMORY;
    }
    
    return AICAM_OK;
}

/**
 * @brief Safe string assignment (direct pointer assignment, no copy)
 */
static aicam_result_t safe_str_assign(char** dest, const char* src) {
    if (!dest) return AICAM_ERROR_INVALID_PARAM;
    
    // Free old memory (if previously allocated dynamically)
    if (*dest) {
        buffer_free(*dest);
        *dest = NULL;
    }
    
    // Direct pointer assignment (assuming src has a long enough lifetime)
    *dest = (char*)src;
    return AICAM_OK;
}

/**
 * @brief Safe free string memory
 */
static void safe_str_free(char** str) {
    if (str && *str) {
        buffer_free(*str);
        *str = NULL;
    }
}

/**
 * @brief Update string configuration field
 */
static aicam_result_t update_string_config(char **config_field,
                                           const char *new_value,
                                           aicam_bool_t *config_changed,
                                           aicam_bool_t should_dup)
{
    if (!config_field || !config_changed)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Safe string comparison
    if (safe_strcmp(*config_field, new_value) != 0)
    {
        aicam_result_t result;

        if (should_dup)
        {
            result = safe_strdup(config_field, new_value);
        }
        else
        {
            result = safe_str_assign(config_field, new_value);
        }

        if (result != AICAM_OK)
        {
            return result;
        }

        *config_changed = AICAM_TRUE;
    }

    return AICAM_OK;
}

/**
 * @brief Update number configuration field
 */
static aicam_result_t update_number_config(uint16_t *config_field,
                                           uint16_t new_value,
                                           aicam_bool_t *config_changed)
{
    if (!config_field || !config_changed)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (*config_field != new_value)
    {
        *config_field = new_value;
        *config_changed = AICAM_TRUE;
    }

    return AICAM_OK;
}

/* ==================== MQTT API Handlers ==================== */

/**
 * @brief Get MQTT configuration handler
 */
static aicam_result_t mqtt_config_get_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    aicam_result_t result;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Get MQTT configuration
    ms_mqtt_config_t config;
    result = mqtt_service_get_config(&config);
    if (result != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get MQTT configuration");
    }
    
    // Create JSON response
    cJSON *response_json = cJSON_CreateObject();
    if (!response_json) {
        // Free allocated MQTT configuration strings before returning error
        free_mqtt_config_strings(&config);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to create response JSON");
    }
    
    // Add connection settings
    cJSON *connection = cJSON_CreateObject();
    cJSON_AddStringToObject(connection, "hostname", config.base.hostname ? config.base.hostname : "");
    cJSON_AddNumberToObject(connection, "port", config.base.port);
    cJSON_AddStringToObject(connection, "client_id", config.base.client_id ? config.base.client_id : "");
    cJSON_AddStringToObject(connection, "protocol_type", config.authentication.ca_path ? "mqtts" : "mqtt");
    //cJSON_AddNumberToObject(connection, "protocol_version", config.base.protocol_ver);
    //cJSON_AddBoolToObject(connection, "clean_session", config.base.clean_session);
    //cJSON_AddNumberToObject(connection, "keepalive", config.base.keepalive);
    cJSON_AddItemToObject(response_json, "connection", connection);
    
    // Add authentication settings
    cJSON *authentication = cJSON_CreateObject();
    cJSON_AddStringToObject(authentication, "username", config.authentication.username ? config.authentication.username : "");
    cJSON_AddStringToObject(authentication, "password", config.authentication.password ? config.authentication.password : "");
    cJSON_AddStringToObject(authentication, "ca_cert_path", config.authentication.ca_path ? config.authentication.ca_path : "");
    cJSON_AddStringToObject(authentication, "client_cert_path", config.authentication.client_cert_path ? config.authentication.client_cert_path : "");
    cJSON_AddStringToObject(authentication, "client_key_path", config.authentication.client_key_path ? config.authentication.client_key_path : "");
    cJSON_AddStringToObject(authentication, "ca_data", config.authentication.ca_data ? config.authentication.ca_data : "");
    cJSON_AddStringToObject(authentication, "client_cert_data", config.authentication.client_cert_data ? config.authentication.client_cert_data : "");
    cJSON_AddStringToObject(authentication, "client_key_data", config.authentication.client_key_data ? config.authentication.client_key_data : "");
    cJSON_AddBoolToObject(authentication, "sni", config.authentication.is_verify_hostname);
    cJSON_AddItemToObject(response_json, "authentication", authentication);
    
    // Add last will settings, skip this
    // cJSON *last_will = cJSON_CreateObject();
    // cJSON_AddStringToObject(last_will, "topic", config.last_will.topic ? config.last_will.topic : "");
    // cJSON_AddStringToObject(last_will, "message", config.last_will.msg ? config.last_will.msg : "");
    // cJSON_AddNumberToObject(last_will, "qos", config.last_will.qos);
    // cJSON_AddBoolToObject(last_will, "retain", config.last_will.retain);
    // cJSON_AddItemToObject(response_json, "last_will", last_will);
    
    // Add network settings, skip this
    // cJSON *network = cJSON_CreateObject();
    // cJSON_AddBoolToObject(network, "auto_reconnect", !config.network.disable_auto_reconnect);
    // cJSON_AddNumberToObject(network, "reconnect_interval_ms", config.network.reconnect_interval_ms);
    // cJSON_AddNumberToObject(network, "connection_timeout_ms", config.network.timeout_ms);
    // cJSON_AddNumberToObject(network, "buffer_size", config.network.buffer_size);
    // cJSON_AddNumberToObject(network, "outbox_limit", config.network.outbox_limit);
    // cJSON_AddNumberToObject(network, "outbox_resend_interval_ms", config.network.outbox_resend_interval_ms);
    // cJSON_AddNumberToObject(network, "outbox_expired_timeout_ms", config.network.outbox_expired_timeout);
    // cJSON_AddItemToObject(response_json, "network", network);
    
    // Add task settings, skip this
    // cJSON *task = cJSON_CreateObject();
    // cJSON_AddNumberToObject(task, "priority", config.task.priority);
    // cJSON_AddNumberToObject(task, "stack_size", config.task.stack_size);
    // cJSON_AddItemToObject(response_json, "task", task);
    
    // Add topic configuration
    mqtt_service_topic_config_t topic_config;
    if (mqtt_service_get_topic_config(&topic_config) == AICAM_OK) {
        cJSON *topics = cJSON_CreateObject();
        cJSON_AddStringToObject(topics, "data_receive_topic", topic_config.data_receive_topic);
        cJSON_AddStringToObject(topics, "data_report_topic", topic_config.data_report_topic);
        // cJSON_AddStringToObject(topics, "status_topic", topic_config.status_topic);
        // cJSON_AddStringToObject(topics, "command_topic", topic_config.command_topic);
        cJSON_AddItemToObject(response_json, "topics", topics);
        

        //for now, only data_receive_qos and data_report_qos are supported,and they are equal to each other
        cJSON *qos = cJSON_CreateObject();
        cJSON_AddNumberToObject(qos, "data_receive_qos", topic_config.data_receive_qos);    
        cJSON_AddNumberToObject(qos, "data_report_qos", topic_config.data_report_qos);
        // cJSON_AddNumberToObject(qos, "status_qos", topic_config.status_qos);
        // cJSON_AddNumberToObject(qos, "command_qos", topic_config.command_qos);
        cJSON_AddItemToObject(response_json, "qos", qos);

        cJSON_AddStringToObject(response_json, "report_content", report_content_to_str(topic_config.report_content));
        
        //cJSON *auto_subscribe = cJSON_CreateObject();
        //cJSON_AddBoolToObject(auto_subscribe, "auto_subscribe_receive", topic_config.auto_subscribe_receive);
        //cJSON_AddBoolToObject(auto_subscribe, "auto_subscribe_command", topic_config.auto_subscribe_command);
        //cJSON_AddItemToObject(response_json, "auto_subscribe", auto_subscribe);
        
        // cJSON *message_config = cJSON_CreateObject();
        // cJSON_AddBoolToObject(message_config, "enable_status_report", topic_config.enable_status_report);
        // cJSON_AddNumberToObject(message_config, "status_report_interval_ms", topic_config.status_report_interval_ms);
        // cJSON_AddBoolToObject(message_config, "enable_heartbeat", topic_config.enable_heartbeat);
        // cJSON_AddNumberToObject(message_config, "heartbeat_interval_ms", topic_config.heartbeat_interval_ms);
        // cJSON_AddItemToObject(response_json, "message_config", message_config);
    }
    
    // Add service status
    cJSON *status = cJSON_CreateObject();
    cJSON_AddBoolToObject(status, "running", mqtt_service_is_initialized());
    cJSON_AddBoolToObject(status, "connected", mqtt_service_is_connected());
    cJSON_AddNumberToObject(status, "state", mqtt_service_get_state());
    cJSON_AddStringToObject(status, "version", mqtt_service_get_version());
    cJSON_AddItemToObject(response_json, "status", status);
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    // Free allocated MQTT configuration strings
    free_mqtt_config_strings(&config);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    result = api_response_success(ctx, json_string, "MQTT configuration retrieved successfully");
    return result;
}

/**
 * @brief Set MQTT configuration handler
 */
static aicam_result_t mqtt_config_set_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Verify content type
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Parse JSON request
    cJSON *request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    
    // Validate report content mode up front (applied with the topic config below)
    uint8_t report_content_mode = MQTT_REPORT_CONTENT_FULL;
    cJSON *report_content = cJSON_GetObjectItem(request_json, "report_content");
    if (report_content && parse_report_content(report_content, &report_content_mode) != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "report_content must be 'full' or 'metadata_only'");
    }

    // Get current configuration
    ms_mqtt_config_t config;
    aicam_result_t result = mqtt_service_get_config(&config);
    if (result != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get current MQTT configuration");
    }
    
    
    // Track if connection-related config changed
    aicam_bool_t connection_config_changed = AICAM_FALSE;
    
    // Update connection settings
    cJSON *connection = cJSON_GetObjectItem(request_json, "connection");
    if (connection && cJSON_IsObject(connection)) {
        cJSON *hostname = cJSON_GetObjectItem(connection, "hostname");
        if (hostname && cJSON_IsString(hostname)) {
            if (strcmp(config.base.hostname ? config.base.hostname : "", hostname->valuestring) != 0) {
                update_string_config(&config.base.hostname, hostname->valuestring, &connection_config_changed, AICAM_TRUE);
            }
        }
        else {
            LOG_SVC_ERROR("hostname is NULL");
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "hostname is NULL");
            safe_str_free(&config.base.hostname);
        }
        
        cJSON *port = cJSON_GetObjectItem(connection, "port");
        if (port && cJSON_IsNumber(port)) {
            LOG_SVC_INFO("port: %d, old_port: %d", (uint16_t)port->valueint, config.base.port);
            update_number_config(&config.base.port, (uint16_t)port->valueint, &connection_config_changed);
        }
        
        cJSON *client_id = cJSON_GetObjectItem(connection, "client_id");
        if (client_id && cJSON_IsString(client_id)) {
            if (strcmp(config.base.client_id ? config.base.client_id : "", client_id->valuestring) != 0) {
                update_string_config(&config.base.client_id, client_id->valuestring, &connection_config_changed, AICAM_TRUE);
            }
        }
        else {
            safe_str_free(&config.base.client_id);
        }
        
        // Note: protocol_type is read-only, derived from ca_data presence
        // Note: protocol_version, clean_session, keepalive are not exposed in GET handler
    }
    
    // Update authentication settings
    cJSON *authentication = cJSON_GetObjectItem(request_json, "authentication");
    if (authentication && cJSON_IsObject(authentication)) {
        cJSON *username = cJSON_GetObjectItem(authentication, "username");
        if (username && cJSON_IsString(username)) {
            if (strcmp(config.authentication.username ? config.authentication.username : "", username->valuestring) != 0) {
                update_string_config(&config.authentication.username, username->valuestring, &connection_config_changed, AICAM_TRUE);
            }
        }
        else {  
            safe_str_free(&config.authentication.username);
        }

        cJSON *password = cJSON_GetObjectItem(authentication, "password");
        if (password && cJSON_IsString(password)) {
            update_string_config(&config.authentication.password, password->valuestring, &connection_config_changed, AICAM_TRUE);
        }
        else {
            safe_str_free(&config.authentication.password);
        }
        
        cJSON *ca_cert_path = cJSON_GetObjectItem(authentication, "ca_cert_path");
        if (ca_cert_path && cJSON_IsString(ca_cert_path)) {
            if (strcmp(config.authentication.ca_path ? config.authentication.ca_path : "", ca_cert_path->valuestring) != 0) {
                update_string_config(&config.authentication.ca_path, ca_cert_path->valuestring, &connection_config_changed, AICAM_TRUE);
            }
        }
        else {
            safe_str_free(&config.authentication.ca_path);
        }

        cJSON *client_cert_path = cJSON_GetObjectItem(authentication, "client_cert_path");
        if (client_cert_path && cJSON_IsString(client_cert_path)) {
            if (strcmp(config.authentication.client_cert_path ? config.authentication.client_cert_path : "", client_cert_path->valuestring) != 0) {
                update_string_config(&config.authentication.client_cert_path, client_cert_path->valuestring, &connection_config_changed, AICAM_TRUE);
            }
        }
        else {
            safe_str_free(&config.authentication.client_cert_path);
        }

        cJSON *client_key_path = cJSON_GetObjectItem(authentication, "client_key_path");
        if (client_key_path && cJSON_IsString(client_key_path)) {
            if (strcmp(config.authentication.client_key_path ? config.authentication.client_key_path : "", client_key_path->valuestring) != 0) {
               update_string_config(&config.authentication.client_key_path, client_key_path->valuestring, &connection_config_changed, AICAM_TRUE);
            }
        }
        else {
            safe_str_free(&config.authentication.client_key_path);
        }
        
        cJSON *ca_data = cJSON_GetObjectItem(authentication, "ca_data");
        if (ca_data && cJSON_IsString(ca_data)) {
            if (strcmp(config.authentication.ca_data ? config.authentication.ca_data : "", ca_data->valuestring) != 0) {
               update_string_config(&config.authentication.ca_data, ca_data->valuestring, &connection_config_changed, AICAM_TRUE);
               config.authentication.ca_len = strlen(ca_data->valuestring) > 0 ? (strlen(ca_data->valuestring) + 1) : 0;
            }
        }
        else {
            safe_str_free(&config.authentication.ca_data);
            config.authentication.ca_len = 0;
        }
        
        cJSON *client_cert_data = cJSON_GetObjectItem(authentication, "client_cert_data");
        if (client_cert_data && cJSON_IsString(client_cert_data)) {
            if (strcmp(config.authentication.client_cert_data ? config.authentication.client_cert_data : "", client_cert_data->valuestring) != 0) {
                update_string_config(&config.authentication.client_cert_data, client_cert_data->valuestring, &connection_config_changed, AICAM_TRUE);
                config.authentication.client_cert_len = strlen(client_cert_data->valuestring) > 0 ? (strlen(client_cert_data->valuestring) + 1) : 0;
            }
        }
        else {
            safe_str_free(&config.authentication.client_cert_data);
            config.authentication.client_cert_len = 0;
        }
        
        cJSON *client_key_data = cJSON_GetObjectItem(authentication, "client_key_data");
        if (client_key_data && cJSON_IsString(client_key_data)) {
            if (strcmp(config.authentication.client_key_data ? config.authentication.client_key_data : "", client_key_data->valuestring) != 0) {
                update_string_config(&config.authentication.client_key_data, client_key_data->valuestring, &connection_config_changed, AICAM_TRUE);
                config.authentication.client_key_len = strlen(client_key_data->valuestring) > 0 ? (strlen(client_key_data->valuestring) + 1) : 0;
                connection_config_changed = AICAM_TRUE;
            }
        }
        else {
            safe_str_free(&config.authentication.client_key_data);
            config.authentication.client_key_len = 0;
        }
        
        cJSON *sni = cJSON_GetObjectItem(authentication, "sni");
        if (sni && cJSON_IsBool(sni)) {
            if (config.authentication.is_verify_hostname != (cJSON_IsTrue(sni) ? 1 : 0)) {
                config.authentication.is_verify_hostname = cJSON_IsTrue(sni) ? 1 : 0;
                connection_config_changed = AICAM_TRUE;
            }
        }
        else {
            config.authentication.is_verify_hostname = 0;
        }
    }
    
    // Note: last_will, network, and task settings are not exposed in GET handler
    // so they are not configurable via API

    
    // Update topic configuration if provided
    cJSON *topics = cJSON_GetObjectItem(request_json, "topics");
    cJSON *qos = cJSON_GetObjectItem(request_json, "qos");
    
    if (topics || qos || report_content) {
        mqtt_service_topic_config_t topic_config;
        result = mqtt_service_get_topic_config(&topic_config);
        if (result == AICAM_OK) {
            // Update topics (only data_receive_topic and data_report_topic are exposed in GET handler)
            if (topics && cJSON_IsObject(topics)) {
                cJSON *data_receive_topic = cJSON_GetObjectItem(topics, "data_receive_topic");
                if (data_receive_topic && cJSON_IsString(data_receive_topic)) {
                    strncpy(topic_config.data_receive_topic, data_receive_topic->valuestring, sizeof(topic_config.data_receive_topic) - 1);
                    topic_config.data_receive_topic[sizeof(topic_config.data_receive_topic) - 1] = '\0';
                }
                
                cJSON *data_report_topic = cJSON_GetObjectItem(topics, "data_report_topic");
                if (data_report_topic && cJSON_IsString(data_report_topic)) {
                    strncpy(topic_config.data_report_topic, data_report_topic->valuestring, sizeof(topic_config.data_report_topic) - 1);
                    topic_config.data_report_topic[sizeof(topic_config.data_report_topic) - 1] = '\0';
                }
                
                // Note: status_topic and command_topic are not exposed in GET handler
            }
            
            // Update QoS settings (only data_receive_qos and data_report_qos are exposed in GET handler)
            if (qos && cJSON_IsObject(qos)) {
                cJSON *data_receive_qos = cJSON_GetObjectItem(qos, "data_receive_qos");
                if (data_receive_qos && cJSON_IsNumber(data_receive_qos)) {
                    topic_config.data_receive_qos = (int)data_receive_qos->valueint;
                }
                
                cJSON *data_report_qos = cJSON_GetObjectItem(qos, "data_report_qos");
                if (data_report_qos && cJSON_IsNumber(data_report_qos)) {
                    topic_config.data_report_qos = (int)data_report_qos->valueint;
                }
                
                // Note: status_qos and command_qos are not exposed in GET handler
            }
            
            // Note: auto_subscribe and message_config are not exposed in GET handler
            
            // Update report content mode if provided (validated above)
            if (report_content) {
                topic_config.report_content = report_content_mode;
            }

            // Apply topic configuration
            result = mqtt_service_set_topic_config(&topic_config);
            if (result != AICAM_OK) {
                cJSON_Delete(request_json);
                return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set MQTT topic configuration");
            }
        }
    }
    
    result = mqtt_service_set_config(&config);
    if (result != AICAM_OK) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to set MQTT base configuration");
    }
    
    
    // Create response with configuration change information
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "message", "MQTT configuration updated successfully");
    cJSON_AddBoolToObject(response_json, "success", true);
    
    if (connection_config_changed) {
        cJSON_AddBoolToObject(response_json, "connection_config_changed", true);
        cJSON_AddStringToObject(response_json, "action_taken", "disconnected_and_ready_for_reconnect");
        cJSON_AddStringToObject(response_json, "next_step", "call /api/v1/apps/mqtt/connect to reconnect with new config");
    } else {
        cJSON_AddBoolToObject(response_json, "connection_config_changed", false);
        cJSON_AddStringToObject(response_json, "action_taken", "configuration_updated_only");
    }
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    cJSON_Delete(request_json);
    
    // Free allocated MQTT configuration strings
    // Note: old_config and config share the same string pointers, so we only need to free once
    free_mqtt_config_strings(&config);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "MQTT configuration updated successfully");
    return api_result;
}

/**
 * @brief Connect to MQTT broker handler
 */
static aicam_result_t mqtt_connect_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Get current connection status
    aicam_bool_t was_connected = mqtt_service_is_connected();
    
    // If already connected, return current status
    if (was_connected) {
        cJSON *response_json = cJSON_CreateObject();
        cJSON_AddStringToObject(response_json, "message", "MQTT already connected");
        cJSON_AddBoolToObject(response_json, "success", true);
        cJSON_AddBoolToObject(response_json, "connected", true);
        cJSON_AddStringToObject(response_json, "status", "already_connected");
        
        // Get connection statistics
        mqtt_service_stats_t stats;
        if (mqtt_service_get_stats(&stats) == AICAM_OK) {
            cJSON *statistics = cJSON_CreateObject();
            cJSON_AddNumberToObject(statistics, "total_connections", stats.total_connections);
            cJSON_AddNumberToObject(statistics, "successful_connections", stats.successful_connections);
            cJSON_AddNumberToObject(statistics, "failed_connections", stats.failed_connections);
            cJSON_AddNumberToObject(statistics, "messages_published", stats.messages_published);
            cJSON_AddNumberToObject(statistics, "messages_received", stats.messages_received);
            cJSON_AddNumberToObject(statistics, "outbox_size", stats.outbox_size);
            cJSON_AddItemToObject(response_json, "statistics", statistics);
        }
        
    json_string = cJSON_Print(response_json);
        cJSON_Delete(response_json);
        
        if (!json_string) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t result = api_response_success(ctx, json_string, "MQTT already connected");
        return result;
    }
    
    // Attempt to connect
    aicam_result_t result = mqtt_service_restart();
    
    // Create response
    cJSON *response_json = cJSON_CreateObject();
    
    if (result == AICAM_OK) {
        // Wait a bit for connection to establish (simplified)
        int timeout = 50;  // 5 seconds timeout
        aicam_bool_t connected = AICAM_FALSE;
        
        while (timeout > 0 && !connected) {
            connected = mqtt_service_is_connected();
            timeout--;
            osDelay(100);
        }
        
        if (connected) {
            cJSON_AddStringToObject(response_json, "message", "MQTT connection successful");
            cJSON_AddBoolToObject(response_json, "success", true);
            cJSON_AddBoolToObject(response_json, "connected", true);
            cJSON_AddStringToObject(response_json, "status", "connected");
        } else {
            cJSON_AddStringToObject(response_json, "message", "MQTT connection timeout");
            cJSON_AddBoolToObject(response_json, "success", false);
            cJSON_AddBoolToObject(response_json, "connected", false);
            cJSON_AddStringToObject(response_json, "status", "timeout");
        }
    } else {
        cJSON_AddStringToObject(response_json, "message", "MQTT connection failed");
        cJSON_AddBoolToObject(response_json, "success", false);
        cJSON_AddBoolToObject(response_json, "connected", false);
        cJSON_AddStringToObject(response_json, "status", "failed");
        cJSON_AddNumberToObject(response_json, "error_code", result);
    }
    
    // Get connection statistics
    mqtt_service_stats_t stats;
    if (mqtt_service_get_stats(&stats) == AICAM_OK) {
        cJSON *statistics = cJSON_CreateObject();
        cJSON_AddNumberToObject(statistics, "total_connections", stats.total_connections);
        cJSON_AddNumberToObject(statistics, "successful_connections", stats.successful_connections);
        cJSON_AddNumberToObject(statistics, "failed_connections", stats.failed_connections);
        cJSON_AddNumberToObject(statistics, "messages_published", stats.messages_published);
        cJSON_AddNumberToObject(statistics, "messages_received", stats.messages_received);
        cJSON_AddNumberToObject(statistics, "outbox_size", stats.outbox_size);
        cJSON_AddItemToObject(response_json, "statistics", statistics);
    }
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    result = api_response_success(ctx, json_string, "MQTT connection completed");
    return result;
}

/**
 * @brief Disconnect from MQTT broker handler
 */
static aicam_result_t mqtt_disconnect_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Get current connection status
    aicam_bool_t was_connected = mqtt_service_is_connected();
    
    // If not connected, return current status
    if (!was_connected) {
        cJSON *response_json = cJSON_CreateObject();
        cJSON_AddStringToObject(response_json, "message", "MQTT already disconnected");
        cJSON_AddBoolToObject(response_json, "success", true);
        cJSON_AddBoolToObject(response_json, "connected", false);
        cJSON_AddStringToObject(response_json, "status", "already_disconnected");
        
        // Get connection statistics
        mqtt_service_stats_t stats;
        if (mqtt_service_get_stats(&stats) == AICAM_OK) {
            cJSON *statistics = cJSON_CreateObject();
            cJSON_AddNumberToObject(statistics, "total_connections", stats.total_connections);
            cJSON_AddNumberToObject(statistics, "successful_connections", stats.successful_connections);
            cJSON_AddNumberToObject(statistics, "failed_connections", stats.failed_connections);
            cJSON_AddNumberToObject(statistics, "messages_published", stats.messages_published);
            cJSON_AddNumberToObject(statistics, "messages_received", stats.messages_received);
            cJSON_AddNumberToObject(statistics, "outbox_size", stats.outbox_size);
            cJSON_AddItemToObject(response_json, "statistics", statistics);
        }
        
        char *json_string = cJSON_Print(response_json);
        cJSON_Delete(response_json);
        
        if (!json_string) {
            return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
        }
        
        aicam_result_t result = api_response_success(ctx, json_string, "MQTT already disconnected");
        return result;
    }
    
    // Attempt to disconnect
    aicam_result_t result = mqtt_service_disconnect();
    
    // Create response
    cJSON *response_json = cJSON_CreateObject();
    
    if (result == AICAM_OK) {
        // Wait a bit for disconnection to complete (simplified)
        int timeout = 50;  // 5 seconds timeout
        aicam_bool_t connected = AICAM_TRUE;
        
        while (timeout > 0 && connected) {
            connected = mqtt_service_is_connected();
            timeout--;
            osDelay(100);
        }
        
        if (!connected) {
            cJSON_AddStringToObject(response_json, "message", "MQTT disconnection successful");
            cJSON_AddBoolToObject(response_json, "success", true);
            cJSON_AddBoolToObject(response_json, "connected", false);
            cJSON_AddStringToObject(response_json, "status", "disconnected");
        } else {
            cJSON_AddStringToObject(response_json, "message", "MQTT disconnection timeout");
            cJSON_AddBoolToObject(response_json, "success", false);
            cJSON_AddBoolToObject(response_json, "connected", true);
            cJSON_AddStringToObject(response_json, "status", "timeout");
        }
    } else {
        cJSON_AddStringToObject(response_json, "message", "MQTT disconnection failed");
        cJSON_AddBoolToObject(response_json, "success", false);
        cJSON_AddBoolToObject(response_json, "connected", true);
        cJSON_AddStringToObject(response_json, "status", "failed");
        cJSON_AddNumberToObject(response_json, "error_code", result);
    }
    
    // Get connection statistics
    mqtt_service_stats_t stats;
    if (mqtt_service_get_stats(&stats) == AICAM_OK) {
        cJSON *statistics = cJSON_CreateObject();
        cJSON_AddNumberToObject(statistics, "total_connections", stats.total_connections);
        cJSON_AddNumberToObject(statistics, "successful_connections", stats.successful_connections);
        cJSON_AddNumberToObject(statistics, "failed_connections", stats.failed_connections);
        cJSON_AddNumberToObject(statistics, "messages_published", stats.messages_published);
        cJSON_AddNumberToObject(statistics, "messages_received", stats.messages_received);
        cJSON_AddNumberToObject(statistics, "outbox_size", stats.outbox_size);
        cJSON_AddItemToObject(response_json, "statistics", statistics);
    }
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    result = api_response_success(ctx, json_string, "MQTT disconnection completed");
    return result;
}

/**
 * @brief Publish data to configured data report topic handler
 */
static aicam_result_t mqtt_publish_data_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Verify content type
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Parse JSON request
    cJSON *request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    
    // Get data from request
    const char *data_str = web_api_get_string(request_json, "data");
    if (!data_str) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing 'data' field");
    }
    
    // Publish data
    int result = mqtt_service_publish_data((const uint8_t*)data_str, strlen(data_str));
    cJSON_Delete(request_json);
    
    if (result < 0) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to publish data");
    }
    
    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "message", "Data published successfully");
    cJSON_AddNumberToObject(response_json, "message_id", result);
    cJSON_AddBoolToObject(response_json, "success", true);
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Data published successfully");
    hal_mem_free(json_string);
    return api_result;
}

/**
 * @brief Publish status to configured status topic handler
 */
static aicam_result_t mqtt_publish_status_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Verify content type
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Parse JSON request
    cJSON *request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    
    // Get status from request
    const char *status = web_api_get_string(request_json, "status");
    if (!status) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing 'status' field");
    }
    
    // Publish status
    int result = mqtt_service_publish_status(status);
    cJSON_Delete(request_json);
    
    if (result < 0) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to publish status");
    }
    
    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "message", "Status published successfully");
    cJSON_AddNumberToObject(response_json, "message_id", result);
    cJSON_AddBoolToObject(response_json, "success", true);
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "Status published successfully");
    hal_mem_free(json_string);
    return api_result;
}

/**
 * @brief Publish JSON data to configured data report topic handler
 */
static aicam_result_t mqtt_publish_data_json_handler(http_handler_context_t* ctx)
{
    char *json_string = NULL;
    // Verify HTTP method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    
    // Verify content type
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // Check if MQTT service is initialized
    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }
    
    // Parse JSON request
    cJSON *request_json = web_api_parse_body(ctx);
    if (!request_json) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }
    
    // Get JSON data from request
    const char *json_data = web_api_get_string(request_json, "json_data");
    if (!json_data) {
        cJSON_Delete(request_json);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing 'json_data' field");
    }
    
    // Publish JSON data
    int result = mqtt_service_publish_data_json(json_data);
    cJSON_Delete(request_json);
    
    if (result < 0) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to publish JSON data");
    }
    
    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "message", "JSON data published successfully");
    cJSON_AddNumberToObject(response_json, "message_id", result);
    cJSON_AddBoolToObject(response_json, "success", true);
    
    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }
    
    aicam_result_t api_result = api_response_success(ctx, json_string, "JSON data published successfully");
    return api_result;
}

/**
 * @brief Capture image and upload via MQTT handler
 */
static aicam_result_t mqtt_capture_handler(http_handler_context_t* ctx)
{
    /* variable declarations */
    aicam_bool_t enable_ai;
    aicam_bool_t store_to_sd;
    unsigned int chunk_size;
    cJSON *request_json;
    system_capture_request_t req;
    system_capture_response_t resp;
    aicam_result_t result;
    api_business_error_code_t biz_code;
    const char *biz_msg;
    aicam_bool_t success;
    char *json_string;

    enable_ai = AICAM_TRUE;
    store_to_sd = AICAM_FALSE;
    chunk_size = 0;
    request_json = NULL;
    memset(&resp, 0, sizeof(resp));
    result = AICAM_OK;
    biz_code = API_BUSINESS_ERROR_NONE;
    biz_msg = "Capture and MQTT upload completed";
    success = AICAM_FALSE;
    json_string = NULL;

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    if (!mqtt_service_is_initialized()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "MQTT service is not running");
    }

    if (system_service_get_status() != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "System service not ready");
    }

    if (ctx && ctx->request.content_length > 0 && ctx->request.body) {
        request_json = web_api_parse_body(ctx);
        if (!request_json) {
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
        }

        cJSON *enable_ai_item = cJSON_GetObjectItem(request_json, "enable_ai");
        if (enable_ai_item && cJSON_IsBool(enable_ai_item)) {
            enable_ai = cJSON_IsTrue(enable_ai_item) ? AICAM_TRUE : AICAM_FALSE;
        }

        cJSON *chunk_size_item = cJSON_GetObjectItem(request_json, "chunk_size");
        if (chunk_size_item && cJSON_IsNumber(chunk_size_item) && chunk_size_item->valueint > 0) {
            chunk_size = (unsigned int)chunk_size_item->valueint;
        }

        cJSON *store_to_sd_item = cJSON_GetObjectItem(request_json, "store_to_sd");
        if (store_to_sd_item && cJSON_IsBool(store_to_sd_item)) {
            store_to_sd = cJSON_IsTrue(store_to_sd_item) ? AICAM_TRUE : AICAM_FALSE;
        }
    }

    req.enable_ai = enable_ai;
    req.chunk_size = chunk_size;
    req.store_to_sd = store_to_sd;
    req.fast_fail_mqtt = AICAM_TRUE;
    req.trigger_type = AICAM_CAPTURE_TRIGGER_WEB;

    result = system_service_capture_request(&req, &resp);

    if (request_json) {
        cJSON_Delete(request_json);
    }

    // Build unified response (success/failure) using api_response_success
    success = (result == AICAM_OK);

    if (!success) {
        biz_code = API_BUSINESS_ERROR_OPERATION_FAILED;
        biz_msg = "Capture failed";

        if (result == AICAM_ERROR_BUSY) {
            biz_code = API_BUSINESS_ERROR_OPERATION_IN_PROGRESS;
            biz_msg = "Capture in progress";
        } else if (result == AICAM_ERROR_UNAVAILABLE) {
            biz_code = API_BUSINESS_ERROR_MQTT_NOT_CONNECTED;
            biz_msg = "MQTT not connected";
        } else if (result == AICAM_ERROR_TIMEOUT) {
            biz_code = API_BUSINESS_ERROR_OPERATION_TIMEOUT;
            biz_msg = "MQTT wait timeout";
        } else if (result == AICAM_ERROR_NOT_INITIALIZED) {
            biz_code = API_BUSINESS_ERROR_OPERATION_FAILED;
            biz_msg = "System not initialized";
        }
    }

    if (!success) {
        return api_response_error(ctx, biz_code, biz_msg);
    }

    cJSON *response_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(response_json, "success", success);
    cJSON_AddNumberToObject(response_json, "code", biz_code);
    cJSON_AddNumberToObject(response_json, "result", result);
    cJSON_AddStringToObject(response_json, "message", biz_msg);
    cJSON_AddBoolToObject(response_json, "enable_ai", enable_ai);
    cJSON_AddBoolToObject(response_json, "store_to_sd", store_to_sd);
    cJSON_AddNumberToObject(response_json, "chunk_size", chunk_size);
    cJSON_AddNumberToObject(response_json, "duration_ms", resp.duration_ms);

    json_string = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    if (!json_string) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize response");
    }

    return api_response_success(ctx, json_string, biz_msg);
}

/* ==================== API Module Registration ==================== */

/**
 * @brief Register MQTT API module
 */
aicam_result_t web_api_register_mqtt_module(void)
{
    // Define API routes
    api_route_t routes[] = {
        {
            .path = API_PATH_PREFIX"/apps/mqtt/config",
            .method = "GET",
            .handler = mqtt_config_get_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/apps/mqtt/config",
            .method = "POST",
            .handler = mqtt_config_set_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/apps/mqtt/connect",
            .method = "POST",
            .handler = mqtt_connect_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/apps/mqtt/disconnect",
            .method = "POST",
            .handler = mqtt_disconnect_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/apps/mqtt/publish/data",
            .method = "POST",
            .handler = mqtt_publish_data_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/apps/mqtt/publish/status",
            .method = "POST",
            .handler = mqtt_publish_status_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/apps/mqtt/publish/json",
            .method = "POST",
            .handler = mqtt_publish_data_json_handler,
            .require_auth = AICAM_TRUE
        },
        {
            .path = API_PATH_PREFIX"/device/capture",
            .method = "POST",
            .handler = mqtt_capture_handler,
            .require_auth = AICAM_TRUE
        }
    };
    
    // Register routes
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&routes[i]);
        if (result != AICAM_OK) {
            return result;
        }
    }
    
    return AICAM_OK;
}
