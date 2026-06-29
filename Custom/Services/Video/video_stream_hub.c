/**
 * @file video_stream_hub.c
 * @brief Video Stream Hub - Central video frame distribution center
 */

#include "video_stream_hub.h"
#include "aicam_error.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "Hal/mem.h"
#include "Services/AI/ai_service.h"
#include <string.h>

/* ==================== Subscriber Structure ==================== */

typedef struct {
    video_hub_subscriber_id_t id;
    video_hub_subscriber_type_t type;
    video_hub_frame_callback_t frame_callback;
    video_hub_sps_pps_callback_t sps_pps_callback;
    void *user_data;
    aicam_bool_t active;
    aicam_bool_t sps_pps_sent;
} video_hub_subscriber_t;

/* ==================== Hub Context ==================== */

typedef struct {
    aicam_bool_t initialized;
    
    // SPS/PPS cache
    uint8_t *sps_data;
    uint32_t sps_size;
    uint8_t *pps_data;
    uint32_t pps_size;
    aicam_bool_t sps_pps_valid;
    
    // Subscribers
    video_hub_subscriber_t subscribers[VIDEO_HUB_MAX_SUBSCRIBERS];
    uint32_t subscriber_count;
    video_hub_subscriber_id_t next_id;
    
    osMutexId_t mutex;
} video_hub_ctx_t;

static video_hub_ctx_t g_hub = {0};

/* ==================== Internal Functions ==================== */

static int extract_sps_pps(const uint8_t *data, uint32_t size,
                           uint8_t **sps, uint32_t *sps_size,
                           uint8_t **pps, uint32_t *pps_size)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    
    *sps = NULL; *sps_size = 0;
    *pps = NULL; *pps_size = 0;
    
    while (p < end - 4) {
        // Find start code (0x00 0x00 0x00 0x01)
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            uint8_t nal_type = (p[4] & 0x1F);
            const uint8_t *nal_start = p + 4;
            const uint8_t *nal_end = nal_start + 1;
            
            // Find next start code
            while (nal_end < end - 3) {
                if (nal_end[0] == 0 && nal_end[1] == 0 &&
                    (nal_end[2] == 1 || (nal_end[2] == 0 && nal_end[3] == 1))) {
                    break;
                }
                nal_end++;
            }
            if (nal_end >= end - 3) nal_end = end;
            
            // SPS (nal_type = 7)
            if (nal_type == 7 && *sps == NULL) {
                *sps_size = (uint32_t)(nal_end - nal_start);
                *sps = (uint8_t*)hal_mem_alloc_large(*sps_size);
                if (!*sps) return -1;
                memcpy(*sps, nal_start, *sps_size);
            }
            // PPS (nal_type = 8)
            else if (nal_type == 8 && *pps == NULL) {
                *pps_size = (uint32_t)(nal_end - nal_start);
                *pps = (uint8_t*)hal_mem_alloc_large(*pps_size);
                if (!*pps) {
                    if (*sps) { hal_mem_free(*sps); *sps = NULL; }
                    return -1;
                }
                memcpy(*pps, nal_start, *pps_size);
            }
            p += 4;
        } else if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            p += 3;
        } else {
            p++;
        }
        
        if (*sps && *pps) return 0;
    }
    
    return (*sps && *pps) ? 0 : -1;
}

static void notify_sps_pps(void)
{
    video_hub_sps_pps_t sps_pps = {
        .sps_data = g_hub.sps_data,
        .sps_size = g_hub.sps_size,
        .pps_data = g_hub.pps_data,
        .pps_size = g_hub.pps_size
    };
    
    for (uint32_t i = 0; i < VIDEO_HUB_MAX_SUBSCRIBERS; i++) {
        video_hub_subscriber_t *sub = &g_hub.subscribers[i];
        if (sub->active && sub->sps_pps_callback && !sub->sps_pps_sent) {
            sub->sps_pps_callback(&sps_pps, sub->user_data);
            sub->sps_pps_sent = AICAM_TRUE;
        }
    }
}

