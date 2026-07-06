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
#include "Services/Video/video_stream_hub.h"
#include "mqtt_service.h"
#include "drtc.h"
#include "common_utils.h"

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

/* ==================== Continuous AI Telemetry Context ==================== */

#define AI_TELEMETRY_FLAG_RESULT    0x0001U
#define AI_TELEMETRY_MAX_DETECTS    32   // matches max_detections in the AI configs
#define AI_TELEMETRY_MAX_KEYPOINTS  64

/**
 * @brief One published telemetry beat, deep-copied at inference time
 * @details The pp module rewrites its output buffers on every inference (on
 *          any path, including captures), so the pointer-backed payloads in
 *          nn_result_t are rebased into the telemetry-owned buffers below.
 */
typedef struct {
    nn_result_t result;                    // Result with detect/keypoint pointers rebased
    uint32_t frame_id;                     // Sensor frame the inference ran on
    uint32_t inference_time_ms;            // Measured wall time of this inference
    uint64_t timestamp_ms;                 // RTC time the result was snapshotted
    char model_name[64];                   // From nn_model_info_t.name
    aicam_bool_t valid;                    // Snapshot holds a publishable result
} ai_telemetry_snapshot_t;

static struct {
    aicam_bool_t initialized;              // Telemetry unit initialization status
    volatile aicam_bool_t task_running;    // Publisher task loop gate
    volatile aicam_bool_t task_exited;     // Publisher task exit handshake
    osThreadId_t task_handle;              // Publisher task handle
    osEventFlagsId_t event_flags;          // New-result signal to the publisher task
    osMutexId_t snapshot_mutex;            // Guards snapshot and the copy buffers
    ai_telemetry_snapshot_t snapshot;      // Latest result handed to the publisher
    od_detect_t *od_detects;               // Deep-copy destination (OD)
    mpe_detect_t *mpe_detects;             // Deep-copy destination (MPE)
    spe_keypoint_t *spe_keypoints;         // Deep-copy destination (SPE)
    iseg_detect_t *iseg_detects;           // Deep-copy destination (ISEG, masks omitted)
    video_hub_subscriber_id_t hub_sub_id;  // Hub subscription that holds the pipeline running
} g_ai_telemetry = {0};

static uint8_t ai_telemetry_task_stack[1024 * 4] ALIGN_32 IN_PSRAM;

/* ==================== Internal Function Declarations ==================== */

static void ai_telemetry_result_callback(const nn_result_t *result, uint32_t frame_id,
                                         uint32_t inference_time_ms, void *user_data);
static aicam_result_t ai_telemetry_hub_frame_cb(const video_hub_frame_t *frame, void *user_data);
static void ai_telemetry_task(void *argument);
static void ai_telemetry_publish(void);
static void ai_telemetry_reconcile_pipeline(void);
static void ai_telemetry_invalidate_snapshot(void);
static aicam_result_t ai_telemetry_init(void);
static aicam_bool_t ai_telemetry_stop_task(void);
static void ai_telemetry_deinit(void);

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

static aicam_result_t ai_reload_model_restart_pipeline(void);

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

    // Telemetry is auxiliary: a failure here must not take down the service
    if (ai_telemetry_init() != AICAM_OK) {
        LOG_SVC_WARN("AI telemetry unavailable, continuing without it");
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

    // Order matters: stop the telemetry task and unregister the result
    // callback first (kills the reconciler so it cannot restart the pipeline),
    // then stop the pipeline so the AI node thread drains any in-flight
    // callback, and only then free the telemetry snapshot resources.
    ai_telemetry_stop_task();

    // Stop pipeline
    ai_pipeline_stop();

    ai_telemetry_deinit();
    
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

    /* Encoder session is torn down with the camera pipeline; drop the stale
     * SPS/PPS cache so the next pipeline start re-extracts fresh params.
     * Otherwise the MSE player is fed stale SPS/PPS and the preview stays
     * black after a model reload / pipeline restart. */
    if (video_hub_is_initialized()) {
        video_hub_invalidate_sps_pps();
    }

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
    camera_config.overlay_results = config->overlay_results;
    
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
    ai_config.inference_interval_ms = config->inference_interval_ms;
    ai_config.enabled = config->ai_enabled;
    ai_config.overlay_results = config->overlay_results;
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

aicam_result_t ai_set_overlay_results(aicam_bool_t overlay_results)
{
    // Update and persist the configuration first, so the setting also
    // takes effect (via config) when the pipeline is not running
    g_ai_service.config.overlay_results = overlay_results;
    json_config_set_overlay_results(overlay_results);

    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node || !g_ai_service.camera_node) {
        LOG_SVC_INFO("AI overlay results %s (pipeline not active, saved to config)",
                     overlay_results ? "enabled" : "disabled");
        return AICAM_OK;
    }

    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    ai_config.overlay_results = overlay_results;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }

    // Update camera node config
    video_camera_config_t camera_config;
    result = video_camera_node_get_config(g_ai_service.camera_node, &camera_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get camera node config: %d", result);
        return result;
    }
    camera_config.overlay_results = overlay_results;
    result = video_camera_node_set_config(g_ai_service.camera_node, &camera_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set camera node config: %d", result);
        return result;
    }

    LOG_SVC_INFO("AI overlay results %s", overlay_results ? "enabled" : "disabled");
    return AICAM_OK;
}

