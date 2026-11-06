#include "quick_snapshot.h"
#include "quick_storage.h"
#include "storage.h"
#include "json_config_internal.h"

/* ---- AE verification-based fast convergence (QS_AE_VERIFY) ----
 * Compile switch: 0 = stock official QS behavior (fixed skip wait);
 *                 1 = statistics-judged convergence gate (passive + adaptive). */
#define QS_AE_VERIFY_ENABLE         1
#define QS_VERIFY_FILL_FLOOR        6U   /* frames: measured physical floor
                                            (Sept tests: skip=5 grabs black on
                                            OS04C10; dummy+sensorDelay >= 6) */
#define QS_VERIFY_MIN_BUDGET_FRAMES 8U   /* budget never below floor+2: the
                                            first usable stat judgement must
                                            fit inside the budget or every
                                            capture degrades to a timeout grab */
#define QS_VERIFY_TOL               8U   /* luma tolerance: low-lux fine-band
                                            variant of isp_ae_algo.c
                                            AE_FINE_TOLERANCE (5 normal / 8-10 low) */
#define QS_VERIFY_CLAMP_DL          2U  /* clamped-accept: |dL| between samples */
#define QS_SENSOR_DELAY             3U   /* must match profile sensorDelay */
/* ---- seed policy ----
 * 0 = fixed factory seed, never learned/updated;
 * 1 = learn on in-band exit (persist attributed E/G).
 * Adaptive budget (QS_VERIFY_EXT_*) must pair with 1: extending the wait
 * only pays off when the converged point is then learned, otherwise every
 * wake in a changed scene pays the extension again. */
#define QS_AE_SEED_LEARN            1
#define QS_AE_SEED_FIXED_E          30000U  /* us - the 307-era frozen value */
#define QS_AE_SEED_FIXED_G          6185U   /* mdb */
/* ---- adaptive verification budget ----
 * Downward AE convergence is slower than the base skip budget. Past the
 * base budget, keep waiting ONLY while |L-target| keeps shrinking; give up
 * after QS_VERIFY_EXT_STALL consecutive non-improving samples. Hard cap
 * bounds the worst wake. */
#define QS_VERIFY_EXT_MAX_MS        3000U
#define QS_VERIFY_EXT_STALL         3U
#include "quick_trace.h"
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "board_hw.h"
#include "camera.h"
#include "jpegc.h"
#include "draw.h"
#include "ai_draw.h"
#include "misc.h"
#include "device_service.h"
#include "npu_cache.h"
#include "mem_map.h"
#include "common_utils.h"
#include "pwr.h"
#include "drtc.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*
 * quick_snapshot architecture:
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

static int qs_prepare_camera_and_jpeg(const qs_snapshot_config_t *cfg, const nn_model_info_t *model_info_opt, uint32_t effective_mode)
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
        /* 3 buffers (matches device-layer NN_BUFFER_NB): during the boot
         * skip-frames window camera_ioctl holds mtx_isr across the GET_PIPE1
         * semaphore wait, so pipe2 frame events cannot recycle a READY buffer
         * and fall into the 4f829b1 demote path. With only 2 buffers the
         * just-published pipe2 frame gets eaten every time, leaving pipe2's
         * latest READY one frame behind pipe1 ("pipe2 frame_id != s_frame_id").
         * The third buffer gives the event an IDLE slot instead. */
        pipe2.buffer_nb = 3;
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE2_PARAM, (uint8_t *)&pipe2, sizeof(pipe_params_t));

        uint8_t ctrl = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE_CTRL, &ctrl, 0);
    } else {
        uint8_t ctrl = CAMERA_CTRL_PIPE1_BIT;
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_PIPE_CTRL, &ctrl, 0);
    }

    /* ISP IQ before start: NVS + stock profiles via quick_storage (no json_config init).
     * IR boards pass the day/night effective mode (DAY/NIGHT) resolved by the
     * snapshot thread; standard boards pass the configured isp_mode verbatim. */
    {
        ISP_IQParamTypeDef isp_param = {0};
        (void)quick_storage_fill_isp_iq_param(effective_mode, &isp_param);
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_ISP_PARAM, (uint8_t *)&isp_param, sizeof(isp_param));
    }

    /* mirror/flip and skip frames */
    {
        sensor_params_t sp = {0};
        if (device_ioctl(s_cam_dev, CAM_CMD_GET_SENSOR_PARAM, (uint8_t *)&sp, sizeof(sp)) == 0) {
            sp.mirror_flip = (int)cfg->mirror_flip;
            (void)device_ioctl(s_cam_dev, CAM_CMD_SET_SENSOR_PARAM, (uint8_t *)&sp, sizeof(sp));
        }
#if QS_AE_VERIFY_ENABLE
        /* camera-level skip only guards pipeline validity (fill floor:
         * dummy frames + sensorDelay); convergence waiting moves to
         * qs_wait_ae_verified() with a budget derived from the user knob. */
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_STARTUP_SKIP_FRAMES, NULL, QS_VERIFY_FILL_FLOOR);
#else
        (void)device_ioctl(s_cam_dev, CAM_CMD_SET_STARTUP_SKIP_FRAMES, NULL, cfg->fast_capture_skip_frames);
