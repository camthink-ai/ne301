#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_host_session_device.h"
#include "pp.h"

#include "cmsis_os2.h"
#include "stm32n6xx_hal.h"

#include "video_ai_node.h"
#include "video_stream_hub.h"
#include "ai_service.h"
#include "nn.h"
#include "cJSON.h"
#include "common_utils.h"
#include "debug.h"

extern uint8_t __app_host_ram_base__[];
extern uint8_t __app_host_ram_end__[];

#define SESSION_TASK_STACK_SIZE (1024u * 16u)

static uint8_t g_session_task_stack[SESSION_TASK_STACK_SIZE] ALIGN_32 IN_PSRAM;
static osThreadId_t g_session_thread;
static video_hub_subscriber_id_t g_session_hub_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;

static uint8_t *dev_stage_base(void *ctx, size_t *cap)
{
    (void)ctx;
    *cap = (size_t)(__app_host_ram_end__ - __app_host_ram_base__);
    return __app_host_ram_base__;
}

static int dev_stage_commit(void *ctx, size_t stage_cap,
                            const app_host_image_info_t *info)
{
    (void)ctx;
    (void)info;
    __DSB();
    SCB_CleanInvalidateDCache_by_Addr(__app_host_ram_base__, (int32_t)stage_cap);
    SCB_InvalidateICache_by_Addr(__app_host_ram_base__, (int32_t)stage_cap);
    __ISB();
    return 0;
}

static int dev_invoke_entry(void *ctx, const void *api, uint32_t abi_version,
                            const app_host_image_info_t *info, int *raw)
{
    uintptr_t entry_addr;

    (void)ctx;
    if (raw == NULL || api == NULL || info == NULL) {
        return -1;
    }
    entry_addr = (uintptr_t)__app_host_ram_base__ + info->entry_offset;
    if (abi_version == APP_SESSION_ABI_VERSION_V3) {
        app_session_entry_v3_fn_t entry =
            (app_session_entry_v3_fn_t)(entry_addr | APP_HOST_ENTRY_THUMB_MASK);
        *raw = entry((const app_host_session_api_table_v3_t *)api, abi_version);
    } else if (abi_version == APP_SESSION_ABI_VERSION_V4) {
        app_session_entry_v4_fn_t entry =
            (app_session_entry_v4_fn_t)(entry_addr | APP_HOST_ENTRY_THUMB_MASK);
        (*raw) = entry((const app_host_session_api_table_v4_t *)api, abi_version);
    } else {
        app_session_entry_fn_t entry =
            (app_session_entry_fn_t)(entry_addr | APP_HOST_ENTRY_THUMB_MASK);
        *raw = entry((const app_host_session_api_table_t *)api, abi_version);
    }
    return 0;
}

static void dev_scrub(void *ctx)
{
    int32_t size = (int32_t)(__app_host_ram_end__ - __app_host_ram_base__);
    (void)ctx;
    memset(__app_host_ram_base__, 0, (size_t)size);
    SCB_CleanInvalidateDCache_by_Addr(__app_host_ram_base__, size);
}

static void dev_log_sink(void *ctx, const char *text)
{
    (void)ctx;
    LOG_SIMPLE("[APP] %s\r\n", (text != NULL) ? text : "");
}

static uint32_t dev_tick_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)osKernelGetTickCount();
}

static void dev_sleep_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    osDelay(ms);
}

static void dev_session_thread_trampoline(void *argument)
{
    void (*fn)(void *) = (void (*)(void *))argument;

    fn(NULL);
}

static aicam_bool_t dev_entry_exited(void *ctx)
{
    osThreadState_t st;
    osThreadId_t handle;

    (void)ctx;
    handle = g_session_thread;
    if (handle == NULL) {
        return AICAM_TRUE;
    }
    st = osThreadGetState(handle);
    if (st != osThreadTerminated) {
        return AICAM_FALSE;
    }
    (void)osThreadTerminate(handle);
    g_session_thread = NULL;
    return AICAM_TRUE;
}