aicam_bool_t ai_get_overlay_results(void)
{
    return g_ai_service.config.overlay_results;
}

aicam_result_t ai_set_inference_interval_ms(uint32_t interval_ms)
{
    // Any uint32 is a valid interval (0 = every frame); range is validated at
    // the API boundary where the request value is still a JSON number.

    // Update and persist the configuration first, so the setting also
    // takes effect (via config) when the pipeline is not running
    g_ai_service.config.inference_interval_ms = interval_ms;
    json_config_set_inference_interval_ms(interval_ms);

    if (!g_ai_service.ai_pipeline_initialized || !g_ai_service.ai_node) {
        LOG_SVC_INFO("AI inference interval set to %u ms (pipeline not active, saved to config)",
                     (unsigned)interval_ms);
        return AICAM_OK;
    }

    // Update AI node configuration
    video_ai_config_t ai_config;
    aicam_result_t result = video_ai_node_get_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to get AI node config: %d", result);
        return result;
    }
    ai_config.inference_interval_ms = interval_ms;
    result = video_ai_node_set_config(g_ai_service.ai_node, &ai_config);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to set AI node config: %d", result);
        return result;
    }

    LOG_SVC_INFO("AI inference interval set to %u ms", (unsigned)interval_ms);
    return AICAM_OK;
}

uint32_t ai_get_inference_interval_ms(void)
{
    return g_ai_service.config.inference_interval_ms;
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

    // Drop any telemetry beat latched against the outgoing model: the AI node
    // thread is now drained (no new beat), and the reload below frees the pp
    // class-label/keypoint metadata that a latched snapshot still points into.
    ai_telemetry_invalidate_snapshot();

    //reload model
    result = video_ai_node_reload_model(g_ai_service.ai_node);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to reload AI model: %d", result);
        if (device_service_camera_start() == AICAM_OK) {
            ai_reload_model_restart_pipeline();
        }
        return result;
    }

    //start camera device
    result = device_service_camera_start();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to start camera device: %d", result);
        return result;
    }

    //Restart pipelines when a preview subscriber is already attached (e.g. a
    //direct /model/reload call with preview active) so frame production
    //resumes and the still-connected subscriber receives the encoder's fresh
    //IDR + SPS/PPS immediately. When preview was stopped first (the web
    //feature-debug upload flow leaves no subscribers here), leave the pipeline
    //stopped: the subsequent /preview/start re-subscribes and the Hub
    //auto-starts the pipeline, which captures the encoder's first IDR instead
    //of dropping it (an unconditional start here would discard that IDR while
    //there are zero subscribers, delaying preview recovery by one GOP).
    ai_reload_model_restart_pipeline();

    //deint draw service
    // result = ai_draw_service_deinit();
    // if (result != AICAM_OK) {
    //     LOG_SVC_ERROR("Failed to reset draw service: %d", result);
    //     return result;
    // }

    return AICAM_OK;
}

/* Restart the AI pipelines after a model reload, but only when the Video Hub
 * currently has subscribers. See ai_reload_model() for the rationale. */
static aicam_result_t ai_reload_model_restart_pipeline(void)
{
    if (!video_hub_is_initialized() || !video_hub_has_subscribers()) {
        return AICAM_OK;
    }

    aicam_result_t result = ai_pipeline_start();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to restart AI pipelines after model reload: %d", result);
    }
    return result;
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

