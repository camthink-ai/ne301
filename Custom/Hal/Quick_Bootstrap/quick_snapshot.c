#include "quick_snapshot.h"
#include "quick_bootstrap.h"
#include "quick_storage.h"
#include "quick_trace.h"
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "camera.h"
#include "jpegc.h"
#include "draw.h"
#include "ai_draw.h"
#include "misc.h"
#include "npu_cache.h"
#include "mem_map.h"
#include "common_utils.h"
#include "pwr.h"
#include "sd_file.h"
#include "drtc.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*
 * quick_snapshot architecture (aligned with quick_network):
 * - init() only creates threads/sync objects (no NVS access here)
 * - snapshot thread reads qs_snapshot_config_t from quick_storage and runs capture/JPEG
 * - optional AI thread loads model, exposes nn_model_info_t, waits for inference frame
 * - wait APIs are multi-consumer safe: event flags are waited with osFlagsNoClear
 */

static osEventFlagsId_t s_evt = NULL;
static osThreadId_t s_snap_tid = NULL;
static osThreadId_t s_ai_tid = NULL;
static aicam_bool_t s_inited = AICAM_FALSE;

static qs_snapshot_config_t s_cfg = {0};

/* capture outputs (owned by quick_snapshot unless explicitly unshared) */
static uint8_t *s_main_fb = NULL;
static size_t s_main_fb_size = 0;
static uint32_t s_frame_id = 0;

static uint8_t *s_jpeg_data = NULL; /* unshared buffer (must be freed via JPEGC_CMD_FREE_ENC_BUFFER by caller) */
static size_t s_jpeg_size = 0;

static uint8_t *s_ai_jpeg_data = NULL; /* unshared buffer (must be freed via JPEGC_CMD_FREE_ENC_BUFFER by caller) */
static size_t s_ai_jpeg_size = 0;

static nn_model_info_t s_model_info = {0};
static nn_result_t s_ai_result = {0};
static uint32_t s_ai_inference_time_ms = 0;

/* AI input frame (pipe2 buffer), ownership is transferred to AI thread once flagged */
static uint8_t *s_ai_fb = NULL;
static size_t s_ai_fb_size = 0;

static device_t *s_cam_dev = NULL;
static device_t *s_jpeg_dev = NULL;
static device_t *s_light_dev = NULL;
static device_t *s_draw_dev = NULL;

/* Keep OD/MPE draw contexts across frames to avoid per-frame init/deinit.
 * This is intentionally "service-init free" for quick bootstrap paths:
 * - no font setup (text labels may be disabled by underlying draw impl)
 * - only uses software draw onto RGB565 FB
 * The visual parameters are aligned with ai_draw_service defaults. */
static aicam_bool_t s_ai_draw_inited = AICAM_FALSE;
static uint32_t s_ai_draw_w = 0, s_ai_draw_h = 0;
static od_draw_conf_t s_od_conf = {0};
static mpe_draw_conf_t s_mpe_conf = {0};

static void qs_ai_draw_deinit(void)
{
    if (!s_ai_draw_inited) return;
    (void)mpe_draw_deinit(&s_mpe_conf);
    (void)od_draw_deinit(&s_od_conf);
    memset(&s_od_conf, 0, sizeof(s_od_conf));
    memset(&s_mpe_conf, 0, sizeof(s_mpe_conf));
    s_ai_draw_inited = AICAM_FALSE;
    s_ai_draw_w = 0;
    s_ai_draw_h = 0;
}