static int dev_entry_spawn(void *ctx, void (*fn)(void *arg), void *arg)
{
    osThreadAttr_t attr;

    (void)arg;
    if (dev_entry_exited(ctx) != AICAM_TRUE) {
        return -1;
    }
    memset(&attr, 0, sizeof(attr));
    attr.name = "appsession";
    attr.attr_bits = osThreadDetached;
    attr.cb_mem = NULL;
    attr.stack_mem = g_session_task_stack;
    attr.stack_size = SESSION_TASK_STACK_SIZE;
    attr.priority = osPriorityNormal;
    g_session_thread = osThreadNew(dev_session_thread_trampoline, (void *)fn, &attr);
    return (g_session_thread != NULL) ? 0 : -1;
}

static void *dev_mutex_create(void *ctx)
{
    (void)ctx;
    return (void *)osMutexNew(NULL);
}

static void dev_mutex_destroy(void *ctx, void *mtx)
{
    (void)ctx;
    if (mtx != NULL) {
        osMutexDelete((osMutexId_t)mtx);
    }
}

static int dev_mutex_lock(void *ctx, void *mtx)
{
    (void)ctx;
    return (osMutexAcquire((osMutexId_t)mtx, osWaitForever) == osOK) ? 0 : -1;
}

static void dev_mutex_unlock(void *ctx, void *mtx)
{
    (void)ctx;
    osMutexRelease((osMutexId_t)mtx);
}

static void *dev_event_create(void *ctx)
{
    (void)ctx;
    return (void *)osEventFlagsNew(NULL);
}

static void dev_event_destroy(void *ctx, void *evt)
{
    (void)ctx;
    if (evt != NULL) {
        osEventFlagsDelete((osEventFlagsId_t)evt);
    }
}

static uint32_t dev_event_wait(void *ctx, void *evt, uint32_t timeout_ms)
{
    uint32_t flags;
    (void)ctx;

    if (evt == NULL) {
        return 0u;
    }
    flags = 0u;
    if (osEventFlagsWait((osEventFlagsId_t)evt, 1u, osFlagsWaitAny,
                         (uint32_t)timeout_ms) == 1u) {
        flags = 1u;
    }
    return flags;
}

static void dev_event_set(void *ctx, void *evt)
{
    (void)ctx;
    if (evt != NULL) {
        osEventFlagsSet((osEventFlagsId_t)evt, 1u);
    }
}

static void dev_ai_listener(const nn_result_t *result, uint32_t frame_id,
                            uint32_t inference_time_ms, void *user_data)
{
    (void)frame_id;
    (void)inference_time_ms;
    (void)user_data;
    if (result == NULL) {
        return;
    }
    if (result->type != PP_TYPE_OD) {
        app_host_session_on_ai_result(0u, NULL, dev_tick_ms(NULL));
        return;
    }
    if (result->od.detects == NULL) {
        app_host_session_on_ai_result(0u, NULL, dev_tick_ms(NULL));
        return;
    }
    app_host_session_on_ai_result((uint32_t)result->od.nb_detect,
                                  result->od.detects,
                                  dev_tick_ms(NULL));
}

static aicam_result_t dev_hub_frame_sink(const video_hub_frame_t *frame, void *user_data)
{
    (void)frame;
    (void)user_data;
    return AICAM_OK;
}