/* ==================== Continuous AI Telemetry ==================== */

/**
 * @brief Snapshot a fresh inference result for the telemetry publisher
 * @details Runs on the AI node's processing thread. Deep-copies the
 *          pointer-backed payloads before the pp output buffers can be
 *          rewritten by the next inference, then signals the publisher task.
 */
static void ai_telemetry_result_callback(const nn_result_t *result, uint32_t frame_id,
                                         uint32_t inference_time_ms, void *user_data)
{
    (void)user_data;

    if (!g_ai_telemetry.initialized || !mqtt_service_get_telemetry_enabled()) {
        return;
    }

    // One beat per inference; the AI node's pacing sets the rate

    // Drop this beat rather than stall the inference thread behind a publish
    if (osMutexAcquire(g_ai_telemetry.snapshot_mutex, 10) != osOK) {
        return;
    }

    ai_telemetry_snapshot_t *snap = &g_ai_telemetry.snapshot;
    memcpy(&snap->result, result, sizeof(nn_result_t));

    // Rebase the pointer-backed payloads into telemetry-owned buffers
    switch (result->type) {
        case PP_TYPE_OD:
            if (result->od.detects && result->od.nb_detect > 0) {
                uint8_t count = result->od.nb_detect;
                if (count > AI_TELEMETRY_MAX_DETECTS) count = AI_TELEMETRY_MAX_DETECTS;
                memcpy(g_ai_telemetry.od_detects, result->od.detects, count * sizeof(od_detect_t));
                snap->result.od.detects = g_ai_telemetry.od_detects;
                snap->result.od.nb_detect = count;
            }
            break;

        case PP_TYPE_MPE:
            if (result->mpe.detects && result->mpe.nb_detect > 0) {
                uint8_t count = result->mpe.nb_detect;
                if (count > AI_TELEMETRY_MAX_DETECTS) count = AI_TELEMETRY_MAX_DETECTS;
                // Keypoints are inline in mpe_detect_t; name/connection pointers
                // reference model metadata that is stable while the model is loaded
                memcpy(g_ai_telemetry.mpe_detects, result->mpe.detects, count * sizeof(mpe_detect_t));
                snap->result.mpe.detects = g_ai_telemetry.mpe_detects;
                snap->result.mpe.nb_detect = count;
            }
            break;

        case PP_TYPE_SPE:
            if (result->spe.keypoints && result->spe.nb_keypoints > 0) {
                uint32_t count = result->spe.nb_keypoints;
                if (count > AI_TELEMETRY_MAX_KEYPOINTS) count = AI_TELEMETRY_MAX_KEYPOINTS;
                memcpy(g_ai_telemetry.spe_keypoints, result->spe.keypoints, count * sizeof(spe_keypoint_t));
                snap->result.spe.keypoints = g_ai_telemetry.spe_keypoints;
                snap->result.spe.nb_keypoints = count;
            }
            break;

        case PP_TYPE_ISEG:
            if (result->iseg.detects && result->iseg.nb_detect > 0) {
                uint8_t count = result->iseg.nb_detect;
                if (count > AI_TELEMETRY_MAX_DETECTS) count = AI_TELEMETRY_MAX_DETECTS;
                memcpy(g_ai_telemetry.iseg_detects, result->iseg.detects, count * sizeof(iseg_detect_t));
                // Masks stay in the shared pp buffer; the JSON serializer only
                // reports mask_size, so drop the pointers rather than race on them
                for (uint8_t i = 0; i < count; i++) {
                    g_ai_telemetry.iseg_detects[i].mask = NULL;
                }
                snap->result.iseg.detects = g_ai_telemetry.iseg_detects;
                snap->result.iseg.nb_detect = count;
            }
            break;

        default:
            // Remaining types carry no payload the JSON serializer dereferences
            break;
    }

    snap->frame_id = frame_id;
    snap->inference_time_ms = inference_time_ms;
    snap->timestamp_ms = rtc_get_timestamp_ms();
    nn_model_info_t model_info;
    if (ai_service_get_model_info(&model_info) == AICAM_OK) {
        strncpy(snap->model_name, model_info.name, sizeof(snap->model_name) - 1);
        snap->model_name[sizeof(snap->model_name) - 1] = '\0';
    } else {
        snap->model_name[0] = '\0';
    }
    snap->valid = AICAM_TRUE;

    osMutexRelease(g_ai_telemetry.snapshot_mutex);

    osEventFlagsSet(g_ai_telemetry.event_flags, AI_TELEMETRY_FLAG_RESULT);
}