static void qs_ai_draw_init_if_needed(uint32_t w, uint32_t h)
{
    if (w == 0U || h == 0U) return;
    if (s_ai_draw_inited && s_ai_draw_w == w && s_ai_draw_h == h) return;

    /* Re-init when resolution changes. */
    qs_ai_draw_deinit();

    /* Align with ai_draw_get_default_config():
     * - OD: red, line_width=2
     * - MPE: blue, line_width=2, box_line_width=2, dot_width=4
     */
    s_od_conf.p_dst = NULL;
    s_od_conf.image_width = w;
    s_od_conf.image_height = h;
    s_od_conf.line_width = 2;
    s_od_conf.color = COLOR_RED;
    (void)od_draw_init(&s_od_conf);

    s_mpe_conf.p_dst = NULL;
    s_mpe_conf.image_width = w;
    s_mpe_conf.image_height = h;
    s_mpe_conf.line_width = 2;
    s_mpe_conf.box_line_width = 2;
    s_mpe_conf.dot_width = 4;
    s_mpe_conf.color = COLOR_BLUE;
    (void)mpe_draw_init(&s_mpe_conf);

    s_ai_draw_inited = AICAM_TRUE;
    s_ai_draw_w = w;
    s_ai_draw_h = h;
}

static void qs_snapshot_thread(void *argument);
static void qs_ai_thread(void *argument);

static void qs_get_pipe1_wh(const qs_snapshot_config_t *cfg, uint32_t *w, uint32_t *h)
{
    if (!w || !h) return;
    *w = 1280;
    *h = 720;
    if (!cfg) return;
    if (cfg->fast_capture_resolution == 1) {
        *w = 1920;
        *h = 1080;
    } else if (cfg->fast_capture_resolution == 2) {
        *w = 2688;
        *h = 1520;
    }
}

static void qs_light_set(aicam_bool_t enable, uint32_t brightness_percent)
{
    if (!s_light_dev) return;
    if (!enable) {
        (void)device_ioctl(s_light_dev, MISC_CMD_PWM_OFF, 0, 0);
        return;
    }
    if (brightness_percent > 100U) brightness_percent = 100U;
    uint8_t duty = brightness_percent;
    (void)device_ioctl(s_light_dev, MISC_CMD_PWM_SET_DUTY, (uint8_t *)&duty, 0);
    (void)device_ioctl(s_light_dev, MISC_CMD_PWM_ON, 0, 0);
}

static void qs_stop_camera_pipes(aicam_bool_t need_ai)
{
    if (!s_cam_dev) return;
    (void)device_ioctl(s_cam_dev, CAM_CMD_UNSHARE_PIPE1_BUFFER, s_main_fb, 0);
    if (need_ai) {
        (void)device_ioctl(s_cam_dev, CAM_CMD_UNSHARE_PIPE2_BUFFER, s_ai_fb, 0);
    }
    device_stop(s_cam_dev);
    camera_deinit_but_not_unregister();
}

