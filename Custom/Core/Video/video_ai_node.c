/**
 * @file video_ai_node.c
 * @brief Video AI Processing Node Implementation
 * @details AI inference node based on nn.h/nn.c device interaction with zero-copy
 */

#include "video_ai_node.h"
#include "video_frame_mgr.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "nn.h"
#include "ai_draw_service.h"
#include <string.h>
#include <stdio.h>
#include "mem_map.h"
#include "cmsis_os2.h"
#include "upgrade_manager.h"
#include "drtc.h"


/* ==================== Internal Function Declarations ==================== */
static aicam_result_t video_ai_node_load_model_active(video_node_t *node);
static aicam_result_t video_ai_node_init_callback(video_node_t *node);
static aicam_result_t video_ai_node_deinit_callback(video_node_t *node);
static aicam_result_t video_ai_node_process_callback(video_node_t *node,
                                                    video_frame_t **input_frames,
                                                    uint32_t input_count,
                                                    video_frame_t **output_frames,
                                                    uint32_t *output_count);
static aicam_result_t video_ai_node_control_callback(video_node_t *node,
                                                    uint32_t cmd,
                                                    void *param);

static aicam_result_t video_ai_start_device(video_ai_node_data_t *data);
static aicam_result_t video_ai_stop_device(video_ai_node_data_t *data);
static aicam_result_t video_ai_process_frame(video_ai_node_data_t *data, 
                                            video_frame_t **output_frame);
static aicam_result_t video_ai_init_draw_service(video_ai_node_data_t *data);
static aicam_result_t video_ai_deinit_draw_service(video_ai_node_data_t *data);


/* ==================== API Implementation ==================== */


void video_ai_get_default_config(video_ai_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(video_ai_config_t));
    config->width = PIPE1_DEFAULT_WIDTH;
    config->height = PIPE1_DEFAULT_HEIGHT;
    config->fps = CAMERA_FPS;
    config->input_format = PIPE1_DEFAULT_FORMAT;
    config->bpp = PIPE1_DEFAULT_BPP;
    config->confidence_threshold = 50;
    config->nms_threshold = 50;
    config->max_detections = 32;
    config->processing_interval = 1;  // Process every frame
    config->enabled = AICAM_TRUE;
    config->overlay_results = AICAM_TRUE;  // Overlay on by default (stock behavior)
    config->enable_drawing = AICAM_TRUE;
    
    // Initialize drawing configuration
    ai_draw_get_default_config(&config->draw_config);
}

video_node_t* video_ai_node_create(const char *name, const video_ai_config_t *config) {
    if (!name || !config) {
        LOG_CORE_ERROR("Invalid parameters for AI node creation");
        return NULL;
    }
    
    // Create node
    video_node_t *node = video_node_create(name, VIDEO_NODE_TYPE_SOURCE);
    if (!node) {
        LOG_CORE_ERROR("Failed to create AI node");
        return NULL;
    }
    
    // Allocate private data
    video_ai_node_data_t *data = buffer_calloc(1, sizeof(video_ai_node_data_t));
    if (!data) {
        LOG_CORE_ERROR("Failed to allocate AI node data");
        video_node_destroy(node);
        return NULL;
    }
    
    // Initialize private data
    memset(data, 0, sizeof(video_ai_node_data_t));
    memcpy(&data->config, config, sizeof(video_ai_config_t));
    
    // Initialize NN result cache (circular queue)
    memset(data->nn_result_cache, 0, sizeof(data->nn_result_cache));
    data->write_index = 0;
    data->read_index = 0;
    data->cache_count = 0;
    data->cache_initialized = AICAM_FALSE;
    
    // Create cache mutex
    data->cache_mutex = osMutexNew(NULL);
    if (!data->cache_mutex) {
        LOG_CORE_ERROR("Failed to create cache mutex");
        buffer_free(data);
        video_node_destroy(node);
        return NULL;
    }
    
    // Set node callbacks
    video_node_callbacks_t callbacks = {
        .init = video_ai_node_init_callback,
        .deinit = video_ai_node_deinit_callback,
        .process = video_ai_node_process_callback,
        .control = video_ai_node_control_callback
    };
    
    aicam_result_t result = video_node_set_callbacks(node, &callbacks);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to set AI node callbacks");
        buffer_free(data);
        video_node_destroy(node);
        return NULL;
    }
    
    // Set private data
    result = video_node_set_private_data(node, data);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to set AI node private data");
        buffer_free(data);
        video_node_destroy(node);
        return NULL;
    }
    
    LOG_CORE_INFO("AI node created: %s", name);
    return node;
}