/**
 * @brief Hub frame sink for the telemetry subscription
 * @details Telemetry holds a hub subscription only to keep the AI pipeline
 *          running (the hub starts/stops it by subscriber refcount); it does
 *          not consume encoded video, so this callback discards the frame.
 */
static aicam_result_t ai_telemetry_hub_frame_cb(const video_hub_frame_t *frame, void *user_data)
{
    (void)frame;
    (void)user_data;
    return AICAM_OK;
}

/**
 * @brief Keep the AI pipeline running while telemetry is enabled
 * @details Continuous inference otherwise only runs while the video hub has
 *          subscribers (preview/RTSP/RTMP), so on a headless unit telemetry
 *          holds its own hub subscription. Going through the hub's refcount
 *          (rather than calling ai_pipeline_start/stop directly) lets the hub
 *          serialize start/stop against real viewers under its own mutex, so a
 *          viewer subscribing as telemetry is disabled cannot be left black.
 */
static void ai_telemetry_reconcile_pipeline(void)
{
    aicam_bool_t desired = mqtt_service_get_telemetry_enabled();

    if (desired && g_ai_service.ai_pipeline_initialized &&
        g_ai_telemetry.hub_sub_id == VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        video_hub_subscriber_id_t id = video_hub_subscribe(
            VIDEO_HUB_SUBSCRIBER_CUSTOM, ai_telemetry_hub_frame_cb, NULL, NULL);
        if (id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
            g_ai_telemetry.hub_sub_id = id;
            LOG_SVC_INFO("Telemetry enabled: holding AI pipeline via hub subscription %ld",
                         (long)id);
        }
    } else if (!desired && g_ai_telemetry.hub_sub_id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        video_hub_unsubscribe(g_ai_telemetry.hub_sub_id);
        LOG_SVC_INFO("Telemetry disabled: released hub subscription %ld",
                     (long)g_ai_telemetry.hub_sub_id);
        g_ai_telemetry.hub_sub_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    }
}

/**
 * @brief Invalidate the latched telemetry snapshot
 * @details Called from the model-reload path after the AI node thread is
 *          drained but before the pp module frees the class-label / keypoint
 *          metadata that the snapshot's copied detect structs still point into.
 *          Without this a beat latched before the reload would be serialized
 *          against freed pointers.
 */
static void ai_telemetry_invalidate_snapshot(void)
{
    if (!g_ai_telemetry.snapshot_mutex) {
        return;
    }
    if (osMutexAcquire(g_ai_telemetry.snapshot_mutex, osWaitForever) != osOK) {
        return;
    }
    g_ai_telemetry.snapshot.valid = AICAM_FALSE;
    osMutexRelease(g_ai_telemetry.snapshot_mutex);
    if (g_ai_telemetry.event_flags) {
        osEventFlagsClear(g_ai_telemetry.event_flags, AI_TELEMETRY_FLAG_RESULT);
    }
}

/**
 * @brief Build and publish one telemetry message from the current snapshot
 * @details The snapshot mutex is held only across the JSON build; the network
 *          publish (which can block on a degraded link) runs unlocked.
 */
static void ai_telemetry_publish(void)
{
    if (osMutexAcquire(g_ai_telemetry.snapshot_mutex, osWaitForever) != osOK) {
        return;
    }

    if (!g_ai_telemetry.snapshot.valid) {
        osMutexRelease(g_ai_telemetry.snapshot_mutex);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        osMutexRelease(g_ai_telemetry.snapshot_mutex);
        LOG_SVC_ERROR("Failed to create telemetry payload");
        return;
    }

    cJSON_AddStringToObject(root, "type", "ai_telemetry");
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)g_ai_telemetry.snapshot.timestamp_ms);
    cJSON_AddNumberToObject(root, "frame_id", g_ai_telemetry.snapshot.frame_id);
    cJSON_AddStringToObject(root, "model_name", g_ai_telemetry.snapshot.model_name);
    cJSON_AddNumberToObject(root, "inference_time_ms", g_ai_telemetry.snapshot.inference_time_ms);

    // Explicit null on absence keeps the payload shape stable for consumers
    cJSON *ai_json = NULL;
    if (g_ai_telemetry.snapshot.result.is_valid) {
        ai_json = nn_create_ai_result_json(&g_ai_telemetry.snapshot.result);
    }
    if (ai_json) {
        cJSON_AddItemToObject(root, "ai_result", ai_json);
    } else {
        cJSON_AddItemToObject(root, "ai_result", cJSON_CreateNull());
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    osMutexRelease(g_ai_telemetry.snapshot_mutex);

    if (!json_str) {
        LOG_SVC_ERROR("Failed to serialize telemetry payload");
        return;
    }

    int publish_result = mqtt_service_publish_telemetry(json_str);
    if (publish_result < 0) {
        // Expected while the broker is unreachable; QoS/backpressure errors land here too
        LOG_SVC_DEBUG("Telemetry publish failed: %d", publish_result);
    }

    buffer_free(json_str);
}

