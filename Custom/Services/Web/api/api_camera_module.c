/**
 * @file    api_camera_module.c
 * @brief Camera Source API - list / detect / config
 * @details
 *   GET  /api/v1/camera/list    active source + known cameras (no probing)
 *   POST /api/v1/camera/detect  cross-probe the non-active camera
 *   GET  /api/v1/camera/config  camera source configuration
 *   PUT  /api/v1/camera/config  update (reboot to apply)
 *
 * Detect semantics (button in the web UI):
 *   - active = native -> power the USB host, enumerate the UVC camera, list
 *     its MJPEG configurations, then recycle the host (full power-down only
 *     when the 4G NIC doesn't own it).
 *   - active = uvc    -> register + I2C-probe the native camera (OS04C10),
 *     report 720P/1080P, then deinit it (sensor power down).
 */

#include "api_camera_module.h"
#include "web_api.h"
#include "web_server.h"
#include "cJSON.h"
#include "json_config_mgr.h"
#include "usbh_uvc_service.h"
#include "camera.h"
#include "dev_manager.h"
#include "debug.h"
#include "cmsis_os2.h"
#include <string.h>

/* Native-camera probe worker: CMW_CAMERA_Init (power-up + I2C sensor probe)
 * wedges when run on the HTTP handler thread (mongoose/web task); it needs
 * its own thread with a real stack. The detect API hands the probe off and
 * waits for the result. */
static struct {
    osThreadId_t thread;
    volatile int busy;
    volatile int done;
    char model[32];
    int present;
} s_cam_probe;

static uint8_t s_cam_probe_stack[32 * 1024] __attribute__((aligned(32)))
    __attribute__((section(".psram_bss")));

static void cam_probe_thread(void *arg)
{
    (void)arg;
    for (;;) {
        while (!s_cam_probe.busy) {
            osDelay(50);
        }

        s_cam_probe.present = 0;
        s_cam_probe.model[0] = '\0';

        if (device_find_pattern(CAMERA_DEVICE_NAME, DEV_TYPE_VIDEO) == NULL) {
            int reg = camera_register();
            LOG_SVC_INFO("camdetect: camera_register -> %d", reg);
        }
        if (camera_get_sensor_name(s_cam_probe.model, sizeof(s_cam_probe.model)) == 0) {
            s_cam_probe.present = 1;
            /* Keep the session initialized: CMW_CAMERA_DeInit ->
             * ISP_Algo_DeInit HardFaults on a never-started session (the
             * native path always stops the sensor first). The sensor idles
             * powered until reboot or a native-source boot re-inits it. */
        }

        s_cam_probe.busy = 0;
        s_cam_probe.done = 1;
    }
}

/* >0 present (model filled), 0 absent/failed, <0 timeout or busy */
static int cam_probe_run(char *model, size_t model_len, uint32_t timeout_ms)
{
    if (s_cam_probe.thread == NULL) {
        static const osThreadAttr_t attr = {
            .name = "cam_probe",
            .stack_mem = s_cam_probe_stack,
            .stack_size = sizeof(s_cam_probe_stack),
            .priority = osPriorityNormal,
        };
        s_cam_probe.thread = osThreadNew(cam_probe_thread, NULL, &attr);
        if (s_cam_probe.thread == NULL) {
            return 0;
        }
    }
    if (s_cam_probe.busy) {
        return -1; /* a probe is already in flight */
    }

    s_cam_probe.done = 0;
    s_cam_probe.busy = 1;

    uint32_t t0 = osKernelGetTickCount();
    while (!s_cam_probe.done && (osKernelGetTickCount() - t0) < timeout_ms) {
        osDelay(50);
    }
    if (!s_cam_probe.done) {
        return -1; /* worker still busy; it will finish and idle on its own */
    }

    if (s_cam_probe.present && model != NULL) {
        snprintf(model, model_len, "%s", s_cam_probe.model);
    }
    return s_cam_probe.present ? 1 : 0;
}

/* ==================== helpers ==================== */

