#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "cmsis_os2.h"

#include "dev_seam.h"
#include "video_stream_hub.h"
#include "pp.h"

/* Native test seam: map the OD model used by the fixture without pulling in
 * the firmware-wide PP registry and all model implementations. */
pp_type_t pp_name_type(const char *name)
{
    return (name != NULL && strncmp(name, "od_", 3) == 0) ? PP_TYPE_OD : PP_TYPE_NONE;
}


static nn_model_info_t g_seam_model_info;
static nn_state_t g_seam_nn_state;
static int g_seam_info_rc;
static int g_seam_load_rc;
static int g_seam_unload_rc;
static uint32_t g_seam_load_calls;
static uintptr_t g_seam_last_load_ptr;
static uint32_t g_seam_active_switch_calls;
static video_ai_node_data_t g_seam_node_data;
static video_node_t g_seam_node_obj;
static video_node_t *g_seam_node = &g_seam_node_obj;

void dev_seam_reset(void)
{
    memset(&g_seam_model_info, 0, sizeof(g_seam_model_info));
    g_seam_nn_state = NN_STATE_READY;
    g_seam_info_rc = 0;
    g_seam_load_rc = 0;
    g_seam_unload_rc = 0;
    g_seam_load_calls = 0u;
    g_seam_last_load_ptr = 0u;
    g_seam_active_switch_calls = 0u;
    if (g_seam_node_data.cache_mutex != NULL) {
        osMutexDelete(g_seam_node_data.cache_mutex);
        g_seam_node_data.cache_mutex = NULL;
    }
    memset(&g_seam_node_data, 0, sizeof(g_seam_node_data));
    memset(&g_seam_node_obj, 0, sizeof(g_seam_node_obj));
    g_seam_node_data.cache_mutex = osMutexNew(NULL);
}

nn_model_info_t *dev_seam_model_info(void)
{
    return &g_seam_model_info;
}

void dev_seam_set_state(nn_state_t state)
{
    g_seam_nn_state = state;
}

void dev_seam_set_info_rc(int rc)
{
    g_seam_info_rc = rc;
}

void dev_seam_set_load_rc(int rc)
{
    g_seam_load_rc = rc;
}

void dev_seam_set_unload_rc(int rc)
{
    g_seam_unload_rc = rc;
}

uint32_t dev_seam_load_calls(void)
{
    return g_seam_load_calls;
}

uintptr_t dev_seam_last_load_ptr(void)
{
    return g_seam_last_load_ptr;
}

uint32_t dev_seam_active_switch_calls(void)
{
    return g_seam_active_switch_calls;
}

video_node_t *dev_seam_ai_node(void)
{
    return g_seam_node;
}

int nn_load_model(const uintptr_t file_ptr)
{
    g_seam_load_calls++;
    g_seam_last_load_ptr = file_ptr;
    return g_seam_load_rc;
}

int nn_unload_model(void)
{
    return g_seam_unload_rc;
}

int nn_get_model_info(nn_model_info_t *model_info)
{
    if (model_info == NULL) {
        return -1;
    }
    if (g_seam_info_rc != 0) {
        return g_seam_info_rc;
    }
    *model_info = g_seam_model_info;
    return 0;
}

nn_state_t nn_get_state(void)
{
    return g_seam_nn_state;
}

int nn_start_inference(void)
{
    return 0;
}

int nn_stop_inference(void)
{
    return 0;
}

int nn_inference_frame(uint8_t *input_data, uint32_t input_size, nn_result_t *result)
{
    (void)input_data;
    (void)input_size;
    (void)result;
    return -1;
}

int nn_set_confidence_threshold(float threshold)
{
    (void)threshold;
    return 0;
}

int nn_set_nms_threshold(float threshold)
{
    (void)threshold;
    return 0;
}

int nn_set_max_detection_count(uint32_t max_count)
{
    (void)max_count;
    return 0;
}

aicam_bool_t json_config_get_ai_1_active(void)
{
    return AICAM_FALSE;
}

aicam_result_t json_config_sync_ai_pipe_nvs_from_input_size(uint32_t input_width,
                                                            uint32_t input_height)
{
    (void)input_width;
    (void)input_height;
    g_seam_active_switch_calls++;
    return AICAM_OK;
}

video_node_t *video_node_create(const char *name, video_node_type_t type)
{
    (void)name;
    (void)type;
    return NULL;
}

aicam_result_t video_node_destroy(video_node_t *node)
{
    (void)node;
    return AICAM_OK;
}

aicam_result_t video_node_set_callbacks(video_node_t *node,
                                        const video_node_callbacks_t *callbacks)
{
    (void)node;
    (void)callbacks;
    return AICAM_OK;
}

aicam_result_t video_node_set_private_data(video_node_t *node, void *data)
{
    (void)node;
    (void)data;
    return AICAM_OK;
}

void *video_node_get_private_data(video_node_t *node)
{
    (void)node;
    return &g_seam_node_data;
}

video_node_t *ai_service_get_ai_node(void)
{
    return g_seam_node;
}

video_hub_subscriber_id_t video_hub_subscribe(video_hub_subscriber_type_t type,
                                              video_hub_frame_callback_t frame_callback,
                                              video_hub_sps_pps_callback_t sps_pps_callback,
                                              void *user_data)
{
    (void)type;
    (void)frame_callback;
    (void)sps_pps_callback;
    (void)user_data;
    return (video_hub_subscriber_id_t)1;
}

aicam_result_t video_hub_unsubscribe(video_hub_subscriber_id_t subscriber_id)
{
    (void)subscriber_id;
    return AICAM_OK;
}

device_t *device_find_pattern(const char *pattern, dev_type_t type)
{
    (void)pattern;
    (void)type;
    return NULL;
}

int device_ioctl(device_t *dev, unsigned int cmd, unsigned char *ubuf, unsigned long arg)
{
    (void)dev;
    (void)cmd;
    (void)ubuf;
    (void)arg;
    return -1;
}

void *buffer_calloc(size_t count, size_t size)
{
    (void)count;
    (void)size;
    return NULL;
}

void buffer_free(void *ptr)
{
    (void)ptr;
}

void ai_draw_get_default_config(ai_draw_config_t *config)
{
    (void)config;
}

aicam_result_t ai_draw_service_init(const ai_draw_config_t *config)
{
    (void)config;
    return AICAM_ERROR;
}

aicam_result_t ai_draw_service_deinit(void)
{
    return AICAM_ERROR;
}

aicam_result_t ai_draw_set_config(const ai_draw_config_t *config)
{
    (void)config;
    return AICAM_ERROR;
}
