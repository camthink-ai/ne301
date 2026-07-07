/**
 * @file nn.c
 * @brief AI Neural Network Module Implementation
 * @details Implementation of AI neural network module based on STM32N6 NPU
 */

#include "common_utils.h"
#include "generic_math.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ll_aton_runtime.h"
#include "ll_aton_reloc_network.h"
#include "pp.h"
#include "nn.h"
#include "cJSON.h"
#include "cbor_enc.h"
#include "gauge_reading.h"
#include "mpool.h"
#include "camera.h"
#include "mem_map.h"
#include "storage.h"

/* ==================== internal data structure ==================== */

// global mutex to serialize NPU access across instances
static osMutexId_t g_npu_mutex = NULL;

static nn_t *g_nn_instances[NN_MAX_INSTANCES] = {NULL};

static void invalidate_output_cache(nn_t *nn)
{
    for (uint32_t i = 0; i < nn->output_buffer_count; i++) {
        SCB_InvalidateDCache_by_Addr(nn->output_buffer[i], nn->output_buffer_size[i]);
    }
}

static void flush_input_cache(nn_t *nn)
{
    for (uint32_t i = 0; i < nn->input_buffer_count; i++) {
        SCB_CleanInvalidateDCache_by_Addr(nn->input_buffer[i], nn->input_buffer_size[i]);
    }
}

static void npu_mutex_acquire(void)
{
    if (g_npu_mutex) {
        osMutexAcquire(g_npu_mutex, osWaitForever);
    }
}

static void npu_mutex_release(void)
{
    if (g_npu_mutex) {
        osMutexRelease(g_npu_mutex);
    }
}

static int nn_init(void *priv)
{
    LOG_DRV_DEBUG("nn_init\r\r\n");
    LL_ATON_RT_RuntimeInit();
    memset(g_nn_instances, 0, sizeof(g_nn_instances));
    g_npu_mutex = osMutexNew(NULL);
    if (g_npu_mutex == NULL) {
        LOG_DRV_ERROR("nn_init: Failed to create NPU mutex\r\r\n");
        return -1;
    }
    return 0;
}

static int nn_deinit(void *priv)
{
    LOG_DRV_DEBUG("nn_deinit\r\r\n");
    LL_ATON_RT_RuntimeDeInit();
    if (g_npu_mutex) {
        osMutexDelete(g_npu_mutex);
        g_npu_mutex = NULL;
    }
    return 0;
}

/* ==================== internal auxiliary function implementation ==================== */

static int load_info(const uintptr_t file_ptr, nn_model_info_t *info)
{
    if (!file_ptr || !info) {
        return -1;
    }

    storage_lock();

    nn_package_header_t *header = (nn_package_header_t *)file_ptr;

    if (header->magic != MODEL_PACKAGE_MAGIC) {
        LOG_DRV_ERROR("Invalid model package magic number\r\r\n");
        storage_unlock();
        return -1;
    }

    if (header->version != MODEL_PACKAGE_VERSION) {
        LOG_DRV_ERROR("Incompatible model package version 0X%lx\r\r\n", header->version);
        storage_unlock();
        return -1;
    }

    info->metadata_ptr = file_ptr + header->metadata_offset;
    /* Model configuration pointer */
    info->config_ptr = file_ptr + header->model_config_offset;
    /* Model pointer */
    info->model_ptr = file_ptr + header->relocatable_model_offset;
    info->model_size = header->relocatable_model_size;
    /* Model configuration */
    cJSON *root = cJSON_Parse((const char *)info->config_ptr);
    if (root == NULL) {
        LOG_DRV_ERROR("load_info: JSON parse failed\r\r\n");
        storage_unlock();
        return -1;
    }

    /* Model name */
    cJSON *json = cJSON_GetObjectItemCaseSensitive(root, "model_info");
    if (cJSON_IsObject(json)) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "name");
        if (cJSON_IsString(name)) {
            strncpy(info->name, name->valuestring, sizeof(info->name) - 1);
        }
        cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
        if (cJSON_IsString(version)) {
            strncpy(info->version, version->valuestring, sizeof(info->version) - 1);
        }
        cJSON *description = cJSON_GetObjectItemCaseSensitive(json, "description");
        if (cJSON_IsString(description)) {
            strncpy(info->description, description->valuestring, sizeof(info->description) - 1);
        }
        cJSON *author = cJSON_GetObjectItemCaseSensitive(json, "author");
        if (cJSON_IsString(author)) {
            strncpy(info->author, author->valuestring, sizeof(info->author) - 1);
        }
    }

    /* Model input specification */
    json = cJSON_GetObjectItemCaseSensitive(root, "input_spec");
    if (cJSON_IsObject(json)) {
        cJSON *width = cJSON_GetObjectItemCaseSensitive(json, "width");
        if (cJSON_IsNumber(width)) {
            info->input_width = (int)width->valuedouble;
        }
        cJSON *height = cJSON_GetObjectItemCaseSensitive(json, "height");
        if (cJSON_IsNumber(height)) {
            info->input_height = (int)height->valuedouble;
        }
        cJSON *channels = cJSON_GetObjectItemCaseSensitive(json, "channels");
        if (cJSON_IsNumber(channels)) {
            info->input_channels = (int)channels->valuedouble;
        }
        cJSON *data_type = cJSON_GetObjectItemCaseSensitive(json, "data_type");
        if (cJSON_IsString(data_type)) {
            strncpy(info->input_data_type, data_type->valuestring, sizeof(info->input_data_type) - 1);
            info->is_int8_input = (strcmp(data_type->valuestring, "int8") == 0);
        }
        cJSON *color_format = cJSON_GetObjectItemCaseSensitive(json, "color_format");
        if (cJSON_IsString(color_format)) {
            strncpy(info->color_format, color_format->valuestring, sizeof(info->color_format) - 1);
        }
    }

    /* Model output specification */
    json = cJSON_GetObjectItemCaseSensitive(root, "output_spec");
    if (cJSON_IsObject(json)) {
        cJSON *outputs = cJSON_GetObjectItemCaseSensitive(json, "outputs");
        if (cJSON_IsArray(outputs)) {
            cJSON *output = cJSON_GetArrayItem(outputs, 0);
            if (cJSON_IsObject(output)) {
                cJSON *data_type = cJSON_GetObjectItemCaseSensitive(output, "data_type");
                if (cJSON_IsString(data_type)) {
                    strncpy(info->output_data_type, data_type->valuestring, sizeof(info->output_data_type) - 1);
                }
            }
        }
    }

    /* Post-processing type */
    json = cJSON_GetObjectItemCaseSensitive(root, "postprocess_type");
    if (cJSON_IsString(json)) {
        strncpy(info->postprocess_type, json->valuestring, sizeof(info->postprocess_type) - 1);
    }

    cJSON_Delete(root);
    
    /* Metadata */
    root = cJSON_Parse((const char *)info->metadata_ptr);
    if (root == NULL) {
        LOG_DRV_ERROR("load_info: JSON parse failed\r\r\n");
        storage_unlock();
        return -1;
    }
    /* Creation time */
    json = cJSON_GetObjectItemCaseSensitive(root, "created_at");
    if (cJSON_IsString(json)) {
        strncpy(info->created_at, json->valuestring, sizeof(info->created_at) - 1);
    }
    /* ST Edge AI version */
    json = cJSON_GetObjectItemCaseSensitive(root, "stedgeai_version");
    if (cJSON_IsString(json)) {
        strncpy(info->stedgeai_version, json->valuestring, sizeof(info->stedgeai_version) - 1);
        info->stedgeai_version[sizeof(info->stedgeai_version) - 1] = '\0';
    } else {
        info->stedgeai_version[0] = '\0';
    }

    cJSON_Delete(root);

    if (strstr(info->stedgeai_version, MODEL_STEDGEAI_VERSION_SUPPORTED) == NULL) {
        LOG_DRV_ERROR("ST Edge AI version not supported, supported: %s, current: %s\r\r\n", MODEL_STEDGEAI_VERSION_SUPPORTED, info->stedgeai_version);
        storage_unlock();
        return -1;
    }

    storage_unlock();
    return 0;
}

