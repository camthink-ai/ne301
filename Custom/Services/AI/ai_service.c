/**
 * @file ai_service.c
 * @brief AI Service Implementation
 * @details AI Service Implementation, support video pipeline management and AI inference control
 */

#include "ai_service.h"
#include "aicam_types.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "dev_manager.h"
#include "mem.h"
#include "pixel_format_map.h"
#include "ai_draw_service.h"
#include <string.h>
#include <stdio.h>
#include "buffer_mgr.h"
#include "mem_map.h"
#include "json_config_mgr.h"
#include "video_camera_node.h"
#include "device_service.h"

/* ==================== AI Service Context ==================== */

/**
 * @brief AI service context structure
 */
typedef struct {
    aicam_bool_t initialized;              // Service initialization status
    aicam_bool_t running;                  // Service running status
    service_state_t state;                 // Service state
    ai_service_config_t config;            // Service configuration
    ai_service_stats_t stats;              // Service statistics
    
    // Pipeline components - Two separate pipelines
    video_pipeline_t *camera_pipeline;     // Camera->Encoder pipeline handle
    video_pipeline_t *ai_pipeline;         // AI pipeline handle
    
    // Camera pipeline nodes
    video_node_t *camera_node;             // Camera node handle
    video_node_t *encoder_node;            // Encoder node handle
    uint32_t camera_node_id;               // Camera node ID in camera pipeline
    uint32_t encoder_node_id;              // Encoder node ID in camera pipeline
    
    // AI pipeline nodes
    video_node_t *ai_node;                 // AI node handle
    uint32_t ai_node_id;                   // AI node ID in AI pipeline
    
    // Pipeline state
    aicam_bool_t camera_pipeline_initialized;  // Camera pipeline initialization status
    aicam_bool_t camera_pipeline_running;      // Camera pipeline running status
    aicam_bool_t ai_pipeline_initialized;      // AI pipeline initialization status
    aicam_bool_t ai_pipeline_running;          // AI pipeline running status
} ai_service_context_t;

static ai_service_context_t g_ai_service = {0};

/* ==================== Internal Function Declarations ==================== */

static void ai_camera_pipeline_event_callback(video_pipeline_t *pipeline,
                                             uint32_t event_type,
                                             void *data,
                                             void *user_data);

static void ai_ai_pipeline_event_callback(video_pipeline_t *pipeline,
                                         uint32_t event_type,
                                         void *data,
                                         void *user_data);

static aicam_result_t ai_create_camera_pipeline_nodes(const ai_service_config_t *config);
static aicam_result_t ai_create_ai_pipeline_nodes(const ai_service_config_t *config);
static aicam_result_t ai_connect_camera_pipeline_nodes(void);
static aicam_result_t ai_connect_ai_pipeline_nodes(void);

static aicam_result_t ai_service_draw_callback(uint8_t *frame_buffer, 
                                             uint32_t width, 
                                             uint32_t height, 
                                             uint32_t frame_id,
                                             void *user_data);

/* ==================== AI Service Implementation ==================== */

aicam_result_t ai_service_init(void *config)
{
    if (g_ai_service.initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Initializing AI Service...");
    
    // Initialize service context
    memset(&g_ai_service, 0, sizeof(ai_service_context_t));

    // get ai debug configuration
    ai_get_ai_config(&g_ai_service.config);

    // Apply custom configuration if provided
    if (config) {
        ai_service_config_t *custom_config = (ai_service_config_t*)config;
        memcpy(&g_ai_service.config, custom_config, sizeof(ai_service_config_t));
    }
    
    g_ai_service.initialized = AICAM_TRUE;
    g_ai_service.state = SERVICE_STATE_INITIALIZED;
    
    LOG_SVC_INFO("AI Service initialized successfully");
    
    return AICAM_OK;
}

aicam_result_t ai_service_start(void)
{
    if (!g_ai_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_ai_service.running) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_SVC_INFO("Starting AI Service...");
    
    // Initialize pipeline
    aicam_result_t result = ai_pipeline_init(&g_ai_service.config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize AI pipeline: %d", result);
        return result;
    }
    
    g_ai_service.running = AICAM_TRUE;
    g_ai_service.state = SERVICE_STATE_RUNNING;
    g_ai_service.stats.start_time_ms = osKernelGetTickCount();
    
    LOG_SVC_INFO("AI Service started successfully");
    
    return AICAM_OK;
}

aicam_result_t ai_service_stop(void)
{
    if (!g_ai_service.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_ai_service.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    LOG_SVC_INFO("Stopping AI Service...");
    
    // Stop pipeline
    ai_pipeline_stop();
    
    g_ai_service.running = AICAM_FALSE;
    g_ai_service.state = SERVICE_STATE_INITIALIZED;
    g_ai_service.stats.end_time_ms = osKernelGetTickCount();
    
    LOG_SVC_INFO("AI Service stopped successfully");
    
    return AICAM_OK;
}

aicam_result_t ai_service_deinit(void)
{
    if (!g_ai_service.initialized) {
        return AICAM_OK;
    }
    
    // Stop if running
    if (g_ai_service.running) {
        ai_service_stop();
    }
    
    LOG_SVC_INFO("Deinitializing AI Service...");
    
    // Deinitialize pipeline
    ai_pipeline_deinit();
    
    // Reset context
    memset(&g_ai_service, 0, sizeof(ai_service_context_t));
    
    LOG_SVC_INFO("AI Service deinitialized successfully");
    
    return AICAM_OK;
}

service_state_t ai_service_get_state(void)
{
    return g_ai_service.state;
}

/* ==================== AI Pipeline Management Functions ==================== */

aicam_result_t ai_pipeline_init(ai_service_config_t *config)
{
    if (!config) {
        LOG_SVC_ERROR("Invalid pipeline configuration");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (g_ai_service.camera_pipeline_initialized && g_ai_service.ai_pipeline_initialized) {
        LOG_SVC_WARN("AI pipelines already initialized");
        return AICAM_OK;
    }
    
    LOG_SVC_INFO("Initializing AI pipelines: %dx%d@%dfps, AI=%s",
                  config->width, config->height, config->fps,
                  config->ai_enabled ? "enabled" : "disabled");
    
    // Initialize video pipeline system
    aicam_result_t result = video_pipeline_system_init();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to initialize video pipeline system: %d", result);
        return result;
    }
    
    // Create camera pipeline configuration
    video_pipeline_config_t camera_pipeline_config = {
        .name = "CameraPipeline",
        .max_nodes = 2,
        .max_connections = 1,
        .global_flow_mode = FLOW_MODE_PUSH,
        .auto_start = AICAM_FALSE,
        .event_callback = ai_camera_pipeline_event_callback,
        .user_data = &g_ai_service
    };
    
    // Create camera pipeline
    result = video_pipeline_create(&camera_pipeline_config, &g_ai_service.camera_pipeline);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to create camera pipeline: %d", result);
        return result;
    }
    
    // Create AI pipeline configuration
    video_pipeline_config_t ai_pipeline_config = {
        .name = "AIPipeline",
        .max_nodes = 1,
        .max_connections = 0,
        .global_flow_mode = FLOW_MODE_PUSH,
        .auto_start = AICAM_FALSE,
        .event_callback = ai_ai_pipeline_event_callback,
        .user_data = &g_ai_service
    };
    
    // // Create AI pipeline
    result = video_pipeline_create(&ai_pipeline_config, &g_ai_service.ai_pipeline);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to create AI pipeline: %d", result);
        video_pipeline_destroy(g_ai_service.camera_pipeline);
        g_ai_service.camera_pipeline = NULL;
        return result;
    }
    
    // Create camera pipeline nodes
    ai_get_normal_config(config);
    result = ai_create_camera_pipeline_nodes(config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to create camera pipeline nodes: %d", result);
        video_pipeline_destroy(g_ai_service.camera_pipeline);
        video_pipeline_destroy(g_ai_service.ai_pipeline);
        g_ai_service.camera_pipeline = NULL;
        g_ai_service.ai_pipeline = NULL;
        return result;
    }
    
    // Create AI pipeline nodes
    ai_get_ai_config(config);
    result = ai_create_ai_pipeline_nodes(config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to create AI pipeline nodes: %d", result);
        video_pipeline_destroy(g_ai_service.camera_pipeline);
        video_pipeline_destroy(g_ai_service.ai_pipeline);
        g_ai_service.camera_pipeline = NULL;
        g_ai_service.ai_pipeline = NULL;
        return result;
    }
    
    // Connect camera pipeline nodes
    result = ai_connect_camera_pipeline_nodes();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to connect camera pipeline nodes: %d", result);
        video_pipeline_destroy(g_ai_service.camera_pipeline);
        video_pipeline_destroy(g_ai_service.ai_pipeline);
        g_ai_service.camera_pipeline = NULL;
        g_ai_service.ai_pipeline = NULL;
        return result;
    }
    
    // Connect AI pipeline nodes (no connections needed for standalone AI node)
    result = ai_connect_ai_pipeline_nodes();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to connect AI pipeline nodes: %d", result);
        video_pipeline_destroy(g_ai_service.camera_pipeline);
        video_pipeline_destroy(g_ai_service.ai_pipeline);
        g_ai_service.camera_pipeline = NULL;
        g_ai_service.ai_pipeline = NULL;
        return result;
    }
    
    g_ai_service.camera_pipeline_initialized = AICAM_TRUE;
    g_ai_service.ai_pipeline_initialized = AICAM_TRUE;
    
    LOG_SVC_INFO("AI pipelines initialized successfully");
    LOG_SVC_INFO("Camera Pipeline: Camera -> Encoder");
    LOG_SVC_INFO("AI Pipeline: AI (standalone)");
    
    return AICAM_OK;
}

