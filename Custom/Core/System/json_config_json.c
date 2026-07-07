/**
 * @file json_config_json.c
 * @brief AI Camera JSON Configuration (de)serialization using cJSON
 * @details This file implements the JSON parsing and serialization
 * functions using the cJSON library.
 */

#include "json_config_internal.h"
#include <stdbool.h> // For true/false used by cJSON helpers
#include "buffer_mgr.h"
#include "generic_file.h"

/* ==================== JSON Parsing Helpers (static) ==================== */

// Helper to safely get a string from a cJSON object
static void json_get_string(cJSON *obj, const char *key, char *target, size_t max_len)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item) && (item->valuestring != NULL))
    {
        strncpy(target, item->valuestring, max_len - 1);
        target[max_len - 1] = '\0'; // Ensure null termination
    }
}

// Helper to safely get a uint32 from a cJSON object
static void json_get_uint32(cJSON *obj, const char *key, uint32_t *target)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item))
    {
        *target = (uint32_t)item->valueint;
    }
}

// Helper to safely get a int32 from a cJSON object
static void json_get_int32(cJSON *obj, const char *key, int32_t *target)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item))
    {
        *target = (int32_t)item->valueint;
    }
}

// Helper to safely get a uint8 from a cJSON object
static void json_get_uint8(cJSON *obj, const char *key, uint8_t *target)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item))
    {
        *target = (uint8_t)item->valueint;
    }
}


// Helper to safely get a float from a cJSON object
static void json_get_float(cJSON *obj, const char *key, float *target)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item))
    {
        *target = (float)item->valuedouble;
    }
}

// Helper to safely get a 64-bit uint from a cJSON object
static void json_get_uint64(cJSON *obj, const char *key, uint64_t *target)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item))
    {
        // cJSON uses double for all numbers; large integers might lose precision.
        // For large 64-bit numbers, cJSON_SetNumberValue() should be used.
        // For parsing, this is a limitation unless using cJSON_GetStringValue.
        *target = (uint64_t)item->valuedouble;
    }
    else if (cJSON_IsString(item))
    {
        // Fallback to parse from string if it's stored as one
        *target = strtoull(item->valuestring, NULL, 10);
    }
}

// Helper to safely get a boolean from a cJSON object
static void json_get_bool(cJSON *obj, const char *key, aicam_bool_t *target)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsBool(item))
    {
        *target = cJSON_IsTrue(item) ? AICAM_TRUE : AICAM_FALSE;
    }
}

/* --- Sub-structure Parsers --- */

static void parse_log_config(cJSON *json, log_config_t *cfg)
{
    json_get_uint32(json, "log_level", &cfg->log_level);
    json_get_uint32(json, "log_file_size_kb", &cfg->log_file_size_kb);
    json_get_uint32(json, "log_file_count", &cfg->log_file_count);
}

static void parse_ai_debug(cJSON *json, ai_debug_config_t *cfg)
{
    json_get_bool(json, "ai_enabled", &cfg->ai_enabled);
    json_get_bool(json, "ai_1_active", &cfg->ai_1_active);
    json_get_uint32(json, "confidence_threshold", &cfg->confidence_threshold);
    json_get_uint32(json, "nms_threshold", &cfg->nms_threshold);
    json_get_bool(json, "overlay_results", &cfg->overlay_results);
    json_get_uint32(json, "inference_interval_ms", &cfg->inference_interval_ms);
}

static void parse_power_mode(cJSON *json, power_mode_config_t *cfg)
{
    json_get_uint32(json, "current_mode", &cfg->current_mode);
    json_get_uint32(json, "default_mode", &cfg->default_mode);
    json_get_uint32(json, "low_power_timeout_ms", &cfg->low_power_timeout_ms);
    json_get_uint64(json, "last_activity_time", &cfg->last_activity_time);
    json_get_uint32(json, "mode_switch_count", &cfg->mode_switch_count);
}

static void parse_device_info(cJSON *json, device_info_config_t *cfg)
{
    json_get_string(json, "device_name", cfg->device_name, sizeof(cfg->device_name));
    json_get_string(json, "mac_address", cfg->mac_address, sizeof(cfg->mac_address));
    json_get_string(json, "serial_number", cfg->serial_number, sizeof(cfg->serial_number));
    json_get_string(json, "hardware_version", cfg->hardware_version, sizeof(cfg->hardware_version));
    json_get_string(json, "software_version", cfg->software_version, sizeof(cfg->software_version));
    json_get_string(json, "camera_module", cfg->camera_module, sizeof(cfg->camera_module));
    json_get_string(json, "extension_modules", cfg->extension_modules, sizeof(cfg->extension_modules));
    json_get_string(json, "storage_card_info", cfg->storage_card_info, sizeof(cfg->storage_card_info));
    json_get_float(json, "storage_usage_percent", &cfg->storage_usage_percent);
    json_get_string(json, "power_supply_type", cfg->power_supply_type, sizeof(cfg->power_supply_type));
    json_get_float(json, "battery_percent", &cfg->battery_percent);
    json_get_string(json, "communication_type", cfg->communication_type, sizeof(cfg->communication_type));
}

static void parse_auth_mgr(cJSON *json, auth_mgr_config_t *cfg)
{
    json_get_uint32(json, "session_timeout_ms", &cfg->session_timeout_ms);
    json_get_bool(json, "enable_session_timeout", &cfg->enable_session_timeout);
    json_get_string(json, "admin_password", cfg->admin_password, sizeof(cfg->admin_password));
}