#endif
    }

#if QS_AE_VERIFY_ENABLE
    /* Resume AE from the last verified point instead of the black frame.
     * QS_AE_SEED_LEARN=1: persisted last state (NVS "ae_state"), learned
     * on every in-band capture. QS_AE_SEED_LEARN=0: fixed factory seed —
     * same deterministic start on every wake, no NVS involved. */
#if QS_AE_SEED_LEARN
    {
        camera_ae_state_record_t ae_rec = {0};
        if (storage_nvs_read(NVS_USER, NVS_KEY_AE_LAST_STATE,
                             &ae_rec, sizeof(ae_rec)) == (int)sizeof(ae_rec) &&
            ae_rec.magic == CAMERA_AE_STATE_MAGIC) {
            if (camera_ae_set_last(ae_rec.exposure_us, ae_rec.gain_mdb) == AICAM_OK) {
                QT_TRACE("[QS] ", "ae resume e=%lu g=%lu",
                         (unsigned long)ae_rec.exposure_us,
                         (unsigned long)ae_rec.gain_mdb);
            }
        }
    }
#else
    (void)camera_ae_set_last(QS_AE_SEED_FIXED_E, QS_AE_SEED_FIXED_G);
    QT_TRACE("[QS] ", "ae resume (fixed) e=%lu g=%lu",
             (unsigned long)QS_AE_SEED_FIXED_E,
             (unsigned long)QS_AE_SEED_FIXED_G);
#endif
#endif

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


#if QS_AE_VERIFY_ENABLE
/* Convergence gate judged from ISP statistics (same source the AE loop
 * reads) instead of a fixed skip-frame wait. Passive: no exposure writes,
 * the AE loop keeps running; we only decide WHEN the frame is good.
 * Exit codes: 1 = in-band, 2 = clamped-accept, 3 = timeout/no-stats.
 *
 * On exit=1 the *attributed* exposure/gain are returned via seed_e/seed_g:
 * the values that produced the verified frame (written ~QS_SENSOR_DELAY
 * frames earlier), NOT the AE's latest write — the AE gain limit-cycle
 * keeps moving between write and effect, a "latest-write" seed lands at a
 * random cycle phase and alternates the next wake's starting point. */
