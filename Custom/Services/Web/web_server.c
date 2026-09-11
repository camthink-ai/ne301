/**
 * @file web_server.c
 * @brief HTTP Web server implementation based on mongoose 
 */

#include "web_server.h"
#include "debug.h"
#include "buffer_mgr.h"
#include "mongoose.h"
#include "aicam_types.h"
#include "aicam_error.h"
#include "cmsis_os2.h"
#include "web_assets.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "common_utils.h"
#include "web_config.h"
#include "cJSON.h"
#include "auth_mgr.h"
#include "json_config_mgr.h"
#include "drtc.h"
#include "communication_service.h"
#include "system_service.h"
#include "api_business_error.h"
#include "device_service.h"
#include "api_ota_module.h"
#include "api_file_module.h"
#include "web_recovery.h"
#include "netif_manager.h"

#define WEB_SERVER_STACK_SIZE (1024 * 32)
#define WEB_SERVER_AP_SLEEP_TIMER_STACK_SIZE (1024 * 8)

 /* ==================== Web Debug Logging (see WEB_DEBUG in web_config.h) ==================== */
 #if WEB_DEBUG
 #define WEB_LOG(fmt, ...)  printf("[WEBDBG][tick %lums] " fmt "\r\n", (unsigned long) osKernelGetTickCount(), ##__VA_ARGS__)
 #else
 #define WEB_LOG(fmt, ...)  do { } while (0)
 #endif

 /* ==================== Global Variables ==================== */
 static uint8_t web_server_stack[WEB_SERVER_STACK_SIZE] ALIGN_32 IN_PSRAM;
 static uint8_t web_server_ap_sleep_timer_stack[WEB_SERVER_AP_SLEEP_TIMER_STACK_SIZE] ALIGN_32 IN_PSRAM;
 static web_server_t g_web_server = {0};


 
 /* ==================== Internal Function Declarations ==================== */
 
 static void web_server_event_handler(struct mg_connection *c, int ev, void *ev_data);
 static aicam_result_t web_server_handle_request(struct mg_connection *c, struct mg_http_message *hm);
 static aicam_result_t web_server_handle_api_request(http_handler_context_t* ctx);
 static aicam_result_t web_server_handle_static_request(http_handler_context_t* ctx);
 static aicam_result_t web_server_allocate_route_capacity(size_t new_capacity);
 static aicam_result_t web_server_find_route(const char* path, const char* method, const api_route_t** route);
 static aicam_result_t web_server_validate_request(http_handler_context_t* ctx);
 static aicam_result_t web_server_log_request(http_handler_context_t* ctx);
 static char *my_strdup(const char *s);
 static void web_server_ap_client_event_cb(netif_ap_client_event_t event, const uint8_t mac_addr[6]);

 /* ==================== FreeRTOS Task Attributes ==================== */
 static osThreadAttr_t web_server_task_attributes = {
    .name = "web_server",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_size = WEB_SERVER_STACK_SIZE,
    .stack_mem = web_server_stack,
 };

 static osThreadAttr_t web_server_ap_sleep_timer_task_attributes = {
    .name = "web_server_ap_sleep_timer",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = WEB_SERVER_AP_SLEEP_TIMER_STACK_SIZE,
    .stack_mem = web_server_ap_sleep_timer_stack,
 };

 /* ==================== FreeRTOS Task Function ==================== */
static void web_server_task_function(void* argument)
{
    (void)argument;
    /* Build listening address */
    char listen_addr[64];
    #if IS_HTTPS
    g_web_server.config.port = HTTPS_PORT;
    snprintf(listen_addr, sizeof(listen_addr), "https://0.0.0.0:%d", g_web_server.config.port);
    #else
    snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", g_web_server.config.port);
    #endif
    
    /* Create listening connection */
    g_web_server.listener = mg_http_listen(&g_web_server.mgr, listen_addr, web_server_event_handler, &g_web_server);
    if (!g_web_server.listener) {
        LOG_SVC_ERROR("[WEB_SERVER] Failed to create listening connection");
        return;
    }

    g_web_server.running = AICAM_TRUE;
    while (g_web_server.running)
    {
        mg_mgr_poll(&g_web_server.mgr, 10);
    }
}

static void web_server_ap_sleep_timer_task_function(void* argument)
{
    (void)argument;
    while (g_web_server.running)
    {
        web_server_ap_sleep_timer_check();
        ota_check_timeout(); // check if the OTA upload is timed out
        osDelay(1000); // 1 second delay
    }
}


/**
 * @brief Get relative timestamp
 * @return Relative timestamp
 */
static uint64_t get_relative_timestamp(void) {
    // Use system tick as relative time base, avoid affected by RTC time modification
    static uint32_t system_start_tick = 0;
    static uint64_t rtc_start_time = 0;
    
    if (system_start_tick == 0) {
        system_start_tick = osKernelGetTickCount();
        rtc_start_time = rtc_get_timeStamp();
    }
    
    uint32_t current_tick = osKernelGetTickCount();
    uint32_t elapsed_ticks = current_tick - system_start_tick;
    uint32_t elapsed_seconds = elapsed_ticks / osKernelGetTickFreq();
    
    return rtc_start_time + elapsed_seconds;
}

 
 /* ==================== Core HTTP Server Implementation ==================== */

/* Keep-alive idle reaper: a peer that vanishes without closing (WiFi drop,
 * tab kill) would leak its lwip netconn forever. Close web connections idle
 * longer than WEB_CONN_IDLE_TIMEOUT_MS; browsers close their own idle pooled
 * sockets well before this, so it should only ever fire on vanished peers.
 * ponytail: fixed 16-slot id table, bump WEB_CONN_TRACK_MAX if ever short. */