static void parse_isp_config(cJSON *json, isp_config_t *cfg)
{
    if (!json || !cJSON_IsObject(json) || !cfg)
        return;
    json_get_bool(json, "valid", &cfg->valid);
    json_get_bool(json, "stat_removal_enable", &cfg->stat_removal_enable);
    json_get_uint32(json, "stat_removal_head_lines", &cfg->stat_removal_head_lines);
    json_get_uint32(json, "stat_removal_valid_lines", &cfg->stat_removal_valid_lines);
    json_get_bool(json, "demosaic_enable", &cfg->demosaic_enable);
    json_get_uint8(json, "demosaic_type", &cfg->demosaic_type);
    json_get_uint8(json, "demosaic_peak", &cfg->demosaic_peak);
    json_get_uint8(json, "demosaic_line_v", &cfg->demosaic_line_v);
    json_get_uint8(json, "demosaic_line_h", &cfg->demosaic_line_h);
    json_get_uint8(json, "demosaic_edge", &cfg->demosaic_edge);
    json_get_bool(json, "contrast_enable", &cfg->contrast_enable);
    cJSON *lut = cJSON_GetObjectItem(json, "contrast_lut");
    if (cJSON_IsArray(lut) && cJSON_GetArraySize(lut) >= 9) {
        for (int i = 0; i < 9; i++)
            cfg->contrast_lut[i] = (uint32_t)cJSON_GetArrayItem(lut, i)->valueint;
    }
    json_get_uint32(json, "stat_area_x", &cfg->stat_area_x);
    json_get_uint32(json, "stat_area_y", &cfg->stat_area_y);
    json_get_uint32(json, "stat_area_width", &cfg->stat_area_width);
    json_get_uint32(json, "stat_area_height", &cfg->stat_area_height);
    json_get_uint32(json, "sensor_gain", &cfg->sensor_gain);
    json_get_uint32(json, "sensor_exposure", &cfg->sensor_exposure);
    json_get_bool(json, "bad_pixel_algo_enable", &cfg->bad_pixel_algo_enable);
    json_get_uint32(json, "bad_pixel_algo_threshold", &cfg->bad_pixel_algo_threshold);
    json_get_bool(json, "bad_pixel_enable", &cfg->bad_pixel_enable);
    json_get_uint8(json, "bad_pixel_strength", &cfg->bad_pixel_strength);
    json_get_bool(json, "black_level_enable", &cfg->black_level_enable);
    json_get_uint8(json, "black_level_r", &cfg->black_level_r);
    json_get_uint8(json, "black_level_g", &cfg->black_level_g);
    json_get_uint8(json, "black_level_b", &cfg->black_level_b);
    json_get_bool(json, "aec_enable", &cfg->aec_enable);
    json_get_int32(json, "aec_exposure_compensation", &cfg->aec_exposure_compensation);
    json_get_uint32(json, "aec_anti_flicker_freq", &cfg->aec_anti_flicker_freq);
    json_get_bool(json, "awb_enable", &cfg->awb_enable);
    cJSON *labels = cJSON_GetObjectItem(json, "awb_label");
    if (cJSON_IsArray(labels) && cJSON_GetArraySize(labels) >= ISP_AWB_PROFILES_MAX) {
        for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
            cJSON *el = cJSON_GetArrayItem(labels, i);
            if (cJSON_IsString(el) && el->valuestring) {
                strncpy(cfg->awb_label[i], el->valuestring, ISP_AWB_LABEL_MAX_LEN - 1);
                cfg->awb_label[i][ISP_AWB_LABEL_MAX_LEN - 1] = '\0';
            }
        }
    }
    cJSON *ref_ct = cJSON_GetObjectItem(json, "awb_ref_color_temp");
    if (cJSON_IsArray(ref_ct) && cJSON_GetArraySize(ref_ct) >= ISP_AWB_PROFILES_MAX) {
        for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++)
            cfg->awb_ref_color_temp[i] = (uint32_t)cJSON_GetArrayItem(ref_ct, i)->valueint;
    }
    cJSON *gr = cJSON_GetObjectItem(json, "awb_gain_r");
    cJSON *gg = cJSON_GetObjectItem(json, "awb_gain_g");
    cJSON *gb = cJSON_GetObjectItem(json, "awb_gain_b");
    if (cJSON_IsArray(gr) && cJSON_IsArray(gg) && cJSON_IsArray(gb)) {
        for (int i = 0; i < ISP_AWB_PROFILES_MAX && i < cJSON_GetArraySize(gr) && i < cJSON_GetArraySize(gg) && i < cJSON_GetArraySize(gb); i++) {
            cfg->awb_gain_r[i] = (uint32_t)cJSON_GetArrayItem(gr, i)->valueint;
            cfg->awb_gain_g[i] = (uint32_t)cJSON_GetArrayItem(gg, i)->valueint;
            cfg->awb_gain_b[i] = (uint32_t)cJSON_GetArrayItem(gb, i)->valueint;
        }
    }
    cJSON *ccm = cJSON_GetObjectItem(json, "awb_ccm");
    if (cJSON_IsArray(ccm) && cJSON_GetArraySize(ccm) >= ISP_AWB_PROFILES_MAX) {
        for (int p = 0; p < ISP_AWB_PROFILES_MAX; p++) {
            cJSON *row = cJSON_GetArrayItem(ccm, p);
            if (cJSON_IsArray(row) && cJSON_GetArraySize(row) >= 9) {
                for (int i = 0; i < 9; i++)
                    cfg->awb_ccm[p][i / 3][i % 3] = (int32_t)cJSON_GetArrayItem(row, i)->valueint;
            }
        }
    }
    cJSON *ref_rgb = cJSON_GetObjectItem(json, "awb_ref_rgb");
    if (cJSON_IsArray(ref_rgb) && cJSON_GetArraySize(ref_rgb) >= ISP_AWB_PROFILES_MAX) {
        for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
            cJSON *rgb = cJSON_GetArrayItem(ref_rgb, i);
            if (cJSON_IsArray(rgb) && cJSON_GetArraySize(rgb) >= 3) {
                cfg->awb_ref_rgb[i][0] = (uint8_t)cJSON_GetArrayItem(rgb, 0)->valueint;
                cfg->awb_ref_rgb[i][1] = (uint8_t)cJSON_GetArrayItem(rgb, 1)->valueint;
                cfg->awb_ref_rgb[i][2] = (uint8_t)cJSON_GetArrayItem(rgb, 2)->valueint;
            }
        }
    }
    json_get_bool(json, "isp_gain_enable", &cfg->isp_gain_enable);
    json_get_uint32(json, "isp_gain_r", &cfg->isp_gain_r);
    json_get_uint32(json, "isp_gain_g", &cfg->isp_gain_g);
    json_get_uint32(json, "isp_gain_b", &cfg->isp_gain_b);
    json_get_bool(json, "color_conv_enable", &cfg->color_conv_enable);
    cJSON *cc = cJSON_GetObjectItem(json, "color_conv_matrix");
    if (cJSON_IsArray(cc) && cJSON_GetArraySize(cc) >= 9) {
        for (int i = 0; i < 9; i++)
            cfg->color_conv_matrix[i / 3][i % 3] = (int32_t)cJSON_GetArrayItem(cc, i)->valueint;
    }
    json_get_bool(json, "gamma_enable", &cfg->gamma_enable);
    json_get_uint8(json, "sensor_delay", &cfg->sensor_delay);
    json_get_uint32(json, "lux_hl_ref", &cfg->lux_hl_ref);
    json_get_uint32(json, "lux_hl_expo1", &cfg->lux_hl_expo1);
    json_get_uint8(json, "lux_hl_lum1", &cfg->lux_hl_lum1);
    json_get_uint32(json, "lux_hl_expo2", &cfg->lux_hl_expo2);
    json_get_uint8(json, "lux_hl_lum2", &cfg->lux_hl_lum2);
    json_get_uint32(json, "lux_ll_ref", &cfg->lux_ll_ref);
    json_get_uint32(json, "lux_ll_expo1", &cfg->lux_ll_expo1);
    json_get_uint8(json, "lux_ll_lum1", &cfg->lux_ll_lum1);
    json_get_uint32(json, "lux_ll_expo2", &cfg->lux_ll_expo2);
    json_get_uint8(json, "lux_ll_lum2", &cfg->lux_ll_lum2);
    json_get_float(json, "lux_calib_factor", &cfg->lux_calib_factor);
}

static void parse_device_service(cJSON *json, device_service_config_t *cfg)
{
    cJSON *img_cfg = cJSON_GetObjectItem(json, "image_config");
    if (cJSON_IsObject(img_cfg))
    {
        json_get_uint32(img_cfg, "brightness", &cfg->image_config.brightness);
        json_get_uint32(img_cfg, "contrast", &cfg->image_config.contrast);
        json_get_bool(img_cfg, "horizontal_flip", &cfg->image_config.horizontal_flip);
        json_get_bool(img_cfg, "vertical_flip", &cfg->image_config.vertical_flip);
        json_get_uint32(img_cfg, "aec", &cfg->image_config.aec);
        json_get_uint32(img_cfg, "isp_mode", &cfg->image_config.isp_mode);
        json_get_uint32(img_cfg, "fast_capture_skip_frames", &cfg->image_config.fast_capture_skip_frames);
        json_get_uint32(img_cfg, "fast_capture_resolution", &cfg->image_config.fast_capture_resolution);
        json_get_uint32(img_cfg, "fast_capture_jpeg_quality", &cfg->image_config.fast_capture_jpeg_quality);
        json_get_bool(img_cfg, "capture_disable_comm", &cfg->image_config.capture_disable_comm);
        json_get_bool(img_cfg, "capture_storage_ai", &cfg->image_config.capture_storage_ai);
    }

    cJSON *light_cfg = cJSON_GetObjectItem(json, "light_config");
    if (cJSON_IsObject(light_cfg))
    {
        json_get_bool(light_cfg, "connected", &cfg->light_config.connected);
        uint32_t temp_uint32;
        json_get_uint32(light_cfg, "mode", &temp_uint32);
        cfg->light_config.mode = (light_mode_t)temp_uint32;
        json_get_uint32(light_cfg, "start_hour", &cfg->light_config.start_hour);
        json_get_uint32(light_cfg, "start_minute", &cfg->light_config.start_minute);
        json_get_uint32(light_cfg, "end_hour", &cfg->light_config.end_hour);
        json_get_uint32(light_cfg, "end_minute", &cfg->light_config.end_minute);
        json_get_uint32(light_cfg, "brightness_level", &cfg->light_config.brightness_level);
        json_get_bool(light_cfg, "auto_trigger_enabled", &cfg->light_config.auto_trigger_enabled);
        json_get_uint32(light_cfg, "light_threshold", &cfg->light_config.light_threshold);
    }

    cJSON *isp_cfg = cJSON_GetObjectItem(json, "isp_config");
    if (cJSON_IsObject(isp_cfg))
        parse_isp_config(isp_cfg, &cfg->isp_config);
}

