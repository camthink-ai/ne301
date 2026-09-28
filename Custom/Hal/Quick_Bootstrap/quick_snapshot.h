#ifndef __QUICK_SNAPSHOT_H__
#define __QUICK_SNAPSHOT_H__

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "quick_storage.h"
#include "nn.h"

/* event flags */
#define QS_FLAG_CFG_READY           (1UL << 0)
#define QS_FLAG_FRAME_READY         (1UL << 1)
#define QS_FLAG_JPEG_READY          (1UL << 2)
#define QS_FLAG_AI_INFO_READY       (1UL << 3)
#define QS_FLAG_AI_FRAME_READY      (1UL << 4)
#define QS_FLAG_AI_RESULT_READY     (1UL << 5)
#define QS_FLAG_AI_JPEG_READY       (1UL << 6)
#define QS_FLAG_AI_ERROR_ABORT      (1UL << 23)
#define QS_FLAG_ERROR_ABORT         (1UL << 24)

#define QS_WAIT_EVENT_TIMEOUT_MS    (3000U)

/* light mode values (aligned with `light_mode_t` in json_config_mgr.h) */
#define QS_LIGHT_MODE_OFF           (0U)
#define QS_LIGHT_MODE_ON            (1U)
#define QS_LIGHT_MODE_AUTO          (2U)
#define QS_LIGHT_MODE_CUSTOM        (3U)

/**
 * @brief Initialize quick snapshot
 * @return 0 on success, other values on error
 */
int quick_snapshot_init(void);

/**
 * @brief Check if quick snapshot is initialized
 * @return AICAM_TRUE if initialized, AICAM_FALSE otherwise
 */
int quick_snapshot_is_init(void);

/**
 * @brief UVC capture buffer identity (the camera JPEG handed out as the
 *        capture result on the UVC wake path)
 * @return Buffer base pointer, or NULL when quick snapshot is not inited
 */
const uint8_t *quick_snapshot_uvc_cap_buf(void);

/**
 * @brief Wait for snapshot config
 * @param snapshot_config Snapshot config
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_config(qs_snapshot_config_t *snapshot_config);

/**
 * @brief Wait for capture frame
 * @param main_fb Main frame
 * @param main_fb_size Main frame size
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_capture_frame(uint8_t **main_fb, size_t *main_fb_size);

/**
 * @brief Wait for capture JPEG
 * @param jpeg_data JPEG data
 * @param jpeg_size JPEG size
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_capture_jpeg(uint8_t **jpeg_data, size_t *jpeg_size);

/**
 * @brief Wait for AI info
 * @param model_info Model info
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_ai_info(nn_model_info_t *model_info);

/**
 * @brief Wait for AI result
 * @param ai_result AI result
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_ai_result(nn_result_t *ai_result);

/**
 * @brief Get AI inference time (ms) for the current snapshot.
 *        The value is published together with QS_FLAG_AI_RESULT_READY.
 * @param inference_time_ms Inference time in milliseconds
 * @return 0 on success, other values on error
 */
int quick_snapshot_get_ai_inference_time_ms(uint32_t *inference_time_ms);

/**
 * @brief Wait for AI JPEG
 * @param ai_jpeg_data AI JPEG data
 * @param ai_jpeg_size AI JPEG size
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_ai_jpeg(uint8_t **ai_jpeg_data, size_t *ai_jpeg_size);

/**
 * @brief Get capture frame id (pipe1 frame id)
 * @param frame_id Frame ID
 * @return 0 on success, other values on error
 */
int quick_snapshot_get_frame_id(uint32_t *frame_id);

/**
 * @brief Wait for event
 * @param event_mask Event mask
 * @return Event flags
 */
uint32_t quick_snapshot_wait_event(uint32_t event_mask, uint32_t timeout);

/**
 * @brief Clear event
 * @param event_mask Event mask
 */
void quick_snapshot_clear_event(uint32_t event_mask);

#ifdef __cplusplus
}
#endif

#endif /* __QUICK_SNAPSHOT_H__ */
