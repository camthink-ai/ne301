/**
 * @file video_camera_node.c
 * @brief Camera Source Node Implementation
 * @details Simple camera node based on driver_test.c device interaction with zero-copy
 */

#include "video_camera_node.h"
#include "video_frame_mgr.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "camera.h"
#include <string.h>
#include <stdio.h>
#include "nn.h"
#include "drtc.h"

/* ==================== Global Camera Device Reference ==================== */

// Global camera device reference for buffer return callback
static device_t *g_camera_device = NULL;
static aicam_bool_t g_ai_enabled = AICAM_FALSE;

/* ==================== Internal Function Declarations ==================== */

static aicam_result_t video_camera_node_init_callback(video_node_t *node);
static aicam_result_t video_camera_node_deinit_callback(video_node_t *node);
static aicam_result_t video_camera_node_process_callback(video_node_t *node,
                                                        video_frame_t **input_frames,
                                                        uint32_t input_count,
                                                        video_frame_t **output_frames,
                                                        uint32_t *output_count);
static aicam_result_t video_camera_node_control_callback(video_node_t *node,
                                                        uint32_t cmd,
                                                        void *param);

static aicam_result_t video_camera_start_device(video_camera_node_data_t *data);
static aicam_result_t video_camera_stop_device(video_camera_node_data_t *data);
static aicam_result_t video_camera_capture_frame_zero_copy(video_camera_node_data_t *data, video_frame_t **output_frame);

/* ==================== Zero-Copy Buffer Return Callback ==================== */

/**
 * @brief Callback to return buffer to camera hardware
 * @param buffer Hardware buffer to return
 */
static void camera_buffer_return_callback(uint8_t *buffer)
{
    // This callback is called when the frame is no longer needed
    // We need to return the buffer to the camera hardware
    
    if (g_camera_device) {
        //if(g_ai_enabled == AICAM_FALSE) {
            device_ioctl(g_camera_device, CAM_CMD_RETURN_PIPE1_BUFFER, buffer, 0);
        // } else {
        //     device_ioctl(g_camera_device, CAM_CMD_RETURN_PIPE2_BUFFER, buffer, 0);
        // }
    }
}

/* ==================== API Implementation ==================== */

void video_camera_get_default_config(video_camera_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(video_camera_config_t));
    config->width = 1280;
    config->height = 720;
    config->fps = 30;
    config->format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    config->bpp = 2;
    config->ai_enabled = AICAM_FALSE;
    config->overlay_results = AICAM_TRUE;
}

video_node_t* video_camera_node_create(const char *name, const video_camera_config_t *config) {
    if (!name || !config) {
        LOG_CORE_ERROR("Invalid parameters for camera node creation");
        return NULL;
    }
    
    // Create node
    video_node_t *node = video_node_create(name, VIDEO_NODE_TYPE_SOURCE);
    if (!node) {
        LOG_CORE_ERROR("Failed to create camera node");
        return NULL;
    }
    
    // Allocate private data
    video_camera_node_data_t *data = buffer_calloc(1, sizeof(video_camera_node_data_t));
    if (!data) {
        LOG_CORE_ERROR("Failed to allocate camera node data");
        video_node_destroy(node);
        return NULL;
    }
    
    // Initialize private data
    memset(data, 0, sizeof(video_camera_node_data_t));
    memcpy(&data->config, config, sizeof(video_camera_config_t));
    
    // Set node callbacks
    video_node_callbacks_t callbacks = {
        .init = video_camera_node_init_callback,
        .deinit = video_camera_node_deinit_callback,
        .process = video_camera_node_process_callback,
        .control = video_camera_node_control_callback
    };
    
    aicam_result_t result = video_node_set_callbacks(node, &callbacks);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to set camera node callbacks");
        buffer_free(data);
        video_node_destroy(node);
        return NULL;
    }
    
    // Set private data
    video_node_set_private_data(node, data);
    
    LOG_CORE_INFO("Camera node created: %s", name);
    return node;
}


