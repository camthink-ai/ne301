/**
 * @file json_config_internal.h
 * @brief Internal definitions for the JSON Configuration Management System
 * @details This header is for internal module use only.
 */

 #ifndef JSON_CONFIG_INTERNAL_H
 #define JSON_CONFIG_INTERNAL_H
 
 #include "json_config_mgr.h" // Include the public API
 #include "cJSON.h"           // Include the cJSON library
 #include "debug.h"
 #include "system_service.h"

 #include "storage.h"         // For NVS access
 #include <string.h>
 #include <stdlib.h>
 #include <stdio.h>
 #include <time.h>
 #include "drtc.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* ==================== Internal Data Structures ==================== */
 
 typedef struct {
     aicam_bool_t initialized;
     aicam_global_config_t current_config;
     uint32_t save_count;
     uint64_t last_save_time;
 } json_config_mgr_context_t;
 
 /* ==================== Global Context (defined in json_config_mgr.c) ==================== */
 
 extern json_config_mgr_context_t g_json_config_ctx;
 extern const aicam_global_config_t default_config;
 
 /* ==================== NVS Key Name Definitions ==================== */
 
 // ... (Wszystkie definicje NVS_KEY_* pozostają takie same jak w poprzednim kroku) ...
 // NVS key names for configuration structure fields
 #define NVS_KEY_CONFIG_VERSION      "cfg_ver"
 #define NVS_KEY_MAGIC_NUMBER        "cfg_magic"
 #define NVS_KEY_CHECKSUM            "cfg_csum"
 #define NVS_KEY_TIMESTAMP           "cfg_time"
 
 // Log configuration key names (from log_config)
 #define NVS_KEY_LOG_LEVEL           "log_level"
 #define NVS_KEY_LOG_FILE_SIZE       "log_size"
 #define NVS_KEY_LOG_FILE_COUNT      "log_count"
 
 // AI Debug configuration key names
 #define NVS_KEY_AI_ENABLE           "ai_enabled"
 #define NVS_KEY_AI_1_ACTIVE         "ai_1_active"
 /* AI pipeline input resolution (stored in NVS). If reading fails or the value is invalid/placeholder,
  * do not guess a resolution. Wait until the model is loaded and obtain the input size from
  * nn_model_info_t (or an equivalent interface) before configuring the AI pipeline. */
 #define NVS_KEY_AI_PIPE_WIDTH       "ai_pipe_width"
 #define NVS_KEY_AI_PIPE_HEIGHT      "ai_pipe_height"
 #define NVS_KEY_CONFIDENCE          "confidence"
 #define NVS_KEY_NMS_THRESHOLD       "nms_thresh"
 #define NVS_KEY_OVERLAY_RESULTS     "overlay_results"
 #define NVS_KEY_INFER_INTERVAL      "ai_infer_iv"
 
 // Power mode configuration key names
 #define NVS_KEY_POWER_CURRENT_MODE  "power_cur_mode"
 #define NVS_KEY_POWER_DEFAULT_MODE  "power_def_mode"
 #define NVS_KEY_POWER_TIMEOUT       "power_timeout"
 #define NVS_KEY_POWER_LAST_ACTIVITY "power_last_act"
 #define NVS_KEY_POWER_SWITCH_COUNT  "power_switch_cnt"
 
 // Device info configuration key names
 #define NVS_KEY_DEVICE_INFO_NAME        "dev_info_name"
 #define NVS_KEY_DEVICE_INFO_MAC         "dev_info_mac"
 #define NVS_KEY_DEVICE_INFO_SERIAL      "dev_info_serial"
 #define NVS_KEY_DEVICE_INFO_HW_VER      "dev_info_hw_ver"
 #define NVS_KEY_DEVICE_INFO_FW_VER      "dev_info_fw_ver"
 #define NVS_KEY_DEVICE_INFO_CAMERA      "dev_info_camera"
 #define NVS_KEY_DEVICE_INFO_EXTENSION   "dev_info_ext"
 #define NVS_KEY_DEVICE_INFO_STORAGE     "dev_info_storage"
 #define NVS_KEY_DEVICE_INFO_STORAGE_PCT "dev_info_stor_pct"
 #define NVS_KEY_DEVICE_INFO_POWER       "dev_info_power"
 #define NVS_KEY_DEVICE_INFO_BATTERY_PCT "dev_info_bat_pct"
