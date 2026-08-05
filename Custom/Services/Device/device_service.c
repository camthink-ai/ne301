/**
 * @file device_service.c
 * @brief Device Service Implementation
 * @details Device service standard interface implementation, including device management, storage management and hardware management
 */

#include "device_service.h"
#include "aicam_types.h"
#include "debug.h"
#include "dev_manager.h"
#include "camera.h"
#include "netif_manager.h"
#include "sd_file.h"
#include "misc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "buffer_mgr.h"
#include "generic_file.h"
#include "storage.h"
#include "drtc.h"
#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "common_utils.h"
#include "upgrade_manager.h"
#include "communication_service.h"
#include "web_server.h"
#include "system_service.h"
#include "nn.h"
#include "json_config_mgr.h"
#include "fsbl_app_common.h"
#include "mem_map.h"
#include "pixel_format_map.h"
#include "u0_module.h"

// Note: check_double_click_timeout_stack_buffer removed - factory reset now triggered by super long press (10s) only

/* ==================== Device Service Context ==================== */

typedef struct {
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t state;
    
    // Device information
    device_info_config_t device_info;
    
    // Storage management
    storage_info_t storage_info;
    device_t *storage_device;
    aicam_bool_t storage_initialized;
    
    // Camera and image management
    device_t *camera_device;
    device_t *jpeg_device;
    camera_config_t camera_config;
    aicam_bool_t camera_initialized;
    
    // Light management
    device_t *light_device;
    light_config_t light_config;
    aicam_bool_t light_initialized;
    
    // ISP management
    isp_config_t isp_config;
    
    // LED management
    device_t *led_device;
    led_config_t led_config;
    aicam_bool_t led_initialized;
    system_indicator_state_t indicator_state;  // Current system indicator state
    
    // Sensor management
    sensor_data_t sensor_data;
    aicam_bool_t sensor_initialized;
    aicam_bool_t pir_enabled;
    
    // GPIO management
    gpio_config_t gpio_configs[32];
    
    // Button management
    device_t *button_device;
    button_callback_t button_sp_callback; // single press callback
    button_callback_t button_dc_callback; // double click callback
    button_callback_t button_lp_callback; // long press callback
    button_callback_t button_slp_callback; // super long press callback
    void *button_user_data; // user data(placeholder)
    aicam_bool_t button_initialized;
    
    // Reset trigger state management
    aicam_bool_t double_click_detected; // Double click detection flag
    uint32_t double_click_timestamp;    // Double click timestamp
    uint32_t reset_timeout_ms;          // Reset timeout (milliseconds)
} device_service_context_t;

static device_service_context_t g_device_service = {0};

/* ==================== Helper Functions ==================== */

/**
 * @brief Apply camera configuration to hardware
 * @param config Camera configuration to apply
 * @return aicam_result_t Operation result
 */
static aicam_result_t apply_camera_config_to_hardware(const camera_config_t *config)
{
    if (!config || !g_device_service.camera_device) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Get current sensor parameters from hardware
    sensor_params_t sensor_param;
    aicam_result_t result = device_ioctl(g_device_service.camera_device, 
                                        CAM_CMD_GET_SENSOR_PARAM, 
                                        (uint8_t *)&sensor_param, 
                                        sizeof(sensor_params_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get current sensor parameters: %d", result);
        return result;
    }
    
    // Update sensor parameters based on camera configuration
    aicam_bool_t needs_update = AICAM_FALSE;
    
    // Update brightness (0-100)
    if (sensor_param.brightness != (int)config->image_config.brightness) {
        sensor_param.brightness = (int)config->image_config.brightness;
        needs_update = AICAM_TRUE;
        LOG_SVC_DEBUG("Updating brightness to %d", sensor_param.brightness);
    }
    
    // Update contrast (0-100) 
    if (sensor_param.contrast != (int)config->image_config.contrast) {
        sensor_param.contrast = (int)config->image_config.contrast;
        needs_update = AICAM_TRUE;
        LOG_SVC_DEBUG("Updating contrast to %d", sensor_param.contrast);
    }
    
    // Update mirror/flip settings
    // Convert boolean horizontal/vertical flip to hardware mirror_flip value
    int new_mirror_flip = 0;
    if (config->image_config.horizontal_flip && config->image_config.vertical_flip) {
        new_mirror_flip = 3; // Both mirror and flip
    } else if (config->image_config.horizontal_flip) {
        new_mirror_flip = 2; // Mirror only
    } else if (config->image_config.vertical_flip) {
        new_mirror_flip = 1; // Flip only
    } else {
        new_mirror_flip = 0; // No mirror/flip
    }
    
    if (sensor_param.mirror_flip != new_mirror_flip) {
        sensor_param.mirror_flip = new_mirror_flip;
        needs_update = AICAM_TRUE;
        LOG_SVC_DEBUG("Updating mirror_flip to %d", sensor_param.mirror_flip);
    }
    
    // Update AEC (Auto Exposure Control) from image config
    if (sensor_param.aec != config->image_config.aec) {
        sensor_param.aec = config->image_config.aec;
        needs_update = AICAM_TRUE;
        LOG_SVC_DEBUG("Updating AEC to %d", sensor_param.aec);
    }
    
    // Apply updated sensor parameters to hardware if any changes were made
    if (needs_update) {
        result = device_ioctl(g_device_service.camera_device,
                            CAM_CMD_SET_SENSOR_PARAM,
                            (uint8_t *)&sensor_param,
                            sizeof(sensor_params_t));
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to set sensor parameters: %d", result);
            return result;
        }

        LOG_SVC_INFO("Applied camera configuration to hardware successfully");
    } else {
        LOG_SVC_DEBUG("No hardware changes needed");
    }

    // Apply startup skip frames configuration (also updates driver for next restart)
    if (config->image_config.startup_skip_frames > 0) {
        result = device_ioctl(g_device_service.camera_device,
                            CAM_CMD_SET_STARTUP_SKIP_FRAMES,
                            NULL,
                            config->image_config.startup_skip_frames);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to set startup skip frames: %d", result);
            // Non-fatal error, continue
        } else {
            LOG_SVC_DEBUG("Set startup skip frames to %u", config->image_config.startup_skip_frames);
        }
    }

    return AICAM_OK;
}

static void device_service_build_isp_iq_param(const image_config_t *img_cfg, ISP_IQParamTypeDef *out_iq)
{
    if (img_cfg == NULL || out_iq == NULL) {
        return;
    }

    if (img_cfg->isp_mode == IMAGE_ISP_MODE_CUSTOM && g_device_service.isp_config.valid) {
        json_config_config_to_isp_param(&g_device_service.isp_config, out_iq);
    } else {
        cam_iq_scene_t scene = CAM_IQ_SCENE_INDOOR;
        if (img_cfg->isp_mode == IMAGE_ISP_MODE_OUTDOOR) {
            scene = CAM_IQ_SCENE_OUTDOOR;
        } else if (img_cfg->isp_mode == IMAGE_ISP_MODE_CUSTOM && !g_device_service.isp_config.valid) {
            LOG_SVC_WARN("ISP mode custom without valid saved profile; using indoor IQ defaults");
        }
        camera_fill_isp_iq_scene(scene, out_iq);
    }
    camera_apply_grayscale_iq(out_iq, img_cfg->grayscale);
}

/**
 * @brief Initialize default device information
 */
static void init_default_device_info(device_info_config_t *info)
{
    if (!info) return;
    
    // get device info from json_config_mgr
    device_info_config_t device_info_config;
    aicam_result_t result = json_config_get_device_info_config(&device_info_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get device information configuration: %d", result);
        return;
    }
    
    // update device info
    snprintf(info->device_name, sizeof(info->device_name), "%s", device_info_config.device_name);
    snprintf(info->mac_address, sizeof(info->mac_address), "%s", device_info_config.mac_address);
    snprintf(info->serial_number, sizeof(info->serial_number), "%s", device_info_config.serial_number);
    snprintf(info->hardware_version, sizeof(info->hardware_version), "%s", device_info_config.hardware_version);
    snprintf(info->software_version, sizeof(info->software_version), "%s", device_info_config.software_version);
    snprintf(info->camera_module, sizeof(info->camera_module), "%s", device_info_config.camera_module);
    snprintf(info->extension_modules, sizeof(info->extension_modules), "%s", device_info_config.extension_modules);
    snprintf(info->storage_card_info, sizeof(info->storage_card_info), "%s", device_info_config.storage_card_info);
    info->storage_usage_percent = device_info_config.storage_usage_percent;
    snprintf(info->power_supply_type, sizeof(info->power_supply_type), "%s", device_info_config.power_supply_type);
    info->battery_percent = device_info_config.battery_percent;
    snprintf(info->communication_type, sizeof(info->communication_type), "%s", device_info_config.communication_type);

    LOG_SVC_DEBUG("Device information updated: name=%s, mac_address=%s, serial_number=%s, hardware_version=%s, software_version=%s, camera_module=%s, extension_modules=%s, storage_card_info=%s, storage_usage_percent=%f, power_supply_type=%s, battery_percent=%f, communication_type=%s",
                info->device_name, info->mac_address, info->serial_number, info->hardware_version, info->software_version, info->camera_module, info->extension_modules, info->storage_card_info, info->storage_usage_percent, info->power_supply_type, info->battery_percent, info->communication_type);
}

/**
 * @brief Initialize default storage information
 */
static void init_default_storage_info(storage_info_t *info)
{
    if (!info) return;
    
    info->sd_card_connected = AICAM_FALSE;
    info->total_capacity_mb = 0;
    info->available_capacity_mb = 0;
    info->used_capacity_mb = 0;
    info->usage_percent = 0.0f;
    info->cyclic_overwrite_enabled = AICAM_TRUE;  // Enable cyclic overwrite by default
    info->overwrite_threshold_percent = 80;       // Start overwriting at 80%
}

/**
 * @brief Initialize default camera configuration
 */