static void dev_fill_model_view_from_json(app_session_model_view_t *view,
                                          const nn_model_info_t *info)
{
    cJSON *root;
    cJSON *params;
    cJSON *names;
    uint32_t count;
    uint32_t i;

    view->identity_addr = info->config_ptr;
    view->identity_size = info->model_size;
    view->result_type_od =
        (pp_name_type(info->postprocess_type) == PP_TYPE_OD) ? AICAM_TRUE : AICAM_FALSE;
    strncpy(view->model_name, info->name, APP_SESSION_MODEL_NAME_SLOT - 1u);
    strncpy(view->model_version, info->version, APP_SESSION_MODEL_VERSION_SLOT - 1u);
    view->model_name[APP_SESSION_MODEL_NAME_SLOT - 1u] = '\0';
    view->model_version[APP_SESSION_MODEL_VERSION_SLOT - 1u] = '\0';
    view->class_count = 0u;

    root = cJSON_Parse((const char *)info->config_ptr);
    if (root == NULL) {
        return;
    }
    params = cJSON_GetObjectItemCaseSensitive(root, "postprocess_params");
    if (params == NULL) {
        params = root;
    }
    names = cJSON_GetObjectItemCaseSensitive(params, "class_names");
    if (cJSON_IsArray(names)) {
        int total = cJSON_GetArraySize(names);
        if (total > 0 && (uint32_t)total <= APP_SESSION_MAX_CLASSES) {
            count = (uint32_t)total;
            for (i = 0u; i < count; i++) {
                cJSON *name = cJSON_GetArrayItem(names, (int)i);
                if (!cJSON_IsString(name) || name->valuestring == NULL) {
                    view->class_count = 0u;
                    break;
                }
                if (strlen(name->valuestring) >= APP_SESSION_CLASS_SLOT) {
                    view->class_count = 0u;
                    break;
                }
                strcpy(view->class_names[i], name->valuestring);
                view->class_count++;
            }
        }
    }
    cJSON_Delete(root);
}

#define DEV_MODEL_CONFIG_DIGEST_MAX_BYTES (65536u)

static uint32_t dev_hash_update(uint32_t h, const uint8_t *p, size_t len)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static aicam_bool_t dev_hash_bounded_cstr(uint32_t *h, const char *s, size_t cap)
{
    size_t len;

    if ((h == NULL) || (s == NULL)) {
        return AICAM_FALSE;
    }
    len = strnlen(s, cap);
    if (len >= cap) {
        return AICAM_FALSE;
    }
    *h = dev_hash_update(*h, (const uint8_t *)s, len);
    return AICAM_TRUE;
}

