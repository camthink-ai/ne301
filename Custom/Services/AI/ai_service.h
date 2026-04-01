/**
 * @file ai_service.h
 * @brief AI Service Interface Header
 * @details AI service standard interface definition, support video pipeline management and AI inference control
 */

#ifndef AI_SERVICE_H
#define AI_SERVICE_H

#include "aicam_types.h"
#include "service_interfaces.h"
#include "video_pipeline.h"
#include "video_ai_node.h"
#include "video_camera_node.h"
#include "video_encoder_node.h"
#include "nn.h"
#include "jpegc.h"
#include "draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== AI Service Configuration ==================== */

/**
 * @brief AI service configuration structure
 */
typedef struct {
    uint32_t width;                       // Video width
    uint32_t height;                      // Video height
    uint32_t fps;                         // Frame rate
    uint32_t format;                      // Pixel format
    uint32_t bpp;                         // Bits per pixel
    uint32_t confidence_threshold;        // AI confidence threshold (0-100)
    uint32_t nms_threshold;               // NMS threshold (0-100)
    uint32_t max_detections;              // Maximum detections per frame
    uint32_t processing_interval;         // AI processing interval (frames)
    aicam_bool_t ai_enabled;              // AI processing enabled
    aicam_bool_t enable_stats;            // Enable statistics logging
    aicam_bool_t enable_debug;            // Enable debug logging
    aicam_bool_t enable_drawing;          // Enable AI result drawing
} ai_service_config_t;

/**
 * @brief AI service statistics
 */
typedef struct {
    uint64_t total_frames_captured;       // Total frames captured by camera
    uint64_t total_frames_processed;      // Total frames processed by AI
    uint64_t total_frames_encoded;        // Total frames encoded
    uint64_t total_detections_found;      // Total detections found
    uint64_t pipeline_errors;             // Pipeline errors
    uint64_t ai_processing_errors;        // AI processing errors
    uint64_t start_time_ms;               // Service start time
    uint64_t end_time_ms;                 // Service end time
    uint32_t avg_fps;                     // Average FPS achieved
    uint32_t avg_ai_processing_time_us;   // Average AI processing time
    uint32_t current_detection_count;     // Current frame detection count
} ai_service_stats_t;

/* ==================== JPEG Processing Structures ==================== */

/**
 * @brief JPEG decode configuration
 */
typedef struct {
    uint32_t width;                       // Image width
    uint32_t height;                      // Image height
    uint32_t chroma_subsampling;          // Chroma subsampling
    uint32_t quality;                     // JPEG quality (1-100)
} ai_jpeg_decode_config_t;

/**
 * @brief JPEG encode configuration
 */
typedef struct {
    uint32_t width;                       // Image width
    uint32_t height;                      // Image height
    uint32_t chroma_subsampling;          // Chroma subsampling
    uint32_t quality;                     // JPEG quality (1-100)
} ai_jpeg_encode_config_t;

/**
 * @brief Single image inference result
 */
typedef struct {
    nn_result_t ai_result;                // AI inference result
    uint8_t *output_jpeg;                 // Output JPEG buffer
    uint32_t output_jpeg_size;            // Output JPEG size
    uint32_t processing_time_ms;          // Total processing time
    aicam_bool_t success;                 // Processing success flag
} ai_single_inference_result_t;

/**
 * @brief Model validation configuration
 */
typedef struct {
    uint8_t* ai_image_data;              // AI image data
    uint8_t* draw_image_data;            // Draw image data
    uint32_t ai_image_size;              // AI image size
    uint32_t draw_image_size;            // Draw image size
    uint32_t ai_image_width;             // AI image width
    uint32_t ai_image_height;            // AI image height
    uint32_t ai_image_quality;           // AI image quality
    uint32_t draw_image_width;           // Draw image width
    uint32_t draw_image_height;          // Draw image height
    uint32_t draw_image_quality;         // Draw image quality
} model_validation_config_t;


/* ==================== AI Service Interface Functions ==================== */

/**
 * @brief Initialize AI service
 * @param config Service configuration (optional)
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_service_init(void *config);

/**
 * @brief Start AI service
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_service_start(void);

/**
 * @brief Stop AI service
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_service_stop(void);

/**
 * @brief Deinitialize AI service
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_service_deinit(void);

/**
 * @brief Get AI service state
 * @return service_state_t Service state
 */
service_state_t ai_service_get_state(void);

/* ==================== AI Pipeline Management Functions ==================== */

/**
 * @brief Initialize AI pipeline with configuration
 * @param config Pipeline configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_pipeline_init(ai_service_config_t *config);

/**
 * @brief Start AI pipeline
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_pipeline_start(void);

/**
 * @brief Stop AI pipeline
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_pipeline_stop(void);

/**
 * @brief Deinitialize AI pipeline
 */
void ai_pipeline_deinit(void);

/**
 * @brief Check if AI pipeline is running
 * @return Running status
 */
aicam_bool_t ai_pipeline_is_running(void);

/**
 * @brief Check if AI pipeline is initialized
 * @return Initialization status
 */
aicam_bool_t ai_pipeline_is_initialized(void);

/**
 * @brief Get AI node handle
 * @return AI node handle or NULL if not initialized
 */
video_node_t* ai_service_get_ai_node(void);

/**
 * @brief Get latest NN result from AI service
 * @param result NN result structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_service_get_nn_result(nn_result_t *result, uint32_t frame_id);

/* ==================== AI Inference Control Functions ==================== */

