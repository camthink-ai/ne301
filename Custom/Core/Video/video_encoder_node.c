/**
 * @file video_encoder_node.c
 * @brief Video Encoder Node Implementation
 * @details Encoder node with zero-copy frame processing and Video Hub integration
 */

#include "video_encoder_node.h"
#include "video_frame_mgr.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "enc.h"
#include "pixel_format_map.h"
#include <string.h>
#include <stdio.h>
#include "websocket_stream_server.h"
#include "camera.h"
#include "h264encapi.h"
#include "Services/Video/video_stream_hub.h"

/* ==================== Internal Function Declarations ==================== */

static aicam_result_t video_encoder_node_init_callback(video_node_t *node);
static aicam_result_t video_encoder_node_deinit_callback(video_node_t *node);
static aicam_result_t video_encoder_node_process_callback(video_node_t *node,
                                                         video_frame_t **input_frames,
                                                         uint32_t input_count,
                                                         video_frame_t **output_frames,
                                                         uint32_t *output_count);
static aicam_result_t video_encoder_node_control_callback(video_node_t *node,
                                                         uint32_t cmd,
                                                         void *param);

static aicam_result_t video_encoder_start_device(video_encoder_node_data_t *data);
static aicam_result_t video_encoder_stop_device(video_encoder_node_data_t *data);
static aicam_result_t video_encoder_encode_frame_zero_copy(video_encoder_node_data_t *data, 
                                                          video_frame_t *input_frame, 
                                                          video_frame_t **output_frame);

/* ==================== API Implementation ==================== */

void video_encoder_get_default_config(video_encoder_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(video_encoder_config_t));
    config->width = VENC_DEFAULT_WIDTH;
    config->height = VENC_DEFAULT_HEIGHT;
    config->fps = VENC_DEFAULT_FPS;
    config->input_type = VENC_DEFAULT_INPUT_TYPE;
    config->quality = 80;
    config->bitrate = 2000;
}

video_node_t* video_encoder_node_create(const char *name, const video_encoder_config_t *config)
{
    if (!name || !config) {
        LOG_CORE_ERROR("Invalid parameters for encoder node creation");
        return NULL;
    }
    
    video_node_t *node = video_node_create(name, VIDEO_NODE_TYPE_ENCODER);
    if (!node) {
        LOG_CORE_ERROR("Failed to create encoder node");
        return NULL;
    }
    
    video_encoder_node_data_t *data = buffer_calloc(1, sizeof(video_encoder_node_data_t));
    if (!data) {
        LOG_CORE_ERROR("Failed to allocate encoder node data");
        video_node_destroy(node);
        return NULL;
    }
    
    memset(data, 0, sizeof(video_encoder_node_data_t));
    memcpy(&data->config, config, sizeof(video_encoder_config_t));
    
    video_node_callbacks_t callbacks = {
        .init = video_encoder_node_init_callback,
        .deinit = video_encoder_node_deinit_callback,
        .process = video_encoder_node_process_callback,
        .control = video_encoder_node_control_callback
    };
    
    aicam_result_t result = video_node_set_callbacks(node, &callbacks);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to set encoder node callbacks");
        buffer_free(data);
        video_node_destroy(node);
        return NULL;
    }
    
    video_node_set_private_data(node, data);
    LOG_CORE_INFO("Encoder node created: %s", name);
    return node;
}

aicam_result_t video_encoder_node_set_config(video_node_t *node, const video_encoder_config_t *config)
{
    if (!node || !config) return AICAM_ERROR_INVALID_PARAM;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (data->is_running) video_encoder_stop_device(data);
    
    memcpy(&data->config, config, sizeof(video_encoder_config_t));
    
    if (data->is_running) return video_encoder_start_device(data);
    
    return AICAM_OK;
}

aicam_result_t video_encoder_node_get_config(video_node_t *node, video_encoder_config_t *config)
{
    if (!node || !config) return AICAM_ERROR_INVALID_PARAM;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    memcpy(config, &data->config, sizeof(video_encoder_config_t));
    return AICAM_OK;
}

aicam_result_t video_encoder_node_get_stats(video_node_t *node, video_encoder_stats_t *stats)
{
    if (!node || !stats) return AICAM_ERROR_INVALID_PARAM;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    memcpy(stats, &data->stats, sizeof(video_encoder_stats_t));
    return AICAM_OK;
}

