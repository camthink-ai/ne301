/**
 * @file rtmp_service_v2.c
 * @brief RTMP Service Implementation - Video Hub mode
 * @details 
 *   RTMP service implementation using Video Stream Hub mode
 *   No direct operation of camera_dev and encoder_dev
 *   No rtmp_stream_task task
 *   Subscribe to encoded frames via video_hub_subscribe()
 *   Receive frame data via callback functions and send to RTMP server
 */

#include "rtmp_service.h"
#include "Services/Video/video_stream_hub.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "Core/Data/buffer_mgr.h"
#include "service_init.h"
#include "json_config_mgr.h"  // configuration persistence
#include "Services/Device/device_service.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "Hal/drtc.h"
#include "common_utils.h"

/* ==================== Configuration ==================== */

#define RTMP_DEFAULT_RECONNECT_INTERVAL 5000
#define RTMP_DEFAULT_MAX_RECONNECT      10
#define RTMP_DEFAULT_TIMEOUT            10000
#define RTMP_DEFAULT_WIDTH              1280
#define RTMP_DEFAULT_HEIGHT             720
#define RTMP_DEFAULT_FPS                30
#define RTMP_DEFAULT_BITRATE            2500
#define RTMP_DEFAULT_GOP                60

/* Async send queue configuration */
#define RTMP_SEND_QUEUE_SIZE            8      // Frame queue size
#define RTMP_MAX_FRAME_SIZE             (512*1024)  // Max frame size 512KB
#define RTMP_SEND_TASK_STACK_SIZE       (4096 * 2)
#define RTMP_SEND_TASK_PRIORITY         osPriorityNormal

#define RTMP_AUTOSTART_TASK_STACK_SIZE  (4096 * 2)
#define RTMP_AUTOSTART_TASK_PRIORITY    osPriorityNormal
#define RTMP_AUTOSTART_STA_SLICE_MS     30000   // one wait slice for STA
#define RTMP_AUTOSTART_STA_BUDGET_MS    900000  // give up after 15 min without STA
#define RTMP_AUTOSTART_MAX_ATTEMPTS     5
#define RTMP_AUTOSTART_RETRY_MS         5000

/* ==================== Frame Queue for Async Send ==================== */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t timestamp_ms;
    aicam_bool_t is_keyframe;
    aicam_bool_t valid;
} rtmp_frame_entry_t;

typedef struct {
    rtmp_frame_entry_t frames[RTMP_SEND_QUEUE_SIZE];
    volatile uint32_t head;     // Write position
    volatile uint32_t tail;     // Read position
    osMutexId_t mutex;
    osSemaphoreId_t sem_data;   // Signal new data available
    uint8_t *frame_buffers[RTMP_SEND_QUEUE_SIZE];  // Pre-allocated buffers
} rtmp_send_queue_t;

static uint8_t rtmp_send_task_stack[RTMP_SEND_TASK_STACK_SIZE] ALIGN_32 IN_PSRAM;
static uint8_t rtmp_autostart_task_stack[RTMP_AUTOSTART_TASK_STACK_SIZE] ALIGN_32 IN_PSRAM;

/* ==================== Service Context ==================== */

typedef struct {
    // Service state
    aicam_bool_t initialized;
    aicam_bool_t running;
    service_state_t service_state;
    rtmp_stream_state_t stream_state;

    // Configuration
    rtmp_service_config_t config;

    // Statistics
    rtmp_service_stats_t stats;

    // RTMP Publisher
    rtmp_publisher_t *publisher;
    
    // Video Hub subscription
    video_hub_subscriber_id_t hub_subscriber_id;
    
    // SPS/PPS state
    aicam_bool_t sps_pps_sent;

    // Synchronization
    osMutexId_t mutex;
    osEventFlagsId_t event_flags;

    // Reconnect control
    uint32_t reconnect_attempts;
    aicam_bool_t pending_reconnect;
    uint32_t last_reconnect_time;

    // Event callbacks
    rtmp_event_callback_t callbacks[RTMP_MAX_CALLBACKS];
    void *callback_user_data[RTMP_MAX_CALLBACKS];
    uint32_t callback_count;
    
    // Timestamp
    uint32_t stream_start_time;
    uint64_t first_frame_timestamp;      // First frame's encoder timestamp (for relative DTS)
    aicam_bool_t first_frame_received;   // Flag to track if first frame timestamp is captured
    
    // Async send queue
    rtmp_send_queue_t send_queue;
    osThreadId_t send_task_handle;
    aicam_bool_t send_task_running;
    aicam_bool_t task_need_cleanup;

    osThreadId_t autostart_task_handle;
    aicam_bool_t autostart_task_running;
} rtmp_service_context_t;

static rtmp_service_context_t g_rtmp_ctx = {0};

/* Event flag definitions */
#define RTMP_EVENT_FLAG_STOP        (1UL << 0)
#define RTMP_EVENT_FLAG_CONNECTED   (1UL << 1)
#define RTMP_EVENT_FLAG_ERROR       (1UL << 2)

/* ==================== Forward Declarations ==================== */

static void notify_event(rtmp_event_type_t event, int error_code, const char *message);
static aicam_result_t rtmp_on_frame(const video_hub_frame_t *frame, void *user_data);
static void rtmp_on_sps_pps(const video_hub_sps_pps_t *sps_pps, void *user_data);
static aicam_result_t try_reconnect(void);
static void rtmp_send_task(void *argument);
static aicam_result_t send_queue_init(rtmp_send_queue_t *queue);
static void send_queue_deinit(rtmp_send_queue_t *queue);
static aicam_result_t send_queue_push(rtmp_send_queue_t *queue, const video_hub_frame_t *frame);
static aicam_result_t send_queue_pop(rtmp_send_queue_t *queue, rtmp_frame_entry_t *out);

/* ==================== Helper Functions ==================== */

/**
 * @brief Notify registered callbacks of an event
 */
static void notify_event(rtmp_event_type_t event, int error_code, const char *message)
{
    rtmp_event_data_t event_data = {
        .event = event,
        .error_code = error_code,
        .error_message = message,
        .reconnect_attempt = g_rtmp_ctx.reconnect_attempts
    };

    for (uint32_t i = 0; i < g_rtmp_ctx.callback_count; i++) {
        if (g_rtmp_ctx.callbacks[i]) {
            g_rtmp_ctx.callbacks[i](&event_data, g_rtmp_ctx.callback_user_data[i]);
        }
    }
}

/**
 * @brief Build full RTMP URL with stream key
 */
static void build_full_url(char *full_url, size_t max_len)
{
    if (g_rtmp_ctx.config.stream_key[0] != '\0') {
        snprintf(full_url, max_len, "%s/%s",
                 g_rtmp_ctx.config.url, g_rtmp_ctx.config.stream_key);
    } else {
        strncpy(full_url, g_rtmp_ctx.config.url, max_len - 1);
        full_url[max_len - 1] = '\0';
    }
}

/* ==================== Async Send Queue Implementation ==================== */

