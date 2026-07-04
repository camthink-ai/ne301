/**
 * @file json_config_mgr.h
 * @brief AI Camera JSON Configuration Management System Header File
 * @details JSON configuration management system based on storage interface, supporting parsing, saving, validation and other functions
 */

 #ifndef JSON_CONFIG_MGR_H
 #define JSON_CONFIG_MGR_H
 
 #include "aicam_types.h"
 #include "storage.h"
 #include <stdint.h>
 #include <stdbool.h>
 #include <stddef.h>
 #include "ms_mqtt_client.h"
 #include "netif_manager.h"
#include "isp_services.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 #define IO_TRIGGER_MAX 2
 #define MAX_TOPIC_LENGTH 128
 
 /* ==================== Configuration Data Structure Definitions ==================== */
 
 // Debug configuration structure
 typedef struct {
     aicam_bool_t ai_enabled;       // AI inference switch
     aicam_bool_t ai_1_active;      // AI_1 active switch
     uint32_t confidence_threshold;             // Confidence threshold 0-100
     uint32_t nms_threshold;                    // NMS threshold 0-100
     aicam_bool_t overlay_results;  // Draw AI results onto encoded frames
     uint32_t inference_interval_ms;            // AI inference pacing interval in ms (0 = every frame)
 } ai_debug_config_t;
 
typedef struct {
    // Basic device information
    char device_name[64];                    // Device name
    char mac_address[18];                    // MAC address
    char serial_number[32];                  // SN serial number
    char hardware_version[32];               // Hardware version
    char software_version[32];               // Software version
    char camera_module[64];                  // Camera module info
    char extension_modules[128];             // Extension modules (placeholder)
    char storage_card_info[128];             // Storage card info
    float storage_usage_percent;             // Storage usage percentage
    char power_supply_type[32];              // Power supply type (Battery/External)
    float battery_percent;                   // Battery percentage (only valid for battery power)
    char communication_type[64];             // Current communication type
} device_info_config_t;
 
 typedef struct {
     uint32_t pin_number; // or other identifier
     aicam_bool_t enable;
     aicam_bool_t input_enable;
     aicam_bool_t output_enable;
     aicam_trigger_type_t input_trigger_type;
     aicam_trigger_type_t output_trigger_type;
 } io_trigger_config_t;
 
 typedef struct {
     aicam_bool_t enable;
     uint32_t pin_number;
     aicam_trigger_type_t trigger_type;
     // PIR sensor configuration parameters
     uint8_t sensitivity_level;  // Sensitivity level (recommended >30, smaller value means more sensitive)
     uint8_t ignore_time_s;      // Ignore time after interrupt (0-15, actual time = 0.5 + 0.5 * value seconds)
     uint8_t pulse_count;        // Pulse count (1-4, actual count = value + 1)
     uint8_t window_time_s;      // Window time (0-3, actual time = 2 + 2 * value seconds)
 } pir_trigger_config_t;
 
 typedef struct {
     aicam_bool_t enable;
     aicam_timer_capture_mode_t capture_mode;
     uint32_t interval_sec;
     uint32_t time_node_count;
     uint32_t time_node[10]; // 10 time nodes
     uint8_t weekdays[10]; // 0: all days, 1: Monday, 2: Tuesday, 3: Wednesday, 4: Thursday, 5: Friday, 6: Saturday, 7: Sunday
     aicam_timer_interval_mode_t interval_mode; // 0=normal (immediate), 1=scheduled (from start_time)
     uint32_t start_time;                       // Seconds since midnight, only for scheduled mode
 } timer_trigger_config_t;

 typedef struct {
    aicam_bool_t enable;
 } remote_trigger_config_t;
 
 
 typedef struct {
     aicam_bool_t enable;
 } image_mode_config_t;

 typedef struct {
    io_trigger_config_t io_trigger[IO_TRIGGER_MAX];
    timer_trigger_config_t timer_trigger;
    pir_trigger_config_t pir_trigger;
    remote_trigger_config_t remote_trigger;
 } trigger_config_t;
 
typedef struct {
    aicam_bool_t enable;                    // Video stream mode enable
    char rtsp_server_url[256];              // RTSP URL (reserved)
    
    // RTMP configuration
    aicam_bool_t rtmp_enable;               // RTMP streaming enable
    char rtmp_url[256];                     // RTMP server URL
    char rtmp_stream_key[128];              // Stream key

    // RTSP server configuration
    aicam_bool_t rtsp_enable;               // RTSP service enable
    uint16_t rtsp_port;                     // RTSP listen port (default 554)
    char rtsp_auth_mode[16];                // Auth mode: "none" or "digest"
    char rtsp_username[64];                 // Auth username
    char rtsp_password[64];                 // Auth password
} video_stream_mode_config_t;
 
// Work mode configuration structure
typedef struct {
    aicam_work_mode_t work_mode;                  // Work mode
    image_mode_config_t image_mode;
    video_stream_mode_config_t video_stream_mode;
    io_trigger_config_t io_trigger[IO_TRIGGER_MAX];
    timer_trigger_config_t timer_trigger;
    pir_trigger_config_t pir_trigger;
    remote_trigger_config_t remote_trigger;
} work_mode_config_t;

