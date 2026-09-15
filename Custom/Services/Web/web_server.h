/**
 * @file web_server.h
 * @brief HTTP Web server based on mongoose - Complete API gateway design
 * @author AI Assistant
 * @date 2024
 */

 #ifndef WEB_SERVER_H
 #define WEB_SERVER_H
 
 #include "mongoose.h"
 #include "aicam_types.h"
 #include "aicam_error.h"
 #include <stdint.h>
 #include <stddef.h>
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* ==================== Basic Data Structures ==================== */
 
 /**
  * @brief HTTP request structure
  */
 typedef struct {
     char method[16];                  ///< HTTP method (GET, POST, PUT, DELETE)
     char uri[256];                    ///< Request URI
     char query_string[512];           ///< Query string
     char content_type[128];            ///< Content-Type
     size_t content_length;            ///< Content length
     char* body;                       ///< Request body data
     char client_ip[16];               ///< Client IP address
     char user_agent[128];             ///< User-Agent
     char authorization[256];          ///< Authorization header
     uint32_t timestamp;               ///< Request timestamp
 } http_request_t;
 
 /**
  * @brief HTTP response structure
  */
 typedef struct {
     int status_code;                  ///< HTTP status code
     char content_type[128];            ///< Content-Type
     char* body;                       ///< Response body data
     size_t body_length;               ///< Response body length
     aicam_bool_t compressed;          ///< Whether the body is compressed
     char* headers;                    ///< Custom headers
     size_t header_count;              ///< Number of headers
 } http_response_t;
 
 /**
  * @brief HTTP server configuration structure
  */
 typedef struct {
     uint16_t port;                    ///< HTTP server port, default 80
     uint8_t max_connections;          ///< Maximum concurrent connections
     uint8_t enable_ssl;               ///< Whether to enable SSL/TLS
     uint16_t session_timeout;         ///< Session timeout in seconds
     char root_path[64];               ///< Website root directory path
     aicam_bool_t enable_logging;      ///< Whether to enable access logging
     aicam_bool_t enable_cors;         ///< Whether to enable CORS support
     char ssl_cert[128];               ///< SSL certificate file path
     char ssl_key[128];                ///< SSL private key file path
     uint32_t max_request_size;        ///< Maximum request size
     uint32_t request_timeout;         ///< Request timeout in seconds
     aicam_bool_t enable_gzip;         ///< Whether to enable gzip compression
 } http_server_config_t;
 
 /* ==================== API Gateway Data Structures ==================== */
 
 /**
  * @brief API error code enumeration
  */
 typedef enum {
     API_ERROR_NONE = 0,               ///< No error
     API_ERROR_INVALID_REQUEST = 400,  ///< Invalid request
     API_ERROR_UNAUTHORIZED = 401,     ///< Unauthorized
     API_ERROR_FORBIDDEN = 403,        ///< Forbidden
     API_ERROR_NOT_FOUND = 404,        ///< Not found
     API_ERROR_METHOD_NOT_ALLOWED = 405, ///< Method not allowed
     API_ERROR_INTERNAL_ERROR = 500,   ///< Internal error
     API_ERROR_SERVICE_UNAVAILABLE = 503, ///< Service unavailable
     API_ERROR_TIMEOUT = 408,          ///< Timeout
     API_ERROR_TOO_MANY_REQUESTS = 429, ///< Too many requests
     API_ERROR_BAD_GATEWAY = 502,      ///< Bad gateway
     API_ERROR_GATEWAY_TIMEOUT = 504   ///< Gateway timeout
 } api_error_code_t;
 
 /**
  * @brief Unified API response format
  */
