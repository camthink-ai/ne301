#include "quick_bootstrap.h"
#include "quick_storage.h"
#include "quick_snapshot.h"
#include "quick_network.h"

#include "cmsis_os2.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "debug.h"
#include "drtc.h"
#include "cJSON.h"
#include "buffer_mgr.h"
#include "dev_manager.h"
#include "jpegc.h"
#include "nn.h"
#include "mbedtls/base64.h"
#include "pwr.h"
#include "u0_module.h"
#include "misc.h"
#include "version.h"
#include "common_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Mutex + vprintf for [QB]/QT_TRACE; newlib printf is not re-entrant across threads. */
static osMutexId_t s_qb_printf_mutex;

void quick_log_mutex_init(void)
{
    if (s_qb_printf_mutex != NULL) {
        return;
    }
    s_qb_printf_mutex = osMutexNew(NULL);
}

void quick_log_printf(const char *fmt, ...)
{
    va_list ap;

    if (s_qb_printf_mutex == NULL) {
        quick_log_mutex_init();
    }

    va_start(ap, fmt);
    if (s_qb_printf_mutex != NULL) {
        (void)osMutexAcquire(s_qb_printf_mutex, osWaitForever);
    }
    (void)vprintf(fmt, ap);
    (void)fflush(stdout);
    if (s_qb_printf_mutex != NULL) {
        (void)osMutexRelease(s_qb_printf_mutex);
    }
    va_end(ap);
}

/* quick_bootstrap:
 * - main path: init components -> wait cfg -> optional upload -> wait tasks done/timeout -> sleep
 * - event thread: consume quick_snapshot event flags once, enqueue storage tasks, notify main thread
 */

#if QB_TRACE_ENABLE && (QB_LOG_LEVEL >= QB_LOG_LEVEL_DEBUG)
#define QB_TRACE(fmt, ...)  QB_LOGD(fmt, ##__VA_ARGS__)
#else
#define QB_TRACE(fmt, ...)  do { } while (0)
#endif

/* Step timing logs are gated independently to avoid extra rtc_get_uptime_ms() calls. */
#ifndef QB_HAVE_NOW_MS
#define QB_HAVE_NOW_MS 1
/* Always-available monotonic time in ms for internal timeouts.
 * Prefer RTC uptime if present; otherwise fallback to kernel ticks.
 */
static inline uint64_t qb_now_ms(void)
{
    /* CMSIS-RTOS2 tick count is monotonic while kernel is running. */
    uint32_t tick = osKernelGetTickCount();
    uint32_t hz = osKernelGetTickFreq();
    if (hz == 0U) return (uint64_t)tick;
    return ((uint64_t)tick * 1000ULL) / (uint64_t)hz;
}
#endif

#if QB_TIMELOG_ENABLE && (QB_LOG_LEVEL >= QB_LOG_LEVEL_DEBUG)
typedef struct {
    uint64_t t0_ms;
    uint64_t last_ms;
} qb_prof_t;

static inline void qb_prof_init(qb_prof_t *p)
{
    if (!p) return;
    p->t0_ms = qb_now_ms();
    p->last_ms = p->t0_ms;
}

static inline void qb_prof_step(qb_prof_t *p, const char *tag)
{
    if (!p || !tag) return;
    uint64_t now = qb_now_ms();
    QB_LOGD("%s +%lums (%lums)",
            tag,
            (unsigned long)(now - p->last_ms),
            (unsigned long)(now - p->t0_ms));
    p->last_ms = now;
}
#else
typedef struct {
    uint8_t _dummy;
} qb_prof_t;
#define qb_prof_init(p)        do { (void)(p); } while (0)
#define qb_prof_step(p, tag)   do { (void)(p); (void)(tag); } while (0)
#endif

/* event flags for bootstrap main thread */
#define QB_FLAG_JPEG_SEEN        (1UL << 0)
#define QB_FLAG_AI_RESULT_SEEN   (1UL << 1)
#define QB_FLAG_AI_JPEG_SEEN     (1UL << 2)
#define QB_FLAG_ABORT            (1UL << 24)

#define QB_FLAG_STORE_JPEG_DONE      (1UL << 8)
#define QB_FLAG_STORE_AI_JSON_DONE   (1UL << 9)
#define QB_FLAG_STORE_AI_JPEG_DONE   (1UL << 10)
#define QB_FLAG_MQTT_PUB_DONE        (1UL << 11)

#define QB_ALL_DONE_MASK (QB_FLAG_STORE_JPEG_DONE | QB_FLAG_STORE_AI_JSON_DONE | QB_FLAG_STORE_AI_JPEG_DONE | QB_FLAG_MQTT_PUB_DONE)

typedef struct {
    uint64_t wake_ts;         /* global wake timestamp */
    uint64_t wake_uptime_ms;  /* monotonic: used for interval scheduling */
    uint32_t frame_id;        /* best-effort per-wakeup unique id */
    qb_wakeup_source_type_t wake_src;

    qs_snapshot_config_t snap_cfg;
    qs_work_mode_config_t work_cfg;
    qs_comm_pref_type_t comm_pref;

    /* capture outputs */
    uint8_t *jpeg;
    size_t jpeg_len;
    aicam_bool_t jpeg_need_free; /* free at finalize if not handed to storage callback */
    uint8_t *ai_jpeg;
    size_t ai_jpeg_len;
    aicam_bool_t ai_jpeg_need_free; /* free at finalize if not handed to storage callback */
    nn_result_t ai_result;
    aicam_bool_t ai_result_valid;
    nn_model_info_t model_info;
    aicam_bool_t model_info_valid;

    /* sync */
    osEventFlagsId_t evt;

    /* pending task bookkeeping */
    volatile uint32_t pending_store;
    volatile uint32_t pending_mqtt;
} qb_ctx_t;

static qb_ctx_t s_qb;
static osThreadId_t s_evt_tid = NULL;

static inline aicam_bool_t qb_is_single_bit(uint32_t v)
{
    return (v != 0U && ((v & (v - 1U)) == 0U)) ? AICAM_TRUE : AICAM_FALSE;
}