#define WEB_CONN_IDLE_TIMEOUT_MS  30000u
#define WEB_CONN_TRACK_MAX        16
static struct { unsigned long id; uint32_t last_ms; } s_conn_last_active[WEB_CONN_TRACK_MAX];

static void web_conn_touch(struct mg_connection *c, uint32_t now_ms)
{
    for (int i = 0; i < WEB_CONN_TRACK_MAX; i++) {
        if (s_conn_last_active[i].id == c->id || s_conn_last_active[i].id == 0) {
            s_conn_last_active[i].id = c->id;
            s_conn_last_active[i].last_ms = now_ms;
            return;
        }
    }
}

static void web_conn_forget(struct mg_connection *c)
{
    for (int i = 0; i < WEB_CONN_TRACK_MAX; i++) {
        if (s_conn_last_active[i].id == c->id) {
            s_conn_last_active[i].id = 0;
            return;
        }
    }
}

 aicam_result_t http_server_init(const http_server_config_t* config)
 {
     if (!config) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     if (g_web_server.initialized) {
         return AICAM_ERROR_ALREADY_INITIALIZED;
     }
 
     /* Initialize Mongoose manager */
     mg_mgr_init(&g_web_server.mgr);
     memset(s_conn_last_active, 0, sizeof(s_conn_last_active));
 
     /* Copy configuration */
     g_web_server.config = *config;
 
     /* Initialize API router */
     g_web_server.api_router.routes = NULL;
     g_web_server.api_router.route_count = 0;
     g_web_server.api_router.route_capacity = 0;
     g_web_server.api_router.base_path = NULL;
     g_web_server.api_router.enable_cors = config->enable_cors;
     g_web_server.api_router.enable_logging = config->enable_logging;
 
     /* Initialize statistics */
     memset(&g_web_server.stats, 0, sizeof(g_web_server.stats));
     g_web_server.stats.start_time = get_relative_timestamp();
 
 
     g_web_server.initialized = AICAM_TRUE;
     g_web_server.running = AICAM_FALSE;
 
     LOG_SVC_INFO("[WEB_SERVER] HTTP server initialized successfully\n");
     return AICAM_OK;
 }
 
 aicam_result_t http_server_deinit(void)
 {
     if (!g_web_server.initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
 
     /* Stop the server */
     if (g_web_server.running) {
         http_server_stop();
     }
 
     /* Free API router resources */
     if (g_web_server.api_router.routes) {
         buffer_free(g_web_server.api_router.routes);
         g_web_server.api_router.routes = NULL;
     }
 
     if (g_web_server.api_router.base_path) {
         buffer_free(g_web_server.api_router.base_path);
         g_web_server.api_router.base_path = NULL;
     }
 
 
     /* Free Mongoose manager */
     mg_mgr_free(&g_web_server.mgr);
 
     g_web_server.initialized = AICAM_FALSE;
 
     return AICAM_OK;
 }
 
 aicam_result_t http_server_start(void)
 {
     if (!g_web_server.initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
 
     if (g_web_server.running) {
         return AICAM_ERROR_ALREADY_RUNNING;
     }

    /* Initialize AP sleep timer from configuration */
    network_service_config_t *config = NULL;
    
    // Dynamically allocate configuration structure
    aicam_result_t result = json_config_get_network_service_config(config);
    if (result == AICAM_OK) {
        web_server_ap_sleep_timer_init(config->ap_sleep_time);
        LOG_SVC_INFO("[WEB_SERVER] AP sleep timer initialized with %u seconds timeout", 
                     config->ap_sleep_time);
    } else {
        /* Use default timeout if config is not available */
        web_server_ap_sleep_timer_init(600); // 10 minutes default
        LOG_SVC_INFO("[WEB_SERVER] AP sleep timer initialized with default 600 seconds timeout");
    }

    LOG_SVC_INFO("[WEB_SERVER] Starting web server task...\r\n");
    g_web_server.server_thread = osThreadNew(web_server_task_function, NULL, &web_server_task_attributes);
    if (!g_web_server.server_thread) {
        return AICAM_ERROR_SERVICE_INIT;
    }

    LOG_SVC_INFO("[WEB_SERVER] Starting AP timer task...\r\n");
    g_web_server.ap_sleep_timer_thread = osThreadNew(web_server_ap_sleep_timer_task_function, NULL, &web_server_ap_sleep_timer_task_attributes);
    if (!g_web_server.ap_sleep_timer_thread) {
        return AICAM_ERROR_SERVICE_INIT;
    }

    /* Subscribe once per boot: AP client association refreshes the sleep
     * countdown (see web_server_ap_client_event_cb). http_server_start may
     * run again after a stop cycle; keep the registration, only log retries. */
    static uint8_t ap_client_evt_subscribed = 0;
    if (!ap_client_evt_subscribed) {
        if (nm_subscribe_ap_client_event(web_server_ap_client_event_cb) == AICAM_OK) {
            ap_client_evt_subscribed = 1;
        } else {
            LOG_SVC_ERROR("[WEB_SERVER] AP client event subscribe failed");
        }
    }

     return AICAM_OK;
 }
 
 
 
 aicam_result_t http_server_stop(void)
 {
     if (!g_web_server.initialized || !g_web_server.running) {
         return AICAM_ERROR_BUSY;
     }
 
     g_web_server.running = AICAM_FALSE;
 
 
     return AICAM_OK;
 }
 
 aicam_result_t http_server_register_route(const api_route_t* route)
 {
	 if(!route) {
		LOG_SVC_ERROR("[WEB_SERVER] register route: invalid route");
		return AICAM_ERROR_INVALID_PARAM;
	 }
	 if(!route->path){
		LOG_SVC_ERROR("[WEB_SERVER] register route: invalid path");
		return AICAM_ERROR_INVALID_PARAM;
	 }
	 if(!route->method) {
		LOG_SVC_ERROR("[WEB_SERVER] register route: invalid method");
		return AICAM_ERROR_INVALID_PARAM;
	 }
	 if(!route->handler) {
		LOG_SVC_ERROR("[WEB_SERVER] register route: invalid handler");
		return AICAM_ERROR_INVALID_PARAM;
	 }
     if (!route || !route->path || !route->method || !route->handler) {
         LOG_SVC_ERROR("[WEB_SERVER] register route: invalid param");
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     if (!g_web_server.initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
 
     /* Check if capacity needs to be expanded */
     if (g_web_server.api_router.route_count >= g_web_server.api_router.route_capacity) {
         size_t new_capacity = g_web_server.api_router.route_capacity == 0 ? 8 : g_web_server.api_router.route_capacity * 2;
         aicam_result_t result = web_server_allocate_route_capacity(new_capacity);
         if (result != AICAM_OK) {
             return result;
         }
     }
 
     /* Add the route */
     g_web_server.api_router.routes[g_web_server.api_router.route_count++] = *route;
 
     return AICAM_OK;
 }
    
 /* ==================== API Gateway Implementation ==================== */
 
 aicam_result_t api_gateway_init(const char* base_path)
 {
     if (!base_path) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     if (!g_web_server.initialized) {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
 
     /* Set the base path */
     if (g_web_server.api_router.base_path) {
         buffer_free(g_web_server.api_router.base_path);
     }
 
     g_web_server.api_router.base_path = my_strdup(base_path);
     if (!g_web_server.api_router.base_path) {
         return AICAM_ERROR_NO_MEMORY;
     }
 
     return AICAM_OK;
 }
 
 aicam_result_t api_gateway_deinit(void)
 {
     if (g_web_server.api_router.base_path) {
         buffer_free(g_web_server.api_router.base_path);
         g_web_server.api_router.base_path = NULL;
     }
 
     return AICAM_OK;
 }


static aicam_result_t api_response_set(http_handler_context_t* ctx, 
                                   const char* data, 
                                   const char* message,
                                   int code,
                                   int error_code)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Set HTTP status code (default to 200 if 0)
    ctx->response.code = (code > 0) ? code : 200;
    
    // Set business error code (default to 0 for success)
    ctx->response.error_code = error_code;
    
    // Set message (default to "success" if NULL)
    ctx->response.message = (char*)(message ? message : "success");
    
    // Set data
    ctx->response.data = (char*)data;

    return AICAM_OK;
}


aicam_result_t api_response_success(http_handler_context_t* ctx, 
                                   const char* data, 
                                   const char* message)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    return api_response_set(ctx, data, message, 200, 0);
}
 
aicam_result_t api_response_error(http_handler_context_t* ctx,
                                  api_error_code_t error_code,
                                  const char* message)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    return api_response_set(ctx, NULL, message, 200, error_code);
}


/* Keep-alive idle reaper helpers live above http_server_init */
 static void web_server_event_handler(struct mg_connection *c, int ev, void *ev_data)
 {
    #if IS_HTTPS
     if (ev == MG_EV_ACCEPT) {
         // printf("[WEB]MG_EV_ACCEPT: %d\r\n", c->fd);
         struct mg_tls_opts opts = {
            .cert = {
                .buf = HTTPS_CERT_STR,
                .len = sizeof(HTTPS_CERT_STR) - 1,
            },
            .key = {
                .buf = HTTPS_KEY_STR,
                .len = sizeof(HTTPS_KEY_STR) - 1,
            },
            .skip_verification = 1
        };
        mg_tls_init(c, &opts);
     } else if (ev == MG_EV_TLS_HS) {
        // printf("[WEB]MG_EV_TLS_HS: %d\r\n", c->fd);
     } else if (ev == MG_EV_CLOSE) {
        // printf("[WEB]MG_EV_CLOSE: %d\r\n", c->fd);
     } else 
    #endif
     if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        // check if the request is for ota upload
        if (mg_match(hm->uri, mg_str(API_PATH_PREFIX "/system/ota/upload"), NULL)) {
            ota_upload_stream_processor(c, ev, ev_data);
            return;
        }
        // check if the request is for file upload - stream to disk
        if (mg_match(hm->uri, mg_str(API_PATH_PREFIX "/files/upload"), NULL) &&
            mg_match(hm->method, mg_str("POST"), NULL)) {
            file_upload_stream_processor(c, ev, ev_data);
            return;
        }
     }

    // check if the request is for ota upload or file upload
    // IMPORTANT: Do NOT process streams on:
    // - Listener connections (is_listening = 1)
    // - During MG_EV_OPEN event (pfn not set yet, would be &g_web_server not ota_ctx)
    // - When fn_data is the server instance (&g_web_server)
    if (c->fn_data != NULL && c->pfn == NULL &&
        !c->is_listening && c->fn_data != &g_web_server) {
        /* Streaming mode detached c->pfn, so MG_EV_HTTP_MSG never fires again
         * and the idle-reaper's last_ms stays at ACCEPT time: a stream that
         * outlives WEB_CONN_IDLE_TIMEOUT_MS (a ~3.8MB app package at
         * ~125KB/s) is reaped on its FIRST post-completion poll - before the
         * queued response can flush, since mg_iotest only asks for POLLOUT
         * once send.len > 0 - and close_conn() discards the unsent response.
         * Browser sees a failed XHR (0 B, ~30s) while the burn succeeded.
         * Refresh on socket-progress events only: MG_EV_POLL fires every
         * tick for open conns and would neuter the vanished-peer reap. */
        if (ev == MG_EV_READ || ev == MG_EV_WRITE) {
            web_conn_touch(c, osKernelGetTickCount());
            /* Socket progress also keeps the AP sleep timer alive: this
             * stream never fires MG_EV_HTTP_MSG, so without this a body
             * transfer longer than the low-power 90 s window is killed by
             * a mid-flight sleep. */
            web_server_ap_sleep_timer_reset();
        }
        /* Streaming CLOSE returns below before the lifecycle block's
         * web_conn_forget() - without this the ACCEPT-time entry leaks one
         * of the 16 table slots per transfer until reaping stops working. */
        if (ev == MG_EV_CLOSE) {
            web_conn_forget(c);
        }
        // use POLL or READ event to drive data write
        // most Mongoose versions will trigger callbacks after POLL or each IO
        /* Route by the connection's OWN context tag, not by the global
         * OTA flag: a file upload and an OTA upload running concurrently
         * (separate tabs/clients) would otherwise feed the file connection's
         * context to the OTA processor - misinterpreted context, unconsumed
         * body and a file handle leaked on close. Both context structs carry
         * their magic as the first field. */
        {
            uint32_t stream_magic = *(const uint32_t *)c->fn_data;
            if (stream_magic == OTA_UPLOAD_CTX_MAGIC) {
                ota_upload_stream_processor(c, ev, ev_data);
            } else {
                file_upload_stream_processor(c, ev, ev_data);
            }
        }
        return;
    }

    /* Connection lifecycle logs: catch keep-alive sockets dying at link level
     * (a POST sent on a dead pooled socket fails instantly browser-side) */
    if (ev == MG_EV_ACCEPT) {
        WEB_LOG("[CONN] accept id=%lu", (unsigned long) c->id);
        web_conn_touch(c, osKernelGetTickCount());
    } else if (ev == MG_EV_CLOSE) {
        WEB_LOG("[CONN] close  id=%lu", (unsigned long) c->id);
        web_conn_forget(c);
    } else if (ev == MG_EV_POLL && !c->is_listening) {
        uint32_t now = osKernelGetTickCount();
        for (int i = 0; i < WEB_CONN_TRACK_MAX; i++) {
            if (s_conn_last_active[i].id == c->id) {
                if (now - s_conn_last_active[i].last_ms > WEB_CONN_IDLE_TIMEOUT_MS) {
                    WEB_LOG("[CONN] idle-reap id=%lu", (unsigned long) c->id);
                    c->is_closing = 1;
                }
                break;
            }
        }
    }

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        web_conn_touch(c, osKernelGetTickCount());
        web_server_handle_request(c, hm);
    }


 }
 
