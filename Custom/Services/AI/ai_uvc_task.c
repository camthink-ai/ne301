/**
 * @file    ai_uvc_task.c
 * @brief   Continuous AI inference for the UVC (MJPEG) image source.
 *
 * Decoupled by design from the MJPEG preview stream: this task keeps its own
 * frame copy (snapshot from the engine's published-frame store) and its own
 * thread, so decode/inference latency never back-pressures the preview.
 * Frames: latest MJPEG -> jpegc decode -> YCbCr->RGB888 -> bilinear resize ->
 * NPU -> JSON -> WebSocket (WS_FRAME_TYPE_METADATA). jpegc is shared with
 * capture/AI-overlay paths through its device mutex (captures may briefly
 * delay inference, never the preview).
 */
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "debug.h"
#include "nn.h"
#include "cJSON.h"
#include "json_config_mgr.h"
#include "mem_map.h"
#include "usbh_uvc_service.h"
#include "usbh_uvc_jpeg.h"
#include "ai_service.h"
#include "websocket_stream_server.h"
#include "ai_uvc_task.h"
#include "buffer_mgr.h"

#ifndef IN_PSRAM
#define IN_PSRAM __attribute__((section(".psram_bss")))
#endif
#ifndef ALIGN_32
#define ALIGN_32 __attribute__((aligned(32)))
#endif

#define AI_UVC_MIN_INTERVAL_MS 200u

static struct {
    osThreadId_t thread;
    volatile int run;
    uint64_t frames;
    uint64_t errors;
    uint8_t oversize_noted;
    uint32_t last_progress_tick;
} g_ai_uvc;

static uint8_t g_ai_uvc_stack[8192] ALIGN_32 IN_PSRAM;
static uint8_t g_ai_uvc_frame[512 * 1024] ALIGN_32 IN_PSRAM;