aicam_result_t video_ai_node_set_config(video_node_t *node, const video_ai_config_t *config) {
    if (!node || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(&data->config, config, sizeof(video_ai_config_t));

    
    LOG_CORE_INFO("AI node config updated");
    return AICAM_OK;
}

aicam_result_t video_ai_node_get_config(video_node_t *node, video_ai_config_t *config) {
    if (!node || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(config, &data->config, sizeof(video_ai_config_t));
    return AICAM_OK;
}

aicam_result_t video_ai_node_get_stats(video_node_t *node, video_ai_stats_t *stats) {
    if (!node || !stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(stats, &data->stats, sizeof(video_ai_stats_t));
    return AICAM_OK;
}

aicam_result_t video_ai_node_reset_stats(video_node_t *node) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memset(&data->stats, 0, sizeof(video_ai_stats_t));
    LOG_CORE_INFO("AI node statistics reset");
    return AICAM_OK;
}

aicam_result_t video_ai_node_start(video_node_t *node) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (data->is_running) {
        LOG_CORE_WARN("AI node already running");
        return AICAM_OK;
    }
    
    aicam_result_t result = video_ai_start_device(data);
    if (result == AICAM_OK) {
        data->is_running = AICAM_TRUE;
        LOG_CORE_INFO("AI node started");
    }
    
    return result;
}

aicam_result_t video_ai_node_stop(video_node_t *node) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!data->is_running) {
        LOG_CORE_WARN("AI node not running");
        return AICAM_OK;
    }
    
    aicam_result_t result = video_ai_stop_device(data);
    if (result == AICAM_OK) {
        data->is_running = AICAM_FALSE;
        LOG_CORE_INFO("AI node stopped");
    }
    
    return result;
}

aicam_bool_t video_ai_node_is_running(video_node_t *node) {
    if (!node) {
        return AICAM_FALSE;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_FALSE;
    }
    
    return data->is_running;
}

aicam_result_t video_ai_node_load_model(video_node_t *node, uintptr_t model_ptr) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if(model_ptr == 0) {
        LOG_CORE_INFO("Load active AI model");
        aicam_result_t result = video_ai_node_load_model_active(node);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to load active AI model: %d", result);
            return result;
        }
        return AICAM_OK;
    }
    
    int nn_ret = nn_load_model(model_ptr);
    if (nn_ret == 0) {
        // Get model information
        nn_get_model_info(&data->model_info);
        LOG_CORE_INFO("AI model loaded: %s", data->model_info.name);
        json_config_sync_ai_pipe_nvs_from_input_size(data->model_info.input_width, data->model_info.input_height);
        return AICAM_OK;
    } else {
        LOG_CORE_ERROR("Failed to load AI model: %d", nn_ret);
        return AICAM_ERROR;
    }
}

aicam_result_t video_ai_node_unload_model(video_node_t *node) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    int nn_ret = nn_unload_model();
    if (nn_ret == 0) {
        memset(&data->model_info, 0, sizeof(nn_model_info_t));
        LOG_CORE_INFO("AI model unloaded");
        return AICAM_OK;
    } else {
        LOG_CORE_ERROR("Failed to unload AI model: %d", nn_ret);
        return AICAM_ERROR;
    }
}