static aicam_result_t web_server_handle_request(struct mg_connection *c, struct mg_http_message *hm)
{
    /* Update statistics */
    g_web_server.stats.total_requests++;
    g_web_server.stats.bytes_received += hm->message.len;
    
    /* Reset AP sleep timer on each request */
    web_server_ap_sleep_timer_reset();
 
     /* Create handler context */
     http_handler_context_t ctx = {0};
     ctx.conn = c;
     ctx.msg = hm;
     ctx.user_data = NULL;
     ctx.session_id = NULL;
     ctx.authenticated = AICAM_FALSE;
 
    /* Parse HTTP request */
    aicam_result_t result = http_parse_request(&ctx);
    if (result != AICAM_OK) {
        api_response_error(&ctx, API_ERROR_INVALID_REQUEST, "Failed to parse request");
        http_send_response(&ctx);   /* send the error, don't leave the client hanging until timeout */
        return result;
    }

    /* Handle CORS preflight OPTIONS request */
    if (strcmp(ctx.request.method, "OPTIONS") == 0) {
        WEB_LOG("[REQ] OPTIONS %s (preflight)", ctx.request.uri);
        mg_http_reply(c, 200,
                      "Connection: close\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                      "Access-Control-Max-Age: 86400\r\n",
                      "");
        c->is_draining = 1;
        return AICAM_OK;
    }

    WEB_LOG("[REQ] %s %s from %s", ctx.request.method, ctx.request.uri, ctx.request.client_ip);

    /* Validate request */
    result = web_server_validate_request(&ctx);
    if (result != AICAM_OK) {
        http_send_response(&ctx);   /* error fields already set by validate */
        return result;
    }
 
     /* Log request */
     if (g_web_server.config.enable_logging) {
         web_server_log_request(&ctx);
     }
 
     /* Handle API request */
     if (g_web_server.api_router.base_path && strncmp(hm->uri.buf, g_web_server.api_router.base_path, strlen(g_web_server.api_router.base_path)) == 0) {
         LOG_SVC_INFO("[WEB] handle api request\r\n");
         result = web_server_handle_api_request(&ctx);
         if (result == AICAM_OK) {
            /* send response */
            LOG_SVC_INFO("[WEB] send response\r\n");
            http_send_response(&ctx);
         }
     } else {
         /* Handle static resource request (sends the response internally) */
         LOG_SVC_INFO("[WEB] handle static request\r\n");
         result = web_server_handle_static_request(&ctx);
         WEB_LOG("[RSP] %s %s -> static, result=%d (%lu ms)",
                 ctx.request.method, ctx.request.uri, (int) result,
                 (unsigned long) (osKernelGetTickCount() - ctx.request.timestamp));
     }
 
     /* Clean up resources */
     if(ctx.response.data) {
         buffer_free(ctx.response.data);
     }
     
     return result;
 }
 
 static aicam_result_t web_server_handle_api_request(http_handler_context_t* ctx)
 {
     LOG_SVC_INFO("[WEB]enter web_server_handle_api_request");
     /* Find matching route */
     const api_route_t* route = NULL;
     aicam_result_t result = web_server_find_route(ctx->request.uri, ctx->request.method, &route);
     if (result != AICAM_OK) {
         LOG_SVC_INFO("[WEB]web_server_handle_api_request: find route failed");
         return api_response_error(ctx, API_ERROR_NOT_FOUND, "API endpoint not found");
     }
 
     /* Check authentication requirements */
     if (route->require_auth) {
         result = auth_verify_user(ctx);
         if (result != AICAM_OK) {
             return api_response_error(ctx, API_ERROR_UNAUTHORIZED, "Authentication required");
         }
     }
 
     /* Call the handler */
     result = route->handler(ctx);
 
     return result;
 }
 
 
 static aicam_result_t web_server_allocate_route_capacity(size_t new_capacity)
 {
    api_route_t *old_routes = g_web_server.api_router.routes;
    size_t old_count = g_web_server.api_router.route_count;

    api_route_t *new_routes = buffer_calloc(1, new_capacity * sizeof(api_route_t));
    if (!new_routes) return AICAM_ERROR_NO_MEMORY;

    if (old_routes && old_count > 0) {
        memcpy(new_routes, old_routes, old_count * sizeof(api_route_t));
        buffer_free(old_routes);
    }

    g_web_server.api_router.routes = new_routes;
    g_web_server.api_router.route_capacity = new_capacity;
    return AICAM_OK;
 }
 
 static aicam_result_t web_server_find_route(const char* path, const char* method, const api_route_t** route)
 {
     if (!path || !method || !route) {
         return AICAM_ERROR_INVALID_PARAM;
     }

     LOG_SVC_INFO("[WEB_SERVER] find route: path:%s method:%s\r\n", path, method);
    
     for(int i = 0; i < (int)g_web_server.api_router.route_count; i++) {
         if (strcmp(g_web_server.api_router.routes[i].path, path) == 0 &&
             strcasecmp(g_web_server.api_router.routes[i].method, method) == 0) {
             *route = &g_web_server.api_router.routes[i];
             return AICAM_OK;
         }
     }
 
     return AICAM_ERROR_NOT_FOUND;
 }
 
 static aicam_result_t web_server_validate_request(http_handler_context_t* ctx)
 {
     if (!ctx) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     /* Check request size */
     if (ctx->request.content_length > g_web_server.config.max_request_size) {
         api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Request body too large");
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     return AICAM_OK;
 }
 
 static aicam_result_t web_server_log_request(http_handler_context_t* ctx)
 {
     if (!ctx) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     /* Simple implementation: print request information */
     LOG_SVC_INFO("[WEB] %s %.*s from %s\r\n",
            ctx->request.method,
            (int)ctx->msg->uri.len, ctx->msg->uri.buf,
            ctx->request.client_ip);
 
     return AICAM_OK;
 }
 
 /* ==================== HTTP Request/Response Handling ==================== */
 
 aicam_result_t http_parse_request(http_handler_context_t* ctx)
 {
     if (!ctx || !ctx->msg) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     /* Parse HTTP method */
     snprintf(ctx->request.method, sizeof(ctx->request.method), "%.*s", (int)ctx->msg->method.len, ctx->msg->method.buf);
 
     /* Parse URI */
     snprintf(ctx->request.uri, sizeof(ctx->request.uri), "%.*s", (int)ctx->msg->uri.len, ctx->msg->uri.buf);

 
     /* Parse query string */
     if (ctx->msg->query.len > 0) {
         snprintf(ctx->request.query_string, sizeof(ctx->request.query_string), "%.*s", (int)ctx->msg->query.len, ctx->msg->query.buf);
     } else {
         ctx->request.query_string[0] = '\0';
     }
 
     /* Parse Content-Type */
     struct mg_str* content_type = mg_http_get_header(ctx->msg, "Content-Type");
     if (content_type) {
         snprintf(ctx->request.content_type, sizeof(ctx->request.content_type), "%.*s", (int)content_type->len, content_type->buf);
     } else {
         ctx->request.content_type[0] = '\0';
     }
 
     /* Parse Content-Length */
     ctx->request.content_length = ctx->msg->body.len;
 
     /* Parse request body */
     if (ctx->msg->body.len > 0) {
         ctx->request.body = (char*)ctx->msg->body.buf;
     } else {
         ctx->request.body = NULL;
     }
     
     /* Parse client IP */
     mg_snprintf(ctx->request.client_ip, sizeof(ctx->request.client_ip), "%M", mg_print_ip, &ctx->conn->rem);
 
 
     /* Parse User-Agent */
     struct mg_str* user_agent = mg_http_get_header(ctx->msg, "User-Agent");
     if (user_agent) {
         snprintf(ctx->request.user_agent, sizeof(ctx->request.user_agent), "%.*s", (int)user_agent->len, user_agent->buf);
     } else {
         ctx->request.user_agent[0] = '\0';
     }
 
     /* Parse Authorization */
     struct mg_str* authorization = mg_http_get_header(ctx->msg, "Authorization");
     if (authorization) {
         snprintf(ctx->request.authorization, sizeof(ctx->request.authorization), "%.*s", (int)authorization->len, authorization->buf);
     } else {
         ctx->request.authorization[0] = '\0';
     }
 
     /* Set timestamp: kernel tick (ms) — consumed by WEB_DEBUG duration log in http_send_response */
     ctx->request.timestamp = (uint32_t) osKernelGetTickCount();
 
     return AICAM_OK;
 }
 


aicam_result_t http_send_response(http_handler_context_t* ctx) {
    if (!ctx || !ctx->conn) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    // Create JSON root
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return AICAM_ERROR_NO_MEMORY;
    }

    // Determine success based on HTTP status code (2xx = success)
    aicam_bool_t is_success = (ctx->response.error_code == 0);
    cJSON_AddBoolToObject(root, "success", is_success);


    // Add business error_code (always present, 0 for success)
    if (ctx->response.error_code != 0 || !is_success) {
        cJSON_AddStringToObject(root, "error_code", api_business_error_code_to_string(ctx->response.error_code));
    }

    // Add message only when not NULL/empty
    if (ctx->response.message && ctx->response.message[0] != '\0') {
        cJSON_AddStringToObject(root, "message", ctx->response.message);
    }

    // Add data only when present and not empty
    if (ctx->response.data && ctx->response.data[0] != '\0') {
        cJSON *data_json = cJSON_Parse(ctx->response.data);
        if (data_json) {
            cJSON_AddItemToObject(root, "data", data_json);  // attach parsed JSON
        } else {
            // fallback: treat it as string
            cJSON_AddStringToObject(root, "data", ctx->response.data);
        }
    }

    // Print JSON to string
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        cJSON_Delete(root);
        return AICAM_ERROR_NO_MEMORY;
    }

    // Send response with CORS headers
    // Use code as HTTP status code (default to 200 if not set)
    int http_status = (ctx->response.code > 0) ? ctx->response.code : 200;
    mg_http_reply(ctx->conn,
                 http_status,
                 "Content-Type: application/json\r\n"
                 "Connection: close\r\n"
                 "Access-Control-Allow-Origin: *\r\n"
                 "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                 "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
                 "%s", json_str);

    WEB_LOG("[RSP] %s %s -> http=%d err=%s (%lu ms)",
            ctx->request.method, ctx->request.uri, http_status,
            api_business_error_code_to_string(ctx->response.error_code),
            (unsigned long) (osKernelGetTickCount() - ctx->request.timestamp));

    // Cleanup
    cJSON_free(json_str);  // free string allocated by cJSON
    cJSON_Delete(root);

    /* One request per connection, ANNOUNCED via "Connection: close" above:
     * the browser then never pools the socket, so our close can't race a
     * reused request (the old instant "Network Error"). Plain keep-alive was
     * tried and the lwip/mongoose socket layer here proved unreliable with
     * long-lived conns (idle conns sometimes stop getting POLL events). */

    ctx->conn->is_draining = 1;

    return AICAM_OK;
}
 
 
 /* ==================== Static Resource Management ==================== */

