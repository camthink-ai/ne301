/**
 * @file video_pipeline.h
 * @brief Video Pipeline with Integrated Flow Manager
 * @details video pipeline with independent node processing and HAL integration
 */

#ifndef VIDEO_PIPELINE_ENHANCED_H
#define VIDEO_PIPELINE_ENHANCED_H

#include "aicam_types.h"
#include "buffer_mgr.h"
#include "dev_manager.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Configuration Constants ==================== */

#define VIDEO_PIPELINE_MAX_NODES        16      // Maximum nodes per pipeline
#define VIDEO_PIPELINE_MAX_CONNECTIONS  32      // Maximum connections between nodes
#define VIDEO_PIPELINE_NODE_NAME_LEN    32      // Node name length
#define VIDEO_PIPELINE_MAX_INPUTS       4       // Maximum inputs per node
#define VIDEO_PIPELINE_MAX_OUTPUTS      4       // Maximum outputs per node
#define VIDEO_FRAME_QUEUE_SIZE          8       // Maximum frames per node queue
#define VIDEO_THREAD_STACK_SIZE         8192    // Thread stack size
#define VIDEO_THREAD_PRIORITY           32      // osPriorityAboveNormal (TX prio 56-32=24); must outrank web/stream tasks

/* ==================== Video Frame Definitions ==================== */

/**
 * @brief Video frame format types
 */
typedef enum {
    VIDEO_FORMAT_UNKNOWN = 0,
    VIDEO_FORMAT_RGB888,
    VIDEO_FORMAT_RGB565,
    VIDEO_FORMAT_YUV420,
    VIDEO_FORMAT_YUV422,
    VIDEO_FORMAT_NV12,
    VIDEO_FORMAT_NV21,
    VIDEO_FORMAT_MJPEG,
    VIDEO_FORMAT_H264,
    VIDEO_FORMAT_H265
} video_format_t;

/**
 * @brief Video frame information
 */
typedef struct {
    uint32_t width;                     // Frame width
    uint32_t height;                    // Frame height
    video_format_t format;              // Pixel format
    uint32_t stride;                    // Bytes per line
    uint32_t size;                      // Total frame size
    uint64_t timestamp;                 // Frame timestamp (ms)
    uint32_t sequence;                  // Frame sequence number
} video_frame_info_t;

/**
 * @brief Video frame data structure
 */
typedef struct {
    video_frame_info_t info;            // Frame information
    uint8_t *data;                      // Frame data pointer
    uint32_t ref_count;                 // Reference count for zero-copy
    void *private_data;                 // Private data for extensions
    aicam_bool_t is_key_frame;          // Key frame flag
    uint32_t quality;                   // Quality parameter (0-100)
} video_frame_t;

/* ==================== Node Type Definitions ==================== */

/**
 * @brief Video pipeline node types
 */
typedef enum {
    VIDEO_NODE_TYPE_UNKNOWN = 0,
    VIDEO_NODE_TYPE_SOURCE,             // Video source (camera, file, network)
    VIDEO_NODE_TYPE_SINK,               // Video sink (display, file, network)
    VIDEO_NODE_TYPE_FILTER,             // Video filter (resize, format convert)
    VIDEO_NODE_TYPE_ENCODER,            // Video encoder (H264, H265, JPEG)
    VIDEO_NODE_TYPE_DECODER,            // Video decoder
    VIDEO_NODE_TYPE_ANALYZER,           // Video analyzer (AI, motion detection)
    VIDEO_NODE_TYPE_MIXER,              // Video mixer (overlay, blend)
    VIDEO_NODE_TYPE_SPLITTER,           // Video splitter (duplicate stream)
    VIDEO_NODE_TYPE_CUSTOM              // Custom processing node
} video_node_type_t;

/**
 * @brief Node processing states
 */
typedef enum {
    VIDEO_NODE_STATE_IDLE = 0,
    VIDEO_NODE_STATE_READY,
    VIDEO_NODE_STATE_RUNNING,
    VIDEO_NODE_STATE_PAUSED,
    VIDEO_NODE_STATE_STOPPING,
    VIDEO_NODE_STATE_ERROR
} video_node_state_t;

/**
 * @brief Node execution states for flow control
 */