aicam_result_t video_ai_node_get_model_info(video_node_t *node, nn_model_info_t *model_info) {
    if (!node || !model_info) {
        LOG_CORE_ERROR("Invalid parameters for AI node get model info");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        LOG_CORE_ERROR("Invalid parameters for AI node get model info");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(model_info, &data->model_info, sizeof(nn_model_info_t));
    return AICAM_OK;
}

aicam_result_t video_ai_node_reload_model(video_node_t *node) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    aicam_result_t nn_ret = video_ai_node_unload_model(node);
    if (nn_ret != AICAM_OK) {
        LOG_CORE_ERROR("Failed to unload AI model: %d", nn_ret);
        return nn_ret;
    }

    //reset cache   
    memset(data->nn_result_cache, 0, sizeof(data->nn_result_cache));
    data->write_index = 0;
    data->read_index = 0;
    data->cache_count = 0;
    data->cache_initialized = AICAM_FALSE;

    nn_ret = video_ai_node_load_model(node, 0);
    if (nn_ret != AICAM_OK) {
        LOG_CORE_ERROR("Failed to load AI model: %d", nn_ret);
        return nn_ret;
    }

    LOG_CORE_INFO("AI model reloaded");

    video_ai_node_init_callback(node);

    return AICAM_OK;
}