static aicam_result_t web_server_handle_static_request(http_handler_context_t* ctx)
{
    char decoded_uri[256];
    const web_asset_t* asset = NULL;
    const char* path_to_find;

    // Recovery mode: the normal web assets are missing, so serve the built-in
    // recovery page for any path. The page uploads a web firmware package to
    // the OTA endpoint to restore the full UI.
    if (web_recovery_is_active()) {
        const char* html = web_recovery_get_html();
        size_t html_size = web_recovery_get_html_size();
        mg_printf(ctx->conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/html; charset=utf-8\r\n"
                  "Content-Length: %u\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "\r\n",
                  (unsigned int)html_size);
        mg_send(ctx->conn, html, html_size);
        ctx->conn->is_draining = 1;
        return AICAM_OK;
    }

    // URL Decode the request URI
    mg_url_decode(ctx->msg->uri.buf, ctx->msg->uri.len, decoded_uri, sizeof(decoded_uri), 0);

    // If the root is requested, serve "index.html"
    if (strcmp(decoded_uri, "/") == 0) {
        path_to_find = "index.html";
    } else {
        path_to_find = decoded_uri;
    }

    LOG_SVC_INFO("[STATIC] Serving static file: %s\r\n", path_to_find);

    asset = web_asset_find(path_to_find);

    if (asset != NULL) {
        /* ETag = content CRC32, computed once per asset. Chrome re-fetches
         * worker scripts and importScripts() on every player (re)start even
         * with max-age set; without a validator it re-downloads the full
         * bundle each time - hundreds of KB over a lossy link per tab switch.
         * With the ETag those revalidations become 304s (a few hundred bytes). */
        static struct { const void *asset; uint32_t crc; } s_etag[32];
        uint32_t etag = 0;
        int cached = 0;
        for (int i = 0; i < (int) (sizeof(s_etag) / sizeof(s_etag[0])); i++) {
            if (s_etag[i].asset == (const void *) asset) {
                etag = s_etag[i].crc;
                cached = 1;
                break;
            }
            if (s_etag[i].asset == NULL) {
                s_etag[i].asset = (const void *) asset;
                s_etag[i].crc = etag = mg_crc32(0, (const char *) asset->data, asset->size);
                cached = 1;
                break;
            }
        }
        if (!cached) {
            etag = mg_crc32(0, (const char *) asset->data, asset->size);
        }
        char etag_hdr[16];
        snprintf(etag_hdr, sizeof(etag_hdr), "\"%08lx\"", (unsigned long) etag);

        struct mg_str *inm = mg_http_get_header(ctx->msg, "If-None-Match");
        /* match `"tag"` or `W/"tag"` (single-tag form; Chrome never sends a
         * list for a plain GET) */
        bool not_modified = false;
        if (inm != NULL) {
            const char *v = inm->buf;
            size_t len = inm->len;
            if (len >= 2 && v[0] == 'W' && v[1] == '/') { v += 2; len -= 2; }
            not_modified = (len == strlen(etag_hdr)) && (memcmp(v, etag_hdr, len) == 0);
        }
        if (not_modified) {
            WEB_LOG("[RSP] %s -> 304 not modified", path_to_find);
            mg_printf(ctx->conn, "HTTP/1.1 304 Not Modified\r\n"
                                 "ETag: %s\r\n"
                                 "Cache-Control: max-age=86400, public\r\n"
                                 "Connection: close\r\n"
                                 "\r\n",
                      etag_hdr);
            ctx->conn->is_draining = 1;
            return AICAM_OK;
        }

        mg_printf(ctx->conn, "HTTP/1.1 200 OK\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %d\r\n"
                             "ETag: %s\r\n"
                             "Cache-Control: max-age=86400, public\r\n"
                             "Connection: close\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                             "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                             "%s"
                             "\r\n",
                  asset->mime_type,
                  (int)asset->size,
                  etag_hdr,
                  asset->is_compressed ? "Content-Encoding: gzip\r\n" : "");

        mg_send(ctx->conn, (uint8_t*)asset->data, asset->size);
        LOG_SVC_INFO("[STATIC] Sent static data size: %d", asset->size);
        ctx->conn->is_draining = 1;
    } else {
        // File not found, send 404 with CORS headers
        mg_http_reply(ctx->conn, 404,
                      "Content-Type: text/plain\r\n"
                      "Connection: close\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type, Authorization\r\n",
                      "Not Found\n");
        ctx->conn->is_draining = 1;
    }
    
    return AICAM_OK;
}

 
 /* ==================== Authentication Management ==================== */
 
 aicam_result_t auth_verify_user(http_handler_context_t* ctx)
 {
     if (!ctx) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     /* Check Authorization header */
     if (ctx->request.authorization[0] == '\0') {
         return AICAM_ERROR_UNAUTHORIZED;
     }

     LOG_SVC_INFO("[AUTH] Authorization header: %s\r\n", ctx->request.authorization);
     
     // parse the authorization header
     char username[32] = {0};
     char password[32] = {0};

     mg_http_creds(ctx->msg, username, sizeof(username), password, sizeof(password));

     if(username[0] == '\0' || password[0] == '\0') {
         return AICAM_ERROR_UNAUTHORIZED;
     }

     LOG_SVC_INFO("[AUTH] Username: %s\r\n", username);
     LOG_SVC_INFO("[AUTH] Password: %s\r\n", password);


     if(strcmp(username, AUTH_ADMIN_USERNAME) != 0 || !auth_mgr_verify_password(password)) {
         return AICAM_ERROR_UNAUTHORIZED;
     }

     ctx->authenticated = AICAM_TRUE;
     return AICAM_OK;
 }
 
 aicam_result_t auth_check_permission(http_handler_context_t* ctx, permission_t required_permission)
 {
     if (!ctx) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     /* Check if authenticated */
     if (!ctx->authenticated) {
         return AICAM_ERROR_UNAUTHORIZED;
     }
 
     // In a real implementation, you would check user permissions here.
     (void)required_permission;
     return AICAM_OK;
 }
 
 /* ==================== Utility Functions ==================== */
 
 aicam_bool_t http_parse_query_param(const char* query_string,
                                    const char* param_name,
                                    char* param_value,
                                    size_t value_size)
 {
     if (!query_string || !param_name || !param_value || value_size == 0) {
         return AICAM_FALSE;
     }
     
     struct mg_str qs = mg_str(query_string);
     struct mg_str val = mg_http_var(qs, mg_str(param_name));
     
     if (val.buf) {
         snprintf(param_value, value_size, "%.*s", (int)val.len, val.buf);
         return AICAM_TRUE;
     }
     
     return AICAM_FALSE;
 }
 
 aicam_bool_t http_parse_json_body(const char* body,
                                  const char* key,
                                  char* value,
                                  size_t value_size)
 {
     if (!body || !key || !value || value_size == 0) {
         return AICAM_FALSE;
     }
     
     struct mg_str body_str = mg_str(body);
     struct mg_str val = mg_json_get_tok(body_str, key);
     
     if (val.buf) {
         snprintf(value, value_size, "%.*s", (int)val.len, val.buf);
         return AICAM_TRUE;
     }
 
     return AICAM_FALSE;
 }
 
 aicam_result_t http_generate_request_id(char* request_id, size_t buffer_size)
 {
     if (!request_id || buffer_size == 0) {
         return AICAM_ERROR_INVALID_PARAM;
     }
 
     /* Simple implementation: use timestamp and a random number */
     uint64_t timestamp = get_relative_timestamp();
     uint32_t random_val = (uint32_t)rand();
 
     snprintf(request_id, buffer_size, "%08x-%08x", (unsigned int)timestamp, (unsigned int)random_val);
 
     return AICAM_OK;
 }