static void notify_frame(const video_hub_frame_t *frame)
{
    // Snapshot subscriber list for thread safety
    video_hub_subscriber_t snapshot[VIDEO_HUB_MAX_SUBSCRIBERS];
    
    osMutexAcquire(g_hub.mutex, osWaitForever);
    memcpy(snapshot, g_hub.subscribers, sizeof(snapshot));
    osMutexRelease(g_hub.mutex);
    
    for (uint32_t i = 0; i < VIDEO_HUB_MAX_SUBSCRIBERS; i++) {
        if (snapshot[i].active && snapshot[i].frame_callback) {
            snapshot[i].frame_callback(frame, snapshot[i].user_data);
        }
    }
}

/* ==================== API Implementation ==================== */

aicam_result_t video_hub_init(void)
{
    if (g_hub.initialized) return AICAM_OK;
    
    memset(&g_hub, 0, sizeof(video_hub_ctx_t));
    
    g_hub.mutex = osMutexNew(NULL);
    if (!g_hub.mutex) {
        LOG_SVC_ERROR("Hub: failed to create mutex");
        return AICAM_ERROR;
    }
    
    g_hub.next_id = 1;
    g_hub.initialized = AICAM_TRUE;
    
    LOG_SVC_INFO("Video Hub v%s initialized", VIDEO_HUB_VERSION);
    return AICAM_OK;
}

aicam_result_t video_hub_deinit(void)
{
    if (!g_hub.initialized) return AICAM_OK;
    
    osMutexAcquire(g_hub.mutex, osWaitForever);
    
    if (g_hub.sps_data) { hal_mem_free(g_hub.sps_data); g_hub.sps_data = NULL; }
    if (g_hub.pps_data) { hal_mem_free(g_hub.pps_data); g_hub.pps_data = NULL; }
    
    osMutexRelease(g_hub.mutex);
    osMutexDelete(g_hub.mutex);
    
    g_hub.initialized = AICAM_FALSE;
    LOG_SVC_INFO("Video Hub deinitialized");
    return AICAM_OK;
}

video_hub_subscriber_id_t video_hub_subscribe(
    video_hub_subscriber_type_t type,
    video_hub_frame_callback_t frame_callback,
    video_hub_sps_pps_callback_t sps_pps_callback,
    void *user_data)
{
    if (!g_hub.initialized || !frame_callback) {
        return VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    }
    
    osMutexAcquire(g_hub.mutex, osWaitForever);
    
    // Find empty slot
    video_hub_subscriber_t *slot = NULL;
    for (uint32_t i = 0; i < VIDEO_HUB_MAX_SUBSCRIBERS; i++) {
        if (!g_hub.subscribers[i].active) {
            slot = &g_hub.subscribers[i];
            break;
        }
    }
    
    if (!slot) {
        osMutexRelease(g_hub.mutex);
        LOG_SVC_ERROR("Hub: max subscribers reached");
        return VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    }
    
    // Check if need to start pipeline (first subscriber)
    aicam_bool_t need_start_pipeline = (g_hub.subscriber_count == 0);
    
    slot->id = g_hub.next_id++;
    slot->type = type;
    slot->frame_callback = frame_callback;
    slot->sps_pps_callback = sps_pps_callback;
    slot->user_data = user_data;
    slot->active = AICAM_TRUE;
    slot->sps_pps_sent = AICAM_FALSE;
    
    g_hub.subscriber_count++;
    video_hub_subscriber_id_t id = slot->id;
    
    // Send cached SPS/PPS to new subscriber
    if (g_hub.sps_pps_valid && sps_pps_callback) {
        video_hub_sps_pps_t sps_pps = {
            .sps_data = g_hub.sps_data,
            .sps_size = g_hub.sps_size,
            .pps_data = g_hub.pps_data,
            .pps_size = g_hub.pps_size
        };
        sps_pps_callback(&sps_pps, user_data);
        slot->sps_pps_sent = AICAM_TRUE;
    }
    
    osMutexRelease(g_hub.mutex);
    
    // First subscriber: auto-start AI pipeline
    if (need_start_pipeline && !ai_pipeline_is_running()) {
        LOG_SVC_INFO("Hub: first subscriber, starting AI pipeline");
        ai_pipeline_start();
    }
    
    LOG_SVC_INFO("Hub: subscriber %ld added (type=%d, total=%lu)",
                 (long)id, type, (unsigned long)g_hub.subscriber_count);
    return id;
}

