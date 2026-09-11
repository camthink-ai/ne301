/**
 * @file api_ota_module.h
 * @brief OTA API Module Header
 * @details OTA (Over-The-Air) upgrade API interface declarations
 */

#ifndef API_OTA_MODULE_H
#define API_OTA_MODULE_H

#include "web_api.h"
#include "ota_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== API Function Declarations ==================== */

/**
 * @brief Register OTA API module
 * @return Operation result
 */
aicam_result_t web_api_register_ota_module(void);

/**
 * @brief OTA upload stream processor
 * @param c Mongoose connection
 * @param ev Event type
 * @param ev_data Event data
 */
void ota_upload_stream_processor(struct mg_connection *c, int ev, void *ev_data);

/* First field of ota_upload_ctx_t: the event loop routes detached
 * streaming connections by this tag (see web_server.c) so concurrent file
 * and OTA uploads cannot cross-dispatch into each other's context. */
#define OTA_UPLOAD_CTX_MAGIC 0x4F544150u  /* 'OTAP' */

/**
 * @brief OTA pre-check handler - POST /api/v1/system/ota/precheck
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_precheck_handler(http_handler_context_t *ctx);

/**
 * @brief OTA upload handler - POST /api/v1/system/ota/upload
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_upload_handler(http_handler_context_t *ctx);

/**
 * @brief OTA local upgrade handler - POST /api/v1/system/ota/upgrade-local
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_upgrade_local_handler(http_handler_context_t *ctx);

/**
 * @brief OTA export firmware handler - POST /api/v1/system/ota/export
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_export_firmware_handler(http_handler_context_t *ctx);

/**
 * @brief OTA bundle pre-check - POST /api/v1/system/ota/bundle/precheck
 * @details Body: the 4096-byte bundle header. The device validates it against
 *          its own partition table and returns the burn plan.
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_bundle_precheck_handler(http_handler_context_t *ctx);

/**
 * @brief Arm a prechecked bundle session - POST /api/v1/system/ota/bundle/begin
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_bundle_begin_handler(http_handler_context_t *ctx);

/**
 * @brief Finish/abort a bundle session - POST /api/v1/system/ota/bundle/finish
 * @details Body: {"result":"ok"|"abort"}. "ok" sets the WiFi update-pending
 *          flag; both clear the session.
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t ota_bundle_finish_handler(http_handler_context_t *ctx);

/**
 * @brief Check OTA upload timeout and reset state if necessary
 * @return AICAM_TRUE if timeout occurred and state was reset, AICAM_FALSE otherwise
 */
aicam_bool_t ota_check_timeout(void);

/**
 * @brief Force reset OTA upload state
 */
void ota_reset_upload_state(void);

/**
 * @brief Check if OTA upload is in progress
 * @return AICAM_TRUE if OTA upload is in progress, AICAM_FALSE otherwise
 */
aicam_bool_t ota_is_upload_in_progress(void);

#ifdef __cplusplus
}
#endif

#endif /* API_OTA_MODULE_H */
