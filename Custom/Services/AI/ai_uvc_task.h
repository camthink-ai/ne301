/**
 * @file    ai_uvc_task.h
 * @brief   Continuous AI inference for the UVC (MJPEG) image source.
 */
#ifndef AI_UVC_TASK_H
#define AI_UVC_TASK_H

/* Start the continuous inference thread (no-op returns <0 when the active
 * image source is not UVC or the thread is already running). Results are
 * pushed to WebSocket clients as WS_FRAME_TYPE_METADATA JSON frames; the
 * frontend draws the overlay, the MJPEG preview stream itself is untouched. */
int ai_uvc_task_start(void);

int ai_uvc_task_is_running(void);

#endif /* AI_UVC_TASK_H */