typedef enum {
    NODE_EXEC_IDLE = 0,                 // Node is idle
    NODE_EXEC_WAITING,                  // Waiting for input data
    NODE_EXEC_PROCESSING,               // Processing data
    NODE_EXEC_BLOCKED,                  // Blocked (output queue full)
    NODE_EXEC_ERROR                     // Error state
} node_exec_state_t;

/**
 * @brief Flow control modes
 */
typedef enum {
    FLOW_MODE_PUSH = 0,                 // Push mode (source drives)
    FLOW_MODE_PULL,                     // Pull mode (sink drives)
    FLOW_MODE_HYBRID                    // Hybrid mode (adaptive)
} flow_mode_t;

/* ==================== Forward Declarations ==================== */
typedef struct video_node_s video_node_t;
typedef struct video_pipeline_s video_pipeline_t;

/* ==================== Callback Function Types ==================== */

/**
 * @brief Node initialization callback
 */
typedef aicam_result_t (*video_node_init_callback_t)(video_node_t *node);

/**
 * @brief Node deinitialization callback
 */
typedef aicam_result_t (*video_node_deinit_callback_t)(video_node_t *node);

/**
 * @brief Node processing callback
 */
typedef aicam_result_t (*video_node_process_callback_t)(video_node_t *node,
                                                       video_frame_t **input_frames,
                                                       uint32_t input_count,
                                                       video_frame_t **output_frames,
                                                       uint32_t *output_count);

/**
 * @brief Node control callback
 */
typedef aicam_result_t (*video_node_control_callback_t)(video_node_t *node,
                                                       uint32_t cmd,
                                                       void *param);

/**
 * @brief Pipeline event callback
 */
typedef void (*video_pipeline_event_callback_t)(video_pipeline_t *pipeline,
                                               uint32_t event,
                                               void *data,
                                               void *user_data);

/* ==================== Frame Queue Structure ==================== */

/**
 * @brief Frame queue for node input/output
 */
typedef struct {
    video_frame_t *frames[VIDEO_FRAME_QUEUE_SIZE];  // Frame buffer array
    uint32_t head;                          // Queue head index
    uint32_t tail;                          // Queue tail index
    uint32_t count;                         // Current frame count
    uint32_t max_size;                      // Maximum queue size
    void *mutex;                            // Thread safety mutex
    void *not_empty_sem;                    // Not empty semaphore
    void *not_full_sem;                     // Not full semaphore
} video_frame_queue_t;

/* ==================== Node Statistics ==================== */

/**
 * @brief Node processing statistics
 */
typedef struct {
    uint64_t frames_processed;              // Total frames processed
    uint64_t frames_dropped;                // Frames dropped due to overflow
    uint64_t avg_processing_time_us;        // Average processing time
    uint64_t max_processing_time_us;        // Maximum processing time
    uint64_t queue_overflows;               // Queue overflow count
    uint32_t current_queue_depth;           // Current queue depth
    uint32_t max_queue_depth;               // Maximum queue depth reached
    node_exec_state_t current_state;        // Current execution state
} video_node_stats_t;

/* ==================== Node Callback Structure ==================== */

/**
 * @brief Node callback functions
 */
typedef struct {
    video_node_init_callback_t init;        // Initialization callback
    video_node_deinit_callback_t deinit;    // Deinitialization callback
    video_node_process_callback_t process;  // Processing callback
    video_node_control_callback_t control;  // Control callback
} video_node_callbacks_t;

/* ==================== Node Configuration ==================== */

/**
 * @brief Node configuration structure
 */
typedef struct {
    char name[VIDEO_PIPELINE_NODE_NAME_LEN];    // Node name
    video_node_type_t type;                     // Node type
    uint32_t max_input_count;                   // Maximum input frames
    uint32_t max_output_count;                  // Maximum output frames
    aicam_bool_t thread_safe;                   // Thread safety requirement
    video_node_callbacks_t callbacks;          // Node callbacks
    void *private_config;                       // Node-specific configuration
} video_node_config_t;

/* ==================== Node Structure ==================== */

/**
 * @brief Video pipeline node structure
 */
struct video_node_s {
    uint32_t node_id;                       // Unique node identifier
    video_pipeline_t *pipeline;             // Parent pipeline reference
    video_node_config_t config;             // Node configuration
    video_node_state_t state;               // Current node state
    