/**
 * @brief Telemetry publisher task
 * @details Publishes on each new-result signal; the wait timeout doubles as
 *          the pipeline-reconciliation tick so config changes apply within ~1 s.
 */
static void ai_telemetry_task(void *argument)
{
    (void)argument;

    while (g_ai_telemetry.task_running) {
        uint32_t flags = osEventFlagsWait(g_ai_telemetry.event_flags, AI_TELEMETRY_FLAG_RESULT,
                                          osFlagsWaitAny, 1000);

        if (!g_ai_telemetry.task_running) {
            break;
        }

        ai_telemetry_reconcile_pipeline();

        if ((int32_t)flags >= 0 && (flags & AI_TELEMETRY_FLAG_RESULT)) {
            ai_telemetry_publish();
        }
    }

    g_ai_telemetry.task_exited = AICAM_TRUE;
    osThreadExit();
}

/**
 * @brief Initialize the telemetry unit and hook it onto the AI node
 * @note Telemetry is auxiliary: failure here must not fail service start
 */
static aicam_result_t ai_telemetry_init(void)
{
    if (g_ai_telemetry.initialized) {
        return AICAM_OK;
    }

    memset(&g_ai_telemetry, 0, sizeof(g_ai_telemetry));
    // memset zeroes hub_sub_id to 0, which is a valid subscriber id
    g_ai_telemetry.hub_sub_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;

    g_ai_telemetry.snapshot_mutex = osMutexNew(NULL);
    g_ai_telemetry.event_flags = osEventFlagsNew(NULL);
    g_ai_telemetry.od_detects = buffer_calloc(AI_TELEMETRY_MAX_DETECTS, sizeof(od_detect_t));
    g_ai_telemetry.mpe_detects = buffer_calloc(AI_TELEMETRY_MAX_DETECTS, sizeof(mpe_detect_t));
    g_ai_telemetry.spe_keypoints = buffer_calloc(AI_TELEMETRY_MAX_KEYPOINTS, sizeof(spe_keypoint_t));
    g_ai_telemetry.iseg_detects = buffer_calloc(AI_TELEMETRY_MAX_DETECTS, sizeof(iseg_detect_t));

    if (!g_ai_telemetry.snapshot_mutex || !g_ai_telemetry.event_flags ||
        !g_ai_telemetry.od_detects || !g_ai_telemetry.mpe_detects ||
        !g_ai_telemetry.spe_keypoints || !g_ai_telemetry.iseg_detects) {
        LOG_SVC_ERROR("Failed to allocate telemetry resources");
        ai_telemetry_deinit();
        return AICAM_ERROR_NO_MEMORY;
    }

    g_ai_telemetry.task_running = AICAM_TRUE;
    const osThreadAttr_t task_attributes = {
        .name = "AITelemetry",
        .stack_mem = ai_telemetry_task_stack,
        .stack_size = sizeof(ai_telemetry_task_stack),
        .priority = osPriorityNormal,
    };
    g_ai_telemetry.task_handle = osThreadNew(ai_telemetry_task, NULL, &task_attributes);
    if (!g_ai_telemetry.task_handle) {
        LOG_SVC_ERROR("Failed to create telemetry task");
        g_ai_telemetry.task_running = AICAM_FALSE;
        ai_telemetry_deinit();
        return AICAM_ERROR_NO_MEMORY;
    }

    aicam_result_t result = video_ai_node_set_result_callback(g_ai_service.ai_node,
                                                              ai_telemetry_result_callback, NULL);
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to register telemetry result callback: %d", result);
        ai_telemetry_deinit();
        return result;
    }

    g_ai_telemetry.initialized = AICAM_TRUE;
    LOG_SVC_INFO("AI telemetry unit initialized");
    return AICAM_OK;
}