static void init_default_camera_config(camera_config_t *config)
{
    if (!config) return;
    
    config->enabled = AICAM_FALSE;
    config->width = 1280;
    config->height = 720;
    config->fps = 30;
    config->image_config.fast_capture_resolution = 0;
    config->image_config.fast_capture_jpeg_quality = 60;
    config->image_config.fast_capture_skip_frames = 10;
    
    // get image config from json_config_mgr
    image_config_t image_config;
    aicam_result_t result = json_config_get_device_service_image_config(&image_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get image configuration: %d", result);
        return;
    }
    
    // update image config
    config->image_config.brightness = image_config.brightness;
    config->image_config.contrast = image_config.contrast;
    config->image_config.horizontal_flip = image_config.horizontal_flip;
    config->image_config.vertical_flip = image_config.vertical_flip;
    config->image_config.aec = image_config.aec;
    config->image_config.startup_skip_frames = image_config.startup_skip_frames;
    config->image_config.fast_capture_skip_frames = image_config.fast_capture_skip_frames;
    config->image_config.fast_capture_resolution = image_config.fast_capture_resolution;
    config->image_config.fast_capture_jpeg_quality = image_config.fast_capture_jpeg_quality;
    config->image_config.capture_disable_comm = image_config.capture_disable_comm;
    config->image_config.capture_storage_ai = image_config.capture_storage_ai;
    config->image_config.isp_mode = image_config.isp_mode;
    config->image_config.grayscale = image_config.grayscale;

    LOG_SVC_DEBUG("Image configuration updated: brightness=%u, contrast=%u, h_flip=%d, v_flip=%d, aec=%d, isp_mode=%u, grayscale=%d, startup_skip=%u, fast_skip=%u, fast_res=%u, fast_jpeg_q=%u, cap_dis_comm=%d, cap_stor_ai=%d",
                config->image_config.brightness,
                config->image_config.contrast,
                config->image_config.horizontal_flip,
                config->image_config.vertical_flip,
                config->image_config.aec,
                config->image_config.isp_mode,
                config->image_config.grayscale,
                config->image_config.startup_skip_frames,
                config->image_config.fast_capture_skip_frames,
                config->image_config.fast_capture_resolution,
                config->image_config.fast_capture_jpeg_quality,
                config->image_config.capture_disable_comm,
                config->image_config.capture_storage_ai);
}

/**
 * @brief Initialize default light configuration
 */
static void init_default_light_config(light_config_t *config)
{
    if (!config) return;

    // get light config from json_config_mgr
    light_config_t light_config;
    aicam_result_t result = json_config_get_device_service_light_config(&light_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get light configuration: %d", result);
        return;
    }
    
    // update light config
    config->connected = light_config.connected;
    config->mode = light_config.mode;
    config->start_hour = light_config.start_hour;
    config->start_minute = light_config.start_minute;
    config->end_hour = light_config.end_hour;
    config->end_minute = light_config.end_minute;
    config->brightness_level = light_config.brightness_level;
    config->auto_trigger_enabled = light_config.auto_trigger_enabled;
    config->light_threshold = light_config.light_threshold;

    LOG_SVC_DEBUG("Light configuration updated: connected=%u, mode=%u, start_hour=%u, start_minute=%u, end_hour=%u, end_minute=%u, brightness_level=%u, auto_trigger_enabled=%u, light_threshold=%u",
                config->connected, config->mode, config->start_hour, config->start_minute, config->end_hour, config->end_minute, config->brightness_level, config->auto_trigger_enabled, config->light_threshold);
}

/**
 * @brief Initialize default ISP configuration
 */
static void init_default_isp_config(isp_config_t *config)
{
    if (!config) return;

    config->valid = AICAM_FALSE;

    // get isp config from json_config_mgr
    isp_config_t isp_config;
    aicam_result_t result = json_config_get_isp_config(&isp_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get ISP configuration: %d", result);
        return;
    }
    
    // update isp config
    memcpy(config, &isp_config, sizeof(isp_config_t));

    LOG_SVC_DEBUG("ISP configuration updated: valid=%u, aec_en=%u, awb_en=%u, gamma_en=%u",
                config->valid, config->aec_enable, config->awb_enable, config->gamma_enable);
}

/**
 * @brief Initialize default LED configuration
 */
static void init_default_led_config(led_config_t *config)
{
    if (!config) return;
    
    config->connected = AICAM_FALSE;
    config->enabled = AICAM_FALSE;
    config->blink_times = 0;        // No blinking by default
    config->interval_ms = 500;      // Default 500ms interval
}

/**
 * @brief Update camera module information
 */
static void update_camera_module_info(device_info_config_t *info)
{
    if (!info) return;
    
    device_t *camera_device = device_find_pattern("camera", DEV_TYPE_VIDEO);
    if (camera_device) {
        sensor_params_t sensor_param;
        device_ioctl(camera_device, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sensor_param, sizeof(sensor_params_t));
        snprintf(info->camera_module, sizeof(info->camera_module), "%s", sensor_param.name);
    }
}

/**
 * @brief Update device MAC address from network interface
 */
void device_service_update_device_mac_address()
{

    netif_info_t netif_info;
    aicam_result_t result = nm_get_netif_info(NETIF_NAME_WIFI_AP, &netif_info);
    if (result == AICAM_OK) {
        // printf("IF_MAC: "NETIF_MAC_STR_FMT"\r\n", NETIF_MAC_PARAMETER(netif_info.if_mac));
        snprintf(g_device_service.device_info.mac_address, sizeof(g_device_service.device_info.mac_address), 
                NETIF_MAC_STR_FMT,
                NETIF_MAC_PARAMETER(netif_info.if_mac));
        
        //save mac address to json_config_mgr, it will generate device name if it's still the default
        aicam_result_t result = json_config_update_device_mac_address(g_device_service.device_info.mac_address);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to update device MAC address: %d", result);
        }
    }

    
}

/**
 * @brief Update current communication type
 */
void device_service_update_communication_type()
{
    
    // Check WiFi connection status
    communication_type_t communication_type = communication_get_selected_type();
    snprintf(g_device_service.device_info.communication_type, sizeof(g_device_service.device_info.communication_type), "%s", communication_type_to_string(communication_type));

    LOG_SVC_DEBUG("Communication type updated: %s", g_device_service.device_info.communication_type);

    return;
}
    


/**
 * @brief Update storage information from HAL layer
 */
static void update_storage_info(storage_info_t *info)
{
    if (!info) return;

    // ── SD Card ──────────────────────────────────────────
    sd_disk_info_t sd_info;
    int result = sd_get_disk_info(&sd_info);

    info->sd_card_connected = (result == 0 &&
                            (sd_info.mode == SD_MODE_NORMAL || sd_info.mode == SD_MODE_FORMATING));

    if (info->sd_card_connected && sd_info.mode == SD_MODE_NORMAL) {
        info->total_capacity_mb = (uint64_t)sd_info.total_KBytes / 1024;
        info->available_capacity_mb = (uint64_t)sd_info.free_KBytes / 1024;
        info->used_capacity_mb = info->total_capacity_mb - info->available_capacity_mb;

        if (info->total_capacity_mb > 0) {
            info->usage_percent = (float)info->used_capacity_mb / info->total_capacity_mb * 100.0f;
        } else {
            info->usage_percent = 0.0f;
        }

        g_device_service.device_info.storage_usage_percent = info->usage_percent;

        snprintf(g_device_service.device_info.storage_card_info,
                sizeof(g_device_service.device_info.storage_card_info),
                "%.1fGB %s SD Card (%.1f%% used)",
                info->total_capacity_mb / 1024.0f,
                sd_info.fs_type,
                info->usage_percent);

        LOG_SVC_DEBUG("SD Card Info: Total=%.1fGB, Used=%.1fGB, Free=%.1fGB, FS=%s",
                    info->total_capacity_mb / 1024.0f,
                    info->used_capacity_mb / 1024.0f,
                    info->available_capacity_mb / 1024.0f,
                    sd_info.fs_type);
    } else {
        info->total_capacity_mb = 0;
        info->used_capacity_mb = 0;
        info->available_capacity_mb = 0;
        info->usage_percent = 0.0f;

        g_device_service.device_info.storage_usage_percent = 0.0f;

        const char* status_msg = "No SD Card";
        switch (sd_info.mode) {
            case SD_MODE_UNPLUG:
                status_msg = "No SD Card";
                break;
            case SD_MODE_UNKNOWN:
                status_msg = "SD Card Error";
                break;
            case SD_MODE_FORMATING:
                status_msg = "SD Card Formatting...";
                break;
            default:
                status_msg = "SD Card Not Ready";
                break;
        }

        snprintf(g_device_service.device_info.storage_card_info,
                sizeof(g_device_service.device_info.storage_card_info),
                "%s", status_msg);

        LOG_SVC_DEBUG("SD Card Status: mode=%d, result=%d", sd_info.mode, result);
    }

    // ── Internal Flash (LittleFS) ────────────────────────
    /* Only check the mounted flag (RAM, O(1)) — do NOT call storage_get_disk_info()
     * here. That runs lfs_fs_size(), a full-FS traverse whose cost grows linearly
     * with file count. With thousands of capture records it takes 3-4 seconds,
     * tripping the watchdog during boot. The free/total capacity is left at 0
     * here; the web API (device_service_get_info → storage_get_disk_info with a
     * 2s cache) fills it in lazily on actual request, not on every boot. */
    info->flash_fs_mounted = storage_is_lfs_mounted();
    info->flash_fs_error = AICAM_FALSE;
    memset(info->flash_error, 0, sizeof(info->flash_error));

    if (info->flash_fs_mounted) {
        /* total is constant — derive from the known partition size, no traverse. */
        info->flash_total_capacity_mb = LITTLEFS_SIZE / (1024 * 1024);
        info->flash_available_capacity_mb = 0;  /* filled lazily by web API */
        info->flash_used_capacity_mb = 0;
        info->flash_usage_percent = 0.0f;
        strncpy(info->flash_fs_type, "littlefs", sizeof(info->flash_fs_type) - 1);
        info->flash_fs_type[sizeof(info->flash_fs_type) - 1] = '\0';
    } else {
        /* Not mounted — surface as an error so the web UI can offer a format. */
        info->flash_fs_error = AICAM_TRUE;
        strncpy(info->flash_error, "not_mounted", sizeof(info->flash_error) - 1);
        info->flash_total_capacity_mb = 0;
        info->flash_available_capacity_mb = 0;
        info->flash_used_capacity_mb = 0;
        info->flash_usage_percent = 0.0f;
        memset(info->flash_fs_type, 0, sizeof(info->flash_fs_type));
    }
}

/**
 * @brief Update device name from json_config_mgr
 */