static cJSON *camera_config_entry(uint32_t width, uint32_t height, uint32_t fps,
                                  const char *format)
{
    cJSON *c = cJSON_CreateObject();
    if (c == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(c, "width", width);
    cJSON_AddNumberToObject(c, "height", height);
    cJSON_AddNumberToObject(c, "fps", fps);
    cJSON_AddStringToObject(c, "format", format);
    return c;
}

/* native camera entry (no probing: only what is already initialized) */
static cJSON *native_camera_entry(aicam_bool_t probe)
{
    cJSON *cam = cJSON_CreateObject();
    if (cam == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(cam, "type", "native");

    aicam_bool_t present = AICAM_FALSE;
    char model[32] = {0};

    if (probe) {
        /* on-demand bring-up in a dedicated worker (see cam_probe_run) */
        int pr = cam_probe_run(model, sizeof(model), 10000);
        if (pr > 0) {
            present = AICAM_TRUE;
        } else {
            LOG_SVC_WARN("camdetect: native probe -> %d", pr);
        }
    } else {
        if (camera_get_sensor_name(model, sizeof(model)) == 0) {
            present = AICAM_TRUE;
        }
    }

    cJSON_AddBoolToObject(cam, "present", present);
    cJSON_AddStringToObject(cam, "model", present ? model : "");

    cJSON *configs = cJSON_CreateArray();
    cJSON_AddItemToArray(configs, camera_config_entry(1280, 720, 30, "h264"));
    cJSON_AddItemToArray(configs, camera_config_entry(1920, 1080, 30, "h264"));
    cJSON_AddItemToObject(cam, "configs", configs);
    return cam;
}

/* uvc camera entry; probe=1 powers the host and waits for enumeration */
static cJSON *uvc_camera_entry(aicam_bool_t probe)
{
    cJSON *cam = cJSON_CreateObject();
    if (cam == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(cam, "type", "uvc");

    uvc_svc_probe_t p;
    memset(&p, 0, sizeof(p));

    aicam_bool_t present = AICAM_FALSE;
    if (probe) {
        present = (usbh_uvc_service_probe(&p, 3000) == 0) ? AICAM_TRUE : AICAM_FALSE;
    } else {
        uvc_svc_status_t st;
        usbh_uvc_service_get_status(&st);
        present = st.dev_ready ? AICAM_TRUE : AICAM_FALSE;
        if (present) {
            /* fill identity from the enumerated device without probing */
            (void)usbh_uvc_service_probe(&p, 0);
        }
    }

    cJSON_AddBoolToObject(cam, "present", present);
    if (present) {
        char vid[8], pid[8];
        snprintf(vid, sizeof(vid), "0x%04x", p.vid);
        snprintf(pid, sizeof(pid), "0x%04x", p.pid);
        cJSON_AddStringToObject(cam, "vid", vid);
        cJSON_AddStringToObject(cam, "pid", pid);
        cJSON_AddStringToObject(cam, "product", p.product[0] ? p.product : "UVC Camera");
        cJSON_AddBoolToObject(cam, "bulk", p.bulk ? 1 : 0);
        cJSON *configs = cJSON_CreateArray();
        for (uint32_t i = 0; i < p.cfg_count; i++) {
            cJSON_AddItemToArray(configs,
                camera_config_entry(p.cfg[i].width, p.cfg[i].height, p.cfg[i].fps, "mjpeg"));
        }
        cJSON_AddItemToObject(cam, "configs", configs);
    } else {
        cJSON_AddStringToObject(cam, "product", "");
        cJSON *configs = cJSON_CreateArray();
        cJSON_AddItemToObject(cam, "configs", configs);
    }
    return cam;
}

static cJSON *uvc_status_entry(void)
{
    uvc_svc_status_t st;
    usbh_uvc_service_get_status(&st);

    cJSON *s = cJSON_CreateObject();
    cJSON_AddBoolToObject(s, "active", st.active);
    cJSON_AddBoolToObject(s, "streaming", st.streaming);
    cJSON_AddNumberToObject(s, "width", st.width);
    cJSON_AddNumberToObject(s, "height", st.height);
    cJSON_AddNumberToObject(s, "fps", st.fps);
    cJSON_AddNumberToObject(s, "fps_measured", st.fps_measured);
    cJSON_AddNumberToObject(s, "frames", (double)st.frames);
    cJSON_AddNumberToObject(s, "frames_bad", (double)st.frames_bad);
    cJSON_AddNumberToObject(s, "reconnects", (double)st.reconnects);
    return s;
}

/* ==================== handlers ==================== */

static aicam_result_t camera_list_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    camera_source_config_t cc;
    json_config_get_camera_config(&cc);

    /* "active" must be the RUNNING source (boot-time decision), not the
     * saved config: the config is reboot-applied, so right after a save
     * they differ and clients (preview player selection!) must not flip
     * to a camera that is not actually running. */
    int runtime_uvc = usbh_uvc_service_is_active();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "active", runtime_uvc ? "uvc" : "native");
    cJSON_AddStringToObject(response, "saved_source",
                             cc.source == CAMERA_SOURCE_UVC ? "uvc" : "native");
    cJSON_AddBoolToObject(response, "restart_required",
                           runtime_uvc != (cc.source == CAMERA_SOURCE_UVC));
    cJSON *cameras = cJSON_CreateArray();
    cJSON_AddItemToArray(cameras, native_camera_entry(AICAM_FALSE));
    cJSON_AddItemToArray(cameras, uvc_camera_entry(AICAM_FALSE));
    cJSON_AddItemToObject(response, "cameras", cameras);
    cJSON_AddItemToObject(response, "uvc_status", uvc_status_entry());

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize");
    }
    return api_response_success(ctx, json_str, "Camera list retrieved");
}