static void qb_wait_done_or_abort(uint32_t done_mask, uint32_t timeout_ms)
{
    if (!s_qb.evt || done_mask == 0U) return;

    /* Fast path: only one flag to wait. ABORT must return immediately. */
    if (qb_is_single_bit(done_mask)) {
        (void)osEventFlagsWait(s_qb.evt,
                               done_mask | QB_FLAG_ABORT,
                               osFlagsWaitAny | osFlagsNoClear,
                               timeout_ms);
        return;
    }

    /* Need multiple flags to be set: use WaitAny loop so ABORT is immediate. */
    uint64_t t0 = qb_now_ms();
    for (;;) {
        uint32_t have = osEventFlagsGet(s_qb.evt);
        if ((have & QB_FLAG_ABORT) != 0U) return;
        if ((have & done_mask) == done_mask) return;

        uint32_t elapsed = (uint32_t)(qb_now_ms() - t0);
        if (elapsed >= timeout_ms) return;
        uint32_t remain = timeout_ms - elapsed;

        uint32_t missing = done_mask & ~have;
        (void)osEventFlagsWait(s_qb.evt,
                               missing | QB_FLAG_ABORT,
                               osFlagsWaitAny | osFlagsNoClear,
                               remain);
    }
}

static device_t *qb_find_jpeg_dev(void)
{
    return device_find_pattern(JPEG_DEVICE_NAME, DEV_TYPE_VIDEO);
}

static void qb_free_jpegc_buffer(void *buf)
{
    if (!buf) return;
    device_t *jpeg = qb_find_jpeg_dev();
    if (jpeg) {
        (void)device_ioctl(jpeg, JPEGC_CMD_FREE_ENC_BUFFER, (uint8_t *)buf, 0);
    }
}

static void qb_store_cb(int result, void *param)
{
    (void)result;
    uint32_t flag = (uint32_t)(uintptr_t)param;
    if (s_qb.evt) {
        (void)osEventFlagsSet(s_qb.evt, flag);
    }
    if (s_qb.pending_store > 0) {
        s_qb.pending_store--;
    }
}

static void qb_store_jpeg_done_cb(int result, void *param)
{
    qb_free_jpegc_buffer(param);
    qb_store_cb(result, (void *)(uintptr_t)QB_FLAG_STORE_JPEG_DONE);
}

static void qb_store_ai_jpeg_done_cb(int result, void *param)
{
    qb_free_jpegc_buffer(param);
    qb_store_cb(result, (void *)(uintptr_t)QB_FLAG_STORE_AI_JPEG_DONE);
}

static void qb_store_ai_json_done_cb(int result, void *param)
{
    if (param) {
        cJSON_free(param);
    }
    qb_store_cb(result, (void *)(uintptr_t)QB_FLAG_STORE_AI_JSON_DONE);
}

static void qb_mqtt_done_cb(int result, void *param)
{
    if (param) {
        cJSON_free(param);
    }
    if (s_qb.evt) {
        if (result != 0) {
            (void)osEventFlagsSet(s_qb.evt, QB_FLAG_MQTT_FAILED);
            QB_LOGW("mqtt publish finished with error %d (see quick_network_get_mqtt_result)", result);
        }
        (void)osEventFlagsSet(s_qb.evt, QB_FLAG_MQTT_PUB_DONE);
    }
    if (s_qb.pending_mqtt > 0) {
        s_qb.pending_mqtt--;
    }
}

static void qb_get_pipe1_wh(const qs_snapshot_config_t *cfg, uint32_t *w, uint32_t *h)
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

static aicam_capture_trigger_t qb_map_trigger(qb_wakeup_source_type_t src)
{
    switch (src) {
        case QB_WAKEUP_SOURCE_TIMER:  return AICAM_CAPTURE_TRIGGER_RTC;
        case QB_WAKEUP_SOURCE_PIR:    return AICAM_CAPTURE_TRIGGER_PIR;
        case QB_WAKEUP_SOURCE_BUTTON: return AICAM_CAPTURE_TRIGGER_BUTTON;
        default:                      return AICAM_CAPTURE_TRIGGER_MAX;
    }
}

static const char *qb_trigger_str(aicam_capture_trigger_t t)
{
    switch (t) {
        case AICAM_CAPTURE_TRIGGER_RTC:      return "rtc";
        case AICAM_CAPTURE_TRIGGER_PIR:      return "pir";
        case AICAM_CAPTURE_TRIGGER_WEB:      return "web";
        case AICAM_CAPTURE_TRIGGER_REMOTE:   return "remote";
        case AICAM_CAPTURE_TRIGGER_GPIO:     return "gpio";
        case AICAM_CAPTURE_TRIGGER_BUTTON:   return "button";
        case AICAM_CAPTURE_TRIGGER_SCHEDULE: return "schedule";
        default:                             return "unknown";
    }
}

static const char *qb_comm_type_str(qs_comm_pref_type_t t)
{
    switch (t) {
        case COMM_PREF_TYPE_WIFI: return "wifi";
        case COMM_PREF_TYPE_CELLULAR: return "cellular";
        case COMM_PREF_TYPE_POE: return "poe";
        case COMM_PREF_TYPE_AUTO: return "auto";
        case COMM_PREF_TYPE_DISABLE: return "none";
        default: return "unknown";
    }
}

static void qb_get_power_and_battery(char power_supply_type[32], float *battery_percent)
{
    if (power_supply_type) power_supply_type[0] = '\0';
    if (battery_percent) *battery_percent = 0.0f;

#if ENABLE_U0_MODULE
    uint32_t usbin_status = 0;
    if (u0_module_get_usbin_value(&usbin_status) == 0 && usbin_status == 1) {
        if (power_supply_type) (void)snprintf(power_supply_type, 32, "full-power");
        if (battery_percent) *battery_percent = 0.0f;
        return;
    }
#endif

    device_t *battery_device = device_find_pattern(BATTERY_DEVICE_NAME, DEV_TYPE_MISC);
    if (!battery_device) {
        if (power_supply_type) (void)snprintf(power_supply_type, 32, "-");
        if (battery_percent) *battery_percent = 0.0f;
        return;
    }

    uint8_t rate = 0;
    int ret = -1;
    for (int retry = 0; retry < 3; retry++) {
        ret = device_ioctl(battery_device, MISC_CMD_ADC_GET_PERCENT, (uint8_t *)&rate, 0);
        if (ret == 0) break;
        osDelay(50);
    }
    if (ret != 0) {
        if (power_supply_type) (void)snprintf(power_supply_type, 32, "-");
        if (battery_percent) *battery_percent = 0.0f;
        return;
    }

    float pct = (float)rate;
    if (pct > 100.0f) {
        pct = 0.0f;
        if (power_supply_type) (void)snprintf(power_supply_type, 32, "full-power");
    } else {
        if (power_supply_type) (void)snprintf(power_supply_type, 32, "battery");
    }
    if (battery_percent) *battery_percent = pct;
}