/**
 * @brief Network scan result
 */
typedef struct {
    char ssid[32];                          // Network SSID
    char bssid[18];                         // Network BSSID
    char password[64];                      // Network password
    int32_t rssi;                           // Signal strength
    uint32_t channel;                       // WiFi channel
    wireless_security_t security;           // Security type
    aicam_bool_t connected;                 // Currently connected
    aicam_bool_t is_known;                  // Known network (previously connected)
    uint32_t last_connected_time;           // Last connection timestamp
} network_scan_result_t;


/**
 * @brief Cellular connection settings for persistent storage
 */
typedef struct {
    char apn[32];                           // APN (Access Point Name)
    char username[64];                      // APN username
    char password[64];                      // APN password
    char pin_code[16];                      // SIM PIN code
    uint8_t authentication;                 // Authentication type (0=None, 1=PAP, 2=CHAP, 3=Auto)
    aicam_bool_t enable_roaming;            // Enable roaming
    uint8_t operator;                       // Mobile operator (0=Auto, 1=CMCC, 2=CUCC, 3=CTCC, etc.)
} cellular_config_persist_t;

/**
 * @brief PoE/Ethernet IP mode
 */
typedef enum {
    POE_IP_MODE_DHCP = 0,                   // DHCP mode (default)
    POE_IP_MODE_STATIC,                     // Static IP mode
} poe_ip_mode_t;

/**
 * @brief PoE/Ethernet connection status codes
 */
typedef enum {
    POE_STATUS_OFFLINE = 0,                 // PoE offline / not powered
    POE_STATUS_LINK_DOWN,                   // Cable not connected
    POE_STATUS_CONNECTING,                  // Connecting (DHCP in progress)
    POE_STATUS_CONNECTED,                   // Connected with valid IP
    POE_STATUS_DHCP_FAILED,                 // DHCP failed
    POE_STATUS_STATIC_CONFIG_ERROR,         // Static IP config error
    POE_STATUS_IP_CONFLICT,                 // IP address conflict detected
    POE_STATUS_GATEWAY_UNREACHABLE,         // Gateway unreachable
    POE_STATUS_DNS_ERROR,                   // DNS resolution error
    POE_STATUS_ERROR,                       // General error
} poe_status_code_t;

/**
 * @brief PoE/Ethernet configuration for persistent storage
 */
typedef struct {
    // IP Mode
    poe_ip_mode_t ip_mode;                  // IP mode (DHCP or Static)
    
    // Static IP configuration
    uint8_t ip_addr[4];                     // IPv4 address
    uint8_t netmask[4];                     // Subnet mask
    uint8_t gateway[4];                     // Default gateway
    uint8_t dns_primary[4];                 // Primary DNS server
    uint8_t dns_secondary[4];               // Secondary DNS server
    char hostname[32];                      // Hostname
    
    // DHCP settings
    uint32_t dhcp_timeout_ms;               // DHCP timeout in milliseconds
    uint32_t dhcp_retry_count;              // DHCP retry count (0 = infinite)
    uint32_t dhcp_retry_interval_ms;        // DHCP retry interval in milliseconds
    
    // Recovery settings
    uint32_t power_recovery_delay_ms;       // Delay after PoE power recovery (default: 5000)
    aicam_bool_t auto_reconnect;            // Auto reconnect on link up
    aicam_bool_t persist_last_ip;           // Persist last DHCP IP for quick recovery
    uint8_t last_dhcp_ip[4];                // Last DHCP assigned IP (for quick recovery)
    
    // Validation settings
    aicam_bool_t validate_gateway;          // Validate gateway reachability
    aicam_bool_t detect_ip_conflict;        // Detect IP address conflicts
    
    // Status (runtime, not persisted)
    poe_status_code_t last_status;          // Last status code
    uint32_t last_error_time;               // Last error timestamp
    char last_error_msg[64];                // Last error message
} poe_config_persist_t;

/**
 * @brief Network service configuration structure
 */
typedef struct {
    uint32_t ap_sleep_time;                 // AP sleep time in seconds
    char ssid[32];                          // AP SSID
    char password[64];                      // AP password
    network_scan_result_t known_networks[16]; // Known network configuration
    uint32_t known_network_count;           // Known network count
    
    // Communication type settings
    uint32_t preferred_comm_type;           // Preferred communication type (0=None, 1=WiFi, 2=Cellular, 3=PoE)
    aicam_bool_t enable_auto_priority;      // Enable automatic priority-based switching
    
    // Cellular/4G settings
    cellular_config_persist_t cellular;     // Cellular configuration
    
    // PoE/Ethernet settings
    poe_config_persist_t poe;               // PoE configuration
} network_service_config_t;
 
 // Power mode configuration structure
 typedef struct {
     uint32_t current_mode;                 // Current power mode (0: low power, 1: full speed)
     uint32_t default_mode;                 // Default power mode
     uint32_t low_power_timeout_ms;         // Low power mode timeout in milliseconds
     uint64_t last_activity_time;           // Last activity timestamp
     uint32_t mode_switch_count;            // Mode switch counter
 } power_mode_config_t;

 // Log configuration structure
 typedef struct {
     uint32_t log_level;                      // Log level
     uint32_t log_file_size_kb;              // Log file size limit (KB)
     uint32_t log_file_count;                // Number of log files to keep
 } log_config_t;

 // MQTT configuration structure (Persistable - No Pointers)