static int qs_prepare_camera_and_jpeg(const qs_snapshot_config_t *cfg, const nn_model_info_t *model_info_opt)
{
    if (!cfg) return AICAM_ERROR_INVALID_PARAM;

    /* pipe1 params (align with device_service_camera_capture_fast) */
    pipe_params_t pipe1 = {0};
    if (cfg->fast_capture_resolution == 1) {
        pipe1.width = 1920;
        pipe1.height = 1080;
    } else if (cfg->fast_capture_resolution == 2) {
        pipe1.width = 2688;
        pipe1.height = 1520;
    } else {
        pipe1.width = 1280;
        pipe1.height = 720;
    }
    pipe1.fps = 30;
    pipe1.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    pipe1.bpp = 2;
    pipe1.buffer_nb = 2;
    int ret = device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE1_PARAM, (uint8_t *)&pipe1, sizeof(pipe_params_t));
    if (ret != 0) return ret;

    aicam_bool_t need_ai = (cfg->ai_enabled != 0);

    if (need_ai) {
        pipe_params_t pipe2 = {0};
        /* AI pipe size: NVS valid -> use it; else use model_info_opt (must be ready) */
        uint32_t w = cfg->ai_pipe_width;
        uint32_t h = cfg->ai_pipe_height;
        if ((w == 0 || h == 0) && model_info_opt) {
            w = model_info_opt->input_width;
            h = model_info_opt->input_height;
        }
        if (w == 0 || h == 0) {
            /* Model info not ready yet; caller must wait AI info before calling again. */
            return AICAM_ERROR_INVALID_PARAM;
        }
        pipe2.width = (int)w;
        pipe2.height = (int)h;
        pipe2.fps = 30;
        pipe2.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
        pipe2.bpp = 3;
        pipe2.buffer_nb = 2;
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE2_PARAM, (uint8_t *)&pipe2, sizeof(pipe_params_t));

        uint8_t ctrl = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE_CTRL, &ctrl, 0);
    } else {
        uint8_t ctrl = CAMERA_CTRL_PIPE1_BIT;
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE_CTRL, &ctrl, 0);
    }

    /* ISP IQ before start: NVS + stock profiles via quick_storage (no json_config init). */
    {
        ISP_IQParamTypeDef isp_param = {0};
        (void)quick_storage_fill_isp_iq_param(cfg->isp_mode, cfg->grayscale, &isp_param);
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_ISP_PARAM, (uint8_t *)&isp_param, sizeof(isp_param));
    }

    /* mirror/flip and skip frames */
    {
        sensor_params_t sp = {0};
        if (device_ioctl(s_cam_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sp, sizeof(sp)) == 0) {
            sp.mirror_flip = (int)cfg->mirror_flip;
            (void)device_ioctl(s_cam_dev, CAM_CMD_SET_SENSOR_PARAM, (uint8_t *)&sp, sizeof(sp));
        }
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_STARTUP_SKIP_FRAMES, NULL, cfg->fast_capture_skip_frames);
    }

    /* start camera (device layer handles sensor/pipes bring-up) */
    ret = device_start(s_cam_dev);
    if (ret != 0) return ret;

    /* configure jpeg encoder to match pipe1 */
    pipe_params_t cur_pipe1 = {0};
    jpegc_params_t jp = {0};
    if (device_ioctl(s_cam_dev, CAM_CMD_GET_PIPE1_PARAM, (uint8_t *)&cur_pipe1, sizeof(cur_pipe1)) != 0) {
        return AICAM_ERROR_IO;
    }
    if (device_ioctl(s_jpeg_dev, JPEGC_CMD_GET_ENC_PARAM, (uint8_t *)&jp, sizeof(jp)) != 0) {
        return AICAM_ERROR_IO;
    }
    jp.ImageWidth = (uint32_t)cur_pipe1.width;
    jp.ImageHeight = (uint32_t)cur_pipe1.height;
    jp.ChromaSubsampling = JPEG_420_SUBSAMPLING;
    jp.ImageQuality = (cfg->fast_capture_jpeg_quality == 0) ? ENC_DEFAULT_IMAGE_QUALITY : cfg->fast_capture_jpeg_quality;
    (void)device_ioctl(s_jpeg_dev, JPEGC_CMD_SET_ENC_PARAM, (uint8_t *)&jp, sizeof(jp));

    return 0;
}