static void qb_generate_image_id(char image_id[64], const char *prefix)
{
    if (!image_id) return;
    uint64_t ts = rtc_get_timeStamp();
    if (prefix && prefix[0] != '\0') {
        (void)snprintf(image_id, 64, "%s_%lu", prefix, (unsigned long)ts);
    } else {
        (void)snprintf(image_id, 64, "img_%lu", (unsigned long)ts);
    }
}

static aicam_bool_t qb_ai_has_detections(const nn_result_t *r);

static int qb_build_ai_result_json_str(char **out_str, uint32_t *out_len)
{
    if (!out_str || !out_len) return AICAM_ERROR_INVALID_PARAM;
    *out_str = NULL;
    *out_len = 0;

    /* Align with upper layer `generate_inference_json()`:
     * - only generate when there are detections
     * - JSON is exactly nn_create_ai_result_json(nn_result)
     * - use cJSON_Print (formatted)
     */
    if (!qb_ai_has_detections(&s_qb.ai_result)) {
        return AICAM_ERROR_UNAVAILABLE;
    }

    cJSON *json_obj = nn_create_ai_result_json(&s_qb.ai_result);
    if (!json_obj) return AICAM_ERROR;

    char *s = cJSON_Print(json_obj);
    cJSON_Delete(json_obj);
    if (!s) return AICAM_ERROR_NO_MEMORY;

    *out_str = s;
    *out_len = (uint32_t)strlen(s);
    return 0;
}

static int qb_build_mqtt_payload(char **out_json, size_t *out_len)
{
    if (!out_json || !out_len) return AICAM_ERROR_INVALID_PARAM;
    *out_json = NULL;
    *out_len = 0;

    if (!s_qb.jpeg || s_qb.jpeg_len == 0) return AICAM_ERROR_INVALID_DATA;

    uint32_t w = 0, h = 0;
    qb_get_pipe1_wh(&s_qb.snap_cfg, &w, &h);

    /* base64 encode with "data:image/jpeg;base64," prefix (aligned with mqtt_service.c) */
    const char *prefix = "data:image/jpeg;base64,";
    const size_t prefix_len = strlen(prefix);
    /* base64 length is predictable: 4*ceil(n/3). Add some spare bytes. */
    const size_t b64_len = ((s_qb.jpeg_len + 2U) / 3U) * 4U;
    const size_t spare = 8U;
    char *image_b64 = (char *)buffer_calloc(1, prefix_len + b64_len + spare + 1U);
    if (!image_b64) return AICAM_ERROR_NO_MEMORY;
    memcpy(image_b64, prefix, prefix_len);
    size_t out_b64 = 0;
    int rc = mbedtls_base64_encode((unsigned char *)(image_b64 + prefix_len), b64_len + spare, &out_b64, s_qb.jpeg, s_qb.jpeg_len);
    if (rc != 0) {
        buffer_free(image_b64);
        return AICAM_ERROR;
    }
    image_b64[prefix_len + out_b64] = '\0';

    /* JSON root (aligned with mqtt_service_publish_image_with_ai) */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        buffer_free(image_b64);
        return AICAM_ERROR_NO_MEMORY;
    }

    cJSON *meta = cJSON_CreateObject();
    if (meta) {
        char image_id[64] = {0};
        qb_generate_image_id(image_id, "cam01");

        uint64_t ts = rtc_get_timeStamp();
        if (ts == 0) ts = s_qb.wake_ts;

        cJSON_AddStringToObject(meta, "image_id", image_id);
        cJSON_AddNumberToObject(meta, "timestamp", (double)ts);
        cJSON_AddStringToObject(meta, "format", "jpeg");
        cJSON_AddNumberToObject(meta, "width", (double)w);
        cJSON_AddNumberToObject(meta, "height", (double)h);
        cJSON_AddNumberToObject(meta, "size", (double)s_qb.jpeg_len);
        uint32_t q = s_qb.snap_cfg.fast_capture_jpeg_quality ? s_qb.snap_cfg.fast_capture_jpeg_quality : 60U;
        cJSON_AddNumberToObject(meta, "quality", (double)q);
        cJSON_AddStringToObject(meta, "trigger_type", qb_trigger_str(qb_map_trigger(s_qb.wake_src)));
        cJSON_AddItemToObject(root, "metadata", meta);
    }

    qs_device_info_t dev = {0};
    (void)quick_storage_read_device_info(&dev);
    cJSON *dev_json = cJSON_CreateObject();
    if (dev_json) {
        /* Keep keys aligned with mqtt_service.c/create_device_info_json (missing fields left empty/0). */
        cJSON_AddStringToObject(dev_json, "device_name", dev.device_name);
        cJSON_AddStringToObject(dev_json, "mac_address", dev.mac_address);
        cJSON_AddStringToObject(dev_json, "serial_number", dev.serial_number);
        cJSON_AddStringToObject(dev_json, "hardware_version", dev.hardware_version);
        cJSON_AddStringToObject(dev_json, "software_version", FW_VERSION_STRING);
        char power_supply_type[32] = {0};
        float battery_percent = 0.0f;
        qb_get_power_and_battery(power_supply_type, &battery_percent);
        cJSON_AddStringToObject(dev_json, "power_supply_type", power_supply_type[0] ? power_supply_type : "-");
        cJSON_AddNumberToObject(dev_json, "battery_percent", battery_percent);
        quick_network_wait_config(&s_qb.comm_pref);
        cJSON_AddStringToObject(dev_json, "communication_type", qb_comm_type_str(s_qb.comm_pref));
        cJSON_AddItemToObject(root, "device_info", dev_json);
    }

    if (s_qb.ai_result_valid) {
        cJSON *ai = cJSON_CreateObject();
        if (ai) {
            cJSON_AddStringToObject(ai, "model_name", s_qb.model_info_valid ? s_qb.model_info.name : "unknown");
            cJSON_AddStringToObject(ai, "model_version", s_qb.model_info_valid ? s_qb.model_info.version : "1.0");
            uint32_t inference_time_ms = 0;
            (void)quick_snapshot_get_ai_inference_time_ms(&inference_time_ms);
            cJSON_AddNumberToObject(ai, "inference_time_ms", (double)inference_time_ms);
            cJSON_AddNumberToObject(ai, "confidence_threshold", (double)s_qb.snap_cfg.confidence_threshold);
            cJSON_AddNumberToObject(ai, "nms_threshold", (double)s_qb.snap_cfg.nms_threshold);
            cJSON *ai_res = nn_create_ai_result_json(&s_qb.ai_result);
            if (ai_res) cJSON_AddItemToObject(ai, "ai_result", ai_res);
            cJSON_AddItemToObject(root, "ai_result", ai);
        }
    } else {
        cJSON_AddItemToObject(root, "ai_result", cJSON_CreateNull());
    }

    cJSON_AddStringToObject(root, "image_data", image_b64);
    cJSON_AddStringToObject(root, "encoding", "base64");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    buffer_free(image_b64);

    if (!json_str) return AICAM_ERROR_NO_MEMORY;
    *out_json = json_str;
    *out_len = strlen(json_str);
    return 0;
}