aicam_result_t video_camera_node_set_config(video_node_t *node, const video_camera_config_t *config) {
    if (!node || !config) return AICAM_ERROR_INVALID_PARAM;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    // Stop camera if running
    // if (data->is_running) {
    //     video_camera_stop_device(data);
    // }
    
    // Update configuration
    data->config.width = config->width;
    data->config.height = config->height;
    data->config.fps = config->fps;
    data->config.format = config->format;
    data->config.bpp = config->bpp;
    data->config.ai_enabled = config->ai_enabled;
    data->config.overlay_results = config->overlay_results;
    LOG_CORE_INFO("Camera node config updated: %dx%d@%dfps, format=%d, bpp=%d, ai_enabled=%d, overlay_results=%d",
                  data->config.width, data->config.height, data->config.fps,
                  data->config.format, data->config.bpp, data->config.ai_enabled,
                  data->config.overlay_results);
    
    // Restart camera if it was running
    // if (data->is_running) {
    //     return video_camera_start_device(data);
    // }
    
    return AICAM_OK;
}

aicam_result_t video_camera_node_get_config(video_node_t *node, video_camera_config_t *config) {
    if (!node || !config) return AICAM_ERROR_INVALID_PARAM;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    memcpy(config, &data->config, sizeof(video_camera_config_t));
    return AICAM_OK;
}

aicam_result_t video_camera_node_get_stats(video_node_t *node, video_camera_stats_t *stats) {
    if (!node || !stats) return AICAM_ERROR_INVALID_PARAM;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    memcpy(stats, &data->stats, sizeof(video_camera_stats_t));
    return AICAM_OK;
}

aicam_result_t video_camera_node_reset_stats(video_node_t *node) {
    if (!node) return AICAM_ERROR_INVALID_PARAM;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    memset(&data->stats, 0, sizeof(video_camera_stats_t));
    return AICAM_OK;
}

aicam_result_t video_camera_node_start(video_node_t *node) {
    if (!node) return AICAM_ERROR_INVALID_PARAM;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (data->is_running) {
        LOG_CORE_WARN("Camera already running");
        return AICAM_OK;
    }
    
    return video_camera_start_device(data);
}

aicam_result_t video_camera_node_stop(video_node_t *node) {
    if (!node) return AICAM_ERROR_INVALID_PARAM;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (!data->is_running) {
        LOG_CORE_WARN("Camera not running");
        return AICAM_OK;
    }
    
    return video_camera_stop_device(data);
}

aicam_bool_t video_camera_node_is_running(video_node_t *node) {
    if (!node) return AICAM_FALSE;
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_FALSE;
    
    return data->is_running;
}

aicam_result_t video_camera_node_set_ai_callback(video_node_t *node, 
                                                ai_draw_callback_t callback, 
                                                void *user_data) {
    if (!node) {
        LOG_CORE_ERROR("Invalid camera node for AI callback");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        LOG_CORE_ERROR("Invalid camera node data for AI callback");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    data->ai_draw_callback = callback;
    data->ai_callback_user_data = user_data;
    
    if (callback) {
        LOG_CORE_INFO("AI drawing callback registered for camera node");
    } else {
        LOG_CORE_INFO("AI drawing callback unregistered for camera node");
    }
    
    return AICAM_OK;
}

/* ==================== Callback Functions ==================== */

static aicam_result_t video_camera_node_init_callback(video_node_t *node) {
    LOG_CORE_INFO("Camera node init callback");
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    // Find camera device (based on driver_test.c)
    data->camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!data->camera_dev) {
        LOG_CORE_ERROR("Camera device not found");
        return AICAM_ERROR;
    }
    
    // Set global camera device reference for buffer return callback
    g_camera_device = data->camera_dev;
    g_ai_enabled = data->config.ai_enabled;
    

    device_ioctl(data->camera_dev, CAM_CMD_GET_SENSOR_PARAM, 
                (uint8_t *)&data->sensor_param, sizeof(sensor_params_t));


    device_ioctl(data->camera_dev, CAM_CMD_GET_PIPE1_PARAM, 
                (uint8_t *)&data->pipe_param, sizeof(pipe_params_t));


    
    LOG_CORE_INFO("Camera node initialized: %dx%d@%dfps, format=%d", 
                  data->config.width, data->config.height, data->config.fps, data->config.format);
    
    data->is_initialized = AICAM_TRUE;

    video_camera_start_device(data);
    return AICAM_OK;
}