static char *my_strdup(const char *s) {
   if (s == NULL) {
       return NULL;
   }

   size_t len = strlen(s) + 1;    
   char *copy = (char *)buffer_calloc(1, len);
   if (copy == NULL) {
       return NULL;
   }

   memcpy(copy, s, len); 
   return copy;
}

/* ==================== AP Client Event Handling ==================== */

/* Runs in the WiFi driver's event context (see netif_manager.h) — short and
 * non-blocking only. A station associating with the AP restarts the sleep
 * countdown so the device cannot fall asleep between the client joining and
 * its first HTTP request (user connected but has not opened the web UI yet). */
static void web_server_ap_client_event_cb(netif_ap_client_event_t event,
                                          const uint8_t mac_addr[6])
{
    if (event == NETIF_AP_CLIENT_CONNECTED) {
        LOG_SVC_INFO("[WEB_SERVER] AP client %02X:%02X:%02X:%02X:%02X:%02X connected, resetting AP sleep timer",
                     mac_addr[0], mac_addr[1], mac_addr[2],
                     mac_addr[3], mac_addr[4], mac_addr[5]);
        web_server_ap_sleep_timer_reset();
    }
}

/* ==================== AP Sleep Timer Management Functions ==================== */

aicam_result_t web_server_ap_sleep_timer_init(uint32_t sleep_timeout)
{
    if (!g_web_server.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_web_server.ap_sleep_timeout = sleep_timeout;
    g_web_server.last_request_time = get_relative_timestamp();
    g_web_server.ap_sleep_enabled = AICAM_TRUE;
    
    LOG_SVC_INFO("[WEB_SERVER] AP sleep timer initialized with timeout: %u seconds\n", sleep_timeout);
    return AICAM_OK;
}

aicam_result_t web_server_ap_sleep_timer_reset(void)
{
    if (!g_web_server.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    /* Unconditional: last_request_time is THE web-activity timestamp. It
     * feeds both the AP sleep countdown and the low-power 90 s web-idle
     * sleep check, so it must keep refreshing even when the AP sleep
     * feature is disabled — system sleep is a separate config. */
    g_web_server.last_request_time = get_relative_timestamp();

    return AICAM_OK;
}

aicam_result_t web_server_ap_sleep_timer_check(void)
{
    if (!g_web_server.initialized) {
        return AICAM_OK; // Not initialized, no action needed
    }

    uint64_t current_time = get_relative_timestamp();
    uint64_t time_since_last_request = current_time - g_web_server.last_request_time;

    //get current power mode
    power_mode_t current_power_mode = system_service_get_current_power_mode();


    if(time_since_last_request >= 90 && current_power_mode == POWER_MODE_LOW_POWER) {
        //enter sleep mode — system sleep is a SEPARATE config from AP sleep:
        // it must keep triggering even when the AP sleep feature is disabled
        LOG_SVC_INFO("[WEB_SERVER] AP sleep timeout reached (90s) in low power mode, entering sleep");
        system_service_task_completed();

        return AICAM_OK;
    }

    if (!g_web_server.ap_sleep_enabled ) {
        return AICAM_OK; // AP sleep feature disabled — everything below is AP-sleep behavior only
    }

    if(g_web_server.ap_sleep_timeout == 0) {
        // no sleep timeout, keep AP running
        return AICAM_OK;
    }
    else if (time_since_last_request >= g_web_server.ap_sleep_timeout) {
        
        // Shutdown AP hotspot
        if (communication_is_interface_connected(NETIF_NAME_WIFI_AP)) {
            LOG_SVC_INFO("[WEB_SERVER] AP is connected, AP sleep timeout reached (%u seconds), shutting down AP hotspot", (uint32_t)g_web_server.ap_sleep_timeout);
            aicam_result_t ret = communication_stop_interface(NETIF_NAME_WIFI_AP);
            if (ret != AICAM_OK) {
                LOG_SVC_ERROR("[WEB_SERVER] Failed to shut down AP hotspot: %d", ret);
            }
            else {
                // Set indicator to slow blink (AP off, system running)
                device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_OFF);
            }
        }
        
        return AICAM_OK;
    } else {
        // Log remaining time for debugging (only every 60 seconds to avoid spam)
        static uint32_t last_log_time = 0;
        if (current_time - last_log_time >= 60) {
            last_log_time = current_time;
            uint32_t remaining_time = g_web_server.ap_sleep_timeout - time_since_last_request;
            LOG_SVC_DEBUG("[WEB_SERVER] AP sleep timer: %u seconds remaining", remaining_time);
        }

        if(communication_is_interface_connected(NETIF_NAME_WIFI_AP) == AICAM_FALSE) {
            LOG_SVC_INFO("[WEB_SERVER] AP is not connected, starting AP");
            aicam_result_t ret = communication_start_interface(NETIF_NAME_WIFI_AP);
            if (ret != AICAM_OK) {
                LOG_SVC_ERROR("[WEB_SERVER] Failed to start AP: %d", ret);
            }
            else {
                // Set indicator to solid on (AP on, system running)
                device_service_set_indicator_state(SYSTEM_INDICATOR_RUNNING_AP_ON);
            }
        }
    }
    
    return AICAM_OK;
}

aicam_result_t web_server_ap_sleep_timer_enable(aicam_bool_t enabled)
{
    if (!g_web_server.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    g_web_server.ap_sleep_enabled = enabled;
    
    if (enabled) {
        g_web_server.last_request_time = get_relative_timestamp();
        LOG_SVC_INFO("[WEB_SERVER] AP sleep timer enabled");
    } else {
        LOG_SVC_INFO("[WEB_SERVER] AP sleep timer disabled");
    }
    
    return AICAM_OK;
}

aicam_result_t web_server_ap_sleep_timer_get_status(uint32_t* timeout, aicam_bool_t* enabled, uint32_t* remaining_time)
{
    if (!g_web_server.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (timeout) {
        *timeout = g_web_server.ap_sleep_timeout;
    }
    
    if (enabled) {
        *enabled = g_web_server.ap_sleep_enabled;
    }
    
    if (remaining_time) {
        if (g_web_server.ap_sleep_enabled) {
            uint64_t current_time = get_relative_timestamp();
            uint64_t time_since_last_request = current_time - g_web_server.last_request_time;
            if (time_since_last_request < g_web_server.ap_sleep_timeout) {
                *remaining_time = (uint32_t)(g_web_server.ap_sleep_timeout - time_since_last_request);
            } else {
                *remaining_time = 0;
            }
        } else {
            *remaining_time = 0;
        }
    }
    
    return AICAM_OK;
}

aicam_result_t web_server_ap_sleep_timer_update(uint32_t sleep_timeout)
{
    if (!g_web_server.initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    LOG_SVC_INFO("[WEB_SERVER] Updating AP sleep timer to %u seconds", sleep_timeout);
    g_web_server.ap_sleep_timeout = sleep_timeout;
    return AICAM_OK;
}   