static void qb_event_thread(void *argument)
{
    (void)argument;

    qb_prof_t prof;
    qb_prof_init(&prof);
    QB_TRACE("evt start");
    qb_prof_step(&prof, "evt:start");

    uint32_t handled = 0;
    const uint32_t all_once = (QS_FLAG_JPEG_READY | QS_FLAG_AI_RESULT_READY | QS_FLAG_AI_JPEG_READY);

    for (;;) {
        /* quick_snapshot_wait_event uses osFlagsNoClear, so we must NOT wait on already-handled flags,
         * otherwise the wait returns immediately and we spin.
         */
        uint32_t wait_mask = (all_once & ~handled) | QS_FLAG_ERROR_ABORT;
        if ((wait_mask & all_once) == 0) {
            /* All one-shot events are handled; exit. */
            osThreadExit();
            return;
        }

        uint32_t flags = quick_snapshot_wait_event(wait_mask, osWaitForever);
        if (flags & QS_FLAG_ERROR_ABORT) {
            QB_TRACE("evt abort");
            qb_prof_step(&prof, "evt:abort");
            if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_ABORT);
            osThreadExit();
            return;
        }

        if ((flags & QS_FLAG_JPEG_READY) && ((handled & QS_FLAG_JPEG_READY) == 0)) {
            handled |= QS_FLAG_JPEG_READY;
            qb_prof_step(&prof, "evt:jpeg");
            uint8_t *jpeg = NULL;
            size_t jpeg_len = 0;
            if (quick_snapshot_wait_capture_jpeg(&jpeg, &jpeg_len) == 0 && jpeg && jpeg_len > 0) {
                s_qb.jpeg = jpeg;
                s_qb.jpeg_len = jpeg_len;
                s_qb.jpeg_need_free = AICAM_TRUE;
                if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_JPEG_SEEN);
                QB_TRACE("jpeg %luB", (unsigned long)jpeg_len);

                /* Align frame_id with camera pipeline frame id */
                uint32_t fid = 0;
                if (quick_snapshot_get_frame_id(&fid) == 0) {
                    s_qb.frame_id = fid;
                }

                /* store original jpeg (async) */
                qs_write_task_param_t w = {0};
                w.mode = 1; /* overwrite */
                w.disk_type = 0; /* auto */
                (void)snprintf(w.file_name, sizeof(w.file_name), "image_%lu_%lu.jpg",
                               (unsigned long)s_qb.wake_ts, (unsigned long)s_qb.frame_id);
                w.data = jpeg;
                w.data_len = jpeg_len;
                w.callback = qb_store_jpeg_done_cb;
                w.callback_param = jpeg; /* free jpegc buffer after write */
                if (quick_storage_add_write_task(&w) == 0) {
                    s_qb.pending_store++;
                    /* storage callback will free jpeg buffer */
                    s_qb.jpeg_need_free = AICAM_FALSE;
                    QB_TRACE("store jpeg queued");
                } else {
                    /* if enqueue failed, defer free to finalize (avoid freeing shared/reused buffers mid-flight) */
                    if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_STORE_JPEG_DONE);
                    QB_TRACE("store jpeg enqueue fail");
                }
            } else {
                QB_TRACE("jpeg wait fail");
                if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_ABORT);
            }
        }

        if ((flags & QS_FLAG_AI_RESULT_READY) && ((handled & QS_FLAG_AI_RESULT_READY) == 0)) {
            handled |= QS_FLAG_AI_RESULT_READY;
            qb_prof_step(&prof, "evt:ai_res");
            nn_result_t r = {0};
            if (quick_snapshot_wait_ai_result(&r) == 0) {
                s_qb.ai_result = r;
                s_qb.ai_result_valid = r.is_valid ? AICAM_TRUE : AICAM_FALSE;

                nn_model_info_t mi = {0};
                if (quick_snapshot_wait_ai_info(&mi) == 0) {
                    s_qb.model_info = mi;
                    s_qb.model_info_valid = (mi.input_width > 0 && mi.input_height > 0) ? AICAM_TRUE : AICAM_FALSE;
                }

                if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_AI_RESULT_SEEN);
                QB_TRACE("ai res valid=%u type=%d", (unsigned int)s_qb.ai_result_valid, (int)r.type);

                /* Ensure frame_id is aligned even if AI result arrives before JPEG. */
                if (s_qb.frame_id == 0) {
                    uint32_t fid = 0;
                    if (quick_snapshot_get_frame_id(&fid) == 0) {
                        s_qb.frame_id = fid;
                    }
                }

                /* store ai json (async) - align with upper layer: only when detections > 0 */
                if (qb_ai_has_detections(&s_qb.ai_result)) {
                    char *ai_json = NULL;
                    uint32_t ai_json_len = 0;
                    if (qb_build_ai_result_json_str(&ai_json, &ai_json_len) == 0 && ai_json && ai_json_len > 0) {
                        qs_write_task_param_t w = {0};
                        w.mode = 1;
                        w.disk_type = 0;
                        (void)snprintf(w.file_name, sizeof(w.file_name), "image_%lu_%lu_inference.json",
                                       (unsigned long)s_qb.wake_ts, (unsigned long)s_qb.frame_id);
                        w.data = ai_json;
                        w.data_len = (size_t)ai_json_len;
                        w.callback = qb_store_ai_json_done_cb;
                        w.callback_param = ai_json; /* free via cJSON_free */
                        if (quick_storage_add_write_task(&w) == 0) {
                            s_qb.pending_store++;
                            QB_TRACE("store ai json queued");
                        } else {
                            cJSON_free(ai_json);
                            if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_STORE_AI_JSON_DONE);
                            QB_TRACE("store ai json enqueue fail");
                        }
                    } else {
                        if (ai_json) cJSON_free(ai_json);
                        if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_STORE_AI_JSON_DONE);
                    }
                } else {
                    if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_STORE_AI_JSON_DONE);
                }
            } else {
                QB_TRACE("ai res wait fail");
                if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_ABORT);
            }
        }

        if ((flags & QS_FLAG_AI_JPEG_READY) && ((handled & QS_FLAG_AI_JPEG_READY) == 0)) {
            handled |= QS_FLAG_AI_JPEG_READY;
            qb_prof_step(&prof, "evt:ai_jpeg");
            uint8_t *aj = NULL;
            size_t aj_len = 0;
            if (quick_snapshot_wait_ai_jpeg(&aj, &aj_len) == 0 && aj && aj_len > 0) {
                s_qb.ai_jpeg = aj;
                s_qb.ai_jpeg_len = aj_len;
                s_qb.ai_jpeg_need_free = AICAM_TRUE;
                if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_AI_JPEG_SEEN);
                QB_TRACE("ai jpeg %luB", (unsigned long)aj_len);

                /* Ensure frame_id is aligned even if AI JPEG arrives before JPEG event processed. */
                if (s_qb.frame_id == 0) {
                    uint32_t fid = 0;
                    if (quick_snapshot_get_frame_id(&fid) == 0) {
                        s_qb.frame_id = fid;
                    }
                }

                /* store ai jpeg - align with upper layer: only when detections > 0 */
                if (qb_ai_has_detections(&s_qb.ai_result)) {
                    qs_write_task_param_t w = {0};
                    w.mode = 1;
                    w.disk_type = 0;
                    (void)snprintf(w.file_name, sizeof(w.file_name), "image_%lu_%lu_inference.jpg",
                                   (unsigned long)s_qb.wake_ts, (unsigned long)s_qb.frame_id);
                    w.data = aj;
                    w.data_len = aj_len;
                    w.callback = qb_store_ai_jpeg_done_cb;
                    w.callback_param = aj; /* free jpegc buffer after write */
                    if (quick_storage_add_write_task(&w) == 0) {
                        s_qb.pending_store++;
                        /* storage callback will free ai jpeg buffer */
                        s_qb.ai_jpeg_need_free = AICAM_FALSE;
                        QB_TRACE("store ai jpeg queued");
                    } else {
                        /* defer free to finalize */
                        if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_STORE_AI_JPEG_DONE);
                        QB_TRACE("store ai jpeg enqueue fail");
                    }
                } else {
                    /* Not storing; defer free to finalize. */
                    if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_STORE_AI_JPEG_DONE);
                }
            } else {
                QB_TRACE("ai jpeg wait fail");
                if (s_qb.evt) (void)osEventFlagsSet(s_qb.evt, QB_FLAG_ABORT);
            }
        }

        if ((handled & (QS_FLAG_JPEG_READY | QS_FLAG_AI_RESULT_READY | QS_FLAG_AI_JPEG_READY)) ==
            (QS_FLAG_JPEG_READY | QS_FLAG_AI_RESULT_READY | QS_FLAG_AI_JPEG_READY)) {
            /* All one-shot events processed; keep thread alive for error abort only, or exit. */
            osThreadExit();
            return;
        }
    }
}