#define WEB_API_MESSAGE_MAX 256
 typedef struct {
     int code;                         ///< Response code (200=success, non-200=error)
     int error_code;                   ///< Error code (0=no error, non-0=error)
     char message[WEB_API_MESSAGE_MAX]; ///< Response message (owned copy, safe after handler returns)
     char* data;                       ///< Response data (JSON string; freed by the dispatcher unless data_borrowed)
     aicam_bool_t data_borrowed;       ///< data points at rodata/static/caller-owned storage — dispatcher must NOT free it
     aicam_bool_t prepared;            ///< envelope composed via api_response_* — dispatcher sends it even if the handler returns an error
     aicam_bool_t sent;                ///< http_send_response already ran — handlers that self-send suppress the dispatcher's send
     uint32_t timestamp;               ///< Timestamp
     char request_id[64];              ///< Request ID
     char* headers;                    ///< Custom headers
 } api_response_t;
 
 /**
  * @brief HTTP handler context
  */
 typedef struct {
     struct mg_connection* conn;       ///< Mongoose connection object
     struct mg_http_message* msg;      ///< HTTP message object
     http_request_t request;           ///< Parsed request
     api_response_t response;          ///< Response object
     void* user_data;                  ///< User data
     char* session_id;                 ///< Session ID
     aicam_bool_t authenticated;       ///< Whether the user is authenticated
 } http_handler_context_t;
 
 
 /**
  * @brief API route structure
  */
 typedef struct {
     const char* path;                 ///< URL path
     const char* method;               ///< HTTP method
     aicam_result_t (*handler)(http_handler_context_t* ctx); ///< Handler function
     aicam_bool_t require_auth;        ///< Whether authentication is required
     void* user_data;                  ///< User data
 } api_route_t;
 
 /**
  * @brief API router structure
  */
 typedef struct {
     api_route_t* routes;              ///< Array of routes
     size_t route_count;               ///< Number of routes
     size_t route_capacity;            ///< Capacity of the route array
     char* base_path;                  ///< Base path for all routes
     aicam_bool_t enable_cors;         ///< Whether to enable CORS
     aicam_bool_t enable_logging;      ///< Whether to enable logging
 } api_router_t;
 
 /* ==================== Authentication-related Data Structures ==================== */
 
 /**
  * @brief User permission enumeration
  */
 typedef enum {
     PERMISSION_NONE = 0,              ///< No permission
     PERMISSION_READ = 1,              ///< Read permission
     PERMISSION_WRITE = 2,             ///< Write permission
     PERMISSION_ADMIN = 4,             ///< Admin permission
     PERMISSION_ALL = 7                ///< All permissions
 } permission_t;
 
 /**
  * @brief User information structure
  */
 typedef struct {
     char username[32];                ///< Username
     char password_hash[64];           ///< Password hash
     permission_t permissions;         ///< User permissions
     uint32_t last_login;              ///< Last login time
     aicam_bool_t is_active;           ///< Whether the user is active
 } user_info_t;
 
 /* ==================== Static Resource Management ==================== */
 
/**
 * @brief Header for the web-assets.bin file (64 bytes)
 * Matches the new binary asset format.
 */
typedef struct {
    char     magic[8];                ///< Magic number: "WEBASSETS"
    uint32_t version;                 ///< Version of the asset file
    uint32_t file_count;              ///< Total number of files in the data section
    uint32_t total_size;              ///< Total size of the data section
    uint32_t compressed_size;         ///< Compressed size (if applicable, 0 otherwise)
    uint32_t timestamp;               ///< Build timestamp of the asset file
    uint8_t  reserved[36];            ///< Reserved for future use
} asset_bin_header_t;

/**
 * @brief Index entry for a single file in the asset binary (32 bytes)
 * Matches the new binary asset format.
 */
typedef struct {
    char     path[24];                ///< Null-terminated file path (e.g., "/css/style.css")
    uint32_t offset;                  ///< Offset of the file data from the beginning of the data section
    uint32_t size;                    ///< Size of the file in bytes
} asset_file_index_t;


/**
 * @brief In-memory representation of a single static file's data.
 */
typedef struct {
    const char* data;                 ///< Pointer to the file data in memory
    size_t size;                      ///< File size
    char content_type[64];            ///< Deduced Content-Type
} asset_file_t;


/**
 * @brief Asset loader structure to manage the loaded web-assets.bin
 */