#define NVS_KEY_DEVICE_INFO_COMM        "dev_info_comm"
#define NVS_KEY_DEVICE_INFO_PASSWORD    "dev_info_password"

// Auth manager configuration key names
#define NVS_KEY_AUTH_SESSION_TIMEOUT    "auth_sess_to"
#define NVS_KEY_AUTH_ENABLE_TIMEOUT     "auth_en_to"
#define NVS_KEY_AUTH_PASSWORD           "auth_password"
 
 // Device service configuration key names
 #define NVS_KEY_IMAGE_BRIGHTNESS        "img_bright"
 #define NVS_KEY_IMAGE_CONTRAST          "img_contrast"
 #define NVS_KEY_IMAGE_HFLIP             "img_hflip"
 #define NVS_KEY_IMAGE_VFLIP             "img_vflip"
 #define NVS_KEY_IMAGE_AEC               "img_aec"
 #define NVS_KEY_IMAGE_ISP_MODE          "img_isp_mode"
 #define NVS_KEY_IMAGE_GRAYSCALE         "img_gray"
 #define NVS_KEY_IMAGE_SKIP_FRAMES       "img_skip"
 #define NVS_KEY_IMAGE_FAST_SKIP_FRAMES  "img_fast_skip"
 #define NVS_KEY_IMAGE_FAST_RESOLUTION   "img_fast_res"
 #define NVS_KEY_IMAGE_FAST_JPEG_QUALITY "img_fast_jq"
 #define NVS_KEY_CAPTURE_DISABLE_COMM    "capture_un_comm"
 #define NVS_KEY_CAPTURE_STORAGE_AI      "capture_stor_ai"
 #define NVS_KEY_CAPTURE_QUICK_MODE      "capture_qmode"
 #define NVS_KEY_LIGHT_CONNECTED         "light_conn"
 #define NVS_KEY_LIGHT_MODE              "light_mode"
 #define NVS_KEY_LIGHT_START_HOUR        "light_s_h"
 #define NVS_KEY_LIGHT_START_MIN         "light_s_m"
 #define NVS_KEY_LIGHT_END_HOUR          "light_e_h"
 #define NVS_KEY_LIGHT_END_MIN           "light_e_m"
 #define NVS_KEY_LIGHT_BRIGHTNESS        "light_brt"
 #define NVS_KEY_LIGHT_AUTO_TRIGGER      "light_auto"
 #define NVS_KEY_LIGHT_THRESHOLD         "light_thr"