aicam_result_t video_ai_node_get_nn_result(video_node_t *node, nn_result_t *result) {
    if (!node || !result) {
        LOG_CORE_ERROR("Invalid parameters for AI node get nn result");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        LOG_CORE_ERROR("Invalid AI node data");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Lock cache mutex
    osStatus_t mutex_status = osMutexAcquire(data->cache_mutex, osWaitForever); 
    if (mutex_status != osOK) {
        LOG_CORE_ERROR("Failed to lock cache mutex: %d", mutex_status);
        return AICAM_ERROR;
    }
    
    // Check if cache is empty
    if (data->cache_count == 0) {
        // Release mutex first
        osMutexRelease(data->cache_mutex);
        LOG_CORE_DEBUG("NN result cache is empty");
        result->od.nb_detect = 0;
        return AICAM_OK;
    }
    
    // Copy the oldest result (FIFO) - extract nn_result_t from nn_result_with_frame_id_t
    memcpy(result, &data->nn_result_cache[data->read_index].result, sizeof(nn_result_t));
    
    // Move read index (circular)
    data->read_index = (data->read_index + 1) % NN_RESULT_CACHE_SIZE;
    data->cache_count--;
    
    // Release cache mutex
    osMutexRelease(data->cache_mutex);
    
    LOG_CORE_DEBUG("Retrieved NN result from cache: %d detections", result->od.nb_detect);
    return AICAM_OK;
}

aicam_result_t video_ai_node_get_best_nn_result(video_node_t *node, nn_result_t *result, uint32_t frame_id) {
    if (!node || !result) {
        LOG_CORE_ERROR("Invalid parameters for AI node get latest nn result");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        LOG_CORE_ERROR("Invalid AI node data");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Lock cache mutex
    osStatus_t mutex_status = osMutexAcquire(data->cache_mutex, osWaitForever); 
    if (mutex_status != osOK) {
        LOG_CORE_ERROR("Failed to lock cache mutex: %d", mutex_status);
        return AICAM_ERROR;
    }
    
    // Check if cache is empty
    if (data->cache_count == 0) {
        // Release mutex first
        osMutexRelease(data->cache_mutex);
        LOG_CORE_DEBUG("NN result cache is empty");
        result->od.nb_detect = 0;
        return AICAM_OK;
    }

    uint32_t best_index = 0;
    uint32_t min_diff = UINT32_MAX;

    for (uint32_t i = 0; i < data->cache_count; i++)
    {
        uint32_t index = (data->write_index + NN_RESULT_CACHE_SIZE - data->cache_count + i) % NN_RESULT_CACHE_SIZE;
        nn_result_with_frame_id_t *entry = &data->nn_result_cache[index];

        uint32_t diff = (entry->frame_id > frame_id) ? (entry->frame_id - frame_id) : (frame_id - entry->frame_id);

        if (diff < min_diff)
        {
            min_diff = diff;
            best_index = index;
        }
    }

    // Copy best match result
    memcpy(result, &data->nn_result_cache[best_index].result, sizeof(nn_result_t));

    // Release cache mutex (no index changes for non-destructive read)
    osMutexRelease(data->cache_mutex);

    //LOG_CORE_DEBUG("Retrieved latest NN result from cache: %d detections", result->od.nb_detect);
    return AICAM_OK;
}

aicam_result_t video_ai_node_set_result_callback(video_node_t *node,
                                                 video_ai_result_callback_t callback,
                                                 void *user_data)
{
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    data->result_callback = callback;
    data->result_callback_user_data = user_data;
    return AICAM_OK;
}

/* ==================== Internal Functions ==================== */

static aicam_result_t video_ai_start_device(video_ai_node_data_t *data) {
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check NN state
    nn_state_t nn_state = nn_get_state();
    if (nn_state == NN_STATE_READY) {
        // Start NN inference
        int nn_ret = nn_start_inference();
        if (nn_ret != 0) {
            LOG_CORE_ERROR("Failed to start NN inference: %d", nn_ret);
            return AICAM_ERROR;
        }
        //LOG_CORE_INFO("NN inference started");
        data->is_running = AICAM_TRUE;
    } else if (nn_state == NN_STATE_UNINIT) {
        LOG_CORE_WARN("NN module not initialized, AI will work in pass-through mode");
    } else {
        LOG_CORE_INFO("NN state: %d", nn_state);
    }
    
    return AICAM_OK;
}

static aicam_result_t video_ai_stop_device(video_ai_node_data_t *data) {
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_CORE_INFO("AI node stop device");

    // if(!data->config.enabled) {
    //     return AICAM_OK;
    // }

    //unload model
    nn_state_t nn_state = nn_get_state();
    
    // Stop NN inference
    if (nn_state == NN_STATE_RUNNING) {
        int nn_ret = nn_stop_inference();
        if (nn_ret != 0) {
            LOG_CORE_ERROR("Failed to stop NN inference: %d", nn_ret);
            return AICAM_ERROR;
        }
        LOG_CORE_INFO("NN inference stopped");
    }

    // nn_state = nn_get_state(); 
    // if(nn_state == NN_STATE_RUNNING || nn_state == NN_STATE_READY) {
    //     nn_unload_model();
    // }
    
    return AICAM_OK;
}

static aicam_result_t video_ai_process_frame(video_ai_node_data_t *data,
                                             video_frame_t **output_frame)
{
    if (!data || !output_frame)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Pace inference to the configured interval. Sleep in bounded slices so
    // config changes and pipeline stop are picked up within ~100 ms, and
    // never inside a camera ioctl (it holds the camera mutex while blocking).
    if (data->config.inference_interval_ms != 0)
    {
        uint32_t elapsed_ms = osKernelGetTickCount() - data->last_inference_tick;
        if (elapsed_ms < data->config.inference_interval_ms)
        {
            uint32_t remaining_ms = data->config.inference_interval_ms - elapsed_ms;
            osDelay(remaining_ms < 100 ? remaining_ms : 100);
            *output_frame = NULL;
            return AICAM_OK;
        }
    }

    // Check processing interval
    if (data->config.processing_interval > 1)
    {
        data->frame_counter++;
        if (data->frame_counter % data->config.processing_interval != 0)
        {
            // Skip this frame
            *output_frame = NULL;
            data->stats.frames_skipped++;
            return AICAM_OK;
        }
    }

    // Check NN state
    nn_state_t nn_state = nn_get_state();
    if (nn_state != NN_STATE_READY && nn_state != NN_STATE_RUNNING)
    {
        LOG_CORE_WARN("NN not ready (state=%d), passing through frame", nn_state);
        *output_frame = NULL;
        data->stats.frames_skipped++;
        return AICAM_OK;
    }

    // get input buffer from pipe2 (use cached device handle)
    device_t *camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!camera_dev)
    {
        LOG_CORE_ERROR("Camera device not found");
        *output_frame = NULL;
        return AICAM_ERROR;
    }

    uint8_t *input_frame_buffer = NULL;
    uint32_t frame_id = 0;
    camera_buffer_with_frame_id_t camera_buffer_with_frame_id;
    int result = device_ioctl(camera_dev, CAM_CMD_GET_PIPE2_BUFFER_WITH_FRAME_ID, 
                            (uint8_t *)&camera_buffer_with_frame_id, 0);


    if (result == AICAM_OK)
    {
        // Reduce logging frequency - only log every 200 frames
        input_frame_buffer = camera_buffer_with_frame_id.buffer;
        frame_id = camera_buffer_with_frame_id.frame_id;
        static uint32_t frame_log_count = 0;
        if ((++frame_log_count % 200) == 1)
        {
            LOG_CORE_DEBUG("Got pipe2 buffer for AI processing: %p (frame %u)", input_frame_buffer, frame_log_count);
        }
    }
    else if (result == AICAM_ERROR_NOT_FOUND)
    {
        *output_frame = NULL;
        return AICAM_OK;
    }
    else
    {
        LOG_CORE_ERROR("Failed to get pipe2 buffer for AI processing, size: %d", camera_buffer_with_frame_id.size);
        *output_frame = NULL;
        return AICAM_ERROR;
    }

    // Prepare NN result structure (use stack allocation for temporary result)
    nn_result_t nn_result;
    memset(&nn_result, 0, sizeof(nn_result_t));


    // Call NN module to process frame
    uint32_t infer_start_tick = osKernelGetTickCount();
    int nn_ret = nn_inference_frame(input_frame_buffer, camera_buffer_with_frame_id.size, &nn_result);
    uint32_t inference_time_ms = osKernelGetTickCount() - infer_start_tick;
    // Anchor pacing to the attempt so inference errors keep the configured cadence
    data->last_inference_tick = infer_start_tick;

    // return pipe2 buffer
    device_ioctl(camera_dev, CAM_CMD_RETURN_PIPE2_BUFFER, input_frame_buffer, 0);

    // Save result to cache if inference was successful
    if (nn_ret == 0)
    {
        // Optimized cache write with reduced lock time
        uint32_t current_write_idx = data->write_index;

        // Prepare result with timestamp and frame_id
        nn_result_with_frame_id_t result_with_ts;
        memcpy(&result_with_ts.result, &nn_result, sizeof(nn_result_t));
        result_with_ts.frame_id = frame_id;

        // Copy result to cache before acquiring lock (reduce critical section)
        memcpy(&data->nn_result_cache[current_write_idx], &result_with_ts, sizeof(nn_result_with_frame_id_t));

        // Lock cache mutex for minimal time
        osStatus_t mutex_status = osMutexAcquire(data->cache_mutex, 10); // Reduced timeout
        if (mutex_status == osOK)
        {
            // Update indices atomically
            data->write_index = (data->write_index + 1) % NN_RESULT_CACHE_SIZE;

            if (data->cache_count >= NN_RESULT_CACHE_SIZE)
            {
                data->read_index = (data->read_index + 1) % NN_RESULT_CACHE_SIZE;
            }
            else
            {
                data->cache_count++;
            }

            if (!data->cache_initialized)
            {
                data->cache_initialized = AICAM_TRUE;
            }

            osMutexRelease(data->cache_mutex);
        }
        else
        {
            // If lock fails, just log once to avoid spam
            static uint32_t lock_fail_count = 0;
            if ((++lock_fail_count % 100) == 1)
            {
                LOG_CORE_WARN("Cache mutex timeout, failed %lu times", lock_fail_count);
            }
        }

        // Hand the fresh result to the registered consumer while the
        // postprocess output buffers still hold this inference's data
        if (data->result_callback)
        {
            data->result_callback(&nn_result, frame_id, inference_time_ms,
                                  data->result_callback_user_data);
        }
    }

    // No output frame generated - results are cached internally
    *output_frame = NULL;

    return AICAM_OK;
}

/* ==================== Callback Functions ==================== */

static aicam_result_t video_ai_node_load_model_active(video_node_t *node) {
    LOG_CORE_INFO("AI node load model");
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    

    uintptr_t model_ptr = json_config_get_ai_1_active() ? AI_1_BASE + 1024 : AI_DEFAULT_BASE + 1024;
    LOG_CORE_INFO("Load model from %p", model_ptr);
    int nn_ret = nn_load_model(model_ptr);
    if(nn_ret != 0) {
        LOG_CORE_ERROR("Failed to load model: %d", nn_ret);
        return AICAM_ERROR;
    }

    if(nn_get_model_info(&data->model_info) != AICAM_OK) {
        LOG_CORE_ERROR("Failed to get model info");
        return AICAM_ERROR;
    }

    json_config_sync_ai_pipe_nvs_from_input_size(data->model_info.input_width, data->model_info.input_height);

    LOG_CORE_INFO("AI model loaded: %dx%d from %p", data->model_info.input_width, data->model_info.input_height, model_ptr);
    return AICAM_OK;
}

static aicam_result_t video_ai_node_init_callback(video_node_t *node) {
    LOG_CORE_INFO("AI node init callback");
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        LOG_CORE_ERROR("Invalid AI node data");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Find AI device
    data->ai_device = device_find_pattern("nn", DEV_TYPE_AI);
    if (!data->ai_device) {
        LOG_CORE_ERROR("AI device not found");
        return AICAM_ERROR;
    }
    
    // Check NN state
    nn_state_t nn_state = nn_get_state();
    if (nn_state == NN_STATE_READY || nn_state == NN_STATE_RUNNING) {
        LOG_CORE_INFO("NN module is ready, AI processing enabled");
        
        // Get model information if available
        nn_get_model_info(&data->model_info);
    }else if(nn_state == NN_STATE_INIT || nn_state == NN_STATE_UNINIT) {
        LOG_CORE_INFO("NN module is initialized, AI node will work in pass-through mode");
        // load active model
        aicam_result_t result = video_ai_node_load_model_active(node);
        if(result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to load active model: %d", result);
            return result;
        }
    }

    aicam_result_t result = video_ai_start_device(data);
    if(result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to start AI device: %d", result);
        return result;
    }

    // set confidence threshold
    nn_set_confidence_threshold((float)data->config.confidence_threshold / 100.0f);
    nn_set_nms_threshold((float)data->config.nms_threshold / 100.0f);

    //set pipe2 config and cache camera device
    device_t* camera_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!camera_dev) {
        LOG_CORE_ERROR("Camera device not found");
        return AICAM_ERROR;
    }
    pipe_params_t pipe_param = {0};
    if(data->config.width != 0 && data->config.height != 0) {
        pipe_param.width = data->model_info.input_width;
        pipe_param.height = data->model_info.input_height;
    } else {
        pipe_param.width = data->config.width;
        pipe_param.height = data->config.height;
    }
    pipe_param.fps = data->config.fps;
    pipe_param.format = data->config.input_format;
    pipe_param.bpp = data->config.bpp;
    pipe_param.buffer_nb = 2;
    printf("Set pipe2 param: %dx%d@%dfps, format=%d, bpp=%d\r\n",
                 pipe_param.width, pipe_param.height, pipe_param.fps, pipe_param.format, pipe_param.bpp);
    device_ioctl(camera_dev, CAM_CMD_SET_PIPE2_PARAM, (uint8_t*)&pipe_param, sizeof(pipe_params_t));


    data->config.width = data->model_info.input_width;
    data->config.height = data->model_info.input_height;
    LOG_CORE_INFO("AI model loaded: %dx%d", data->config.width, data->config.height);

    uint8_t pipe_ctrl = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
    device_ioctl(camera_dev, CAM_CMD_SET_PIPE_CTRL, 
        &pipe_ctrl, 0);


    data->is_initialized = AICAM_TRUE;
    LOG_CORE_INFO("AI node initialized: %dx%d@%dfps, enabled=%d, drawing=%d, nn_state=%d",
                 data->config.width, data->config.height, data->config.fps,
                 data->config.enabled, data->config.enable_drawing, nn_state);
    
    return AICAM_OK;
}