static aicam_bool_t qb_ai_has_detections(const nn_result_t *r)
{
    if (!r) return AICAM_FALSE;
    if (!r->is_valid) return AICAM_FALSE;
    if (r->type == PP_TYPE_OD) return (r->od.nb_detect > 0) ? AICAM_TRUE : AICAM_FALSE;
    if (r->type == PP_TYPE_MPE) return (r->mpe.nb_detect > 0) ? AICAM_TRUE : AICAM_FALSE;
    if (r->type == PP_TYPE_SPE) return (r->spe.nb_keypoints > 0) ? AICAM_TRUE : AICAM_FALSE;
    return AICAM_FALSE;
}

static int qb_configure_pir_from_workcfg(const qs_work_mode_config_t *cfg)
{
    if (!cfg) return AICAM_ERROR_INVALID_PARAM;
    if (!cfg->pir_trigger_enabled) return 0;

    /* Keep consistent with system_service.c/configure_pir_sensor (no service/controller dependency). */
    ms_bridging_pir_cfg_t pir_cfg;
    memset(&pir_cfg, 0, sizeof(pir_cfg));

    pir_cfg.sensitivity_level = cfg->pir_trigger_sensitivity;
    if (pir_cfg.sensitivity_level == 0) pir_cfg.sensitivity_level = 30;

    pir_cfg.ignore_time_s = cfg->pir_trigger_ignore_time;
    if (pir_cfg.ignore_time_s == 0) pir_cfg.ignore_time_s = 7;

    pir_cfg.pulse_count = cfg->pir_trigger_pulse_count;
    if (pir_cfg.pulse_count == 0) pir_cfg.pulse_count = 1;

    pir_cfg.window_time_s = cfg->pir_trigger_window_time;
    pir_cfg.motion_enable = 1;
    pir_cfg.interrupt_src = 0;
    pir_cfg.volt_select = 0;

    int ret = u0_module_cfg_pir(&pir_cfg);
    return (ret == 0) ? 0 : AICAM_ERROR;
}