static void update_inference_stats(nn_t *nn, uint32_t inference_time)
{
    if (!nn) {
        return;
    }
    nn->inference_count++;
    nn->total_inference_time += inference_time;
}

static int model_init(const uintptr_t model_ptr, nn_t *nn)
{
    if (!model_ptr) {
        return -1;
    }

    storage_lock();

    /* Print model information */
    ll_aton_reloc_log_info(model_ptr);

    /* Get model information */
    ll_aton_reloc_info rt;
    int res = ll_aton_reloc_get_info(model_ptr, &rt);
    if (res != 0) {
        LOG_DRV_ERROR("ll_aton_reloc_get_info failed %d\r\r\n", res);
        storage_unlock();
        return -1;
    }
    /* Create and install an instance of the relocatable model */
    ll_aton_reloc_config config;

    nn->exec_ram_addr = hal_mem_alloc_large(rt.rt_ram_copy);
    nn->ext_ram_addr = hal_mem_alloc_large(rt.ext_ram_sz);
    if (nn->exec_ram_addr == NULL || nn->ext_ram_addr == NULL) {
        LOG_DRV_ERROR("model_init: OOM\r\r\n");
        storage_unlock();
        return -1;
    }
    config.exec_ram_addr = (uintptr_t)nn->exec_ram_addr;
    config.exec_ram_size = rt.rt_ram_copy;
    config.ext_ram_addr = (uintptr_t)nn->ext_ram_addr;
    config.ext_ram_size = rt.ext_ram_sz;
    config.ext_param_addr = 0;  /* or @ of the weights/params if split mode is used */
    config.mode = AI_RELOC_RT_LOAD_MODE_COPY; // | AI_RELOC_RT_LOAD_MODE_CLEAR;

    LOG_DRV_INFO("Installing relocatable model...\r\r\n");
    LOG_DRV_INFO("  Model file: 0x%08lX\r\r\n", (uint32_t)model_ptr);
    LOG_DRV_INFO("  Exec RAM: 0x%08lX (size: %lu)\r\r\n", (uint32_t)config.exec_ram_addr, config.exec_ram_size);
    LOG_DRV_INFO("  Ext RAM: 0x%08lX (size: %u)\r\r\n", (uint32_t)config.ext_ram_addr, config.ext_ram_size);
    LOG_DRV_INFO("  Mode: 0x%08lX\r\r\n", config.mode);

    nn->nn_inst = (NN_Instance_TypeDef *)hal_mem_alloc_any(sizeof(NN_Instance_TypeDef));
    if (nn->nn_inst == NULL) {
        LOG_DRV_ERROR("model_init: OOM\r\r\n");
        storage_unlock();
        return -1;
    }

    res = ll_aton_reloc_install(model_ptr, &config, nn->nn_inst);
    if (res != 0) {
        LOG_DRV_ERROR("ll_aton_reloc_install failed %d\r\r\n", res);
        hal_mem_free(nn->nn_inst);
        nn->nn_inst = NULL;
        storage_unlock();
        return -1;
    }

    storage_unlock();

    const LL_Buffer_InfoTypeDef *ll_buffer = NULL;
    while (nn->input_buffer_count < NN_MAX_INPUT_BUFFER) {
        ll_buffer = ll_aton_reloc_get_input_buffers_info(nn->nn_inst, nn->input_buffer_count);
        if (ll_buffer == NULL || ll_buffer->name == NULL) {
            break;
        }
        nn->input_buffer[nn->input_buffer_count] = (void *)LL_Buffer_addr_start(ll_buffer);
        nn->input_buffer_size[nn->input_buffer_count] = LL_Buffer_len(ll_buffer);
        LOG_DRV_DEBUG("input_buffer[%d]: 0x%08lX (size: %lu)\r\r\n", nn->input_buffer_count,
                      (uint32_t)nn->input_buffer[nn->input_buffer_count], nn->input_buffer_size[nn->input_buffer_count]);
        nn->input_buffer_count++;
    }

    while (nn->output_buffer_count < NN_MAX_OUTPUT_BUFFER) {
        ll_buffer = ll_aton_reloc_get_output_buffers_info(nn->nn_inst, nn->output_buffer_count);
        if (ll_buffer == NULL || ll_buffer->name == NULL) {
            break;
        }
        nn->output_buffer[nn->output_buffer_count] = (void *)LL_Buffer_addr_start(ll_buffer);
        nn->output_buffer_size[nn->output_buffer_count] = LL_Buffer_len(ll_buffer);
        LOG_DRV_DEBUG("output_buffer[%d]: 0x%08lX (size: %lu)\r\r\n", nn->output_buffer_count,
                      (uint32_t)nn->output_buffer[nn->output_buffer_count], nn->output_buffer_size[nn->output_buffer_count]);
        nn->output_buffer_count++;
    }

    LL_ATON_RT_Init_Network(nn->nn_inst);

    return 0;

}

static int model_deinit(nn_t *nn)
{
    if (nn->nn_inst) {
        LL_ATON_RT_DeInit_Network(nn->nn_inst);
        hal_mem_free(nn->nn_inst);
        nn->nn_inst = NULL;
    }
    /* reset IO buffer pointers, sizes and counts */
    if (nn->input_buffer_count) {
        for (uint32_t i = 0; i < nn->input_buffer_count; i++) {
            nn->input_buffer[i] = NULL;
            nn->input_buffer_size[i] = 0;
        }
        nn->input_buffer_count = 0;
    }
    if (nn->output_buffer_count) {
        for (uint32_t i = 0; i < nn->output_buffer_count; i++) {
            nn->output_buffer[i] = NULL;
            nn->output_buffer_size[i] = 0;
        }
        nn->output_buffer_count = 0;
    }
    if (nn->exec_ram_addr) {
        hal_mem_free(nn->exec_ram_addr);
        nn->exec_ram_addr = NULL;
    }
    if (nn->ext_ram_addr) {
        hal_mem_free(nn->ext_ram_addr);
        nn->ext_ram_addr = NULL;
    }
    return 0;
}