static void qs_snapshot_thread(void *argument)
{
    uint32_t flags = 0;
    (void)argument;

    qt_prof_t prof;
    qt_prof_init(&prof);
    QT_TRACE("[QS] ", "snap start");
    qt_prof_step(&prof, "[QS] snap:start ");

    if (quick_storage_read_snapshot_config(&s_cfg) != 0) {
        QT_TRACE("[QS] ", "snap cfg read fail");
        (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
        osThreadExit();
        return;
    }

    /* publish config */
    (void)osEventFlagsSet(s_evt, QS_FLAG_CFG_READY);
    QT_TRACE("[QS] ", "cfg ai=%u store_ai=%u", (unsigned)s_cfg.ai_enabled, (unsigned)s_cfg.capture_storage_ai);
    qt_prof_step(&prof, "[QS] snap:cfg ");

    /* find devices */
    if (!s_cam_dev) {
        s_cam_dev = device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (!s_cam_dev) {
            QT_TRACE("[QS] ", "cam dev missing");
            (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
            osThreadExit();
            return;
        }
    }
    if (!s_jpeg_dev) {
        s_jpeg_dev = device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
        if (!s_jpeg_dev) {
            QT_TRACE("[QS] ", "jpeg dev missing");
            (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
            osThreadExit();
            return;
        }
    }
    if (!s_light_dev) {
        s_light_dev = device_find_pattern(FLASH_DEVICE_NAME, DEV_TYPE_MISC);
        if (!s_light_dev) {
            QT_TRACE("[QS] ", "light dev missing");
            (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
            osThreadExit();
            return;
        }
    }
    qt_prof_step(&prof, "[QS] snap:dev ");

    const aicam_bool_t need_ai = (s_cfg.ai_enabled != 0);

    /* If AI enabled and NVS didn't provide ai_pipe_width/height, wait AI model info first. */
    nn_model_info_t *model_info_opt = NULL;
    if (need_ai && (s_cfg.ai_pipe_width == 0 || s_cfg.ai_pipe_height == 0)) {
        flags =osEventFlagsWait(s_evt, QS_FLAG_AI_INFO_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
        if (flags & QS_FLAG_ERROR_ABORT) {
            osThreadExit();
            return;
        }
        model_info_opt = &s_model_info;
        QT_TRACE("[QS] ", "ai pipe from model %lux%lu",
                 (unsigned long)s_model_info.input_width,
                 (unsigned long)s_model_info.input_height);
        qt_prof_step(&prof, "[QS] snap:ai_info ");
    }

    /* light control: follow device_service fast path behavior (AUTO treated as ON) */
    if (s_cfg.light_mode != QS_LIGHT_MODE_OFF) {
        aicam_bool_t light_on = AICAM_FALSE;
        if (s_cfg.light_mode == QS_LIGHT_MODE_ON) light_on = AICAM_TRUE;
        else if (s_cfg.light_mode == QS_LIGHT_MODE_AUTO) light_on = AICAM_TRUE;
        else if (s_cfg.light_mode == QS_LIGHT_MODE_CUSTOM) {
            uint64_t ts = rtc_get_local_timestamp();
            uint32_t now_s = (uint32_t)(ts % 86400);
            if (s_cfg.light_start_time < s_cfg.light_end_time) {
                light_on = (now_s >= s_cfg.light_start_time && now_s <= s_cfg.light_end_time);
            } else {
                /* cross-midnight: e.g. 22:00–06:00 */
                light_on = (now_s >= s_cfg.light_start_time || now_s <= s_cfg.light_end_time);
            }
        }
        if (light_on) {
            qs_light_set(AICAM_TRUE, s_cfg.light_brightness);
        }
    }

    /* init camera/jpeg based on config (+ model info if needed) */
    int prep = qs_prepare_camera_and_jpeg(&s_cfg, model_info_opt);
    if (prep != 0) {
        QT_TRACE("[QS] ", "prepare cam/jpeg fail %d", prep);
        (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
        osThreadExit();
        return;
    }
    qt_prof_step(&prof, "[QS] snap:prep ");

    /* capture pipe1 buffer (+ pipe2 buffer if AI) */
    camera_buffer_with_frame_id_t pipe1 = {0};
    int fb_ret = device_ioctl(s_cam_dev, CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID, (uint8_t *)&pipe1, 0);
    if (fb_ret == AICAM_OK && pipe1.buffer && pipe1.size > 0) {
        s_main_fb = pipe1.buffer;
        s_main_fb_size = (size_t)pipe1.size;
        s_frame_id = pipe1.frame_id;
        (void)osEventFlagsSet(s_evt, QS_FLAG_FRAME_READY);
        QT_TRACE("[QS] ", "pipe1 frame %lu size %lu",
                 (unsigned long)s_frame_id,
                 (unsigned long)s_main_fb_size);
        qt_prof_step(&prof, "[QS] snap:pipe1 ");
    } else {
        qs_stop_camera_pipes(need_ai);
        QT_TRACE("[QS] ", "pipe1 get fail %d", fb_ret);
        (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
        osThreadExit();
        return;
    }
    // printf("[QS]end1, %lu ms\r\n", HAL_GetTick());

    if (need_ai) {
        camera_buffer_with_frame_id_t pipe2 = {0};
        int pipe2_ret = device_ioctl(s_cam_dev, CAM_CMD_GET_PIPE2_BUFFER_WITH_FRAME_ID, (uint8_t *)&pipe2, 0);
        if (pipe2_ret == AICAM_OK && pipe2.buffer && pipe2.size > 0) {
            s_ai_fb = pipe2.buffer;
            s_ai_fb_size = (size_t)pipe2.size;
            (void)osEventFlagsSet(s_evt, QS_FLAG_AI_FRAME_READY);
            qt_prof_step(&prof, "[QS] snap:pipe2 ");
            if (pipe2.frame_id != s_frame_id) {
                QB_LOGW("pipe2 frame_id != s_frame_id: %lu != %lu",
                        (unsigned long)pipe2.frame_id,
                        (unsigned long)s_frame_id);
            }
        } else {
            qs_stop_camera_pipes(need_ai);
            QT_TRACE("[QS] ", "pipe2 get fail %d", pipe2_ret);
            (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
            osThreadExit();
            return;
        }
        // printf("[QS]end2, %lu ms\r\n", HAL_GetTick());
    }
    printf("SNAP: %lu ms\r\n", HAL_GetTick());

    /* turn light off after capture */
    if (s_cfg.light_mode != QS_LIGHT_MODE_OFF) {
        qs_light_set(AICAM_FALSE, 0);
    }

    /* Stop pipes/sensor early to reduce power while encoding/inference runs */
    qs_stop_camera_pipes(need_ai);
    qt_prof_step(&prof, "[QS] snap:stop ");

    /* JPEG encode using pipe1 buffer */
    if (s_main_fb && s_main_fb_size > 0) {
        if (device_ioctl(s_jpeg_dev, JPEGC_CMD_INPUT_ENC_BUFFER, s_main_fb, (uint32_t)s_main_fb_size) == 0) {
            unsigned char *out = NULL;
            int out_len = device_ioctl(s_jpeg_dev, JPEGC_CMD_OUTPUT_ENC_BUFFER, (unsigned char *)&out, 0);
            if (out && out_len > 0) {
                /* detach from jpegc internal ownership (see quick_readme.md) */
                (void)device_ioctl(s_jpeg_dev, JPEGC_CMD_UNSHARE_ENC_BUFFER, out, 0);
                s_jpeg_data = (uint8_t *)out;
                s_jpeg_size = (size_t)out_len;
                (void)osEventFlagsSet(s_evt, QS_FLAG_JPEG_READY);
                QT_TRACE("[QS] ", "jpeg %luB", (unsigned long)s_jpeg_size);
                qt_prof_step(&prof, "[QS] snap:jpeg ");
            } else {
                QT_TRACE("[QS] ", "jpeg out fail %d", out_len);
                (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
                osThreadExit();
                return;
            }
        }
    }

    osThreadExit();
}

extern void NPURam_enable();
extern void NPURam_disable();
extern void NPUCache_config();
extern void npu_cache_disable(void);
static void qs_ai_thread(void *argument)
{
    uint32_t flags = 0;
    (void)argument;

    qt_prof_t prof;
    qt_prof_init(&prof);
    QT_TRACE("[QS] ", "ai start");
    qt_prof_step(&prof, "[QS] ai:start ");

    flags = osEventFlagsWait(s_evt, QS_FLAG_CFG_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
    if (flags & QS_FLAG_ERROR_ABORT) {
        osThreadExit();
        return;
    }

    if (!s_cfg.ai_enabled) {
        s_ai_inference_time_ms = 0;
        (void)osEventFlagsSet(s_evt, QS_FLAG_AI_INFO_READY | QS_FLAG_AI_RESULT_READY);
        osThreadExit();
        return;
    }

    /* Ensure NN runtime/device is initialized in quick path (driver_core skips nn_register). */
    // NPURam_enable();
    // NPUCache_config();
    // nn_register();
    qt_prof_step(&prof, "[QS] ai:nn ");

    /* load model: follow device_service fast path selection */
    uintptr_t model_ptr = (s_cfg.ai_1_active) ? (AI_2_BASE + 1024U) : (AI_1_BASE + 1024U);
    if (nn_load_model(model_ptr) != 0) {
        QT_TRACE("[QS] ", "load model fail");
        (void)osEventFlagsSet(s_evt, QS_FLAG_AI_INFO_READY | QS_FLAG_AI_RESULT_READY);
        return;
    }
    qt_prof_step(&prof, "[QS] ai:load ");

    memset(&s_model_info, 0, sizeof(s_model_info));
    if (nn_get_model_info(&s_model_info) != AICAM_OK) {
        /* still allow snapshot thread to proceed if NVS provided ai_pipe size */
        memset(&s_model_info, 0, sizeof(s_model_info));
    }
    (void)osEventFlagsSet(s_evt, QS_FLAG_AI_INFO_READY);
    QT_TRACE("[QS] ", "model %lux%lu",
             (unsigned long)s_model_info.input_width,
             (unsigned long)s_model_info.input_height);

    /* wait for inference frame from snapshot thread */
    (void)osEventFlagsWait(s_evt, QS_FLAG_AI_FRAME_READY, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
    qt_prof_step(&prof, "[QS] ai:frame ");

    s_ai_inference_time_ms = 0;
    if (s_ai_fb && s_ai_fb_size > 0) {
        memset(&s_ai_result, 0, sizeof(s_ai_result));

        /* thresholds from config are stored as uint32; device_service uses float. */
        (void)nn_set_confidence_threshold((float)s_cfg.confidence_threshold / 100.0f);
        (void)nn_set_nms_threshold((float)s_cfg.nms_threshold / 100.0f);

        uint32_t start = osKernelGetTickCount();
        (void)nn_inference_frame(s_ai_fb, s_ai_fb_size, &s_ai_result);
        s_ai_inference_time_ms = osKernelGetTickCount() - start;
        QT_TRACE("[QS] ", "infer %lums valid=%u type=%d",
                 (unsigned long)s_ai_inference_time_ms,
                 (unsigned)s_ai_result.is_valid,
                 (int)s_ai_result.type);
        qt_prof_step(&prof, "[QS] ai:infer ");

        /* release pipe2 buffer now (transfer ownership to AI thread) */
        if (s_cam_dev) {
            (void)device_ioctl(s_cam_dev, CAM_CMD_RETURN_PIPE2_BUFFER, s_ai_fb, 0);
        }
        s_ai_fb = NULL;
        s_ai_fb_size = 0;
    }

    /* release NPU/model resources */
    // (void)nn_unload_model();
    // nn_unregister();
    // npu_cache_disable();
    // NPURam_disable();

    (void)osEventFlagsSet(s_evt, QS_FLAG_AI_RESULT_READY);
    s_cfg.capture_storage_ai = sd_is_detected() ? AICAM_TRUE : AICAM_FALSE;
    if (s_cfg.capture_storage_ai) {
        if (!s_draw_dev) {
            s_draw_dev = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
        }
        if (s_draw_dev) {
            flags = osEventFlagsWait(s_evt, QS_FLAG_JPEG_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
            if (flags & QS_FLAG_ERROR_ABORT) {
                (void)osEventFlagsSet(s_evt, QS_FLAG_AI_ERROR_ABORT);
                osThreadExit();
                return;
            }
            qt_prof_step(&prof, "[QS] ai:draw ");

            /* Draw AI results on the captured main frame (RGB565), then encode another JPEG. */
            if (s_main_fb && s_main_fb_size > 0) {
                uint32_t w = 0, h = 0;
                qs_get_pipe1_wh(&s_cfg, &w, &h);

                if (w > 0 && h > 0) {
                    qs_ai_draw_init_if_needed(w, h);

                    if (s_ai_draw_inited && s_ai_result.type == PP_TYPE_OD) {
                        s_od_conf.p_dst = s_main_fb;
                        s_od_conf.image_width = w;
                        s_od_conf.image_height = h;
                        for (int i = 0; i < s_ai_result.od.nb_detect; i++) {
                            (void)od_draw_result(&s_od_conf, (od_detect_t *)&s_ai_result.od.detects[i]);
                        }
                    } else if (s_ai_draw_inited && s_ai_result.type == PP_TYPE_MPE) {
                        s_mpe_conf.p_dst = s_main_fb;
                        s_mpe_conf.image_width = w;
                        s_mpe_conf.image_height = h;
                        for (int i = 0; i < s_ai_result.mpe.nb_detect; i++) {
                            (void)mpe_draw_result(&s_mpe_conf, (mpe_detect_t *)&s_ai_result.mpe.detects[i]);
                        }
                    } else {
                        /* Unknown postprocess type: still allow saving a copy (no overlay). */
                    }

                    /* Encode AI-overlay JPEG (same lifecycle rules as s_jpeg_data). */
                    jpegc_params_t enc_params = {0};
                    if (device_ioctl(s_jpeg_dev, JPEGC_CMD_GET_ENC_PARAM, (uint8_t *)&enc_params, sizeof(jpegc_params_t)) == 0) {
                        if (device_ioctl(s_jpeg_dev, JPEGC_CMD_SET_ENC_PARAM, (uint8_t *)&enc_params, sizeof(jpegc_params_t)) == 0) {
                            if (s_jpeg_dev && device_ioctl(s_jpeg_dev, JPEGC_CMD_INPUT_ENC_BUFFER, s_main_fb, (uint32_t)s_main_fb_size) == 0) {
                                unsigned char *out = NULL;
                                int out_len = device_ioctl(s_jpeg_dev, JPEGC_CMD_OUTPUT_ENC_BUFFER, (unsigned char *)&out, 0);
                                if (out && out_len > 0) {
                                    (void)device_ioctl(s_jpeg_dev, JPEGC_CMD_UNSHARE_ENC_BUFFER, out, 0);
                                    s_ai_jpeg_data = (uint8_t *)out;
                                    s_ai_jpeg_size = (size_t)out_len;
                                    (void)osEventFlagsSet(s_evt, QS_FLAG_AI_JPEG_READY);
                                    QT_TRACE("[QS] ", "ai jpeg %luB", (unsigned long)s_ai_jpeg_size);
                                    qt_prof_step(&prof, "[QS] ai:jpeg2 ");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (s_ai_jpeg_data == NULL || s_ai_jpeg_size == 0) (void)osEventFlagsSet(s_evt, QS_FLAG_AI_ERROR_ABORT);
    osThreadExit();
}

int quick_snapshot_init(void)
{
    if (s_inited) return 0;

    if (s_evt == NULL) {
        s_evt = osEventFlagsNew(NULL);
        if (s_evt == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    /* Place stacks in PSRAM to reduce internal SRAM pressure. */
    static uint8_t s_snap_stack[4096 * 4] ALIGN_32;
    static uint8_t s_ai_stack[4096 * 4] ALIGN_32;

    if (s_snap_tid == NULL) {
        static const osThreadAttr_t attr = {
            .name = "qs_snap",
            .stack_mem = s_snap_stack,
            .stack_size = sizeof(s_snap_stack),
            .priority = osPriorityRealtime,
            .cb_mem     = 0,
            .cb_size    = 0,
            .attr_bits  = 0u,
            .tz_module  = 0u,
        };
        s_snap_tid = osThreadNew(qs_snapshot_thread, NULL, &attr);
        if (s_snap_tid == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    /* Always create AI thread; it will exit quickly if AI is disabled. */
    if (s_ai_tid == NULL) {
        static const osThreadAttr_t attr = {
            .name = "qs_ai",
            .stack_mem = s_ai_stack,
            .stack_size = sizeof(s_ai_stack),
            .priority = osPriorityBelowNormal,
            .cb_mem     = 0,
            .cb_size    = 0,
            .attr_bits  = 0u,
            .tz_module  = 0u,
        };
        s_ai_tid = osThreadNew(qs_ai_thread, NULL, &attr);
        if (s_ai_tid == NULL) return AICAM_ERROR_NO_MEMORY;
    }

    s_inited = AICAM_TRUE;
    QT_TRACE("[QS] ", "init ok");
    return 0;
}

int quick_snapshot_is_init(void)
{
    return s_inited;
}

int quick_snapshot_wait_config(qs_snapshot_config_t *snapshot_config)
{
    uint32_t flags = 0;
    if (!snapshot_config) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_CFG_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    } else {
        *snapshot_config = s_cfg;
        return 0;
    }
}

int quick_snapshot_wait_capture_frame(uint8_t **main_fb, size_t *main_fb_size)
{
    uint32_t flags = 0;
    if (!main_fb || !main_fb_size) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_FRAME_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    } else {
        *main_fb = s_main_fb;
        *main_fb_size = s_main_fb_size;
        return 0;
    }
}

int quick_snapshot_wait_capture_jpeg(uint8_t **jpeg_data, size_t *jpeg_size)
{
    uint32_t flags = 0;
    if (!jpeg_data || !jpeg_size) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_JPEG_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    } else {
        *jpeg_data = s_jpeg_data;
        *jpeg_size = s_jpeg_size;
        return 0;
    }
}

int quick_snapshot_wait_ai_info(nn_model_info_t *model_info)
{
    uint32_t flags = 0;
    if (!model_info) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_AI_INFO_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    } else {
        *model_info = s_model_info;
        return 0;
    }
}

int quick_snapshot_wait_ai_result(nn_result_t *ai_result)
{
    uint32_t flags = 0;
    if (!ai_result) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_AI_RESULT_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    } else {
        *ai_result = s_ai_result;
        return 0;
    }
}

int quick_snapshot_get_ai_inference_time_ms(uint32_t *inference_time_ms)
{
    uint32_t flags = 0;
    if (!inference_time_ms) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_AI_RESULT_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    }
    *inference_time_ms = s_ai_inference_time_ms;
    return 0;
}

/**
 * @brief Wait for AI JPEG
 * @param ai_jpeg_data AI JPEG data
 * @param ai_jpeg_size AI JPEG size
 * @return 0 on success, other values on error
 */
int quick_snapshot_wait_ai_jpeg(uint8_t **ai_jpeg_data, size_t *ai_jpeg_size)
{
    uint32_t flags = 0;
    if (!ai_jpeg_data || !ai_jpeg_size) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    flags = osEventFlagsWait(s_evt, QS_FLAG_AI_JPEG_READY | QS_FLAG_AI_ERROR_ABORT | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_AI_ERROR_ABORT || flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    } else {
        *ai_jpeg_data = s_ai_jpeg_data;
        *ai_jpeg_size = s_ai_jpeg_size;
        return 0;
    }
}

int quick_snapshot_get_frame_id(uint32_t *frame_id)
{
    if (!frame_id) return AICAM_ERROR_INVALID_PARAM;
    if (!s_inited || s_evt == NULL) return AICAM_ERROR_NOT_INITIALIZED;

    uint32_t flags = osEventFlagsWait(s_evt, QS_FLAG_FRAME_READY | QS_FLAG_ERROR_ABORT, osFlagsWaitAny | osFlagsNoClear, QS_WAIT_EVENT_TIMEOUT_MS);
    if (flags & QS_FLAG_ERROR_ABORT) {
        return AICAM_ERROR;
    }
    *frame_id = s_frame_id;
    return 0;
}

/**
 * @brief Wait for event
 * @param event_mask Event mask
 * @return Event flags
 */
uint32_t quick_snapshot_wait_event(uint32_t event_mask, uint32_t timeout)
{
    return osEventFlagsWait(s_evt, event_mask, osFlagsWaitAny | osFlagsNoClear, timeout);
}

/**
 * @brief Clear event
 * @param event_mask Event mask
 */
void quick_snapshot_clear_event(uint32_t event_mask)
{
    (void)osEventFlagsClear(s_evt, event_mask);
}