static uint32_t qb_calc_next_abs_sleep_sec(const qs_work_mode_config_t *c)
{
    if (!c) return 0;
    if (!c->timer_trigger_enabled) return 0;
    if (c->timer_trigger_time_node_count == 0) return 0;
    if (c->timer_trigger_time_node_count > 10) return 0;

    /* Use local timestamp for schedule decisions (matches UI expectation). */
    uint64_t now_ts = rtc_get_local_timestamp();
    if (now_ts == 0) now_ts = rtc_get_timeStamp();
    if (now_ts == 0) return 0;

    RTC_TIME_S now;
    memset(&now, 0, sizeof(now));
    timeStamp_to_time(now_ts, &now);

    /* dayOfWeek: treat 1..7 (Mon..Sun) if available; if 0, treat as unknown -> allow all. */
    uint8_t dow = now.dayOfWeek; /* expected 1..7 */

    uint32_t now_sod = (uint32_t)now.hour * 3600U + (uint32_t)now.minute * 60U + (uint32_t)now.second;

    uint32_t best = 0;
    aicam_bool_t have = AICAM_FALSE;

    for (uint32_t i = 0; i < c->timer_trigger_time_node_count; i++) {
        uint32_t node = c->timer_trigger_time_node[i];
        if (node >= 24U * 3600U) continue;

        uint8_t w = c->timer_trigger_weekdays[i]; /* 0=all, 1=Mon..7=Sun */
        for (uint32_t add_day = 0; add_day < 8; add_day++) {
            aicam_bool_t ok_day = AICAM_TRUE;
            if (w != 0 && dow != 0) {
                uint8_t cand = (uint8_t)(((dow - 1U + add_day) % 7U) + 1U);
                ok_day = (cand == w) ? AICAM_TRUE : AICAM_FALSE;
            } else if (w != 0 && dow == 0) {
                ok_day = AICAM_TRUE;
            }
            if (!ok_day) continue;

            int32_t delta = (int32_t)node - (int32_t)now_sod;
            if (add_day == 0 && delta <= 0) {
                continue;
            }
            if (add_day > 0) {
                delta += (int32_t)(add_day * 24U * 3600U);
            }
            if (delta <= 0) continue;

            uint32_t d = (uint32_t)delta;
            if (!have || d < best) {
                best = d;
                have = AICAM_TRUE;
            }
            break; /* for this node, earliest matching day found */
        }
    }

    return have ? best : 0;
}

static void qb_build_u0_wakeup_and_power(const qs_work_mode_config_t *cfg,
                                        aicam_bool_t remote_wakeup_ok,
                                        uint32_t *out_wakeup_flags,
                                        uint32_t *out_switch_bits,
                                        uint32_t *out_sleep_sec)
{
    uint32_t wakeup_flags = 0;
    uint32_t switch_bits = 0;
    uint32_t sleep_sec = 0;

    /* Align with system_service low-power defaults: RTC timing + config key. */
    wakeup_flags |= PWR_WAKEUP_FLAG_CONFIG_KEY;

    if (cfg) {
        /* Timer trigger (interval only in quick path). */
        if (cfg->timer_trigger_enabled && cfg->timer_trigger_capture_mode == AICAM_TIMER_CAPTURE_MODE_INTERVAL) {
            if (cfg->timer_trigger_interval_sec > 0) {
                wakeup_flags |= PWR_WAKEUP_FLAG_RTC_TIMING;
                /* Interval is defined from wake moment (not from "enter sleep"),
                 * so compensate current work time to avoid drift.
                 */
                uint64_t now_ms = rtc_get_uptime_ms();
                uint64_t elapsed_ms = (now_ms >= s_qb.wake_uptime_ms) ? (now_ms - s_qb.wake_uptime_ms) : 0ULL;
                uint64_t interval_ms = (uint64_t)cfg->timer_trigger_interval_sec * 1000ULL;
                uint64_t remain_ms = (elapsed_ms < interval_ms) ? (interval_ms - elapsed_ms) : 0ULL;
                /* Must be >0 to arm RTC timing wakeup. */
                sleep_sec = (remain_ms == 0ULL) ? 1U : (uint32_t)((remain_ms + 999ULL) / 1000ULL);
            }
        } else if (cfg->timer_trigger_enabled && cfg->timer_trigger_capture_mode == AICAM_TIMER_CAPTURE_MODE_ABSOLUTE) {
            /* Absolute schedule: compute next time-node delta and use timing wakeup. */
            uint32_t d = qb_calc_next_abs_sleep_sec(cfg);
            if (d > 0) {
                wakeup_flags |= PWR_WAKEUP_FLAG_RTC_TIMING;
                sleep_sec = d;
            }
        }

        /* PIR trigger */
        if (cfg->pir_trigger_enabled) {
            /* qs_work_mode_config_t:
             * 0:rising 1:falling 2:both 3:high 4:low
             */
            switch (cfg->pir_trigger_type) {
                case 1: wakeup_flags |= PWR_WAKEUP_FLAG_PIR_FALLING; break;
                case 3: wakeup_flags |= PWR_WAKEUP_FLAG_PIR_HIGH; break;
                case 4: wakeup_flags |= PWR_WAKEUP_FLAG_PIR_LOW; break;
                case 2: /* both edges: use rising like system_service */
                case 0:
                default: wakeup_flags |= PWR_WAKEUP_FLAG_PIR_RISING; break;
            }
        }

        /* Remote trigger: only meaningful if WiFi remote wakeup mode is ready. */
        if (cfg->remote_trigger_enabled && remote_wakeup_ok) {
            wakeup_flags |= PWR_WAKEUP_FLAG_SI91X;
            /* Keep WiFi + 3V3 power like system_service low power remote wakeup path. */
            switch_bits |= (PWR_WIFI_SWITCH_BIT | PWR_3V3_SWITCH_BIT);
        }
    }

    if (out_wakeup_flags) *out_wakeup_flags = wakeup_flags;
    if (out_switch_bits) *out_switch_bits = switch_bits;
    if (out_sleep_sec) *out_sleep_sec = sleep_sec;
}

