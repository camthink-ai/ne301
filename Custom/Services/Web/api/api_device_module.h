/**
 * @file api_device_module.h
 * @brief Device API Module Header
 * @details Device management API interface declarations
 */

#ifndef API_DEVICE_MODULE_H
#define API_DEVICE_MODULE_H

#include "web_api.h"
#include "device_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== API Function Declarations ==================== */

/**
 * @brief Register device API module
 * @return Operation result
 */
aicam_result_t web_api_register_device_module(void);

/**
 * @brief Device information handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_info_handler(http_handler_context_t *ctx);

/**
 * @brief Storage information handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_storage_handler(http_handler_context_t *ctx);

/**
 * @brief Storage configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_storage_config_handler(http_handler_context_t *ctx);

/**
 * @brief POST /api/v1/device/storage/format - Format internal flash LittleFS.
 *        Destructive (erases all flash files). Requires auth.
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_storage_format_handler(http_handler_context_t *ctx);

/**
 * @brief Image configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_image_config_handler(http_handler_context_t *ctx);

/**
 * @brief FSBL persisted CPU clock profile (GET/POST)
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_sys_clk_config_handler(http_handler_context_t *ctx);

/**
 * @brief Light configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_light_config_handler(http_handler_context_t *ctx);

/**
 * @brief Light control handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_light_control_handler(http_handler_context_t *ctx);

/**
 * @brief Camera configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_camera_config_handler(http_handler_context_t *ctx);

/**
 * @brief System time configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t system_time_handler(http_handler_context_t *ctx);

/**
 * @brief Device name configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_name_handler(http_handler_context_t *ctx);

/**
 * @brief System logs handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t system_logs_handler(http_handler_context_t *ctx);

/**
 * @brief System logs export handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t system_logs_export_handler(http_handler_context_t *ctx);

/**
 * @brief System restart handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t system_restart_handler(http_handler_context_t *ctx);

/**
 * @brief System factory reset handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t system_factory_reset_handler(http_handler_context_t *ctx);

/**
 * @brief Device configuration export handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_config_export_handler(http_handler_context_t *ctx);

/**
 * @brief Device configuration import handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t device_config_import_handler(http_handler_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* API_DEVICE_MODULE_H */