static void update_device_name(device_info_config_t *info)
{

    if (!info) return;
    
    //get device name from json_config_mgr
    device_info_config_t device_info_config;
    aicam_result_t result = json_config_get_device_info_config(&device_info_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get device information configuration: %d", result);
        return;
    }
    LOG_SVC_DEBUG("Device mac address: %s", info->mac_address);
    snprintf(info->device_name, sizeof(info->device_name), "%s", device_info_config.device_name);
    if(info->device_name[0] == '\0' || strcmp(info->device_name, "AICAM-000000") == 0){
        //generate device name from mac last 6 digits and ignore colon(capital)
        uint8_t mac_last_6_digits[6];
        sscanf(info->mac_address, NETIF_MAC_SCAN_STR_FMT, &mac_last_6_digits[0], &mac_last_6_digits[1], &mac_last_6_digits[2], &mac_last_6_digits[3], &mac_last_6_digits[4], &mac_last_6_digits[5]);
        snprintf(info->device_name, sizeof(info->device_name), "NE301-%02X%02X%02X", mac_last_6_digits[3], mac_last_6_digits[4], mac_last_6_digits[5]);
        LOG_SVC_DEBUG("Generated device name from mac last 6 digits: %s", info->device_name);
    }

}

/**
 * @brief Update battery information from HAL layer
 */
static void update_battery_info(device_info_config_t *info)
{   
    int ret = 0;
    uint8_t battery_rate = 0;
#if ENABLE_U0_MODULE
    uint32_t usbin_status = 0;
#endif
    device_t *battery_device = NULL;
    if (!info) return;

#if ENABLE_U0_MODULE
    ret = u0_module_get_usbin_value(&usbin_status);
    if (ret == 0 && usbin_status == 1) {
        info->battery_percent = 0.0f;
        snprintf(info->power_supply_type, sizeof(info->power_supply_type), "full-power");
        LOG_SVC_DEBUG("Power from USB is connected.");
        return;
    }
#endif

    battery_device = device_find_pattern(BATTERY_DEVICE_NAME, DEV_TYPE_MISC);
    if (battery_device != NULL) {
        /* Retry up to 3 times to handle ADC instability after wakeup from sleep */
        for (int retry = 0; retry < 3; retry++) {
            ret = device_ioctl(battery_device, MISC_CMD_ADC_GET_PERCENT, (uint8_t *)&battery_rate, 0);
            if (ret == 0) break;
            LOG_SVC_WARN("Battery ADC read failed (attempt %d/3), retrying...", retry + 1);
            osDelay(50);
        }
        if (ret == 0) {
            info->battery_percent = (float)battery_rate;
            
            if (info->battery_percent > 100.0f) {
                info->battery_percent = 0.0f;
                snprintf(info->power_supply_type, sizeof(info->power_supply_type), "full-power");
            }
            else
            {
                snprintf(info->power_supply_type, sizeof(info->power_supply_type), "battery");
            }
            
            LOG_SVC_DEBUG("Battery level updated from HAL: %.1f%%", info->battery_percent);
        } else {
            info->battery_percent = 0.0f;
            snprintf(info->power_supply_type, sizeof(info->power_supply_type), "-");
            LOG_SVC_WARN("Failed to get battery level from HAL after retries");
        }
    } else {
        info->battery_percent = 0.0f;
        snprintf(info->power_supply_type, sizeof(info->power_supply_type), "-");
        LOG_SVC_WARN("Battery device not found, using default battery level");
    }
}


/**
 * @brief Check if current time is within custom light schedule
 */
static aicam_bool_t is_in_custom_light_schedule(const light_config_t *config)
{
    if (!config || config->mode != LIGHT_MODE_CUSTOM) {
        return AICAM_FALSE;
    }
    
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    
    uint32_t current_minutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
    uint32_t start_minutes = config->start_hour * 60 + config->start_minute;
    uint32_t end_minutes = config->end_hour * 60 + config->end_minute;
    
    // Handle cross-day case
    if (start_minutes > end_minutes) {
        return (current_minutes >= start_minutes || current_minutes <= end_minutes);
    } else {
        return (current_minutes >= start_minutes && current_minutes <= end_minutes);
    }
}

/**
 * @brief Apply light control based on configuration
 */
static void apply_light_control(const light_config_t *config)
{
    if (!config || !config->connected || !g_device_service.light_device) {
        return;
    }
    
    aicam_bool_t should_enable = AICAM_FALSE;
    
    switch (config->mode) {
        case LIGHT_MODE_OFF:
            should_enable = AICAM_FALSE;
            break;
            
        case LIGHT_MODE_ON:
            should_enable = AICAM_TRUE;
            break;
            
        case LIGHT_MODE_AUTO:
            // Auto control based on light sensor
            if (config->auto_trigger_enabled && g_device_service.sensor_initialized) {
                should_enable = (g_device_service.sensor_data.light_level < config->light_threshold);
            }
            break;
            
        case LIGHT_MODE_CUSTOM:
            should_enable = is_in_custom_light_schedule(config);
            break;
            
        default:
            break;
    }
    
    // Control fill light hardware through HAL layer interface
    if (should_enable) {
        // Set brightness level
        uint8_t duty = config->brightness_level;
        device_ioctl(g_device_service.light_device, MISC_CMD_PWM_SET_DUTY, (uint8_t *)&duty, 0);
        device_ioctl(g_device_service.light_device, MISC_CMD_PWM_ON, 0, 0);
        LOG_SVC_DEBUG("Light turned ON with brightness: %u%% (duty: %u)", config->brightness_level, duty);
    } else {
        device_ioctl(g_device_service.light_device, MISC_CMD_PWM_OFF, 0, 0);
        LOG_SVC_DEBUG("Light turned OFF");
    }
}

/**
 * @brief Single press callback - Take photo
 * @details Running state: Take photo, maintain current LED state
 *          - AP off (slow blink): Take photo, maintain slow blink
 *          - AP on (solid): Take photo, maintain solid
 */
static void single_press_callback(void *user_data)
{
    LOG_SVC_INFO("Single press callback - taking photo\r\n");
    
    // LED state is maintained (no change needed)
    // - If AP is on, LED stays solid
    // - If AP is off, LED stays slow blink
    
    aicam_result_t result = system_service_capture_and_upload_mqtt(AICAM_TRUE, 0, AICAM_TRUE, AICAM_CAPTURE_TRIGGER_BUTTON);
    if(result != AICAM_OK){
        LOG_SVC_ERROR("Upload image to mqtt failed :%d\r\n",result);
    }
}

/**
 * @brief Double click callback - detect double click
 */
static void double_click_callback(void *user_data)
{
    LOG_SVC_INFO("Double click detected\r\n");
    // Double click is now available for other purposes
    // Factory reset is triggered by super long press (10s) only
}

/**
 * @brief Long press callback (2s) - Enable AP hotspot
 * @details Running state:
 *          - AP off (slow blink): Enable AP, LED solid on
 *          - AP on (solid): Keep AP on, maintain solid
 */
static void long_press_callback(void *user_data)
{
    LOG_SVC_INFO("Long press (2s) detected - enabling AP\r\n");
    
    if(communication_is_interface_connected(NETIF_NAME_WIFI_AP) == AICAM_FALSE){
        LOG_SVC_INFO("AP is not connected, starting AP\r\n");
        // Set LED to solid on (AP will be enabled)
        device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_ON);
        web_server_ap_sleep_timer_reset();
    } else {
        LOG_SVC_INFO("AP is already connected, maintaining solid LED\r\n");
        // Ensure LED is solid on
        device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_ON);
    }
}

/**
 * @brief super long press callback - directly trigger factory reset (10s press)
 */
static void super_long_press_callback(void *user_data)
{
    LOG_SVC_INFO("Super long press (10s) detected - triggering factory reset\r\n");
    
    // Directly trigger device reset on super long press
    device_service_reset_to_factory_defaults();
}

// Note: Double click timeout check removed - factory reset now triggered by super long press (10s) only

/* ==================== Device Service Implementation ==================== */