static int model_run(nn_t *nn, nn_result_t *result, bool is_callback)
{
    if (nn->nn_inst) {
        // Serialize NPU access across all instances
        npu_mutex_acquire();
        /* flush input cache */
        flush_input_cache(nn);
        /* start time */
        uint32_t start_time = osKernelGetTickCount();
        /* Run inference using LL_ATON */
        LL_ATON_RT_RetValues_t ll_aton_ret;
        do {
            ll_aton_ret = LL_ATON_RT_RunEpochBlock(nn->nn_inst);
            if (ll_aton_ret == LL_ATON_RT_WFE) {
                LL_ATON_OSAL_WFE();
            }
        } while (ll_aton_ret != LL_ATON_RT_DONE);
        /* reset network */
        LL_ATON_RT_Reset_Network(nn->nn_inst);
        /* invalidate output cache before CPU reads NPU results */
        invalidate_output_cache(nn);
        /* postprocess */
        if (nn->pp_vt && nn->pp_vt->run) {
            if (nn->pp_vt->run((void **)nn->output_buffer, nn->output_buffer_count, result, nn->pp_params, nn->nn_inst) != 0) {
                LOG_DRV_ERROR("model_run: postprocess run failed\r\r\n");
                npu_mutex_release();
                return -1;
            }
            /* end time */
            update_inference_stats(nn, osKernelGetTickCount() - start_time);
            if (is_callback && nn->callback) {
                nn->callback(result, nn->callback_user_data);
            }
        }
        npu_mutex_release();
        return 0;
    }
    return -1;
}

static int load_model(nn_t *nn, const uintptr_t file_ptr)
{
    if (!file_ptr) {
        return -1;
    }
    LOG_DRV_INFO("Loading model: 0x%lx\r\r\n", file_ptr);

    /* load model information */
    if (load_info(file_ptr, &nn->model) != 0) {
        LOG_DRV_ERROR("load_model: load model info failed\r\r\n");
        return -1;
    }

    /* initialize model */
    if (model_init(nn->model.model_ptr, nn) != 0) {
        LOG_DRV_ERROR("load_model: model init failed\r\r\n");
        return -1;
    }
    
    /* load postprocess */
    const pp_vtable_t *pp_vt = pp_find((const char *)nn->model.postprocess_type);
    if (pp_vt == NULL) {
        LOG_DRV_ERROR("load_model: postprocess type[%s] not found\r\r\n", nn->model.postprocess_type);
        return -1;
    }

    /* initialize postprocess */
    if (pp_vt->init && pp_vt->init((const char *)nn->model.config_ptr, &nn->pp_params, nn->nn_inst) != 0) {
        LOG_DRV_ERROR("load_model: postprocess init failed\r\r\n");
        return -1;
    }

    nn->pp_vt = pp_vt;

    LOG_DRV_INFO("Model loaded successfully\r\r\n");
    return 0;
}

static int unload_model(nn_t *nn)
{
    LOG_DRV_INFO("Unloading model\r\r\n");

    // deinit postprocess
    if (nn->pp_vt && nn->pp_vt->deinit) {
        nn->pp_vt->deinit(nn->pp_params);
    }
    nn->pp_vt = NULL;
    nn->pp_params = NULL;
    // unload model
    model_deinit(nn);
    // clear model information
    memset(&nn->model, 0, sizeof(nn_model_info_t));

    LOG_DRV_INFO("Model unloaded successfully\r\r\n");
    return 0;
}

static int validate_model(const uintptr_t file_ptr)
{
    if (!file_ptr) {
        return -1;
    }

    storage_lock();

    nn_package_header_t *header = (nn_package_header_t *)file_ptr;

    /* Check magic number */
    if (header->magic != MODEL_PACKAGE_MAGIC) {
        LOG_DRV_ERROR("Invalid model package magic number\r\r\n");
        storage_unlock();
        return NN_ERROR_INVALID_PACKAGE;
    }

    /* Check version */
    if (header->version != MODEL_PACKAGE_VERSION) {
        LOG_DRV_ERROR("Incompatible model package version 0X%lx\r\r\n", header->version);
        storage_unlock();
        return NN_ERROR_INCOMPATIBLE;
    }

    /* Quick size validation */
    if (header->package_size == 0 || header->relocatable_model_size == 0) {
        LOG_DRV_ERROR("Invalid package size\r\r\n");
        storage_unlock();
        return NN_ERROR_INVALID_PACKAGE;
    }

    /* Validate relocatable model magic */
    const uint32_t *model_magic = (const uint32_t *)(file_ptr + header->relocatable_model_offset);
    if (*model_magic != MODEL_RELOCATABLE_MAGIC) {
        LOG_DRV_ERROR("Invalid relocatable model magic number\r\r\n");
        storage_unlock();
        return NN_ERROR_INVALID_MODEL;
    }

    /* Validate header checksum */
    uint32_t checksum = generic_crc32((const uint8_t *)header, offsetof(nn_package_header_t, header_checksum));
    if (checksum != header->header_checksum) {
        LOG_DRV_ERROR("Invalid header checksum\r\r\n");
        storage_unlock();
        return NN_ERROR_INVALID_CHECKSUM;
    }

    /* Validate model checksum */
    checksum = generic_crc32((const uint8_t *)(file_ptr + header->relocatable_model_offset),
                             header->relocatable_model_size);
    if (checksum != header->model_checksum) {
        LOG_DRV_ERROR("Invalid relocatable model checksum\r\r\n");
        storage_unlock();
        return NN_ERROR_INVALID_CHECKSUM;
    }

    /* Validate config checksum */
    checksum = generic_crc32((const uint8_t *)(file_ptr + header->model_config_offset), header->model_config_size);
    if (checksum != header->config_checksum) {
        LOG_DRV_ERROR("Invalid config checksum\r\r\n");
        storage_unlock();
        return NN_ERROR_INVALID_CHECKSUM;
    }

    storage_unlock();
    return NN_ERROR_OK;
}

/* ==================== multi-instance public API implementation ==================== */

static int add_new_instance(nn_handle_t handle)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn) {
        return -1;
    }
    for (int i = 0; i < NN_MAX_INSTANCES; i++) {
        if (g_nn_instances[i] == NULL) {
            g_nn_instances[i] = nn;
            return 0;
        }
    }
    return -1;
}

static int remove_instance(nn_handle_t handle)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn) {
        return -1;
    }
    for (int i = 0; i < NN_MAX_INSTANCES; i++) {
        if (g_nn_instances[i] == nn) {
            g_nn_instances[i] = NULL;
            return 0;
        }
    }
    return -1;
}

static nn_t *get_instance(int index)
{
    if (index < 0 || index >= NN_MAX_INSTANCES) {
        return NULL;
    }
    return g_nn_instances[index];
}

nn_handle_t nn_instance_create(void)
{
    nn_t *nn = (nn_t *)hal_mem_alloc_fast(sizeof(nn_t));
    if (!nn) {
        return NULL;
    }
    memset(nn, 0, sizeof(nn_t));

    // Create mutex
    nn->mtx_id = osMutexNew(NULL);
    if (nn->mtx_id == NULL) {
        hal_mem_free(nn);
        return NULL;
    }

    nn->state = NN_STATE_INIT;
    // Add instance
    add_new_instance(nn);

    return nn;
}

int nn_instance_destroy(nn_handle_t handle)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn) {
        return -1;
    }

    if (nn->state == NN_STATE_READY) {
        LOG_DRV_ERROR("model is still loaded, please unload it first\r\r\n");
        return -1;
    }
    // Delete mutex
    if (nn->mtx_id) {
        osMutexDelete(nn->mtx_id);
        nn->mtx_id = NULL;
    }

    nn->state = NN_STATE_UNINIT;
    // Remove instance
    remove_instance(nn);

    // Free instance memory
    hal_mem_free(nn);
    return 0;
}

