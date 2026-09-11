/**
 * @file websocket_stream_server.c
 * @brief  WebSocket Stream Server Implementation 
 * @details WebSocket server implementation based on mongoose library.
 */

#include "websocket_stream_server.h"
#include "aicam_error.h"
#include "buffer_mgr.h"
#include "mongoose.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "common_utils.h"
#include "web_config.h"
#include "h264encapi.h"  // For H264EncOut structure
// Include FreeRTOS headers
#include "cmsis_os2.h"
#include "drtc.h"
#include "Services/Video/video_stream_hub.h"

#define MAX_CLIENTS 2
#define MAX_FRAME_SIZE (1024 * 512)

 /* ==================== Web Debug Logging (see WEB_DEBUG in web_config.h) ==================== */
 /* Per-video-frame (MG_EV_WAKEUP) logs are deliberately NOT emitted: 30 fps flood. */
 #if WEB_DEBUG
 #define WS_LOG(fmt, ...)  printf("[WSDBG][tick %lums] " fmt "\r\n", (unsigned long) osKernelGetTickCount(), ##__VA_ARGS__)
 #else
 #define WS_LOG(fmt, ...)  do { } while (0)
 #endif

/* ==================== Global Variables ==================== */

/**
 * @brief WebSocket client connection information
 */
typedef struct {
    struct mg_connection *conn;
    uint32_t client_id;
    uint64_t connect_time_ms;
    aicam_bool_t is_active;
    char client_ip[64];                     // Client IP address for identification
    uint64_t last_ping_time_ms;             // Last ping send time
    uint64_t last_pong_time_ms;             // Last pong receive time
    aicam_bool_t ping_pending;              // Ping sent but pong not received
    /* FALSE until this client has received a keyframe prefixed with SPS/PPS.
     * The encoder emits SPS/PPS only once per session (enc.c latch), so a
     * client connecting later never sees them in-band - without this gate
     * jmuxer never sets readyToDecode and silently drops every frame (black
     * screen + loading loop after each watchdog reconnect). */
    aicam_bool_t sps_synced;
} websocket_client_t;

/**
 * @brief Global WebSocket stream server context
 */
static struct {
    struct mg_mgr mgr;
    websocket_stream_config_t config;
    
    // Client management
    websocket_client_t *clients;
    uint32_t client_count;
    uint32_t next_client_id;
    
    // Stream status
    aicam_bool_t stream_active;
    uint32_t current_stream_id;
    uint32_t frame_sequence;
    uint64_t stream_start_time_ms;
    uint32_t stream_frame_counter;
    
    // Statistics
    websocket_stream_stats_t stats;
    uint64_t start_time_ms;
    
    // Thread and status
    osMutexId_t mutex;
    osThreadId_t server_task_id;
    volatile aicam_bool_t is_running;
    aicam_bool_t is_initialized;
    
    // Video Hub subscription 
    video_hub_subscriber_id_t hub_subscriber_id;
    aicam_bool_t hub_mode_enabled;  // whether Hub mode is enabled
} g_websocket_server = {0};

#define WS_BROADCAST_ID  ((unsigned long)-1)

struct MessageData {
       void *buf;
       size_t size;
       int ws_op;
       unsigned long target_id;
};

/* ==================== Internal Function Declarations ==================== */

static void ws_stream_server_task(void *argument);
static void ws_stream_event_handler(struct mg_connection *c, int ev, void *ev_data);
static void ws_stream_add_client(struct mg_connection *conn);
static void ws_stream_remove_client(struct mg_connection *conn);
static void ws_stream_cleanup_old_connections(const char *client_ip);
static void ws_stream_get_client_ip(struct mg_connection *conn, char *ip_buffer, size_t buffer_size);
static void ws_stream_broadcast_packet(const void *packet, size_t packet_size);
static void ws_stream_send_ping_to_clients(void);
static void ws_stream_check_pong_timeout(void);
static aicam_bool_t ws_stream_is_client_alive(websocket_client_t *client);
static uint8_t websocket_stack[1024 * 4] ALIGN_32 IN_PSRAM;

// Video Hub callback functions
static aicam_result_t ws_stream_on_hub_frame(const video_hub_frame_t *frame, void *user_data);
static void ws_stream_on_hub_sps_pps(const video_hub_sps_pps_t *sps_pps, void *user_data);

/**
 * @brief Send close frame with status code and reason to a client
 */
static void ws_stream_send_close(struct mg_connection *conn, uint16_t code, const char *reason)
{
    if (!conn) return;

    char payload[64] = {0};
    size_t reason_len = reason ? strlen(reason) : 0;
    if (reason_len > sizeof(payload) - 2) {
        reason_len = sizeof(payload) - 2;
    }

    payload[0] = (uint8_t)(code >> 8);
    payload[1] = (uint8_t)(code & 0xFF);
    if (reason_len > 0) {
        memcpy(&payload[2], reason, reason_len);
    }

    mg_ws_send(conn, payload, 2 + reason_len, WEBSOCKET_OP_CLOSE);
    conn->is_draining = 1; // allow graceful close so client receives reason
}

/**
 * @brief Get relative timestamp
 * @return Relative timestamp
 */
static uint64_t get_relative_timestamp(void) {
    // Use system tick as relative time base, avoid affected by RTC time modification
    static uint32_t system_start_tick = 0;
    static uint64_t rtc_start_time = 0;
    
    if (system_start_tick == 0) {
        system_start_tick = osKernelGetTickCount();
        rtc_start_time = rtc_get_timeStamp();
    }
    
    uint32_t current_tick = osKernelGetTickCount();
    uint32_t elapsed_ticks = current_tick - system_start_tick;
    uint32_t elapsed_seconds = elapsed_ticks / osKernelGetTickFreq();
    
    return (uint64_t)(rtc_start_time + elapsed_seconds) * 1000;
}


/* ==================== API Implementation ==================== */

void websocket_stream_get_default_config(websocket_stream_config_t *config) {
    if (!config) return;
    
    // Clear entire structure first
    memset(config, 0, sizeof(websocket_stream_config_t));
    
    config->port = 8081;
    config->max_clients = MAX_CLIENTS;
    config->max_frame_size = MAX_FRAME_SIZE;
    
    // Safely copy string
    strncpy(config->stream_path, "/stream", sizeof(config->stream_path) - 1);
    config->stream_path[sizeof(config->stream_path) - 1] = '\0';
    
    config->task_priority = (uint32_t)osPriorityRealtime;
    config->task_stack_size = 4096;
    config->ping_interval_ms = 30000;     // 30 seconds ping interval (less aggressive)
    config->pong_timeout_ms = 10000;      // 10 seconds pong timeout (more tolerant)
}