aicam_result_t device_service_init(void *config)
{
    if (g_device_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing Device Service...");
    
    // Initialize device information
    init_default_device_info(&g_device_service.device_info);
    
    // Initialize storage information
    init_default_storage_info(&g_device_service.storage_info);
    
    // Initialize camera configuration
    init_default_camera_config(&g_device_service.camera_config);
    
    // Initialize light configuration
    init_default_light_config(&g_device_service.light_config);

    // Initialize ISP configuration
    init_default_isp_config(&g_device_service.isp_config);
    
    // Initialize LED configuration
    init_default_led_config(&g_device_service.led_config);
    
    // Initialize sensor data
    memset(&g_device_service.sensor_data, 0, sizeof(sensor_data_t));
    g_device_service.pir_enabled = AICAM_FALSE;
    
    // Initialize GPIO configurations
    memset(g_device_service.gpio_configs, 0, sizeof(g_device_service.gpio_configs));
    
    // Initialize button management
    g_device_service.button_sp_callback = single_press_callback;
    g_device_service.button_dc_callback = double_click_callback;
    g_device_service.button_lp_callback = long_press_callback;
    g_device_service.button_slp_callback = super_long_press_callback;
    g_device_service.button_user_data = NULL;
    
    // Note: Reset trigger state fields kept for compatibility but no longer used
    // Factory reset is now triggered by super long press (10s) only
    
    // Set initialization flags
    g_device_service.storage_initialized = AICAM_FALSE;
    g_device_service.camera_initialized = AICAM_FALSE;
    g_device_service.light_initialized = AICAM_FALSE;
    g_device_service.led_initialized = AICAM_FALSE;
    g_device_service.sensor_initialized = AICAM_FALSE;
    g_device_service.button_initialized = AICAM_FALSE;
    
    
    g_device_service.initialized = AICAM_TRUE;
    g_device_service.state = SERVICE_STATE_INITIALIZED;

    //init camera
    device_service_camera_init();
    
    LOG_SVC_INFO("Device Service initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_start(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_device_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting Device Service...");
    
    // Initialize storage management (using sd_get_disk_info interface)
    g_device_service.storage_device = NULL;  // No longer depends on device manager
    g_device_service.storage_initialized = AICAM_TRUE;
    LOG_SVC_INFO("Storage management initialized using sd_get_disk_info interface");
    
    // Find and initialize camera device
    device_service_camera_start();

    // Find and initialize jpeg device
    g_device_service.jpeg_device = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (g_device_service.jpeg_device) {
        LOG_SVC_INFO("Jpeg device found");
    }
    
    // Find and initialize light device (using the same device name as flash_cmd)
    g_device_service.light_device = device_find_pattern(FLASH_DEVICE_NAME, DEV_TYPE_MISC);
    if (g_device_service.light_device) {
        LOG_SVC_INFO("Light device found: %s", FLASH_DEVICE_NAME);
        g_device_service.light_config.connected = AICAM_TRUE;
        g_device_service.light_initialized = AICAM_TRUE;
    } else {
        LOG_SVC_WARN("Light device not found: %s", FLASH_DEVICE_NAME);
    }
    
    // Find and initialize LED device
    g_device_service.led_device = device_find_pattern(IND_EXT_DEVICE_NAME, DEV_TYPE_MISC);
    if (g_device_service.led_device) {
        LOG_SVC_INFO("LED device found");
        g_device_service.led_config.connected = AICAM_TRUE;
        g_device_service.led_initialized = AICAM_TRUE;
        
        // Set initial indicator state: system running, AP not yet started
        g_device_service.indicator_state = SYSTEM_INDICATOR_RUNNING_AP_OFF;
        device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_OFF);
    }
    
    // Find and initialize button device
    g_device_service.button_device = device_find_pattern("key", DEV_TYPE_MISC);
    if (g_device_service.button_device) {
        LOG_SVC_INFO("Button device found");
        device_ioctl(g_device_service.button_device, MISC_CMD_BUTTON_SET_SP_CB, (uint8_t *)g_device_service.button_sp_callback, 0);
        device_ioctl(g_device_service.button_device, MISC_CMD_BUTTON_SET_DC_CB, (uint8_t *)g_device_service.button_dc_callback, 0);
        device_ioctl(g_device_service.button_device, MISC_CMD_BUTTON_SET_LP_CB, (uint8_t *)g_device_service.button_lp_callback, 0);
        device_ioctl(g_device_service.button_device, MISC_CMD_BUTTON_SET_SLP_CB, (uint8_t *)g_device_service.button_slp_callback, 0);
    }
    g_device_service.button_initialized = AICAM_TRUE;
    
    // Initialize sensors
    g_device_service.sensor_data.temperature = 25.0f;
    g_device_service.sensor_data.humidity = 50.0f;
    g_device_service.sensor_data.pir_detected = AICAM_FALSE;
    g_device_service.sensor_data.light_level = 500;
    g_device_service.sensor_initialized = AICAM_TRUE;
    
    // Update device information
    update_camera_module_info(&g_device_service.device_info);
    update_storage_info(&g_device_service.storage_info);
    update_device_name(&g_device_service.device_info);


    // Note: Double click timeout check thread removed - factory reset now triggered by super long press (10s) only
    
    g_device_service.running = AICAM_TRUE;
    g_device_service.state = SERVICE_STATE_RUNNING;
    
    LOG_SVC_INFO("Device Service started successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_stop(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping Device Service...");
    
    // Turn off light if enabled
    if (g_device_service.light_initialized && g_device_service.light_device) {
        device_ioctl(g_device_service.light_device, 0, NULL, 0);
    }
    
    // Stop camera if running
    if (g_device_service.camera_initialized && g_device_service.camera_device) {
        device_stop(g_device_service.camera_device);
    }
    
    g_device_service.running = AICAM_FALSE;
    g_device_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_SVC_INFO("Device Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_deinit(void)
{
    if (!g_device_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_device_service.running) {
        device_service_stop();
    }
    
    LOG_SVC_INFO("Deinitializing Device Service...");
    
    // Reset context
    memset(&g_device_service, 0, sizeof(device_service_context_t));
    
    LOG_SVC_INFO("Device Service deinitialized successfully");
    
    return AICAM_OK;
}

service_state_t device_service_get_state(void)
{
    return g_device_service.state;
}

/* ==================== Device Information Management ==================== */

aicam_result_t device_service_get_info(device_info_config_t *info)
{
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Battery info is read directly from HAL, works regardless of running state
    update_battery_info(&g_device_service.device_info);

    // Update dynamic information (requires full service start)
    if (g_device_service.running) {
        update_storage_info(&g_device_service.storage_info);
        /* Lazily fetch flash free-space — update_storage_info skips this to
         * avoid lfs_fs_size on the boot path. Here (web API request) the 2s
         * cache in storage_get_disk_info makes repeated polls cheap; the first
         * call per cache window does traverse, but that's a user-initiated
         * request, not the boot hot path. */
        storage_disk_info_t fi;
        if (storage_get_disk_info(&fi) == 0 && fi.mounted) {
            g_device_service.storage_info.flash_available_capacity_mb = (uint64_t)fi.free_KBytes / 1024;
            g_device_service.storage_info.flash_used_capacity_mb =
                g_device_service.storage_info.flash_total_capacity_mb -
                g_device_service.storage_info.flash_available_capacity_mb;
            if (g_device_service.storage_info.flash_total_capacity_mb > 0) {
                g_device_service.storage_info.flash_usage_percent =
                    (float)g_device_service.storage_info.flash_used_capacity_mb /
                    g_device_service.storage_info.flash_total_capacity_mb * 100.0f;
            }
        }
        update_device_name(&g_device_service.device_info);
        //device_service_update_device_mac_address();
    }

    device_service_update_communication_type();
    memcpy(info, &g_device_service.device_info, sizeof(device_info_config_t));
    
    return AICAM_OK;
}

aicam_result_t device_service_update_info(const device_info_config_t *info)
{
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(&g_device_service.device_info, info, sizeof(device_info_config_t));

    //store config to json_config_mgr
    aicam_result_t result = json_config_set_device_info_config(&g_device_service.device_info);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set device information configuration: %d", result);
    }
    
    LOG_SVC_INFO("Device information updated");

    return AICAM_OK;
}

/* Lightweight read of cached device info — no storage scan, no side effects.
 * Use this in hot paths (e.g. capture metadata build) where only battery %,
 * device name, and serial are needed. Avoids device_service_get_info() which
 * internally calls update_storage_info() → storage_get_disk_info() → lfs_fs_size()
 * (230 ms on a full 32 MB littlefs volume). */
aicam_result_t device_service_get_cached_info(device_info_config_t *info)
{
    if (!info) return AICAM_ERROR_INVALID_PARAM;
    if (!g_device_service.initialized) return AICAM_ERROR_NOT_INITIALIZED;

    /* Battery is dynamic but fast (HAL GPIO read); update it here. */
    update_battery_info(&g_device_service.device_info);

    memcpy(info, &g_device_service.device_info, sizeof(device_info_config_t));
    return AICAM_OK;
}

/* ==================== Storage Management ==================== */

aicam_bool_t device_service_storage_is_sd_connected(void)
{
    if (!g_device_service.initialized || sd_wait_ready_for_open(1000) != AICAM_OK) {
        return AICAM_FALSE;
    }
    
    sd_disk_info_t sd_info;
    int result = sd_get_disk_info(&sd_info);
    
    return (result == 0 && 
            (sd_info.mode == SD_MODE_NORMAL || sd_info.mode == SD_MODE_FORMATING));
}

aicam_result_t device_service_storage_get_info(storage_info_t *info)
{
    if (!info) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    update_storage_info(&g_device_service.storage_info);
    /* Lazily fetch flash free-space — update_storage_info skips this on the boot
     * path to avoid lfs_fs_size (O(file count), 3-4s with many files → watchdog).
     * Here it's a web API request; the 2s cache in storage_get_disk_info makes
     * repeated polls cheap. */
    storage_disk_info_t fi;
    int fi_ret = storage_get_disk_info(&fi);
    if (fi_ret == 0 && fi.mounted) {
        g_device_service.storage_info.flash_available_capacity_mb = (uint64_t)fi.free_KBytes / 1024;
        g_device_service.storage_info.flash_used_capacity_mb =
            g_device_service.storage_info.flash_total_capacity_mb -
            g_device_service.storage_info.flash_available_capacity_mb;
        if (g_device_service.storage_info.flash_total_capacity_mb > 0) {
            g_device_service.storage_info.flash_usage_percent =
                (float)g_device_service.storage_info.flash_used_capacity_mb /
                g_device_service.storage_info.flash_total_capacity_mb * 100.0f;
        }
    } else {
        LOG_SVC_WARN("storage_get_info: flash disk info failed (ret=%d, mounted=%d) -- lfs_fs_size may have errored on a corrupt FS",
                     fi_ret, (int)fi.mounted);
        /* Surface the failure so the web UI can offer a format. If the FS
         * reported mounted but lfs_fs_size errored, it's corrupt; otherwise
         * it's simply not mounted (already flagged by update_storage_info). */
        g_device_service.storage_info.flash_fs_error = AICAM_TRUE;
        if (fi.mounted) {
            strncpy(g_device_service.storage_info.flash_error, "corrupt",
                    sizeof(g_device_service.storage_info.flash_error) - 1);
            g_device_service.storage_info.flash_error[
                sizeof(g_device_service.storage_info.flash_error) - 1] = '\0';
        }
    }
    memcpy(info, &g_device_service.storage_info, sizeof(storage_info_t));

    return AICAM_OK;
}

aicam_result_t device_service_storage_set_cyclic_overwrite(aicam_bool_t enabled, uint32_t threshold_percent)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (threshold_percent > 100) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    g_device_service.storage_info.cyclic_overwrite_enabled = enabled;
    g_device_service.storage_info.overwrite_threshold_percent = threshold_percent;
    
    LOG_SVC_INFO("Cyclic overwrite policy updated: %s, threshold: %u%%", 
                enabled ? "enabled" : "disabled", threshold_percent);
    
    return AICAM_OK;
}

aicam_result_t sd_write_file(const uint8_t *buffer, uint32_t size, const char *filename)
{
    if (!buffer || !filename || size == 0) {
        LOG_SVC_DEBUG("sd_write_file: invalid parameter\r\n");
        return AICAM_ERROR_INVALID_PARAM;
    }

    LOG_SVC_DEBUG("sd_write_file: %s size:%u\r\n", filename, (unsigned int)size);

    const size_t WRITE_CHUNK_SIZE = 4096;
    uint8_t* write_buf = (uint8_t*)buffer_malloc_aligned_ex(WRITE_CHUNK_SIZE, 32, BUFFER_MEMORY_TYPE_RAM);
    if (!write_buf) {
        LOG_SVC_ERROR("sd_write_file: cannot malloc %s\r\n", filename);
        return AICAM_ERROR_INVALID_PARAM;
    }

    LOG_SVC_DEBUG("sd_write_file open: %s size:%u\r\n", filename, (unsigned int)size);
    void *fd = file_fopen(filename, "w");
    if (!fd) {
        LOG_SVC_ERROR("sd_write_file: cannot open %s\r\n", filename);
        buffer_free(write_buf);
        return AICAM_ERROR_INVALID_PARAM;
    }

    size_t total_written = 0;
    const uint8_t *p = buffer;
    aicam_result_t result = AICAM_OK;

    while (total_written < size) {
        size_t chunk_size = (size - total_written < WRITE_CHUNK_SIZE)
                            ? (size - total_written) : WRITE_CHUNK_SIZE;

        memcpy(write_buf, p + total_written, chunk_size);
        int written = file_fwrite(fd, write_buf, chunk_size);
        if (written != (int)chunk_size) {
            LOG_SVC_ERROR("sd_write_file: write error at offset %u, written=%d, expected=%u\r\n",
                         (unsigned int)total_written, written, (unsigned int)chunk_size);
            result = AICAM_ERROR_INVALID_PARAM;
            break;
        }
        total_written += chunk_size;
    }
    
    file_fclose(fd);
    buffer_free(write_buf);
    return result;
}

