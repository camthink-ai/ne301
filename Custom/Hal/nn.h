/**
 * @file nn.h
 * @brief AI Neural Network Module Header
 * @details AI Neural Network Module based on STM32N6 NPU
 */

#ifndef _NN_H
#define _NN_H

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "camera.h"
#include "pp.h"
#include "cJSON.h"
/* ==================== AI model related definition ==================== */
#define NN_MAX_INPUT_BUFFER 3
#define NN_MAX_OUTPUT_BUFFER 5

#define NN_MAX_INSTANCES 2

typedef pp_result_t nn_result_t;

/* callback function type */
typedef void (*nn_callback_t)(nn_result_t *result, void *user_data);

// AI model state enumeration
typedef enum {
    NN_STATE_UNINIT = 0,      // uninitialized
    NN_STATE_INIT,            // initialized
    NN_STATE_READY,           // ready
    NN_STATE_RUNNING = NN_STATE_READY,         // running
    NN_STATE_ERROR,           // error state
} nn_state_t;

typedef enum {
    NN_ERROR_OK = 0,
    NN_ERROR_INVALID_PACKAGE,
    NN_ERROR_INCOMPATIBLE,
    NN_ERROR_INVALID_CHECKSUM,
    NN_ERROR_INVALID_MODEL,
    NN_ERROR_INVALID_CONFIG,
} nn_error_t;

// AI model information structure
typedef struct {
    char name[64];                    // model name
    char version[32];                 // model version
    char description[128];            // model description
    char created_at[32];              // created at
    char author[64];                  // author
    char postprocess_type[32];        // postprocess type
    char input_data_type[32];         // input data type
    char output_data_type[32];        // data type
    char color_format[32];            // color format
    char stedgeai_version[64];        // ST Edge AI Core version (e.g. v3.0.0-20426 123672867)
    uint32_t input_width;             // input width
    uint32_t input_height;            // input height
    uint32_t input_channels;          // input channels
    uint32_t model_size;              // model size(bytes)
    uintptr_t model_ptr;              // model pointer
    uintptr_t config_ptr;             // model config pointer
    uintptr_t metadata_ptr;           // metadata pointer
    bool is_int8_input;               // true if input data type is int8 (cached from input_data_type)
} nn_model_info_t;

// AI neural network module structure (single instance)
typedef struct {
    osMutexId_t mtx_id;              // mutex id

    // AI model related
    nn_state_t state;                 // current state
    nn_model_info_t model;            // current loaded model

    // buffer management
    void *input_buffer[NN_MAX_INPUT_BUFFER];            // input buffer
    void *output_buffer[NN_MAX_OUTPUT_BUFFER];          // output buffer
    uint32_t input_buffer_count;                           // input buffer count
    uint32_t output_buffer_count;                          // output buffer count
    uint32_t input_buffer_size[NN_MAX_INPUT_BUFFER];       // input buffer size
    uint32_t output_buffer_size[NN_MAX_OUTPUT_BUFFER];     // output buffer size
    void *exec_ram_addr;                               // exec ram address
    void *ext_ram_addr;                                // ext ram address

    // inference related
    uint32_t inference_count;         // inference count
    uint32_t total_inference_time;    // total inference time

    //postprocess
    const pp_vtable_t *pp_vt;
    void *pp_params;

    // model instance
    void *nn_inst;

    // callback function
    nn_callback_t callback;
    void *callback_user_data;
} nn_t;

// Instance handle type for multi-instance support
typedef nn_t* nn_handle_t;

/* ==================== model file header definition ==================== */
typedef struct {
    uint32_t magic;                     /* Package magic number */
    uint32_t version;                   /* Package version */
    uint32_t package_size;              /* Total package size */

    /* Configuration section (single integrated JSON) */
    uint32_t metadata_offset;
    uint32_t metadata_size;
    uint32_t model_config_offset;
    uint32_t model_config_size;

    /* Model data sections */
    uint32_t relocatable_model_offset;  /* network_rel.bin offset */
    uint32_t relocatable_model_size;    /* network_rel.bin size */
    uint32_t extension_data_offset;
    uint32_t extension_data_size;

    /* Checksums */
    uint32_t header_checksum;           /* CRC32 over bytes before this field */
    uint32_t model_checksum;            /* CRC32 over relocatable model data */
    uint32_t config_checksum;           /* CRC32 over integrated config JSON */
    uint32_t package_checksum;          /* CRC32 over entire package with this field zeroed */
} nn_package_header_t;