aicam_result_t ai_pipeline_start(void)
{
    if (!g_ai_service.camera_pipeline_initialized || !g_ai_service.ai_pipeline_initialized) {
        LOG_SVC_ERROR("AI pipelines not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_ai_service.camera_pipeline_running && g_ai_service.ai_pipeline_running) {
        LOG_SVC_WARN("AI pipelines already running");
        return AICAM_OK;
    }
    
    // Start camera pipeline
    aicam_result_t result = video_pipeline_start(g_ai_service.camera_pipeline);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to start camera pipeline: %d", result);
        return result;
    }
    
    g_ai_service.camera_pipeline_running = AICAM_TRUE;
    
    // Start AI pipeline
    result = video_pipeline_start(g_ai_service.ai_pipeline);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to start AI pipeline: %d", result);
        // Stop camera pipeline on AI pipeline failure
        video_pipeline_stop(g_ai_service.camera_pipeline);
        g_ai_service.camera_pipeline_running = AICAM_FALSE;
        return result;
    }
    
    g_ai_service.ai_pipeline_running = AICAM_TRUE;
    
    LOG_SVC_INFO("AI pipelines started successfully");
    LOG_SVC_INFO("Camera Pipeline: Camera -> Encoder");
    LOG_SVC_INFO("AI Pipeline: AI (standalone)");
    
    return AICAM_OK;
}