// ISP configuration key names
#define NVS_KEY_ISP_VALID               "isp_valid"
// StatRemoval
#define NVS_KEY_ISP_SR_ENABLE           "isp_sr_en"
#define NVS_KEY_ISP_SR_HEADLINES        "isp_sr_hl"
#define NVS_KEY_ISP_SR_VALIDLINES       "isp_sr_vl"
// Demosaicing
#define NVS_KEY_ISP_DEMO_ENABLE         "isp_dm_en"
#define NVS_KEY_ISP_DEMO_TYPE           "isp_dm_type"
#define NVS_KEY_ISP_DEMO_PEAK           "isp_dm_peak"
#define NVS_KEY_ISP_DEMO_LINEV          "isp_dm_lv"
#define NVS_KEY_ISP_DEMO_LINEH          "isp_dm_lh"
#define NVS_KEY_ISP_DEMO_EDGE           "isp_dm_edge"
// Contrast
#define NVS_KEY_ISP_CONTRAST_ENABLE     "isp_ctr_en"
#define NVS_KEY_ISP_CONTRAST_LUT        "isp_ctr_lut"
// StatArea
#define NVS_KEY_ISP_STAT_X              "isp_stat_x"
#define NVS_KEY_ISP_STAT_Y              "isp_stat_y"
#define NVS_KEY_ISP_STAT_W              "isp_stat_w"
#define NVS_KEY_ISP_STAT_H              "isp_stat_h"
// Sensor Gain/Exposure
#define NVS_KEY_ISP_SENSOR_GAIN         "isp_s_gain"
#define NVS_KEY_ISP_SENSOR_EXPO         "isp_s_expo"
// BadPixel
#define NVS_KEY_ISP_BPA_ENABLE          "isp_bpa_en"
#define NVS_KEY_ISP_BPA_THRESH          "isp_bpa_th"
#define NVS_KEY_ISP_BP_ENABLE           "isp_bp_en"
#define NVS_KEY_ISP_BP_STRENGTH         "isp_bp_str"
// BlackLevel
#define NVS_KEY_ISP_BL_ENABLE           "isp_bl_en"
#define NVS_KEY_ISP_BL_R                "isp_bl_r"
#define NVS_KEY_ISP_BL_G                "isp_bl_g"
#define NVS_KEY_ISP_BL_B                "isp_bl_b"
// AEC
#define NVS_KEY_ISP_AEC_ENABLE          "isp_aec_en"
#define NVS_KEY_ISP_AEC_EXPCOMP         "isp_aec_ec"
#define NVS_KEY_ISP_AEC_AFLK            "isp_aec_af"
// AWB (stored as binary blob due to size)
#define NVS_KEY_ISP_AWB_ENABLE          "isp_awb_en"
#define NVS_KEY_ISP_AWB_DATA            "isp_awb_dat"
// ISP Gain
#define NVS_KEY_ISP_GAIN_ENABLE         "isp_g_en"
#define NVS_KEY_ISP_GAIN_R              "isp_g_r"
#define NVS_KEY_ISP_GAIN_G              "isp_g_g"
#define NVS_KEY_ISP_GAIN_B              "isp_g_b"
// Color Conversion
#define NVS_KEY_ISP_CCM_ENABLE          "isp_ccm_en"
#define NVS_KEY_ISP_CCM_DATA            "isp_ccm_dat"
// Gamma
#define NVS_KEY_ISP_GAMMA_ENABLE        "isp_gamma_en"
// Sensor Delay
#define NVS_KEY_ISP_SENSOR_DELAY        "isp_s_dly"
// Lux Reference
#define NVS_KEY_ISP_LUX_DATA            "isp_lux_dat"

// Network service configuration key names
#define NVS_KEY_NETWORK_AP_SLEEP_TIME   "net_ap_sleep"
#define NVS_KEY_NETWORK_SSID            "net_ssid"
#define NVS_KEY_NETWORK_PASSWORD        "net_password"
#define NVS_KEY_NETWORK_KNOWN_COUNT     "net_known_cnt"
// Note: Individual known network entries use format "net_<idx>_<field>"
// where <idx> is 0-15 and <field> is ssid/bssid/pwd/rssi/ch/sec/conn/known/time

// Wi-Fi HaLow last-connected info
#define NVS_KEY_HALOW_SSID              "hw_ssid"
#define NVS_KEY_HALOW_PASSWORD          "hw_pwd"
#define NVS_KEY_HALOW_SECURITY          "hw_sec"
#define NVS_KEY_HALOW_COUNTRY_CODE      "hw_cc"
#define NVS_KEY_HALOW_BSSID              "hw_bssid"
#define NVS_KEY_HALOW_IP_MODE            "hw_ip_mode"
#define NVS_KEY_HALOW_IP_ADDR            "hw_ip"
#define NVS_KEY_HALOW_NETMASK            "hw_mask"
#define NVS_KEY_HALOW_GATEWAY            "hw_gw"
#define NVS_KEY_HALOW_TX_POWER           "hw_txpwr"
#define NVS_KEY_HALOW_SCAN_DWELL         "hw_scan_dw"
#define NVS_KEY_HALOW_RC_MCS             "hw_rc_mcs"
#define NVS_KEY_HALOW_RC_BW              "hw_rc_bw"
#define NVS_KEY_HALOW_RC_GI              "hw_rc_gi"

// Communication type configuration key names
#define NVS_KEY_COMM_PREFERRED_TYPE     "comm_pref"
#define NVS_KEY_COMM_AUTO_PRIORITY      "comm_auto_pri"