static aicam_result_t video_camera_node_deinit_callback(video_node_t *node) {
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    // stop camera if running
    if (data->is_running) {
        LOG_CORE_INFO("Camera node stop device");
        video_camera_stop_device(data);
    }
    
    data->is_initialized = AICAM_FALSE;
    LOG_CORE_INFO("Camera node deinitialized");
    return AICAM_OK;
}

static aicam_result_t video_camera_node_process_callback(video_node_t *node,
                                                        video_frame_t **input_frames,
                                                        uint32_t input_count,
                                                        video_frame_t **output_frames,
                                                        uint32_t *output_count) {

    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data || !output_frames || !output_count) return AICAM_ERROR_INVALID_PARAM;

    
    // Source node doesn't use input frames
    (void)input_frames;
    (void)input_count;
    
    if (!data->is_running) {
        *output_count = 0;
        LOG_CORE_INFO("Camera node process callback: not running");
        return AICAM_OK;
    }
    
    // Capture frame with zero-copy
    video_frame_t *output_frame = NULL;
    aicam_result_t result = video_camera_capture_frame_zero_copy(data, &output_frame);
    if (result == AICAM_OK && output_frame) {
        output_frames[0] = output_frame;
        *output_count = 1;
    } else {
        *output_count = 0;
    }

    
    return AICAM_OK;
}

static aicam_result_t video_camera_node_control_callback(video_node_t *node,
                                                        uint32_t cmd,
                                                        void *param) {
    video_camera_node_data_t *data = (video_camera_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;

    LOG_CORE_INFO("Camera node control callback");
    switch (cmd) {
        case CAMERA_CMD_START_CAPTURE:
            return video_camera_start_device(data);
            
        case CAMERA_CMD_STOP_CAPTURE:
            return video_camera_stop_device(data);
            
        case CAMERA_CMD_SET_RESOLUTION:
            if (param) {
                uint32_t *resolution = (uint32_t*)param;
                data->config.width = resolution[0];
                data->config.height = resolution[1];
                return video_camera_node_set_config(node, &data->config);
            }
            break;
            
        case CAMERA_CMD_SET_FPS:
            if (param) {
                data->config.fps = *(uint32_t*)param;
                return video_camera_node_set_config(node, &data->config);
            }
            break;
            
        case CAMERA_CMD_GET_SENSOR_INFO:
            if (param) {
                memcpy(param, &data->sensor_param, sizeof(sensor_params_t));
                return AICAM_OK;
            }
            break;
            
        default:
            LOG_CORE_WARN("Unknown camera control command: 0x%x", cmd);
            break;
    }
    
    return AICAM_ERROR_INVALID_PARAM;
}

/* ==================== Internal Functions ==================== */

static aicam_result_t video_camera_start_device(video_camera_node_data_t *data) {
    if (!data || !data->is_initialized) return AICAM_ERROR_INVALID_PARAM;
    LOG_CORE_INFO("Camera node start device");
    
    if (data->is_running) {
        LOG_CORE_WARN("Camera already running");
        return AICAM_OK;
    }
    
    // Update pipeline parameters if needed (based on driver_test.c)
    if (data->pipe_param.width != data->config.width ||
       data->pipe_param.height != data->config.height ||
       data->pipe_param.fps != data->config.fps) {
        
        data->pipe_param.width = data->config.width;
        data->pipe_param.height = data->config.height;
        data->pipe_param.fps = data->config.fps;
        data->pipe_param.format = data->config.format;
        data->pipe_param.bpp = data->config.bpp;
        data->pipe_param.buffer_nb = 3;

        LOG_CORE_INFO("Camera node set pipe param: %dx%d@%dfps, format=%d, bpp=%d", 
                      data->pipe_param.width, data->pipe_param.height, data->pipe_param.fps, 
                      data->pipe_param.format, data->pipe_param.bpp);
        
        //if (data->config.ai_enabled == AICAM_FALSE) {
            device_ioctl(data->camera_dev, CAM_CMD_SET_PIPE1_PARAM, 
                        (uint8_t *)&data->pipe_param, sizeof(pipe_params_t));
        // } else {
        //     device_ioctl(data->camera_dev, CAM_CMD_SET_PIPE2_PARAM, 
        //                 (uint8_t *)&data->pipe_param, sizeof(pipe_params_t));
        // }
   }
    
    // Start camera device (based on driver_test.c)
    // aicam_result_t result = device_start(data->camera_dev);
    // if (result != AICAM_OK) {
    //     LOG_CORE_ERROR("Failed to start camera device: %d", result);
    //     return result;
    // }

    
    data->is_running = AICAM_TRUE;
    data->frame_sequence = 0;
    
    LOG_CORE_INFO("Camera started: %dx%d@%dfps", 
                  data->config.width, data->config.height, data->config.fps);
    
    return AICAM_OK;
}

static aicam_result_t video_camera_stop_device(video_camera_node_data_t *data) {
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (!data->is_running) {
        LOG_CORE_WARN("Camera not running");
        return AICAM_OK;
    }
    
    // Stop camera device (based on driver_test.c)
    aicam_result_t result = device_stop(data->camera_dev);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to stop camera device: %d", result);
        return result;
    }
    
    data->is_running = AICAM_FALSE;
    
    LOG_CORE_INFO("Camera stopped");
    return AICAM_OK;
}