static aicam_result_t video_ai_node_deinit_callback(video_node_t *node) {
    LOG_CORE_INFO("AI node deinit callback");
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    data->is_initialized = AICAM_FALSE;
    data->is_running = AICAM_FALSE;

    // Deinitialize drawing service
    if (data->draw_service_initialized) {
        video_ai_deinit_draw_service(data);
        data->draw_service_initialized = AICAM_FALSE;
    }

    // Stop device if running
    if (data->is_running) {
        video_ai_stop_device(data);
    }
    
    // Clean up cache mutex
    if (data->cache_mutex) {
        osMutexDelete(data->cache_mutex);
        data->cache_mutex = NULL;
    }
    
    // Clear cache
    data->cache_initialized = AICAM_FALSE;
    memset(data->nn_result_cache, 0, sizeof(data->nn_result_cache));
    
    LOG_CORE_INFO("AI node deinitialized");
    return AICAM_OK;
}

static aicam_result_t video_ai_node_process_callback(video_node_t *node,
                                                    video_frame_t **input_frames,
                                                    uint32_t input_count,
                                                    video_frame_t **output_frames,
                                                    uint32_t *output_count) {
    if (!node || !input_frames || !output_frames || !output_count) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data || !data->is_initialized) {
        return AICAM_OK;
    }

    //printf("AI node process callback\r\n");
    
    *output_count = 0;
    
    // Process AI inference
    video_frame_t *output_frame = NULL;
    aicam_result_t result = video_ai_process_frame(data, &output_frame);

    if (result != AICAM_OK) {
        LOG_CORE_ERROR("AI node process failed: %d", result);
        *output_count = 0;
        return result;
    }
    
    // AI node does not generate output frames - results are cached internally
    *output_count = 0;
    //LOG_CORE_INFO("AI node processing completed, results cached");

    return result;
}