/* ==================== Hardware Management ==================== */

aicam_result_t device_service_image_get_config(image_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(config, &g_device_service.camera_config.image_config, sizeof(image_config_t));
    
    return AICAM_OK;
}

aicam_result_t device_service_image_set_config(const image_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Validate parameters
    if (config->brightness > 100 || config->contrast > 100) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (config->isp_mode != IMAGE_ISP_MODE_INDOOR &&
        config->isp_mode != IMAGE_ISP_MODE_OUTDOOR &&
        config->isp_mode != IMAGE_ISP_MODE_CUSTOM) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    memcpy(&g_device_service.camera_config.image_config, config, sizeof(image_config_t));

    
    // Apply configuration to camera device if initialized
    if (g_device_service.camera_initialized && g_device_service.camera_device) {
        aicam_result_t result = apply_camera_config_to_hardware(&g_device_service.camera_config);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to apply camera configuration to hardware: %d", result);
            return result;
        }
    }

    //store config to json_config_mgr
    aicam_result_t result =  json_config_set_device_service_image_config(&g_device_service.camera_config.image_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set image configuration: %d", result);
    }


    LOG_SVC_INFO("Image configuration applied: brightness=%u, contrast=%u, h_flip=%d, v_flip=%d, isp_mode=%u, grayscale=%d",
                config->brightness, config->contrast, config->horizontal_flip, config->vertical_flip,
                config->isp_mode, config->grayscale);

    return AICAM_OK;
}

aicam_result_t device_service_reload_isp_config_from_storage(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    isp_config_t ic = {0};
    aicam_result_t r = json_config_get_isp_config(&ic);
    if (r == AICAM_OK) {
        memcpy(&g_device_service.isp_config, &ic, sizeof(isp_config_t));
    }
    return r;
}

aicam_result_t device_service_light_get_config(light_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(config, &g_device_service.light_config, sizeof(light_config_t));
    
    return AICAM_OK;
}

aicam_result_t device_service_light_set_config(const light_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Validate parameters
    if (config->brightness_level > 100 || 
        config->start_hour >= 24 || config->end_hour >= 24 ||
        config->start_minute >= 60 || config->end_minute >= 60) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(&g_device_service.light_config, config, sizeof(light_config_t));
    
    //store config to json_config_mgr
    aicam_result_t result = json_config_set_device_service_light_config(&g_device_service.light_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set light configuration: %d", result);
    }
    
    LOG_SVC_INFO("Light configuration updated: mode=%d, brightness=%u%%", 
                config->mode, config->brightness_level);
    
    return AICAM_OK;
}

aicam_bool_t device_service_light_is_connected(void)
{
    return g_device_service.light_config.connected;
}

aicam_result_t device_service_light_control(aicam_bool_t enable)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.light_initialized || !g_device_service.light_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Manual control - temporarily override automatic control
    if (enable) {
        // Set current configured brightness level
        uint8_t duty = g_device_service.light_config.brightness_level;
        device_ioctl(g_device_service.light_device, MISC_CMD_PWM_SET_DUTY, (uint8_t *)&duty, 0);
        device_ioctl(g_device_service.light_device, MISC_CMD_PWM_ON, 0, 0);
        LOG_SVC_INFO("Light manually controlled: ON (brightness: %u%%)", g_device_service.light_config.brightness_level);
    } else {
        device_ioctl(g_device_service.light_device, MISC_CMD_PWM_OFF, 0, 0);
        LOG_SVC_INFO("Light manually controlled: OFF");
    }
    
    return AICAM_OK;
}

aicam_result_t device_service_light_set_brightness(uint32_t brightness_level)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.light_initialized || !g_device_service.light_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    
    if (brightness_level > 100) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Update brightness level in configuration
    g_device_service.light_config.brightness_level = brightness_level;
    
    // Set PWM duty cycle
    uint8_t duty = brightness_level;
    device_ioctl(g_device_service.light_device, MISC_CMD_PWM_SET_DUTY, (uint8_t *)&duty, 0);
    
    LOG_SVC_INFO("Light brightness set to: %u%% (duty: %u)", brightness_level, duty);
    
    return AICAM_OK;
}

aicam_result_t device_service_light_blink(uint32_t blink_times, uint32_t interval_ms)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.light_initialized || !g_device_service.light_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    
    if (interval_ms == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Set blink parameters
    blink_params_t blink_params;
    blink_params.blink_times = (int)blink_times;
    blink_params.interval_ms = (int)interval_ms;
    
    aicam_result_t result = device_ioctl(g_device_service.light_device, 
                                        MISC_CMD_PWM_SET_BLINK, 
                                        (uint8_t *)&blink_params, 
                                        0);
    if (result == AICAM_OK) {
        LOG_SVC_INFO("Light blink set: times=%u, interval=%ums", blink_times, interval_ms);
    } else {
        LOG_SVC_ERROR("Failed to set light blink: %d", result);
    }
    
    return result;
}

/* ==================== Camera Interface ==================== */

