/**
 * @file api_auth_module.c
 * @brief Authentication API Module
 * @details Authentication API with login and change password
 */

#include "web_api.h"
#include "web_server.h"
#include "auth_mgr.h"
#include "cJSON.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


/* ==================== API Handlers ==================== */

/**
 * @brief Login handler
 */
aicam_result_t login_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    //parse the body
    cJSON* request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    //get the username and password
    const char* password = web_api_get_string(request, "password");

    //check the username and password
    aicam_bool_t auth_result = AICAM_FALSE;
    if (password != NULL) {
        auth_result = auth_mgr_verify_password(password);
    }

    cJSON_Delete(request);
    request = NULL;
    password = NULL;

    //send the response
    if (auth_result == AICAM_TRUE) {
        return api_response_success(ctx, NULL, "Login successful");
    } else {
        return api_response_error(ctx, API_BUSINESS_ERROR_INVALID_PASSWORD, "Login failed, invalid password");
    }
}

/**
 * @brief Change password handler
 */
aicam_result_t change_password_handler(http_handler_context_t* ctx) {
    if (!ctx) return AICAM_ERROR_INVALID_PARAM;

    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }

    if (ctx->authenticated != AICAM_TRUE) {
        return api_response_error(ctx, API_ERROR_UNAUTHORIZED, "Authentication required");
    }

    cJSON* request = web_api_parse_body(ctx);
    if (!request) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON");
    }

    const char* password = web_api_get_string(request, "password");

    if (!password) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing required parameters");
    }

    //check the password length
    size_t password_len = strlen(password);
    if (password_len < 8 || password_len > 32) {
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid password length");
    }

    char new_password[33];
    memcpy(new_password, password, password_len + 1);

    cJSON_Delete(request);
    request = NULL;
    password = NULL;

    auth_result_t auth_result = auth_mgr_change_password(new_password);

    memset(new_password, 0, sizeof(new_password));

    if (auth_result == AUTH_RESULT_SUCCESS) {
        return api_response_success(ctx, NULL, "Password changed successfully");
    } else {
        return api_response_error(ctx, API_BUSINESS_ERROR_INVALID_PASSWORD, "Invalid credentials");
    }
}

/* ==================== Module Definition ==================== */

// Login module routes
static const api_route_t login_module_routes[] = {
    {
        .path = API_PATH_PREFIX "/login",
        .method = "POST",
        .handler = login_handler,
        .require_auth = AICAM_FALSE,
        .user_data = NULL
    },
    {
        .path = API_PATH_PREFIX "/change-password",
        .method = "POST",
        .handler = change_password_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/* ==================== Public API ==================== */

/**
 * @brief Register login module
 */
aicam_result_t web_api_register_auth_module(void) {
    LOG_CORE_INFO("Registering login module");
    
    for (int i = 0; i < sizeof(login_module_routes) / sizeof(login_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&login_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to register login module: %d", result);
            return result;
        }
    }
    
    LOG_CORE_INFO("Login module registered successfully");
    return AICAM_OK;
}