static void parse_network_service(cJSON *json, network_service_config_t *cfg)
{
    json_get_uint32(json, "ap_sleep_time", &cfg->ap_sleep_time);
    json_get_string(json, "ssid", cfg->ssid, sizeof(cfg->ssid));
    json_get_string(json, "password", cfg->password, sizeof(cfg->password));
    
    // Parse known_networks array
    json_get_uint32(json, "known_network_count", &cfg->known_network_count);
    if (cfg->known_network_count > 16) {
        cfg->known_network_count = 16;
    }
    
    cJSON *networks = cJSON_GetObjectItem(json, "known_networks");
    if (cJSON_IsArray(networks)) {
        int count = cJSON_GetArraySize(networks);
        if (count > 16) count = 16;
        
        for (int i = 0; i < count; i++) {
            cJSON *net = cJSON_GetArrayItem(networks, i);
            if (cJSON_IsObject(net)) {
                json_get_string(net, "ssid", cfg->known_networks[i].ssid, sizeof(cfg->known_networks[i].ssid));
                json_get_string(net, "bssid", cfg->known_networks[i].bssid, sizeof(cfg->known_networks[i].bssid));
                json_get_string(net, "password", cfg->known_networks[i].password, sizeof(cfg->known_networks[i].password));
                json_get_int32(net, "rssi", &cfg->known_networks[i].rssi);
                json_get_uint32(net, "channel", &cfg->known_networks[i].channel);
                json_get_uint32(net, "security", (uint32_t*)&cfg->known_networks[i].security);
                json_get_bool(net, "connected", &cfg->known_networks[i].connected);
                json_get_bool(net, "is_known", &cfg->known_networks[i].is_known);
                json_get_uint32(net, "last_connected_time", &cfg->known_networks[i].last_connected_time);
            }
        }
    }
    
    // Parse communication type settings
    json_get_uint32(json, "preferred_comm_type", &cfg->preferred_comm_type);
    json_get_bool(json, "enable_auto_priority", &cfg->enable_auto_priority);
    
    // Parse cellular configuration
    cJSON *cellular = cJSON_GetObjectItem(json, "cellular");
    if (cJSON_IsObject(cellular)) {
        json_get_string(cellular, "apn", cfg->cellular.apn, sizeof(cfg->cellular.apn));
        json_get_string(cellular, "username", cfg->cellular.username, sizeof(cfg->cellular.username));
        json_get_string(cellular, "password", cfg->cellular.password, sizeof(cfg->cellular.password));
        json_get_string(cellular, "pin_code", cfg->cellular.pin_code, sizeof(cfg->cellular.pin_code));
        uint32_t auth = 0;
        json_get_uint32(cellular, "authentication", &auth);
        cfg->cellular.authentication = (uint8_t)auth;
        json_get_bool(cellular, "enable_roaming", &cfg->cellular.enable_roaming);
        uint32_t oper = 0;
        json_get_uint32(cellular, "operator", &oper);
        cfg->cellular.operator = (uint8_t)oper;
    }
    
    // Parse PoE/Ethernet configuration
    cJSON *poe = cJSON_GetObjectItem(json, "poe");
    if (cJSON_IsObject(poe)) {
        uint32_t temp = 0;
        json_get_uint32(poe, "ip_mode", &temp);
        cfg->poe.ip_mode = (poe_ip_mode_t)temp;
        
        // Parse IP addresses from arrays
        cJSON *ip_arr = cJSON_GetObjectItem(poe, "ip_addr");
        if (cJSON_IsArray(ip_arr) && cJSON_GetArraySize(ip_arr) == 4) {
            for (int i = 0; i < 4; i++) {
                cfg->poe.ip_addr[i] = (uint8_t)cJSON_GetArrayItem(ip_arr, i)->valueint;
            }
        }
        
        cJSON *mask_arr = cJSON_GetObjectItem(poe, "netmask");
        if (cJSON_IsArray(mask_arr) && cJSON_GetArraySize(mask_arr) == 4) {
            for (int i = 0; i < 4; i++) {
                cfg->poe.netmask[i] = (uint8_t)cJSON_GetArrayItem(mask_arr, i)->valueint;
            }
        }
        
        cJSON *gw_arr = cJSON_GetObjectItem(poe, "gateway");
        if (cJSON_IsArray(gw_arr) && cJSON_GetArraySize(gw_arr) == 4) {
            for (int i = 0; i < 4; i++) {
                cfg->poe.gateway[i] = (uint8_t)cJSON_GetArrayItem(gw_arr, i)->valueint;
            }
        }
        
        cJSON *dns1_arr = cJSON_GetObjectItem(poe, "dns_primary");
        if (cJSON_IsArray(dns1_arr) && cJSON_GetArraySize(dns1_arr) == 4) {
            for (int i = 0; i < 4; i++) {
                cfg->poe.dns_primary[i] = (uint8_t)cJSON_GetArrayItem(dns1_arr, i)->valueint;
            }
        }
        
        cJSON *dns2_arr = cJSON_GetObjectItem(poe, "dns_secondary");
        if (cJSON_IsArray(dns2_arr) && cJSON_GetArraySize(dns2_arr) == 4) {
            for (int i = 0; i < 4; i++) {
                cfg->poe.dns_secondary[i] = (uint8_t)cJSON_GetArrayItem(dns2_arr, i)->valueint;
            }
        }
        
        json_get_string(poe, "hostname", cfg->poe.hostname, sizeof(cfg->poe.hostname));
        
        // DHCP settings
        json_get_uint32(poe, "dhcp_timeout_ms", &cfg->poe.dhcp_timeout_ms);
        json_get_uint32(poe, "dhcp_retry_count", &cfg->poe.dhcp_retry_count);
        json_get_uint32(poe, "dhcp_retry_interval_ms", &cfg->poe.dhcp_retry_interval_ms);
        
        // Recovery settings
        json_get_uint32(poe, "power_recovery_delay_ms", &cfg->poe.power_recovery_delay_ms);
        json_get_bool(poe, "auto_reconnect", &cfg->poe.auto_reconnect);
        json_get_bool(poe, "persist_last_ip", &cfg->poe.persist_last_ip);
        
        // Validation settings
        json_get_bool(poe, "validate_gateway", &cfg->poe.validate_gateway);
        json_get_bool(poe, "detect_ip_conflict", &cfg->poe.detect_ip_conflict);
    }
}

static void json_save_cert_data(cJSON *obj, const char *key, const char *cert_path, uint16_t cert_len)
{
    if (cert_path[0] == '\0')
    {
        return;
    }
    uint8_t *cert_data = (uint8_t *)buffer_calloc(1, cert_len);
    if (cert_data == NULL)
    {
        LOG_CORE_ERROR("Failed to allocate memory for cert data");
        return;
    }
    json_get_string(obj, key, (char *)cert_data, cert_len);
    void *fd = disk_file_fopen(FS_FLASH, cert_path, "w");
    if (fd == NULL)
    {
        LOG_CORE_ERROR("Failed to open cert file: %s", key);
        return;
    }
    disk_file_fwrite(FS_FLASH, fd, cert_data, cert_len);
    disk_file_fclose(FS_FLASH, fd);
    buffer_free(cert_data);
}