/**
 * @brief Initialize send queue with pre-allocated buffers
 */
static aicam_result_t send_queue_init(rtmp_send_queue_t *queue)
{
    if (!queue) return AICAM_ERROR_INVALID_PARAM;
    
    memset(queue, 0, sizeof(rtmp_send_queue_t));
    
    // Create mutex
    queue->mutex = osMutexNew(NULL);
    if (!queue->mutex) {
        LOG_SVC_ERROR("Failed to create queue mutex");
        return AICAM_ERROR;
    }
    
    // Create semaphore for signaling
    queue->sem_data = osSemaphoreNew(RTMP_SEND_QUEUE_SIZE, 0, NULL);
    if (!queue->sem_data) {
        LOG_SVC_ERROR("Failed to create queue semaphore");
        osMutexDelete(queue->mutex);
        return AICAM_ERROR;
    }
    
    // Pre-allocate frame buffers (32-byte aligned for DMA)
    for (int i = 0; i < RTMP_SEND_QUEUE_SIZE; i++) {
        queue->frame_buffers[i] = (uint8_t *)buffer_malloc_aligned(RTMP_MAX_FRAME_SIZE, 32);
        if (!queue->frame_buffers[i]) {
            LOG_SVC_ERROR("Failed to allocate frame buffer %d", i);
            // Free already allocated
            for (int j = 0; j < i; j++) {
                buffer_free(queue->frame_buffers[j]);
            }
            osSemaphoreDelete(queue->sem_data);
            osMutexDelete(queue->mutex);
            return AICAM_ERROR_NO_MEMORY;
        }
        queue->frames[i].data = queue->frame_buffers[i];
        queue->frames[i].valid = AICAM_FALSE;
    }
    
    queue->head = 0;
    queue->tail = 0;
    
    LOG_SVC_INFO("Send queue initialized: %d slots, %d KB each",
                 RTMP_SEND_QUEUE_SIZE, RTMP_MAX_FRAME_SIZE / 1024);
    return AICAM_OK;
}

/**
 * @brief Deinitialize send queue and free resources
 */
static void send_queue_deinit(rtmp_send_queue_t *queue)
{
    if (!queue) return;
    
    // Free frame buffers
    for (int i = 0; i < RTMP_SEND_QUEUE_SIZE; i++) {
        if (queue->frame_buffers[i]) {
            buffer_free(queue->frame_buffers[i]);
            queue->frame_buffers[i] = NULL;
        }
    }
    
    if (queue->sem_data) {
        osSemaphoreDelete(queue->sem_data);
        queue->sem_data = NULL;
    }
    
    if (queue->mutex) {
        osMutexDelete(queue->mutex);
        queue->mutex = NULL;
    }
    
    LOG_SVC_INFO("Send queue deinitialized");
}

/**
 * @brief Push frame to queue (non-blocking, drops oldest if full)
 * @note Called from Hub callback context - must be fast
 */