/**
 * @brief MQTT base configuration for persistent storage
 * @note This structure contains no pointers and can be saved to NVS
 * @note Designed to map 1:1 with ms_mqtt_config_t structure
 */
typedef struct {
    // Basic connection (maps to ms_mqtt_config_t.base)
    uint8_t protocol_ver;                        // Protocol version (3 = 3.1, 4 = 3.1.1)
    char hostname[128];                          // Server hostname/IP
    uint16_t port;                               // Server port
    char client_id[64];                          // Client ID
    uint8_t clean_session;                       // Clean session flag
    uint16_t keepalive;                          // Keepalive interval (seconds)
    
    // Authentication (maps to ms_mqtt_config_t.authentication)
    char username[64];                           // Username
    char password[128];                          // Password
    
    // SSL/TLS configuration - CA certificate
    char ca_cert_path[128];                      // CA certificate file path (preferred)
    char ca_cert_data[128];                     // CA certificate data
    uint16_t ca_cert_len;                        // CA certificate length (0 = use strlen)
    
    // SSL/TLS configuration - Client certificate
    char client_cert_path[128];                  // Client certificate file path (preferred)
    char client_cert_data[128];                 // Client certificate data
    uint16_t client_cert_len;                    // Client certificate length (0 = use strlen)
    
    // SSL/TLS configuration - Client key
    char client_key_path[128];                   // Client key file path (preferred)
    char client_key_data[128];                  // Client key data
    uint16_t client_key_len;                     // Client key length (0 = use strlen)
    
    uint8_t verify_hostname;                     // Verify hostname in SSL
    
    // Last Will and Testament (maps to ms_mqtt_config_t.last_will)
    char lwt_topic[MAX_TOPIC_LENGTH];           // Last will topic
    char lwt_message[256];                       // Last will message
    uint16_t lwt_msg_len;                        // Last will message length (0 = use strlen)
    uint8_t lwt_qos;                            // Last will QoS (0-2)
    uint8_t lwt_retain;                         // Last will retain flag
    
    // Task parameters (maps to ms_mqtt_config_t.task)
    uint16_t task_priority;                      // Task priority
    uint32_t task_stack_size;                    // Task stack size
    
    // Network parameters (maps to ms_mqtt_config_t.network)
    uint8_t disable_auto_reconnect;              // Disable auto reconnect
    uint8_t outbox_limit;                        // Outbox limit
    uint16_t outbox_resend_interval_ms;          // Outbox resend interval (ms)
    uint16_t outbox_expired_timeout_ms;          // Outbox expired timeout (ms)
    uint16_t reconnect_interval_ms;              // Reconnect interval (ms)
    uint16_t timeout_ms;                         // Network timeout (ms)
    uint32_t buffer_size;                        // Default TX/RX buffer size
    uint32_t tx_buf_size;                        // TX buffer size (0 = use buffer_size)
    uint32_t rx_buf_size;                        // RX buffer size (0 = use buffer_size)
} mqtt_base_config_t;

/**
 * @brief Data report content mode
 * @note Selects what the capture/report flow publishes on the data report topic
 * @note Applies to the normal capture path; the Quick_Bootstrap fast-capture
 *       path builds its report independently and always sends the full report
 */
typedef enum {
    MQTT_REPORT_CONTENT_FULL = 0,           // Image + metadata + AI result
    MQTT_REPORT_CONTENT_METADATA_ONLY = 1,  // Metadata + AI result only, no image
} mqtt_report_content_t;

/**
 * @brief Extended MQTT service configuration
 * @note Combines base config with application-specific settings
 */
typedef struct {
    mqtt_base_config_t base_config;              // Persistable base configuration
    
    // Topic configuration
    char data_receive_topic[MAX_TOPIC_LENGTH];    // Data receive topic
    char data_report_topic[MAX_TOPIC_LENGTH];     // Data report topic
    char status_topic[MAX_TOPIC_LENGTH];          // Status topic
    char command_topic[MAX_TOPIC_LENGTH];         // Command topic
    
    // QoS configuration
    uint8_t data_receive_qos;                    // Data receive QoS (0-2)
    uint8_t data_report_qos;                     // Data report QoS (0-2)
    uint8_t status_qos;                          // Status QoS (0-2)
    uint8_t command_qos;                         // Command QoS (0-2)
    
    // Auto subscription
    aicam_bool_t auto_subscribe_receive;         // Auto subscribe to receive topic
    aicam_bool_t auto_subscribe_command;         // Auto subscribe to command topic
    
    // Message configuration
    aicam_bool_t enable_status_report;           // Enable status reporting
    uint32_t status_report_interval_ms;          // Status report interval (ms)
    aicam_bool_t enable_heartbeat;               // Enable heartbeat
    uint32_t heartbeat_interval_ms;              // Heartbeat interval (ms)
    uint8_t report_content;                      // Report content mode (mqtt_report_content_t)

    // Continuous AI telemetry
    aicam_bool_t telemetry_enabled;              // Publish AI results continuously
    char telemetry_topic[MAX_TOPIC_LENGTH];      // Telemetry topic
    uint8_t telemetry_qos;                       // Telemetry QoS (0-2)
} mqtt_service_config_t;