static void parse_mqtt_service(cJSON *json, mqtt_service_config_t *cfg)
{
    // Parse complete base_config (mqtt_base_config_t)
    cJSON *base_cfg = cJSON_GetObjectItem(json, "base_config");
    if (cJSON_IsObject(base_cfg))
    {
        uint32_t temp_uint32;

        // Basic connection
        json_get_uint8(base_cfg, "protocol_ver", &cfg->base_config.protocol_ver);
        json_get_string(base_cfg, "hostname", cfg->base_config.hostname, sizeof(cfg->base_config.hostname));
        json_get_uint32(base_cfg, "port", &temp_uint32);
        cfg->base_config.port = (uint16_t)temp_uint32;
        json_get_string(base_cfg, "client_id", cfg->base_config.client_id, sizeof(cfg->base_config.client_id));
        json_get_uint8(base_cfg, "clean_session", &cfg->base_config.clean_session);
        json_get_uint32(base_cfg, "keepalive", &temp_uint32);
        cfg->base_config.keepalive = (uint16_t)temp_uint32;

        // Authentication
        json_get_string(base_cfg, "username", cfg->base_config.username, sizeof(cfg->base_config.username));
        json_get_string(base_cfg, "password", cfg->base_config.password, sizeof(cfg->base_config.password));

        // SSL/TLS - CA certificate
        json_get_string(base_cfg, "ca_cert_path", cfg->base_config.ca_cert_path, sizeof(cfg->base_config.ca_cert_path));
        // Note: ca_cert_data is binary, typically not parsed from JSON
        json_get_uint32(base_cfg, "ca_cert_len", &temp_uint32);
        cfg->base_config.ca_cert_len = (uint16_t)temp_uint32;
        // data save to flash
        json_save_cert_data(base_cfg, "ca_cert_data", cfg->base_config.ca_cert_path, cfg->base_config.ca_cert_len);

        // SSL/TLS - Client certificate
        json_get_string(base_cfg, "client_cert_path", cfg->base_config.client_cert_path, sizeof(cfg->base_config.client_cert_path));
        json_get_uint32(base_cfg, "client_cert_len", &temp_uint32);
        cfg->base_config.client_cert_len = (uint16_t)temp_uint32;
        // data save to flash
        json_save_cert_data(base_cfg, "client_cert_data", cfg->base_config.client_cert_path, cfg->base_config.client_cert_len);

        // SSL/TLS - Client key
        json_get_string(base_cfg, "client_key_path", cfg->base_config.client_key_path, sizeof(cfg->base_config.client_key_path));
        json_get_uint32(base_cfg, "client_key_len", &temp_uint32);
        cfg->base_config.client_key_len = (uint16_t)temp_uint32;
        // data save to flash
        json_save_cert_data(base_cfg, "client_key_data", cfg->base_config.client_key_path, cfg->base_config.client_key_len);

        json_get_uint8(base_cfg, "verify_hostname", &cfg->base_config.verify_hostname);

        // Last Will and Testament
        json_get_string(base_cfg, "lwt_topic", cfg->base_config.lwt_topic, sizeof(cfg->base_config.lwt_topic));
        json_get_string(base_cfg, "lwt_message", cfg->base_config.lwt_message, sizeof(cfg->base_config.lwt_message));
        json_get_uint32(base_cfg, "lwt_msg_len", &temp_uint32);
        cfg->base_config.lwt_msg_len = (uint16_t)temp_uint32;
        json_get_uint8(base_cfg, "lwt_qos", &cfg->base_config.lwt_qos);
        json_get_uint8(base_cfg, "lwt_retain", &cfg->base_config.lwt_retain);

        // Task parameters
        json_get_uint32(base_cfg, "task_priority", &temp_uint32);
        cfg->base_config.task_priority = (uint16_t)temp_uint32;
        json_get_uint32(base_cfg, "task_stack_size", &cfg->base_config.task_stack_size);

        // Network parameters
        json_get_uint8(base_cfg, "disable_auto_reconnect", &cfg->base_config.disable_auto_reconnect);
        json_get_uint8(base_cfg, "outbox_limit", &cfg->base_config.outbox_limit);
        json_get_uint32(base_cfg, "outbox_resend_interval_ms", &temp_uint32);
        cfg->base_config.outbox_resend_interval_ms = (uint16_t)temp_uint32;
        json_get_uint32(base_cfg, "outbox_expired_timeout_ms", &temp_uint32);
        cfg->base_config.outbox_expired_timeout_ms = (uint16_t)temp_uint32;
        json_get_uint32(base_cfg, "reconnect_interval_ms", &temp_uint32);
        cfg->base_config.reconnect_interval_ms = (uint16_t)temp_uint32;
        json_get_uint32(base_cfg, "timeout_ms", &temp_uint32);
        cfg->base_config.timeout_ms = (uint16_t)temp_uint32;
        json_get_uint32(base_cfg, "buffer_size", &temp_uint32);
        cfg->base_config.buffer_size = temp_uint32;
        json_get_uint32(base_cfg, "tx_buf_size", &temp_uint32);
        cfg->base_config.tx_buf_size = temp_uint32;
        json_get_uint32(base_cfg, "rx_buf_size", &temp_uint32);
        cfg->base_config.rx_buf_size = temp_uint32;
    }

    // Extended MQTT service configuration
    json_get_string(json, "data_receive_topic", cfg->data_receive_topic, sizeof(cfg->data_receive_topic));
    json_get_string(json, "data_report_topic", cfg->data_report_topic, sizeof(cfg->data_report_topic));
    json_get_string(json, "status_topic", cfg->status_topic, sizeof(cfg->status_topic));
    json_get_string(json, "command_topic", cfg->command_topic, sizeof(cfg->command_topic));

    json_get_uint8(json, "data_receive_qos", &cfg->data_receive_qos);
    json_get_uint8(json, "data_report_qos", &cfg->data_report_qos);
    json_get_uint8(json, "status_qos", &cfg->status_qos);
    json_get_uint8(json, "command_qos", &cfg->command_qos);

    json_get_bool(json, "auto_subscribe_receive", &cfg->auto_subscribe_receive);
    json_get_bool(json, "auto_subscribe_command", &cfg->auto_subscribe_command);
    json_get_bool(json, "enable_status_report", &cfg->enable_status_report);
    json_get_uint32(json, "status_report_interval_ms", &cfg->status_report_interval_ms);
    json_get_bool(json, "enable_heartbeat", &cfg->enable_heartbeat);
    json_get_uint32(json, "heartbeat_interval_ms", &cfg->heartbeat_interval_ms);
    json_get_uint8(json, "report_content", &cfg->report_content);
    if (cfg->report_content > MQTT_REPORT_CONTENT_METADATA_ONLY) {
        cfg->report_content = MQTT_REPORT_CONTENT_FULL;  // normalize unknown imported values
    }
    json_get_bool(json, "telemetry_enabled", &cfg->telemetry_enabled);
    json_get_string(json, "telemetry_topic", cfg->telemetry_topic, sizeof(cfg->telemetry_topic));
    json_get_uint8(json, "telemetry_qos", &cfg->telemetry_qos);
    json_get_uint8(json, "telemetry_format", &cfg->telemetry_format);
}

static void parse_work_mode(cJSON *json, work_mode_config_t *cfg)
{
    json_get_uint32(json, "work_mode", (uint32_t *)&cfg->work_mode);

    cJSON *img_mode = cJSON_GetObjectItem(json, "image_mode");
    if (cJSON_IsObject(img_mode))
    {
        json_get_bool(img_mode, "enable", &cfg->image_mode.enable);
    }

    cJSON *vid_mode = cJSON_GetObjectItem(json, "video_stream_mode");
    if (cJSON_IsObject(vid_mode))
    {
        json_get_bool(vid_mode, "enable", &cfg->video_stream_mode.enable);
        json_get_string(vid_mode, "rtsp_server_url", cfg->video_stream_mode.rtsp_server_url, sizeof(cfg->video_stream_mode.rtsp_server_url));
    }

    cJSON *pir = cJSON_GetObjectItem(json, "pir_trigger");
    if (cJSON_IsObject(pir))
    {
        json_get_bool(pir, "enable", &cfg->pir_trigger.enable);
        json_get_uint32(pir, "pin_number", &cfg->pir_trigger.pin_number);
        json_get_uint32(pir, "trigger_type", (uint32_t *)&cfg->pir_trigger.trigger_type);
        // PIR sensor configuration parameters
        json_get_uint8(pir, "sensitivity_level", &cfg->pir_trigger.sensitivity_level);
        json_get_uint8(pir, "ignore_time_s", &cfg->pir_trigger.ignore_time_s);
        json_get_uint8(pir, "pulse_count", &cfg->pir_trigger.pulse_count);
        json_get_uint8(pir, "window_time_s", &cfg->pir_trigger.window_time_s);
    }

    cJSON *timer = cJSON_GetObjectItem(json, "timer_trigger");
    if (cJSON_IsObject(timer))
    {
        json_get_bool(timer, "enable", &cfg->timer_trigger.enable);
        uint32_t temp_uint32;
        json_get_uint32(timer, "capture_mode", &temp_uint32);
        cfg->timer_trigger.capture_mode = (aicam_timer_capture_mode_t)temp_uint32;
        json_get_uint32(timer, "interval_sec", &cfg->timer_trigger.interval_sec);
        json_get_uint32(timer, "time_node_count", &cfg->timer_trigger.time_node_count);

        cJSON *nodes = cJSON_GetObjectItem(timer, "time_node");
        int node_count = cJSON_GetArraySize(nodes);
        for (int i = 0; i < node_count && i < 10; i++)
        {
            cJSON *node = cJSON_GetArrayItem(nodes, i);
            if (cJSON_IsNumber(node))
            {
                cfg->timer_trigger.time_node[i] = (uint32_t)node->valueint;
            }
        }

        cJSON *weekdays = cJSON_GetObjectItem(timer, "weekdays");
        int wday_count = cJSON_GetArraySize(weekdays);
        for (int i = 0; i < wday_count && i < 10; i++)
        {
            cJSON *wday = cJSON_GetArrayItem(weekdays, i);
            if (cJSON_IsNumber(wday))
            {
                cfg->timer_trigger.weekdays[i] = (uint8_t)wday->valueint;
            }
        }
    }

    cJSON *io_triggers = cJSON_GetObjectItem(json, "io_trigger");
    int io_count = cJSON_GetArraySize(io_triggers);
    for (int i = 0; i < io_count && i < IO_TRIGGER_MAX; i++)
    {
        cJSON *io = cJSON_GetArrayItem(io_triggers, i);
        if (cJSON_IsObject(io))
        {
            json_get_uint32(io, "pin_number", &cfg->io_trigger[i].pin_number);
            json_get_bool(io, "enable", &cfg->io_trigger[i].enable);
            json_get_bool(io, "input_enable", &cfg->io_trigger[i].input_enable);
            json_get_bool(io, "output_enable", &cfg->io_trigger[i].output_enable);
            uint32_t temp_uint32;
            json_get_uint32(io, "input_trigger_type", &temp_uint32);
            cfg->io_trigger[i].input_trigger_type = (aicam_trigger_type_t)temp_uint32;
            json_get_uint32(io, "output_trigger_type", &temp_uint32);
            cfg->io_trigger[i].output_trigger_type = (aicam_trigger_type_t)temp_uint32;
        }
    }

    cJSON *remote = cJSON_GetObjectItem(json, "remote_trigger");
    if (cJSON_IsObject(remote))
    {
        json_get_bool(remote, "enable", &cfg->remote_trigger.enable);
    }
}