aicam_result_t websocket_stream_server_init(const websocket_stream_config_t *config) {
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    
    // Check if already initialized
    if (g_websocket_server.is_initialized) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    // Clear global structure
    memset(&g_websocket_server, 0, sizeof(g_websocket_server));
    g_websocket_server.hub_subscriber_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    
    // Copy configuration
    memcpy(&g_websocket_server.config, config, sizeof(websocket_stream_config_t));
    
    // Print configuration information
    LOG_SVC_INFO("WebSocket server init - port: %d, path: %s", 
                 g_websocket_server.config.port, g_websocket_server.config.stream_path);
    
    g_websocket_server.next_client_id = 1;
    
    // 1. Create mutex
    g_websocket_server.mutex = osMutexNew(NULL);
    if (!g_websocket_server.mutex) {
        return AICAM_ERROR;
    }
    
    // 2. Dynamically allocate client list
    g_websocket_server.clients = buffer_calloc(g_websocket_server.config.max_clients, sizeof(websocket_client_t));
    if (!g_websocket_server.clients) {
        osMutexDelete(g_websocket_server.mutex);
        return AICAM_ERROR;
    }


    //g_websocket_server.mgr.buffer_size = g_websocket_server.config.max_frame_size;
    
    g_websocket_server.is_initialized = AICAM_TRUE;
    
    LOG_SVC_INFO("WebSocket server initialized successfully");
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_deinit(void) {
    if (!g_websocket_server.is_initialized) {
        return AICAM_OK;
    }
    
    // 取消Hub订阅
    websocket_stream_server_unsubscribe_hub();
    
    // Ensure server is stopped
    websocket_stream_server_stop();
    
    // Release resources
    if (g_websocket_server.clients) {
        buffer_free(g_websocket_server.clients);
        g_websocket_server.clients = NULL;
    }
    
    if (g_websocket_server.mutex) {
        osMutexDelete(g_websocket_server.mutex);
        g_websocket_server.mutex = NULL;
    }
    
    mg_mgr_free(&g_websocket_server.mgr);
    
    // Clear global structure
    memset(&g_websocket_server, 0, sizeof(g_websocket_server));
    
    LOG_SVC_INFO("WebSocket server deinitialized");
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_start(void) {
    if (!g_websocket_server.is_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    if (g_websocket_server.is_running) {
        osMutexRelease(g_websocket_server.mutex);
        return AICAM_ERROR_ALREADY_RUNNING;
    }

    
    g_websocket_server.is_running = AICAM_TRUE;
    g_websocket_server.start_time_ms = get_relative_timestamp();
    
    const osThreadAttr_t task_attrs = {
        .name = "ws_stream_server",
        .stack_mem = websocket_stack,
        .stack_size = g_websocket_server.config.task_stack_size,
        .priority = (osPriority_t)g_websocket_server.config.task_priority,
    };
    
    g_websocket_server.server_task_id = osThreadNew(ws_stream_server_task, NULL, &task_attrs);
    if (!g_websocket_server.server_task_id) {
        g_websocket_server.is_running = AICAM_FALSE;
        osMutexRelease(g_websocket_server.mutex);
        return AICAM_ERROR;
    }
    
    osMutexRelease(g_websocket_server.mutex);
    
    LOG_SVC_INFO("WebSocket server started successfully");
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_stop(void) {
    if (!g_websocket_server.is_initialized) {
        return AICAM_OK;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    if (!g_websocket_server.is_running) {
        osMutexRelease(g_websocket_server.mutex);
        return AICAM_OK;
    }
    
    g_websocket_server.is_running = AICAM_FALSE;
    
    osMutexRelease(g_websocket_server.mutex);
    
    if (g_websocket_server.server_task_id) {
        osThreadTerminate(g_websocket_server.server_task_id);
        g_websocket_server.server_task_id = NULL;
    }
    
    LOG_SVC_INFO("WebSocket server stopped");
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_start_stream(uint32_t stream_id) {
    if (!g_websocket_server.is_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Subscribe to Video Hub (receive encoded frames via Hub)
    aicam_result_t result = websocket_stream_server_subscribe_hub();
    if (result != AICAM_OK) {
        LOG_SVC_ERROR("Failed to subscribe to Video Hub");
        return result;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    g_websocket_server.stream_active = AICAM_TRUE;
    g_websocket_server.current_stream_id = stream_id;
    g_websocket_server.frame_sequence = 0;
    g_websocket_server.stream_start_time_ms = get_relative_timestamp();
    g_websocket_server.stream_frame_counter = 0;
    g_websocket_server.stats.stream_active = AICAM_TRUE;
    g_websocket_server.stats.stream_id = stream_id;

    /* New stream session: force every connected client to re-sync at the
     * next keyframe with fresh SPS/PPS (encoder params may have changed). */
    for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
        g_websocket_server.clients[i].sps_synced = AICAM_FALSE;
    }
    
    osMutexRelease(g_websocket_server.mutex);
    
    LOG_SVC_INFO("WebSocket stream started (Hub mode) - ID: %u", stream_id);
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_stop_stream(void) {
    if (!g_websocket_server.is_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    // Unsubscribe from Video Hub
    websocket_stream_server_unsubscribe_hub();
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    g_websocket_server.stream_active = AICAM_FALSE;
    g_websocket_server.stats.stream_active = AICAM_FALSE;
    g_websocket_server.stats.stream_fps = 0;
    
    osMutexRelease(g_websocket_server.mutex);
    
    LOG_SVC_INFO("WebSocket stream stopped (Hub unsubscribed)");
    return AICAM_OK;
}

/**
 * @deprecated After using Hub mode, this function is deprecated. Frames are automatically distributed to WebSocket via video_hub.
 */
aicam_result_t websocket_stream_server_send_frame(const void *frame_data, size_t frame_size, 
                                                uint64_t timestamp, websocket_frame_type_t frame_type,
                                                uint32_t width, uint32_t height) {
    return websocket_stream_server_send_frame_with_encoder_info(frame_data, frame_size, timestamp, 
                                                              frame_type, width, height, NULL);
}

/**
 * @deprecated After using Hub mode, this function is deprecated. Frames are automatically distributed to WebSocket via video_hub.
 */
aicam_result_t websocket_stream_server_send_frame_with_encoder_info(const void *frame_data, size_t frame_size, 
                                                                   uint64_t timestamp, websocket_frame_type_t frame_type,
                                                                   uint32_t width, uint32_t height,
                                                                   const void *encoder_info) {
    if (!g_websocket_server.is_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (!frame_data || frame_size == 0) {
        LOG_SVC_ERROR("Invalid frame data or size");
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Verify frame_data has enough space for header
    if (frame_size < sizeof(websocket_frame_header_t)) {
        LOG_SVC_ERROR("Frame size too small for header");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    if (!g_websocket_server.stream_active) {
        osMutexRelease(g_websocket_server.mutex);
        LOG_SVC_ERROR("Stream is not active");
        return AICAM_ERROR;
    }

    if (frame_size > g_websocket_server.config.max_frame_size) {
        g_websocket_server.stats.error_count++;
        osMutexRelease(g_websocket_server.mutex);
        LOG_SVC_ERROR("Frame size is too large");
        return AICAM_ERROR;
    }

    uint8_t *packet_buffer = (uint8_t*)frame_data;

    // Fill frame header at the beginning of frame_data
    websocket_frame_header_t *header = (websocket_frame_header_t*)packet_buffer;
    
    // Fill basic header fields
    header->magic = WS_TO_NETWORK_32(WS_FRAME_MAGIC);
    header->version = WS_FRAME_VERSION;
    header->frame_type = (uint8_t)frame_type;
    header->timestamp = WS_TO_NETWORK_64(timestamp);
    
    // Fill frame metadata
    // header->frame_size = WS_TO_NETWORK_32((uint32_t)frame_size);
    // header->stream_id = WS_TO_NETWORK_32(g_websocket_server.current_stream_id);
    // header->sequence = WS_TO_NETWORK_32(g_websocket_server.frame_sequence++);
    // header->width = WS_TO_NETWORK_32(width);
    // header->height = WS_TO_NETWORK_32(height);
    // header->format = 0; // Reserved for future use
    // header->flags = 0;  // Reserved for future use
    
    // Fill encoder information if provided
    // if (encoder_info) {
    //     const H264EncOut *enc_info = (const H264EncOut *)encoder_info;
    //     header->coding_type = WS_TO_NETWORK_32((uint32_t)enc_info->codingType);
    //     header->stream_size = WS_TO_NETWORK_32((uint32_t)enc_info->streamSize);
    //     header->num_nalus = WS_TO_NETWORK_32((uint32_t)enc_info->numNalus);
    //     header->mse_mul256 = WS_TO_NETWORK_32((uint32_t)enc_info->mse_mul256);
    // }
    
    // Cache timestamp for statistics calculation
    uint64_t current_time_ms = get_relative_timestamp();
    
    // Broadcast to all clients (send the entire frame_data including header)
    ws_stream_broadcast_packet(packet_buffer, frame_size);
    
    // Update statistics
    g_websocket_server.stats.total_frames_sent++;
    g_websocket_server.stats.total_bytes_sent += frame_size;
    g_websocket_server.stream_frame_counter++;
    uint64_t stream_duration_ms = current_time_ms - g_websocket_server.stream_start_time_ms;
    if (stream_duration_ms > 1000) {
        g_websocket_server.stats.stream_fps = (uint32_t)(g_websocket_server.stream_frame_counter * 1000 / stream_duration_ms);
    }
    
    osMutexRelease(g_websocket_server.mutex);
    return AICAM_OK;
}

aicam_result_t websocket_stream_server_get_stats(websocket_stream_stats_t *stats) {
    if (!g_websocket_server.is_initialized || !stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    *stats = g_websocket_server.stats;
    stats->active_clients = g_websocket_server.client_count;
    stats->uptime_ms = get_relative_timestamp() - g_websocket_server.start_time_ms;

    
    osMutexRelease(g_websocket_server.mutex);
    
    return AICAM_OK;
}

aicam_bool_t websocket_stream_server_is_initialized(void) {
    return g_websocket_server.is_initialized;
}

aicam_bool_t websocket_stream_server_is_running(void) {
    return g_websocket_server.is_initialized && g_websocket_server.is_running;
}

/* ==================== Internal Functions ==================== */

static void ws_stream_server_task(void *argument) {
    (void)argument; // Avoid unused parameter warning
    
    uint64_t last_ping_check_ms = 0;
    const uint32_t ping_check_interval_ms = 1000; // Check ping/pong every 1 second

    mg_mgr_init(&g_websocket_server.mgr);

    char url[128];
    #if IS_HTTPS
        snprintf(url, sizeof(url), "wss://0.0.0.0:%d", g_websocket_server.config.port);
    #else
        snprintf(url, sizeof(url), "ws://0.0.0.0:%d", g_websocket_server.config.port);
    #endif
        
    LOG_SVC_INFO("Starting WebSocket server on %s", url);
    
    if (mg_http_listen(&g_websocket_server.mgr, url, ws_stream_event_handler, NULL) == NULL) {
        LOG_SVC_ERROR("Failed to start WebSocket server");
        g_websocket_server.is_running = AICAM_FALSE;
        return;
    }

    if (mg_wakeup_init(&g_websocket_server.mgr) == false) {
        LOG_SVC_ERROR("Failed to initialize wakeup");
        g_websocket_server.is_running = AICAM_FALSE;
        return;
    }


    
    while (g_websocket_server.is_running) {
        mg_mgr_poll(&g_websocket_server.mgr, 20); // 20ms poll timeout
        
        uint64_t current_time_ms = get_relative_timestamp();
        
        // Periodically check ping/pong
        if (current_time_ms - last_ping_check_ms >= ping_check_interval_ms) {
            last_ping_check_ms = current_time_ms;
            
            // Send ping to clients if enabled
            if (g_websocket_server.config.ping_interval_ms > 0) {
                ws_stream_send_ping_to_clients();
            }
            
            // Check pong timeout if enabled
            if (g_websocket_server.config.pong_timeout_ms > 0) {
                ws_stream_check_pong_timeout();
            }
        }
    }
}

/* ==================== Frame copy FIFO (fix for the in-flight buffer race) ==
 * Proven by probe ("WS RACE CONFIRMED ... after 48ms"): the wakeup pipe used
 * to carry POINTERS into the encoder's single output buffer; a web-task
 * stall > 40ms (one frame period) let the next encode's DMA overwrite the
 * frame while mg_ws_send was reading it -> torn frame at every client
 * (WebKit: MEDIA_ERR_DECODE; Chromium conceals).
 * Frames are now COPIED into private PSRAM ring slots in the producer task
 * (encoder context, mutex held) and drained by the web task on MG_EV_POLL -
 * the copy can no longer be overwritten by the encoder.
 * Overflow policy: drop new frames and resume at the next keyframe so P
 * chains stay decodable (drops counted). */
#define WS_FIFO_SLOTS      5
#define WS_FIFO_SLOT_SIZE  (128u * 1024u)
/* Cap a client's unsent outbound buffer: bounded worst-case for a dead
 * connection, while allowing one full max-size frame to queue for a slow
 * but live client. */
#define WS_SEND_BUF_CAP    (512u * 1024u)
static uint8_t s_ws_fifo_mem[WS_FIFO_SLOTS][WS_FIFO_SLOT_SIZE] ALIGN_32 IN_PSRAM;
static struct {
    uint8_t *data;  /* static slot, or heap buffer for oversize frames */
    uint32_t size;
    uint8_t is_heap;
} s_ws_fifo_meta[WS_FIFO_SLOTS];
static uint32_t s_ws_fifo_head = 0, s_ws_fifo_tail = 0, s_ws_fifo_count = 0;
static aicam_bool_t s_ws_fifo_skip_until_key = AICAM_FALSE;
static uint32_t s_ws_fifo_drops = 0;

/* Producer (encoder task, g_websocket_server.mutex already held). */
static void ws_frame_fifo_push(const void *packet, size_t size)
{
    const websocket_frame_header_t *hdr = (const websocket_frame_header_t *) packet;

    /* Drop decisions FIRST - anything allocated below must be guaranteed a
     * slot in the fifo or we leak it. */
    if (s_ws_fifo_skip_until_key) {
        if (hdr->frame_type != WS_FRAME_TYPE_H264_KEY) {
            s_ws_fifo_drops++;
            return; /* wait for a clean restart point */
        }
        s_ws_fifo_skip_until_key = AICAM_FALSE;
    }

    if (s_ws_fifo_count == WS_FIFO_SLOTS) {
        s_ws_fifo_drops++;
        s_ws_fifo_skip_until_key = AICAM_TRUE;
        LOG_SVC_WARN("WS fifo full, dropping until next keyframe (total drops %lu)",
                     (unsigned long) s_ws_fifo_drops);
        return;
    }

    /* Static slot covers normal frames; oversize ones (e.g. huge I-frames at
     * high bitrate) fall back to a heap copy instead of being dropped:
     * dropping recurring keyframes would permanently break the stream. */
    uint8_t *copy_dst = s_ws_fifo_mem[s_ws_fifo_head];
    uint8_t is_heap = 0;
    if (size > WS_FIFO_SLOT_SIZE) {
        copy_dst = hal_mem_alloc_large(size);
        if (copy_dst == NULL) {
            s_ws_fifo_drops++;
            s_ws_fifo_skip_until_key = AICAM_TRUE;
            LOG_SVC_WARN("WS fifo: frame %lu > slot %lu and alloc failed, dropped (total %lu)",
                         (unsigned long) size, (unsigned long) WS_FIFO_SLOT_SIZE,
                         (unsigned long) s_ws_fifo_drops);
            return;
        }
        is_heap = 1;
    }

    memcpy(copy_dst, packet, size);
    s_ws_fifo_meta[s_ws_fifo_head].data = copy_dst;
    s_ws_fifo_meta[s_ws_fifo_head].size = (uint32_t) size;
    s_ws_fifo_meta[s_ws_fifo_head].is_heap = is_heap;
    s_ws_fifo_head = (s_ws_fifo_head + 1) % WS_FIFO_SLOTS;
    s_ws_fifo_count++;
}


/* Build the late-joiner frame [header][SPS][PPS][keyframe] in ONE buffer -
 * the exact byte shape of an encoder session start, which is the only form
 * a cold jmuxer reliably initializes from (an SPS/PPS-only feed would push
 * a slice-less sample into MSE; a bare IDR leaves readyToDecode false).
 * The hub cache stores bare NAL payloads (no start codes), so the annexB
 * start code is re-inserted before each. Returns a hal_mem_alloc_large
 * buffer the caller must free, or NULL when the cache is unavailable or
 * the alloc fails - caller then falls back to the bare keyframe, which is
 * self-sufficient when the frame itself carries in-band SPS/PPS. */
#define WS_SPS_PPS_MAX 128
static const uint8_t ws_annexb_start_code[4] = {0x00, 0x00, 0x00, 0x01};

static uint8_t *ws_build_join_frame(const uint8_t *fifo_frame, uint32_t size,
                                    size_t *join_size)
{
    video_hub_sps_pps_t hub_sp;

    if (video_hub_get_sps_pps(&hub_sp) != AICAM_OK ||
        hub_sp.sps_size == 0 || hub_sp.pps_size == 0 ||
        hub_sp.sps_size > WS_SPS_PPS_MAX || hub_sp.pps_size > WS_SPS_PPS_MAX) {
        LOG_SVC_WARN("WS join-sync: no usable SPS/PPS cache, sending bare keyframe");
        return NULL;
    }

    const size_t hdr_size = sizeof(websocket_frame_header_t);
    uint32_t key_size = size - (uint32_t) hdr_size;
    uint32_t params_size = 4 + hub_sp.sps_size + 4 + hub_sp.pps_size;

    /* Called with the ws mutex held; lock order ws->hub is safe because the
     * hub never holds its mutex while calling back into this server
     * (notify_frame/notify_sps_pps snapshot-and-release). */
    uint8_t *buf = hal_mem_alloc_large(hdr_size + params_size + key_size);
    if (buf == NULL) {
        LOG_SVC_WARN("WS join-sync: alloc failed, sending bare keyframe");
        return NULL;
    }

    memcpy(buf, fifo_frame, hdr_size);            /* reuse keyframe header */
    uint8_t *p = buf + hdr_size;
    memcpy(p, ws_annexb_start_code, 4); p += 4;
    memcpy(p, hub_sp.sps_data, hub_sp.sps_size); p += hub_sp.sps_size;
    memcpy(p, ws_annexb_start_code, 4); p += 4;
    memcpy(p, hub_sp.pps_data, hub_sp.pps_size); p += hub_sp.pps_size;
    memcpy(p, fifo_frame + hdr_size, key_size);

    websocket_frame_header_t *hdr = (websocket_frame_header_t *) buf;
    hdr->frame_size = WS_TO_NETWORK_32(params_size + key_size);

    *join_size = hdr_size + params_size + key_size;
    return buf;
}

/* Consumer (web task, MG_EV_POLL): send queued frames to all live clients. */
static void ws_frame_fifo_drain(void)
{
    for (;;) {
        uint8_t *data;
        uint32_t size;
        uint8_t heap;
        uint8_t *join_buf = NULL;
        size_t join_size = 0;
        aicam_bool_t join_built = AICAM_FALSE;

        osMutexAcquire(g_websocket_server.mutex, osWaitForever);
        if (s_ws_fifo_count == 0) {
            osMutexRelease(g_websocket_server.mutex);
            return;
        }
        data = s_ws_fifo_meta[s_ws_fifo_tail].data;
        size = s_ws_fifo_meta[s_ws_fifo_tail].size;
        heap = s_ws_fifo_meta[s_ws_fifo_tail].is_heap;
        s_ws_fifo_tail = (s_ws_fifo_tail + 1) % WS_FIFO_SLOTS;
        s_ws_fifo_count--;

        for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
            websocket_client_t *client = &g_websocket_server.clients[i];
            struct mg_connection *conn = client->conn;
            if (!(client->is_active &&
                  conn &&
                  !conn->is_closing &&
                  ws_stream_is_client_alive(client))) {
                continue;
            }
            /* Backpressure cap: a client that stops reading (abrupt
             * disconnect before ping/pong detects it) would otherwise
             * accumulate mg_ws_send data in conn->send unboundedly - the
             * iobuf grew to 4MB and then failed every resize, flooding
             * MG_ERROR at frame rate. Drop frames for a stalled client
             * instead of buffering past one max frame. */
            if (conn->send.len >= WS_SEND_BUF_CAP) {
                LOG_SVC_WARN("WS send-buffer cap hit (%lu bytes), dropping frame for client %u",
                             (unsigned long) conn->send.len,
                             client->client_id);
                continue;
            }

            if (!client->sps_synced) {
                /* Late joiner: hold P-frames until a keyframe, then send
                 * SPS/PPS + that keyframe as one frame. Without this the
                 * client never gets parameter sets (encoder emits them
                 * once per session, before it connected) and jmuxer drops
                 * every frame with no error. */
                const websocket_frame_header_t *hdr =
                    (const websocket_frame_header_t *) data;
                if (size < sizeof(*hdr) ||
                    hdr->magic != WS_TO_NETWORK_32(WS_FRAME_MAGIC) ||
                    hdr->frame_type != WS_FRAME_TYPE_H264_KEY) {
                    continue; /* raw/headerless or delta frame - wait */
                }
                if (!join_built) {
                    join_buf = ws_build_join_frame(data, size, &join_size);
                    if (join_buf == NULL) {
                        join_buf = data; /* bare-keyframe fallback */
                        join_size = size;
                    }
                    join_built = AICAM_TRUE;
                }
                mg_ws_send(conn, join_buf, join_size, WEBSOCKET_OP_BINARY);
                client->sps_synced = AICAM_TRUE;
                WS_LOG("[WS] join-sync id=%lu client=%u frame=%luB (sps+pps+key)",
                       (unsigned long) conn->id, client->client_id,
                       (unsigned long) join_size);
                continue;
            }

            mg_ws_send(conn, data, size, WEBSOCKET_OP_BINARY);
        }
        osMutexRelease(g_websocket_server.mutex);
        if (heap) {
            hal_mem_free(data);
        }
        if (join_built && join_buf != data) {
            hal_mem_free(join_buf);
        }
    }
}

static void ws_stream_event_handler(struct mg_connection *c, int ev, void *ev_data) {

    /* Connection lifecycle logs (same purpose as web server's [CONN]):
     * who disconnected the phone - us (replace/pong-timeout) or the peer */
    if (ev == MG_EV_ACCEPT) {
        WS_LOG("[WS] tcp-accept id=%lu", (unsigned long) c->id);
    }
    if (ev == MG_EV_ERROR) {
        /* Socket-level failure on this conn (the mg_error "socket error" path
         * the phone hits) - distinguish peer RST vs our tx failure */
        WS_LOG("[WS] socket-error id=%lu err=%s",
               (unsigned long) c->id,
               ev_data ? (const char *) ev_data : "?");
    }

    switch (ev) {
    #if IS_HTTPS
        case MG_EV_ACCEPT: {
            // printf("[WS]MG_EV_ACCEPT: %d\r\n", c->fd);
            LOG_SVC_INFO("WebSocket connection accepted");
            struct mg_tls_opts opts = {
            .cert = {
                .buf = HTTPS_CERT_STR,
                .len = sizeof(HTTPS_CERT_STR) - 1,
            },
            .key = {
                .buf = HTTPS_KEY_STR,
                .len = sizeof(HTTPS_KEY_STR) - 1,
            },
            .skip_verification = 1
            };
            mg_tls_init(c, &opts);
            break;
        }
        case MG_EV_TLS_HS: {
            // printf("[WS]MG_EV_TLS_HS: %d\r\n", c->fd);
            LOG_SVC_INFO("WebSocket TLS handshake completed");
            break;
        }
    #endif
        case MG_EV_HTTP_MSG: {
            struct mg_http_message *hm = (struct mg_http_message *)ev_data;
            LOG_SVC_INFO("HTTP request: %s", hm->uri.buf);
            
            if (mg_match(hm->uri, mg_str(g_websocket_server.config.stream_path), NULL)) {
                LOG_SVC_INFO("Upgrading to WebSocket");
                mg_ws_upgrade(c, hm, NULL);
            } else {
                LOG_SVC_INFO("Not found: %s", hm->uri.buf);
                mg_http_reply(c, 404, "", "Not Found\n");
            }
            break;
        }
        case MG_EV_WS_OPEN: {
            LOG_SVC_INFO("WebSocket connection opened");
            LOG_SVC_INFO("[WS]MG_EV_OPEN: %d", c->fd);
            ws_stream_add_client(c);
            break;
        }
        case MG_EV_WAKEUP: {
            struct mg_str *data = (struct mg_str *)ev_data;
            if (data->len == sizeof(struct MessageData)) {
                struct MessageData *msg = (struct MessageData *)data->buf;
                if (msg->target_id == WS_BROADCAST_ID) {
                    // broadcast to all websocket clients
                    for(struct mg_connection *conn = c->mgr->conns; conn != NULL; conn = conn->next) {
                        if (conn->data[0] == 'W' && !conn->is_closing) {
                            mg_ws_send(conn, msg->buf, msg->size, msg->ws_op);
                        }
                    }
                } else {
                    // send to specific client
                    for(struct mg_connection *conn = c->mgr->conns; conn != NULL; conn = conn->next) {
                        if (conn->data[0] == 'W' && conn->id == msg->target_id && !conn->is_closing) {
                            mg_ws_send(conn, msg->buf, msg->size, msg->ws_op);
                            break; // Found the target, no need to continue
                        }
                    }
                }
            }
            break;
        }
        case MG_EV_POLL: {
            /* Drain queued frame copies once per poll tick (~20ms). Fired for
             * every connection; empty-queue calls are a cheap locked check. */
            ws_frame_fifo_drain();
            break;
        }
        case MG_EV_WS_CTL: {
            // Handle WebSocket control frames (ping/pong)
            struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
            WS_LOG("[WS] ctl op=0x%x len=%u conn=%lu",
                   wm ? (wm->flags & 0x0F) : 0, wm ? (unsigned) wm->data.len : 0,
                   (unsigned long) c->id);
            // Close frame: decode code+reason so the log shows WHO closed and
            // why ('teardown' = frontend worker cleanup, 1006/empty = link died)
            if (wm && (wm->flags & 0x0F) == WEBSOCKET_OP_CLOSE && wm->data.len >= 2) {
                char reason[32];
                size_t rlen = wm->data.len - 2;
                if (rlen > sizeof(reason) - 1) rlen = sizeof(reason) - 1;
                if (rlen > 0) memcpy(reason, wm->data.buf + 2, rlen);
                reason[rlen] = '\0';
                WS_LOG("[WS] peer-close id=%lu code=%u reason=%s",
                       (unsigned long) c->id,
                       (unsigned) (((uint8_t) wm->data.buf[0] << 8) | (uint8_t) wm->data.buf[1]),
                       reason);
            }
            if (wm && (wm->flags & 0x0F) == WEBSOCKET_OP_PONG) {
                // Received pong, update client status
                // Note: Mongoose automatically sends pong in response to ping,
                // so this is the pong response to our ping
                osMutexAcquire(g_websocket_server.mutex, osWaitForever);
                for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
                    if (g_websocket_server.clients[i].is_active && 
                        g_websocket_server.clients[i].conn == c) {
                        uint64_t current_time = get_relative_timestamp();
                        g_websocket_server.clients[i].last_pong_time_ms = current_time;
                        g_websocket_server.clients[i].ping_pending = AICAM_FALSE;
                        break;
                    }
                }
                osMutexRelease(g_websocket_server.mutex);
            }
            // Note: WEBSOCKET_OP_PING from client is automatically handled by mongoose
            // (mongoose sends pong automatically), so we don't need to handle it here
            break;
        }
        case MG_EV_CLOSE: {
            LOG_SVC_INFO("[WS]MG_EV_CLOSE: %d", c->fd);
            LOG_SVC_INFO("WebSocket Connection closed");
            ws_stream_remove_client(c);
            break;
        }
    }
}

static void ws_stream_add_client(struct mg_connection *conn) {
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    // Get client IP address
    char client_ip[64];
    ws_stream_get_client_ip(conn, client_ip, sizeof(client_ip));

    //flag the connection is a websocket connection for wakeup
    conn->data[0] = 'W';
    
    LOG_SVC_INFO("New WebSocket connection from IP: %s ", client_ip);
    
    // Clean up any existing connections from the same IP
    ws_stream_cleanup_old_connections(client_ip);
    
    if (g_websocket_server.client_count >= g_websocket_server.config.max_clients) {
        // Send close frame and close connection
        ws_stream_send_close(conn, 1000, "Too many clients");
        osMutexRelease(g_websocket_server.mutex);
        LOG_SVC_WARN("Rejected connection id=%lu from %s: too many clients",
                     (unsigned long) conn->id, client_ip);
        return;
    }
    
    for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
        if (!g_websocket_server.clients[i].is_active) {
            g_websocket_server.clients[i].is_active = AICAM_TRUE;
            g_websocket_server.clients[i].conn = conn;
            g_websocket_server.clients[i].client_id = g_websocket_server.next_client_id++;
            uint64_t current_time = get_relative_timestamp();
            g_websocket_server.clients[i].connect_time_ms = current_time;
            // Initialize last_ping_time_ms to current time to avoid immediate ping on first check
            g_websocket_server.clients[i].last_ping_time_ms = current_time;
            g_websocket_server.clients[i].last_pong_time_ms = current_time;
            g_websocket_server.clients[i].ping_pending = AICAM_FALSE;
            /* Wait for a keyframe+SPS/PPS join frame before streaming to
             * this client (see ws_frame_fifo_drain). */
            g_websocket_server.clients[i].sps_synced = AICAM_FALSE;
            
            // Store client IP for future identification
            strncpy(g_websocket_server.clients[i].client_ip, client_ip, sizeof(g_websocket_server.clients[i].client_ip) - 1);
            g_websocket_server.clients[i].client_ip[sizeof(g_websocket_server.clients[i].client_ip) - 1] = '\0';
            
            g_websocket_server.client_count++;
            g_websocket_server.stats.total_connections++;

            WS_LOG("[WS] open id=%lu ip=%s clients=%lu/%lu",
                   (unsigned long) conn->id, client_ip,
                   (unsigned long) g_websocket_server.client_count,
                   (unsigned long) g_websocket_server.config.max_clients);
            LOG_SVC_INFO("Client connected - IP: %s, ID: %u, Total: %u",
                        client_ip, g_websocket_server.clients[i].client_id, g_websocket_server.client_count);
            break;
        }
    }
    
    osMutexRelease(g_websocket_server.mutex);
}

static void ws_stream_remove_client(struct mg_connection *conn) {
    
    if (osMutexAcquire(g_websocket_server.mutex, 100) != osOK) {
        return;
    }
    
    for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
        if (g_websocket_server.clients[i].is_active && g_websocket_server.clients[i].conn == conn) {
            LOG_SVC_INFO("Client disconnected - IP: %s, ID: %u, Total: %u",
                        g_websocket_server.clients[i].client_ip,
                        g_websocket_server.clients[i].client_id,
                        g_websocket_server.client_count - 1);
            WS_LOG("[WS] closed id=%lu ip=%s clients=%lu (peer closed or our close finished)",
                   (unsigned long) conn->id,
                   g_websocket_server.clients[i].client_ip,
                   (unsigned long) g_websocket_server.client_count - 1);

            g_websocket_server.clients[i].is_active = AICAM_FALSE;
            g_websocket_server.clients[i].client_ip[0] = '\0'; // Clear IP
            g_websocket_server.client_count--;
            g_websocket_server.stats.total_disconnections++;
            break;
        }
    }
    
    osMutexRelease(g_websocket_server.mutex);
}

static void ws_stream_get_client_ip(struct mg_connection *conn, char *ip_buffer, size_t buffer_size) {
    if (!conn || !ip_buffer || buffer_size == 0) {
        if (ip_buffer && buffer_size > 0) {
            ip_buffer[0] = '\0';
        }
        return;
    }
    
    // Extract IP address from remote address
    char addr_str[64];
    mg_snprintf(addr_str, sizeof(addr_str), "%M", mg_print_ip, &conn->rem);
    
    // Copy only the IP part (remove port if present)
    char *colon_pos = strchr(addr_str, ':');
    if (colon_pos) {
        size_t ip_len = colon_pos - addr_str;
        size_t copy_len = (ip_len < buffer_size - 1) ? ip_len : buffer_size - 1;
        strncpy(ip_buffer, addr_str, copy_len);
        ip_buffer[copy_len] = '\0';
    } else {
        strncpy(ip_buffer, addr_str, buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
    }
}

static void ws_stream_cleanup_old_connections(const char *client_ip) {
    // Note: This function must be called with mutex already held
    if (!client_ip) return;
    
    // Store connection IDs to close (avoid closing the connection we're about to add)
    uint32_t conn_ids_to_close[MAX_CLIENTS];
    uint32_t close_count = 0;
    
    for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
        if (g_websocket_server.clients[i].is_active && 
            strcmp(g_websocket_server.clients[i].client_ip, client_ip) == 0 &&
            g_websocket_server.clients[i].conn) {
            
            // Store connection ID for later closing
            if (close_count < MAX_CLIENTS) {
                conn_ids_to_close[close_count++] = g_websocket_server.clients[i].conn->id;
            }
            
            LOG_SVC_INFO("Cleaning up old connection from IP: %s (ID: %u)", 
                        client_ip, g_websocket_server.clients[i].client_id);
            
            // Mark as inactive first
            g_websocket_server.clients[i].is_active = AICAM_FALSE;
            g_websocket_server.clients[i].conn = NULL; // Clear connection pointer
            g_websocket_server.client_count--;
            g_websocket_server.stats.total_disconnections++;
            WS_LOG("[WS] replace-close id=%lu ip=%s (superseded by new conn from same ip)",
                   (unsigned long) conn_ids_to_close[close_count - 1], client_ip);
        }
    }
    
    // Close connections after marking them inactive (outside the loop to avoid issues)
    for (uint32_t j = 0; j < close_count; j++) {
        // Find the connection by ID and close it
        for (struct mg_connection *conn = g_websocket_server.mgr.conns; conn != NULL; conn = conn->next) {
            if (conn->id == conn_ids_to_close[j] && conn->data[0] == 'W') {
                ws_stream_send_close(conn, 1000, "Connection replaced");
                break;
            }
        }
    }
}

/**
 * @brief Check if client is alive
 * @note This function must be called with mutex already held
 */
static aicam_bool_t ws_stream_is_client_alive(websocket_client_t *client) {
    if (!client || !client->is_active || !client->conn || client->conn->is_closing) {
        return AICAM_FALSE;
    }
    
    // If ping/pong is disabled, consider client alive if connection is not closing
    if (g_websocket_server.config.ping_interval_ms == 0 || 
        g_websocket_server.config.pong_timeout_ms == 0) {
        return AICAM_TRUE;
    }
    
    // If ping is pending and timeout exceeded, client is dead
    if (client->ping_pending) {
        uint64_t current_time_ms = get_relative_timestamp();
        if (current_time_ms - client->last_ping_time_ms > g_websocket_server.config.pong_timeout_ms) {
            return AICAM_FALSE;
        }
    }
    
    return AICAM_TRUE;
}

static void ws_stream_broadcast_packet(const void *packet, size_t packet_size) {
    // Note: This function must be called with mutex already held
    if (g_websocket_server.client_count == 0) return;

    /* Copy into the private FIFO (see block above): the encoder reuses its
     * output buffer 40ms later; handing raw pointers to the async web task
     * is the torn-frame race. Web task drains on MG_EV_POLL. */
    ws_frame_fifo_push(packet, packet_size);
}

static void ws_stream_send_ping_to_clients(void) {
    if (g_websocket_server.client_count == 0) return;
    if (g_websocket_server.config.ping_interval_ms == 0) return;
    
    uint64_t current_time_ms = get_relative_timestamp();
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
        if (g_websocket_server.clients[i].is_active && 
            g_websocket_server.clients[i].conn &&
            !g_websocket_server.clients[i].conn->is_closing) {
            
            // Check if it's time to send ping
            uint64_t time_since_last_ping = current_time_ms - g_websocket_server.clients[i].last_ping_time_ms;
            
            // Send ping if interval has passed and no ping is pending
            // Note: If ping_interval_ms < ping_check_interval_ms (1000ms), 
            // ping may be sent with slight delay, but this is acceptable
            if (!g_websocket_server.clients[i].ping_pending &&
                time_since_last_ping >= g_websocket_server.config.ping_interval_ms) {
                
                // Send ping frame (empty payload)
                struct MessageData message_data = {
                    .buf = "",
                    .size = 0,
                    .ws_op = WEBSOCKET_OP_PING,
                    .target_id = g_websocket_server.clients[i].conn->id
                };
                mg_wakeup(&g_websocket_server.mgr, 1, &message_data, sizeof(message_data));

                g_websocket_server.clients[i].last_ping_time_ms = current_time_ms;
                g_websocket_server.clients[i].ping_pending = AICAM_TRUE;
            }
        }
    }
    
    osMutexRelease(g_websocket_server.mutex);
}

static void ws_stream_check_pong_timeout(void) {
    if (g_websocket_server.client_count == 0) return;
    if (g_websocket_server.config.pong_timeout_ms == 0) return;
    
    uint64_t current_time_ms = get_relative_timestamp();
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
        if (g_websocket_server.clients[i].is_active && 
            g_websocket_server.clients[i].ping_pending &&
            g_websocket_server.clients[i].conn &&
            !g_websocket_server.clients[i].conn->is_closing) {
            
            // Check if pong timeout exceeded
            uint64_t time_since_ping = current_time_ms - g_websocket_server.clients[i].last_ping_time_ms;
            
            if (time_since_ping > g_websocket_server.config.pong_timeout_ms) {
                LOG_SVC_WARN("Pong timeout for client %u (IP: %s), closing connection id=%lu since_ping=%lums",
                            g_websocket_server.clients[i].client_id,
                            g_websocket_server.clients[i].client_ip,
                            (unsigned long) g_websocket_server.clients[i].conn->id,
                            (unsigned long) time_since_ping);

                // Close connection
                ws_stream_send_close(g_websocket_server.clients[i].conn, 1000, "Pong timeout");
                
                // Mark as inactive
                g_websocket_server.clients[i].is_active = AICAM_FALSE;
                g_websocket_server.clients[i].conn = NULL;
                g_websocket_server.client_count--;
                g_websocket_server.stats.total_disconnections++;
            }
        }
    }
    
    osMutexRelease(g_websocket_server.mutex);
}

/* ==================== Video Hub Callbacks ==================== */

/**
 * @brief Hub frame callback - broadcast encoded frame to all WebSocket clients
 */
static aicam_result_t ws_stream_on_hub_frame(const video_hub_frame_t *frame, void *user_data)
{
    (void)user_data;
    
    if (!g_websocket_server.is_initialized || !g_websocket_server.stream_active) {
        return AICAM_ERROR_UNAVAILABLE;
    }
    
    if (!frame || frame->size == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (g_websocket_server.client_count == 0) {
        return AICAM_OK;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    uint8_t *send_data;
    size_t send_size;
    
    // Check if reserved space is sufficient for WebSocket header
    if (frame->header_offset >= sizeof(websocket_frame_header_t)) {
        // Zero-copy: fill header in reserved space
        send_data = frame->data - sizeof(websocket_frame_header_t);
        websocket_frame_header_t *header = (websocket_frame_header_t*)send_data;
        
        header->magic = WS_TO_NETWORK_32(WS_FRAME_MAGIC);
        header->version = WS_FRAME_VERSION;
        header->frame_type = frame->is_keyframe ? WS_FRAME_TYPE_H264_KEY : WS_FRAME_TYPE_H264_DELTA;
        header->reserved = 0;
        header->timestamp = WS_TO_NETWORK_64(frame->timestamp);
        header->frame_size = WS_TO_NETWORK_32(frame->size);
        header->stream_id = WS_TO_NETWORK_32(g_websocket_server.current_stream_id);
        header->sequence = WS_TO_NETWORK_32(g_websocket_server.frame_sequence);
        header->width = WS_TO_NETWORK_32(frame->width);
        header->height = WS_TO_NETWORK_32(frame->height);
        header->format = 0;
        header->flags = 0;
        header->coding_type = 0;
        header->stream_size = 0;
        header->num_nalus = 0;
        header->mse_mul256 = 0;
        header->header_size = WS_TO_NETWORK_32(sizeof(websocket_frame_header_t));
        
        send_size = sizeof(websocket_frame_header_t) + frame->size;
    } else {
        // Fallback: send raw H.264 data without header
        send_data = frame->data;
        send_size = frame->size;
    }
    
    // Debug: check H.264 data validity
    uint8_t *h264_data = send_data + sizeof(websocket_frame_header_t);
    if (send_size > sizeof(websocket_frame_header_t) + 4) {
        // Check NAL type (first byte after start code)
        if (h264_data[0] == 0 && h264_data[1] == 0 && h264_data[2] == 0 && h264_data[3] == 1) {
            uint8_t nal_type = h264_data[4] & 0x1F;
            if (nal_type == 0 || nal_type > 23) {
                LOG_SVC_WARN("WS: Invalid NAL type=%d, frame_size=%lu, keyframe=%d",
                             nal_type, (unsigned long)frame->size, frame->is_keyframe);
            }
        }
    }
    
    ws_stream_broadcast_packet(send_data, send_size);
    
    // Update statistics
    g_websocket_server.stats.total_frames_sent++;
    g_websocket_server.stats.total_bytes_sent += send_size;
    g_websocket_server.stream_frame_counter++;
    g_websocket_server.frame_sequence++;
    
    uint64_t current_time_ms = get_relative_timestamp();
    uint64_t stream_duration_ms = current_time_ms - g_websocket_server.stream_start_time_ms;
    if (stream_duration_ms > 1000) {
        g_websocket_server.stats.stream_fps = 
            (uint32_t)(g_websocket_server.stream_frame_counter * 1000 / stream_duration_ms);
    }
    
    osMutexRelease(g_websocket_server.mutex);
    
    return AICAM_OK;
}

/**
 * @brief Hub SPS/PPS callback
 */
static void ws_stream_on_hub_sps_pps(const video_hub_sps_pps_t *sps_pps, void *user_data)
{
    (void)user_data;
    
    if (!sps_pps || !sps_pps->sps_data || !sps_pps->pps_data) {
        return;
    }
    
    LOG_SVC_INFO("WebSocket: SPS/PPS received (SPS=%u, PPS=%u bytes)",
                 sps_pps->sps_size, sps_pps->pps_size);
}

/**
 * @brief Subscribe to Video Hub to receive encoded frames
 */
aicam_result_t websocket_stream_server_subscribe_hub(void)
{
    if (!g_websocket_server.is_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (g_websocket_server.hub_subscriber_id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        LOG_SVC_WARN("Already subscribed to Video Hub");
        return AICAM_OK;
    }
    
    if (!video_hub_is_initialized()) {
        LOG_SVC_ERROR("Video Hub not initialized");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_websocket_server.hub_subscriber_id = video_hub_subscribe(
        VIDEO_HUB_SUBSCRIBER_WEBSOCKET,
        ws_stream_on_hub_frame,
        ws_stream_on_hub_sps_pps,
        NULL
    );
    
    if (g_websocket_server.hub_subscriber_id == VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        LOG_SVC_ERROR("Failed to subscribe to Video Hub");
        return AICAM_ERROR;
    }
    
    g_websocket_server.hub_mode_enabled = AICAM_TRUE;
    LOG_SVC_INFO("WebSocket subscribed to Video Hub, subscriber_id=%ld",
                 (long)g_websocket_server.hub_subscriber_id);
    
    return AICAM_OK;
}

/**
 * @brief unsubscribe from Video Hub (Video Hub mode)
 */
aicam_result_t websocket_stream_server_unsubscribe_hub(void)
{
    if (g_websocket_server.hub_subscriber_id == VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        return AICAM_OK;
    }
    
    video_hub_unsubscribe(g_websocket_server.hub_subscriber_id);
    g_websocket_server.hub_subscriber_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    g_websocket_server.hub_mode_enabled = AICAM_FALSE;
    
    LOG_SVC_INFO("WebSocket unsubscribed from Video Hub");
    return AICAM_OK;
}

/* ==================== WebSocket Status Command ==================== */

/**
 * @brief Display WebSocket stream server status
 */
static void websocket_stream_display_status(void)
{
    if (!g_websocket_server.is_initialized) {
        printf("WebSocket server not initialized\r\n");
        return;
    }
    
    osMutexAcquire(g_websocket_server.mutex, osWaitForever);
    
    websocket_stream_stats_t stats;
    memcpy(&stats, &g_websocket_server.stats, sizeof(websocket_stream_stats_t));
    stats.active_clients = g_websocket_server.client_count;
    stats.uptime_ms = get_relative_timestamp() - g_websocket_server.start_time_ms;
    
    printf("\r\n========== WEBSOCKET STREAM SERVER STATUS ==========\r\n");
    printf("Server Status: %s\r\n", g_websocket_server.is_running ? "RUNNING" : "STOPPED");
    printf("Initialized: %s\r\n", g_websocket_server.is_initialized ? "YES" : "NO");
    printf("Port: %u\r\n", g_websocket_server.config.port);
    printf("Path: %s\r\n", g_websocket_server.config.stream_path);
    printf("Max Clients: %lu\r\n", (unsigned long)g_websocket_server.config.max_clients);
    printf("Max Frame Size: %lu bytes\r\n", (unsigned long)g_websocket_server.config.max_frame_size);
    printf("Ping Interval: %lu ms\r\n", (unsigned long)g_websocket_server.config.ping_interval_ms);
    printf("Pong Timeout: %lu ms\r\n", (unsigned long)g_websocket_server.config.pong_timeout_ms);
    printf("\r\n");
    
    printf("--- Statistics ---\r\n");
    printf("  Uptime: %lu ms (%.2f hours)\r\n", 
           (unsigned long)stats.uptime_ms,
           stats.uptime_ms / 3600000.0f);
    printf("  Total Connections: %lu\r\n", (unsigned long)stats.total_connections);
    printf("  Total Disconnections: %lu\r\n", (unsigned long)stats.total_disconnections);
    printf("  Active Clients: %lu\r\n", (unsigned long)stats.active_clients);
    printf("  Total Frames Sent: %lu\r\n", (unsigned long)stats.total_frames_sent);
    printf("  Total Bytes Sent: %lu bytes (%.2f MB)\r\n", 
           (unsigned long)stats.total_bytes_sent,
           stats.total_bytes_sent / (1024.0f * 1024.0f));
    printf("  Error Count: %lu\r\n", (unsigned long)stats.error_count);
    printf("\r\n");
    
    printf("--- Stream Status ---\r\n");
    printf("  Stream Active: %s\r\n", stats.stream_active ? "YES" : "NO");
    if (stats.stream_active) {
        printf("  Stream ID: %lu\r\n", (unsigned long)stats.stream_id);
        printf("  Stream FPS: %lu\r\n", (unsigned long)stats.stream_fps);
        if (g_websocket_server.stream_start_time_ms > 0) {
            uint64_t stream_duration_ms = get_relative_timestamp() - g_websocket_server.stream_start_time_ms;
            printf("  Stream Duration: %lu ms (%.2f minutes)\r\n",
                   (unsigned long)stream_duration_ms,
                   stream_duration_ms / 60000.0f);
            printf("  Stream Frames: %lu\r\n", (unsigned long)g_websocket_server.stream_frame_counter);
        }
    }
    printf("\r\n");
    
    if (stats.active_clients > 0) {
        printf("--- Active Clients ---\r\n");
        for (uint32_t i = 0; i < g_websocket_server.config.max_clients; i++) {
            if (g_websocket_server.clients[i].is_active) {
                websocket_client_t *client = &g_websocket_server.clients[i];
                uint64_t connect_duration_ms = get_relative_timestamp() - client->connect_time_ms;
                uint64_t time_since_last_pong = get_relative_timestamp() - client->last_pong_time_ms;
                
                printf("  [%lu] Client ID: %lu, IP: %s\r\n",
                       (unsigned long)i,
                       (unsigned long)client->client_id,
                       client->client_ip);
                printf("      Connected: %lu ms ago\r\n", (unsigned long)connect_duration_ms);
                printf("      Connection Status: %s\r\n",
                       client->conn && !client->conn->is_closing ? "ACTIVE" : "CLOSING");
                printf("      Ping Pending: %s\r\n", client->ping_pending ? "YES" : "NO");
                if (g_websocket_server.config.ping_interval_ms > 0) {
                    printf("      Last Pong: %lu ms ago\r\n", (unsigned long)time_since_last_pong);
                }
                printf("\r\n");
            }
        }
    } else {
        printf("--- Active Clients ---\r\n");
        printf("  No active clients\r\n");
        printf("\r\n");
    }
    
    // Calculate average FPS if stream is active
    if (stats.stream_active && stats.total_frames_sent > 0 && stats.uptime_ms > 0) {
        float avg_fps = (stats.total_frames_sent * 1000.0f) / stats.uptime_ms;
        printf("--- Performance ---\r\n");
        printf("  Average FPS (since start): %.2f\r\n", avg_fps);
        if (stats.total_bytes_sent > 0 && stats.total_frames_sent > 0) {
            float avg_frame_size = (float)stats.total_bytes_sent / stats.total_frames_sent;
            printf("  Average Frame Size: %.2f bytes\r\n", avg_frame_size);
            float avg_bandwidth_mbps = (avg_fps * avg_frame_size * 8.0f) / (1024.0f * 1024.0f);
            printf("  Average Bandwidth: %.2f Mbps\r\n", avg_bandwidth_mbps);
        }
        printf("\r\n");
    }
    
    osMutexRelease(g_websocket_server.mutex);
    printf("==================================================\r\n\r\n");
}

/**
 * @brief Command handler for WebSocket stream server status
 */
static int websocket_stream_status_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    websocket_stream_display_status();
    return 0;
}

/**
 * @brief Register WebSocket stream server commands
 */
void websocket_stream_server_register_commands(void)
{
    static const debug_cmd_reg_t websocket_stream_cmd_table[] = {
        {"wsstatus", "Display WebSocket stream server status", websocket_stream_status_cmd},
    };
    
    debug_register_commands(websocket_stream_cmd_table, 
                            sizeof(websocket_stream_cmd_table) / sizeof(websocket_stream_cmd_table[0]));
}