#define MODEL_PACKAGE_MAGIC 0x314D364E  // 'N6M1' - v3.0
#define MODEL_PACKAGE_VERSION 0x030000  // v3.0.0
#define MODEL_RELOCATABLE_MAGIC 0x4E49424E  // 'NBIN' - v1.0
#define MODEL_STEDGEAI_VERSION_SUPPORTED "v3.0.0"

/* ==================== public API functions ==================== */

// basic AI operations
void nn_register(void);
void nn_unregister(void);

/* ==================== multi-instance API (preferred for new code) ==================== */

/**
 * @brief Create a new NN instance (not registered as a device).
 * @return nn_handle_t Instance handle or NULL on failure.
 */
nn_handle_t nn_instance_create(void);

/**
 * @brief Destroy an NN instance created by nn_instance_create.
 * @param handle Instance handle.
 * @return 0 success, -1 failed
 */
int nn_instance_destroy(nn_handle_t handle);

// AI model management (per instance)
/*
* description: load model from file
* input: file pointer
* output: 0 success, -1 failed
*/
int nn_instance_load_model(nn_handle_t handle, const uintptr_t file_ptr);
/*
* description: unload model
* input: none
* output: 0 success, -1 failed
*/
int nn_instance_unload_model(nn_handle_t handle);
/*
* description: get model input buffer
* input: buffer pointer, buffer size
* output: 0 success, -1 failed
*/
int nn_instance_get_model_input_buffer(nn_handle_t handle, uint8_t **buffer, uint32_t *size);
/*
* description: get model output buffer
* input: buffer pointer, buffer size
* output: 0 success, -1 failed
*/
int nn_instance_get_model_output_buffer(nn_handle_t handle, uint8_t **buffer, uint32_t *size);
/*
* description: get model info
* input: model info pointer
* output: 0 success, -1 failed
*/
int nn_instance_get_model_info(nn_handle_t handle, nn_model_info_t *model_info);

// AI inference control (per instance)
/*
* description: inference one frame
* input: input data, input size, result pointer
* output: 0 success, -1 failed
*/
int nn_instance_inference_frame(nn_handle_t handle, uint8_t *input_data, uint32_t input_size, nn_result_t *result);

// Thresholds (per instance)
/*
* description: set confidence threshold
* input: threshold
* output: 0 success, -1 failed
*/
int nn_instance_set_confidence_threshold(nn_handle_t handle, float threshold);
/*
* description: get confidence threshold
* input: threshold pointer
* output: 0 success, -1 failed
*/
int nn_instance_get_confidence_threshold(nn_handle_t handle, float *threshold);
/*
* description: set nms threshold
* input: threshold
* output: 0 success, -1 failed
*/
int nn_instance_set_nms_threshold(nn_handle_t handle, float threshold);
/*
* description: get nms threshold
* input: threshold pointer
* output: 0 success, -1 failed
*/
int nn_instance_get_nms_threshold(nn_handle_t handle, float *threshold);

// State & statistics (per instance)
/*
* description: get state
* input: none
* output: nn state
*/
nn_state_t nn_instance_get_state(nn_handle_t handle);
/*
* description: get inference stats
* input: count pointer, total time pointer
* output: 0 success, -1 failed
*/
int nn_instance_get_inference_stats(nn_handle_t handle, uint32_t *count, uint32_t *total_time);

// Callback (per instance)
/*
* description: set callback function for inference result before nn_start_inference
* input: callback function pointer, user data pointer
* output: 0 success, -1 failed
*/
int nn_instance_set_callback(nn_handle_t handle, nn_callback_t callback, void *user_data);