int nn_instance_load_model(nn_handle_t handle, const uintptr_t file_ptr)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state == NN_STATE_READY) {
        LOG_DRV_WARN("model already loaded\r\r\n");
        return 0;
    }

    osMutexAcquire(nn->mtx_id, osWaitForever);
    if (load_model(nn, file_ptr) != 0) {
        osMutexRelease(nn->mtx_id);
        return -1;
    }
    nn->state = NN_STATE_READY;
    osMutexRelease(nn->mtx_id);
    return 0;
}

int nn_instance_unload_model(nn_handle_t handle)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY) {
        LOG_DRV_WARN("model already unloaded\r\r\n");
        return 0;
    }

    osMutexAcquire(nn->mtx_id, osWaitForever);
    if (unload_model(nn) != 0) {
        osMutexRelease(nn->mtx_id);
        return -1;
    }
    nn->state = NN_STATE_INIT;
    osMutexRelease(nn->mtx_id);
    return 0;
}

int nn_instance_get_model_input_buffer(nn_handle_t handle, uint8_t **buffer, uint32_t *size)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !buffer || !size) {
        LOG_DRV_ERROR("model not loaded or buffer or size is NULL\r\r\n");
        return -1;
    }

    osMutexAcquire(nn->mtx_id, osWaitForever);
    *buffer = (uint8_t *)nn->input_buffer[0];
    *size = nn->input_buffer_size[0];
    osMutexRelease(nn->mtx_id);

    return 0;
}

int nn_instance_get_model_output_buffer(nn_handle_t handle, uint8_t **buffer, uint32_t *size)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !buffer || !size) {
        LOG_DRV_ERROR("model not loaded or buffer or size is NULL\r\r\n");
        return -1;
    }

    osMutexAcquire(nn->mtx_id, osWaitForever);
    *buffer = (uint8_t *)nn->output_buffer[0];
    *size = nn->output_buffer_size[0];
    osMutexRelease(nn->mtx_id);

    return 0;
}

int nn_instance_get_model_info(nn_handle_t handle, nn_model_info_t *info)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !info) {
        LOG_DRV_ERROR("model not loaded or info is NULL\r\r\n");
        return -1;
    }

    osMutexAcquire(nn->mtx_id, osWaitForever);
    memcpy(info, &nn->model, sizeof(nn_model_info_t));
    osMutexRelease(nn->mtx_id);

    return 0;
}

int nn_instance_inference_frame(nn_handle_t handle, uint8_t *input_data, uint32_t input_size, nn_result_t *result)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !input_data || !result) {
        LOG_DRV_ERROR("model not loaded or input_data or result is NULL\r\r\n");
        return -1;
    }

    osMutexAcquire(nn->mtx_id, osWaitForever);

    /* Convert camera uint8 to model input: float32, int8, or uint8 passthrough */
    if (nn->input_buffer_size[0] == input_size * 4) {
        /* uint8 → float32: (pixel - 127.5) * (1/127.5) → [-1, 1] */
        float *dst = (float *)nn->input_buffer[0];
        for (uint32_t i = 0; i < input_size; i++) {
            dst[i] = ((float)input_data[i] - 127.5f) * 0.00784313725f;
        }
    } else if (nn->input_buffer_size[0] != input_size) {
        LOG_DRV_ERROR("input_buffer_size[0] != input_size\r\r\n");
        osMutexRelease(nn->mtx_id);
        return -1;
    } else if (nn->model.is_int8_input) {
        int8_t *dst = (int8_t *)nn->input_buffer[0];
        for (uint32_t i = 0; i < input_size; i++) {
            dst[i] = (int8_t)((int)input_data[i] - 128);
        }
    } else {
        memcpy(nn->input_buffer[0], input_data, input_size);
    }

    int ret = model_run(nn, result, false);
    osMutexRelease(nn->mtx_id);

    return ret;
}

int nn_instance_set_confidence_threshold(nn_handle_t handle, float threshold)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY) {
        LOG_DRV_ERROR("model not loaded\r\r\n");
        return -1;
    }
    osMutexAcquire(nn->mtx_id, osWaitForever);
    if (nn->pp_vt && nn->pp_vt->set_confidence_threshold) {
        nn->pp_vt->set_confidence_threshold(nn->pp_params, threshold);
    }
    osMutexRelease(nn->mtx_id);

    return 0;
}

int nn_instance_get_confidence_threshold(nn_handle_t handle, float *threshold)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !threshold) {
        LOG_DRV_ERROR("model not loaded or threshold is NULL\r\r\n");
        return -1;
    }
    osMutexAcquire(nn->mtx_id, osWaitForever);
    if (nn->pp_vt && nn->pp_vt->get_confidence_threshold) {
        nn->pp_vt->get_confidence_threshold(nn->pp_params, threshold);
    }
    osMutexRelease(nn->mtx_id);

    return 0;
}

int nn_instance_set_nms_threshold(nn_handle_t handle, float threshold)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY) {
        LOG_DRV_ERROR("model not loaded\r\r\n");
        return -1;
    }
    osMutexAcquire(nn->mtx_id, osWaitForever);
    if (nn->pp_vt && nn->pp_vt->set_nms_threshold) {
        nn->pp_vt->set_nms_threshold(nn->pp_params, threshold);
    }
    osMutexRelease(nn->mtx_id);

    return 0;
}

int nn_instance_get_nms_threshold(nn_handle_t handle, float *threshold)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !threshold) {
        LOG_DRV_ERROR("model not loaded or threshold is NULL\r\r\n");
        return -1;
    }
    osMutexAcquire(nn->mtx_id, osWaitForever);
    if (nn->pp_vt && nn->pp_vt->get_nms_threshold) {
        nn->pp_vt->get_nms_threshold(nn->pp_params, threshold);
    }
    osMutexRelease(nn->mtx_id);

    return 0;
}

nn_state_t nn_instance_get_state(nn_handle_t handle)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn) {
        return NN_STATE_UNINIT;
    }
    return nn->state;
}

int nn_instance_get_inference_stats(nn_handle_t handle, uint32_t *count, uint32_t *total_time)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn || nn->state != NN_STATE_READY || !count || !total_time) {
        LOG_DRV_ERROR("model not loaded or count or total_time is NULL\r\r\n");
        return -1;
    }

    *count = nn->inference_count;
    *total_time = nn->total_inference_time;

    return 0;
}

int nn_instance_set_callback(nn_handle_t handle, nn_callback_t callback, void *user_data)
{
    nn_t *nn = (nn_t *)handle;
    if (!nn) {
        return -1;
    }
    nn->callback = callback;
    nn->callback_user_data = user_data;
    return 0;
}


/* ==================== JSON creation functions ==================== */
/**
 * @brief Create detection result JSON
 */