static aicam_result_t camera_detect_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    camera_source_config_t cc;
    json_config_get_camera_config(&cc);

    int runtime_uvc2 = usbh_uvc_service_is_active();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "active", runtime_uvc2 ? "uvc" : "native");
    cJSON_AddStringToObject(response, "saved_source",
                             cc.source == CAMERA_SOURCE_UVC ? "uvc" : "native");
    cJSON_AddBoolToObject(response, "restart_required",
                           runtime_uvc2 != (cc.source == CAMERA_SOURCE_UVC));
    cJSON *cameras = cJSON_CreateArray();

    if (cc.source == CAMERA_SOURCE_UVC) {
        /* active camera: report live status; cross-probe the native camera */
        cJSON_AddItemToArray(cameras, native_camera_entry(AICAM_TRUE));
        cJSON_AddItemToArray(cameras, uvc_camera_entry(AICAM_FALSE));
    } else {
        /* active camera: native already initialized; probe UVC then recycle */
        cJSON_AddItemToArray(cameras, native_camera_entry(AICAM_FALSE));
        cJSON_AddItemToArray(cameras, uvc_camera_entry(AICAM_TRUE));
        usbh_uvc_service_probe_release();
    }
    cJSON_AddItemToObject(response, "cameras", cameras);
    cJSON_AddItemToObject(response, "uvc_status", uvc_status_entry());

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize");
    }
    return api_response_success(ctx, json_str, "Camera detection complete");
}

static aicam_result_t camera_config_get_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "GET")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    camera_source_config_t cc;
    json_config_get_camera_config(&cc);

    int runtime_uvc3 = usbh_uvc_service_is_active();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "source",
                             cc.source == CAMERA_SOURCE_UVC ? "uvc" : "native");
    cJSON_AddStringToObject(response, "active_source", runtime_uvc3 ? "uvc" : "native");
    cJSON_AddNumberToObject(response, "native_stream_res", cc.native_stream_res);
    cJSON *uvc = cJSON_CreateObject();
    cJSON_AddNumberToObject(uvc, "width", cc.uvc_stream_width);
    cJSON_AddNumberToObject(uvc, "height", cc.uvc_stream_height);
    cJSON_AddNumberToObject(uvc, "fps", cc.uvc_stream_fps);
    cJSON_AddItemToObject(response, "uvc_stream", uvc);
    cJSON_AddBoolToObject(response, "restart_required", 1);

    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize");
    }
    return api_response_success(ctx, json_str, "Camera config retrieved");
}