/* ==================== legacy single-instance API (kept for compatibility) ==================== */

// AI model management (default instance)
/*
* description: load model from file
* input: file pointer
* output: 0 success, -1 failed
*/
int nn_load_model(const uintptr_t file_ptr);

/*
* description: unload model
* input: none
* output: 0 success, -1 failed
*/
int nn_unload_model(void);

/*
* description: get model input buffer
* input: buffer pointer, buffer size
* output: 0 success, -1 failed
*/
int nn_get_model_input_buffer(uint8_t **buffer, uint32_t *size);

/*
* description: get model output buffer
* input: buffer pointer, buffer size
* output: 0 success, -1 failed
*/
int nn_get_model_output_buffer(uint8_t **buffer, uint32_t *size);

/*
* description: get model info
* input: model info pointer
* output: 0 success, -1 failed
*/
int nn_get_model_info(nn_model_info_t *model_info);

// AI inference control (default instance)

/*
* description: start inference after model loaded with async mode
* input: none
* output: 0 success, -1 failed
* note: the inference will be done in the nn_process thread
* note: if callback function (via nn_set_callback) is set, the callback function will be called in the nn_process thread
*/
int nn_start_inference(void);

/*
* description: stop inference after model loaded with async mode
* input: none
* output: 0 success, -1 failed
*/
int nn_stop_inference(void);

/*
* description: inference one frame with sync mode after model loaded
* input: input data, input size, result pointer
* output: 0 success, -1 failed
*/
int nn_inference_frame(uint8_t *input_data, uint32_t input_size, nn_result_t *result);

/*
* description: set confidence threshold for postprocess
* input: threshold
* output: 0 success, -1 failed
*/
int nn_set_confidence_threshold(float threshold);

/*
* description: get confidence threshold for postprocess
* input: threshold pointer
* output: 0 success, -1 failed
*/
int nn_get_confidence_threshold(float *threshold);

/*
* description: set nms threshold for postprocess
* input: threshold
* output: 0 success, -1 failed
*/
int nn_set_nms_threshold(float threshold);

/*
* description: get nms threshold for postprocess
* input: threshold pointer
* output: 0 success, -1 failed
*/
int nn_get_nms_threshold(float *threshold);

// AI state query (default instance)

/*
* description: get nn state
* input: none
* output: nn state
*/
nn_state_t nn_get_state(void);

/*
* description: get inference stats
* input: count pointer, total time pointer
* output: 0 success, -1 failed
*/
int nn_get_inference_stats(uint32_t *count, uint32_t *total_time);

// callback function (default instance)
/*
* description: set callback function for inference result before nn_start_inference
* input: callback function pointer, user data pointer
* output: 0 success, -1 failed
* note: the callback function will be called in the nn_process thread
*/
int nn_set_callback(nn_callback_t callback, void *user_data);

/*
* description: validate model
* input: file pointer
* output: 0 success, -1 failed
*/
int nn_validate_model(const uintptr_t file_ptr);


/* ==================== JSON creation functions ==================== */
/**
 * @brief Create AI result JSON
 * @param ai_result AI result
 * @return AI result JSON
 */
cJSON* nn_create_ai_result_json(const nn_result_t* ai_result);

/**
 * @brief Encode an AI result as a compact CBOR map (RFC 8949)
 * @details Binary counterpart of nn_create_ai_result_json for
 *          bandwidth-constrained publishers. Coordinates and confidences are
 *          clamped to [0,1] and carried as half-precision floats; static
 *          per-model metadata (keypoint names, skeleton connections) is not
 *          carried. ISEG carries mask_size only, matching the JSON path.
 *          The result's counts and pointers are trusted as-is (same caller
 *          contract as nn_create_ai_result_json).
 * @param ai_result AI result
 * @param buf Output buffer
 * @param cap Output buffer capacity in bytes
 * @return Bytes written, or -1 on invalid arguments or buffer overflow
 */
int nn_encode_ai_result_cbor(const nn_result_t* ai_result, uint8_t* buf, size_t cap);

#endif // _NN_H