aicam_result_t ai_pipeline_stop(void)
{
    if (!g_ai_service.camera_pipeline_initialized || !g_ai_service.ai_pipeline_initialized) {
        LOG_SVC_ERROR("AI pipelines not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_ai_service.camera_pipeline_running && !g_ai_service.ai_pipeline_running) {
        LOG_SVC_WARN("AI pipelines not running");
        return AICAM_OK;
    }
    
    aicam_result_t result = AICAM_OK;
    
    // Stop camera pipeline
    if (g_ai_service.camera_pipeline_running) {
        aicam_result_t camera_result = video_pipeline_stop(g_ai_service.camera_pipeline);
        if (camera_result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to stop camera pipeline: %d", camera_result);
            result = camera_result;
        } else {
            g_ai_service.camera_pipeline_running = AICAM_FALSE;
        }
    }
    
    // Stop AI pipeline
    if (g_ai_service.ai_pipeline_running) {
        aicam_result_t ai_result = video_pipeline_stop(g_ai_service.ai_pipeline);
        if (ai_result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to stop AI pipeline: %d", ai_result);
            result = ai_result;
        } else {
            g_ai_service.ai_pipeline_running = AICAM_FALSE;
        }
    }
    
    LOG_SVC_INFO("AI pipelines stopped successfully");
    
    return result;
}

void ai_pipeline_deinit(void)
{
    if (!g_ai_service.camera_pipeline_initialized && !g_ai_service.ai_pipeline_initialized) {
        return;
    }
    
    // Stop pipelines if running
    if (g_ai_service.camera_pipeline_running || g_ai_service.ai_pipeline_running) {
        ai_pipeline_stop();
    }
    
    // Destroy camera pipeline
    if (g_ai_service.camera_pipeline) {
        video_pipeline_destroy(g_ai_service.camera_pipeline);
        g_ai_service.camera_pipeline = NULL;
    }
    
    // Destroy AI pipeline
    if (g_ai_service.ai_pipeline) {
        video_pipeline_destroy(g_ai_service.ai_pipeline);
        g_ai_service.ai_pipeline = NULL;
    }
    
    // Deinitialize video pipeline system
    // video_pipeline_system_deinit();
    
    g_ai_service.camera_pipeline_initialized = AICAM_FALSE;
    g_ai_service.camera_pipeline_running = AICAM_FALSE;
    g_ai_service.ai_pipeline_initialized = AICAM_FALSE;
    g_ai_service.ai_pipeline_running = AICAM_FALSE;
    
    LOG_SVC_INFO("AI pipelines deinitialized");
}

aicam_bool_t ai_pipeline_is_running(void)
{
    return g_ai_service.camera_pipeline_running && g_ai_service.ai_pipeline_running;
}

aicam_bool_t ai_pipeline_is_initialized(void)
{
    return g_ai_service.camera_pipeline_initialized && g_ai_service.ai_pipeline_initialized;
}

video_node_t* ai_service_get_ai_node(void)
{
    if (!g_ai_service.initialized || !g_ai_service.ai_pipeline_initialized) {
        LOG_SVC_ERROR("AI service not initialized");
        return NULL;
    }
    
    return g_ai_service.ai_node;
}

aicam_result_t ai_service_get_nn_result(nn_result_t *result, uint32_t frame_id)
{
    if (!result) {
        LOG_SVC_ERROR("Invalid parameter for AI service get NN result");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.initialized || !g_ai_service.ai_pipeline_initialized) {
        LOG_SVC_ERROR("AI service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI node not available");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Get NN result from AI node
    aicam_result_t ret = video_ai_node_get_best_nn_result(g_ai_service.ai_node, result, frame_id);
    if (ret != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get NN result from AI node: %d", ret);
        return ret;
    }

    //LOG_SVC_DEBUG("Retrieved NN result from AI service: %d detections", result->od.nb_detect);
    return AICAM_OK;
}

aicam_result_t ai_service_get_model_info(nn_model_info_t *model_info)
{
    if (!model_info) {
        LOG_SVC_ERROR("Invalid parameter for AI service get model info");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    return nn_get_model_info(model_info);
}

/* ==================== Internal Functions ==================== */

static aicam_result_t ai_service_draw_callback(uint8_t *frame_buffer, 
                                             uint32_t width, 
                                             uint32_t height, 
                                             uint32_t frame_id,
                                             void *user_data)
{
    if (!frame_buffer) {
        LOG_SVC_ERROR("Invalid frame buffer for AI drawing");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Get latest NN result from AI service
    nn_result_t nn_result;
    memset(&nn_result, 0, sizeof(nn_result_t));
    
    aicam_result_t ai_ret = ai_service_get_nn_result(&nn_result, frame_id);
    if (ai_ret == AICAM_OK && (nn_result.od.nb_detect > 0 || nn_result.mpe.nb_detect > 0)) {
        
        // Initialize AI draw service if not already done
        if (!ai_draw_is_initialized()) {
            ai_draw_config_t draw_config;
            ai_draw_get_default_config(&draw_config);
            draw_config.image_width = width;
            draw_config.image_height = height;
            
            aicam_result_t draw_init_ret = ai_draw_service_init(&draw_config);
            if (draw_init_ret != AICAM_OK) {
                LOG_SVC_WARN("Failed to initialize AI draw service: %d", draw_init_ret);
                return draw_init_ret;
            } else {
                LOG_SVC_INFO("AI draw service initialized for camera callback");
            }
        }
        
        // Draw AI results on the frame buffer
        if (ai_draw_is_initialized()) {
            aicam_result_t draw_ret = ai_draw_results(frame_buffer, width, height, &nn_result);
            if (draw_ret == AICAM_OK) {
                return AICAM_OK;
            } else {
                LOG_SVC_WARN("Failed to draw AI results on camera frame: %d", draw_ret);
                return draw_ret;
            }
        }
    } else if (ai_ret != AICAM_OK && ai_ret != AICAM_ERROR_NOT_INITIALIZED) {
        LOG_SVC_WARN("Failed to get NN result for camera drawing: %d", ai_ret);
    }
    
    // No detections or AI not ready - this is normal
    return AICAM_OK;
}

static void ai_camera_pipeline_event_callback(video_pipeline_t *pipeline,
                                             uint32_t event_type,
                                             void *data,
                                             void *user_data)
{
    switch (event_type) {
        case VIDEO_PIPELINE_EVENT_STARTED:
            LOG_SVC_INFO("Camera Pipeline event: Pipeline started");
            break;
            
        case VIDEO_PIPELINE_EVENT_STOPPED:
            LOG_SVC_INFO("Camera Pipeline event: Pipeline stopped");
            break;
            
        case VIDEO_PIPELINE_EVENT_ERROR:
            LOG_SVC_ERROR("Camera Pipeline event: Pipeline error");
            break;
            
        case VIDEO_PIPELINE_EVENT_NODE_ADDED:
            LOG_SVC_INFO("Camera Pipeline event: Node added");
            break;
            
        case VIDEO_PIPELINE_EVENT_CONNECTED:
            LOG_SVC_INFO("Camera Pipeline event: Nodes connected");
            break;
            
        default:
            LOG_SVC_DEBUG("Camera Pipeline event: Unknown event %d from pipeline", event_type);
            break;
    }
}

static void ai_ai_pipeline_event_callback(video_pipeline_t *pipeline,
                                         uint32_t event_type,
                                         void *data,
                                         void *user_data)
{
    switch (event_type) {
        case VIDEO_PIPELINE_EVENT_STARTED:
            LOG_SVC_INFO("AI Pipeline event: Pipeline started");
            break;
            
        case VIDEO_PIPELINE_EVENT_STOPPED:
            LOG_SVC_INFO("AI Pipeline event: Pipeline stopped");
            break;
            
        case VIDEO_PIPELINE_EVENT_ERROR:
            LOG_SVC_ERROR("AI Pipeline event: Pipeline error");
            break;
            
        case VIDEO_PIPELINE_EVENT_NODE_ADDED:
            LOG_SVC_INFO("AI Pipeline event: Node added");
            break;
            
        case VIDEO_PIPELINE_EVENT_CONNECTED:
            LOG_SVC_INFO("AI Pipeline event: Nodes connected");
            break;
            
        default:
            LOG_SVC_DEBUG("AI Pipeline event: Unknown event %d from pipeline", event_type);
            break;
    }
}

static aicam_result_t ai_create_camera_pipeline_nodes(const ai_service_config_t *config)
{
    // Create camera node configuration
    video_camera_config_t camera_config;
    video_camera_get_default_config(&camera_config);
    camera_config.width = config->width;
    camera_config.height = config->height;
    camera_config.fps = config->fps;
    camera_config.bpp = config->bpp;
    camera_config.format = config->format;
    camera_config.ai_enabled = config->ai_enabled;
    
    // Create encoder node configuration
    video_encoder_config_t encoder_config;
    video_encoder_get_default_config(&encoder_config);
    
    // Create camera and encoder nodes
    g_ai_service.camera_node = video_camera_node_create("CameraPipelineCamera", &camera_config);
    g_ai_service.encoder_node = video_encoder_node_create("CameraPipelineEncoder", &encoder_config);
    
    if (!g_ai_service.camera_node || !g_ai_service.encoder_node) {
        LOG_SVC_ERROR("Failed to create camera pipeline nodes");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Register nodes with camera pipeline
    aicam_result_t result = video_pipeline_register_node(g_ai_service.camera_pipeline,
                                                        g_ai_service.camera_node, 
                                                        &g_ai_service.camera_node_id);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to register camera node: %d", result);
        return result;
    }
    
    result = video_pipeline_register_node(g_ai_service.camera_pipeline, 
                                        g_ai_service.encoder_node, 
                                        &g_ai_service.encoder_node_id);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to register encoder node: %d", result);
        return result;
    }
    
    // Register AI drawing callback to camera node
  
    result = video_camera_node_set_ai_callback(g_ai_service.camera_node, 
                                                ai_service_draw_callback, 
                                                &g_ai_service);
    if (result != AICAM_OK) {
        LOG_SVC_WARN("Failed to register AI callback to camera node: %d", result);
        // Not a fatal error, continue without callback
    } else {
        LOG_SVC_INFO("AI drawing callback registered to camera node");
    }
    
    
    LOG_SVC_INFO("Camera pipeline nodes created successfully");
    
    return AICAM_OK;
}

static aicam_result_t ai_create_ai_pipeline_nodes(const ai_service_config_t *config)
{
    // Create AI node configuration
    video_ai_config_t ai_config;
    video_ai_get_default_config(&ai_config);
    ai_config.width = config->width;
    ai_config.height = config->height;
    ai_config.fps = config->fps;
    ai_config.input_format = config->format;
    ai_config.bpp = config->bpp;
    ai_config.confidence_threshold = config->confidence_threshold;
    ai_config.nms_threshold = config->nms_threshold;
    ai_config.max_detections = config->max_detections;
    ai_config.processing_interval = config->processing_interval;
    ai_config.enabled = config->ai_enabled;
    ai_config.enable_drawing = config->enable_drawing;
    
    // Create AI node
    g_ai_service.ai_node = video_ai_node_create("AIPipelineAI", &ai_config);
    
    if (!g_ai_service.ai_node) {
        LOG_SVC_ERROR("Failed to create AI pipeline nodes");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Register AI node with AI pipeline
    aicam_result_t result = video_pipeline_register_node(g_ai_service.ai_pipeline, 
                                                        g_ai_service.ai_node, 
                                                        &g_ai_service.ai_node_id);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to register AI node: %d", result);
        return result;
    }

    //load model
    // result = ai_load_model(AI_DEFAULT_BASE);
    // if (result != AICAM_OK) {
    //     LOG_SVC_ERROR("Failed to load AI model: %d", result);
    //     return result;
    // }
    
    LOG_SVC_INFO("AI pipeline nodes created successfully");
    
    return AICAM_OK;
}

static aicam_result_t ai_connect_camera_pipeline_nodes(void)
{
    // Connect camera to encoder in camera pipeline
    aicam_result_t result = video_pipeline_connect_nodes(g_ai_service.camera_pipeline, 
                                                        g_ai_service.camera_node_id, 0,    // camera node, output 0
                                                        g_ai_service.encoder_node_id, 0);  // encoder node, input 0
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to connect camera to encoder: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("Camera pipeline nodes connected successfully");
    LOG_SVC_INFO("Camera Pipeline: Camera -> Encoder");
    
    return AICAM_OK;
}

static aicam_result_t ai_connect_ai_pipeline_nodes(void)
{
    // AI pipeline has only one node (AI node), no connections needed
    LOG_SVC_INFO("AI pipeline nodes connected successfully");
    LOG_SVC_INFO("AI Pipeline: AI (standalone)");
    
    return AICAM_OK;
}


/* ==================== AI Inference Control Functions ==================== */

aicam_result_t ai_set_inference_enabled(aicam_bool_t enabled)
{
    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI pipeline not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update configuration
    g_ai_service.config.ai_enabled = enabled;
    // if(enabled) {
    //     ai_get_ai_config(&g_ai_service.config);
    // } else {
    //     ai_get_normal_config(&g_ai_service.config);
    // }

    //stop pipeline
    //ai_pipeline_stop();
    
    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    ai_config.enabled = enabled;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }

    //Update camera node config
    video_camera_config_t camera_config;
    result = video_camera_node_get_config(g_ai_service.camera_node, &camera_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get camera node config: %d", result);
        return result;
    }
    camera_config.ai_enabled = enabled;
    LOG_SVC_INFO("Camera node config: %dx%d@%dfps, format=%d, bpp=%d, ai_enabled=%d", 
                  camera_config.width, camera_config.height, camera_config.fps, 
                  camera_config.format, camera_config.bpp, camera_config.ai_enabled);
    result = video_camera_node_set_config(g_ai_service.camera_node, &camera_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set camera node config: %d", result);
        return result;
    }

    //start pipeline
   //ai_pipeline_start();


    LOG_SVC_INFO("AI inference %s", enabled ? "enabled" : "disabled");
    return AICAM_OK;
}

aicam_bool_t ai_get_inference_enabled(void)
{
    return g_ai_service.config.ai_enabled;
}

aicam_result_t ai_set_nms_threshold(uint32_t threshold)
{
    if (threshold > 100) {
        LOG_SVC_ERROR("Invalid confidence threshold: %d", threshold);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI pipeline not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update configuration
    g_ai_service.config.nms_threshold = threshold;
    
    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    
    ai_config.nms_threshold = threshold;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }
    
    // Update NN module if available
    nn_state_t nn_state = nn_get_state();
    if (nn_state == NN_STATE_READY || nn_state == NN_STATE_RUNNING) {
        float nms_threshold = (float)threshold / 100.0f;
        nn_set_nms_threshold(nms_threshold);
    }

    // update to json config
    json_config_set_nms_threshold(threshold);
    
    LOG_SVC_INFO("AI NMS threshold set to %d", threshold);
    
    return AICAM_OK;
}

uint32_t ai_get_nms_threshold(void)
{
    float nms_threshold = (float)g_ai_service.config.nms_threshold / 100.0f;
    nn_get_nms_threshold(&nms_threshold);
    return (uint32_t)(nms_threshold * 100);
}

aicam_result_t ai_set_confidence_threshold(uint32_t threshold)
{
    if (threshold > 100) {
        LOG_SVC_ERROR("Invalid confidence threshold: %d", threshold);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI pipeline not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update configuration
    g_ai_service.config.confidence_threshold = threshold;
    
    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    
    ai_config.confidence_threshold = threshold;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }
    
    // Update NN module if available
    nn_state_t nn_state = nn_get_state();
    if (nn_state == NN_STATE_READY || nn_state == NN_STATE_RUNNING) {
        float confidence_threshold = (float)threshold / 100.0f;
        nn_set_confidence_threshold(confidence_threshold);
    }

    // update to json config
    json_config_set_confidence_threshold(threshold);
    
    LOG_SVC_INFO("AI confidence threshold set to %d", threshold);
    
    return AICAM_OK;
}

uint32_t ai_get_confidence_threshold(void)
{
    float confidence_threshold = (float)g_ai_service.config.confidence_threshold / 100.0f;
    nn_get_confidence_threshold(&confidence_threshold);
    return (uint32_t)(confidence_threshold * 100);
}

aicam_result_t ai_set_max_detections(uint32_t max_detections)
{
    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI pipeline not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update configuration
    g_ai_service.config.max_detections = max_detections;
    
    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    
    ai_config.max_detections = max_detections;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("AI max detections set to %d", max_detections);
    
    return AICAM_OK;
}

uint32_t ai_get_max_detections(void)
{
    return g_ai_service.config.max_detections;
}

aicam_result_t ai_set_processing_interval(uint32_t interval)
{
    if (interval == 0) {
        LOG_SVC_ERROR("Invalid processing interval: %d", interval);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI pipeline not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Update configuration
    g_ai_service.config.processing_interval = interval;
    
    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    
    ai_config.processing_interval = interval;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("AI processing interval set to %d", interval);
    
    return AICAM_OK;
}

uint32_t ai_get_processing_interval(void)
{
    return g_ai_service.config.processing_interval;
}

/* ==================== AI Model Management Functions ==================== */

aicam_result_t ai_load_model(uintptr_t model_ptr)
{   
    aicam_result_t result = video_ai_node_load_model(g_ai_service.ai_node, model_ptr);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to load AI model: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("AI model loaded successfully");
    
    return AICAM_OK;
}

aicam_result_t ai_unload_model(void)
{
    aicam_result_t result = video_ai_node_unload_model(g_ai_service.ai_node);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to unload AI model: %d", result);
        return result;
    }
    
    LOG_SVC_INFO("AI model unloaded successfully");
    
    return AICAM_OK;
}

aicam_result_t ai_get_model_info(nn_model_info_t *model_info)
{
    if (!model_info) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_ERROR("AI pipeline not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    aicam_result_t result = video_ai_node_get_model_info(g_ai_service.ai_node, model_info);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI model info: %d", result);
        return result;
    }
    
    return AICAM_OK;
}


aicam_result_t ai_reload_model(void)
{
    //stop ai pipeline
    ai_pipeline_stop();

    //stop camera device
    aicam_result_t result = device_service_camera_stop();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to stop camera device: %d", result);
        return result;
    }

    //reload model
    result = video_ai_node_reload_model(g_ai_service.ai_node);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reload AI model: %d", result);
        device_service_camera_start();
        return result;
    }

    //start camera device
    result = device_service_camera_start();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to start camera device: %d", result);
        return result;
    }

    //deint draw service
    // result = ai_draw_service_deinit();
    // if (result != AICAM_OK) {
    //     LOG_SVC_ERROR("Failed to reset draw service: %d", result);
    //     return result;
    // }

    return AICAM_OK;
}