static aicam_result_t send_queue_push(rtmp_send_queue_t *queue, const video_hub_frame_t *frame)
{
    if (!queue || !frame || !frame->data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (frame->size > RTMP_MAX_FRAME_SIZE) {
        LOG_SVC_WARN("Frame too large: %lu > %d", (unsigned long)frame->size, RTMP_MAX_FRAME_SIZE);
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(queue->mutex, osWaitForever);
    
    // Get write position
    uint32_t write_pos = queue->head % RTMP_SEND_QUEUE_SIZE;
    rtmp_frame_entry_t *entry = &queue->frames[write_pos];
    
    // Check if overwriting unread data
    if (entry->valid) {
        // Queue is full - overwrite oldest (drop frame)
        g_rtmp_ctx.stats.dropped_frames++;
        static uint32_t last_warn_time = 0;
        uint32_t now = osKernelGetTickCount();
        if (now - last_warn_time >= 1000) {  // Warn at most once per second
            LOG_SVC_WARN("Queue full! head=%lu, tail=%lu, dropped=%lu",
                         (unsigned long)queue->head,
                         (unsigned long)queue->tail,
                         (unsigned long)g_rtmp_ctx.stats.dropped_frames);
            last_warn_time = now;
        }
    }
    
    // Copy frame data
    memcpy(entry->data, frame->data, frame->size);
    entry->size = frame->size;
    // timestamp_ms will be calculated at send time (not at enqueue time)
    entry->timestamp_ms = 0;
    entry->is_keyframe = frame->is_keyframe;
    entry->valid = AICAM_TRUE;
    
    queue->head++;
    
    osMutexRelease(queue->mutex);
    
    // Signal data available
    osSemaphoreRelease(queue->sem_data);
    
    return AICAM_OK;
}

/**
 * @brief Pop frame from queue (blocking with timeout)
 * @return AICAM_OK if frame retrieved, AICAM_ERROR_TIMEOUT if no data
 */
static aicam_result_t send_queue_pop(rtmp_send_queue_t *queue, rtmp_frame_entry_t *out)
{
    if (!queue || !out) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Wait for data (100ms timeout)
    if (osSemaphoreAcquire(queue->sem_data, 100) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    osMutexAcquire(queue->mutex, osWaitForever);
    
    uint32_t read_pos = queue->tail % RTMP_SEND_QUEUE_SIZE;
    rtmp_frame_entry_t *entry = &queue->frames[read_pos];
    
    if (!entry->valid) {
        osMutexRelease(queue->mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Copy to output (data pointer points to queue buffer, caller must use before next pop)
    out->data = entry->data;
    out->size = entry->size;
    out->timestamp_ms = entry->timestamp_ms;
    out->is_keyframe = entry->is_keyframe;
    out->valid = AICAM_TRUE;
    
    // Mark as consumed
    entry->valid = AICAM_FALSE;
    queue->tail++;
    
    osMutexRelease(queue->mutex);
    
    return AICAM_OK;
}

/**
 * @brief Async send task - processes frames from queue
 */
static void rtmp_send_task(void *argument)
{
    (void)argument;
    
    LOG_SVC_INFO("RTMP send task started");
    uint32_t send_count = 0;
    uint32_t last_log_time = 0;
    
    while (g_rtmp_ctx.send_task_running) {
        rtmp_frame_entry_t frame;
        
        // Pop frame from queue (blocks up to 100ms)
        if (send_queue_pop(&g_rtmp_ctx.send_queue, &frame) != AICAM_OK) {
            continue;  // Timeout, check running flag
        }
        
        uint32_t now = osKernelGetTickCount();
        
        // Check connection
        if (!g_rtmp_ctx.publisher || !rtmp_publisher_is_connected(g_rtmp_ctx.publisher)) {
            // Handle reconnect
            if (g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_STREAMING) {
                g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_RECONNECTING;
                g_rtmp_ctx.pending_reconnect = AICAM_TRUE;
                notify_event(RTMP_EVENT_DISCONNECTED, 0, NULL);
            }
            
            if (g_rtmp_ctx.pending_reconnect && g_rtmp_ctx.config.auto_reconnect) {
                uint32_t now = osKernelGetTickCount();
                if (now - g_rtmp_ctx.last_reconnect_time >= g_rtmp_ctx.config.reconnect_interval_ms) {
                    aicam_result_t rc = try_reconnect();
                    if (rc == AICAM_OK) {
                        g_rtmp_ctx.pending_reconnect = AICAM_FALSE;
                    } else if (rc == AICAM_ERROR_REACH_MAX_ATTEMPTS) {
                        // Max attempts reached, stop retrying
                        g_rtmp_ctx.pending_reconnect = AICAM_FALSE;
                        LOG_SVC_INFO("Reconnect failed, stopping stream");
                    }
                    // AICAM_ERROR_BUSY means waiting for interval, continue
                }
            }
            
            // If in ERROR state and not retrying, cleanup and exit task
            if (g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_ERROR && 
                !g_rtmp_ctx.pending_reconnect) {
                LOG_SVC_INFO("Stream in error state, cleanup and exit");
                
                // Unsubscribe from Hub
                if (g_rtmp_ctx.hub_subscriber_id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
                    video_hub_unsubscribe(g_rtmp_ctx.hub_subscriber_id);
                    g_rtmp_ctx.hub_subscriber_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
                    LOG_SVC_INFO("Unsubscribed from Video Hub");
                }
                
                // Disconnect and destroy publisher
                if (g_rtmp_ctx.publisher) {
                    rtmp_publisher_disconnect(g_rtmp_ctx.publisher);
                    rtmp_publisher_destroy(g_rtmp_ctx.publisher);
                    g_rtmp_ctx.publisher = NULL;
                }
                
                notify_event(RTMP_EVENT_STREAM_STOPPED, -1, "Max reconnect attempts reached");
                break;
            }
            
            g_rtmp_ctx.stats.dropped_frames++;
            continue;
        }
        
        // Check SPS/PPS
        if (!g_rtmp_ctx.sps_pps_sent) {
            if (frame.is_keyframe) {
                video_hub_sps_pps_t sps_pps;
                if (video_hub_get_sps_pps(&sps_pps) == AICAM_OK) {
                    rtmp_on_sps_pps(&sps_pps, NULL);
                }
            }
            if (!g_rtmp_ctx.sps_pps_sent) {
                g_rtmp_ctx.stats.dropped_frames++;
                continue;
            }
        }
        
        // Send frame - use send time as timestamp (ensures monotonic DTS even with dropped frames)
        uint32_t send_start = osKernelGetTickCount();
        uint32_t timestamp_ms = send_start - g_rtmp_ctx.stream_start_time;
        int ret = rtmp_publisher_send_video_frame(
            g_rtmp_ctx.publisher,
            frame.data,
            frame.size,
            frame.is_keyframe,
            timestamp_ms
        );
        uint32_t send_time = osKernelGetTickCount() - send_start;
        
        // Warn if send takes too long
        if (send_time > 100) {
            LOG_SVC_WARN("RTMP send slow: %lu ms, size=%lu, keyframe=%d",
                         (unsigned long)send_time, (unsigned long)frame.size, frame.is_keyframe);
        }
        
        if (ret == RTMP_PUB_OK) {
            send_count++;
            g_rtmp_ctx.stats.frames_sent++;
            g_rtmp_ctx.stats.bytes_sent += frame.size;
            if (frame.is_keyframe) {
                g_rtmp_ctx.stats.keyframes_sent++;
            }
            
            // Update stats
            if (g_rtmp_ctx.stats.frames_sent > 0) {
                g_rtmp_ctx.stats.avg_frame_size = 
                    (uint32_t)(g_rtmp_ctx.stats.bytes_sent / g_rtmp_ctx.stats.frames_sent);
            }
            uint32_t elapsed_sec = (now - g_rtmp_ctx.stream_start_time) / 1000;
            if (elapsed_sec > 0) {
                g_rtmp_ctx.stats.current_bitrate_kbps = 
                    (uint32_t)(g_rtmp_ctx.stats.bytes_sent * 8 / elapsed_sec / 1000);
            }
            g_rtmp_ctx.stats.stream_duration_sec = elapsed_sec;
            
            // Log every 5 seconds
            if (now - last_log_time >= 5000) {
                uint32_t queue_depth = (g_rtmp_ctx.send_queue.head - g_rtmp_ctx.send_queue.tail);
                LOG_SVC_DEBUG("RTMP: sent=%lu, dropped=%lu, queue=%lu, last_send=%lu ms",
                              (unsigned long)send_count,
                              (unsigned long)g_rtmp_ctx.stats.dropped_frames,
                              (unsigned long)queue_depth,
                              (unsigned long)send_time);
                last_log_time = now;
                send_count = 0;
            }
        } else {
            g_rtmp_ctx.stats.errors++;
            g_rtmp_ctx.stats.dropped_frames++;
            LOG_SVC_WARN("RTMP send failed: ret=%d, size=%lu", ret, (unsigned long)frame.size);
            rtmp_publisher_disconnect(g_rtmp_ctx.publisher);
        }
    }
    
    // Cleanup send queue if task is exiting due to error
    if (g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_ERROR) {
        send_queue_deinit(&g_rtmp_ctx.send_queue);
        g_rtmp_ctx.task_need_cleanup = AICAM_TRUE;
    }
    
    LOG_SVC_INFO("RTMP send task exiting");
}

/* ==================== Video Hub Callbacks ==================== */

/**
 * @brief SPS/PPS callback - send SPS/PPS to RTMP server when received
 */
static void rtmp_on_sps_pps(const video_hub_sps_pps_t *sps_pps, void *user_data)
{
    (void)user_data;
    
    if (!g_rtmp_ctx.publisher || !sps_pps) {
        return;
    }
    
    if (!rtmp_publisher_is_connected(g_rtmp_ctx.publisher)) {
        LOG_SVC_WARN("RTMP not connected, skip SPS/PPS");
        return;
    }
    
    int ret = rtmp_publisher_send_sps_pps(
        g_rtmp_ctx.publisher,
        sps_pps->sps_data, sps_pps->sps_size,
        sps_pps->pps_data, sps_pps->pps_size
    );
    
    if (ret == RTMP_PUB_OK) {
        g_rtmp_ctx.sps_pps_sent = AICAM_TRUE;
        LOG_SVC_DEBUG("SPS/PPS sent to RTMP: SPS=%lu, PPS=%lu",
                      (unsigned long)sps_pps->sps_size,
                      (unsigned long)sps_pps->pps_size);
    } else {
        LOG_SVC_ERROR("Failed to send SPS/PPS: %d", ret);
        g_rtmp_ctx.stats.errors++;
    }
}

/**
 * @brief Frame callback - sync send or enqueue for async sending
 */
static aicam_result_t rtmp_on_frame(const video_hub_frame_t *frame, void *user_data)
{
    (void)user_data;
    
    if (!frame || !frame->data) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Async mode: enqueue frame, send task handles everything
    if (g_rtmp_ctx.config.async_send) {
        return send_queue_push(&g_rtmp_ctx.send_queue, frame);
    }
    
    // Sync mode: send directly in callback context
    if (g_rtmp_ctx.stream_state != RTMP_STREAM_STATE_STREAMING) {
        return AICAM_ERROR_BUSY;
    }
    
    if (!rtmp_publisher_is_connected(g_rtmp_ctx.publisher)) {
        g_rtmp_ctx.stats.dropped_frames++;
        return AICAM_ERROR;
    }
    
    // Check SPS/PPS
    if (!g_rtmp_ctx.sps_pps_sent) {
        if (frame->is_keyframe) {
            video_hub_sps_pps_t sps_pps;
            if (video_hub_get_sps_pps(&sps_pps) == AICAM_OK) {
                rtmp_on_sps_pps(&sps_pps, NULL);
            }
        }
        if (!g_rtmp_ctx.sps_pps_sent) {
            g_rtmp_ctx.stats.dropped_frames++;
            return AICAM_OK;  // Wait for I-frame with SPS/PPS
        }
    }
    
    // Send frame directly - use send time as timestamp (ensures monotonic DTS)
    uint32_t timestamp = osKernelGetTickCount() - g_rtmp_ctx.stream_start_time;
    int ret = rtmp_publisher_send_video_frame(
        g_rtmp_ctx.publisher,
        frame->data,
        frame->size,
        frame->is_keyframe,
        timestamp
    );
    
    if (ret == RTMP_PUB_OK) {
        g_rtmp_ctx.stats.frames_sent++;
        g_rtmp_ctx.stats.bytes_sent += frame->size;
    } else {
        g_rtmp_ctx.stats.errors++;
        g_rtmp_ctx.stats.dropped_frames++;
    }
    
    return (ret == RTMP_PUB_OK) ? AICAM_OK : AICAM_ERROR;
}

/**
 * @brief try to reconnect to RTMP server
 * @note This function may be called in callback context, should return quickly
 */
static aicam_result_t try_reconnect(void)
{
    uint32_t now = osKernelGetTickCount();
    
    // check reconnect interval (allow first reconnect when last_reconnect_time is 0)
    if (g_rtmp_ctx.last_reconnect_time != 0 &&
        now - g_rtmp_ctx.last_reconnect_time < g_rtmp_ctx.config.reconnect_interval_ms) {
        return AICAM_ERROR_BUSY;
    }
    
    // check reconnect attempts
    if (g_rtmp_ctx.config.max_reconnect_attempts > 0 &&
        g_rtmp_ctx.reconnect_attempts >= g_rtmp_ctx.config.max_reconnect_attempts) {
        LOG_SVC_ERROR("Max reconnect attempts reached");
        notify_event(RTMP_EVENT_RECONNECT_FAILED, 0, "Max attempts reached");
        g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_ERROR;
        return AICAM_ERROR_REACH_MAX_ATTEMPTS;
    }
    
    g_rtmp_ctx.reconnect_attempts++;
    g_rtmp_ctx.last_reconnect_time = now;
    
    notify_event(RTMP_EVENT_RECONNECTING, 0, NULL);
    LOG_SVC_INFO("Reconnect attempt %lu/%lu",
                 (unsigned long)g_rtmp_ctx.reconnect_attempts,
                 (unsigned long)g_rtmp_ctx.config.max_reconnect_attempts);
    
    int ret = rtmp_publisher_connect(g_rtmp_ctx.publisher);
    if (ret == RTMP_PUB_OK) {
        LOG_SVC_INFO("Reconnected successfully");
        rtmp_publisher_set_chunk_size(g_rtmp_ctx.publisher, 60000);
        g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_STREAMING;
        g_rtmp_ctx.reconnect_attempts = 0;
        g_rtmp_ctx.sps_pps_sent = AICAM_FALSE;  // need to resend SPS/PPS
        g_rtmp_ctx.stream_start_time = osKernelGetTickCount();  // reset timestamp baseline for new stream
        notify_event(RTMP_EVENT_CONNECTED, 0, NULL);
        g_rtmp_ctx.stats.reconnect_count++;
        return AICAM_OK;
    } else {
        LOG_SVC_WARN("Reconnect failed: %d", ret);
        return AICAM_ERROR;
    }
}

/* ==================== Service Interface Implementation ==================== */

void rtmp_service_get_default_config(rtmp_service_config_t *config)
{
    if (!config) return;

    memset(config, 0, sizeof(rtmp_service_config_t));
    config->auto_reconnect = AICAM_TRUE;
    config->reconnect_interval_ms = RTMP_DEFAULT_RECONNECT_INTERVAL;
    config->max_reconnect_attempts = RTMP_DEFAULT_MAX_RECONNECT;
    config->async_send = AICAM_TRUE; 
}

aicam_result_t rtmp_service_init(void *config)
{
    if (g_rtmp_ctx.initialized) {
        LOG_SVC_WARN("RTMP service already initialized");
        return AICAM_OK;
    }

    LOG_SVC_INFO("Initializing RTMP service v%s (Video Hub mode)", RTMP_SERVICE_VERSION);

    memset(&g_rtmp_ctx, 0, sizeof(rtmp_service_context_t));
    g_rtmp_ctx.hub_subscriber_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;

    // 1. Set default config
    rtmp_service_get_default_config(&g_rtmp_ctx.config);
    
    // 2. Load from NVS (via video_stream_mode)
    video_stream_mode_config_t vs_config;
    if (json_config_get_video_stream_mode(&vs_config) == AICAM_OK) {
        strncpy(g_rtmp_ctx.config.url, vs_config.rtmp_url, sizeof(g_rtmp_ctx.config.url) - 1);
        strncpy(g_rtmp_ctx.config.stream_key, vs_config.rtmp_stream_key, sizeof(g_rtmp_ctx.config.stream_key) - 1);
        LOG_SVC_INFO("Loaded RTMP config from NVS");
    }
    
    // 3. Override with passed config if provided
    if (config) {
        memcpy(&g_rtmp_ctx.config, config, sizeof(rtmp_service_config_t));
    }

    // Create mutex
    g_rtmp_ctx.mutex = osMutexNew(NULL);
    if (!g_rtmp_ctx.mutex) {
        LOG_SVC_ERROR("Failed to create mutex");
        return AICAM_ERROR;
    }

    // Create event flags
    g_rtmp_ctx.event_flags = osEventFlagsNew(NULL);
    if (!g_rtmp_ctx.event_flags) {
        LOG_SVC_ERROR("Failed to create event flags");
        osMutexDelete(g_rtmp_ctx.mutex);
        return AICAM_ERROR;
    }

    g_rtmp_ctx.initialized = AICAM_TRUE;
    g_rtmp_ctx.service_state = SERVICE_STATE_INITIALIZED;

    LOG_SVC_INFO("RTMP service initialized");
    return AICAM_OK;
}

/**
 * @brief Start the configured push once the network is actually up
 *
 * Own task: service_start() holds the manager mutex across every start_func
 * (service_init.c:617-641), and the wait must be on the latched
 * SERVICE_READY_STA flag because communication_service_start() returns while
 * netif init is still asynchronous (communication_service.c:1001-1017).
 */
static void rtmp_autostart_task(void *argument)
{
    (void)argument;

    // Slices rather than one shot: a slow or initially failed association
    // still gets its push when the link finally comes up.
    aicam_result_t result = AICAM_ERROR;
    uint32_t waited_ms = 0;
    while (waited_ms < RTMP_AUTOSTART_STA_BUDGET_MS) {
        if (!g_rtmp_ctx.autostart_task_running || !g_rtmp_ctx.running) {
            break;
        }
        result = service_wait_for_ready(SERVICE_READY_STA, AICAM_TRUE,
                                        RTMP_AUTOSTART_STA_SLICE_MS);
        if (result == AICAM_OK) {
            break;
        }
        waited_ms += RTMP_AUTOSTART_STA_SLICE_MS;
    }

    if (result != AICAM_OK) {
        LOG_SVC_WARN("RTMP auto-start: no STA after %lu ms (%d), leaving push idle",
                     (unsigned long)waited_ms, result);
    } else {
        for (uint32_t attempt = 1; attempt <= RTMP_AUTOSTART_MAX_ATTEMPTS; attempt++) {
            if (!g_rtmp_ctx.autostart_task_running || !g_rtmp_ctx.running) {
                break;
            }
            // Someone called the API first; leave their stream alone.
            if (g_rtmp_ctx.stream_state != RTMP_STREAM_STATE_IDLE) {
                LOG_SVC_INFO("RTMP auto-start: stream already active, standing down");
                break;
            }
            result = rtmp_service_start_stream();
            if (result == AICAM_OK) {
                LOG_SVC_WARN("RTMP auto-start: push started from rtmp_enable on attempt %lu",
                             (unsigned long)attempt);
                break;
            }
            LOG_SVC_WARN("RTMP auto-start: attempt %lu failed (%d)",
                         (unsigned long)attempt, result);
            osDelay(RTMP_AUTOSTART_RETRY_MS);
        }
    }

    g_rtmp_ctx.autostart_task_running = AICAM_FALSE;
    g_rtmp_ctx.autostart_task_handle = NULL;
    osThreadExit();
}

aicam_result_t rtmp_service_start(void)
{
    if (!g_rtmp_ctx.initialized) {
        LOG_SVC_ERROR("RTMP service not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if (g_rtmp_ctx.running) {
        LOG_SVC_WARN("RTMP service already running");
        return AICAM_OK;
    }

    LOG_SVC_INFO("Starting RTMP service");

    g_rtmp_ctx.running = AICAM_TRUE;
    g_rtmp_ctx.service_state = SERVICE_STATE_RUNNING;

    // See rtmp_autostart_task() for why this is not done inline.
    video_stream_mode_config_t vs_config;
    if (json_config_get_video_stream_mode(&vs_config) == AICAM_OK && vs_config.rtmp_enable) {
        if (g_rtmp_ctx.autostart_task_handle == NULL) {
            const osThreadAttr_t autostart_task_attr = {
                .name = "rtmp_autostart",
                .stack_size = sizeof(rtmp_autostart_task_stack),
                .stack_mem = rtmp_autostart_task_stack,
                .priority = RTMP_AUTOSTART_TASK_PRIORITY,
            };
            g_rtmp_ctx.autostart_task_running = AICAM_TRUE;
            g_rtmp_ctx.autostart_task_handle =
                osThreadNew(rtmp_autostart_task, NULL, &autostart_task_attr);
            if (!g_rtmp_ctx.autostart_task_handle) {
                g_rtmp_ctx.autostart_task_running = AICAM_FALSE;
                LOG_SVC_ERROR("Failed to create RTMP auto-start task");
            }
        }
    } else {
        LOG_SVC_INFO("RTMP: rtmp_enable not set, push left idle");
    }

    LOG_SVC_INFO("RTMP service started");
    return AICAM_OK;
}

aicam_result_t rtmp_service_stop(void)
{
    if (!g_rtmp_ctx.running) {
        return AICAM_OK;
    }

    LOG_SVC_INFO("Stopping RTMP service");

    // Retire the auto-start task first so it cannot resurrect the stream.
    if (g_rtmp_ctx.autostart_task_handle) {
        g_rtmp_ctx.autostart_task_running = AICAM_FALSE;
        osThreadTerminate(g_rtmp_ctx.autostart_task_handle);
        g_rtmp_ctx.autostart_task_handle = NULL;
    }

    // Stop streaming first
    rtmp_service_stop_stream();

    g_rtmp_ctx.running = AICAM_FALSE;
    g_rtmp_ctx.service_state = SERVICE_STATE_INITIALIZED;

    LOG_SVC_INFO("RTMP service stopped");
    return AICAM_OK;
}

aicam_result_t rtmp_service_deinit(void)
{
    if (!g_rtmp_ctx.initialized) {
        return AICAM_OK;
    }

    LOG_SVC_INFO("Deinitializing RTMP service");

    // Stop service if running
    rtmp_service_stop();

    // Delete mutex
    if (g_rtmp_ctx.mutex) {
        osMutexDelete(g_rtmp_ctx.mutex);
        g_rtmp_ctx.mutex = NULL;
    }

    // Delete event flags
    if (g_rtmp_ctx.event_flags) {
        osEventFlagsDelete(g_rtmp_ctx.event_flags);
        g_rtmp_ctx.event_flags = NULL;
    }

    g_rtmp_ctx.initialized = AICAM_FALSE;
    g_rtmp_ctx.service_state = SERVICE_STATE_UNINITIALIZED;

    LOG_SVC_INFO("RTMP service deinitialized");
    return AICAM_OK;
}

service_state_t rtmp_service_get_state(void)
{
    return g_rtmp_ctx.service_state;
}

/* ==================== Stream Control Functions ==================== */

aicam_result_t rtmp_service_start_stream(void)
{
    if (!g_rtmp_ctx.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if (!g_rtmp_ctx.running) {
        return AICAM_ERROR_UNAVAILABLE;
    }

    // Allow start from IDLE or ERROR state
    if (g_rtmp_ctx.stream_state != RTMP_STREAM_STATE_IDLE &&
        g_rtmp_ctx.stream_state != RTMP_STREAM_STATE_ERROR) {
        LOG_SVC_WARN("Stream already active, state: %d", g_rtmp_ctx.stream_state);
        return AICAM_ERROR_BUSY;
    }

    if (g_rtmp_ctx.config.url[0] == '\0') {
        LOG_SVC_ERROR("RTMP URL not configured");
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Check Video Hub
    if (!video_hub_is_initialized()) {
        LOG_SVC_ERROR("Video Hub not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    osMutexAcquire(g_rtmp_ctx.mutex, osWaitForever);

    LOG_SVC_INFO("Starting RTMP stream (%s mode)", 
                 g_rtmp_ctx.config.async_send ? "async" : "sync");
    g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_CONNECTING;

    // Initialize send queue (only for async mode)
    if (g_rtmp_ctx.config.async_send) {
        if (send_queue_init(&g_rtmp_ctx.send_queue) != AICAM_OK) {
            LOG_SVC_ERROR("Failed to init send queue");
            g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_ERROR;
            osMutexRelease(g_rtmp_ctx.mutex);
            return AICAM_ERROR;
        }
    }

    // Build full URL
    char full_url[RTMP_MAX_URL_LENGTH + RTMP_MAX_STREAM_KEY_LENGTH + 2];
    build_full_url(full_url, sizeof(full_url));

    // Create publisher
    rtmp_pub_config_t pub_config = {0};
    rtmp_publisher_get_default_config(&pub_config);
    strncpy(pub_config.url, full_url, sizeof(pub_config.url) - 1);
    
    // Get actual camera configuration from pipe1 param
    pipe_params_t pipe_param = {0};
    device_t *camera_device = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
    if (camera_device) {
        aicam_result_t pipe_result = device_ioctl(camera_device, CAM_CMD_GET_PIPE1_PARAM, 
                                                  (uint8_t *)&pipe_param, sizeof(pipe_params_t));
        if (pipe_result == AICAM_OK) {
            pub_config.width = pipe_param.width;
            pub_config.height = pipe_param.height;
            pub_config.fps = pipe_param.fps;
        }
    }
    LOG_SVC_INFO("Creating RTMP publisher with config: %dx%d@%dfps", 
                 pub_config.width, pub_config.height, pub_config.fps);


    g_rtmp_ctx.publisher = rtmp_publisher_create(&pub_config);
    if (!g_rtmp_ctx.publisher) {
        LOG_SVC_ERROR("Failed to create RTMP publisher");
        if (g_rtmp_ctx.config.async_send) {
            send_queue_deinit(&g_rtmp_ctx.send_queue);
        }
        g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_ERROR;
        osMutexRelease(g_rtmp_ctx.mutex);
        return AICAM_ERROR;
    }

    // Connect to RTMP server
    LOG_SVC_INFO("Connecting to %s", full_url);
    int ret = rtmp_publisher_connect(g_rtmp_ctx.publisher);
    if (ret != RTMP_PUB_OK) {
        LOG_SVC_ERROR("Failed to connect: %d", ret);
        rtmp_publisher_destroy(g_rtmp_ctx.publisher);
        g_rtmp_ctx.publisher = NULL;
        if (g_rtmp_ctx.config.async_send) {
            send_queue_deinit(&g_rtmp_ctx.send_queue);
        }
        g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_ERROR;
        osMutexRelease(g_rtmp_ctx.mutex);
        return AICAM_ERROR;
    }

    LOG_SVC_INFO("Connected to RTMP server");
    
    // Increase chunk size to reduce network round-trips (default 128 is too small)
    rtmp_publisher_set_chunk_size(g_rtmp_ctx.publisher, 60000);
    
    notify_event(RTMP_EVENT_CONNECTED, 0, NULL);

    // Start send task (only for async mode)
    if (g_rtmp_ctx.config.async_send) {
        if (g_rtmp_ctx.task_need_cleanup && g_rtmp_ctx.send_task_handle) {
            osThreadTerminate(g_rtmp_ctx.send_task_handle);
            g_rtmp_ctx.send_task_handle = NULL;
        }
        // cleanup task need cleanup flag
        g_rtmp_ctx.task_need_cleanup = AICAM_FALSE;


        g_rtmp_ctx.send_task_running = AICAM_TRUE;
        const osThreadAttr_t send_task_attr = {
            .name = "rtmp_send",
            .stack_size = sizeof(rtmp_send_task_stack),
            .stack_mem = rtmp_send_task_stack,
            .priority = RTMP_SEND_TASK_PRIORITY,
        };
        g_rtmp_ctx.send_task_handle = osThreadNew(rtmp_send_task, NULL, &send_task_attr);
        if (!g_rtmp_ctx.send_task_handle) {
            LOG_SVC_ERROR("Failed to create send task");
            rtmp_publisher_disconnect(g_rtmp_ctx.publisher);
            rtmp_publisher_destroy(g_rtmp_ctx.publisher);
            g_rtmp_ctx.publisher = NULL;
            send_queue_deinit(&g_rtmp_ctx.send_queue);
            g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_ERROR;
            osMutexRelease(g_rtmp_ctx.mutex);
            return AICAM_ERROR;
        }
    }

    // Subscribe to Video Hub
    g_rtmp_ctx.hub_subscriber_id = video_hub_subscribe(
        VIDEO_HUB_SUBSCRIBER_RTMP,
        rtmp_on_frame,
        rtmp_on_sps_pps,
        &g_rtmp_ctx
    );
    
    if (g_rtmp_ctx.hub_subscriber_id == VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        LOG_SVC_ERROR("Failed to subscribe to Video Hub");
        if (g_rtmp_ctx.config.async_send) {
            g_rtmp_ctx.send_task_running = AICAM_FALSE;
            osThreadTerminate(g_rtmp_ctx.send_task_handle);
            g_rtmp_ctx.send_task_handle = NULL;
            send_queue_deinit(&g_rtmp_ctx.send_queue);
        }
        rtmp_publisher_disconnect(g_rtmp_ctx.publisher);
        rtmp_publisher_destroy(g_rtmp_ctx.publisher);
        g_rtmp_ctx.publisher = NULL;
        g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_ERROR;
        osMutexRelease(g_rtmp_ctx.mutex);
        return AICAM_ERROR;
    }
    
    LOG_SVC_INFO("Subscribed to Video Hub, subscriber_id=%ld", 
                 (long)g_rtmp_ctx.hub_subscriber_id);

    // Reset state
    g_rtmp_ctx.sps_pps_sent = AICAM_FALSE;
    g_rtmp_ctx.reconnect_attempts = 0;
    g_rtmp_ctx.last_reconnect_time = 0;
    g_rtmp_ctx.pending_reconnect = AICAM_FALSE;
    g_rtmp_ctx.stream_start_time = osKernelGetTickCount();
    g_rtmp_ctx.first_frame_timestamp = 0;
    g_rtmp_ctx.first_frame_received = AICAM_FALSE;
    memset(&g_rtmp_ctx.stats, 0, sizeof(rtmp_service_stats_t));
    g_rtmp_ctx.stats.stream_start_time = g_rtmp_ctx.stream_start_time;

    g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_STREAMING;
    notify_event(RTMP_EVENT_STREAM_STARTED, 0, NULL);

    osMutexRelease(g_rtmp_ctx.mutex);

    LOG_SVC_INFO("RTMP stream started (async mode)");
    return AICAM_OK;
}

aicam_result_t rtmp_service_stop_stream(void)
{
    if (g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_IDLE) {
        return AICAM_OK;
    }

    LOG_SVC_INFO("Stopping RTMP stream");

    // Mark stopping state and get handles
    osMutexAcquire(g_rtmp_ctx.mutex, osWaitForever);
    g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_STOPPING;
    video_hub_subscriber_id_t sub_id = g_rtmp_ctx.hub_subscriber_id;
    g_rtmp_ctx.hub_subscriber_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    rtmp_publisher_t *pub = g_rtmp_ctx.publisher;
    g_rtmp_ctx.publisher = NULL;
    osThreadId_t task = g_rtmp_ctx.send_task_handle;
    g_rtmp_ctx.send_task_running = AICAM_FALSE;
    osMutexRelease(g_rtmp_ctx.mutex);

    // Unsubscribe from Hub first (stops new frames from arriving)
    if (sub_id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        video_hub_unsubscribe(sub_id);
        LOG_SVC_INFO("Unsubscribed from Video Hub");
    }

    // Stop send task (only for async mode)
    if (g_rtmp_ctx.config.async_send && task) {
        // Signal semaphore to wake up task if waiting
        if (g_rtmp_ctx.send_queue.sem_data) {
            osSemaphoreRelease(g_rtmp_ctx.send_queue.sem_data);
        }
        
        // Wait for task to exit (max 500ms)
        for (int i = 0; i < 50; i++) {
            osThreadState_t state = osThreadGetState(task);
            if (state == osThreadTerminated || state == osThreadError) {
                break;
            }
            osDelay(10);
        }
        osThreadTerminate(task);
        g_rtmp_ctx.send_task_handle = NULL;
        LOG_SVC_INFO("Send task stopped");
    }

    // Disconnect and destroy publisher
    if (pub) {
        rtmp_publisher_disconnect(pub);
        rtmp_publisher_destroy(pub);
    }

    // Deinit send queue (only for async mode)
    if (g_rtmp_ctx.config.async_send) {
        send_queue_deinit(&g_rtmp_ctx.send_queue);
    }

    osMutexAcquire(g_rtmp_ctx.mutex, osWaitForever);
    g_rtmp_ctx.stream_state = RTMP_STREAM_STATE_IDLE;
    osMutexRelease(g_rtmp_ctx.mutex);
    
    notify_event(RTMP_EVENT_STREAM_STOPPED, 0, NULL);

    LOG_SVC_INFO("RTMP stream stopped, frames: %lu, bytes: %lu, dropped: %lu, bitrate: %lu kbps",
                 (unsigned long)g_rtmp_ctx.stats.frames_sent,
                 (unsigned long)g_rtmp_ctx.stats.bytes_sent,
                 (unsigned long)g_rtmp_ctx.stats.dropped_frames,
                 (unsigned long)g_rtmp_ctx.stats.current_bitrate_kbps);
    return AICAM_OK;
}

aicam_result_t rtmp_service_restart_stream(void)
{
    aicam_result_t ret = rtmp_service_stop_stream();
    if (ret != AICAM_OK) {
        return ret;
    }
    osDelay(500);
    return rtmp_service_start_stream();
}

aicam_bool_t rtmp_service_is_streaming(void)
{
    return (g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_STREAMING ||
            g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_RECONNECTING);
}

rtmp_stream_state_t rtmp_service_get_stream_state(void)
{
    return g_rtmp_ctx.stream_state;
}

const char* rtmp_stream_state_to_string(rtmp_stream_state_t state)
{
    switch (state) {
        case RTMP_STREAM_STATE_IDLE:         return "idle";
        case RTMP_STREAM_STATE_CONNECTING:   return "connecting";
        case RTMP_STREAM_STATE_STREAMING:    return "streaming";
        case RTMP_STREAM_STATE_RECONNECTING: return "reconnecting";
        case RTMP_STREAM_STATE_STOPPING:     return "stopping";
        case RTMP_STREAM_STATE_ERROR:        return "error";
        default:                             return "unknown";
    }
}

/* ==================== Configuration Implementation ==================== */

aicam_result_t rtmp_service_get_config(rtmp_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    memcpy(config, &g_rtmp_ctx.config, sizeof(rtmp_service_config_t));
    return AICAM_OK;
}

aicam_result_t rtmp_service_set_config(const rtmp_service_config_t *config)
{
    if (!config) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (g_rtmp_ctx.stream_state != RTMP_STREAM_STATE_IDLE) {
        LOG_SVC_WARN("Cannot change config while streaming");
        return AICAM_ERROR_BUSY;
    }

    osMutexAcquire(g_rtmp_ctx.mutex, osWaitForever);
    memcpy(&g_rtmp_ctx.config, config, sizeof(rtmp_service_config_t));
    osMutexRelease(g_rtmp_ctx.mutex);

    return AICAM_OK;
}

aicam_result_t rtmp_service_set_url(const char *url)
{
    if (!url) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    osMutexAcquire(g_rtmp_ctx.mutex, osWaitForever);
    strncpy(g_rtmp_ctx.config.url, url, RTMP_MAX_URL_LENGTH - 1);
    g_rtmp_ctx.config.url[RTMP_MAX_URL_LENGTH - 1] = '\0';
    osMutexRelease(g_rtmp_ctx.mutex);

    return AICAM_OK;
}

aicam_result_t rtmp_service_set_stream_key(const char *stream_key)
{
    if (!stream_key) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    osMutexAcquire(g_rtmp_ctx.mutex, osWaitForever);
    strncpy(g_rtmp_ctx.config.stream_key, stream_key, RTMP_MAX_STREAM_KEY_LENGTH - 1);
    g_rtmp_ctx.config.stream_key[RTMP_MAX_STREAM_KEY_LENGTH - 1] = '\0';
    osMutexRelease(g_rtmp_ctx.mutex);

    return AICAM_OK;
}

aicam_result_t rtmp_service_save_config(void)
{
    // Save via video_stream_mode API
    video_stream_mode_config_t vs_config;
    json_config_get_video_stream_mode(&vs_config);
    
    strncpy(vs_config.rtmp_url, g_rtmp_ctx.config.url, sizeof(vs_config.rtmp_url) - 1);
    strncpy(vs_config.rtmp_stream_key, g_rtmp_ctx.config.stream_key, sizeof(vs_config.rtmp_stream_key) - 1);
    
    aicam_result_t ret = json_config_set_video_stream_mode(&vs_config);
    if (ret == AICAM_OK) {
        LOG_SVC_INFO("RTMP config saved to NVS");
    } else {
        LOG_SVC_ERROR("Failed to save RTMP config: %d", ret);
    }
    return ret;
}

aicam_result_t rtmp_service_load_config(void)
{
    // Load via video_stream_mode API
    video_stream_mode_config_t vs_config;
    aicam_result_t ret = json_config_get_video_stream_mode(&vs_config);
    if (ret == AICAM_OK) {
        strncpy(g_rtmp_ctx.config.url, vs_config.rtmp_url, sizeof(g_rtmp_ctx.config.url) - 1);
        strncpy(g_rtmp_ctx.config.stream_key, vs_config.rtmp_stream_key, sizeof(g_rtmp_ctx.config.stream_key) - 1);
        LOG_SVC_INFO("RTMP config loaded from NVS");
    } else {
        LOG_SVC_WARN("Failed to load RTMP config, using defaults");
    }
    return ret;
}

/* ==================== Statistics Implementation ==================== */

aicam_result_t rtmp_service_get_stats(rtmp_service_stats_t *stats)
{
    if (!stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    memcpy(stats, &g_rtmp_ctx.stats, sizeof(rtmp_service_stats_t));

    // Update duration if streaming
    if (g_rtmp_ctx.stream_state == RTMP_STREAM_STATE_STREAMING) {
        stats->stream_duration_sec =
            (osKernelGetTickCount() - g_rtmp_ctx.stats.stream_start_time) / 1000;
    }

    return AICAM_OK;
}

aicam_result_t rtmp_service_reset_stats(void)
{
    memset(&g_rtmp_ctx.stats, 0, sizeof(rtmp_service_stats_t));
    return AICAM_OK;
}

/* ==================== Event Management Implementation ==================== */

aicam_result_t rtmp_service_register_callback(rtmp_event_callback_t callback, void *user_data)
{
    if (!callback) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (g_rtmp_ctx.callback_count >= RTMP_MAX_CALLBACKS) {
        return AICAM_ERROR_NO_MEMORY;
    }

    g_rtmp_ctx.callbacks[g_rtmp_ctx.callback_count] = callback;
    g_rtmp_ctx.callback_user_data[g_rtmp_ctx.callback_count] = user_data;
    g_rtmp_ctx.callback_count++;

    return AICAM_OK;
}

aicam_result_t rtmp_service_unregister_callback(rtmp_event_callback_t callback)
{
    for (uint32_t i = 0; i < g_rtmp_ctx.callback_count; i++) {
        if (g_rtmp_ctx.callbacks[i] == callback) {
            // Shift remaining callbacks
            for (uint32_t j = i; j < g_rtmp_ctx.callback_count - 1; j++) {
                g_rtmp_ctx.callbacks[j] = g_rtmp_ctx.callbacks[j + 1];
                g_rtmp_ctx.callback_user_data[j] = g_rtmp_ctx.callback_user_data[j + 1];
            }
            g_rtmp_ctx.callback_count--;
            return AICAM_OK;
        }
    }
    return AICAM_ERROR_NOT_FOUND;
}

/* ==================== Utility Functions ==================== */

const char* rtmp_service_get_version(void)
{
    return RTMP_SERVICE_VERSION;
}

aicam_bool_t rtmp_service_is_initialized(void)
{
    return g_rtmp_ctx.initialized;
}

aicam_bool_t rtmp_service_is_running(void)
{
    return g_rtmp_ctx.running;
}

/* ==================== CLI Commands ==================== */

static int rtmp_status_cmd(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    LOG_SIMPLE("=== RTMP Service Status (Video Hub) ===\r\n");
    LOG_SIMPLE("Version: %s\r\n", RTMP_SERVICE_VERSION);
    LOG_SIMPLE("Initialized: %s\r\n", g_rtmp_ctx.initialized ? "Yes" : "No");
    LOG_SIMPLE("Running: %s\r\n", g_rtmp_ctx.running ? "Yes" : "No");
    LOG_SIMPLE("Stream State: %s\r\n", rtmp_stream_state_to_string(g_rtmp_ctx.stream_state));
    LOG_SIMPLE("Hub Subscriber: %ld\r\n", (long)g_rtmp_ctx.hub_subscriber_id);
    LOG_SIMPLE("URL: %s\r\n", g_rtmp_ctx.config.url[0] ? g_rtmp_ctx.config.url : "(not set)");

    if (g_rtmp_ctx.stats.frames_sent > 0) {
        LOG_SIMPLE("--- Statistics ---\r\n");
        LOG_SIMPLE("Frames sent: %lu\r\n", (unsigned long)g_rtmp_ctx.stats.frames_sent);
        LOG_SIMPLE("Bytes sent: %lu\r\n", (unsigned long)g_rtmp_ctx.stats.bytes_sent);
        LOG_SIMPLE("Keyframes: %lu\r\n", (unsigned long)g_rtmp_ctx.stats.keyframes_sent);
        LOG_SIMPLE("Dropped: %lu\r\n", (unsigned long)g_rtmp_ctx.stats.dropped_frames);
        LOG_SIMPLE("Errors: %lu\r\n", (unsigned long)g_rtmp_ctx.stats.errors);
        LOG_SIMPLE("Bitrate: %lu kbps\r\n", (unsigned long)g_rtmp_ctx.stats.current_bitrate_kbps);
        LOG_SIMPLE("Duration: %lu sec\r\n", (unsigned long)g_rtmp_ctx.stats.stream_duration_sec);
        LOG_SIMPLE("Reconnects: %lu\r\n", (unsigned long)g_rtmp_ctx.stats.reconnect_count);
    }

    return 0;
}

static int rtmp_start_cmd(int argc, char *argv[])
{
    if (argc >= 2) {
        rtmp_service_set_url(argv[1]);
    }
    if (argc >= 3) {
        rtmp_service_set_stream_key(argv[2]);
    }

    aicam_result_t ret = rtmp_service_start_stream();
    if (ret != AICAM_OK) {
        LOG_SIMPLE("Failed to start stream: %d\r\n", ret);
        return -1;
    }
    LOG_SIMPLE("Stream started\r\n");
    return 0;
}

static int rtmp_stop_cmd(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    aicam_result_t ret = rtmp_service_stop_stream();
    if (ret != AICAM_OK) {
        LOG_SIMPLE("Failed to stop stream: %d\r\n", ret);
        return -1;
    }
    LOG_SIMPLE("Stream stopped\r\n");
    return 0;
}

static int rtmp_set_url_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        LOG_SIMPLE("Usage: rtmp_url <url> [stream_key]\r\n");
        return -1;
    }

    rtmp_service_set_url(argv[1]);
    if (argc >= 3) {
        rtmp_service_set_stream_key(argv[2]);
    }

    LOG_SIMPLE("URL set: %s\r\n", argv[1]);
    return 0;
}

#include "debug.h"

static debug_cmd_reg_t rtmp_cmd_table[] = {
    {"rtmp_status", "Show RTMP service status", rtmp_status_cmd},
    {"rtmp_go", "Start RTMP stream: rtmp_go [url] [key]", rtmp_start_cmd},
    {"rtmp_end", "Stop RTMP stream", rtmp_stop_cmd},
    {"rtmp_url", "Set RTMP URL: rtmp_url <url> [key]", rtmp_set_url_cmd},
};

void rtmp_cmd_register(void)
{
    debug_cmdline_register(rtmp_cmd_table,
                           sizeof(rtmp_cmd_table) / sizeof(rtmp_cmd_table[0]));
    LOG_SVC_INFO("RTMP CLI commands registered (Video Hub mode)");
}

