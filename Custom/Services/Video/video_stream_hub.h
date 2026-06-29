/**
 * @file video_stream_hub.h
 * @brief Video Stream Hub - video stream distribution center
 * @details 
 *   Pure distribution mode: receive encoded frames and distribute to subscribers
 *   
 *   Architecture:
 *   video_encoder_node → inject_frame() → [Hub] → distribute
 *                                                  ├→ WebSocket
 *                                                  └→ RTMP
 */

#ifndef VIDEO_STREAM_HUB_H
#define VIDEO_STREAM_HUB_H

#include "aicam_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Constants ==================== */

#define VIDEO_HUB_MAX_SUBSCRIBERS   4
#define VIDEO_HUB_VERSION          "2.0.0"

/* ==================== Type Definitions ==================== */

/**
 * @brief H.264 encoded frame information
 */
typedef struct {
    uint8_t *data;              ///< frame data pointer
    uint32_t size;              ///< frame size
    uint64_t timestamp;         ///< timestamp
    aicam_bool_t is_keyframe;   ///< is keyframe
    uint32_t frame_id;          ///< frame id
    uint32_t width;             ///< width
    uint32_t height;            ///< height
    uint32_t header_offset;     ///< reserved header space for WebSocket header etc.
} video_hub_frame_t;

/**
 * @brief SPS/PPS information
 */
typedef struct {
    uint8_t *sps_data;
    uint32_t sps_size;
    uint8_t *pps_data;
    uint32_t pps_size;
} video_hub_sps_pps_t;

/**
 * @brief frame callback function
 */
typedef aicam_result_t (*video_hub_frame_callback_t)(const video_hub_frame_t *frame, void *user_data);

/**
 * @brief SPS/PPS callback function
 */
typedef void (*video_hub_sps_pps_callback_t)(const video_hub_sps_pps_t *sps_pps, void *user_data);

/**
 * @brief subscriber ID
 */
typedef int32_t video_hub_subscriber_id_t;
#define VIDEO_HUB_INVALID_SUBSCRIBER_ID  (-1)

/**
 * @brief subscriber type
 */
typedef enum {
    VIDEO_HUB_SUBSCRIBER_WEBSOCKET = 0,
    VIDEO_HUB_SUBSCRIBER_RTMP,
    VIDEO_HUB_SUBSCRIBER_RTSP,
    VIDEO_HUB_SUBSCRIBER_RECORD,
    VIDEO_HUB_SUBSCRIBER_CUSTOM
} video_hub_subscriber_type_t;

/* ==================== Core API ==================== */

/**
 * @brief initialize Hub
 */
aicam_result_t video_hub_init(void);

/**
 * @brief deinitialize Hub
 */
aicam_result_t video_hub_deinit(void);

/**
 * @brief subscribe to video stream
 */
video_hub_subscriber_id_t video_hub_subscribe(
    video_hub_subscriber_type_t type,
    video_hub_frame_callback_t frame_callback,
    video_hub_sps_pps_callback_t sps_pps_callback,
    void *user_data
);

/**
 * @brief unsubscribe
 */
aicam_result_t video_hub_unsubscribe(video_hub_subscriber_id_t subscriber_id);

/**
 * @brief inject encoded frame
 * @param data frame data pointer (指向实际H.264数据)
 * @param size frame size
 * @param timestamp timestamp
 * @param is_keyframe is keyframe
 * @param header_offset reserved header space for WebSocket header etc.
 * @param width width
 * @param height height
 */
aicam_result_t video_hub_inject_frame(const uint8_t *data, uint32_t size, 
                                       uint64_t timestamp, aicam_bool_t is_keyframe,
                                       uint32_t header_offset,
                                       uint32_t width, uint32_t height);

/**
 * @brief inject SPS/PPS
 */
aicam_result_t video_hub_inject_sps_pps(const uint8_t *sps, uint32_t sps_size,
                                         const uint8_t *pps, uint32_t pps_size);

/**
 * @brief get SPS/PPS
 */
aicam_result_t video_hub_get_sps_pps(video_hub_sps_pps_t *sps_pps);

/**
 * @brief Invalidate cached SPS/PPS and reset per-subscriber send flags
 * @details Call when the encoder session is torn down (e.g. pipeline stop /
 *          model reload) so the next keyframe re-extracts fresh SPS/PPS and
 *          re-delivers them to subscribers. Stale SPS/PPS causes the MSE
 *          decoder to reject the new stream (black preview).
 */
aicam_result_t video_hub_invalidate_sps_pps(void);

/* ==================== Status API ==================== */

aicam_bool_t video_hub_is_initialized(void);
aicam_bool_t video_hub_has_subscribers(void);
uint32_t video_hub_get_subscriber_count(void);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_STREAM_HUB_H */