    // Flow control and threading
    void *thread_handle;                    // Processing thread handle
    aicam_bool_t thread_active;             // Thread active flag
    aicam_bool_t thread_exited;             // Thread exited flag
    uint32_t thread_priority;               // Thread priority
    flow_mode_t flow_mode;                  // Flow control mode
    aicam_bool_t auto_release_input;        // Auto release input frames
    aicam_bool_t zero_copy_mode;            // Zero-copy processing
    
    // Data queue 
    video_frame_queue_t output_queue;       // Output frame queue
    uint32_t max_output_queue_size;         // Maximum output queue size
    uint32_t processing_timeout_ms;         // Processing timeout
    
    // Statistics and monitoring
    video_node_stats_t stats;               // Node statistics
    uint64_t last_process_time;             // Last processing timestamp

    //stack_memory
    void *stack_memory;                     // Stack memory
    
    // Private data
    void *private_data;                     // Node private data
};

/* ==================== Connection Structure ==================== */

/**
 * @brief Video connection between nodes
 */
typedef struct {
    uint32_t connection_id;                 // Unique connection identifier
    video_node_t *source_node;             // Source node
    uint32_t source_port;                   // Source port ID
    video_node_t *sink_node;                // Sink node
    uint32_t sink_port;                     // Sink port ID
    video_format_t format;                  // Connection format
    aicam_bool_t is_active;                 // Connection status
    
    // Statistics
    uint64_t frames_transferred;            // Total frames transferred
    uint64_t bytes_transferred;             // Total bytes transferred
    uint32_t queue_overruns;                // Queue overrun count
} video_connection_t;

/* ==================== Pipeline Configuration ==================== */

/**
 * @brief Pipeline configuration
 */
typedef struct {
    char name[VIDEO_PIPELINE_NODE_NAME_LEN];    // Pipeline name
    uint32_t max_nodes;                         // Maximum nodes
    uint32_t max_connections;                   // Maximum connections
    flow_mode_t global_flow_mode;               // Global flow mode
    aicam_bool_t auto_start;                    // Auto start on creation
    video_pipeline_event_callback_t event_callback; // Pipeline event callback
    void *user_data;                            // User data for callbacks
} video_pipeline_config_t;

/**
 * @brief Pipeline states
 */
typedef enum {
    VIDEO_PIPELINE_STATE_IDLE = 0,
    VIDEO_PIPELINE_STATE_READY,
    VIDEO_PIPELINE_STATE_RUNNING,
    VIDEO_PIPELINE_STATE_PAUSED,
    VIDEO_PIPELINE_STATE_STOPPING,
    VIDEO_PIPELINE_STATE_ERROR
} video_pipeline_state_t;

/* ==================== Pipeline Structure ==================== */

/**
 * @brief  video pipeline structure
 */
struct video_pipeline_s {
    uint32_t pipeline_id;                       // Unique pipeline identifier
    video_pipeline_config_t config;             // Pipeline configuration
    video_pipeline_state_t state;               // Current pipeline state
    
    // Node management
    video_node_t *nodes[VIDEO_PIPELINE_MAX_NODES];         // Registered nodes
    uint32_t node_count;                        // Active node count
    uint32_t next_node_id;                      // Next node ID to assign
    
    // Connection management
    video_connection_t connections[VIDEO_PIPELINE_MAX_CONNECTIONS];  // Connections
    uint32_t connection_count;                  // Active connection count
    uint32_t next_connection_id;                // Next connection ID to assign
    
    // Flow control and processing
    aicam_bool_t is_running;                    // Pipeline running state
    uint64_t start_time;                        // Pipeline start time
    uint64_t total_processing_time;             // Total processing time
    
    // Statistics
    uint64_t total_frames_processed;            // Total pipeline throughput
    float current_fps;                          // Current FPS
    
    // Thread safety
    void *mutex;                                // Pipeline mutex
};

/* ==================== Configuration Macros ==================== */

#define VIDEO_NODE_CONFIG_INIT(node_name, node_type) \
    { \
        .name = node_name, \
        .type = node_type, \
        .max_input_count = 1, \
        .max_output_count = 1, \
        .thread_safe = AICAM_TRUE, \
        .callbacks = {0}, \
        .private_config = NULL \
    }