// Cellular/4G configuration key names
#define NVS_KEY_CELLULAR_APN            "cell_apn"
#define NVS_KEY_CELLULAR_USERNAME       "cell_user"
#define NVS_KEY_CELLULAR_PASSWORD       "cell_pass"
#define NVS_KEY_CELLULAR_PIN            "cell_pin"
#define NVS_KEY_CELLULAR_AUTH           "cell_auth"
#define NVS_KEY_CELLULAR_ROAMING        "cell_roam"
#define NVS_KEY_CELLULAR_OPERATOR       "cell_operator"
#define NVS_KEY_CELLULAR_PLMN           "cell_plmn"

// PoE/Ethernet configuration key names
#define NVS_KEY_POE_IP_MODE             "poe_ip_mode"
#define NVS_KEY_POE_IP_ADDR             "poe_ip"
#define NVS_KEY_POE_NETMASK             "poe_mask"
#define NVS_KEY_POE_GATEWAY             "poe_gw"
#define NVS_KEY_POE_DNS_PRIMARY         "poe_dns1"
#define NVS_KEY_POE_DNS_SECONDARY       "poe_dns2"
#define NVS_KEY_POE_HOSTNAME            "poe_host"
#define NVS_KEY_POE_DHCP_TIMEOUT        "poe_dhcp_to"
#define NVS_KEY_POE_DHCP_RETRY_COUNT    "poe_dhcp_cnt"
#define NVS_KEY_POE_DHCP_RETRY_INTERVAL "poe_dhcp_iv"
#define NVS_KEY_POE_RECOVERY_DELAY      "poe_rec_dly"
#define NVS_KEY_POE_AUTO_RECONNECT      "poe_auto_rcn"
#define NVS_KEY_POE_PERSIST_LAST_IP     "poe_persist"
#define NVS_KEY_POE_LAST_DHCP_IP        "poe_last_ip"
#define NVS_KEY_POE_VALIDATE_GATEWAY    "poe_val_gw"
#define NVS_KEY_POE_DETECT_CONFLICT     "poe_det_conf"

// MQTT service configuration key names
// Basic connection
#define NVS_KEY_MQTT_PROTOCOL_VER       "mqtt_proto"
#define NVS_KEY_MQTT_HOST               "mqtt_host"
#define NVS_KEY_MQTT_PORT               "mqtt_port"
#define NVS_KEY_MQTT_CLIENT_ID          "mqtt_cid"
#define NVS_KEY_MQTT_CLEAN_SESSION      "mqtt_clean"
#define NVS_KEY_MQTT_KEEPALIVE          "mqtt_ka"

// Authentication
#define NVS_KEY_MQTT_USERNAME           "mqtt_user"
#define NVS_KEY_MQTT_PASSWORD           "mqtt_pass"

// SSL/TLS - CA certificate
#define NVS_KEY_MQTT_CA_CERT_PATH       "mqtt_ca_path"
#define NVS_KEY_MQTT_CA_CERT_DATA       "mqtt_ca_data"
#define NVS_KEY_MQTT_CA_CERT_LEN        "mqtt_ca_len"

// SSL/TLS - Client certificate
#define NVS_KEY_MQTT_CLIENT_CERT_PATH   "mqtt_crt_path"
#define NVS_KEY_MQTT_CLIENT_CERT_DATA   "mqtt_crt_data"
#define NVS_KEY_MQTT_CLIENT_CERT_LEN    "mqtt_crt_len"

// SSL/TLS - Client key
#define NVS_KEY_MQTT_CLIENT_KEY_PATH    "mqtt_key_path"
#define NVS_KEY_MQTT_CLIENT_KEY_DATA    "mqtt_key_data"
#define NVS_KEY_MQTT_CLIENT_KEY_LEN     "mqtt_key_len"

// SSL/TLS - Settings
#define NVS_KEY_MQTT_VERIFY_HOSTNAME    "mqtt_verify"