static int qs_wait_ae_verified(uint32_t budget_ms,
                               uint32_t *seed_e, uint32_t *seed_g)
{
    uint32_t t0 = HAL_GetTick();
    uint8_t tgt = 0, L = 0, L_prev = 0;
    uint32_t fid = 0, fid_prev = 0;
    int have_prev = 0;
    int32_t d_prev = 0x7FFFFFFF;
    uint32_t stall = 0;
    uint32_t extended_from = 0;
    /* ring of last E/G writes indexed by stats frame id, for attribution */
    struct { uint32_t fid; uint32_t e, g; } ring[8] = {0};

    if (camera_ae_get_target(&tgt) != AICAM_OK) {
        QT_TRACE("[QS] ", "verify: no target, fall through");
        return 3;
    }
    QT_TRACE("[QS] ", "verify begin tgt=%u budget=%lums",
             (unsigned)tgt, (unsigned long)budget_ms);

    while ((HAL_GetTick() - t0) < QS_VERIFY_EXT_MAX_MS) {
        uint32_t elapsed = HAL_GetTick() - t0;
        uint8_t dl;
        uint32_t e = 0, g = 0;
        int32_t d;

        /* past the base budget: keep waiting only while converging */
        if (elapsed >= budget_ms && stall >= QS_VERIFY_EXT_STALL) {
            break;
        }

        if (camera_stats_get_luma(&L, &fid) != AICAM_OK ||
            fid < QS_VERIFY_FILL_FLOOR || fid == fid_prev) {
            osDelay(5);
            continue;
        }
        camera_ae_get_last(&e, &g);
        ring[fid & 7U].fid = fid;
        ring[fid & 7U].e = e;
        ring[fid & 7U].g = g;

        /* convergence progress tracker: |L-target| must keep shrinking */
        d = (int32_t)L - (int32_t)tgt;
        if (d < 0) d = -d;
        if (have_prev) {
            if (d < d_prev) {
                stall = 0;
                if (elapsed >= budget_ms && !extended_from) {
                    extended_from = 1;
                    QT_TRACE("[QS] ", "verify extend (converging) L=%u",
                             (unsigned)L);
                }
            } else {
                stall++;
            }
        }
        d_prev = d;

        /* in-band: same criterion family as the AE loop fine band */
        if (L >= (uint8_t)((tgt > QS_VERIFY_TOL) ? (tgt - QS_VERIFY_TOL) : 0) &&
            L <= (uint8_t)(tgt + QS_VERIFY_TOL)) {
            /* attribute: the grabbed frame is ~the next frame after this
             * verdict, produced by the write QS_SENSOR_DELAY frames back */
            uint32_t want = (fid + 1U > QS_SENSOR_DELAY) ? (fid + 1U - QS_SENSOR_DELAY) : 0;
            if (seed_e && seed_g && ring[want & 7U].fid == want) {
                *seed_e = ring[want & 7U].e;
                *seed_g = ring[want & 7U].g;
            } else if (seed_e && seed_g) {
                *seed_e = e;
                *seed_g = g;
            }
            QT_TRACE("[QS] ", "verify exit=1 inband L=%u e=%lu g=%lu +%lums",
                     (unsigned)L, (unsigned long)e, (unsigned long)g,
                     (unsigned long)(HAL_GetTick() - t0));
            return 1;
        }
        /* clamped-accept: BOTH exposure and gain at their ceilings and luma
         * settled -> this is the best the scene allows. Exposure alone is
         * NOT sufficient: in dim scenes exposure sits at its ceiling while
         * gain is still ramping. */
        dl = (uint8_t)(L_prev > L ? (L_prev - L) : (L - L_prev));
        if (have_prev &&
            e >= EXPOSURE_MAX && g >= ((GAIN_MAX / 100U) * 95U) &&
            dl <= QS_VERIFY_CLAMP_DL) {
            QT_TRACE("[QS] ", "verify exit=2 clamp L=%u e=%lu g=%lu +%lums",
                     (unsigned)L, (unsigned long)e, (unsigned long)g,
                     (unsigned long)(HAL_GetTick() - t0));
            return 2;
        }
        L_prev = L;
        fid_prev = fid;
        have_prev = 1;
        osDelay(5);
    }
    QT_TRACE("[QS] ", "verify exit=3 timeout L=%u +%lums",
             (unsigned)L, (unsigned long)(HAL_GetTick() - t0));
    return 3;
}
#endif

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

    /* Light control for the capture — board-variant split:
     * IR boards: day/night evaluation (device_service is NOT running in the
     *   sleep-wake fast path, so the config + isp_mode come from NVS via
     *   quick_storage; AUTO evaluates TIME/LIGHT_SENSOR, ISP_STATS is not
     *   feasible before the stream starts -> day). capture_prepare also
     *   switches the IR-CUT filter per the effective mode.
     * Standard boards: keep the legacy light_mode logic (AUTO treated as ON,
     *   CUSTOM = white-light schedule window). */
    uint32_t eff_mode = s_cfg.isp_mode;
    if (board_is_ir() == AICAM_TRUE) {
        day_night_config_t dn_cfg;
        aicam_bool_t ir_on = AICAM_FALSE;
        uint8_t ir_bri = 0U;
        (void)quick_storage_read_day_night_config(&dn_cfg);
        eff_mode = device_service_day_night_capture_prepare(&dn_cfg, s_cfg.isp_mode, &ir_on, &ir_bri);
        if (ir_on) {
            qs_light_set(AICAM_TRUE, ir_bri);
        }
    }
    else if (s_cfg.light_mode != QS_LIGHT_MODE_OFF) {
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
    int prep = qs_prepare_camera_and_jpeg(&s_cfg, model_info_opt, eff_mode);
    if (prep != 0) {
        QT_TRACE("[QS] ", "prepare cam/jpeg fail %d", prep);
        (void)osEventFlagsSet(s_evt, QS_FLAG_ERROR_ABORT);
        osThreadExit();
        return;
    }
    qt_prof_step(&prof, "[QS] snap:prep ");

#if QS_AE_VERIFY_ENABLE
    /* convergence gate: judge from stats instead of the fixed skip wait */
    int verify_exit = 3;
    uint32_t seed_e = 0, seed_g = 0;
    {
        pipe_params_t vp = {0};
        uint32_t fps = 30;
        uint32_t budget_frames;
        if (device_ioctl(s_cam_dev, CAM_CMD_GET_PIPE1_PARAM,
                         (uint8_t *)&vp, sizeof(vp)) == 0 && vp.fps > 0) {
            fps = (uint32_t)vp.fps;
        }
        budget_frames = (s_cfg.fast_capture_skip_frames > QS_VERIFY_MIN_BUDGET_FRAMES)
                        ? s_cfg.fast_capture_skip_frames : QS_VERIFY_MIN_BUDGET_FRAMES;
        verify_exit = qs_wait_ae_verified((budget_frames * 1000U) / fps,
                                          &seed_e, &seed_g);
        qt_prof_step(&prof, "[QS] snap:vfy ");
    }
#endif

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
#if QS_AE_VERIFY_ENABLE
#if QS_AE_SEED_LEARN
        /* Persist the AE seed ONLY when the convergence gate actually
         * passed (exit=1): a timeout/clamp grab is NOT a converged frame,
         * and persisting its AE state re-seeds the next wake from a
         * limit-cycle phase — the wake-to-wake bright/dark alternation
         * seen in low light. On bad exits the previous good seed stays. */
        if (verify_exit == 1) {
            camera_ae_state_record_t rec = {0};
            rec.magic = CAMERA_AE_STATE_MAGIC;
            rec.exposure_us = seed_e;
            rec.gain_mdb = seed_g;
            if (storage_nvs_write(NVS_USER, NVS_KEY_AE_LAST_STATE,
                                  &rec, sizeof(rec)) != (int)sizeof(rec)) {
                QT_TRACE("[QS] ", "ae save fail");
            } else {
                QT_TRACE("[QS] ", "ae saved e=%lu g=%lu",
                         (unsigned long)rec.exposure_us,
                         (unsigned long)rec.gain_mdb);
            }
        } else {
            QT_TRACE("[QS] ", "ae seed kept (verify exit=%d)", verify_exit);
        }
#else
        /* fixed-seed mode: never persist; every wake restarts from the
         * factory constants for deterministic behavior across scenes */
        QT_TRACE("[QS] ", "ae seed fixed (verify exit=%d)", verify_exit);
#endif
#endif
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

    /* turn light off after capture — IR boards always (the wake path returns
     * to sleep); standard boards only if the light config could have lit it */
    if (board_is_ir() == AICAM_TRUE || s_cfg.light_mode != QS_LIGHT_MODE_OFF) {
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
    /* Honor the NVS toggle (loaded into s_cfg by quick_storage_read_snapshot_config).
     * Do NOT override with sd_is_detected(): the caller (system_service) gates on the
     * same NVS key, and persist_record writes the inference JPEG to the active FS
     * (flash or SD). Overriding silently dropped the AI image on flash-only units
     * and wasted an encode + leaked the jpegc buffer when the toggle was off. */
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