aicam_result_t device_service_camera_init(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_device_service.camera_initialized) {
        return AICAM_OK;
    }
    
    // Find camera device if not already found
    if (!g_device_service.camera_device) {
        g_device_service.camera_device = device_find_pattern("camera", DEV_TYPE_VIDEO);
        if (!g_device_service.camera_device) {
            LOG_SVC_ERROR("Camera device not found");
            return AICAM_ERROR_NOT_FOUND;
        }
    }
    
    g_device_service.camera_initialized = AICAM_TRUE;
    LOG_SVC_INFO("Camera initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_camera_start(void)
{
    aicam_result_t result;

    if (!g_device_service.camera_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if (!g_device_service.camera_device) {
        return AICAM_ERROR_NOT_FOUND;
    }

    // Apply startup_skip_frames BEFORE device_start, so pipe_start_common uses correct value
    if (g_device_service.camera_config.image_config.startup_skip_frames > 0) {
        device_ioctl(g_device_service.camera_device,
                    CAM_CMD_SET_STARTUP_SKIP_FRAMES,
                    NULL,
                    g_device_service.camera_config.image_config.startup_skip_frames);
    }

    // apply isp IQ init buffer (built-in scene or custom profile from NVS) + grayscale overlay
    {
        ISP_IQParamTypeDef isp_param = {0};
        device_service_build_isp_iq_param(&g_device_service.camera_config.image_config, &isp_param);
        device_ioctl(g_device_service.camera_device,
                    CAM_CMD_SET_ISP_PARAM,
                    (uint8_t *)&isp_param,
                    sizeof(ISP_IQParamTypeDef));
    }

    result = device_start(g_device_service.camera_device);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to start camera: %d", result);
        return result;
    }

    //apply camera config to hardware (other settings like brightness, contrast, flip, aec)
    result = apply_camera_config_to_hardware(&g_device_service.camera_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to apply camera configuration to hardware: %d", result);
        return result;
    }
        
    
    g_device_service.camera_config.enabled = AICAM_TRUE;
    LOG_SVC_INFO("Camera started successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_camera_stop(void)
{
    if (!g_device_service.camera_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.camera_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    
    aicam_result_t result = device_stop(g_device_service.camera_device);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to stop camera");
        return result;
    }
    
    g_device_service.camera_config.enabled = AICAM_FALSE;
    LOG_SVC_INFO("Camera stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_camera_get_config(camera_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Start with stored configuration
    memcpy(config, &g_device_service.camera_config, sizeof(camera_config_t));
    
    // If camera is initialized, sync with hardware parameters
    if (g_device_service.camera_initialized && g_device_service.camera_device) {
        sensor_params_t sensor_param;
        aicam_result_t result = device_ioctl(g_device_service.camera_device,
                                        CAM_CMD_GET_SENSOR_PARAM,
                                        (uint8_t *)&sensor_param,
                                        sizeof(sensor_params_t));
        if (result == AICAM_OK) {
            // Sync image configuration from hardware
            config->image_config.brightness = (uint32_t)sensor_param.brightness;
            config->image_config.contrast = (uint32_t)sensor_param.contrast;
            
            // Convert hardware mirror_flip to boolean flags
            config->image_config.horizontal_flip = (sensor_param.mirror_flip == 2 || sensor_param.mirror_flip == 3) ? AICAM_TRUE : AICAM_FALSE;
            config->image_config.vertical_flip = (sensor_param.mirror_flip == 1 || sensor_param.mirror_flip == 3) ? AICAM_TRUE : AICAM_FALSE;
            
            // Update AEC from hardware
            config->image_config.aec = sensor_param.aec;
            
            // Update camera dimensions from sensor
            config->width = (uint32_t)sensor_param.width;
            config->height = (uint32_t)sensor_param.height;
            config->fps = (uint32_t)sensor_param.fps;
            
            LOG_SVC_DEBUG("Synchronized camera config from hardware");
        } else {
            LOG_SVC_WARN("Failed to sync camera config from hardware, using stored values");
        }
    }
    
    return AICAM_OK;
}

aicam_result_t device_service_camera_set_config(const camera_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Apply configuration to camera device if initialized
    if (g_device_service.camera_initialized && g_device_service.camera_device) {
        aicam_result_t result = apply_camera_config_to_hardware(config);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to apply camera configuration to hardware: %d", result);
            return result;
        }
    }
    
    // Update local configuration after successful hardware update
    memcpy(&g_device_service.camera_config, config, sizeof(camera_config_t));
    
    LOG_SVC_INFO("Camera configuration updated successfully");
    return AICAM_OK;
}

aicam_result_t device_service_camera_capture(uint8_t **buffer, int *out_len,
                                             aicam_bool_t need_ai_inference, nn_result_t *nn_result, uint32_t *frame_id)
{
    if (!g_device_service.camera_initialized || !g_device_service.camera_device)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if (!g_device_service.camera_config.enabled)
    {
        return AICAM_ERROR_UNAVAILABLE;
    }

    aicam_result_t result = AICAM_OK;
    uint32_t fb_len = 0;
    uint32_t ret = 0;
    uint8_t *fb = NULL;
    uint8_t *input_frame_buffer = NULL;
    uint32_t pipe2_fb_len = 0;
    aicam_bool_t light_on = AICAM_FALSE;
    pipe_params_t pipe_param;
    jpegc_params_t jpeg_param;


    // 1. light control
    if (g_device_service.light_config.mode == LIGHT_MODE_AUTO &&
        g_device_service.light_config.auto_trigger_enabled)
    {
        light_on = AICAM_TRUE;
    }
    else if (g_device_service.light_config.mode == LIGHT_MODE_CUSTOM)
    {
        RTC_TIME_S now_time = rtc_get_time();
        int start_minutes = g_device_service.light_config.start_hour * 60 + g_device_service.light_config.start_minute;
        int end_minutes = g_device_service.light_config.end_hour * 60 + g_device_service.light_config.end_minute;
        int now_minutes = now_time.hour * 60 + now_time.minute;

        if (start_minutes < end_minutes)
        {
            light_on = (now_minutes >= start_minutes && now_minutes < end_minutes);
        }
        else if (start_minutes > end_minutes)
        {
            light_on = (now_minutes >= start_minutes || now_minutes < end_minutes);
        }
        else
        {
            light_on = AICAM_FALSE;
        }
    }

    if (light_on)
    {
        device_service_light_control(AICAM_TRUE);
    }

    // 2. get camera and jpeg config
    ret = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&pipe_param, sizeof(pipe_params_t));
    if (ret != 0)
    {
        result = AICAM_ERROR_IO;
        goto cleanup;
    }

    ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_GET_ENC_PARAM, (uint8_t *)&jpeg_param, sizeof(jpegc_params_t));
    if (ret != 0)
    {
        result = AICAM_ERROR_IO;
        goto cleanup;
    }

    jpeg_param.ImageWidth = pipe_param.width;
    jpeg_param.ImageHeight = pipe_param.height;
    jpeg_param.ChromaSubsampling = JPEG_420_SUBSAMPLING;
    jpeg_param.ImageQuality = 60;
    ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_SET_ENC_PARAM, (uint8_t *)&jpeg_param, sizeof(jpegc_params_t));
    if (ret != 0)
    {
        result = AICAM_ERROR_IO;
        goto cleanup;
    }

    // 3. get frame buffer with frame ID
    uint32_t captured_frame_id = 0;
    camera_buffer_with_frame_id_t buffer_with_id = {0};
    aicam_result_t buffer_result = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID, 
                                                 (uint8_t *)&buffer_with_id, 0);
    if (buffer_result == AICAM_OK && buffer_with_id.buffer != NULL && buffer_with_id.size > 0) {
        fb = buffer_with_id.buffer;
        fb_len = buffer_with_id.size;
        captured_frame_id = buffer_with_id.frame_id;
    } else {
        // Fallback to old method if WITH_FRAME_ID is not supported
        fb_len = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE1_BUFFER, (uint8_t *)&fb, 0);
        if (fb_len <= 0 || fb == NULL)
        {
            LOG_SVC_WARN("Failed to get pipe1 buffer");
            result = AICAM_ERROR_INVALID_PARAM;
            goto cleanup;
        }
    }
    
    // Return frame ID if requested
    if (frame_id != NULL) {
        *frame_id = captured_frame_id;
    }

    if (need_ai_inference)
    {
        pipe2_fb_len = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE2_BUFFER, (uint8_t *)&input_frame_buffer, 0);
        if (pipe2_fb_len <= 0 || input_frame_buffer == NULL)
        {
            LOG_SVC_WARN("Failed to get pipe2 buffer");
            result = AICAM_ERROR_INVALID_PARAM;
            goto cleanup;
        }
    }

    // 4. JPEG encode
    ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_INPUT_ENC_BUFFER, fb, fb_len);
    if (ret != 0)
    {
        LOG_SVC_WARN("JPEG encode failed :%d", ret);
        result = AICAM_ERROR_INVALID_PARAM;
        goto cleanup;
    }

    *out_len = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_OUTPUT_ENC_BUFFER, (unsigned char *)buffer, 0);
    if (*out_len <= 0 || *buffer == NULL)
    {
        LOG_SVC_WARN("JPEG output failed :%d", *out_len);
        result = AICAM_ERROR_INVALID_PARAM;
        goto cleanup;
    }

    LOG_SVC_INFO("JPEG buffer pointer:%p, size:%d", *buffer, *out_len);

    // 5. AI inference (optional)
    if (need_ai_inference && input_frame_buffer)
    {
        nn_result_t nn_result_copy;
        memset(&nn_result_copy, 0, sizeof(nn_result_t));
        ret = nn_inference_frame(input_frame_buffer, pipe2_fb_len, &nn_result_copy);
        if (ret != AICAM_OK)
        {
            LOG_SVC_WARN("AI inference failed :%d", ret);
            result = AICAM_ERROR_INVALID_PARAM;
            goto cleanup;
        }
        memcpy(nn_result, &nn_result_copy, sizeof(nn_result_t));
    }

cleanup:
    // 6. clean up and release resources
    if (fb)
    {
        device_ioctl(g_device_service.camera_device, CAM_CMD_RETURN_PIPE1_BUFFER, fb, 0);
        fb = NULL;
    }

    if (input_frame_buffer)
    {
        device_ioctl(g_device_service.camera_device, CAM_CMD_RETURN_PIPE2_BUFFER, input_frame_buffer, 0);
        input_frame_buffer = NULL;
    }

    if (light_on)
    {
        device_service_light_control(AICAM_FALSE);
    }

    return result;
}

aicam_result_t device_service_camera_get_jpeg_params(jpegc_params_t *jpeg_params)
{
    if (!jpeg_params) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_device_service.jpeg_device) {
        g_device_service.jpeg_device = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (!g_device_service.jpeg_device) return AICAM_ERROR_NOT_FOUND;
    }
    aicam_result_t ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_GET_ENC_PARAM, (uint8_t *)jpeg_params, sizeof(jpegc_params_t));
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get JPEG parameters: %d", ret);
        return ret;
    }
    return AICAM_OK;
}

aicam_result_t device_service_camera_free_jpeg_buffer(uint8_t *buffer)
{
    if (!buffer) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!g_device_service.jpeg_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    aicam_result_t ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_RETURN_ENC_BUFFER, buffer, 0);
    if (ret != AICAM_OK) ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_FREE_ENC_BUFFER, buffer, 0);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("Failed to free JPEG buffer: %d", ret);
        return ret;
    }
    return AICAM_OK;
}

/**
 * @brief Fast capture image for low-power RTC wakeup
 * @details Automatically initializes and starts camera/JPEG/light devices if needed,
 *          loads AI model, sets pipe2 parameters, and captures image with AI inference.
 *          Designed for fast startup scenarios where device service may not be fully started.
 *          This API is consistent with device_service_camera_capture but includes device initialization.
 */