/* ==================== JSON Serialization Helpers (static) ==================== */

// Helper to safely add a 64-bit uint to a cJSON object
// cJSON represents numbers as doubles, which can lose precision for uint64_t.
// A common workaround is to store them as strings.
static void json_add_uint64_as_string(cJSON *obj, const char *key, uint64_t value)
{
    char value_str[21]; // Max length for uint64_t string
    int i = 0;

    if (value == 0) {
        value_str[i++] = '0';
    } else {
        while (value > 0 && i < 20) {
            value_str[i++] = '0' + (value % 10);
            value /= 10;
        }
    }
    value_str[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char tmp = value_str[j];
        value_str[j] = value_str[i - 1 - j];
        value_str[i - 1 - j] = tmp;
    }
    cJSON_AddStringToObject(obj, key, value_str);
}

/* --- Sub-structure Serializers --- */

static cJSON *serialize_log_config(const log_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "log_level", cfg->log_level);
    cJSON_AddNumberToObject(json, "log_file_size_kb", cfg->log_file_size_kb);
    cJSON_AddNumberToObject(json, "log_file_count", cfg->log_file_count);
    return json;
}

static cJSON *serialize_ai_debug(const ai_debug_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ai_enabled", cfg->ai_enabled);
    cJSON_AddBoolToObject(json, "ai_1_active", cfg->ai_1_active);
    cJSON_AddNumberToObject(json, "confidence_threshold", cfg->confidence_threshold);
    cJSON_AddNumberToObject(json, "nms_threshold", cfg->nms_threshold);
    cJSON_AddBoolToObject(json, "overlay_results", cfg->overlay_results);
    cJSON_AddNumberToObject(json, "inference_interval_ms", cfg->inference_interval_ms);
    return json;
}

static cJSON *serialize_power_mode(const power_mode_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "current_mode", cfg->current_mode);
    cJSON_AddNumberToObject(json, "default_mode", cfg->default_mode);
    cJSON_AddNumberToObject(json, "low_power_timeout_ms", cfg->low_power_timeout_ms);
    json_add_uint64_as_string(json, "last_activity_time", cfg->last_activity_time);
    cJSON_AddNumberToObject(json, "mode_switch_count", cfg->mode_switch_count);
    return json;
}

static cJSON *serialize_device_info(const device_info_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_name", cfg->device_name);
    cJSON_AddStringToObject(json, "mac_address", cfg->mac_address);
    cJSON_AddStringToObject(json, "serial_number", cfg->serial_number);
    cJSON_AddStringToObject(json, "hardware_version", cfg->hardware_version);
    cJSON_AddStringToObject(json, "software_version", cfg->software_version);
    cJSON_AddStringToObject(json, "camera_module", cfg->camera_module);
    cJSON_AddStringToObject(json, "extension_modules", cfg->extension_modules);
    cJSON_AddStringToObject(json, "storage_card_info", cfg->storage_card_info);
    cJSON_AddNumberToObject(json, "storage_usage_percent", cfg->storage_usage_percent);
    cJSON_AddStringToObject(json, "power_supply_type", cfg->power_supply_type);
    cJSON_AddNumberToObject(json, "battery_percent", cfg->battery_percent);
    cJSON_AddStringToObject(json, "communication_type", cfg->communication_type);
    return json;
}

static cJSON *serialize_auth_mgr(const auth_mgr_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "session_timeout_ms", cfg->session_timeout_ms);
    cJSON_AddBoolToObject(json, "enable_session_timeout", cfg->enable_session_timeout);
    cJSON_AddStringToObject(json, "admin_password", cfg->admin_password); // Security Note: Storing password in JSON is sensitive
    return json;
}

static cJSON *serialize_isp_config(const isp_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "valid", cfg->valid);
    cJSON_AddBoolToObject(json, "stat_removal_enable", cfg->stat_removal_enable);
    cJSON_AddNumberToObject(json, "stat_removal_head_lines", cfg->stat_removal_head_lines);
    cJSON_AddNumberToObject(json, "stat_removal_valid_lines", cfg->stat_removal_valid_lines);
    cJSON_AddBoolToObject(json, "demosaic_enable", cfg->demosaic_enable);
    cJSON_AddNumberToObject(json, "demosaic_type", cfg->demosaic_type);
    cJSON_AddNumberToObject(json, "demosaic_peak", cfg->demosaic_peak);
    cJSON_AddNumberToObject(json, "demosaic_line_v", cfg->demosaic_line_v);
    cJSON_AddNumberToObject(json, "demosaic_line_h", cfg->demosaic_line_h);
    cJSON_AddNumberToObject(json, "demosaic_edge", cfg->demosaic_edge);
    cJSON_AddBoolToObject(json, "contrast_enable", cfg->contrast_enable);
    cJSON *lut = cJSON_CreateArray();
    for (int i = 0; i < 9; i++)
        cJSON_AddItemToArray(lut, cJSON_CreateNumber(cfg->contrast_lut[i]));
    cJSON_AddItemToObject(json, "contrast_lut", lut);
    cJSON_AddNumberToObject(json, "stat_area_x", cfg->stat_area_x);
    cJSON_AddNumberToObject(json, "stat_area_y", cfg->stat_area_y);
    cJSON_AddNumberToObject(json, "stat_area_width", cfg->stat_area_width);
    cJSON_AddNumberToObject(json, "stat_area_height", cfg->stat_area_height);
    cJSON_AddNumberToObject(json, "sensor_gain", cfg->sensor_gain);
    cJSON_AddNumberToObject(json, "sensor_exposure", cfg->sensor_exposure);
    cJSON_AddBoolToObject(json, "bad_pixel_algo_enable", cfg->bad_pixel_algo_enable);
    cJSON_AddNumberToObject(json, "bad_pixel_algo_threshold", cfg->bad_pixel_algo_threshold);
    cJSON_AddBoolToObject(json, "bad_pixel_enable", cfg->bad_pixel_enable);
    cJSON_AddNumberToObject(json, "bad_pixel_strength", cfg->bad_pixel_strength);
    cJSON_AddBoolToObject(json, "black_level_enable", cfg->black_level_enable);
    cJSON_AddNumberToObject(json, "black_level_r", cfg->black_level_r);
    cJSON_AddNumberToObject(json, "black_level_g", cfg->black_level_g);
    cJSON_AddNumberToObject(json, "black_level_b", cfg->black_level_b);
    cJSON_AddBoolToObject(json, "aec_enable", cfg->aec_enable);
    cJSON_AddNumberToObject(json, "aec_exposure_compensation", cfg->aec_exposure_compensation);
    cJSON_AddNumberToObject(json, "aec_anti_flicker_freq", cfg->aec_anti_flicker_freq);
    cJSON_AddBoolToObject(json, "awb_enable", cfg->awb_enable);
    cJSON *labels = cJSON_CreateArray();
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++)
        cJSON_AddItemToArray(labels, cJSON_CreateString(cfg->awb_label[i]));
    cJSON_AddItemToObject(json, "awb_label", labels);
    cJSON *ref_ct = cJSON_CreateArray();
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++)
        cJSON_AddItemToArray(ref_ct, cJSON_CreateNumber(cfg->awb_ref_color_temp[i]));
    cJSON_AddItemToObject(json, "awb_ref_color_temp", ref_ct);
    cJSON *gr = cJSON_CreateArray(), *gg = cJSON_CreateArray(), *gb = cJSON_CreateArray();
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
        cJSON_AddItemToArray(gr, cJSON_CreateNumber(cfg->awb_gain_r[i]));
        cJSON_AddItemToArray(gg, cJSON_CreateNumber(cfg->awb_gain_g[i]));
        cJSON_AddItemToArray(gb, cJSON_CreateNumber(cfg->awb_gain_b[i]));
    }
    cJSON_AddItemToObject(json, "awb_gain_r", gr);
    cJSON_AddItemToObject(json, "awb_gain_g", gg);
    cJSON_AddItemToObject(json, "awb_gain_b", gb);
    cJSON *ccm = cJSON_CreateArray();
    for (int p = 0; p < ISP_AWB_PROFILES_MAX; p++) {
        cJSON *row = cJSON_CreateArray();
        for (int i = 0; i < 9; i++)
            cJSON_AddItemToArray(row, cJSON_CreateNumber(cfg->awb_ccm[p][i / 3][i % 3]));
        cJSON_AddItemToArray(ccm, row);
    }
    cJSON_AddItemToObject(json, "awb_ccm", ccm);
    cJSON *ref_rgb = cJSON_CreateArray();
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++) {
        cJSON *rgb = cJSON_CreateArray();
        cJSON_AddItemToArray(rgb, cJSON_CreateNumber(cfg->awb_ref_rgb[i][0]));
        cJSON_AddItemToArray(rgb, cJSON_CreateNumber(cfg->awb_ref_rgb[i][1]));
        cJSON_AddItemToArray(rgb, cJSON_CreateNumber(cfg->awb_ref_rgb[i][2]));
        cJSON_AddItemToArray(ref_rgb, rgb);
    }
    cJSON_AddItemToObject(json, "awb_ref_rgb", ref_rgb);
    cJSON_AddBoolToObject(json, "isp_gain_enable", cfg->isp_gain_enable);
    cJSON_AddNumberToObject(json, "isp_gain_r", cfg->isp_gain_r);
    cJSON_AddNumberToObject(json, "isp_gain_g", cfg->isp_gain_g);
    cJSON_AddNumberToObject(json, "isp_gain_b", cfg->isp_gain_b);
    cJSON_AddBoolToObject(json, "color_conv_enable", cfg->color_conv_enable);
    cJSON *cc = cJSON_CreateArray();
    for (int i = 0; i < 9; i++)
        cJSON_AddItemToArray(cc, cJSON_CreateNumber(cfg->color_conv_matrix[i / 3][i % 3]));
    cJSON_AddItemToObject(json, "color_conv_matrix", cc);
    cJSON_AddBoolToObject(json, "gamma_enable", cfg->gamma_enable);
    cJSON_AddNumberToObject(json, "sensor_delay", cfg->sensor_delay);
    cJSON_AddNumberToObject(json, "lux_hl_ref", cfg->lux_hl_ref);
    cJSON_AddNumberToObject(json, "lux_hl_expo1", cfg->lux_hl_expo1);
    cJSON_AddNumberToObject(json, "lux_hl_lum1", cfg->lux_hl_lum1);
    cJSON_AddNumberToObject(json, "lux_hl_expo2", cfg->lux_hl_expo2);
    cJSON_AddNumberToObject(json, "lux_hl_lum2", cfg->lux_hl_lum2);
    cJSON_AddNumberToObject(json, "lux_ll_ref", cfg->lux_ll_ref);
    cJSON_AddNumberToObject(json, "lux_ll_expo1", cfg->lux_ll_expo1);
    cJSON_AddNumberToObject(json, "lux_ll_lum1", cfg->lux_ll_lum1);
    cJSON_AddNumberToObject(json, "lux_ll_expo2", cfg->lux_ll_expo2);
    cJSON_AddNumberToObject(json, "lux_ll_lum2", cfg->lux_ll_lum2);
    cJSON_AddNumberToObject(json, "lux_calib_factor", (double)cfg->lux_calib_factor);
    return json;
}