static void qb_enter_sleep_from_workcfg(const qs_work_mode_config_t *cfg, aicam_bool_t remote_wakeup_ok)
{
    /* Update RTC time to U0 before sleep (same intent as system_service). */
    (void)u0_module_update_rtc_time();

    /* Configure PIR if enabled; ignore errors (same as system_service: warn & continue). */
    (void)qb_configure_pir_from_workcfg(cfg);

    uint32_t wakeup_flags = 0;
    uint32_t switch_bits = 0;
    uint32_t sleep_sec = 0;
    qb_build_u0_wakeup_and_power(cfg, remote_wakeup_ok, &wakeup_flags, &switch_bits, &sleep_sec);

    /* Align with upper layer remote-wakeup fallback:
     * If remote trigger is enabled but remote wakeup couldn't be configured,
     * force a periodic RTC timing wakeup (e.g. 30min) to retry later.
     */
    if (cfg && cfg->remote_trigger_enabled && !remote_wakeup_ok) {
        const uint32_t fallback_sleep_sec = 1800U; /* 30 minutes */
        if (sleep_sec == 0 || fallback_sleep_sec < sleep_sec) {
            sleep_sec = fallback_sleep_sec;
        }
        if (sleep_sec > 0) {
            wakeup_flags |= PWR_WAKEUP_FLAG_RTC_TIMING;
        }
    }

    /* Align with upper layer: optionally provide scheduler-based RTC alarms (A/B). */
    ms_bridging_alarm_t alarm_a;
    ms_bridging_alarm_t alarm_b;
    memset(&alarm_a, 0, sizeof(alarm_a));
    memset(&alarm_b, 0, sizeof(alarm_b));

    uint64_t next_a = 0;
    if (rtc_get_next_wakeup_time(1, &next_a) == 0 && next_a != 0) {
        RTC_TIME_S t;
        memset(&t, 0, sizeof(t));
        timeStamp_to_time(next_a, &t);
        /* ms_bridging_alarm_t: week_day 1~7, 0 disabled */
        if (t.dayOfWeek >= 1 && t.dayOfWeek <= 7) {
            alarm_a.is_valid = 1;
            alarm_a.week_day = t.dayOfWeek;
            alarm_a.date = 0;
            alarm_a.hour = t.hour;
            alarm_a.minute = t.minute;
            alarm_a.second = t.second;
            wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_A;
        }
    }

    uint64_t next_b = 0;
    if (rtc_get_next_wakeup_time(2, &next_b) == 0 && next_b != 0) {
        RTC_TIME_S t;
        memset(&t, 0, sizeof(t));
        timeStamp_to_time(next_b, &t);
        if (t.dayOfWeek >= 1 && t.dayOfWeek <= 7) {
            alarm_b.is_valid = 1;
            alarm_b.week_day = t.dayOfWeek;
            alarm_b.date = 0;
            alarm_b.hour = t.hour;
            alarm_b.minute = t.minute;
            alarm_b.second = t.second;
            wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_B;
        }
    }

    int ret = u0_module_enter_sleep_mode_ex(
        wakeup_flags,
        switch_bits,
        sleep_sec,
        alarm_a.is_valid ? &alarm_a : NULL,
        alarm_b.is_valid ? &alarm_b : NULL);
    if (ret != 0) {
        QB_LOGE("enter sleep mode failed: %d", ret);
    }
    pwr_enter_standby_mode();
}

