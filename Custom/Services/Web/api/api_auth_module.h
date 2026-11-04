/**
 * @file api_auth_module.h
 * @brief Authentication API Module Header
 */

#ifndef API_LOGIN_MODULE_H
#define API_LOGIN_MODULE_H

#include "aicam_types.h"
#include "web_server.h"

#ifdef __cplusplus
extern "C" {
#endif

aicam_result_t login_handler(http_handler_context_t* ctx);

aicam_result_t change_password_handler(http_handler_context_t* ctx);

/**
 * @brief Register login module
 * @return Operation result
 */
aicam_result_t web_api_register_auth_module(void);

#ifdef __cplusplus
}
#endif

#endif /* API_LOGIN_MODULE_H */