static cJSON *serialize_device_service(const device_service_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();

    cJSON *img_cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(img_cfg, "brightness", cfg->image_config.brightness);
    cJSON_AddNumberToObject(img_cfg, "contrast", cfg->image_config.contrast);
    cJSON_AddBoolToObject(img_cfg, "horizontal_flip", cfg->image_config.horizontal_flip);
    cJSON_AddBoolToObject(img_cfg, "vertical_flip", cfg->image_config.vertical_flip);
    cJSON_AddNumberToObject(img_cfg, "aec", cfg->image_config.aec);
    cJSON_AddNumberToObject(img_cfg, "isp_mode", cfg->image_config.isp_mode);
    cJSON_AddNumberToObject(img_cfg, "fast_capture_skip_frames", cfg->image_config.fast_capture_skip_frames);
    cJSON_AddNumberToObject(img_cfg, "fast_capture_resolution", cfg->image_config.fast_capture_resolution);
    cJSON_AddNumberToObject(img_cfg, "fast_capture_jpeg_quality", cfg->image_config.fast_capture_jpeg_quality);
    cJSON_AddBoolToObject(img_cfg, "capture_disable_comm", cfg->image_config.capture_disable_comm);
    cJSON_AddBoolToObject(img_cfg, "capture_storage_ai", cfg->image_config.capture_storage_ai);
    cJSON_AddItemToObject(json, "image_config", img_cfg);

    cJSON *light_cfg = cJSON_CreateObject();
    cJSON_AddBoolToObject(light_cfg, "connected", cfg->light_config.connected);
    cJSON_AddNumberToObject(light_cfg, "mode", cfg->light_config.mode);
    cJSON_AddNumberToObject(light_cfg, "start_hour", cfg->light_config.start_hour);
    cJSON_AddNumberToObject(light_cfg, "start_minute", cfg->light_config.start_minute);
    cJSON_AddNumberToObject(light_cfg, "end_hour", cfg->light_config.end_hour);
    cJSON_AddNumberToObject(light_cfg, "end_minute", cfg->light_config.end_minute);
    cJSON_AddNumberToObject(light_cfg, "brightness_level", cfg->light_config.brightness_level);
    cJSON_AddBoolToObject(light_cfg, "auto_trigger_enabled", cfg->light_config.auto_trigger_enabled);
    cJSON_AddNumberToObject(light_cfg, "light_threshold", cfg->light_config.light_threshold);
    cJSON_AddItemToObject(json, "light_config", light_cfg);

    cJSON_AddItemToObject(json, "isp_config", serialize_isp_config(&cfg->isp_config));

    return json;
}

static cJSON *serialize_network_service(const network_service_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "ap_sleep_time", cfg->ap_sleep_time);
    cJSON_AddStringToObject(json, "ssid", cfg->ssid);
    cJSON_AddStringToObject(json, "password", cfg->password);
    cJSON_AddNumberToObject(json, "known_network_count", cfg->known_network_count);

    // Serialize known_networks array
    cJSON *networks = cJSON_CreateArray();
    for (uint32_t i = 0; i < cfg->known_network_count && i < 16; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", cfg->known_networks[i].ssid);
        cJSON_AddStringToObject(net, "bssid", cfg->known_networks[i].bssid);
        cJSON_AddStringToObject(net, "password", cfg->known_networks[i].password);
        cJSON_AddNumberToObject(net, "rssi", cfg->known_networks[i].rssi);
        cJSON_AddNumberToObject(net, "channel", cfg->known_networks[i].channel);
        cJSON_AddNumberToObject(net, "security", cfg->known_networks[i].security);
        cJSON_AddBoolToObject(net, "connected", cfg->known_networks[i].connected);
        cJSON_AddBoolToObject(net, "is_known", cfg->known_networks[i].is_known);
        cJSON_AddNumberToObject(net, "last_connected_time", cfg->known_networks[i].last_connected_time);
        cJSON_AddItemToArray(networks, net);
    }
    cJSON_AddItemToObject(json, "known_networks", networks);
    
    // Serialize communication type settings
    cJSON_AddNumberToObject(json, "preferred_comm_type", cfg->preferred_comm_type);
    cJSON_AddBoolToObject(json, "enable_auto_priority", cfg->enable_auto_priority);
    
    // Serialize cellular configuration
    cJSON *cellular = cJSON_CreateObject();
    cJSON_AddStringToObject(cellular, "apn", cfg->cellular.apn);
    cJSON_AddStringToObject(cellular, "username", cfg->cellular.username);
    cJSON_AddStringToObject(cellular, "password", cfg->cellular.password);
    cJSON_AddStringToObject(cellular, "pin_code", cfg->cellular.pin_code);
    cJSON_AddNumberToObject(cellular, "authentication", cfg->cellular.authentication);
    cJSON_AddBoolToObject(cellular, "enable_roaming", cfg->cellular.enable_roaming);
    cJSON_AddNumberToObject(cellular, "operator", cfg->cellular.operator);
    cJSON_AddItemToObject(json, "cellular", cellular);
    
    // Serialize PoE/Ethernet configuration
    cJSON *poe = cJSON_CreateObject();
    cJSON_AddNumberToObject(poe, "ip_mode", (int)cfg->poe.ip_mode);
    
    // Serialize IP addresses as arrays
    cJSON *ip_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(ip_arr, cJSON_CreateNumber(cfg->poe.ip_addr[i]));
    }
    cJSON_AddItemToObject(poe, "ip_addr", ip_arr);
    
    cJSON *mask_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(mask_arr, cJSON_CreateNumber(cfg->poe.netmask[i]));
    }
    cJSON_AddItemToObject(poe, "netmask", mask_arr);
    
    cJSON *gw_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(gw_arr, cJSON_CreateNumber(cfg->poe.gateway[i]));
    }
    cJSON_AddItemToObject(poe, "gateway", gw_arr);
    
    cJSON *dns1_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(dns1_arr, cJSON_CreateNumber(cfg->poe.dns_primary[i]));
    }
    cJSON_AddItemToObject(poe, "dns_primary", dns1_arr);
    
    cJSON *dns2_arr = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(dns2_arr, cJSON_CreateNumber(cfg->poe.dns_secondary[i]));
    }
    cJSON_AddItemToObject(poe, "dns_secondary", dns2_arr);
    
    cJSON_AddStringToObject(poe, "hostname", cfg->poe.hostname);
    
    // DHCP settings
    cJSON_AddNumberToObject(poe, "dhcp_timeout_ms", cfg->poe.dhcp_timeout_ms);
    cJSON_AddNumberToObject(poe, "dhcp_retry_count", cfg->poe.dhcp_retry_count);
    cJSON_AddNumberToObject(poe, "dhcp_retry_interval_ms", cfg->poe.dhcp_retry_interval_ms);
    
    // Recovery settings
    cJSON_AddNumberToObject(poe, "power_recovery_delay_ms", cfg->poe.power_recovery_delay_ms);
    cJSON_AddBoolToObject(poe, "auto_reconnect", cfg->poe.auto_reconnect);
    cJSON_AddBoolToObject(poe, "persist_last_ip", cfg->poe.persist_last_ip);
    
    // Validation settings
    cJSON_AddBoolToObject(poe, "validate_gateway", cfg->poe.validate_gateway);
    cJSON_AddBoolToObject(poe, "detect_ip_conflict", cfg->poe.detect_ip_conflict);
    
    cJSON_AddItemToObject(json, "poe", poe);

    return json;
}