#define VIDEO_PIPELINE_CONFIG_INIT(pipeline_name) \
    { \
        .name = pipeline_name, \
        .max_nodes = VIDEO_PIPELINE_MAX_NODES, \
        .max_connections = VIDEO_PIPELINE_MAX_CONNECTIONS, \
        .global_flow_mode = FLOW_MODE_PUSH, \
        .auto_start = AICAM_FALSE, \
        .event_callback = NULL, \
        .user_data = NULL \
    }

/* ==================== Public API Functions ==================== */

/* System Management */
aicam_result_t video_pipeline_system_init(void);
aicam_result_t video_pipeline_system_deinit(void);

/* Pipeline Management */
aicam_result_t video_pipeline_create(const video_pipeline_config_t *config, video_pipeline_t **pipeline);
aicam_result_t video_pipeline_destroy(video_pipeline_t *pipeline);
aicam_result_t video_pipeline_start(video_pipeline_t *pipeline);
aicam_result_t video_pipeline_stop(video_pipeline_t *pipeline);


/* Node Management */
aicam_result_t video_pipeline_register_node(video_pipeline_t *pipeline,
                                           video_node_t *standalone_node,
                                           uint32_t *node_id);
aicam_result_t video_pipeline_unregister_node(video_pipeline_t *pipeline, video_node_t *node);
video_node_t* video_pipeline_find_node(video_pipeline_t *pipeline, const char *name);

/* Connection Management */
aicam_result_t video_pipeline_connect_nodes(video_pipeline_t *pipeline,
                                           uint32_t source_node_id, uint32_t source_port,
                                           uint32_t sink_node_id, uint32_t sink_port);
aicam_result_t video_pipeline_disconnect_nodes(video_pipeline_t *pipeline,
                                              uint32_t source_node_id, uint32_t source_port,
                                              uint32_t sink_node_id, uint32_t sink_port);

/* Node Control */
aicam_result_t video_node_control(video_node_t *node, uint32_t cmd, void *param);
aicam_result_t video_node_get_stats(video_node_t *node, video_node_stats_t *stats);
aicam_result_t video_node_reset_stats(video_node_t *node);

/* Node Creation and Management */
video_node_t* video_node_create(const char *name, video_node_type_t type);
aicam_result_t video_node_destroy(video_node_t *node);
aicam_result_t video_node_set_callbacks(video_node_t *node, const video_node_callbacks_t *callbacks);
aicam_result_t video_node_set_private_data(video_node_t *node, void *data);
void* video_node_get_private_data(video_node_t *node);
aicam_result_t video_node_set_config(video_node_t *node, const video_node_config_t *config);

/* Pipeline Statistics */
aicam_result_t video_pipeline_get_stats(video_pipeline_t *pipeline,
                                       float *total_fps,
                                       uint64_t *total_frames);

/* Frame Management */
aicam_result_t video_pipeline_push_frame(video_pipeline_t *pipeline,
                                        const char *node_name,
                                        video_frame_t *frame);
aicam_result_t video_pipeline_pull_frame(video_pipeline_t *pipeline,
                                        const char *node_name,
                                        video_frame_t **frame);

/* Node Processing Thread (Internal) */
void video_node_processing_thread(void *argument);

/* Event Types */
#define VIDEO_PIPELINE_EVENT_STARTED        0x1000
#define VIDEO_PIPELINE_EVENT_STOPPED        0x1001
#define VIDEO_PIPELINE_EVENT_PAUSED         0x1002
#define VIDEO_PIPELINE_EVENT_RESUMED        0x1003
#define VIDEO_PIPELINE_EVENT_ERROR          0x1004
#define VIDEO_PIPELINE_EVENT_NODE_ADDED     0x1005
#define VIDEO_PIPELINE_EVENT_NODE_REMOVED   0x1006
#define VIDEO_PIPELINE_EVENT_CONNECTED      0x1007
#define VIDEO_PIPELINE_EVENT_DISCONNECTED   0x1008

/**
 * @brief Register video pipeline debug commands
 */
void video_pipeline_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_PIPELINE_ENHANCED_H 