/** Stored in image_config_t.isp_mode — built-in profiles vs NVS-backed custom IQ. */
#define IMAGE_ISP_MODE_OUTDOOR  0u   /* default */
#define IMAGE_ISP_MODE_INDOOR   1u
#define IMAGE_ISP_MODE_CUSTOM   255u   /* 0xFF: use isp_config_t from NVS when valid */

//device service configuration structure
typedef struct {
    uint32_t brightness;                     // image brightness (0-100)
    uint32_t contrast;                       // image contrast (0-100)
    aicam_bool_t horizontal_flip;            // image horizontal flip
    aicam_bool_t vertical_flip;              // image vertical flip
    uint32_t isp_mode;                       // IMAGE_ISP_MODE_OUTDOOR(0) / INDOOR(1) / CUSTOM
    uint32_t aec;                            // image auto exposure control (0=manual, 1=auto)
    uint32_t startup_skip_frames;            // frames to skip on camera startup for stabilization (1-300)
    uint32_t fast_capture_skip_frames;       // frames to skip for fast capture (number of skipped frames for snapshot capture)
    uint32_t fast_capture_resolution;       // 0: 1280x720, 1: 1920x1080, 2: 2688x1520
    uint32_t fast_capture_jpeg_quality;     // fast capture JPEG encoding quality (0-100)
    aicam_bool_t capture_disable_comm;      // after capture: local storage only (no comm upload)
    aicam_bool_t capture_storage_ai;        // capture extra save draw AI result picture to storage switch
} image_config_t;

/**
 * @brief ISP (Image Signal Processor) configuration for persistent storage
 * @note Contains key ISP parameters that can be saved and restored
 * @note Matches ISP_IQParamTypeDef structure from STM32 ISP Library
 */

#define ISP_AWB_PROFILES_MAX        5    // ISP_AWB_COLORTEMP_REF
#define ISP_AWB_LABEL_MAX_LEN       32   // ISP_AWB_PROFILE_ID_MAX_LENGTH

typedef struct {
    aicam_bool_t valid;                      // Configuration validity flag

    // StatRemoval
    aicam_bool_t stat_removal_enable;
    uint32_t stat_removal_head_lines;        // 0-7
    uint32_t stat_removal_valid_lines;       // 0-4094

    // Demosaicing
    aicam_bool_t demosaic_enable;
    uint8_t demosaic_type;                   // Bayer pattern (0-4)
    uint8_t demosaic_peak;                   // 0-7
    uint8_t demosaic_line_v;                 // 0-7
    uint8_t demosaic_line_h;                 // 0-7
    uint8_t demosaic_edge;                   // 0-7

    // Contrast
    aicam_bool_t contrast_enable;
    uint32_t contrast_lut[9];                // LUM_0 to LUM_256 (0-394)

    // StatArea
    uint32_t stat_area_x;
    uint32_t stat_area_y;
    uint32_t stat_area_width;
    uint32_t stat_area_height;

    // SensorGain (static, when AEC disabled)
    uint32_t sensor_gain;                    // in mdB

    // SensorExposure (static, when AEC disabled)
    uint32_t sensor_exposure;                // in microseconds

    // BadPixelAlgo
    aicam_bool_t bad_pixel_algo_enable;
    uint32_t bad_pixel_algo_threshold;

    // BadPixelStatic
    aicam_bool_t bad_pixel_enable;
    uint8_t bad_pixel_strength;              // 0-7

    // BlackLevel
    aicam_bool_t black_level_enable;
    uint8_t black_level_r;                   // 0-255
    uint8_t black_level_g;                   // 0-255
    uint8_t black_level_b;                   // 0-255

    // AEC Algo
    aicam_bool_t aec_enable;
    int32_t aec_exposure_compensation;       // -4 to +4 EV
    uint32_t aec_anti_flicker_freq;          // 0, 50, 60

    // AWB Algo (complete 5 profiles)
    aicam_bool_t awb_enable;
    char awb_label[ISP_AWB_PROFILES_MAX][ISP_AWB_LABEL_MAX_LEN];
    uint32_t awb_ref_color_temp[ISP_AWB_PROFILES_MAX];
    uint32_t awb_gain_r[ISP_AWB_PROFILES_MAX];
    uint32_t awb_gain_g[ISP_AWB_PROFILES_MAX];
    uint32_t awb_gain_b[ISP_AWB_PROFILES_MAX];
    int32_t awb_ccm[ISP_AWB_PROFILES_MAX][3][3];  // Color Conversion Matrix
    uint8_t awb_ref_rgb[ISP_AWB_PROFILES_MAX][3];

    // ISP Gain (static, when AWB disabled)
    aicam_bool_t isp_gain_enable;
    uint32_t isp_gain_r;                     // 100000000 = 1.0x
    uint32_t isp_gain_g;
    uint32_t isp_gain_b;

    // Color Conversion (static, when AWB disabled)
    aicam_bool_t color_conv_enable;
    int32_t color_conv_matrix[3][3];         // 100000000 = 1.0x

    // Gamma
    aicam_bool_t gamma_enable;

    // Sensor Delay
    uint8_t sensor_delay;                    // frames

    // Lux Reference (for lux estimation calibration)
    uint32_t lux_hl_ref;                     // High lux reference
    uint32_t lux_hl_expo1;
    uint8_t lux_hl_lum1;
    uint32_t lux_hl_expo2;
    uint8_t lux_hl_lum2;
    uint32_t lux_ll_ref;                     // Low lux reference
    uint32_t lux_ll_expo1;
    uint8_t lux_ll_lum1;
    uint32_t lux_ll_expo2;
    uint8_t lux_ll_lum2;
    float lux_calib_factor;
} isp_config_t;