static aicam_result_t video_camera_capture_frame_zero_copy(video_camera_node_data_t *data, video_frame_t **output_frame) {
    if (!data || !output_frame) return AICAM_ERROR_INVALID_PARAM;
    
    uint64_t start_time = osKernelGetTickCount();
    
    // Get frame buffer from camera (hardware buffer) - NO COPYING!
    camera_buffer_with_frame_id_t camera_buffer_with_frame_id;
    int result = device_ioctl(data->camera_dev, CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID, 
                            (uint8_t *)&camera_buffer_with_frame_id, 0);


    if (result != AICAM_OK) {
        data->stats.buffer_underruns++;
        return AICAM_ERROR;
    }
   
    data->current_buffer = camera_buffer_with_frame_id.buffer;
    data->frame_id = camera_buffer_with_frame_id.frame_id;

    //if ai enabled and overlay allowed, draw ai result onto the frame
    if (data->config.ai_enabled == AICAM_TRUE && data->config.overlay_results == AICAM_TRUE &&
        data->ai_draw_callback) {
        result = device_ioctl(data->camera_dev, CAM_CMD_LOCK_PIPE1_BUFFER, 
                            camera_buffer_with_frame_id.buffer, 0);
        if (result == AICAM_OK) {
            // Call AI drawing callback to draw results on frame buffer
            aicam_result_t callback_ret = data->ai_draw_callback(data->current_buffer, 
                                                            data->config.width,
                                                            data->config.height, 
                                                            data->frame_id,
                                                            data->ai_callback_user_data);
            if (callback_ret != AICAM_OK) {
                LOG_CORE_WARN("AI drawing callback failed: %d", callback_ret);
            }
            // Unlock buffer after AI drawing
            device_ioctl(data->camera_dev, CAM_CMD_UNLOCK_PIPE1_BUFFER, 
                        camera_buffer_with_frame_id.buffer, 0);
        } else {
            LOG_CORE_WARN("Failed to lock camera buffer for AI drawing: %d", result);
        }
    }
    
    // Prepare frame information
    video_frame_info_t frame_info = {
        .width = data->config.width,
        .height = data->config.height,
        .format = (video_format_t)data->config.format,
        .stride = data->config.width * data->config.bpp,
        .size = camera_buffer_with_frame_id.size,
        .timestamp = rtc_get_local_timestamp(),
        .sequence = data->frame_sequence++
    };
    
    // Create zero-copy frame (directly uses hardware buffer - NO MEMCPY!)
    video_frame_t *frame = video_frame_create_zero_copy(&frame_info,
                                                       data->current_buffer,  // Direct hardware buffer
                                                       camera_buffer_with_frame_id.size,
                                                       camera_buffer_return_callback);
    if (!frame) {
        device_ioctl(data->camera_dev, CAM_CMD_RETURN_PIPE1_BUFFER, data->current_buffer, 0);
        data->stats.capture_errors++;
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Update statistics
    data->stats.frames_captured++;
    uint64_t capture_time = osKernelGetTickCount() - start_time;
    data->stats.avg_capture_time_us = (data->stats.avg_capture_time_us + capture_time) / 2;
    if (capture_time > data->stats.max_capture_time_us) {
        data->stats.max_capture_time_us = capture_time;
    }
    
    *output_frame = frame;
    return AICAM_OK;
}