aicam_result_t video_encoder_node_reset_stats(video_node_t *node)
{
    if (!node) return AICAM_ERROR_INVALID_PARAM;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    memset(&data->stats, 0, sizeof(video_encoder_stats_t));
    return AICAM_OK;
}

aicam_result_t video_encoder_node_start(video_node_t *node)
{
    if (!node) return AICAM_ERROR_INVALID_PARAM;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (data->is_running) return AICAM_OK;
    
    return video_encoder_start_device(data);
}

aicam_result_t video_encoder_node_stop(video_node_t *node)
{
    if (!node) return AICAM_ERROR_INVALID_PARAM;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (!data->is_running) return AICAM_OK;
    
    return video_encoder_stop_device(data);
}

aicam_bool_t video_encoder_node_is_running(video_node_t *node)
{
    if (!node) return AICAM_FALSE;
    
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    return data ? data->is_running : AICAM_FALSE;
}

/* ==================== Callback Functions ==================== */

static aicam_result_t video_encoder_node_init_callback(video_node_t *node)
{
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    data->encoder_dev = device_find_pattern(ENC_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!data->encoder_dev) {
        LOG_CORE_ERROR("Encoder device not found");
        return AICAM_ERROR;
    }
    
    device_ioctl(data->encoder_dev, ENC_CMD_GET_PARAM, 
                (uint8_t *)&data->enc_param, sizeof(enc_param_t));
    
    data->enc_param.width = data->config.width;
    data->enc_param.height = data->config.height;
    data->enc_param.fps = data->config.fps;
    data->enc_param.bpp = ENC_BYTES_PER_PIXEL(data->config.input_type);
    data->enc_param.input_type = data->config.input_type;
    
    device_ioctl(data->encoder_dev, ENC_CMD_SET_PARAM, 
                (uint8_t *)&data->enc_param, sizeof(enc_param_t));
    
    LOG_CORE_INFO("Encoder node initialized: %dx%d@%dfps", 
                  data->config.width, data->config.height, data->config.fps);
    
    data->is_initialized = AICAM_TRUE;
    video_encoder_start_device(data);
    return AICAM_OK;
}

static aicam_result_t video_encoder_node_deinit_callback(video_node_t *node)
{
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    if (data->is_running) video_encoder_stop_device(data);
    
    data->is_initialized = AICAM_FALSE;
    LOG_CORE_INFO("Encoder node deinitialized");
    return AICAM_OK;
}

static aicam_result_t video_encoder_node_process_callback(video_node_t *node,
                                                        video_frame_t **input_frames,
                                                        uint32_t input_count,
                                                        video_frame_t **output_frames,
                                                        uint32_t *output_count)
{
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data || !input_frames || !output_frames || !output_count) return AICAM_ERROR_INVALID_PARAM;
    
    *output_count = 0;
    if (!data->is_running) return AICAM_OK;
    
    for (uint32_t i = 0; i < input_count && i < 1; i++) {
        if (input_frames[i]) {
            video_frame_t *output_frame = NULL;
            if (video_encoder_encode_frame_zero_copy(data, input_frames[i], &output_frame) == AICAM_OK && output_frame) {
                output_frames[*output_count] = output_frame;
                (*output_count)++;
            }
        }
    }
    
    return AICAM_OK;
}

static aicam_result_t video_encoder_node_control_callback(video_node_t *node,
                                                        uint32_t cmd,
                                                        void *param)
{
    video_encoder_node_data_t *data = (video_encoder_node_data_t*)video_node_get_private_data(node);
    if (!data) return AICAM_ERROR_INVALID_PARAM;
    
    switch (cmd) {
        case ENCODER_CMD_START_ENCODE:
            return video_encoder_start_device(data);
            
        case ENCODER_CMD_STOP_ENCODE:
            return video_encoder_stop_device(data);
            
        case ENCODER_CMD_SET_QUALITY:
            if (param) {
                data->config.quality = *(uint32_t*)param;
                return video_encoder_node_set_config(node, &data->config);
            }
            break;
            
        case ENCODER_CMD_SET_BITRATE:
            if (param) {
                data->config.bitrate = *(uint32_t*)param;
                return video_encoder_node_set_config(node, &data->config);
            }
            break;
            
        case ENCODER_CMD_GET_PARAM:
            if (param) {
                memcpy(param, &data->enc_param, sizeof(enc_param_t));
                return AICAM_OK;
            }
            break;
            
        default:
            LOG_CORE_WARN("Unknown encoder command: 0x%x", cmd);
            break;
    }
    
    return AICAM_ERROR_INVALID_PARAM;
}