/**
 * @brief Light working modes
 */
typedef enum {
    LIGHT_MODE_OFF = 0,                      // light off
    LIGHT_MODE_ON,                           // light on
    LIGHT_MODE_AUTO,                         // auto light
    LIGHT_MODE_CUSTOM                        // custom light
} light_mode_t;

/**
 * @brief Light management configuration
 */
typedef struct {
    aicam_bool_t connected;                  // light connected status
    light_mode_t mode;                       // work mode
    uint32_t start_hour;                     // custom mode start time (hour)
    uint32_t start_minute;                   // custom mode start time (minute)
    uint32_t end_hour;                       // custom mode end time (hour)
    uint32_t end_minute;                     // custom mode end time (minute)
    uint32_t brightness_level;               // brightness level (0-100)
    aicam_bool_t auto_trigger_enabled;       // auto trigger enabled
    uint32_t light_threshold;                // light threshold
} light_config_t;

typedef struct {
    image_config_t image_config;
    light_config_t light_config;
    isp_config_t isp_config;
} device_service_config_t;

typedef struct {
    uint32_t session_timeout_ms;                       // Session timeout in milliseconds
    aicam_bool_t enable_session_timeout;               // Enable session timeout
    char admin_password[64];                           // Admin password (default: "hicamthink")
} auth_mgr_config_t;

/* ==================== Webhook Configuration ==================== */

#define WEBHOOK_URL_MAX_LEN     256
#define WEBHOOK_SECRET_MAX_LEN  128

typedef struct {
    aicam_bool_t enable;                          // Webhook enable switch
    char url[WEBHOOK_URL_MAX_LEN];                // HTTP(S) push URL
    char auth_type[16];                           // "none" | "bearer" | "basic" | "custom"
    char secret[WEBHOOK_SECRET_MAX_LEN];          // Auth token/credentials
} webhook_config_t;