/**
 * @brief Stop the telemetry task, unregister the callback, release the hub hold
 * @details Idempotent. After this returns TRUE, the reconciler and publisher
 *          are gone and no NEW result callback will fire, but a callback
 *          already in flight on the AI node thread may still be running — the
 *          caller must stop the AI pipeline before freeing snapshot resources.
 * @return AICAM_TRUE when no task remains (never started, or joined);
 *         AICAM_FALSE when the task did not exit within the join window, in
 *         which case the task handle and hub hold are left in place so a
 *         later call can retry the join.
 */
static aicam_bool_t ai_telemetry_stop_task(void)
{
    g_ai_telemetry.initialized = AICAM_FALSE;

    if (g_ai_service.ai_node) {
        video_ai_node_set_result_callback(g_ai_service.ai_node, NULL, NULL);
    }

    if (g_ai_telemetry.task_handle) {
        g_ai_telemetry.task_running = AICAM_FALSE;
        osEventFlagsSet(g_ai_telemetry.event_flags, AI_TELEMETRY_FLAG_RESULT);
        // Wait for the task to leave its loop. Worst-case exit latency is one
        // flag-wait timeout (1 s) plus one publish blocked on a degraded link
        // (network timeout, 3 s default), so 6 s covers it with margin.
        for (uint32_t waited_ms = 0; !g_ai_telemetry.task_exited && waited_ms < 6000; waited_ms += 100) {
            osDelay(100);
        }
        if (!g_ai_telemetry.task_exited) {
            LOG_SVC_ERROR("Telemetry task did not exit within the join window");
            return AICAM_FALSE;
        }
        g_ai_telemetry.task_handle = NULL;
    }

    // Release the pipeline hold now that the reconciler can no longer run
    if (g_ai_telemetry.hub_sub_id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        video_hub_unsubscribe(g_ai_telemetry.hub_sub_id);
        g_ai_telemetry.hub_sub_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    }

    return AICAM_TRUE;
}

/**
 * @brief Tear down the telemetry unit
 * @warning Frees the snapshot mutex and copy buffers. The caller must ensure
 *          the AI node thread is stopped first (so no result callback is in
 *          flight); ai_service_stop does this via ai_pipeline_stop(). The
 *          init-failure paths are safe because the AI pipeline is not yet
 *          running when ai_telemetry_init runs. If the publisher task cannot
 *          be joined, the shared resources are deliberately leaked rather
 *          than freed under a live task.
 */
static void ai_telemetry_deinit(void)
{
    if (!ai_telemetry_stop_task()) {
        LOG_SVC_ERROR("Leaking telemetry resources: publisher task still running");
        return;
    }

    if (g_ai_telemetry.event_flags) {
        osEventFlagsDelete(g_ai_telemetry.event_flags);
        g_ai_telemetry.event_flags = NULL;
    }
    if (g_ai_telemetry.snapshot_mutex) {
        osMutexDelete(g_ai_telemetry.snapshot_mutex);
        g_ai_telemetry.snapshot_mutex = NULL;
    }
    if (g_ai_telemetry.od_detects) {
        buffer_free(g_ai_telemetry.od_detects);
        g_ai_telemetry.od_detects = NULL;
    }
    if (g_ai_telemetry.mpe_detects) {
        buffer_free(g_ai_telemetry.mpe_detects);
        g_ai_telemetry.mpe_detects = NULL;
    }
    if (g_ai_telemetry.spe_keypoints) {
        buffer_free(g_ai_telemetry.spe_keypoints);
        g_ai_telemetry.spe_keypoints = NULL;
    }
    if (g_ai_telemetry.iseg_detects) {
        buffer_free(g_ai_telemetry.iseg_detects);
        g_ai_telemetry.iseg_detects = NULL;
    }
}

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
    config->inference_interval_ms = json_config_get_inference_interval_ms();
    config->ai_enabled = AICAM_TRUE;
    config->overlay_results = json_config_get_overlay_results();
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
    config->inference_interval_ms = json_config_get_inference_interval_ms();
    config->ai_enabled = AICAM_TRUE;
    config->overlay_results = json_config_get_overlay_results();
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