static cJSON* create_detection_json(const od_detect_t* detection, int index) {
    cJSON* detection_json = cJSON_CreateObject();
    if (!detection_json) return NULL;
    
    cJSON_AddNumberToObject(detection_json, "index", index);
    cJSON_AddStringToObject(detection_json, "class_name", detection->class_name);
    cJSON_AddNumberToObject(detection_json, "confidence", detection->conf);
    cJSON_AddNumberToObject(detection_json, "x", detection->x);
    cJSON_AddNumberToObject(detection_json, "y", detection->y);
    cJSON_AddNumberToObject(detection_json, "width", detection->width);
    cJSON_AddNumberToObject(detection_json, "height", detection->height);

    return detection_json;
}

/**
 * @brief Create ISEG segment detection JSON
 */
static cJSON* create_iseg_detection_json(const iseg_detect_t* detection, int index) {
    cJSON* detection_json = cJSON_CreateObject();
    if (!detection_json) return NULL;

    cJSON_AddNumberToObject(detection_json, "index", index);
    cJSON_AddStringToObject(detection_json, "class_name", detection->class_name);
    cJSON_AddNumberToObject(detection_json, "confidence", detection->conf);
    cJSON_AddNumberToObject(detection_json, "x", detection->x);
    cJSON_AddNumberToObject(detection_json, "y", detection->y);
    cJSON_AddNumberToObject(detection_json, "width", detection->width);
    cJSON_AddNumberToObject(detection_json, "height", detection->height);
    cJSON_AddNumberToObject(detection_json, "mask_size", detection->mask_size);

    return detection_json;
}

/**
 * @brief Create keypoint JSON
 */
static cJSON* create_keypoint_json(const keypoint_t* keypoint, int index) {
    cJSON* keypoint_json = cJSON_CreateObject();
    if (!keypoint_json) return NULL;

    cJSON_AddNumberToObject(keypoint_json, "index", index);
    cJSON_AddNumberToObject(keypoint_json, "x", keypoint->x);
    cJSON_AddNumberToObject(keypoint_json, "y", keypoint->y);
    cJSON_AddNumberToObject(keypoint_json, "confidence", keypoint->conf);

    return keypoint_json;
}

/**
 * @brief Add keypoint connections ("connections" array + "connection_count") to a pose/detection JSON
 */
static void add_connections_json(cJSON* parent_json, const uint8_t* connections, uint8_t num_connections) {
    if (!connections || num_connections == 0) return;

    cJSON* connections_array = cJSON_CreateArray();
    if (connections_array) {
        for (uint8_t i = 0; i < num_connections; i++) {
            cJSON* connection_json = cJSON_CreateObject();
            if (connection_json) {
                cJSON_AddNumberToObject(connection_json, "from", connections[i * 2]);
                cJSON_AddNumberToObject(connection_json, "to", connections[i * 2 + 1]);
                cJSON_AddItemToArray(connections_array, connection_json);
            }
        }
        cJSON_AddItemToObject(parent_json, "connections", connections_array);
    }
    cJSON_AddNumberToObject(parent_json, "connection_count", num_connections);
}

/**
 * @brief Find a keypoint by name and copy its geometry into out.
 * @retval 1 found, out filled.
 * @retval 0 not found (or no name table).
 */