// Last Will and Testament
#define NVS_KEY_MQTT_LWT_TOPIC          "mqtt_lwt_t"
#define NVS_KEY_MQTT_LWT_MESSAGE        "mqtt_lwt_m"
#define NVS_KEY_MQTT_LWT_MSG_LEN        "mqtt_lwt_len"
#define NVS_KEY_MQTT_LWT_QOS            "mqtt_lwt_q"
#define NVS_KEY_MQTT_LWT_RETAIN         "mqtt_lwt_r"

// Task parameters
#define NVS_KEY_MQTT_TASK_PRIORITY      "mqtt_tsk_pri"
#define NVS_KEY_MQTT_TASK_STACK         "mqtt_tsk_stk"

// Network parameters
#define NVS_KEY_MQTT_DISABLE_RECONNECT  "mqtt_no_rcn"
#define NVS_KEY_MQTT_OUTBOX_LIMIT       "mqtt_ob_lmt"
#define NVS_KEY_MQTT_OUTBOX_RESEND_IV   "mqtt_ob_rsd"
#define NVS_KEY_MQTT_OUTBOX_EXPIRE      "mqtt_ob_exp"
#define NVS_KEY_MQTT_RECONNECT_INTERVAL "mqtt_rcn_iv"
#define NVS_KEY_MQTT_TIMEOUT            "mqtt_timeout"
#define NVS_KEY_MQTT_BUFFER_SIZE        "mqtt_buf"
#define NVS_KEY_MQTT_TX_BUF_SIZE        "mqtt_tx_buf"
#define NVS_KEY_MQTT_RX_BUF_SIZE        "mqtt_rx_buf"

// Topics
#define NVS_KEY_MQTT_RECV_TOPIC         "mqtt_t_recv"
#define NVS_KEY_MQTT_REPORT_TOPIC       "mqtt_t_rpt"
#define NVS_KEY_MQTT_STATUS_TOPIC       "mqtt_t_sts"
#define NVS_KEY_MQTT_CMD_TOPIC          "mqtt_t_cmd"

// QoS
#define NVS_KEY_MQTT_RECV_QOS           "mqtt_q_recv"
#define NVS_KEY_MQTT_REPORT_QOS         "mqtt_q_rpt"
#define NVS_KEY_MQTT_STATUS_QOS         "mqtt_q_sts"
#define NVS_KEY_MQTT_CMD_QOS            "mqtt_q_cmd"

// Auto subscription
#define NVS_KEY_MQTT_AUTO_SUB_RECV      "mqtt_as_rcv"
#define NVS_KEY_MQTT_AUTO_SUB_CMD       "mqtt_as_cmd"

// Status and heartbeat
#define NVS_KEY_MQTT_ENABLE_STATUS      "mqtt_en_sts"
#define NVS_KEY_MQTT_STATUS_INTERVAL    "mqtt_sts_iv"
#define NVS_KEY_MQTT_ENABLE_HEARTBEAT   "mqtt_en_hb"
#define NVS_KEY_MQTT_HEARTBEAT_INTERVAL "mqtt_hb_iv"

// Report content
#define NVS_KEY_MQTT_REPORT_CONTENT     "mqtt_rpt_ct"

// Continuous AI telemetry
#define NVS_KEY_MQTT_TELEMETRY_ENABLE   "mqtt_tel_en"
#define NVS_KEY_MQTT_TELEMETRY_TOPIC    "mqtt_t_tel"
#define NVS_KEY_MQTT_TELEMETRY_QOS      "mqtt_q_tel"
#define NVS_KEY_MQTT_TELEMETRY_FORMAT   "mqtt_f_tel"
 
 // Work mode configuration key names
 #define NVS_KEY_WORK_MODE           "work_mode"
 #define NVS_KEY_IMAGE_MODE_ENABLE   "img_mode_en"
 #define NVS_KEY_VIDEO_STREAM_MODE_ENABLE "vid_mode_en"