aicam_result_t device_service_camera_capture_fast(uint8_t **buffer, int *out_len,
                                                  aicam_bool_t need_ai_inference, nn_result_t *nn_result, uint32_t *frame_id)
{
    if (!buffer || !out_len) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (need_ai_inference && !nn_result) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    uint64_t start_time = rtc_get_uptime_ms();
    // 1. Ensure device_service is at least initialized
    aicam_result_t result = AICAM_OK;
    uint32_t ret = 0;
    uint8_t *fb = NULL;
    uint8_t *input_frame_buffer = NULL;
    uint32_t fb_len = 0;
    uint32_t pipe2_fb_len = 0;
    aicam_bool_t light_on = AICAM_FALSE;
    pipe_params_t pipe_param;
    jpegc_params_t jpeg_param;
    nn_model_info_t model_info = {0};


    init_default_camera_config(&g_device_service.camera_config);
    init_default_light_config(&g_device_service.light_config);
    init_default_isp_config(&g_device_service.isp_config);

    // 2. Initialize camera device if not already initialized
    if (!g_device_service.camera_initialized || !g_device_service.camera_device) {
        LOG_SVC_INFO("[FAST] Initializing camera device...");
        if (!g_device_service.camera_device) {
            g_device_service.camera_device = device_find_pattern("camera", DEV_TYPE_VIDEO);
            if (!g_device_service.camera_device) {
                LOG_SVC_ERROR("[FAST] Camera device not found");
                return AICAM_ERROR_NOT_FOUND;
            }
        }
        g_device_service.camera_initialized = AICAM_TRUE;
    }

    // 3. Find JPEG device if not already found
    if (!g_device_service.jpeg_device) {
        LOG_SVC_INFO("[FAST] Finding JPEG device...");
        g_device_service.jpeg_device = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (!g_device_service.jpeg_device) {
            LOG_SVC_ERROR("[FAST] JPEG device not found");
            return AICAM_ERROR_NOT_FOUND;
        }
    }

    // 4. Initialize light device if not already initialized
    if (!g_device_service.light_initialized || !g_device_service.light_device) {
        LOG_SVC_INFO("[FAST] Initializing light device...");
        if (!g_device_service.light_device) {
            g_device_service.light_device = device_find_pattern(FLASH_DEVICE_NAME, DEV_TYPE_MISC);
            if (g_device_service.light_device) {
                g_device_service.light_config.connected = AICAM_TRUE;
                g_device_service.light_initialized = AICAM_TRUE;
            } else {
                LOG_SVC_WARN("[FAST] Light device not found, continuing without light control");
            }
        }
    }

    // 5. Load AI model if needed and not already loaded
    if (need_ai_inference) {
        nn_state_t nn_state = nn_get_state();
        if (nn_state == NN_STATE_UNINIT || nn_state == NN_STATE_INIT) {
            LOG_SVC_INFO("[FAST] Loading AI model...");
            uintptr_t model_ptr = json_config_get_ai_1_active() ? AI_2_BASE + 1024 : AI_1_BASE + 1024;
            LOG_SVC_INFO("[FAST] Loading model from %p", model_ptr);
            int nn_ret = nn_load_model(model_ptr);
            if (nn_ret != 0) {
                LOG_SVC_ERROR("[FAST] Failed to load model: %d", nn_ret);
                return AICAM_ERROR;
            }
            
            // Get model info after loading
            if (nn_get_model_info(&model_info) != AICAM_OK) {
                LOG_SVC_ERROR("[FAST] Failed to get model info");
                return AICAM_ERROR;
            }
            LOG_SVC_INFO("[FAST] AI model loaded: %dx%d", model_info.input_width, model_info.input_height);
        } else {
            // Model already loaded, get model info
            if (nn_get_model_info(&model_info) != AICAM_OK) {
                LOG_SVC_WARN("[FAST] Failed to get model info, using defaults");
                model_info.input_width = 224;
                model_info.input_height = 224;
            }
        }
    }

    // 6. Start camera if not already started
    if (!g_device_service.camera_config.enabled) {
        // set pipe1 parameters
        pipe_params_t pipe1_param = {0};
        // pipe1_param.width = g_device_service.camera_config.width;
        // pipe1_param.height = g_device_service.camera_config.height;
        if (g_device_service.camera_config.image_config.fast_capture_resolution == 1) {
            pipe1_param.width = 1920;
            pipe1_param.height = 1080;
        } else if (g_device_service.camera_config.image_config.fast_capture_resolution == 2) {
            pipe1_param.width = 2688;
            pipe1_param.height = 1520;
        } else {
            pipe1_param.width = 1280;
            pipe1_param.height = 720;
        }
        pipe1_param.fps = g_device_service.camera_config.fps;
        pipe1_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
        pipe1_param.bpp = 2;
        pipe1_param.buffer_nb = 2;
        ret = device_ioctl(g_device_service.camera_device, CAM_CMD_SET_PIPE1_PARAM, (uint8_t *)&pipe1_param, sizeof(pipe_params_t));
        if (ret != 0) {
            LOG_SVC_ERROR("[FAST] Failed to set pipe1 params: %d, continuing anyway", ret);
            return ret;
        }

        // Set pipe2 parameters based on model input size (only if AI inference is needed)
        if (need_ai_inference && model_info.input_width > 0 && model_info.input_height > 0) {
            pipe_params_t pipe2_param = {0};
            pipe2_param.width = model_info.input_width;
            pipe2_param.height = model_info.input_height;
            pipe2_param.fps = 30;
            pipe2_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
            pipe2_param.bpp = 3;
            pipe2_param.buffer_nb = 2;
            LOG_SVC_INFO("[FAST] Setting pipe2 param: %dx%d@%dfps, format=%d, bpp=%d",
                         pipe2_param.width, pipe2_param.height, pipe2_param.fps, pipe2_param.format, pipe2_param.bpp);
            ret = device_ioctl(g_device_service.camera_device, CAM_CMD_SET_PIPE2_PARAM, 
                              (uint8_t *)&pipe2_param, sizeof(pipe_params_t));
            if (ret != 0) {
                LOG_SVC_WARN("[FAST] Failed to set pipe2 params: %d, continuing anyway", ret);
            }

            // Enable both pipe1 and pipe2
            uint8_t pipe_ctrl = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
            ret = device_ioctl(g_device_service.camera_device, CAM_CMD_SET_PIPE_CTRL, &pipe_ctrl, 0);
            if (ret != 0) {
                LOG_SVC_WARN("[FAST] Failed to set pipe control: %d, continuing anyway", ret);
            }
        } else {
            // Only enable pipe1 if AI inference is not needed
            uint8_t pipe_ctrl = CAMERA_CTRL_PIPE1_BIT;
            ret = device_ioctl(g_device_service.camera_device, CAM_CMD_SET_PIPE_CTRL, &pipe_ctrl, 0);
            if (ret != 0) {
                LOG_SVC_WARN("[FAST] Failed to set pipe control: %d, continuing anyway", ret);
            }
        }

        // Apply startup_skip_frames BEFORE device_start
        device_ioctl(g_device_service.camera_device,
            CAM_CMD_SET_STARTUP_SKIP_FRAMES,
            NULL,
            g_device_service.camera_config.image_config.fast_capture_skip_frames);

        {
            ISP_IQParamTypeDef isp_param = {0};
            device_service_build_isp_iq_param(&g_device_service.camera_config.image_config, &isp_param);
            device_ioctl(g_device_service.camera_device,
                        CAM_CMD_SET_ISP_PARAM,
                        (uint8_t *)&isp_param,
                        sizeof(ISP_IQParamTypeDef));
        }

        LOG_SVC_INFO("[FAST] Starting camera...");
        result = device_start(g_device_service.camera_device);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("[FAST] Failed to start camera: %d", result);
            return result;
        }

        // Apply camera configuration to hardware
        result = apply_camera_config_to_hardware(&g_device_service.camera_config);
        if (result != AICAM_OK) {
            LOG_SVC_WARN("[FAST] Failed to apply camera config, continuing anyway: %d", result);
        }

        g_device_service.camera_config.enabled = AICAM_TRUE;
    }

    // 7. Light control (same logic as device_service_camera_capture)
    if (g_device_service.light_initialized && g_device_service.light_device) {
        if (g_device_service.light_config.mode == LIGHT_MODE_AUTO &&
            g_device_service.light_config.auto_trigger_enabled)
        {
            light_on = AICAM_TRUE;
        }
        else if (g_device_service.light_config.mode == LIGHT_MODE_CUSTOM)
        {
            RTC_TIME_S now_time = rtc_get_time();
            int start_minutes = g_device_service.light_config.start_hour * 60 + g_device_service.light_config.start_minute;
            int end_minutes = g_device_service.light_config.end_hour * 60 + g_device_service.light_config.end_minute;
            int now_minutes = now_time.hour * 60 + now_time.minute;

            if (start_minutes < end_minutes)
            {
                light_on = (now_minutes >= start_minutes && now_minutes < end_minutes);
            }
            else if (start_minutes > end_minutes)
            {
                light_on = (now_minutes >= start_minutes || now_minutes < end_minutes);
            }
            else
            {
                light_on = AICAM_FALSE;
            }
        }

        if (light_on)
        {
            device_service_light_control(AICAM_TRUE);
        }
    }

    // 8. Get camera and JPEG config (same as device_service_camera_capture)
    ret = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE1_PARAM, 
                       (uint8_t *)&pipe_param, sizeof(pipe_params_t));
    if (ret != 0)
    {
        result = AICAM_ERROR_IO;
        goto cleanup;
    }

    ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_GET_ENC_PARAM, 
                       (uint8_t *)&jpeg_param, sizeof(jpegc_params_t));
    if (ret != 0)
    {
        result = AICAM_ERROR_IO;
        goto cleanup;
    }

    jpeg_param.ImageWidth = pipe_param.width;
    jpeg_param.ImageHeight = pipe_param.height;
    jpeg_param.ChromaSubsampling = JPEG_420_SUBSAMPLING;
    jpeg_param.ImageQuality = g_device_service.camera_config.image_config.fast_capture_jpeg_quality;
    ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_SET_ENC_PARAM, 
                       (uint8_t *)&jpeg_param, sizeof(jpegc_params_t));
    if (ret != 0)
    {
        result = AICAM_ERROR_IO;
        goto cleanup;
    }

    uint64_t get_fb_time = rtc_get_uptime_ms();
    printf("prepare photo time %lu / %lu ms\r\n", (unsigned long)(get_fb_time - start_time), (unsigned long)get_fb_time);

    // 9. Get frame buffer with frame ID (same as device_service_camera_capture)
    uint32_t captured_frame_id = 0;
    camera_buffer_with_frame_id_t buffer_with_id = {0};
    aicam_result_t buffer_result = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID, 
                                                 (uint8_t *)&buffer_with_id, 0);
    if (buffer_result == AICAM_OK && buffer_with_id.buffer != NULL && buffer_with_id.size > 0) {
        fb = buffer_with_id.buffer;
        fb_len = buffer_with_id.size;
        captured_frame_id = buffer_with_id.frame_id;
    } else {
        // Fallback to old method if WITH_FRAME_ID is not supported
        fb_len = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE1_BUFFER, 
                              (uint8_t *)&fb, 0);
        if (fb_len <= 0 || fb == NULL)
        {
            LOG_SVC_WARN("[FAST] Failed to get pipe1 buffer, fb_len:%d", fb_len);
            result = AICAM_ERROR_INVALID_PARAM;
            goto cleanup;
        }
    }
    
    // Return frame ID if requested
    if (frame_id != NULL) {
        *frame_id = captured_frame_id;
    }
    
    uint64_t take_photo_time = rtc_get_uptime_ms();
    printf("camera photo time %lu / %lu ms\r\n", (unsigned long)(take_photo_time - get_fb_time), (unsigned long)take_photo_time);

    // Get pipe2 buffer for AI inference (only if needed)
    if (need_ai_inference)
    {
        pipe2_fb_len = device_ioctl(g_device_service.camera_device, CAM_CMD_GET_PIPE2_BUFFER, 
                                    (uint8_t *)&input_frame_buffer, 0);
        if (pipe2_fb_len <= 0 || input_frame_buffer == NULL)
        {
            LOG_SVC_WARN("[FAST] Failed to get pipe2 buffer");
            result = AICAM_ERROR_INVALID_PARAM;
            goto cleanup;
        }
    }

    // 10. JPEG encode (same as device_service_camera_capture)
    ret = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_INPUT_ENC_BUFFER, fb, fb_len);
    if (ret != 0)
    {
        LOG_SVC_WARN("[FAST] JPEG encode failed: %d", ret);
        result = AICAM_ERROR_INVALID_PARAM;
        goto cleanup;
    }

    *out_len = device_ioctl(g_device_service.jpeg_device, JPEGC_CMD_OUTPUT_ENC_BUFFER, 
                            (unsigned char *)buffer, 0);
    if (*out_len <= 0 || *buffer == NULL)
    {
        LOG_SVC_WARN("[FAST] JPEG output failed: %d", *out_len);
        result = AICAM_ERROR_INVALID_PARAM;
        goto cleanup;
    }

    LOG_SVC_INFO("[FAST] JPEG buffer pointer:%p, size:%d", *buffer, *out_len);

    // 11. AI inference (same as device_service_camera_capture)
    if (need_ai_inference && input_frame_buffer)
    {
        nn_result_t nn_result_copy;
        memset(&nn_result_copy, 0, sizeof(nn_result_t));
        ret = nn_inference_frame(input_frame_buffer, pipe2_fb_len, &nn_result_copy);
        if (ret != AICAM_OK)
        {
            LOG_SVC_WARN("[FAST] AI inference failed: %d", ret);
            result = AICAM_ERROR_INVALID_PARAM;
            goto cleanup;
        }
        memcpy(nn_result, &nn_result_copy, sizeof(nn_result_t));
        LOG_SVC_INFO("[FAST] AI inference completed: %d detections", nn_result->od.nb_detect);
    }