static aicam_bool_t dev_model_digest(const nn_model_info_t *info, uint32_t *out)
{
    uint32_t h = 2166136261u;

    if ((info == NULL) || (out == NULL)) {
        return AICAM_FALSE;
    }
    if (info->config_ptr == (uintptr_t)NULL) {
        return AICAM_FALSE;
    }
    if (dev_hash_bounded_cstr(&h, (const char *)info->config_ptr,
                              DEV_MODEL_CONFIG_DIGEST_MAX_BYTES) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (dev_hash_bounded_cstr(&h, info->name, sizeof(info->name)) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (dev_hash_bounded_cstr(&h, info->version, sizeof(info->version)) != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    h ^= (uint32_t)((uintptr_t)info->config_ptr & 0xFFFFFFFFu);
    h *= 16777619u;
    h ^= (uint32_t)(((uintptr_t)info->config_ptr >> 16) & 0xFFFF0000u);
    h *= 16777619u;
    h ^= (uint32_t)((uintptr_t)info->model_ptr & 0xFFFFFFFFu);
    h *= 16777619u;
    h ^= info->model_size;
    h *= 16777619u;
    h ^= video_ai_node_model_load_generation();
    h *= 16777619u;
    if (h == 0u) {
        return AICAM_FALSE;
    }
    *out = h;
    return AICAM_TRUE;
}

static aicam_bool_t g_model_cache_valid;
static uintptr_t g_model_cache_addr;
static uintptr_t g_model_cache_model_addr;
static uint32_t g_model_cache_size;
static uint32_t g_model_cache_generation;
static app_session_model_view_t g_model_cache;

static aicam_bool_t dev_model_cache_hit(const nn_model_info_t *info)
{
    if (g_model_cache_valid != AICAM_TRUE) {
        return AICAM_FALSE;
    }
    if (g_model_cache_addr != info->config_ptr) {
        return AICAM_FALSE;
    }
    if (g_model_cache_model_addr != info->model_ptr) {
        return AICAM_FALSE;
    }
    if (g_model_cache_size != (uint32_t)info->model_size) {
        return AICAM_FALSE;
    }
    if (g_model_cache_generation != video_ai_node_model_load_generation()) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

static aicam_bool_t dev_ai_model_view(void *ctx, app_session_model_view_t *out)
{
    nn_model_info_t info;
    uint32_t digest;

    (void)ctx;
    if (out == NULL) {
        return AICAM_FALSE;
    }
    memset(out, 0, sizeof(*out));
    if (nn_get_model_info(&info) != 0) {
        return AICAM_FALSE;
    }
    if (nn_get_state() != NN_STATE_READY) {
        return AICAM_FALSE;
    }
    if (dev_model_cache_hit(&info) == AICAM_TRUE) {
        *out = g_model_cache;
        return AICAM_TRUE;
    }
    if (dev_model_digest(&info, &digest) != AICAM_TRUE) {
        g_model_cache_valid = AICAM_FALSE;
        return AICAM_FALSE;
    }
    dev_fill_model_view_from_json(out, &info);
    out->content_digest = digest;
    g_model_cache = *out;
    g_model_cache_addr = info.config_ptr;
    g_model_cache_model_addr = info.model_ptr;
    g_model_cache_size = (uint32_t)info.model_size;
    g_model_cache_generation = video_ai_node_model_load_generation();
    g_model_cache_valid = AICAM_TRUE;
    return AICAM_TRUE;
}

static aicam_bool_t dev_ai_attach(void *ctx)
{
    video_node_t *node = ai_service_get_ai_node();
    video_hub_subscriber_id_t hub_id;

    (void)ctx;
    if (node == NULL) {
        return AICAM_FALSE;
    }
    if (video_ai_node_add_result_listener(node, dev_ai_listener, NULL) != AICAM_OK) {
        return AICAM_FALSE;
    }
    hub_id = video_hub_subscribe(VIDEO_HUB_SUBSCRIBER_CUSTOM, dev_hub_frame_sink,
                                 NULL, NULL);
    if (hub_id == VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        video_ai_node_remove_result_listener(node, dev_ai_listener, NULL);
        return AICAM_FALSE;
    }
    g_session_hub_id = hub_id;
    return AICAM_TRUE;
}

static void dev_ai_detach(void *ctx)
{
    video_node_t *node = ai_service_get_ai_node();

    (void)ctx;
    if (g_session_hub_id != VIDEO_HUB_INVALID_SUBSCRIBER_ID) {
        video_hub_unsubscribe(g_session_hub_id);
        g_session_hub_id = VIDEO_HUB_INVALID_SUBSCRIBER_ID;
    }
    if (node != NULL) {
        video_ai_node_remove_result_listener(node, dev_ai_listener, NULL);
    }
}

static app_session_result_t dev_pair_authorize(void *ctx,
                                               const app_install_io_t *io,
                                               const app_install_record_t *native_record,
                                               char *reason,
                                               size_t reason_cap)
{
    (void)ctx;
    return app_host_session_pair_authorize(io, native_record, reason, reason_cap);
}

static const app_host_session_backend_t g_app_host_session_device_backend = {
    NULL,
    dev_stage_base,
    dev_stage_commit,
    dev_invoke_entry,
    dev_scrub,
    dev_log_sink,
    dev_tick_ms,
    dev_sleep_ms,
    dev_entry_spawn,
    dev_entry_exited,
    dev_mutex_create,
    dev_mutex_destroy,
    dev_mutex_lock,
    dev_mutex_unlock,
    dev_event_create,
    dev_event_destroy,
    dev_event_wait,
    dev_event_set,
    dev_ai_attach,
    dev_ai_detach,
    dev_ai_model_view,
    dev_pair_authorize
};

const app_host_session_backend_t *app_host_session_device_backend(void)
{
    return &g_app_host_session_device_backend;
}