static void serialize_cert_data(cJSON *obj, const char *key, const char *cert_path, uint16_t cert_len)
{
    if (cert_path[0] == '\0')
    {
        cJSON_AddNullToObject(obj, key);
        return;
    }
    void *fd = disk_file_fopen(FS_FLASH, cert_path, "r");
    if (fd == NULL)
    {
        LOG_CORE_ERROR("Failed to open cert file: %s", cert_path);
        return;
    }
    uint8_t *cert_data = (uint8_t *)buffer_calloc(1, cert_len);
    if (cert_data == NULL)
    {
        LOG_CORE_ERROR("Failed to allocate memory for cert data");
        return;
    }
    disk_file_fread(FS_FLASH, fd, cert_data, cert_len);
    cJSON_AddStringToObject(obj, key, (char *)cert_data);
    buffer_free(cert_data);
    disk_file_fclose(FS_FLASH, fd);
}

static cJSON *serialize_mqtt_service(const mqtt_service_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();

    // Serialize complete base_config (mqtt_base_config_t)
    cJSON *base_cfg = cJSON_CreateObject();

    // Basic connection
    cJSON_AddNumberToObject(base_cfg, "protocol_ver", cfg->base_config.protocol_ver);
    cJSON_AddStringToObject(base_cfg, "hostname", cfg->base_config.hostname);
    cJSON_AddNumberToObject(base_cfg, "port", cfg->base_config.port);
    cJSON_AddStringToObject(base_cfg, "client_id", cfg->base_config.client_id);
    cJSON_AddNumberToObject(base_cfg, "clean_session", cfg->base_config.clean_session);
    cJSON_AddNumberToObject(base_cfg, "keepalive", cfg->base_config.keepalive);

    // Authentication
    cJSON_AddStringToObject(base_cfg, "username", cfg->base_config.username);
    cJSON_AddStringToObject(base_cfg, "password", cfg->base_config.password);

    // SSL/TLS - CA certificate
    cJSON_AddStringToObject(base_cfg, "ca_cert_path", cfg->base_config.ca_cert_path);
    // Note: ca_cert_data is binary, skip in JSON or use base64
    cJSON_AddNumberToObject(base_cfg, "ca_cert_len", cfg->base_config.ca_cert_len);
    // ca_cert_data get from flash
    serialize_cert_data(base_cfg, "ca_cert_data", cfg->base_config.ca_cert_path, cfg->base_config.ca_cert_len);

    // SSL/TLS - Client certificate
    cJSON_AddStringToObject(base_cfg, "client_cert_path", cfg->base_config.client_cert_path);
    cJSON_AddNumberToObject(base_cfg, "client_cert_len", cfg->base_config.client_cert_len);
    // client_cert_data get from flash
    serialize_cert_data(base_cfg, "client_cert_data", cfg->base_config.client_cert_path, cfg->base_config.client_cert_len);

    // SSL/TLS - Client key
    cJSON_AddStringToObject(base_cfg, "client_key_path", cfg->base_config.client_key_path);
    cJSON_AddNumberToObject(base_cfg, "client_key_len", cfg->base_config.client_key_len);
    // client_key_data get from flash
    serialize_cert_data(base_cfg, "client_key_data", cfg->base_config.client_key_path, cfg->base_config.client_key_len);

    cJSON_AddNumberToObject(base_cfg, "verify_hostname", cfg->base_config.verify_hostname);

    // Last Will and Testament
    cJSON_AddStringToObject(base_cfg, "lwt_topic", cfg->base_config.lwt_topic);
    cJSON_AddStringToObject(base_cfg, "lwt_message", cfg->base_config.lwt_message);
    cJSON_AddNumberToObject(base_cfg, "lwt_msg_len", cfg->base_config.lwt_msg_len);
    cJSON_AddNumberToObject(base_cfg, "lwt_qos", cfg->base_config.lwt_qos);
    cJSON_AddNumberToObject(base_cfg, "lwt_retain", cfg->base_config.lwt_retain);

    // Task parameters
    cJSON_AddNumberToObject(base_cfg, "task_priority", cfg->base_config.task_priority);
    cJSON_AddNumberToObject(base_cfg, "task_stack_size", cfg->base_config.task_stack_size);

    // Network parameters
    cJSON_AddNumberToObject(base_cfg, "disable_auto_reconnect", cfg->base_config.disable_auto_reconnect);
    cJSON_AddNumberToObject(base_cfg, "outbox_limit", cfg->base_config.outbox_limit);
    cJSON_AddNumberToObject(base_cfg, "outbox_resend_interval_ms", cfg->base_config.outbox_resend_interval_ms);
    cJSON_AddNumberToObject(base_cfg, "outbox_expired_timeout_ms", cfg->base_config.outbox_expired_timeout_ms);
    cJSON_AddNumberToObject(base_cfg, "reconnect_interval_ms", cfg->base_config.reconnect_interval_ms);
    cJSON_AddNumberToObject(base_cfg, "timeout_ms", cfg->base_config.timeout_ms);
    cJSON_AddNumberToObject(base_cfg, "buffer_size", cfg->base_config.buffer_size);
    cJSON_AddNumberToObject(base_cfg, "tx_buf_size", cfg->base_config.tx_buf_size);
    cJSON_AddNumberToObject(base_cfg, "rx_buf_size", cfg->base_config.rx_buf_size);

    cJSON_AddItemToObject(json, "base_config", base_cfg);

    // Extended MQTT service configuration
    cJSON_AddStringToObject(json, "data_receive_topic", cfg->data_receive_topic);
    cJSON_AddStringToObject(json, "data_report_topic", cfg->data_report_topic);
    cJSON_AddStringToObject(json, "status_topic", cfg->status_topic);
    cJSON_AddStringToObject(json, "command_topic", cfg->command_topic);

    cJSON_AddNumberToObject(json, "data_receive_qos", cfg->data_receive_qos);
    cJSON_AddNumberToObject(json, "data_report_qos", cfg->data_report_qos);
    cJSON_AddNumberToObject(json, "status_qos", cfg->status_qos);
    cJSON_AddNumberToObject(json, "command_qos", cfg->command_qos);

    cJSON_AddBoolToObject(json, "auto_subscribe_receive", cfg->auto_subscribe_receive);
    cJSON_AddBoolToObject(json, "auto_subscribe_command", cfg->auto_subscribe_command);
    cJSON_AddBoolToObject(json, "enable_status_report", cfg->enable_status_report);
    cJSON_AddNumberToObject(json, "status_report_interval_ms", cfg->status_report_interval_ms);
    cJSON_AddBoolToObject(json, "enable_heartbeat", cfg->enable_heartbeat);
    cJSON_AddNumberToObject(json, "heartbeat_interval_ms", cfg->heartbeat_interval_ms);
    cJSON_AddNumberToObject(json, "report_content", cfg->report_content);
    cJSON_AddBoolToObject(json, "telemetry_enabled", cfg->telemetry_enabled);
    cJSON_AddStringToObject(json, "telemetry_topic", cfg->telemetry_topic);
    cJSON_AddNumberToObject(json, "telemetry_qos", cfg->telemetry_qos);
    cJSON_AddNumberToObject(json, "telemetry_format", cfg->telemetry_format);

    return json;
}