// PIR
#define NVS_KEY_PIR_ENABLE          "pir_enable"
#define NVS_KEY_PIR_PIN             "pir_pin"
#define NVS_KEY_PIR_TRIGGER_TYPE    "pir_type"
#define NVS_KEY_PIR_SENSITIVITY     "pir_sens"
#define NVS_KEY_PIR_IGNORE_TIME     "pir_ignore"
#define NVS_KEY_PIR_PULSE_COUNT     "pir_pulse"
#define NVS_KEY_PIR_WINDOW_TIME     "pir_window"
#define NVS_KEY_PIR_DISABLE_PREVIEW  "pir_dis_pv"
 // Remote Trigger
 #define NVS_KEY_REMOTE_TRIGGER_ENABLE "remote_trigger_enable"
 // IO (Indexed)
 #define NVS_KEY_IO_ENABLE_PREFIX    "io_enable" // Removed trailing underscore to match original

// RTMP configuration (simplified, part of video_stream_mode)
#define NVS_KEY_RTMP_ENABLE             "rtmp_en"
#define NVS_KEY_RTMP_URL                "rtmp_url"
#define NVS_KEY_RTMP_STREAM_KEY         "rtmp_key"
 #define NVS_KEY_IO_PIN_PREFIX       "io_pin"
 #define NVS_KEY_IO_INPUT_EN_PREFIX  "io_in_en"
 #define NVS_KEY_IO_OUTPUT_EN_PREFIX "io_out_en"
 #define NVS_KEY_IO_INPUT_TYPE_PREFIX "io_in_type"
 #define NVS_KEY_IO_OUTPUT_TYPE_PREFIX "io_out_type"
 // Timer
 #define NVS_KEY_TIMER_ENABLE        "timer_en"
 #define NVS_KEY_TIMER_INTERVAL      "timer_intv"
 #define NVS_KEY_TIMER_CAPTURE_MODE  "timer_mode"
 #define NVS_KEY_TIMER_NODE_COUNT    "timer_node_count"
 #define NVS_KEY_TIMER_NODE_PREFIX   "timer_node_"
 #define NVS_KEY_TIMER_WEEKDAYS_PREFIX "timer_weekdays_"
 #define NVS_KEY_TIMER_INTERVAL_MODE  "timer_iv_mode"
 #define NVS_KEY_TIMER_START_TIME     "timer_start_t"
 // Video Stream
 #define NVS_KEY_RTSP_URL            "rtsp_url"
 #define NVS_KEY_RTSP_ENABLE         "rtsp_en"
 #define NVS_KEY_RTSP_PORT           "rtsp_port"
 #define NVS_KEY_RTSP_AUTH_MODE      "rtsp_auth"
 #define NVS_KEY_RTSP_USERNAME       "rtsp_user"
 #define NVS_KEY_RTSP_PASSWORD       "rtsp_pass"

// UI preferences
#define NVS_KEY_PREF_STREAM_TAB     "pref_stab"

// Webhook configuration key names
#define NVS_KEY_WEBHOOK_ENABLE      "wh_enable"
#define NVS_KEY_WEBHOOK_URL         "wh_url"
#define NVS_KEY_WEBHOOK_AUTH_TYPE   "wh_auth"
#define NVS_KEY_WEBHOOK_SECRET      "wh_secret"
#define NVS_KEY_WEBHOOK_CA_CERT_PATH "wh_ca_path"

// Capture-upload configuration key names
#define NVS_KEY_CAPUP_VERSION       "cu_ver"
#define NVS_KEY_CAPUP_MODE          "cu_mode"
#define NVS_KEY_CAPUP_STORAGE       "cu_store"
#define NVS_KEY_CAPUP_POLICY        "cu_policy"
#define NVS_KEY_CAPUP_PROTO         "cu_proto"
#define NVS_KEY_CAPUP_RETRY_EN      "cu_re_en"
#define NVS_KEY_CAPUP_RETRY_MAX     "cu_re_max"
#define NVS_KEY_CAPUP_BATCH_N       "cu_bat_n"
#define NVS_KEY_CAPUP_SCHED_CNT     "cu_sch_cnt"
#define NVS_KEY_CAPUP_SCHED_MIN_FMT "cu_sch_%u"   /* uint16_t index 0..7 */
#define NVS_KEY_CAPUP_KEEP_HOURS    "cu_keep_h"
#define NVS_KEY_CAPUP_MAX_PENDING   "cu_max_pd"
#define NVS_KEY_CAPUP_COMM_TYPE     "cu_comm"


 /* ==================== Internal Function Prototypes ==================== */
 
 /* --- From json_config_nvs.c --- */
 aicam_result_t json_config_save_log_config_to_nvs(const log_config_t *config);
 aicam_result_t json_config_save_ai_debug_config_to_nvs(const ai_debug_config_t *config);
 aicam_result_t json_config_save_work_mode_config_to_nvs(const work_mode_config_t *config);
 aicam_result_t json_config_save_power_mode_config_to_nvs(const power_mode_config_t *config);