/* ==================== Internal Functions ==================== */

static aicam_result_t video_encoder_start_device(video_encoder_node_data_t *data)
{
    if (!data || !data->is_initialized) return AICAM_ERROR_INVALID_PARAM;
    if (data->is_running) return AICAM_OK;
    
    aicam_result_t result = device_start(data->encoder_dev);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to start encoder: %d", result);
        return result;
    }
    
    data->is_running = AICAM_TRUE;
    LOG_CORE_INFO("Encoder started: %dx%d@%dfps", 
                  data->config.width, data->config.height, data->config.fps);
    return AICAM_OK;
}

static aicam_result_t video_encoder_stop_device(video_encoder_node_data_t *data)
{
    if (!data || !data->is_running) return AICAM_OK;
    
    aicam_result_t result = device_stop(data->encoder_dev);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to stop encoder: %d", result);
        return result;
    }
    
    data->is_running = AICAM_FALSE;
    LOG_CORE_INFO("Encoder stopped");
    return AICAM_OK;
}

static aicam_result_t video_encoder_encode_frame_zero_copy(video_encoder_node_data_t *data, 
                                                          video_frame_t *input_frame, 
                                                          video_frame_t **output_frame)
{
    if (!data || !input_frame || !output_frame) return AICAM_ERROR_INVALID_PARAM;
    
    // Input frame to encoder
    aicam_result_t result = device_ioctl(data->encoder_dev, ENC_CMD_INPUT_BUFFER, 
                                        input_frame->data, input_frame->info.size);
    if (result != AICAM_OK) {
        data->stats.encode_errors++;
        return result;
    }
    
    // Get encoded output
    enc_out_frame_t enc_frame = {0};
    result = device_ioctl(data->encoder_dev, ENC_CMD_OUTPUT_FRAME, (uint8_t *)&enc_frame, 0);
    if (result != AICAM_OK || enc_frame.data_size == 0) {
        data->stats.encode_errors++;
        // LOG_CORE_WARN("Encode failed: result=%d, data_size=%lu, errors=%lu", 
        //                result, (unsigned long)enc_frame.data_size, 
        //                (unsigned long)data->stats.encode_errors);
        return AICAM_ERROR;
    }

    aicam_bool_t is_keyframe = (enc_frame.frame_info.codingType == H264ENC_INTRA_FRAME);
    uint8_t *frame_data = enc_frame.frame_buffer + enc_frame.header_size;
    uint32_t frame_size = enc_frame.data_size;
    
    // Distribute via Video Hub (preferred mode)
    if (video_hub_is_initialized() && video_hub_has_subscribers()) {
        result = video_hub_inject_frame(
            frame_data, frame_size,
            input_frame->info.timestamp,
            is_keyframe,
            enc_frame.header_size,  // Reserved header space for zero-copy
            input_frame->info.width,
            input_frame->info.height
        );
        
        if (result == AICAM_OK) {
            data->stats.frames_encoded++;
            data->stats.total_bytes_encoded += frame_size;
        }
    } else {
        // Fallback: direct WebSocket send (legacy mode)
        websocket_frame_type_t frame_type = is_keyframe ? 
            WS_FRAME_TYPE_H264_KEY : WS_FRAME_TYPE_H264_DELTA;
        
        websocket_stream_server_send_frame_with_encoder_info(
            enc_frame.frame_buffer, 
            enc_frame.header_size + enc_frame.data_size, 
            input_frame->info.timestamp, 
            frame_type, 
            input_frame->info.width, 
            input_frame->info.height,
            &enc_frame.frame_info);
        
        data->stats.frames_encoded++;
        data->stats.total_bytes_encoded += frame_size;
    }
    
    *output_frame = NULL;
    return AICAM_OK;
}