void quick_bootstrap_run(qb_wakeup_source_type_t wakeup_source)
{
    qb_prof_t prof;
    qb_prof_init(&prof);

    memset(&s_qb, 0, sizeof(s_qb));
    s_qb.wake_src = wakeup_source;
    s_qb.wake_uptime_ms = rtc_get_uptime_ms();
    s_qb.wake_ts = rtc_get_timeStamp();
    if (s_qb.wake_ts == 0) {
        /* fallback to uptime (monotonic) if RTC is not set (keep unit in seconds) */
        s_qb.wake_ts = s_qb.wake_uptime_ms / 1000ULL;
    }
    /* frame_id will be updated from camera pipeline if available */
    s_qb.frame_id = 0;

    quick_log_mutex_init();

    QB_TRACE("run src=%u ts=%lu", (unsigned)wakeup_source, (unsigned long)s_qb.wake_ts);
    qb_prof_step(&prof, "run:start");

    if (s_qb.evt == NULL) {
        s_qb.evt = osEventFlagsNew(NULL);
    }
    if (s_qb.evt == NULL) {
        QB_LOGE("evt create fail");
        return;
    }

    osDelay(10);
    /* init components */
    qb_prof_step(&prof, "init:begin");
    (void)quick_storage_init();
    qb_prof_step(&prof, "init:storage");
    (void)quick_snapshot_init();
    qb_prof_step(&prof, "init:snapshot");
    (void)quick_network_init();
    qb_prof_step(&prof, "init:network");
    qb_prof_step(&prof, "init:done");

    /* start event consumer thread */
    if (s_evt_tid == NULL) {
        /* CMSIS-RTOS2 may require user-provided stacks to be 8-byte aligned. */
        static uint8_t s_evt_stack[4096 * 2] ALIGN_32;
        static const osThreadAttr_t attr = {
            .name = "qs_evt",
            .stack_mem = s_evt_stack,
            .stack_size = sizeof(s_evt_stack),
            .priority = osPriorityAboveNormal,
            .cb_mem     = 0,
            .cb_size    = 0,
            .attr_bits  = 0u,
            .tz_module  = 0u,
        };
        s_evt_tid = osThreadNew(qb_event_thread, NULL, &attr);
    }

    /* wait snapshot config and network config */
    (void)quick_snapshot_wait_config(&s_qb.snap_cfg);
    (void)quick_storage_read_work_mode_config(&s_qb.work_cfg);
    (void)quick_network_wait_config(&s_qb.comm_pref);
    QB_TRACE("cfg ai=%u store_ai=%u comm=%s",
             (unsigned)s_qb.snap_cfg.ai_enabled,
             (unsigned)s_qb.snap_cfg.capture_storage_ai,
             qb_comm_type_str(s_qb.comm_pref));
    qb_prof_step(&prof, "cfg:ready");

    const aicam_bool_t upload_enabled = (s_qb.comm_pref != COMM_PREF_TYPE_DISABLE) ? AICAM_TRUE : AICAM_FALSE;

    /* If upload enabled: wait jpeg first, then optional ai result, then publish */
    if (upload_enabled) {
        /* wait for jpeg */
        uint32_t w = osEventFlagsWait(s_qb.evt, QB_FLAG_JPEG_SEEN | QB_FLAG_ABORT, osFlagsWaitAny | osFlagsNoClear, 60000);
        if (w & QB_FLAG_ABORT) goto qb_finalize;
        qb_prof_step(&prof, "main:jpeg");

        /* if AI enabled, wait ai result before building mqtt payload */
        if (s_qb.snap_cfg.ai_enabled) {
            w = osEventFlagsWait(s_qb.evt, QB_FLAG_AI_RESULT_SEEN | QB_FLAG_ABORT, osFlagsWaitAny | osFlagsNoClear, 60000);
            if (w & QB_FLAG_ABORT) goto qb_finalize;
            qb_prof_step(&prof, "main:ai_res");
        }

        char *payload = NULL;
        size_t payload_len = 0;
        if (qb_build_mqtt_payload(&payload, &payload_len) == 0 && payload && payload_len > 0) {
            QB_TRACE("mqtt payload %luB", (unsigned long)payload_len);
            qb_prof_step(&prof, "mqtt:payload");
            qs_mqtt_task_param_t task = {0};
            task.cmd = 0;
            task.data = payload;
            task.data_len = payload_len;
            task.is_wait_pub_ack = 1;
            task.callback = qb_mqtt_done_cb;
            task.callback_param = payload; /* free after publish */
            if (quick_network_add_mqtt_task(&task) == 0) {
                s_qb.pending_mqtt++;
                QB_TRACE("mqtt queued");
            } else {
                cJSON_free(payload);
                (void)osEventFlagsSet(s_qb.evt, QB_FLAG_MQTT_FAILED);
                (void)osEventFlagsSet(s_qb.evt, QB_FLAG_MQTT_PUB_DONE);
                QB_LOGW("mqtt enqueue failed (queue full or not ready)");
            }
        } else {
            if (payload) cJSON_free(payload);
            (void)osEventFlagsSet(s_qb.evt, QB_FLAG_MQTT_PUB_DONE);
        }
    } else {
        /* upload disabled: don't wait for mqtt */
        (void)osEventFlagsSet(s_qb.evt, QB_FLAG_MQTT_PUB_DONE);
        QB_TRACE("upload disabled");
    }

qb_finalize:
    /* wait tasks done or timeout */
    uint32_t need = QB_ALL_DONE_MASK;
    if (!s_qb.snap_cfg.ai_enabled || !s_qb.snap_cfg.capture_storage_ai) {
        /* AI jpeg may never be produced */
        need &= ~QB_FLAG_STORE_AI_JPEG_DONE;
    }
    if (!s_qb.snap_cfg.ai_enabled) {
        need &= ~QB_FLAG_STORE_AI_JSON_DONE;
    }
    if (!upload_enabled) {
        need &= ~QB_FLAG_MQTT_PUB_DONE;
    }

    const uint32_t store_need = need & ~QB_FLAG_MQTT_PUB_DONE;
    const uint32_t mqtt_need = need & QB_FLAG_MQTT_PUB_DONE;

    /* Requirements:
     * - QB_FLAG_ABORT arrives => return immediately (no long wait)
     * - storage timeout: 10s
     * - mqtt timeout: cap (publish+ack normally completes well under this)
     */
    qb_wait_done_or_abort(store_need, 10000U);
    qb_wait_done_or_abort(mqtt_need, 20000U);
    if (upload_enabled && s_qb.evt != NULL) {
        uint32_t ef = osEventFlagsGet(s_qb.evt);
        if ((ef & QB_FLAG_MQTT_FAILED) != 0U) {
            QB_LOGW("mqtt failed path taken (net/mqtt rc=%d)", quick_network_get_mqtt_result());
        }
    }
    QB_TRACE("done store=%lu mqtt=%lu",
             (unsigned long)s_qb.pending_store,
             (unsigned long)s_qb.pending_mqtt);
    qb_prof_step(&prof, "final:done");
    printf("quick_bootstrap_run end, %lu ms\r\n", HAL_GetTick());

    /* Finalize: free any unshared jpegc buffers still owned by quick_bootstrap. */
    if (s_qb.jpeg_need_free && s_qb.jpeg) {
        qb_free_jpegc_buffer(s_qb.jpeg);
        s_qb.jpeg = NULL;
        s_qb.jpeg_len = 0;
        s_qb.jpeg_need_free = AICAM_FALSE;
    }
    if (s_qb.ai_jpeg_need_free && s_qb.ai_jpeg) {
        qb_free_jpegc_buffer(s_qb.ai_jpeg);
        s_qb.ai_jpeg = NULL;
        s_qb.ai_jpeg_len = 0;
        s_qb.ai_jpeg_need_free = AICAM_FALSE;
    }

    /* Enter sleep without core/services dependency:
     * - (optional) switch WiFi to remote-wakeup keep-alive mode
     * - deinit quick network threads
     * - configure U0 wakeup sources + power switches from qs_work_mode_config_t
     * - enter sleep via u0_module_enter_sleep_mode_ex(...)
     */
    aicam_bool_t remote_wakeup_ok = AICAM_FALSE;
    if (s_qb.work_cfg.remote_trigger_enabled && upload_enabled && s_qb.comm_pref == COMM_PREF_TYPE_WIFI) {
        remote_wakeup_ok = (quick_network_switch_remote_wakeup_mode() == 0) ? AICAM_TRUE : AICAM_FALSE;
    }

    // quick_network_deinit();
    // qb_prof_step(&prof, "net:deinit");
    qb_enter_sleep_from_workcfg(&s_qb.work_cfg, remote_wakeup_ok);
}