static int find_keypoint_by_name(const mpe_detect_t* detection, const char* name, gauge_point_t* out)
{
    uint32_t i;

    if ((detection == NULL) || (name == NULL) || (out == NULL) || (detection->keypoint_names == NULL)) {
        return 0;
    }
    for (i = 0; i < detection->nb_keypoints; i++) {
        if ((detection->keypoint_names[i] != NULL) && (strcmp(detection->keypoint_names[i], name) == 0)) {
            out->x    = detection->keypoints[i].x;
            out->y    = detection->keypoints[i].y;
            out->conf = detection->keypoints[i].conf;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Create MPE detection result JSON
 */
static cJSON* create_mpe_detection_json(const mpe_detect_t* detection, int index) {
    cJSON* detection_json = cJSON_CreateObject();
    if (!detection_json) return NULL;
    
    cJSON_AddNumberToObject(detection_json, "index", index);
    cJSON_AddStringToObject(detection_json, "class_name", detection->class_name ? detection->class_name : "person");
    cJSON_AddNumberToObject(detection_json, "confidence", detection->conf);
    cJSON_AddNumberToObject(detection_json, "x", detection->x);
    cJSON_AddNumberToObject(detection_json, "y", detection->y);
    cJSON_AddNumberToObject(detection_json, "width", detection->width);
    cJSON_AddNumberToObject(detection_json, "height", detection->height);
    
    // Add keypoints array
    cJSON* keypoints_array = cJSON_CreateArray();
    if (keypoints_array) {
        for (uint32_t i = 0; i < detection->nb_keypoints && i < 33; i++) {
            cJSON* keypoint_json = create_keypoint_json(&detection->keypoints[i], i);
            if (keypoint_json) {
                // Add keypoint name if available
                if (detection->keypoint_names && detection->keypoint_names[i]) {
                    cJSON_AddStringToObject(keypoint_json, "name", detection->keypoint_names[i]);
                }
                cJSON_AddItemToArray(keypoints_array, keypoint_json);
            }
        }
        cJSON_AddItemToObject(detection_json, "keypoints", keypoints_array);
    }
    cJSON_AddNumberToObject(detection_json, "keypoint_count", detection->nb_keypoints);

    {
        gauge_point_t g_center;
        gauge_point_t g_min;
        gauge_point_t g_max;
        gauge_point_t g_tip;
        gauge_reading_t gr;

        if (find_keypoint_by_name(detection, "center", &g_center) &&
            find_keypoint_by_name(detection, "min", &g_min) &&
            find_keypoint_by_name(detection, "max", &g_max) &&
            find_keypoint_by_name(detection, "tip", &g_tip) &&
            (gauge_reading_compute(&g_center, &g_min, &g_max, &g_tip,
                                   0.0f, 100.0f, 0.0f, &gr) == 0)) {
            cJSON* reading_json = cJSON_CreateObject();
            if (reading_json != NULL) {
                cJSON_AddNumberToObject(reading_json, "value",
                    (double)(roundf(gr.value * 100.0f) / 100.0f));
                cJSON_AddNumberToObject(reading_json, "ratio",
                    (double)(roundf(gr.ratio * 10000.0f) / 10000.0f));
                cJSON_AddStringToObject(reading_json, "direction", gr.direction);
                cJSON_AddItemToObject(detection_json, "reading", reading_json);
            }
        }
    }
    
    // Add connections array if available
    add_connections_json(detection_json, detection->keypoint_connections, detection->num_connections);

    return detection_json;
}

/**
 * @brief Create SPE pose result JSON (detector-free single instance, no bounding box)
 */
static cJSON* create_spe_pose_json(const pp_spe_out_t* spe_result, int index) {
    cJSON* pose_json = cJSON_CreateObject();
    if (!pose_json) return NULL;

    cJSON_AddNumberToObject(pose_json, "index", index);

    // Add keypoints array
    cJSON* keypoints_array = cJSON_CreateArray();
    if (keypoints_array) {
        for (uint32_t i = 0; i < spe_result->nb_keypoints; i++) {
            cJSON* keypoint_json = create_keypoint_json(&spe_result->keypoints[i], i);
            if (keypoint_json) {
                // Add keypoint name if available
                if (spe_result->keypoint_names && spe_result->keypoint_names[i]) {
                    cJSON_AddStringToObject(keypoint_json, "name", spe_result->keypoint_names[i]);
                }
                cJSON_AddItemToArray(keypoints_array, keypoint_json);
            }
        }
        cJSON_AddItemToObject(pose_json, "keypoints", keypoints_array);
    }
    cJSON_AddNumberToObject(pose_json, "keypoint_count", spe_result->nb_keypoints);

    // Add connections array if available
    add_connections_json(pose_json, spe_result->keypoint_connections, spe_result->num_connections);

    return pose_json;
}

/**
 * @brief Create AI result JSON
 */
cJSON* nn_create_ai_result_json(const nn_result_t* ai_result) {
    cJSON* result_json = cJSON_CreateObject();
    if (!result_json) return NULL;
    
    cJSON_AddNumberToObject(result_json, "type", ai_result->type);
    
    if (ai_result->type == PP_TYPE_OD && ai_result->od.nb_detect > 0) {
        // Object Detection results
        cJSON* detections_array = cJSON_CreateArray();
        if (detections_array) {
            for (int i = 0; i < ai_result->od.nb_detect; i++) {
                cJSON* detection_json = create_detection_json(&ai_result->od.detects[i], i);
                if (detection_json) {
                    cJSON_AddItemToArray(detections_array, detection_json);
                }
            }
            cJSON_AddItemToObject(result_json, "detections", detections_array);
        }
        cJSON_AddNumberToObject(result_json, "detection_count", ai_result->od.nb_detect);
        
        // Add empty pose results for consistency
        cJSON_AddItemToObject(result_json, "poses", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "pose_count", 0);
        
    } else if (ai_result->type == PP_TYPE_MPE && ai_result->mpe.nb_detect > 0) {
        // Multi-Pose Estimation results
        cJSON* poses_array = cJSON_CreateArray();
        if (poses_array) {
            for (int i = 0; i < ai_result->mpe.nb_detect; i++) {
                cJSON* pose_json = create_mpe_detection_json(&ai_result->mpe.detects[i], i);
                if (pose_json) {
                    cJSON_AddItemToArray(poses_array, pose_json);
                }
            }
            cJSON_AddItemToObject(result_json, "poses", poses_array);
        }
        cJSON_AddNumberToObject(result_json, "pose_count", ai_result->mpe.nb_detect);

        // Add empty detection results for consistency
        cJSON_AddItemToObject(result_json, "detections", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "detection_count", 0);

    } else if (ai_result->type == PP_TYPE_SPE && ai_result->spe.keypoints != NULL &&
               ai_result->spe.nb_keypoints > 0) {
        // Single-Pose Estimation results (detector-free, one instance, no bounding box)
        int pose_count = 0;
        cJSON* poses_array = cJSON_CreateArray();
        if (poses_array) {
            cJSON* pose_json = create_spe_pose_json(&ai_result->spe, 0);
            if (pose_json) {
                cJSON_AddItemToArray(poses_array, pose_json);
                pose_count = 1;
            }
            cJSON_AddItemToObject(result_json, "poses", poses_array);
        }
        cJSON_AddNumberToObject(result_json, "pose_count", pose_count);

        // Add empty detection results for consistency
        cJSON_AddItemToObject(result_json, "detections", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "detection_count", 0);

    } else if (ai_result->type == PP_TYPE_ISEG && ai_result->iseg.nb_detect > 0) {
        /* Instance Segmentation results */
        cJSON* segments_array = cJSON_CreateArray();
        if (segments_array) {
            for (int i = 0; i < ai_result->iseg.nb_detect; i++) {
                cJSON* seg = create_iseg_detection_json(&ai_result->iseg.detects[i], i);
                if (seg) {
                    cJSON_AddItemToArray(segments_array, seg);
                }
            }
            cJSON_AddItemToObject(result_json, "segments", segments_array);
        }
        cJSON_AddNumberToObject(result_json, "segment_count", ai_result->iseg.nb_detect);

        cJSON_AddItemToObject(result_json, "detections", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "detection_count", 0);
        cJSON_AddItemToObject(result_json, "poses", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "pose_count", 0);

    } else {
        // No results or unsupported type
        cJSON_AddItemToObject(result_json, "detections", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "detection_count", 0);
        cJSON_AddItemToObject(result_json, "poses", cJSON_CreateArray());
        cJSON_AddNumberToObject(result_json, "pose_count", 0);
    }
    
    // Add type name for better frontend understanding
    const char* type_name = "unknown";
    switch (ai_result->type) {
        case PP_TYPE_OD:
            type_name = "object_detection";
            break;
        case PP_TYPE_MPE:
            type_name = "multi_pose_estimation";
            break;
        case PP_TYPE_SEG:
            type_name = "segmentation";
            break;
        case PP_TYPE_CLASS:
            type_name = "classification";
            break;
        case PP_TYPE_PD:
            type_name = "person_detection";
            break;
        case PP_TYPE_SPE:
            type_name = "single_pose_estimation";
            break;
        case PP_TYPE_ISEG:
            type_name = "instance_segmentation";
            break;
        case PP_TYPE_SSEG:
            type_name = "semantic_segmentation";
            break;
        default:
            type_name = "unknown";
            break;
    }
    cJSON_AddStringToObject(result_json, "type_name", type_name);

    return result_json;
}

/* ==================== Compact CBOR encoding ==================== */

// Longest class name carried in a compact payload; longer names truncate
#define NN_CBOR_CLASS_NAME_MAX 63

/**
 * @brief Clamp to [0,1] before half-precision encoding
 * @details The pp layer floor-clamps some MPE fields only (box and keypoint
 *          x/y have no upper clamp); NaN maps to 0.
 */
static float nn_cbor_clamp01(float value) {
    if (!(value > 0.0f)) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static void nn_cbor_put_class(cbor_enc_t* enc, const char* class_name, const char* fallback) {
    const char* name = class_name ? class_name : (fallback ? fallback : "");
    size_t len = strlen(name);
    if (len > NN_CBOR_CLASS_NAME_MAX) {
        len = NN_CBOR_CLASS_NAME_MAX;
        // Never cut a multi-byte UTF-8 sequence: RFC 8949 text strings must be
        // valid UTF-8, and strict decoders reject the whole document otherwise
        while (len > 0 && ((unsigned char)name[len] & 0xC0U) == 0x80U) {
            len--;
        }
    }
    cbor_enc_text(enc, "c");
    cbor_enc_text_n(enc, name, len);
}

static void nn_cbor_put_conf(cbor_enc_t* enc, float conf) {
    cbor_enc_text(enc, "conf");
    cbor_enc_f16(enc, nn_cbor_clamp01(conf));
}

static void nn_cbor_put_box(cbor_enc_t* enc, float x, float y, float width, float height) {
    cbor_enc_text(enc, "box");
    cbor_enc_array(enc, 4);
    cbor_enc_f16(enc, nn_cbor_clamp01(x));
    cbor_enc_f16(enc, nn_cbor_clamp01(y));
    cbor_enc_f16(enc, nn_cbor_clamp01(width));
    cbor_enc_f16(enc, nn_cbor_clamp01(height));
}

static void nn_cbor_put_keypoints(cbor_enc_t* enc, const keypoint_t* keypoints, uint32_t count) {
    if (!keypoints) {
        count = 0;
    }
    cbor_enc_text(enc, "kp");
    cbor_enc_array(enc, count);
    for (uint32_t i = 0; i < count && !enc->overflow; i++) {
        cbor_enc_array(enc, 3);
        cbor_enc_f16(enc, nn_cbor_clamp01(keypoints[i].x));
        cbor_enc_f16(enc, nn_cbor_clamp01(keypoints[i].y));
        cbor_enc_f16(enc, nn_cbor_clamp01(keypoints[i].conf));
    }
}

/**
 * @brief Encode AI result as a compact CBOR map (see nn.h)
 * @details Map shape mirrors the JSON serializer's dispatch: {"type": N} plus
 *          one kind-specific key (OD "det", MPE/SPE "poses", ISEG "seg");
 *          unknown kinds carry {"type": N} alone. Counts and pointers in the
 *          result are trusted exactly as nn_create_ai_result_json trusts them
 *          - the caller guarantees they match their backing allocations.
 *          Loops stop early once the output buffer overflows.
 */
int nn_encode_ai_result_cbor(const nn_result_t* ai_result, uint8_t* buf, size_t cap) {
    if (!ai_result || !buf) {
        return -1;
    }

    cbor_enc_t enc;
    cbor_enc_init(&enc, buf, cap);

    switch (ai_result->type) {
    case PP_TYPE_OD: {
        uint8_t count = ai_result->od.detects ? ai_result->od.nb_detect : 0;
        cbor_enc_map(&enc, 2);
        cbor_enc_text(&enc, "type");
        cbor_enc_uint(&enc, PP_TYPE_OD);
        cbor_enc_text(&enc, "det");
        cbor_enc_array(&enc, count);
        for (uint8_t i = 0; i < count && !enc.overflow; i++) {
            const od_detect_t* detect = &ai_result->od.detects[i];
            cbor_enc_map(&enc, 3);
            nn_cbor_put_class(&enc, detect->class_name, "");
            nn_cbor_put_conf(&enc, detect->conf);
            nn_cbor_put_box(&enc, detect->x, detect->y, detect->width, detect->height);
        }
        break;
    }
    case PP_TYPE_MPE: {
        uint8_t count = ai_result->mpe.detects ? ai_result->mpe.nb_detect : 0;
        cbor_enc_map(&enc, 2);
        cbor_enc_text(&enc, "type");
        cbor_enc_uint(&enc, PP_TYPE_MPE);
        cbor_enc_text(&enc, "poses");
        cbor_enc_array(&enc, count);
        for (uint8_t i = 0; i < count && !enc.overflow; i++) {
            const mpe_detect_t* detect = &ai_result->mpe.detects[i];
            // Same inline-array bound as the JSON serializer's keypoint loop
            uint32_t nb_kp = detect->nb_keypoints < 33 ? detect->nb_keypoints : 33;
            cbor_enc_map(&enc, 4);
            nn_cbor_put_class(&enc, detect->class_name, "person");
            nn_cbor_put_conf(&enc, detect->conf);
            nn_cbor_put_box(&enc, detect->x, detect->y, detect->width, detect->height);
            nn_cbor_put_keypoints(&enc, detect->keypoints, nb_kp);
        }
        break;
    }
    case PP_TYPE_SPE: {
        int have_pose = (ai_result->spe.keypoints != NULL && ai_result->spe.nb_keypoints > 0);
        cbor_enc_map(&enc, 2);
        cbor_enc_text(&enc, "type");
        cbor_enc_uint(&enc, PP_TYPE_SPE);
        cbor_enc_text(&enc, "poses");
        cbor_enc_array(&enc, have_pose ? 1 : 0);
        if (have_pose) {
            cbor_enc_map(&enc, 1);
            nn_cbor_put_keypoints(&enc, ai_result->spe.keypoints, ai_result->spe.nb_keypoints);
        }
        break;
    }
    case PP_TYPE_ISEG: {
        uint8_t count = ai_result->iseg.detects ? ai_result->iseg.nb_detect : 0;
        cbor_enc_map(&enc, 2);
        cbor_enc_text(&enc, "type");
        cbor_enc_uint(&enc, PP_TYPE_ISEG);
        cbor_enc_text(&enc, "seg");
        cbor_enc_array(&enc, count);
        for (uint8_t i = 0; i < count && !enc.overflow; i++) {
            const iseg_detect_t* detect = &ai_result->iseg.detects[i];
            cbor_enc_map(&enc, 4);
            nn_cbor_put_class(&enc, detect->class_name, "");
            nn_cbor_put_conf(&enc, detect->conf);
            nn_cbor_put_box(&enc, detect->x, detect->y, detect->width, detect->height);
            cbor_enc_text(&enc, "msz");
            cbor_enc_uint(&enc, detect->mask_size);
        }
        break;
    }
    default:
        cbor_enc_map(&enc, 1);
        cbor_enc_text(&enc, "type");
        cbor_enc_uint(&enc, (uint64_t)ai_result->type);
        break;
    }

    size_t total = 0;
    if (cbor_enc_finish(&enc, &total) != 0) {
        return -1;
    }
    return (int)total;
}

/* ==================== command processing function ==================== */

static int nn_cmd(int argc, char *argv[])
{
    if (argc < 3) {
        LOG_SIMPLE("Usage: nn <model_index> <command> [args...]\r\n");
        LOG_SIMPLE("Commands:\r\n");
        LOG_SIMPLE("  create          - Create a new NN instance\r\n");
        LOG_SIMPLE("  destroy         - Destroy a NN instance\r\n");
        LOG_SIMPLE("  status          - Show NN status\r\n");
        LOG_SIMPLE("  load <path>     - Load model from path\r\n");
        LOG_SIMPLE("  unload          - Unload current model\r\n");
        LOG_SIMPLE("  stats           - Show inference statistics\r\n");
        LOG_SIMPLE("  set <key> <value> - Set configuration\r\n");
        return 0;
    }

    const char *model_index = argv[1];
    const char *cmd = argv[2];

    if (strcmp(cmd, "create") == 0) {
        // create a new NN instance
        nn_handle_t handle = nn_instance_create();
        if (handle) {
            LOG_SIMPLE("NN instance created successfully\r\n");
        } else {
            LOG_SIMPLE("Failed to create NN instance\r\n");
        }
        return 0;
    } 
    nn_t *nn = get_instance(atoi(model_index));
    if (!nn) {
        LOG_SIMPLE("Error: Invalid model index\r\n");
        return -1;
    }

    if (strcmp(cmd, "status") == 0) {
        // show state
        nn_state_t state = nn_instance_get_state(nn);
        const char *state_str[] = {"UNINIT", "INIT", "READY", "RUNNING", "ERROR"};
        LOG_SIMPLE("NN Status: %s\r\n", state_str[state]);

        if (nn->model.name[0] != '\0') {
            LOG_SIMPLE("Current Model: %s (v%s)\r\n", nn->model.name, nn->model.version);
            LOG_SIMPLE("Model Description: %s\r\n", nn->model.description);
            LOG_SIMPLE("Model Author: %s\r\n", nn->model.author);
            LOG_SIMPLE("Model Created At: %s\r\n", nn->model.created_at);
            LOG_SIMPLE("Model ST Edge AI Version: %s\r\n", nn->model.stedgeai_version);
            LOG_SIMPLE("Model Color Format: %s\r\n", nn->model.color_format);
            LOG_SIMPLE("Model Input Data Type: %s\r\n", nn->model.input_data_type);
            LOG_SIMPLE("Model Output Data Type: %s\r\n", nn->model.output_data_type);
            LOG_SIMPLE("Input: %lux%lux%lu\r\n", nn->model.input_width, nn->model.input_height, nn->model.input_channels);
        }

        LOG_SIMPLE("Inference Count: %ld, Total Time: %ld ms, Average Time: %ld ms\r\n",
                   nn->inference_count, nn->total_inference_time,
                   nn->inference_count > 0 ? nn->total_inference_time / nn->inference_count : 0);

    } else if (strcmp(cmd, "load") == 0) {
        // load model
        if (argc < 4) {
            LOG_SIMPLE("Error: Please specify model path\r\n");
            return -1;
        }
        uintptr_t model_ptr = strtoul(argv[3], NULL, 16);
        if (model_ptr < AI_DEFAULT_BASE || model_ptr > AI_3_END) {
            LOG_SIMPLE("Error: model path is not in [0x%lx, 0x%lx]\r\n", AI_DEFAULT_BASE, AI_3_END);
            return -1;
        }
        int ret = nn_instance_load_model(nn, model_ptr);
        if (ret == 0) {
            LOG_SIMPLE("Model loaded successfully: %s\r\n", argv[3]);
        } else {
            LOG_SIMPLE("Failed to load model: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "unload") == 0) {
        // unload model
        int ret = nn_instance_unload_model(nn);
        if (ret == 0) {
            LOG_SIMPLE("Model unloaded successfully\r\n");
        } else {
            LOG_SIMPLE("Failed to unload model: %d\r\n", ret);
        }

    } else if (strcmp(cmd, "set") == 0) {
        // set configuration
        if (argc < 5) {
            LOG_SIMPLE("Error: Please specify key and value\r\n");
            return -1;
        }

        const char *key = argv[3];
        const char *value = argv[4];

        if (strcmp(key, "confidence") == 0) {
            nn_instance_set_confidence_threshold(nn, atof(value));
        } else if (strcmp(key, "nms") == 0) {
            nn_instance_set_nms_threshold(nn, atof(value));
        } else {
            LOG_SIMPLE("Unknown configuration key: %s\r\n", key);
            return -1;
        }

    } else if (strcmp(cmd, "stats") == 0) {
        // show statistics
        uint32_t count, total_time;
        if (nn_instance_get_inference_stats(nn, &count, &total_time) == 0) {
            LOG_SIMPLE("Inference Statistics:\r\n");
            LOG_SIMPLE("  Total Inferences: %lu\r\n", count);
            LOG_SIMPLE("  Total Time: %lu ms\r\n", total_time);
            if (count > 0) {
                LOG_SIMPLE("  Average Time: %.2f ms\r\n", (float)total_time / count);
            }
        }
    } else if (strcmp(cmd, "destroy") == 0) {
        // destroy a NN instance
        int ret = nn_instance_destroy(nn);
        if (ret == 0) {
            LOG_SIMPLE("NN instance destroyed successfully\r\n");
        } else {
            LOG_SIMPLE("Failed to destroy NN instance: %d\r\n", ret);
        }
    }

    return 0;
}

// command registration table
debug_cmd_reg_t nn_cmd_table[] = {
    {"nn", "Neural Network control", nn_cmd},
};

// command registration function
static void nn_cmd_register(void)
{
    debug_cmdline_register(nn_cmd_table, sizeof(nn_cmd_table) / sizeof(nn_cmd_table[0]));
}

/* ==================== public API function implementation ==================== */

void nn_register(void)
{
    static dev_ops_t nn_ops = {
        .init = nn_init,
        .deinit = nn_deinit,
        .start = NULL,
        .stop = NULL,
        .ioctl = NULL
    };

    // create device instance
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));

    // set device information
    strcpy(dev->name, NN_DEVICE_NAME);
    dev->type = DEV_TYPE_AI;  // use AI device type
    dev->ops = &nn_ops;
    dev->priv_data = NULL;

    // register device
    device_register(dev);

    // register command
    driver_cmd_register_callback(NN_DEVICE_NAME, nn_cmd_register);

    LOG_DRV_INFO("NN module registered successfully\r\r\n");
}

void nn_unregister(void)
{
    device_t *dev = device_find_pattern(NN_DEVICE_NAME, DEV_TYPE_AI);
    if (dev) {
        device_unregister(dev);
        hal_mem_free(dev);
    }

    LOG_DRV_INFO("NN module unregistered\r\r\n");
}


/* ==================== single-instance API implementation ==================== */
static nn_handle_t g_nn_single_instance = NULL;

int nn_load_model(const uintptr_t file_ptr)
{
    if (!g_nn_single_instance) {
        g_nn_single_instance = nn_instance_create();
    }
    return nn_instance_load_model(g_nn_single_instance, file_ptr);
}

int nn_unload_model(void)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_unload_model(g_nn_single_instance);
}

int nn_get_model_input_buffer(uint8_t **buffer, uint32_t *size)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_get_model_input_buffer(g_nn_single_instance, buffer, size);
}

int nn_get_model_output_buffer(uint8_t **buffer, uint32_t *size)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_get_model_output_buffer(g_nn_single_instance, buffer, size);
}

int nn_get_model_info(nn_model_info_t *model_info)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_get_model_info(g_nn_single_instance, model_info);
}