// RTMP config is now part of video_stream_mode_config_t
// These macros are kept for compatibility
#define RTMP_CONFIG_MAX_URL_LENGTH         256
#define RTMP_CONFIG_MAX_STREAM_KEY_LENGTH  128
 
 // Global configuration structure 
 typedef struct {
     uint32_t config_version;               // Configuration version number
     uint32_t magic_number;                 // Magic number for configuration validity verification
     uint32_t checksum;                     // Configuration checksum
     uint64_t timestamp;                    // Configuration timestamp
     
    log_config_t log_config;
    ai_debug_config_t ai_debug;
    work_mode_config_t work_mode_config;
    power_mode_config_t power_mode_config; // Power mode configuration
    device_info_config_t device_info;
    device_service_config_t device_service;
    network_service_config_t network_service;
    mqtt_service_config_t mqtt_service;
    auth_mgr_config_t auth_mgr;
    webhook_config_t webhook_config;
    // RTMP config is now in work_mode_config.video_stream_mode
 } aicam_global_config_t;
 
 /* ==================== JSON Configuration Manager Parameter Definitions ==================== */
 
 #define JSON_CONFIG_FILE_PATH_PRIMARY    "/config/aicam_config.json"
 #define JSON_CONFIG_FILE_PATH_BACKUP     "/config/aicam_config_backup.json"
 #define JSON_CONFIG_FILE_PATH_DEFAULT    "/config/aicam_config_default.json"
 
 #define JSON_CONFIG_MAX_FILE_SIZE        (32 * 1024)    // 32KB max configuration file size
 #define JSON_CONFIG_MAX_BUFFER_SIZE      (16 * 1024)    // 16KB JSON buffer size
 #define JSON_CONFIG_MAX_KEY_LENGTH       128             // Maximum key name length
 #define JSON_CONFIG_MAX_VALUE_LENGTH     512             // Maximum value length
 
 #define JSON_CONFIG_VERSION_CURRENT      1
 #define JSON_CONFIG_MAGIC_NUMBER         0x41494341      // "AICA"
 
 /* ==================== JSON Configuration Manager Status and Options ==================== */
 
 typedef struct {
     aicam_bool_t validate_json_syntax;      // Validate JSON syntax
     aicam_bool_t validate_data_types;       // Validate data types
     aicam_bool_t validate_value_ranges;     // Validate value ranges
     aicam_bool_t validate_checksum;         // Validate checksum
     aicam_bool_t strict_mode;               // Strict mode
 } json_config_validation_options_t;
 
 /* ==================== JSON Configuration Manager API Interface ==================== */
 
 /**
  * @brief Initialize JSON configuration manager
  * @details Initialize storage interface, load default configuration, create necessary directory structure
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_mgr_init(void);
 
 /**
  * @brief Deinitialize JSON configuration manager
  * @details Save current configuration, release resources
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_mgr_deinit(void);
 
 /**
  * @brief Load JSON configuration from file
  * @param file_path Configuration file path, NULL means use default path
  * @param config Output configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_load_from_file(const char *file_path, aicam_global_config_t *config);
 
 /**
  * @brief Save configuration to JSON file
  * @param file_path Configuration file path, NULL means use default path
  * @param config Configuration structure pointer to save
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_save_to_file(const char *file_path, aicam_global_config_t *config);
 
 /**
  * @brief Parse configuration from JSON string
  * @param json_string JSON string
  * @param config Output configuration structure pointer
  * @param validation_options Validation options, NULL means use default validation
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_parse_from_string(const char *json_string, 
                                              aicam_global_config_t *config,
                                              const json_config_validation_options_t *validation_options);
 
 /**
  * @brief Serialize configuration to JSON string
  * @param config Configuration structure pointer
  * @param json_buffer Output JSON string buffer
  * @param buffer_size Buffer size
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_serialize_to_string(const aicam_global_config_t *config,
                                                char *json_buffer,
                                                size_t buffer_size);
 
 /**
  * @brief Load default configuration
  * @param config Output configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_load_default(aicam_global_config_t *config);
 
 /**
  * @brief Validate configuration data validity
  * @param config Configuration structure pointer to validate
  * @param validation_options Validation options
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_validate(const aicam_global_config_t *config,
                                     const json_config_validation_options_t *validation_options);
 
 /**
  * @brief Calculate configuration checksum
  * @param config Configuration structure pointer
  * @param checksum Output checksum
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_calculate_checksum(const aicam_global_config_t *config, uint32_t *checksum);
 
 /**
  * @brief Create configuration backup
  * @param source_path Source configuration file path, NULL means use default path
  * @param backup_path Backup file path, NULL means use default backup path
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_create_backup(const char *source_path, const char *backup_path);
 
 /**
  * @brief Restore configuration from backup
  * @param backup_path Backup file path, NULL means use default backup path
  * @param target_path Target configuration file path, NULL means use default path
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_restore_from_backup(const char *backup_path, const char *target_path);
 
 /**
  * @brief Reset configuration to default values
  * @param file_path Configuration file path, NULL means use default path
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_reset_to_default(const char *file_path);
 
 /**
  * @brief Get configuration
  * @param config Configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_get_config(aicam_global_config_t *config);
 
 /**
  * @brief Set configuration
  * @param config Configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_set_config(aicam_global_config_t *config);

 /**
  * @brief Get log configuration
  * @param log_config Log configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_get_log_config(log_config_t *log_config);

 /**
  * @brief Set log configuration
  * @param log_config Log configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_set_log_config(log_config_t *log_config);

 /**
  * @brief Get work mode configuration
  * @param config Configuration structure pointer
  * @param work_mode_config Work mode configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_get_work_mode_config(work_mode_config_t *work_mode_config);
 
 /**
  * @brief Set work mode configuration
  * @param config Configuration structure pointer
  * @param work_mode_config Work mode configuration structure pointer
  * @return aicam_result_t Operation result
  */
 aicam_result_t json_config_set_work_mode_config(work_mode_config_t *work_mode_config);

/**
 * @brief Get device info configuration
 * @param device_info_config Device info configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_device_info_config(device_info_config_t *device_info_config);

/**
 * @brief Set device info configuration
 * @param device_info_config Device info configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_device_info_config(device_info_config_t *device_info_config);

/**
 * @brief Update device MAC address and generate device name if needed
 * @param mac_address MAC address string (format: "XX:XX:XX:XX:XX:XX")
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_update_device_mac_address(const char *mac_address);

/**
 * @brief Get network service configuration
 * @param network_service_config Network service configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_network_service_config(network_service_config_t *network_service_config);

/**
 * @brief Set network service configuration
 * @param network_service_config Network service configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_network_service_config(network_service_config_t *network_service_config);

/**
 * @brief Get device admin password
 * @param password_buffer Output buffer for password
 * @param buffer_size Size of password buffer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_device_password(char *password_buffer, size_t buffer_size);

/**
 * @brief Set device admin password
 * @param password New admin password
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_device_password(const char *password);

/**
 * @brief Get AI_1 active status
 * @return aicam_bool_t AI_1 active status
 */