/* ==================== AI Statistics Functions ==================== */

aicam_result_t ai_get_stats(ai_service_stats_t *stats)
{
    if (!stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.initialized) {
        LOG_SVC_ERROR("AI service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Get camera statistics
    if (g_ai_service.camera_node) {
        video_node_stats_t camera_stats;
        video_node_get_stats(g_ai_service.camera_node, &camera_stats);
        g_ai_service.stats.total_frames_captured = camera_stats.frames_processed;
    }
    
    // Get AI statistics
    if (g_ai_service.ai_node) {
        video_ai_stats_t ai_stats;
        video_ai_node_get_stats(g_ai_service.ai_node, &ai_stats);
        g_ai_service.stats.total_frames_processed = ai_stats.frames_processed;
        g_ai_service.stats.total_detections_found = ai_stats.detections_found;
        g_ai_service.stats.ai_processing_errors = ai_stats.processing_errors;
        g_ai_service.stats.avg_ai_processing_time_us = ai_stats.avg_processing_time_us;
        g_ai_service.stats.current_detection_count = ai_stats.current_detection_count;
    }
    
    // Get encoder statistics
    if (g_ai_service.encoder_node) {
        video_node_stats_t encoder_stats;
        video_node_get_stats(g_ai_service.encoder_node, &encoder_stats);
        g_ai_service.stats.total_frames_encoded = encoder_stats.frames_processed;
    }
    
    // Calculate derived statistics
    if (g_ai_service.stats.end_time_ms > g_ai_service.stats.start_time_ms) {
        uint64_t duration_ms = g_ai_service.stats.end_time_ms - g_ai_service.stats.start_time_ms;
        if (duration_ms > 0) {
            g_ai_service.stats.avg_fps = (uint32_t)((g_ai_service.stats.total_frames_encoded * 1000) / duration_ms);
        }
    }
    
    memcpy(stats, &g_ai_service.stats, sizeof(ai_service_stats_t));
    return AICAM_OK;
}

aicam_result_t ai_reset_stats(void)
{
    if (!g_ai_service.initialized) {
        LOG_SVC_ERROR("AI service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Reset service statistics
    memset(&g_ai_service.stats, 0, sizeof(ai_service_stats_t));
    
    // Reset node statistics
    if (g_ai_service.ai_node) {
        video_ai_node_reset_stats(g_ai_service.ai_node);
    }
    
    LOG_SVC_INFO("AI service statistics reset");
    
    return AICAM_OK;
}

void ai_print_stats(void)
{
    ai_service_stats_t stats;
    aicam_result_t result = ai_get_stats(&stats);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI service statistics");
        return;
    }
    
    LOG_SVC_INFO("=== AI Service Statistics ===");
    LOG_SVC_INFO("Total frames captured: %llu", stats.total_frames_captured);
    LOG_SVC_INFO("Total frames processed: %llu", stats.total_frames_processed);
    LOG_SVC_INFO("Total frames encoded: %llu", stats.total_frames_encoded);
    LOG_SVC_INFO("Total detections found: %llu", stats.total_detections_found);
    LOG_SVC_INFO("Pipeline errors: %llu", stats.pipeline_errors);
    LOG_SVC_INFO("AI processing errors: %llu", stats.ai_processing_errors);
    LOG_SVC_INFO("Average FPS: %d", stats.avg_fps);
    LOG_SVC_INFO("Average AI processing time: %d us", stats.avg_ai_processing_time_us);
    LOG_SVC_INFO("Current detection count: %d", stats.current_detection_count);
    
    if (stats.end_time_ms > stats.start_time_ms) {
        uint64_t duration_ms = stats.end_time_ms - stats.start_time_ms;
        LOG_SVC_INFO("Service duration: %llu ms (%.2f seconds)", duration_ms, duration_ms / 1000.0f);
    }
    
    LOG_SVC_INFO("=============================");
}

/* ==================== AI Configuration Functions ==================== */

void ai_get_normal_config(ai_service_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(ai_service_config_t));
    config->width = 1280;
    config->height = 720;
    config->fps = 30;
    config->format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    config->bpp = 2;
    config->confidence_threshold = json_config_get_confidence_threshold();
    config->nms_threshold = json_config_get_nms_threshold();
    config->max_detections = 32;
    config->processing_interval = 1;
    config->ai_enabled = AICAM_TRUE;
    config->enable_stats = AICAM_TRUE;
    config->enable_drawing = AICAM_FALSE;
    config->enable_debug = AICAM_FALSE;
}

void ai_get_ai_config(ai_service_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(ai_service_config_t));
    config->width = 224;
    config->height = 224;
    config->fps = 30;
    config->format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
    config->bpp = 3;
    config->confidence_threshold = json_config_get_confidence_threshold();
    config->nms_threshold = json_config_get_nms_threshold();
    config->max_detections = 32;
    config->processing_interval = 1;
    config->ai_enabled = AICAM_TRUE;
    config->enable_stats = AICAM_TRUE;
    config->enable_debug = AICAM_FALSE;
    config->enable_drawing = AICAM_TRUE;
}

aicam_result_t ai_set_config(const ai_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.initialized) {
        LOG_SVC_ERROR("AI service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_ai_service.running) {
        LOG_SVC_ERROR("Cannot change configuration while service is running");
        return AICAM_ERROR;
    }
    
    memcpy(&g_ai_service.config, config, sizeof(ai_service_config_t));
    
    LOG_SVC_INFO("AI service configuration updated: %dx%d@%dfps, AI=%s",
                  config->width, config->height, config->fps,
                  config->ai_enabled ? "enabled" : "disabled");
    
    return AICAM_OK;
}

aicam_result_t ai_get_config(ai_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_ai_service.initialized) {
        LOG_SVC_ERROR("AI service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(config, &g_ai_service.config, sizeof(ai_service_config_t));
    
    return AICAM_OK;
}



/* ==================== JPEG Processing Functions ==================== */

aicam_result_t ai_jpeg_decode(const uint8_t *jpeg_data, 
                              uint32_t jpeg_size,
                              const ai_jpeg_decode_config_t *decode_config,
                              uint8_t **raw_buffer, 
                              uint32_t *raw_size)
{
    if (!jpeg_data || !decode_config || !raw_buffer || !raw_size) {
        LOG_SVC_ERROR("Invalid parameters for JPEG decode");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (jpeg_size == 0) {
        LOG_SVC_ERROR("Invalid JPEG size: %d", jpeg_size);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Decoding JPEG: %dx%d, size=%d", 
                  decode_config->width, decode_config->height, jpeg_size);
    
    // Find JPEG device
    device_t *jpeg_dev = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!jpeg_dev) {
        LOG_SVC_ERROR("JPEG device not found");
        return AICAM_ERROR;
    }
    
    // Set decode parameters
    jpegc_params_t jpeg_dec_param = {0};
    jpeg_dec_param.ImageWidth = decode_config->width;
    jpeg_dec_param.ImageHeight = decode_config->height;
    jpeg_dec_param.ChromaSubsampling = decode_config->chroma_subsampling;

    LOG_SVC_INFO("JPEG decode parameters: width:%d, height:%d, chroma_subsampling:%d", 
                 jpeg_dec_param.ImageWidth, jpeg_dec_param.ImageHeight, jpeg_dec_param.ChromaSubsampling);
    
    aicam_result_t result = device_ioctl(jpeg_dev, JPEGC_CMD_SET_DEC_PARAM, 
                                        (uint8_t *)&jpeg_dec_param, 
                                        sizeof(jpegc_params_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set JPEG decode parameters: %d", result);
        return result;
    }

    //get jpeg decode parameters
    jpegc_params_t jpeg_dec_info = {0};
    result = device_ioctl(jpeg_dev, JPEGC_CMD_GET_DEC_INFO, 
                        (uint8_t *)&jpeg_dec_info, sizeof(jpegc_params_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get JPEG decode info: %d", result);
        return result;
    }
    LOG_SVC_INFO("JPEG decode info: width:%d, height:%d, chroma_subsampling:%d", 
                 jpeg_dec_info.ImageWidth, jpeg_dec_info.ImageHeight, jpeg_dec_info.ChromaSubsampling);
    
    // Input JPEG data for decoding
    result = device_ioctl(jpeg_dev, JPEGC_CMD_INPUT_DEC_BUFFER, 
                         (uint8_t *)jpeg_data, jpeg_size);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to input JPEG decode buffer: %d", result);
        return result;
    }
    
    // Get decoded raw data
    uint8_t *raw_data = NULL;
    LOG_SVC_INFO("Output JPEG decode buffer");
    int raw_len = device_ioctl(jpeg_dev, JPEGC_CMD_OUTPUT_DEC_BUFFER, 
                              (uint8_t *)&raw_data, 0);
    if (raw_len <= 0) {
        LOG_SVC_ERROR("Failed to get JPEG decode output: %d", raw_len);
        return AICAM_ERROR;
    }
    
    *raw_buffer = raw_data;
    *raw_size = raw_len;
    
    LOG_SVC_INFO("JPEG decoded successfully: %d bytes", raw_len);
    
    return AICAM_OK;
}

aicam_result_t ai_jpeg_encode(const uint8_t *raw_data, 
                              uint32_t raw_size,
                              const ai_jpeg_encode_config_t *encode_config,
                              uint8_t **jpeg_buffer, 
                              uint32_t *jpeg_size)
{
    if (!raw_data || !encode_config || !jpeg_buffer || !jpeg_size) {
        LOG_SVC_ERROR("Invalid parameters for JPEG encode");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (raw_size == 0) {
        LOG_SVC_ERROR("Invalid raw data size: %d", raw_size);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Encoding JPEG: %dx%d, quality=%d, raw_size=%d", 
                  encode_config->width, encode_config->height, 
                  encode_config->quality, raw_size);
    
    // Find JPEG device
    device_t *jpeg_dev = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!jpeg_dev) {
        LOG_SVC_ERROR("JPEG device not found");
        return AICAM_ERROR;
    }
    
    // Set encode parameters
    jpegc_params_t jpeg_enc_param = {0};
    jpeg_enc_param.ImageWidth = encode_config->width;
    jpeg_enc_param.ImageHeight = encode_config->height;
    jpeg_enc_param.ChromaSubsampling = encode_config->chroma_subsampling;
    jpeg_enc_param.ImageQuality = encode_config->quality;
    jpeg_enc_param.ColorSpace = JPEG_YCBCR_COLORSPACE;
    
    aicam_result_t result = device_ioctl(jpeg_dev, JPEGC_CMD_SET_ENC_PARAM, 
                                        (uint8_t *)&jpeg_enc_param, 
                                        sizeof(jpegc_params_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set JPEG encode parameters: %d", result);
        return result;
    }
    
    // Input raw data for encoding
    result = device_ioctl(jpeg_dev, JPEGC_CMD_INPUT_ENC_BUFFER, 
                         (uint8_t *)raw_data, raw_size);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to input JPEG encode buffer: %d", result);
        return result;
    }
    
    // Get encoded JPEG data
    uint8_t *jpeg_data = NULL;
    int jpeg_len = device_ioctl(jpeg_dev, JPEGC_CMD_OUTPUT_ENC_BUFFER, 
                               (uint8_t *)&jpeg_data, 0);
    if (jpeg_len <= 0) {
        LOG_SVC_ERROR("Failed to get JPEG encode output: %d", jpeg_len);
        return AICAM_ERROR;
    }
    
    *jpeg_buffer = jpeg_data;
    *jpeg_size = jpeg_len;
    
    LOG_SVC_INFO("JPEG encoded successfully: %d bytes", jpeg_len);
    
    return AICAM_OK;
}

aicam_result_t ai_color_convert(const uint8_t *src_data,
                                uint32_t src_width,
                                uint32_t src_height,
                                uint32_t src_format,
                                uint32_t rb_swap,
                                uint32_t chroma_subsampling,
                                uint8_t **dst_data,
                                uint32_t *dst_size,
                                uint32_t dst_format)
{
    if (!src_data || !dst_data) {
        LOG_SVC_ERROR("Invalid parameters for color convert");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    LOG_SVC_INFO("Color converting: %dx%d, %d -> %d", 
                  src_width, src_height, src_format, dst_format);
    
    // Find draw device
    device_t *draw_dev = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!draw_dev) {
        LOG_SVC_ERROR("Draw device not found");
        return AICAM_ERROR;
    }
    
    // Calculate destination buffer size
    uint32_t dst_bpp = 0;
    switch (dst_format) {
        case DMA2D_OUTPUT_RGB565:
            dst_bpp = 2;
            break;
        case DMA2D_OUTPUT_RGB888:
            dst_bpp = 3;
            break;
        case DMA2D_OUTPUT_ARGB8888:
            dst_bpp = 4;
            break;
        default:
            LOG_SVC_ERROR("Unsupported destination format: %d", dst_format);
            return AICAM_ERROR_INVALID_PARAM;
    }
    
    uint32_t dst_size_tmp = src_width * src_height * dst_bpp;
    
    // Allocate destination buffer
    uint8_t *converted_data = buffer_malloc_aligned(dst_size_tmp, 32);
    if (!converted_data) {
        LOG_SVC_ERROR("Failed to allocate color convert buffer");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Setup color convert parameters
    draw_color_convert_param_t color_convert_param = {0};
    color_convert_param.src_width = src_width;
    color_convert_param.src_height = src_height;
    color_convert_param.in_colormode = src_format;
    color_convert_param.out_colormode = dst_format;
    color_convert_param.p_src = (uint8_t *)src_data;
    color_convert_param.p_dst = converted_data;
    color_convert_param.rb_swap = rb_swap; // JPEG decoded images need RB swap
    color_convert_param.ChromaSubSampling = CSS_jpeg_to_dma2d(chroma_subsampling);


    // printf("color_convert_param.p_src:%p\r\n", color_convert_param.p_src);
    // printf("color_convert_param.p_dst:%p\r\n", color_convert_param.p_dst);
    // printf("color_convert_param.src_width:%d\r\n", color_convert_param.src_width);
    // printf("color_convert_param.src_height:%d\r\n", color_convert_param.src_height);
    // printf("color_convert_param.in_colormode:%d\r\n", color_convert_param.in_colormode);
    // printf("color_convert_param.out_colormode:%d\r\n", color_convert_param.out_colormode);
    // printf("color_convert_param.rb_swap:%d\r\n", color_convert_param.rb_swap);
    // printf("color_convert_param.ChromaSubSampling:%d \r\n", color_convert_param.ChromaSubSampling);
    
    // Perform color conversion
    aicam_result_t result = device_ioctl(draw_dev, DRAW_CMD_COLOR_CONVERT, 
                                        (uint8_t *)&color_convert_param, 
                                        sizeof(draw_color_convert_param_t));
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to perform color conversion: %d", result);
        buffer_free(converted_data);
        return result;
    }
    
    *dst_data = converted_data;
    *dst_size = dst_size_tmp;
    
    LOG_SVC_INFO("Color conversion completed: %d bytes", dst_size_tmp);
    
    return AICAM_OK;
}

aicam_result_t ai_single_image_inference(const model_validation_config_t *model_validation_config,
                                         ai_single_inference_result_t *result)
{
    if (!model_validation_config || !result)
    {
        LOG_SVC_ERROR("Invalid parameters for single image inference");
        return AICAM_ERROR_INVALID_PARAM;
    }

    uint32_t start_time = osKernelGetTickCount();
    memset(result, 0, sizeof(ai_single_inference_result_t));

    device_t *jpeg_dev = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (!jpeg_dev)
    {
        LOG_SVC_ERROR("JPEG device not found");
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("Starting single image inference: AI=%d bytes, Draw=%d bytes",
                 model_validation_config->ai_image_size, model_validation_config->draw_image_size);

    // Initialize all buffer pointers to NULL for proper cleanup
    uint8_t *ai_jpeg_data_copy = NULL;
    uint8_t *ai_raw_data = NULL;
    uint8_t *ai_rgb_data = NULL;
    uint8_t *draw_jepg_data_copy = NULL;
    uint8_t *draw_raw_data = NULL;
    uint8_t *draw_rgb_data = NULL;
    uint8_t *output_jpeg = NULL;    
    uint32_t ai_raw_size = 0;
    uint32_t draw_raw_size = 0;

    aicam_result_t ret = AICAM_OK;

    // Step 1: Decode AI JPEG (small size for AI inference)
    ai_jpeg_data_copy = buffer_calloc(1, model_validation_config->ai_image_size);
    if (!ai_jpeg_data_copy)
    {
        LOG_SVC_ERROR("Failed to allocate AI JPEG data copy");
        ret = AICAM_ERROR_NO_MEMORY;
        goto cleanup;
    }
    memcpy(ai_jpeg_data_copy, model_validation_config->ai_image_data, model_validation_config->ai_image_size);

    ai_jpeg_decode_config_t ai_decode_config = {
        .width = model_validation_config->ai_image_width,
        .height = model_validation_config->ai_image_height,
        .chroma_subsampling = JPEG_420_SUBSAMPLING,
        .quality = model_validation_config->ai_image_quality};

    ret = ai_jpeg_decode(ai_jpeg_data_copy, model_validation_config->ai_image_size,
                         &ai_decode_config, &ai_raw_data, &ai_raw_size);
    if (ret != AICAM_OK)
    {
        LOG_SVC_ERROR("Failed to decode AI JPEG: %d", ret);
        goto cleanup;
    }

    // Step 2: Color convert for AI
    ret = ai_color_convert(ai_raw_data, ai_decode_config.width, ai_decode_config.height,
                           DMA2D_INPUT_YCBCR, 1, ai_decode_config.chroma_subsampling, &ai_rgb_data, &ai_raw_size, DMA2D_OUTPUT_RGB888);
    if (ret != AICAM_OK)
    {
        LOG_SVC_ERROR("Failed to convert color for AI: %d", ret);
        goto cleanup;
    }

    // Return AI decode buffer
    device_ioctl(jpeg_dev, JPEGC_CMD_RETURN_DEC_BUFFER, ai_raw_data, 0);
    ai_raw_data = NULL; // Mark as freed

    // Step 3: Perform AI inference
    LOG_SVC_INFO("Performing AI inference");
    nn_result_t nn_result;
    memset(&nn_result, 0, sizeof(nn_result));
    int nn_ret = nn_inference_frame(ai_rgb_data, ai_raw_size, &nn_result);
    if (nn_ret != 0)
    {
        LOG_SVC_ERROR("AI inference failed: %d", nn_ret);
        ret = AICAM_ERROR;
        goto cleanup;
    }
    LOG_SVC_INFO("AI inference completed: %d detections", nn_result.od.nb_detect);

    // Free AI RGB data
    buffer_free(ai_rgb_data);
    ai_rgb_data = NULL; // Mark as freed

    // Step 4: Decode drawing JPEG (large size for drawing)
    ai_jpeg_decode_config_t draw_decode_config = {
        .width = model_validation_config->draw_image_width,
        .height = model_validation_config->draw_image_height,
        .chroma_subsampling = JPEG_420_SUBSAMPLING,
        .quality = model_validation_config->draw_image_quality};

    draw_jepg_data_copy = buffer_calloc(1, model_validation_config->draw_image_size);
    if (!draw_jepg_data_copy)
    {
        LOG_SVC_ERROR("Failed to allocate draw JPEG data copy");
        ret = AICAM_ERROR_NO_MEMORY;
        goto cleanup;
    }
    memcpy(draw_jepg_data_copy, model_validation_config->draw_image_data, model_validation_config->draw_image_size);

    ret = ai_jpeg_decode(draw_jepg_data_copy, model_validation_config->draw_image_size,
                         &draw_decode_config, &draw_raw_data, &draw_raw_size);
    if (ret != AICAM_OK)
    {
        LOG_SVC_ERROR("Failed to decode draw JPEG: %d", ret);
        goto cleanup;
    }

    // Step 5: Color convert for drawing
    ret = ai_color_convert(draw_raw_data, draw_decode_config.width, draw_decode_config.height,
                           DMA2D_INPUT_YCBCR, 0, draw_decode_config.chroma_subsampling, &draw_rgb_data, &draw_raw_size, DMA2D_OUTPUT_RGB565);
    if (ret != AICAM_OK)
    {
        LOG_SVC_ERROR("Failed to convert color for drawing: %d", ret);
        goto cleanup;
    }

    // Return draw decode buffer
    device_ioctl(jpeg_dev, JPEGC_CMD_RETURN_DEC_BUFFER, draw_raw_data, 0);
    draw_raw_data = NULL; // Mark as freed

    // Step 6: Draw AI results on image
    if (nn_result.od.nb_detect > 0 || nn_result.mpe.nb_detect > 0)
    {
        // Initialize AI draw service if not already initialized
        if (!ai_draw_is_initialized())
        {
            ai_draw_config_t draw_config;
            ai_draw_get_default_config(&draw_config);
            draw_config.image_width = draw_decode_config.width;
            draw_config.image_height = draw_decode_config.height;

            aicam_result_t draw_init_ret = ai_draw_service_init(&draw_config);
            if (draw_init_ret != AICAM_OK)
            {
                LOG_SVC_WARN("Failed to initialize AI draw service: %d", draw_init_ret);
            }
        }

        // Draw AI results
        if (ai_draw_is_initialized())
        {
            aicam_result_t draw_ret = ai_draw_results(draw_rgb_data, draw_decode_config.width,
                                                      draw_decode_config.height, &nn_result);
            if (draw_ret != AICAM_OK)
            {
                LOG_SVC_WARN("Failed to draw AI results: %d", draw_ret);
            }
            else
            {
                LOG_SVC_INFO("AI results drawn on image");
            }
        }
    }
    else
    {
        LOG_SVC_INFO("No AI results to draw");
    }

    // Step 7: Encode final image to JPEG
    ai_jpeg_encode_config_t encode_config = {
        .width = draw_decode_config.width,
        .height = draw_decode_config.height,
        .chroma_subsampling = JPEG_420_SUBSAMPLING,
        .quality = 90};

    uint32_t output_jpeg_size = 0;
    ret = ai_jpeg_encode(draw_rgb_data, draw_raw_size, &encode_config,
                         &output_jpeg, &output_jpeg_size);
    if (ret != AICAM_OK)
    {
        LOG_SVC_ERROR("Failed to encode final JPEG: %d", ret);
        goto cleanup;
    }

    // Step 8: Fill result structure
    memcpy(&result->ai_result, &nn_result, sizeof(nn_result));
    result->output_jpeg = output_jpeg;
    result->output_jpeg_size = output_jpeg_size;
    result->processing_time_ms = osKernelGetTickCount() - start_time;
    result->success = AICAM_TRUE;

    // Free draw RGB data
    buffer_free(draw_rgb_data);
    draw_rgb_data = NULL; // Mark as freed

    LOG_SVC_INFO("Single image inference completed: %d detections, %d ms, output=%d bytes",
                 nn_result.od.nb_detect, result->processing_time_ms, output_jpeg_size);

    // Success - only free the remaining buffers
    if (ai_jpeg_data_copy)
    {
        buffer_free(ai_jpeg_data_copy);
    }
    if (draw_jepg_data_copy)
    {
        buffer_free(draw_jepg_data_copy);
    }

    return AICAM_OK;

cleanup:
    // Comprehensive cleanup on error
    if (ai_jpeg_data_copy)
    {
        buffer_free(ai_jpeg_data_copy);
    }
    if (ai_raw_data)
    {
        device_ioctl(jpeg_dev, JPEGC_CMD_RETURN_DEC_BUFFER, ai_raw_data, 0);
    }
    if (ai_rgb_data)
    {
        buffer_free(ai_rgb_data);
    }
    if (draw_jepg_data_copy)
    {
        buffer_free(draw_jepg_data_copy);
    }
    if (draw_raw_data)
    {
        device_ioctl(jpeg_dev, JPEGC_CMD_RETURN_DEC_BUFFER, draw_raw_data, 0);
    }
    if (draw_rgb_data)
    {
        buffer_free(draw_rgb_data);
    }
    if (output_jpeg)
    {
        device_ioctl(jpeg_dev, JPEGC_CMD_RETURN_ENC_BUFFER, output_jpeg, 0);
    }

    return ret;
}

void ai_jpeg_free_buffer(uint8_t *buffer)
{
    if (buffer) {
        // Return JPEG encode buffer
        device_t *jpeg_dev = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (jpeg_dev) {
            device_ioctl(jpeg_dev, JPEGC_CMD_RETURN_ENC_BUFFER, buffer, 0);
        }
    }
}