typedef struct {
    const asset_bin_header_t* header; ///< Pointer to the header in the binary data
    const asset_file_index_t* index;  ///< Pointer to the index table in the binary data
    const char* data_section;         ///< Pointer to the start of the file data section
    aicam_bool_t initialized;         ///< Initialization status
} asset_loader_t;
 
 /**
  * @brief Static file handler structure
  */
 typedef struct {
     asset_loader_t* loader;           ///< Asset loader
     char* cache_dir;                  ///< Cache directory
     aicam_bool_t enable_cache;        ///< Whether to enable caching
     uint32_t cache_timeout;           ///< Cache timeout
     char* default_file;               ///< Default file
 } static_handler_t;
 
 /* ==================== Web Server Core Structure ==================== */
 
 /**
  * @brief Web server instance structure
  */
 typedef struct {
     struct mg_mgr mgr;                ///< Mongoose manager
     struct mg_connection* listener;   ///< Listening connection
     http_server_config_t config;      ///< Server configuration
     aicam_bool_t initialized;         ///< Initialization status
     aicam_bool_t running;             ///< Running status
 
     /* API Gateway */
     api_router_t api_router;          ///< API router
 
     /* Authentication Management */
     user_info_t* users;               ///< List of users
     size_t user_count;                ///< Number of users
 
     /* Statistics */
     struct {
         uint32_t total_requests;      ///< Total number of requests
         uint32_t active_connections;  ///< Current number of active connections
         uint32_t bytes_sent;          ///< Number of bytes sent
         uint32_t bytes_received;      ///< Number of bytes received
         uint32_t error_count;         ///< Error count
         uint32_t start_time;          ///< Start timestamp
     } stats;
 
    /* Thread-related */
    void* server_thread;              ///< Server thread
    void* mutex;                      ///< Mutex
    
    /* AP Sleep Timer */
    uint32_t ap_sleep_timeout;        ///< AP sleep timeout in seconds
    uint64_t last_request_time;       ///< Last request timestamp
    aicam_bool_t ap_sleep_enabled;    ///< Whether AP sleep is enabled
    void* ap_sleep_timer_thread;      ///< AP sleep timer thread
} web_server_t;
 
 /* ==================== Core API Functions ==================== */
 
 /**
  * @brief Initialize the HTTP server
  * @param config Server configuration
  * @return Operation result
  */
 aicam_result_t http_server_init(const http_server_config_t* config);
 
 /**
  * @brief Deinitialize the HTTP server
  * @return Operation result
  */
 aicam_result_t http_server_deinit(void);
 
 /**
  * @brief Start the HTTP server
  * @return Operation result
  */
 aicam_result_t http_server_start(void);
 
 /**
  * @brief Stop the HTTP server
  * @return Operation result
  */
 aicam_result_t http_server_stop(void);
 
 /**
  * @brief Register an API route
  * @param route API route
  * @return Operation result
  */
 aicam_result_t http_server_register_route(const api_route_t* route);

 
 /* ==================== API Gateway Functions ==================== */
 
 /**
  * @brief Initialize the API gateway
  * @param base_path Base path for all routes
  * @return Operation result
  */
 aicam_result_t api_gateway_init(const char* base_path);
 
 /**
  * @brief Deinitialize the API gateway
  * @return Operation result
  */
 aicam_result_t api_gateway_deinit(void);
 
/**
 * @brief Generate a success response
 * @param ctx Handler context
 * @param data Response data (JSON string, can be NULL)
 * @param message Response message (can be NULL, will use "success" as default)
 * @return Operation result
 */
aicam_result_t api_response_success(http_handler_context_t* ctx,
                                   const char* data,
                                   const char* message);

/**
 * @brief Generate a success response with borrowed data
 * @details Same envelope as api_response_success, but data points at storage
 *          the caller owns or that is not heap-allocated (string literal,
 *          static buffer): the dispatcher will NOT free it.
 * @param ctx Handler context
 * @param data Response data (borrowed, can be NULL)
 * @param message Response message (can be NULL, will use "success" as default)
 * @return Operation result
 */
aicam_result_t api_response_success_static(http_handler_context_t* ctx,
                                   const char* data,
                                   const char* message);

/**
 * @brief Generate an error response
 * @param ctx Handler context
 * @param error_code Business error code (e.g., 400, 500, 504)
 * @param message Error message
 * @return Operation result
 */
aicam_result_t api_response_error(http_handler_context_t* ctx,
                                  api_error_code_t error_code,
                                  const char* message);

/**
 * @brief Generate an error response that still carries a data payload
 * @details For endpoints whose failure detail lives in data (e.g. per-field
 *          results). data follows the same ownership rule as
 *          api_response_success: heap (cJSON) memory freed by the dispatcher.
 * @param ctx Handler context
 * @param error_code Business error code (e.g., 400, 500, 504)
 * @param message Error message
 * @param data Response data (JSON string, can be NULL)
 * @return Operation result
 */