static void ai_uvc_thread(void *arg)
{
    nn_model_info_t model = {0};
    uint8_t *model_input = NULL;
    uint32_t interval_ms = json_config_get_inference_interval_ms();
    if (interval_ms < AI_UVC_MIN_INTERVAL_MS) {
        interval_ms = AI_UVC_MIN_INTERVAL_MS;
    }

    (void)arg;
    LOG_SVC_INFO("ai_uvc: thread up (interval %lums)", (unsigned long)interval_ms);
    g_ai_uvc.last_progress_tick = osKernelGetTickCount();

    while (g_ai_uvc.run) {
        uvc_svc_status_t st;
        usbh_uvc_service_get_status(&st);
        if (!st.streaming) {
            g_ai_uvc.last_progress_tick = osKernelGetTickCount(); /* idle is fine */
            osDelay(500);
            continue;
        }

        /* one-per-second visibility into whichever continue path is taken */
        static uint32_t diag_tick;
        uint32_t now_tick = osKernelGetTickCount();
        int diag = ((now_tick - diag_tick) >= 1000u);
        if (diag) {
            diag_tick = now_tick;
        }

        /* stall watchdog: streaming but no completed inference for 30s
         * means something wedged (raster lock / NPU) - make it visible
         * instead of silently idling. osKernelGetTickCount is in TICKS
         * (ThreadX: 10ms), convert to ms for the threshold and the log. */
        {
            uint32_t since = osKernelGetTickCount() - g_ai_uvc.last_progress_tick;
            uint32_t since_ms = (uint32_t)(((uint64_t)since * 1000u) /
                                           (osKernelGetTickFreq() ? osKernelGetTickFreq() : 1000u));
            if (since_ms > 30000u) {
                LOG_SVC_ERROR("ai_uvc: no inference for %lums (errors=%u)",
                              (unsigned long)since_ms,
                              (unsigned)g_ai_uvc.errors);
                g_ai_uvc.last_progress_tick = osKernelGetTickCount();
            }
        }

        /* model load (once; reload after nn state loss is tolerated) */
        nn_state_t ns = nn_get_state();
        if (ns == NN_STATE_UNINIT || ns == NN_STATE_INIT) {
            uintptr_t model_ptr = json_config_get_ai_1_active() ? AI_2_BASE + 1024 : AI_1_BASE + 1024;
            if (nn_load_model(model_ptr) != 0) {
                LOG_SVC_ERROR("ai_uvc: model load failed");
                osDelay(5000);
                continue;
            }
        }
        if (nn_get_model_info(&model) != AICAM_OK || model.input_width == 0 || model.input_height == 0) {
            if (diag) {
                printf("[ai_uvc] model info invalid (w=%u h=%u)\r\n",
                       (unsigned)model.input_width, (unsigned)model.input_height);
            }
            osDelay(5000);
            continue;
        }

        /* snapshot the latest published frame into our own buffer */
        const uint8_t *p = NULL;
        uint32_t len = 0;
        uint16_t w = 0, h = 0;
        uint32_t seq = usbh_uvc_service_latest_frame(&p, &len, &w, &h);
        if (seq == 0 || len == 0 || len > sizeof(g_ai_uvc_frame)) {
            if (diag) {
                printf("[ai_uvc] frame invalid (seq=%lu len=%lu %ux%u)\r\n",
                       (unsigned long)seq, (unsigned long)len,
                       (unsigned)w, (unsigned)h);
            }
            osDelay(interval_ms);
            continue;
        }
        memcpy(g_ai_uvc_frame, p, len);
        /* EOI tail aid for the core's post-MCU window (uvc_jpeg_pad_tail):
         * this buffer is decode-only - never stored or uploaded - so the
         * padded length stays internal */
        len = uvc_jpeg_pad_tail(g_ai_uvc_frame, len, sizeof(g_ai_uvc_frame));

        /* Resource guard: above 1080P the decode path is skipped entirely
         * (the 4K raster + full-frame convert would exhaust the external
         * pool). No per-frame notification: the source-config page warns
         * in red when the selected resolution exceeds the budget. */
        if ((uint32_t)w * h > 1920u * 1080u) {
            if (!g_ai_uvc.oversize_noted) {
                g_ai_uvc.oversize_noted = 1;
                LOG_SVC_WARN("ai_uvc: %ux%u exceeds the 1080P decode budget, AI idle",
                             (unsigned)w, (unsigned)h);
            }
            osDelay(interval_ms);
            continue;
        }

        if (model_input == NULL) {
            model_input = buffer_malloc_aligned(
                (size_t)model.input_width * model.input_height * 3u, 32);
            if (model_input == NULL) {
                LOG_SVC_ERROR("ai_uvc: model input alloc failed");
                osDelay(5000);
                continue;
            }
        }

        if (ai_uvc_frame_to_model_input(g_ai_uvc_frame, len, model_input,
                                        model.input_width, model.input_height) != AICAM_OK) {
            g_ai_uvc.errors++;
            osDelay(interval_ms);
            continue;
        }

        nn_result_t result;
        memset(&result, 0, sizeof(result));
        (void)nn_set_confidence_threshold((float)json_config_get_confidence_threshold() / 100.0f);
        (void)nn_set_nms_threshold((float)json_config_get_nms_threshold() / 100.0f);
        if (nn_inference_frame(model_input,
                               model.input_width * model.input_height * 3u,
                               &result) != 0) {
            g_ai_uvc.errors++;
            osDelay(interval_ms);
            continue;
        }
        g_ai_uvc.frames++;
        g_ai_uvc.last_progress_tick = osKernelGetTickCount();

        /* JSON result -> WebSocket metadata frames (frontend draws overlay).
         * Skip when nobody is watching: the MJPEG preview page drives WS. */
        websocket_stream_stats_t ws_stats;
        if (websocket_stream_server_get_stats(&ws_stats) == AICAM_OK &&
            ws_stats.active_clients > 0) {
            cJSON *root = nn_create_ai_result_json(&result);
            if (root != NULL) {
                char *json = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                if (json != NULL) {
                    /* send_frame writes the 64-byte WSFS header INTO the
                     * caller's buffer start: the payload must live at
                     * +64 (single-threaded task -> static staging packet) */
                    static uint8_t pkt[64 + 4096];
                    size_t jl = strlen(json);
                    if (jl <= 4096) {
                        memcpy(pkt + 64, json, jl);
                        (void)websocket_stream_server_send_frame(pkt, 64 + jl,
                                                                 (uint64_t)osKernelGetTickCount() * 1000u,
                                                                 WS_FRAME_TYPE_METADATA, w, h);
                    }
                    cJSON_free(json);
                }
            }
        }

        osDelay(interval_ms);
    }

    if (model_input != NULL) {
        buffer_free(model_input);
    }
    LOG_SVC_INFO("ai_uvc: thread exit");
}

int ai_uvc_task_start(void)
{
    if (!usbh_uvc_service_is_active()) {
        return -1;
    }
    if (g_ai_uvc.thread != NULL) {
        return 0;
    }

    g_ai_uvc.run = 1;
    static const osThreadAttr_t attr = {
        .name = "ai_uvc",
        .stack_mem = g_ai_uvc_stack,
        .stack_size = sizeof(g_ai_uvc_stack),
        /* Below the streaming/preview band: inference must never preempt
         * the web sender or the USB worker. */
        .priority = osPriorityBelowNormal,
    };
    g_ai_uvc.thread = osThreadNew(ai_uvc_thread, NULL, &attr);
    if (g_ai_uvc.thread == NULL) {
        g_ai_uvc.run = 0;
        return -1;
    }
    return 0;
}

int ai_uvc_task_is_running(void)
{
    return (g_ai_uvc.thread != NULL && g_ai_uvc.run) ? 1 : 0;
}
