/**
 * @file api_network_module.h
 * @brief Network API Module Header
 * @details Network management API interface declarations
 */

#ifndef API_NETWORK_MODULE_H
#define API_NETWORK_MODULE_H

#include "web_api.h"
#include "communication_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== API Function Declarations ==================== */

/**
 * @brief Register network API module
 * @return Operation result
 */
aicam_result_t web_api_register_network_module(void);

/**
 * @brief Network status handler (Communication Overview)
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_status_handler(http_handler_context_t *ctx);

/**
 * @brief WiFi STA (Client) status handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_wifi_sta_handler(http_handler_context_t *ctx);

/**
 * @brief WiFi AP (Hotspot) config handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_wifi_ap_handler(http_handler_context_t *ctx);

/**
 * @brief WiFi configuration handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_wifi_config_handler(http_handler_context_t *ctx);

/**
 * @brief Network scan refresh handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_scan_refresh_handler(http_handler_context_t *ctx);

/**
 * @brief Network disconnect handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_disconnect_handler(http_handler_context_t *ctx);

/**
 * @brief Delete known network handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_delete_known_handler(http_handler_context_t *ctx);

#if NETIF_WIFI_HALOW_IS_ENABLE
aicam_result_t network_halow_sta_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_region_get_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_region_set_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_scan_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_connect_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_disconnect_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_delete_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_ip_handler(http_handler_context_t *ctx);
aicam_result_t network_halow_radio_handler(http_handler_context_t *ctx);
#endif

/* ==================== Communication Type APIs ==================== */

/**
 * @brief Get all communication types handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_comm_types_handler(http_handler_context_t *ctx);

/**
 * @brief Switch communication type handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_comm_switch_handler(http_handler_context_t *ctx);

/**
 * @brief Preferred type handler (GET/POST)
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_comm_prefer_handler(http_handler_context_t *ctx);

/**
 * @brief Apply priority handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_comm_priority_handler(http_handler_context_t *ctx);

/* ==================== Cellular/4G APIs ==================== */

/**
 * @brief Cellular status handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_status_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular settings handler (GET/POST)
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_settings_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular save settings handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_save_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular connect handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_connect_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular disconnect handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_disconnect_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular detailed info handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_info_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular refresh info handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_refresh_handler(http_handler_context_t *ctx);

/**
 * @brief Cellular AT command handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_cellular_at_handler(http_handler_context_t *ctx);

/* ==================== PoE/Ethernet APIs ==================== */

/**
 * @brief PoE status handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_status_handler(http_handler_context_t *ctx);

/**
 * @brief PoE connect handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_connect_handler(http_handler_context_t *ctx);

/**
 * @brief PoE disconnect handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_disconnect_handler(http_handler_context_t *ctx);

/**
 * @brief PoE detailed info handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_info_handler(http_handler_context_t *ctx);

/**
 * @brief PoE config handler (GET/POST)
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_config_handler(http_handler_context_t *ctx);

/**
 * @brief PoE validate config handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_validate_handler(http_handler_context_t *ctx);

/**
 * @brief PoE apply config handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_apply_handler(http_handler_context_t *ctx);

/**
 * @brief PoE save config handler
 * @param ctx HTTP request context
 * @return Operation result
 */
aicam_result_t network_poe_save_handler(http_handler_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* API_NETWORK_MODULE_H */