static aicam_result_t video_ai_node_control_callback(video_node_t *node,
                                                    uint32_t cmd,
                                                    void *param) {
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    video_ai_node_data_t *data = (video_ai_node_data_t*)video_node_get_private_data(node);
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    switch (cmd) {
        case AI_CMD_START_PROCESSING:
            return video_ai_node_start(node);
            
        case AI_CMD_STOP_PROCESSING:
            return video_ai_node_stop(node);
            
        case AI_CMD_SET_CONFIDENCE:
            if (param) {
                uint32_t threshold = *(uint32_t*)param;
                data->config.confidence_threshold = threshold;
                
                // Update NN module
                nn_state_t nn_state = nn_get_state();
                if (nn_state == NN_STATE_READY || nn_state == NN_STATE_RUNNING) {
                    float confidence_threshold = (float)threshold / 100.0f;
                    nn_set_confidence_threshold(confidence_threshold);
                }
                
                LOG_CORE_INFO("AI confidence threshold set to %d", threshold);
                return AICAM_OK;
            }
            break;
            
        case AI_CMD_SET_MAX_DETECTIONS:
            if (param) {
                uint32_t max_detections = *(uint32_t*)param;
                data->config.max_detections = max_detections;
                
                // Update NN module
                nn_state_t nn_state = nn_get_state();
                if (nn_state == NN_STATE_READY || nn_state == NN_STATE_RUNNING) {
                    //nn_set_max_detection_count(max_detections);
                }
                
                LOG_CORE_INFO("AI max detections set to %d", max_detections);
                return AICAM_OK;
            }
            break;
            
        case AI_CMD_LOAD_MODEL:
            if (param) {
                uintptr_t model_ptr = *(uintptr_t*)param;
                return video_ai_node_load_model(node, model_ptr);
            }
            break;
            
        case AI_CMD_UNLOAD_MODEL:
            return video_ai_node_unload_model(node);
            
        case AI_CMD_GET_MODEL_INFO:
            if (param) {
                nn_model_info_t *model_info = (nn_model_info_t*)param;
                return video_ai_node_get_model_info(node, model_info);
            }
            break;
            
        case AI_CMD_ENABLE_DRAWING:
            data->config.enable_drawing = AICAM_TRUE;
            if (!data->draw_service_initialized) {
                aicam_result_t result = video_ai_init_draw_service(data);
                if (result == AICAM_OK) {
                    data->draw_service_initialized = AICAM_TRUE;
                    LOG_CORE_INFO("AI drawing enabled");
                } else {
                    LOG_CORE_ERROR("Failed to enable AI drawing: %d", result);
                    return result;
                }
            }
            return AICAM_OK;
            
        case AI_CMD_DISABLE_DRAWING:
            data->config.enable_drawing = AICAM_FALSE;
            if (data->draw_service_initialized) {
                video_ai_deinit_draw_service(data);
                data->draw_service_initialized = AICAM_FALSE;
                LOG_CORE_INFO("AI drawing disabled");
            }
            return AICAM_OK;
            
        case AI_CMD_SET_DRAW_CONFIG:
            if (param) {
                ai_draw_config_t *draw_config = (ai_draw_config_t*)param;
                memcpy(&data->config.draw_config, draw_config, sizeof(ai_draw_config_t));
                
                // Update drawing service configuration if initialized
                if (data->draw_service_initialized) {
                    aicam_result_t result = ai_draw_set_config(draw_config);
                    if (result != AICAM_OK) {
                        LOG_CORE_ERROR("Failed to update draw config: %d", result);
                        return result;
                    }
                }
                
                LOG_CORE_INFO("AI draw configuration updated");
                return AICAM_OK;
            }
            break;
            
        case AI_CMD_GET_DRAW_CONFIG:
            if (param) {
                ai_draw_config_t *draw_config = (ai_draw_config_t*)param;
                memcpy(draw_config, &data->config.draw_config, sizeof(ai_draw_config_t));
                return AICAM_OK;
            }
            break;
            
        default:
            LOG_CORE_WARN("Unknown AI control command: 0x%x", cmd);
            return AICAM_ERROR_INVALID_PARAM;
    }
    
    return AICAM_OK;
}

/* ==================== AI Drawing Service Integration ==================== */

static aicam_result_t video_ai_init_draw_service(video_ai_node_data_t *data)
{
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Update drawing configuration with current AI node settings
    data->config.draw_config.image_width = data->config.width;
    data->config.draw_config.image_height = data->config.height;
    
    // Initialize AI drawing service
    aicam_result_t result = ai_draw_service_init(&data->config.draw_config);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize AI draw service: %d", result);
        return result;
    }
    
    LOG_CORE_INFO("AI draw service initialized for %dx%d", 
                  data->config.width, data->config.height);
    
    return AICAM_OK;
}

static aicam_result_t video_ai_deinit_draw_service(video_ai_node_data_t *data)
{
    if (!data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Deinitialize AI drawing service
    aicam_result_t result = ai_draw_service_deinit();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to deinitialize AI draw service: %d", result);
        return result;
    }
    
    LOG_CORE_INFO("AI draw service deinitialized");
    
    return AICAM_OK;
}