static cJSON *serialize_work_mode(const work_mode_config_t *cfg)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "work_mode", cfg->work_mode);

    cJSON *img_mode = cJSON_CreateObject();
    cJSON_AddBoolToObject(img_mode, "enable", cfg->image_mode.enable);
    cJSON_AddItemToObject(json, "image_mode", img_mode);

    cJSON *vid_mode = cJSON_CreateObject();
    cJSON_AddBoolToObject(vid_mode, "enable", cfg->video_stream_mode.enable);
    cJSON_AddStringToObject(vid_mode, "rtsp_server_url", cfg->video_stream_mode.rtsp_server_url);
    cJSON_AddItemToObject(json, "video_stream_mode", vid_mode);

    cJSON *pir = cJSON_CreateObject();
    cJSON_AddBoolToObject(pir, "enable", cfg->pir_trigger.enable);
    cJSON_AddNumberToObject(pir, "pin_number", cfg->pir_trigger.pin_number);
    cJSON_AddNumberToObject(pir, "trigger_type", cfg->pir_trigger.trigger_type);
    // PIR sensor configuration parameters
    cJSON_AddNumberToObject(pir, "sensitivity_level", cfg->pir_trigger.sensitivity_level);
    cJSON_AddNumberToObject(pir, "ignore_time_s", cfg->pir_trigger.ignore_time_s);
    cJSON_AddNumberToObject(pir, "pulse_count", cfg->pir_trigger.pulse_count);
    cJSON_AddNumberToObject(pir, "window_time_s", cfg->pir_trigger.window_time_s);
    cJSON_AddItemToObject(json, "pir_trigger", pir);

    cJSON *timer = cJSON_CreateObject();
    cJSON_AddBoolToObject(timer, "enable", cfg->timer_trigger.enable);
    cJSON_AddNumberToObject(timer, "capture_mode", cfg->timer_trigger.capture_mode);
    cJSON_AddNumberToObject(timer, "interval_sec", cfg->timer_trigger.interval_sec);
    cJSON_AddNumberToObject(timer, "time_node_count", cfg->timer_trigger.time_node_count);

    cJSON *nodes = cJSON_CreateArray();
    for (int i = 0; i < 10; i++)
    {
        cJSON_AddItemToArray(nodes, cJSON_CreateNumber(cfg->timer_trigger.time_node[i]));
    }
    cJSON_AddItemToObject(timer, "time_node", nodes);

    cJSON *weekdays = cJSON_CreateArray();
    for (int i = 0; i < 10; i++)
    {
        cJSON_AddItemToArray(weekdays, cJSON_CreateNumber(cfg->timer_trigger.weekdays[i]));
    }
    cJSON_AddItemToObject(timer, "weekdays", weekdays);
    cJSON_AddItemToObject(json, "timer_trigger", timer);

    cJSON *io_triggers = cJSON_CreateArray();
    for (int i = 0; i < IO_TRIGGER_MAX; i++)
    {
        cJSON *io = cJSON_CreateObject();
        cJSON_AddNumberToObject(io, "pin_number", cfg->io_trigger[i].pin_number);
        cJSON_AddBoolToObject(io, "enable", cfg->io_trigger[i].enable);
        cJSON_AddBoolToObject(io, "input_enable", cfg->io_trigger[i].input_enable);
        cJSON_AddBoolToObject(io, "output_enable", cfg->io_trigger[i].output_enable);
        cJSON_AddNumberToObject(io, "input_trigger_type", cfg->io_trigger[i].input_trigger_type);
        cJSON_AddNumberToObject(io, "output_trigger_type", cfg->io_trigger[i].output_trigger_type);
        cJSON_AddItemToArray(io_triggers, io);
    }
    cJSON_AddItemToObject(json, "io_trigger", io_triggers);

    cJSON *remote = cJSON_CreateObject();
    cJSON_AddBoolToObject(remote, "enable", cfg->remote_trigger.enable);
    cJSON_AddItemToObject(json, "remote_trigger", remote);

    return json;
}

/* ==================== Public API (JSON) Implementation ==================== */

/**
 * @brief Parse configuration from JSON string using cJSON
 */
aicam_result_t json_config_parse_json_object(const char *json_str, aicam_global_config_t *config)
{
    // First, load default configuration as a base
    memcpy(config, &default_config, sizeof(aicam_global_config_t));

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            LOG_CORE_ERROR("cJSON_Parse error near: %s", error_ptr);
        }
        else
        {
            LOG_CORE_ERROR("cJSON_Parse error: Failed to parse JSON string.");
        }
        return AICAM_ERROR_INVALID_PARAM;
    }

    aicam_result_t result = AICAM_OK;

    // Parse root level items
    json_get_uint32(root, "config_version", &config->config_version);
    json_get_uint32(root, "magic_number", &config->magic_number);
    json_get_uint32(root, "checksum", &config->checksum);
    json_get_uint64(root, "timestamp", &config->timestamp);

    // Parse nested objects
    cJSON *log_cfg = cJSON_GetObjectItem(root, "log_config");
    if (cJSON_IsObject(log_cfg))
        parse_log_config(log_cfg, &config->log_config);

    cJSON *ai_cfg = cJSON_GetObjectItem(root, "ai_debug");
    if (cJSON_IsObject(ai_cfg))
        parse_ai_debug(ai_cfg, &config->ai_debug);

    cJSON *pwr_cfg = cJSON_GetObjectItem(root, "power_mode_config");
    if (cJSON_IsObject(pwr_cfg))
        parse_power_mode(pwr_cfg, &config->power_mode_config);

    cJSON *dev_info = cJSON_GetObjectItem(root, "device_info");
    if (cJSON_IsObject(dev_info))
        parse_device_info(dev_info, &config->device_info);

    cJSON *dev_svc = cJSON_GetObjectItem(root, "device_service");
    if (cJSON_IsObject(dev_svc))
        parse_device_service(dev_svc, &config->device_service);

    cJSON *net_svc = cJSON_GetObjectItem(root, "network_service");
    if (cJSON_IsObject(net_svc))
        parse_network_service(net_svc, &config->network_service);

    cJSON *mqtt_svc = cJSON_GetObjectItem(root, "mqtt_service");
    if (cJSON_IsObject(mqtt_svc))
        parse_mqtt_service(mqtt_svc, &config->mqtt_service);

    cJSON *work_mode = cJSON_GetObjectItem(root, "work_mode_config");
    if (cJSON_IsObject(work_mode))
        parse_work_mode(work_mode, &config->work_mode_config);

    cJSON *auth_mgr = cJSON_GetObjectItem(root, "auth_mgr");
    if (cJSON_IsObject(auth_mgr))
        parse_auth_mgr(auth_mgr, &config->auth_mgr);

    cJSON_Delete(root);

    // Cross-group invariants can only be checked once every section has parsed
    json_config_enforce_invariants(config);

    return result;
}

/**
 * @brief Serialize configuration to JSON string using cJSON
 */
aicam_result_t json_config_serialize_json_object(const aicam_global_config_t *config,
                                                 char *json_buffer,
                                                 size_t buffer_size)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return AICAM_ERROR_NO_MEMORY;
    }

    // Add root level items
    cJSON_AddNumberToObject(root, "config_version", config->config_version);
    cJSON_AddNumberToObject(root, "magic_number", config->magic_number);
    cJSON_AddNumberToObject(root, "checksum", config->checksum);
    json_add_uint64_as_string(root, "timestamp", config->timestamp);

    // Add nested objects
    cJSON_AddItemToObject(root, "log_config", serialize_log_config(&config->log_config));
    cJSON_AddItemToObject(root, "ai_debug", serialize_ai_debug(&config->ai_debug));
    cJSON_AddItemToObject(root, "power_mode_config", serialize_power_mode(&config->power_mode_config));
    cJSON_AddItemToObject(root, "device_info", serialize_device_info(&config->device_info));
    cJSON_AddItemToObject(root, "device_service", serialize_device_service(&config->device_service));
    cJSON_AddItemToObject(root, "network_service", serialize_network_service(&config->network_service));
    cJSON_AddItemToObject(root, "mqtt_service", serialize_mqtt_service(&config->mqtt_service));
    cJSON_AddItemToObject(root, "work_mode_config", serialize_work_mode(&config->work_mode_config));
    cJSON_AddItemToObject(root, "auth_mgr", serialize_auth_mgr(&config->auth_mgr));

    // Print to string buffer
    // Using PrintPreallocated is safer as it respects buffer_size
    // cJSON_PrintUnformatted is simpler but less safe if buffer is small
    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string == NULL)
    {
        cJSON_Delete(root);
        return AICAM_ERROR_NO_MEMORY;
    }

    size_t json_len = strlen(json_string);
    aicam_result_t result = AICAM_OK;

    if (json_len < buffer_size)
    {
        strcpy(json_buffer, json_string);
    }
    else
    {
        LOG_CORE_ERROR("JSON buffer too small. Need %zu, have %zu", json_len + 1, buffer_size);
        result = AICAM_ERROR_NO_MEMORY;
    }

    // Cleanup
    cJSON_free(json_string);
    cJSON_Delete(root);

    return result;
}