cleanup:
    // 12. Clean up and release resources (same as device_service_camera_capture)
    if (fb)
    {
        device_ioctl(g_device_service.camera_device, CAM_CMD_RETURN_PIPE1_BUFFER, fb, 0);
        fb = NULL;
    }

    if (input_frame_buffer)
    {
        device_ioctl(g_device_service.camera_device, CAM_CMD_RETURN_PIPE2_BUFFER, input_frame_buffer, 0);
        input_frame_buffer = NULL;
    }

    if (light_on && g_device_service.light_initialized && g_device_service.light_device)
    {
        device_service_light_control(AICAM_FALSE);
    }

    return result;
}

/* ==================== Sensor Interface ==================== */

aicam_result_t device_service_sensor_init(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_device_service.sensor_initialized) {
        return AICAM_OK;
    }
    
    // Initialize sensor data
    g_device_service.sensor_data.temperature = 25.0f;
    g_device_service.sensor_data.humidity = 50.0f;
    g_device_service.sensor_data.pir_detected = AICAM_FALSE;
    g_device_service.sensor_data.light_level = 500;
    
    g_device_service.sensor_initialized = AICAM_TRUE;
    LOG_SVC_INFO("Sensors initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t device_service_sensor_read(sensor_data_t *data)
{
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.sensor_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // TODO: Read actual sensor data through HAL layer interface
    // For now, simulate sensor readings
    g_device_service.sensor_data.temperature = 20.0f + (float)(rand() % 20);
    g_device_service.sensor_data.humidity = 30.0f + (float)(rand() % 40);
    g_device_service.sensor_data.light_level = 100 + (rand() % 900);
    
    // PIR detection simulation
    if (g_device_service.pir_enabled) {
        g_device_service.sensor_data.pir_detected = (rand() % 10 == 0) ? AICAM_TRUE : AICAM_FALSE;
    }
    
    memcpy(data, &g_device_service.sensor_data, sizeof(sensor_data_t));
    
    // Update light control based on light sensor reading
    if (g_device_service.light_initialized && 
        g_device_service.light_config.mode == LIGHT_MODE_AUTO &&
        g_device_service.light_config.auto_trigger_enabled) {
        apply_light_control(&g_device_service.light_config);
    }
    
    return AICAM_OK;
}

aicam_result_t device_service_sensor_pir_enable(aicam_bool_t enable)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_device_service.pir_enabled = enable;
    
    LOG_SVC_INFO("PIR sensor %s", enable ? "enabled" : "disabled");
    
    return AICAM_OK;
}

/* ==================== GPIO Interface ==================== */

aicam_result_t device_service_gpio_config(const gpio_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (config->pin_number >= 32) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Save GPIO configuration
    memcpy(&g_device_service.gpio_configs[config->pin_number], config, sizeof(gpio_config_t));
    
    // TODO: Apply GPIO configuration to hardware through HAL layer interface
    
    LOG_SVC_INFO("GPIO pin %u configured", config->pin_number);
    
    return AICAM_OK;
}

aicam_result_t device_service_gpio_set(uint32_t pin_number, aicam_bool_t state)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (pin_number >= 32) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check if pin is configured as output
    if (g_device_service.gpio_configs[pin_number].is_input) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // TODO: Set actual GPIO pin state through HAL layer interface
    
    LOG_SVC_INFO("GPIO pin %u set to %s", pin_number, state ? "HIGH" : "LOW");
    
    return AICAM_OK;
}

aicam_result_t device_service_gpio_get(uint32_t pin_number, aicam_bool_t *state)
{
    if (!state) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (pin_number >= 32) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // TODO: Read actual GPIO pin state through HAL layer interface
    // For now, simulate random state
    *state = (rand() % 2) ? AICAM_TRUE : AICAM_FALSE;
    
    return AICAM_OK;
}

/* ==================== Device Reset Interface ==================== */

/**
 * @brief Restart the system
 * @return aicam_result_t Operation result
 */
static aicam_result_t restart_system(void)
{
    LOG_SVC_INFO("Initiating system restart...");

    // NVS writes are now immediate, no flush needed
    osDelay(200);  // Wait for Flash operations to complete
    
    // Note: This function will not return, the system will reboot immediately
#if ENABLE_U0_MODULE
    u0_module_clear_wakeup_flag();
    u0_module_reset_chip_n6();
#endif
    HAL_NVIC_SystemReset();
    
    return AICAM_OK;
}

aicam_result_t device_service_reset_to_factory_defaults(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting device reset to factory defaults...");

    // Set indicator to fast blink (5Hz) during factory reset
    device_service_set_indicator_state(SYSTEM_INDICATOR_FACTORY_RESET);

    osDelay(200);
    
    // 1. reset NVS data
    aicam_result_t result = json_config_reset_to_default(NULL);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reset JSON config: %d", result);
        return result;
    }
    
    // 2. Optional: Clear other configuration data
    // clear ai model
    SystemState *state = get_system_state();
    if (state) {
        // Mark both slots as IDLE
        state->slot[FIRMWARE_AI_2][SLOT_A].status = IDLE;
        state->slot[FIRMWARE_AI_2][SLOT_B].status = IDLE;
        save_system_state();
        LOG_SVC_INFO("AI model cleared");
    }

    // 3. Boot CPU clock: cleared inside json_config_reset_to_default (persisted FSBL profile)

    LOG_SVC_INFO("Device reset to factory defaults completed, restarting system...");
    
    // 4. Restart system
    result = restart_system();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to restart system: %d", result);
        return result;
    }
    
    return AICAM_OK;
}

/* ==================== LED Interface Implementation ==================== */

aicam_result_t device_service_led_get_config(led_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(config, &g_device_service.led_config, sizeof(led_config_t));
    
    return AICAM_OK;
}

aicam_result_t device_service_led_set_config(const led_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Validate parameters
    if (config->interval_ms == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(&g_device_service.led_config, config, sizeof(led_config_t));
    
    LOG_SVC_INFO("LED configuration updated: enabled=%d, blink_times=%u, interval=%ums", 
                config->enabled, config->blink_times, config->interval_ms);
    
    return AICAM_OK;
}

aicam_bool_t device_service_led_is_connected(void)
{
    return g_device_service.led_config.connected;
}

aicam_result_t device_service_led_on(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.led_initialized || !g_device_service.led_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    
    aicam_result_t result = device_ioctl(g_device_service.led_device, MISC_CMD_LED_ON, 0, 0);
    if (result == AICAM_OK) {
        g_device_service.led_config.enabled = AICAM_TRUE;
        LOG_SVC_INFO("LED turned ON");
    } else {
        LOG_SVC_ERROR("Failed to turn LED ON: %d", result);
    }
    
    return result;
}

aicam_result_t device_service_led_off(void)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_device_service.led_initialized || !g_device_service.led_device) {
        return AICAM_ERROR_NOT_FOUND;
    }
    
    aicam_result_t result = device_ioctl(g_device_service.led_device, MISC_CMD_LED_OFF, 0, 0);
    if (result == AICAM_OK) {
        g_device_service.led_config.enabled = AICAM_FALSE;
        LOG_SVC_INFO("LED turned OFF");
    } else {
        LOG_SVC_ERROR("Failed to turn LED OFF: %d", result);
    }
    
    return result;
}

aicam_result_t device_service_led_blink(uint32_t blink_times, uint32_t interval_ms)
{

    
    if (interval_ms == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if(!g_device_service.led_initialized || !g_device_service.led_device) {
        g_device_service.led_device = device_find_pattern(IND_EXT_DEVICE_NAME, DEV_TYPE_MISC);
        if(!g_device_service.led_device) {
            return AICAM_ERROR_NOT_FOUND;
        }
        g_device_service.led_initialized = AICAM_TRUE;
    }
    
    blink_params_t blink_params;
    blink_params.blink_times = (int)blink_times;
    blink_params.interval_ms = (int)interval_ms;
    
    aicam_result_t result = device_ioctl(g_device_service.led_device, 
                                        MISC_CMD_LED_SET_BLINK, 
                                        (uint8_t *)&blink_params, 
                                        0);
    if (result == AICAM_OK) {
        g_device_service.led_config.blink_times = blink_times;
        g_device_service.led_config.interval_ms = interval_ms;
        g_device_service.led_config.enabled = AICAM_TRUE;
        LOG_SVC_INFO("LED blink set: times=%u, interval=%ums", blink_times, interval_ms);
    } else {
        LOG_SVC_ERROR("Failed to set LED blink: %d", result);
    }
    
    return result;
}

aicam_result_t device_service_set_indicator_state(system_indicator_state_t state)
{
    if (!g_device_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (state >= SYSTEM_INDICATOR_MAX) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    aicam_result_t result = AICAM_OK;
    
    switch (state) {
        case SYSTEM_INDICATOR_SLEEP:
            // LED off - low power/power off mode
            result = device_service_led_off();
            if (result == AICAM_OK) {
                LOG_SVC_INFO("System indicator: SLEEP (LED off)");
            }
            break;
            
        case SYSTEM_INDICATOR_RUNNING_AP_OFF:
            // Slow blink 1Hz - 1 blink per second (500ms on + 500ms off)
            result = device_service_led_blink(0, 500);  // 0 = infinite blink
            if (result == AICAM_OK) {
                LOG_SVC_INFO("System indicator: RUNNING_AP_OFF (1Hz slow blink)");
            }
            break;
            
        case SYSTEM_INDICATOR_RUNNING_AP_ON:
            // LED solid on - AP is active
            result = device_service_led_on();
            if (result == AICAM_OK) {
                LOG_SVC_INFO("System indicator: RUNNING_AP_ON (LED solid on)");
            }
            break;
            
        case SYSTEM_INDICATOR_FACTORY_RESET:
            // Fast blink 5Hz - 5 blinks per second (100ms on + 100ms off)
            result = device_service_led_blink(0, 100);  // 0 = infinite blink
            if (result == AICAM_OK) {
                LOG_SVC_INFO("System indicator: FACTORY_RESET (5Hz fast blink)");
            }
            break;
            
        default:
            return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (result == AICAM_OK) {
        g_device_service.indicator_state = state;
    }
    
    return result;
}

system_indicator_state_t device_service_get_indicator_state(void)
{
    return g_device_service.indicator_state;
}
 
 