aicam_bool_t json_config_get_ai_1_active(void);

/**
 * @brief Set AI_1 active status
 * @param ai_1_active AI_1 active status
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_ai_1_active(aicam_bool_t ai_1_active);

/**
 * @brief Persist AI pipe input size to NVS when it differs from stored values
 * @param input_width Model input width (must be non-zero)
 * @param input_height Model input height (must be non-zero)
 * @return aicam_result_t AICAM_OK if already matching or write succeeded
 */
aicam_result_t json_config_sync_ai_pipe_nvs_from_input_size(uint32_t input_width, uint32_t input_height);

/* ==================== Power Mode Configuration API ==================== */

/**
 * @brief Get power mode configuration
 * @param config Pointer to store power mode configuration
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_power_mode_config(power_mode_config_t *config);

/**
 * @brief Set power mode configuration
 * @param config Power mode configuration to set
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_power_mode_config(const power_mode_config_t *config);

/**
 * @brief Set confidence threshold
 * @param confidence_threshold Confidence threshold
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_confidence_threshold(uint32_t confidence_threshold);

/**
 * @brief Set NMS threshold
 * @param nms_threshold NMS threshold
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_nms_threshold(uint32_t nms_threshold);

/**
 * @brief Get confidence threshold
 * @return confidence threshold
 */
uint32_t json_config_get_confidence_threshold(void);

/**
 * @brief Get NMS threshold
 * @return NMS threshold
 */
uint32_t json_config_get_nms_threshold(void);

/**
 * @brief Set AI overlay results flag
 * @param overlay_results Draw AI results onto encoded frames
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_overlay_results(aicam_bool_t overlay_results);

/**
 * @brief Get AI overlay results flag
 * @return AI overlay results flag
 */
aicam_bool_t json_config_get_overlay_results(void);

/**
 * @brief Set AI inference pacing interval
 * @param interval_ms Interval in milliseconds (0 = process every frame)
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_inference_interval_ms(uint32_t interval_ms);

/**
 * @brief Get AI inference pacing interval
 * @return Interval in milliseconds (0 = process every frame)
 */
uint32_t json_config_get_inference_interval_ms(void);


/**
 * @brief Get mqtt service configuration
 * @param mqtt_service_config Mqtt service configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_mqtt_service_config(mqtt_service_config_t *mqtt_service_config);

/**
 * @brief Set mqtt service configuration
 * @param mqtt_service_config Mqtt service configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_mqtt_service_config(const mqtt_service_config_t *mqtt_service_config);

/**
 * @brief Get device service image configuration
 * @param image_config Image configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_device_service_image_config(image_config_t *image_config);

/**
 * @brief Set device service image configuration
 * @param image_config Image configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_device_service_image_config(const image_config_t *image_config);

/**
 * @brief Get device service light configuration
 * @param light_config Light configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_device_service_light_config(light_config_t *light_config);

/**
 * @brief Set device service light configuration
 * @param light_config Light configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_device_service_light_config(const light_config_t *light_config);

/**
 * @brief Get ISP configuration
 * @param isp_config ISP configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_isp_config(isp_config_t *isp_config);

/**
 * @brief Set ISP configuration
 * @param isp_config ISP configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_isp_config(const isp_config_t *isp_config);

/**
 * @brief Convert ISP parameter to ISP configuration
 * @param isp_param ISP parameter structure pointer
 * @param isp_config ISP configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_isp_param_to_config(ISP_IQParamTypeDef *isp_param, isp_config_t *isp_config);

/**
 * @brief Convert ISP configuration to ISP parameter
 * @param isp_config ISP configuration structure pointer
 * @param isp_param ISP parameter structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_config_to_isp_param(isp_config_t *isp_config, ISP_IQParamTypeDef *isp_param);

/* ==================== PoE Configuration API ==================== */

/**
 * @brief Get PoE configuration
 * @param poe_config PoE configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_poe_config(poe_config_persist_t *poe_config);

/**
 * @brief Set PoE configuration
 * @param poe_config PoE configuration structure pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_poe_config(const poe_config_persist_t *poe_config);

/**
 * @brief Get PoE IP mode
 * @return poe_ip_mode_t IP mode
 */
poe_ip_mode_t json_config_get_poe_ip_mode(void);

/**
 * @brief Set PoE IP mode
 * @param mode IP mode
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_poe_ip_mode(poe_ip_mode_t mode);

/**
 * @brief Save PoE last DHCP IP for quick recovery
 * @param ip_addr IP address (4 bytes)
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_save_poe_last_dhcp_ip(const uint8_t *ip_addr);

/**
 * @brief Get PoE status code string
 * @param status Status code
 * @return const char* Status string
 */
const char* poe_status_code_to_string(poe_status_code_t status);