aicam_result_t api_response_error_data(http_handler_context_t* ctx,
                                  api_error_code_t error_code,
                                  const char* message,
                                  const char* data);

 
 /**
  * @brief Parse an HTTP request
  * @param ctx Handler context
  * @return Operation result
  */
 aicam_result_t http_parse_request(http_handler_context_t* ctx);
 
 /**
  * @brief Send an HTTP response
  * @param ctx Handler context
  * @return Operation result
  */
 aicam_result_t http_send_response(http_handler_context_t* ctx);
 
 
 /* ==================== Authentication Management Functions ==================== */
 
 /**
  * @brief Verify user authentication
  * @param ctx Handler context
  * @return Operation result
  */
 aicam_result_t auth_verify_user(http_handler_context_t* ctx);
 
 /**
  * @brief Check user permission
  * @param ctx Handler context
  * @param required_permission Required permission
  * @return Operation result
  */
 aicam_result_t auth_check_permission(http_handler_context_t* ctx, permission_t required_permission);
 
 /* ==================== Utility Functions ==================== */
 
 /**
  * @brief Parse a URL query parameter
  * @param query_string Query string
  * @param param_name Parameter name
  * @param param_value Buffer for the parameter value
  * @param value_size Size of the buffer
  * @return Whether the parameter was found
  */
 aicam_bool_t http_parse_query_param(const char* query_string,
                                    const char* param_name,
                                    char* param_value,
                                    size_t value_size);
 
 /**
  * @brief Parse a JSON request body
  * @param body Request body
  * @param key Key name
  * @param value Buffer for the value
  * @param value_size Size of the buffer
  * @return Whether the key was found
  */
 aicam_bool_t http_parse_json_body(const char* body,
                                  const char* key,
                                  char* value,
                                  size_t value_size);
 
/**
 * @brief Generate a request ID
 * @param request_id Buffer for the request ID
 * @param buffer_size Size of the buffer
 * @return Operation result
 */
aicam_result_t http_generate_request_id(char* request_id, size_t buffer_size);

/* ==================== AP Sleep Timer Management Functions ==================== */

/**
 * @brief Initialize AP sleep timer
 * @param sleep_timeout Sleep timeout in seconds
 * @return Operation result
 */
aicam_result_t web_server_ap_sleep_timer_init(uint32_t sleep_timeout);

/**
 * @brief Reset AP sleep timer (call on each request)
 * @return Operation result
 */
aicam_result_t web_server_ap_sleep_timer_reset(void);

/**
 * @brief Check if AP should be put to sleep
 * @return Operation result
 */
aicam_result_t web_server_ap_sleep_timer_check(void);

/**
 * @brief Enable/disable AP sleep timer
 * @param enabled Whether to enable AP sleep timer
 * @return Operation result
 */
aicam_result_t web_server_ap_sleep_timer_enable(aicam_bool_t enabled);

/**
 * @brief Get AP sleep timer status
 * @param timeout Current timeout value (output)
 * @param enabled Whether timer is enabled (output)
 * @param remaining_time Remaining time until sleep (output)
 * @return Operation result
 */
aicam_result_t web_server_ap_sleep_timer_get_status(uint32_t* timeout, aicam_bool_t* enabled, uint32_t* remaining_time);

/**
 * @brief Update AP sleep timer timeout
 * @param sleep_timeout New timeout in seconds
 * @return Operation result
 */
aicam_result_t web_server_ap_sleep_timer_update(uint32_t sleep_timeout);

 /* ==================== Configuration Macros ==================== */
 
 /**
  * @brief Default HTTP server configuration initialization macro
  */
 #define HTTP_SERVER_CONFIG_INIT() (http_server_config_t){ \
    .port = 80, \
    .max_connections = 10, \
    .enable_ssl = AICAM_FALSE, \
    .session_timeout = 300, \
    .root_path = "/web", \
    .enable_logging = AICAM_TRUE, \
    .enable_cors = AICAM_TRUE, \
    .ssl_cert = "", \
    .ssl_key = "", \
    .max_request_size = 5 * 1024 * 1024, \
    .request_timeout = 30, \
    .enable_gzip = AICAM_TRUE \
}


 
 /**
  * @brief Magic number for the asset binary file
  */
 #define ASSET_BIN_MAGIC 0x12345678
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* WEB_SERVER_H */