static aicam_result_t camera_config_set_handler(http_handler_context_t* ctx)
{
    if (!web_api_verify_method(ctx, "PUT") && !web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    cJSON *request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    camera_source_config_t cc;
    if (json_config_get_camera_config(&cc) != AICAM_OK) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Config unavailable");
    }

    cJSON *source = cJSON_GetObjectItem(request, "source");
    if (cJSON_IsString(source)) {
        if (strcmp(source->valuestring, "native") == 0) {
            cc.source = CAMERA_SOURCE_NATIVE;
        } else if (strcmp(source->valuestring, "uvc") == 0) {
            cc.source = CAMERA_SOURCE_UVC;
        } else {
            cJSON_Delete(request);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid source");
        }
    }

    cJSON *res = cJSON_GetObjectItem(request, "native_stream_res");
    if (cJSON_IsNumber(res)) {
        uint32_t r = (uint32_t)res->valuedouble;
        if (r != CAMERA_NATIVE_RES_720P && r != CAMERA_NATIVE_RES_1080P) {
            cJSON_Delete(request);
            return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid native_stream_res");
        }
        cc.native_stream_res = r;
    }

    cJSON *uvc = cJSON_GetObjectItem(request, "uvc_stream");
    if (cJSON_IsObject(uvc)) {
        cJSON *w = cJSON_GetObjectItem(uvc, "width");
        cJSON *h = cJSON_GetObjectItem(uvc, "height");
        cJSON *f = cJSON_GetObjectItem(uvc, "fps");
        if (cJSON_IsNumber(w)) cc.uvc_stream_width = (uint32_t)w->valuedouble;
        if (cJSON_IsNumber(h)) cc.uvc_stream_height = (uint32_t)h->valuedouble;
        if (cJSON_IsNumber(f)) cc.uvc_stream_fps = (uint32_t)f->valuedouble;
    }
    cJSON_Delete(request);

    aicam_result_t r = json_config_set_camera_config(&cc);
    if (r != AICAM_OK) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid camera config");
    }

    /* UVC mode cannot push RTMP/RTSP (MJPEG-only source): disable both so
     * the boot-time auto-start doesn't spin on a missing H.264 stream. */
    if (cc.source == CAMERA_SOURCE_UVC) {
        work_mode_config_t wm;
        if (json_config_get_work_mode_config(&wm) == AICAM_OK &&
            (wm.video_stream_mode.rtmp_enable || wm.video_stream_mode.rtsp_enable)) {
            wm.video_stream_mode.rtmp_enable = AICAM_FALSE;
            wm.video_stream_mode.rtsp_enable = AICAM_FALSE;
            (void)json_config_set_work_mode_config(&wm);
        }
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "restart_required", 1);
    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    if (!json_str) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to serialize");
    }
    return api_response_success(ctx, json_str, "Camera config saved, reboot to apply");
}

/* ==================== API Module Registration ==================== */

aicam_result_t web_api_register_camera_module(void)
{
    api_route_t routes[] = {
        { .path = API_PATH_PREFIX"/camera/list",   .method = "GET",  .handler = camera_list_handler,        .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/camera/detect", .method = "POST", .handler = camera_detect_handler,      .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/camera/config", .method = "GET",  .handler = camera_config_get_handler,  .require_auth = AICAM_TRUE },
        { .path = API_PATH_PREFIX"/camera/config", .method = "PUT",  .handler = camera_config_set_handler,  .require_auth = AICAM_TRUE },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register camera route: %s", routes[i].path);
            return result;
        }
    }

    LOG_SVC_INFO("Camera source API module registered");
    return AICAM_OK;
}
