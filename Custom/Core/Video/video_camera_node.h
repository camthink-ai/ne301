/**
 * @file video_camera_node.h
 * @brief Camera Source Node
 * @details Simple camera node based on driver_test.c device interaction with zero-copy
 */

#ifndef VIDEO_CAMERA_NODE_H
#define VIDEO_CAMERA_NODE_H

#include "video_pipeline.h"
#include "aicam_types.h"
#include "dev_manager.h"
#include "camera.h"
#include "video_frame_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Camera Node Configuration ==================== */

/**
 * @brief Camera node configuration
 */
typedef struct {
    uint32_t width;                       // Camera width
    uint32_t height;                      // Camera height
    uint32_t fps;                         // Frame rate
    uint32_t format;                      // Pixel format
    uint32_t bpp;                         // Bytes per pixel
    uint32_t ai_enabled;                  // AI processing enabled
    uint32_t overlay_results;             // Draw AI results onto captured frames
} video_camera_config_t;

/**
 * @brief Camera node statistics
 */
typedef struct {
    uint64_t frames_captured;             // Total frames captured
    uint64_t capture_errors;              // Capture errors
    uint64_t buffer_underruns;            // Buffer underrun count
    uint64_t avg_capture_time_us;         // Average capture time
    uint64_t max_capture_time_us;         // Maximum capture time
} video_camera_stats_t;

/**
 * @brief AI result callback function type
 * @param frame_buffer Frame buffer to draw on
 * @param width Frame width
 * @param height Frame height
 * @param user_data User data passed to callback
 * @return Operation result
 */
typedef aicam_result_t (*ai_draw_callback_t)(uint8_t *frame_buffer, 
                                           uint32_t width, 
                                           uint32_t height, 
                                           uint32_t frame_id,
                                           void *user_data);

/**
 * @brief Camera node private data
 */
typedef struct {
    device_t *camera_dev;                 // Camera device handle
    video_camera_config_t config;         // Camera configuration
    video_camera_stats_t stats;           // Camera statistics
    sensor_params_t sensor_param;         // Sensor parameters
    pipe_params_t pipe_param;             // Pipeline parameters
    uint8_t *current_buffer;              // Current frame buffer
    uint32_t frame_id;                    // Frame ID
    uint32_t frame_sequence;              // Frame sequence counter
    aicam_bool_t is_initialized;          // Initialization status
    aicam_bool_t is_running;              // Running status
    
    // AI integration callback
    ai_draw_callback_t ai_draw_callback;  // AI drawing callback function
    void *ai_callback_user_data;          // User data for AI callback
} video_camera_node_data_t;

/* ==================== API Functions ==================== */

/**
 * @brief Create camera node
 * @param name Node name
 * @param config Camera configuration
 * @return Camera node handle or NULL on error
 */
video_node_t* video_camera_node_create(const char *name, const video_camera_config_t *config);

/**
 * @brief Get default camera configuration
 * @param config Configuration structure to fill
 */
void video_camera_get_default_config(video_camera_config_t *config);

/**
 * @brief Set camera parameters
 * @param node Camera node handle
 * @param config New configuration
 * @return Operation result
 */
aicam_result_t video_camera_node_set_config(video_node_t *node, const video_camera_config_t *config);

/**
 * @brief Get camera parameters
 * @param node Camera node handle
 * @param config Configuration structure to fill
 * @return Operation result
 */
aicam_result_t video_camera_node_get_config(video_node_t *node, video_camera_config_t *config);

/**
 * @brief Get camera statistics
 * @param node Camera node handle
 * @param stats Statistics structure to fill
 * @return Operation result
 */
aicam_result_t video_camera_node_get_stats(video_node_t *node, video_camera_stats_t *stats);

/**
 * @brief Reset camera statistics
 * @param node Camera node handle
 * @return Operation result
 */
aicam_result_t video_camera_node_reset_stats(video_node_t *node);

/**
 * @brief Start camera capture
 * @param node Camera node handle
 * @return Operation result
 */
aicam_result_t video_camera_node_start(video_node_t *node);

/**
 * @brief Stop camera capture
 * @param node Camera node handle
 * @return Operation result
 */
aicam_result_t video_camera_node_stop(video_node_t *node);

/**
 * @brief Check if camera is running
 * @param node Camera node handle
 * @return Running status
 */
aicam_bool_t video_camera_node_is_running(video_node_t *node);

/**
 * @brief Set AI drawing callback
 * @param node Camera node handle
 * @param callback AI drawing callback function
 * @param user_data User data to pass to callback
 * @return Operation result
 */
aicam_result_t video_camera_node_set_ai_callback(video_node_t *node, 
                                                ai_draw_callback_t callback, 
                                                void *user_data);

/* ==================== Control Commands ==================== */

#define CAMERA_CMD_START_CAPTURE         0x1001
#define CAMERA_CMD_STOP_CAPTURE          0x1002
#define CAMERA_CMD_SET_RESOLUTION        0x1003
#define CAMERA_CMD_SET_FPS               0x1004
#define CAMERA_CMD_GET_SENSOR_INFO       0x1005

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_CAMERA_NODE_H */