/* ==================== Video Stream Mode Configuration API ==================== */

/**
 * @brief Get video stream mode configuration (includes RTMP)
 * @param config Video stream mode configuration pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_get_video_stream_mode(video_stream_mode_config_t *config);

/**
 * @brief Set video stream mode configuration (includes RTMP)
 * @param config Video stream mode configuration pointer
 * @return aicam_result_t Operation result
 */
aicam_result_t json_config_set_video_stream_mode(const video_stream_mode_config_t *config);

/**
 * @brief Get webhook configuration
 */
aicam_result_t json_config_get_webhook_config(webhook_config_t *config);

/**
 * @brief Set webhook configuration
 */
aicam_result_t json_config_set_webhook_config(const webhook_config_t *config);

/**
 * @brief Get webhook custom CA certificate (from LittleFS file)
 * @param cert_data Output buffer (caller must free with buffer_free). NULL if no cert.
 * @param cert_len Output length (0 if no cert)
 */
aicam_result_t json_config_get_webhook_ca_cert(char **cert_data, size_t *cert_len);

/**
 * @brief Save webhook custom CA certificate to LittleFS, store path in NVS
 * @param cert_data PEM data
 * @param cert_len Length of PEM data
 */
aicam_result_t json_config_set_webhook_ca_cert(const char *cert_data, size_t cert_len);

/**
 * @brief Delete webhook custom CA certificate (file + NVS path)
 */
aicam_result_t json_config_delete_webhook_ca_cert(void);

 /* ==================== Convenient Access Macro Definitions ==================== */
 
// Macros for quick access to debug configuration
#define JSON_CONFIG_GET_AI_ENABLE(config)      ((config)->ai_debug.ai_enabled)
#define JSON_CONFIG_GET_AI_1_ACTIVE(config)    ((config)->ai_debug.ai_1_active)
#define JSON_CONFIG_GET_CONFIDENCE(config)     ((config)->ai_debug.confidence_threshold)
#define JSON_CONFIG_GET_NMS_THRESHOLD(config)  ((config)->ai_debug.nms_threshold)
#define JSON_CONFIG_GET_OVERLAY_RESULTS(config) ((config)->ai_debug.overlay_results)
#define JSON_CONFIG_GET_INFER_INTERVAL(config) ((config)->ai_debug.inference_interval_ms)

// Macros for quick access to power mode configuration
#define JSON_CONFIG_GET_POWER_MODE(config)     ((config)->power_mode_config.current_mode)
#define JSON_CONFIG_GET_DEFAULT_POWER_MODE(config) ((config)->power_mode_config.default_mode)
#define JSON_CONFIG_GET_POWER_TIMEOUT(config)  ((config)->power_mode_config.low_power_timeout_ms)
#define JSON_CONFIG_GET_LAST_ACTIVITY(config)  ((config)->power_mode_config.last_activity_time)
#define JSON_CONFIG_GET_MODE_SWITCH_COUNT(config) ((config)->power_mode_config.mode_switch_count)

 // Macros for quick access to device info configuration
 #define JSON_CONFIG_GET_DEVICE_INFO_NAME(config)    ((config)->device_info.device_name)
 #define JSON_CONFIG_GET_DEVICE_INFO_FW_VER(config)  ((config)->device_info.software_version) // Corrected from firmware_version
 #define JSON_CONFIG_GET_DEVICE_INFO_HW_VER(config)  ((config)->device_info.hardware_version)
 #define JSON_CONFIG_GET_DEVICE_INFO_SERIAL(config)  ((config)->device_info.serial_number)
 
 // Macros for quick access to work mode configuration
 #define JSON_CONFIG_GET_WORK_MODE(config)      ((config)->work_mode_config.work_mode)
 // (FIXED) PIR trigger is a direct child of work_mode_config
 #define JSON_CONFIG_GET_PIR_ENABLE(config)     ((config)->work_mode_config.pir_trigger.enable)

 // Macros for quick access to IO trigger array
 // (FIXED) IO trigger is a direct child of work_mode_config
 #define JSON_CONFIG_GET_IO_TRIGGER_ENABLE(config, index)     ((config)->work_mode_config.io_trigger[index].enable)
 #define JSON_CONFIG_GET_IO_TRIGGER_PIN(config, index)        ((config)->work_config.io_trigger[index].pin_number)
 #define JSON_CONFIG_GET_IO_TRIGGER_INPUT_ENABLE(config, index) ((config)->work_mode_config.io_trigger[index].input_enable)
 #define JSON_CONFIG_GET_IO_TRIGGER_OUTPUT_ENABLE(config, index) ((config)->work_mode_config.io_trigger[index].output_enable)
 #define JSON_CONFIG_GET_IO_TRIGGER_INPUT_TYPE(config, index) ((config)->work_mode_config.io_trigger[index].input_trigger_type)
 #define JSON_CONFIG_GET_IO_TRIGGER_OUTPUT_TYPE(config, index) ((config)->work_mode_config.io_trigger[index].output_trigger_type)
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif // JSON_CONFIG_MGR_H