aicam_result_t video_hub_unsubscribe(video_hub_subscriber_id_t subscriber_id)
{
    if (!g_hub.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    
    osMutexAcquire(g_hub.mutex, osWaitForever);
    
    aicam_bool_t found = AICAM_FALSE;
    aicam_bool_t need_stop_pipeline = AICAM_FALSE;
    
    for (uint32_t i = 0; i < VIDEO_HUB_MAX_SUBSCRIBERS; i++) {
        if (g_hub.subscribers[i].active && g_hub.subscribers[i].id == subscriber_id) {
            g_hub.subscribers[i].active = AICAM_FALSE;
            g_hub.subscriber_count--;
            found = AICAM_TRUE;
            
            // Last subscriber removed: mark for pipeline stop
            if (g_hub.subscriber_count == 0) {
                need_stop_pipeline = AICAM_TRUE;
            }
            break;
        }
    }
    
    osMutexRelease(g_hub.mutex);
    
    if (found) {
        LOG_SVC_INFO("Hub: subscriber %ld removed (remaining=%lu)",
                     (long)subscriber_id, (unsigned long)g_hub.subscriber_count);
        
        // No subscribers: auto-stop AI pipeline
        if (need_stop_pipeline && ai_pipeline_is_running()) {
            LOG_SVC_INFO("Hub: no subscribers, stopping AI pipeline");
            ai_pipeline_stop();
        }
    }
    return found ? AICAM_OK : AICAM_ERROR_NOT_FOUND;
}

aicam_result_t video_hub_inject_frame(const uint8_t *data, uint32_t size, 
                                       uint64_t timestamp, aicam_bool_t is_keyframe,
                                       uint32_t header_offset,
                                       uint32_t width, uint32_t height)
{
    if (!g_hub.initialized || !data || size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (g_hub.subscriber_count == 0) return AICAM_OK;
    
    // Extract SPS/PPS from first I-frame
    if (!g_hub.sps_pps_valid && is_keyframe) {
        uint8_t *sps = NULL, *pps = NULL;
        uint32_t sps_len = 0, pps_len = 0;
        
        if (extract_sps_pps(data, size, &sps, &sps_len, &pps, &pps_len) == 0) {
            osMutexAcquire(g_hub.mutex, osWaitForever);
            if (g_hub.sps_data) hal_mem_free(g_hub.sps_data);
            if (g_hub.pps_data) hal_mem_free(g_hub.pps_data);
            g_hub.sps_data = sps;
            g_hub.sps_size = sps_len;
            g_hub.pps_data = pps;
            g_hub.pps_size = pps_len;
            g_hub.sps_pps_valid = AICAM_TRUE;
            osMutexRelease(g_hub.mutex);
            
            notify_sps_pps();
        }
    }
    
    // Distribute frame to subscribers
    static uint32_t frame_id = 0;
    video_hub_frame_t frame = {
        .data = (uint8_t*)data,
        .size = size,
        .timestamp = timestamp,
        .is_keyframe = is_keyframe,
        .frame_id = frame_id++,
        .width = width,
        .height = height,
        .header_offset = header_offset
    };
    
    notify_frame(&frame);
    return AICAM_OK;
}

aicam_result_t video_hub_inject_sps_pps(const uint8_t *sps, uint32_t sps_size,
                                         const uint8_t *pps, uint32_t pps_size)
{
    if (!g_hub.initialized || !sps || !pps || sps_size == 0 || pps_size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_hub.mutex, osWaitForever);
    
    if (g_hub.sps_data) hal_mem_free(g_hub.sps_data);
    if (g_hub.pps_data) hal_mem_free(g_hub.pps_data);
    
    g_hub.sps_data = (uint8_t*)hal_mem_alloc_large(sps_size);
    g_hub.pps_data = (uint8_t*)hal_mem_alloc_large(pps_size);
    
    if (!g_hub.sps_data || !g_hub.pps_data) {
        if (g_hub.sps_data) { hal_mem_free(g_hub.sps_data); g_hub.sps_data = NULL; }
        if (g_hub.pps_data) { hal_mem_free(g_hub.pps_data); g_hub.pps_data = NULL; }
        osMutexRelease(g_hub.mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    memcpy(g_hub.sps_data, sps, sps_size);
    memcpy(g_hub.pps_data, pps, pps_size);
    g_hub.sps_size = sps_size;
    g_hub.pps_size = pps_size;
    g_hub.sps_pps_valid = AICAM_TRUE;
    
    osMutexRelease(g_hub.mutex);
    
    notify_sps_pps();
    return AICAM_OK;
}

aicam_result_t video_hub_get_sps_pps(video_hub_sps_pps_t *sps_pps)
{
    if (!sps_pps) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_hub.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    /* The cache can be freed at runtime by video_hub_invalidate_sps_pps()
     * (called from ai_pipeline_stop on model reload). Take the mutex so the
     * valid-flag check and the pointer/size reads happen atomically against
     * a concurrent invalidation, avoiding a use-after-free of sps_data/pps_data. */
    osMutexAcquire(g_hub.mutex, osWaitForever);

    if (!g_hub.sps_pps_valid) {
        osMutexRelease(g_hub.mutex);
        return AICAM_ERROR_UNAVAILABLE;
    }

    sps_pps->sps_data = g_hub.sps_data;
    sps_pps->sps_size = g_hub.sps_size;
    sps_pps->pps_data = g_hub.pps_data;
    sps_pps->pps_size = g_hub.pps_size;

    osMutexRelease(g_hub.mutex);
    return AICAM_OK;
}

aicam_result_t video_hub_invalidate_sps_pps(void)
{
    if (!g_hub.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    osMutexAcquire(g_hub.mutex, osWaitForever);

    /* Drop the cached SPS/PPS so the next injected keyframe re-extracts the
     * encoder's current SPS/PPS (the encoder session may have been rebuilt
     * after a model reload / pipeline restart). */
    if (g_hub.sps_data) { hal_mem_free(g_hub.sps_data); g_hub.sps_data = NULL; }
    if (g_hub.pps_data) { hal_mem_free(g_hub.pps_data); g_hub.pps_data = NULL; }
    g_hub.sps_size = 0;
    g_hub.pps_size = 0;
    g_hub.sps_pps_valid = AICAM_FALSE;

    /* Allow the freshly extracted SPS/PPS to be re-delivered to subscribers
     * that already received the now-stale copy. */
    for (uint32_t i = 0; i < VIDEO_HUB_MAX_SUBSCRIBERS; i++) {
        g_hub.subscribers[i].sps_pps_sent = AICAM_FALSE;
    }

    osMutexRelease(g_hub.mutex);

    LOG_SVC_INFO("Hub: SPS/PPS cache invalidated (encoder session reset)");
    return AICAM_OK;
}

aicam_bool_t video_hub_is_initialized(void) { return g_hub.initialized; }
aicam_bool_t video_hub_has_subscribers(void) { return g_hub.subscriber_count > 0; }
uint32_t video_hub_get_subscriber_count(void) { return g_hub.subscriber_count; }