aicam_result_t json_config_save_device_info_config_to_nvs(const device_info_config_t *config);
aicam_result_t json_config_save_device_service_image_config_to_nvs(const image_config_t *config);
aicam_result_t json_config_save_device_service_light_config_to_nvs(const light_config_t *config);
aicam_result_t json_config_save_isp_config_to_nvs(const isp_config_t *config);
aicam_result_t json_config_save_network_service_config_to_nvs(const network_service_config_t *config);
aicam_result_t json_config_save_poe_config_to_nvs(const poe_config_persist_t *config);
aicam_result_t json_config_load_poe_config_from_nvs(poe_config_persist_t *config);
aicam_result_t json_config_save_mqtt_service_config_to_nvs(const mqtt_service_config_t *config);
// RTMP config now part of video_stream_mode, use json_config_get/set_video_stream_mode()
aicam_result_t json_config_save_auth_mgr_config_to_nvs(const auth_mgr_config_t *config);
aicam_result_t json_config_save_webhook_config_to_nvs(const webhook_config_t *config);
aicam_result_t json_config_load_webhook_from_nvs(webhook_config_t *config);
aicam_result_t json_config_save_capture_upload_to_nvs(const capture_upload_config_t *config);
aicam_result_t json_config_load_capture_upload_from_nvs(capture_upload_config_t *config);
 aicam_result_t json_config_save_to_nvs(const aicam_global_config_t *config);
 aicam_result_t json_config_load_from_nvs(aicam_global_config_t *config);
 
 // NVS Helper Functions (NOW INTERNAL API)
 aicam_result_t json_config_nvs_write_string(const char *key, const char *value);
 aicam_result_t json_config_nvs_read_string(const char *key, char *value, size_t max_len);
 aicam_result_t json_config_nvs_write_uint32(const char *key, uint32_t value);
 aicam_result_t json_config_nvs_read_uint32(const char *key, uint32_t *value);
 aicam_result_t json_config_nvs_write_uint64(const char *key, uint64_t value);
 aicam_result_t json_config_nvs_read_uint64(const char *key, uint64_t *value);
 aicam_result_t json_config_nvs_write_float(const char *key, float value);
 aicam_result_t json_config_nvs_read_float(const char *key, float *value);
 aicam_result_t json_config_nvs_write_uint8(const char *key, uint8_t value);
 aicam_result_t json_config_nvs_read_uint8(const char *key, uint8_t *value);
 aicam_result_t json_config_nvs_write_bool(const char *key, aicam_bool_t value);
 aicam_result_t json_config_nvs_read_bool(const char *key, aicam_bool_t *value);
 aicam_result_t json_config_nvs_write_int32(const char *key, int32_t value);
 aicam_result_t json_config_nvs_read_int32(const char *key, int32_t *value);
 
 
 /* --- From json_config_json.c --- */
 aicam_result_t json_config_parse_json_object(const char *json_str, aicam_global_config_t *config);
 aicam_result_t json_config_serialize_json_object(const aicam_global_config_t *config, char *json_buffer, size_t buffer_size);
 
 /* --- From json_config_utils.c --- */
 uint32_t json_config_crc32(const void *data, size_t length);
 uint64_t json_config_get_timestamp(void);
 aicam_result_t json_config_validate_ranges(const aicam_global_config_t *config);
 aicam_bool_t json_config_enforce_invariants(aicam_global_config_t *config);
 void json_config_generate_device_name_from_mac(char *device_name, size_t name_size, const char *mac_address);
 
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif // JSON_CONFIG_INTERNAL_H