int nn_start_inference(void)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    // Note: start/stop inference is not implemented in instance API
    // This is a placeholder that maintains API compatibility
    nn_t *nn = (nn_t *)g_nn_single_instance;
    nn->state = NN_STATE_RUNNING;
    return 0;
}

int nn_stop_inference(void)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    // Note: start/stop inference is not implemented in instance API
    // This is a placeholder that maintains API compatibility
    nn_t *nn = (nn_t *)g_nn_single_instance;
    nn->state = NN_STATE_READY;
    return 0;
}

int nn_inference_frame(uint8_t *input_data, uint32_t input_size, nn_result_t *result)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_inference_frame(g_nn_single_instance, input_data, input_size, result);
}

int nn_set_confidence_threshold(float threshold)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_set_confidence_threshold(g_nn_single_instance, threshold);
}

int nn_get_confidence_threshold(float *threshold)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_get_confidence_threshold(g_nn_single_instance, threshold);
}

int nn_set_nms_threshold(float threshold)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_set_nms_threshold(g_nn_single_instance, threshold);
}

int nn_get_nms_threshold(float *threshold)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_get_nms_threshold(g_nn_single_instance, threshold);
}

nn_state_t nn_get_state(void)
{
    if (!g_nn_single_instance) {
        return NN_STATE_UNINIT;
    }
    return nn_instance_get_state(g_nn_single_instance);
}

int nn_get_inference_stats(uint32_t *count, uint32_t *total_time)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_get_inference_stats(g_nn_single_instance, count, total_time);
}

int nn_set_callback(nn_callback_t callback, void *user_data)
{
    if (!g_nn_single_instance) {
        return -1;
    }
    return nn_instance_set_callback(g_nn_single_instance, callback, user_data);
}

int nn_validate_model(const uintptr_t file_ptr)
{
    return validate_model(file_ptr);
}