/**
 * @brief Enable/disable AI inference
 * @param enabled AI inference enabled flag
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_set_inference_enabled(aicam_bool_t enabled);

/**
 * @brief Get AI inference enabled status
 * @return AI inference enabled status
 */
aicam_bool_t ai_get_inference_enabled(void);

/**
 * @brief Set AI NMS threshold
 * @param threshold NMS threshold (0-100)
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_set_nms_threshold(uint32_t threshold);

/**
 * @brief Get AI NMS threshold
 * @return NMS threshold (0-100)
 */
uint32_t ai_get_nms_threshold(void);

/**
 * @brief Set AI confidence threshold
 * @param threshold Confidence threshold (0-100)
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_set_confidence_threshold(uint32_t threshold);

/**
 * @brief Get AI confidence threshold
 * @return Confidence threshold (0-100)
 */
uint32_t ai_get_confidence_threshold(void);

/**
 * @brief Set AI maximum detections
 * @param max_detections Maximum detections per frame
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_set_max_detections(uint32_t max_detections);

/**
 * @brief Get AI maximum detections
 * @return Maximum detections per frame
 */
uint32_t ai_get_max_detections(void);

/**
 * @brief Set AI processing interval
 * @param interval Processing interval (frames)
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_set_processing_interval(uint32_t interval);

/**
 * @brief Get AI processing interval
 * @return Processing interval (frames)
 */
uint32_t ai_get_processing_interval(void);

/* ==================== AI Model Management Functions ==================== */

/**
 * @brief Load AI model
 * @param model_ptr Model pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_load_model(uintptr_t model_ptr);

/**
 * @brief Unload AI model
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_unload_model(void);

/**
 * @brief Get AI model information
 * @param model_info Model info structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_get_model_info(nn_model_info_t *model_info);

/**
 * @brief Get AI model information
 * @param model_info Model info structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_service_get_model_info(nn_model_info_t *model_info);

/**
 * @brief Reload AI model
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_reload_model(void);

/* ==================== AI Statistics Functions ==================== */

/**
 * @brief Get AI service statistics
 * @param stats Statistics structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_get_stats(ai_service_stats_t *stats);

/**
 * @brief Reset AI service statistics
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_reset_stats(void);

/**
 * @brief Print AI service statistics
 */
void ai_print_stats(void);

/* ==================== AI Configuration Functions ==================== */

/**
 * @brief Get default AI service configuration
 * @param config Configuration structure to fill
 */
void ai_get_default_config(ai_service_config_t *config);

/**
 * @brief Get normal AI service configuration
 * @param config Configuration structure to fill
 */
void ai_get_normal_config(ai_service_config_t *config);

/**
 * @brief Get AI service configuration for AI processing
 * @param config Configuration structure to fill
 */
void ai_get_ai_config(ai_service_config_t *config);

/**
 * @brief Set AI service configuration
 * @param config New configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_set_config(const ai_service_config_t *config);

/**
 * @brief Get AI service configuration
 * @param config Configuration structure to fill
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_get_config(ai_service_config_t *config);

/* ==================== JPEG Processing Functions ==================== */

/**
 * @brief Decode JPEG image to raw buffer
 * @param jpeg_data Input JPEG data
 * @param jpeg_size Input JPEG size
 * @param decode_config Decode configuration
 * @param raw_buffer Output raw buffer (allocated by function)
 * @param raw_size Output raw buffer size
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_jpeg_decode(const uint8_t *jpeg_data, 
                              uint32_t jpeg_size,
                              const ai_jpeg_decode_config_t *decode_config,
                              uint8_t **raw_buffer, 
                              uint32_t *raw_size);

/**
 * @brief Encode raw buffer to JPEG
 * @param raw_data Input raw data
 * @param raw_size Input raw size
 * @param encode_config Encode configuration
 * @param jpeg_buffer Output JPEG buffer (allocated by function)
 * @param jpeg_size Output JPEG buffer size
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_jpeg_encode(const uint8_t *raw_data, 
                              uint32_t raw_size,
                              const ai_jpeg_encode_config_t *encode_config,
                              uint8_t **jpeg_buffer, 
                              uint32_t *jpeg_size);

/**
 * @brief Convert color format using DMA2D
 * @param src_data Source data
 * @param src_width Source width
 * @param src_height Source height
 * @param src_format Source format
 * @param rb_swap RB swap
 * @param dst_data Destination data (allocated by function)
 * @param dst_size Destination size
 * @param dst_format Destination format
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_color_convert(const uint8_t *src_data,
                                uint32_t src_width,
                                uint32_t src_height,
                                uint32_t src_format,
                                uint32_t rb_swap,
                                uint32_t chroma_subsampling,
                                uint8_t **dst_data,
                                uint32_t* dst_size,
                                uint32_t dst_format);

/**
 * @brief Single image inference with drawing
 * @param ai_jpeg_data AI inference JPEG data (small size for AI)
 * @param ai_jpeg_size AI inference JPEG size
 * @param draw_jpeg_data Drawing JPEG data (large size for drawing)
 * @param draw_jpeg_size Drawing JPEG size
 * @param result Output inference result
 * @return aicam_result_t Operation result
 */
aicam_result_t ai_single_image_inference(const model_validation_config_t *model_validation_config,
                                         ai_single_inference_result_t *result);

/**
 * @brief Free memory allocated by JPEG processing functions
 * @param buffer Buffer to free
 */
void ai_jpeg_free_buffer(uint8_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* AI_SERVICE_H */
