/**
 * @file aicam_types.h
 * @brief AI Camera Common Types and Definitions
 * @details Common type definitions and system-level enums and structures
 */

#ifndef AICAM_TYPES_H
#define AICAM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Basic Type Definitions ==================== */

/**
 * @brief Boolean type definition
 */
typedef enum {
    AICAM_FALSE = 0,
    AICAM_TRUE = 1
} aicam_bool_t;

/**
 * @brief System result/error codes
 */
typedef enum {
    AICAM_OK = 0,                    // Operation successful
    AICAM_ERROR = -1,                // Generic error
    AICAM_ERROR_INVALID_PARAM = -2,  // Invalid parameter
    AICAM_ERROR_INVALID_DATA = -3,   // Invalid data
    AICAM_ERROR_NO_MEMORY = -4,      // Out of memory
    AICAM_ERROR_TIMEOUT = -5,        // Operation timeout
    AICAM_ERROR_BUSY = -6,           // Resource busy
    AICAM_ERROR_NOT_FOUND = -7,      // Resource not found
    AICAM_ERROR_NOT_SUPPORTED = -8,  // Operation not supported
    AICAM_ERROR_PERMISSION = -9,     // Permission denied
    AICAM_ERROR_IO = -10,             // I/O error
    AICAM_ERROR_NETWORK = -11,       // Network error
    AICAM_ERROR_FORMAT = -12,        // Format error
    AICAM_ERROR_CHECKSUM = -13,      // Checksum error
    AICAM_ERROR_OVERFLOW = -14,      // Buffer overflow
    AICAM_ERROR_UNDERFLOW = -15,     // Buffer underflow
    AICAM_ERROR_CORRUPTED = -16,     // Data corrupted
    AICAM_ERROR_LOCKED = -17,        // Resource locked
    AICAM_ERROR_UNAVAILABLE = -18,   // Service unavailable
    AICAM_ERROR_CANCELLED = -19,     // Operation cancelled
    AICAM_ERROR_DUPLICATE = -20,     // Duplicate operation
    AICAM_ERROR_FULL = -21,          // Container full
    AICAM_ERROR_EMPTY = -22,         // Container empty
    AICAM_ERROR_CONFIG = -23,        // Configuration error
    AICAM_ERROR_HARDWARE = -24,      // Hardware error
    AICAM_ERROR_FIRMWARE = -25,      // Firmware error
    AICAM_ERROR_PROTOCOL = -26,      // Protocol error
    AICAM_ERROR_VERSION = -27,       // Version incompatible
    AICAM_ERROR_SIGNATURE = -28,     // Signature verification failed
    AICAM_ERROR_ENCRYPTION = -29,    // Encryption/decryption failed
    AICAM_ERROR_AUTHENTICATION = -30,// Authentication failed
    AICAM_ERROR_AUTHORIZATION = -31, // Authorization failed
    AICAM_ERROR_QUOTA_EXCEEDED = -32,// Quota exceeded
    AICAM_ERROR_RATE_LIMIT = -33,    // Rate limit exceeded
    AICAM_ERROR_MAINTENANCE = -34,   // System under maintenance
    AICAM_ERROR_DEPRECATED = -35,    // Feature deprecated
    AICAM_ERROR_NOT_INITIALIZED = -36,// Not initialized
    AICAM_ERROR_BUFFER_FULL = -37,   // Buffer full
    AICAM_ERROR_BUFFER_EMPTY = -38,  // Buffer empty
    AICAM_ERROR_ALREADY_EXISTS = -39,// Already exists
    AICAM_ERROR_OUT_OF_MEMORY = -40, // Out of memory
    AICAM_ERROR_ALREADY_RUNNING = -41,// Already running
    AICAM_ERROR_ALREADY_INITIALIZED = -42,// Already initialized
    AICAM_ERROR_NOT_SENT_AGAIN = -43,// Not sent again
    AICAM_ERROR_REACH_MAX_ATTEMPTS = -44, // Reach max attempts
    AICAM_ERROR_INVALID_STATE = -45,// State mismatch - operation not applicable
    
    // Layer-specific error codes
    AICAM_ERROR_HAL_INIT = -100,     // HAL initialization failed
    AICAM_ERROR_HAL_CONFIG = -101,   // HAL configuration error
    AICAM_ERROR_HAL_IO = -102,       // HAL I/O error
    
    AICAM_ERROR_CORE_INIT = -200,    // Core initialization failed
    AICAM_ERROR_CORE_CONFIG = -201,  // Core configuration error
    
    AICAM_ERROR_SERVICE_INIT = -300,   // Service initialization failed
    AICAM_ERROR_SERVICE_CONFIG = -301, // Service configuration error
    
    AICAM_ERROR_APP_INIT = -400,     // Application initialization failed
    AICAM_ERROR_APP_CONFIG = -401,   // Application configuration error

    AICAM_ERROR_UNAUTHORIZED = -500, // Unauthorized

    
    AICAM_ERROR_UNKNOWN = -999       // Unknown error
} aicam_result_t;

/**
 * @brief System states
 */
typedef enum {
    AICAM_STATE_UNKNOWN = 0,
    AICAM_STATE_INITIALIZING = 1,
    AICAM_STATE_READY = 2,
    AICAM_STATE_RUNNING = 3,
    AICAM_STATE_STOPPING = 4,
    AICAM_STATE_STOPPED = 5,
    AICAM_STATE_ERROR = 6,
    AICAM_STATE_MAINTENANCE = 7
} aicam_state_t;

/**
 * @brief Priority levels
 */
typedef enum {
    AICAM_PRIORITY_IDLE = 0,
    AICAM_PRIORITY_LOW = 1,
    AICAM_PRIORITY_NORMAL = 2,
    AICAM_PRIORITY_HIGH = 3,
    AICAM_PRIORITY_CRITICAL = 4,
    AICAM_PRIORITY_REALTIME = 5
} aicam_priority_t;

/* ==================== Hardware Related Types ==================== */

/**
 * @brief GPIO states
 */
typedef enum {
    AICAM_GPIO_LOW = 0,
    AICAM_GPIO_HIGH = 1
} aicam_gpio_state_t;

/**
 * @brief Power modes
 */
typedef enum {
    AICAM_POWER_MODE_FULL = 0,
    AICAM_POWER_MODE_LOW = 1,
    AICAM_POWER_MODE_SLEEP = 2,
    AICAM_POWER_MODE_DEEP_SLEEP = 3,
    AICAM_POWER_MODE_SHUTDOWN = 4
} aicam_power_mode_t;

/**
 * @brief Connection types
 */
typedef enum {
    AICAM_CONNECTION_NONE = 0,
    AICAM_CONNECTION_USB = 1,
    AICAM_CONNECTION_WIFI_AP = 2,
    AICAM_CONNECTION_WIFI_STA = 3,
    AICAM_CONNECTION_POE = 4
} aicam_connection_type_t;

/**
 * @brief Work modes
 */
typedef enum {
    AICAM_WORK_MODE_IMAGE = 0,
    AICAM_WORK_MODE_VIDEO_STREAM = 1,
    AICAM_WORK_MODE_MAX
} aicam_work_mode_t;

/* ==================== AI/ML Related Types ==================== */

/**
 * @brief AI model formats
 */
typedef enum {
    AICAM_MODEL_FORMAT_ONNX = 0,
    AICAM_MODEL_FORMAT_TFLITE = 1,
    AICAM_MODEL_FORMAT_NCNN = 2,
    AICAM_MODEL_FORMAT_OPENVINO = 3
} aicam_model_format_t;

/**
 * @brief AI inference states
 */
typedef enum {
    AICAM_AI_STATE_IDLE = 0,
    AICAM_AI_STATE_LOADING = 1,
    AICAM_AI_STATE_READY = 2,
    AICAM_AI_STATE_RUNNING = 3,
    AICAM_AI_STATE_ERROR = 4
} aicam_ai_state_t;

/* ==================== Camera Related Types ==================== */

/**
 * @brief Camera resolutions
 */
typedef enum {
    AICAM_RESOLUTION_QVGA = 0,      // 320x240
    AICAM_RESOLUTION_VGA = 1,       // 640x480
    AICAM_RESOLUTION_SVGA = 2,      // 800x600
    AICAM_RESOLUTION_HD = 3,        // 1280x720
    AICAM_RESOLUTION_FHD = 4,       // 1920x1080
    AICAM_RESOLUTION_2K = 5,        // 2560x1440
    AICAM_RESOLUTION_4K = 6         // 3840x2160
} aicam_resolution_t;

/**
 * @brief Camera pixel formats
 */
typedef enum {
    AICAM_PIXEL_FORMAT_RGB565 = 0,
    AICAM_PIXEL_FORMAT_RGB888 = 1,
    AICAM_PIXEL_FORMAT_YUV422 = 2,
    AICAM_PIXEL_FORMAT_YUV420 = 3,
    AICAM_PIXEL_FORMAT_JPEG = 4,
    AICAM_PIXEL_FORMAT_RAW = 5
} aicam_pixel_format_t;

/* ==================== Storage Related Types ==================== */

/**
 * @brief Storage types
 */
typedef enum {
    AICAM_STORAGE_TYPE_INTERNAL = 0,
    AICAM_STORAGE_TYPE_SD_CARD = 1,
    AICAM_STORAGE_TYPE_USB = 2,
    AICAM_STORAGE_TYPE_NETWORK = 3
} aicam_storage_type_t;

/**
 * @brief File system types
 */
typedef enum {
    AICAM_FS_TYPE_LITTLEFS = 0,
    AICAM_FS_TYPE_FATFS = 1,
    AICAM_FS_TYPE_SPIFFS = 2
} aicam_fs_type_t;

/* ==================== Network Related Types ==================== */

/**
 * @brief Network protocols
 */
typedef enum {
    AICAM_PROTOCOL_HTTP = 0,
    AICAM_PROTOCOL_HTTPS = 1,
    AICAM_PROTOCOL_WEBSOCKET = 2,
    AICAM_PROTOCOL_MQTT = 3,
    AICAM_PROTOCOL_RTSP = 4,
    AICAM_PROTOCOL_UDP = 5,
    AICAM_PROTOCOL_TCP = 6
} aicam_protocol_t;

/**
 * @brief Network security types
 */
typedef enum {
    AICAM_SECURITY_NONE = 0,
    AICAM_SECURITY_WEP = 1,
    AICAM_SECURITY_WPA = 2,
    AICAM_SECURITY_WPA2 = 3,
    AICAM_SECURITY_WPA3 = 4
} aicam_security_type_t;


/**
 * @brief Trigger types
 */
typedef enum {
    AICAM_TRIGGER_TYPE_RISING = 0,
    AICAM_TRIGGER_TYPE_FALLING = 1,
    AICAM_TRIGGER_TYPE_BOTH_EDGES = 2,
    AICAM_TRIGGER_TYPE_HIGH = 3,
    AICAM_TRIGGER_TYPE_LOW = 4,
    AICAM_TRIGGER_TYPE_MAX
} aicam_trigger_type_t;

/**
 * @brief Capture trigger source types
 */
typedef enum {
    AICAM_CAPTURE_TRIGGER_UNKNOWN    = 0,  ///< Unknown / unspecified
    AICAM_CAPTURE_TRIGGER_RTC        = 1,  ///< RTC timer scheduled capture
    AICAM_CAPTURE_TRIGGER_PIR        = 2,  ///< PIR motion sensor
    AICAM_CAPTURE_TRIGGER_WEB        = 3,  ///< Web page manual capture
    AICAM_CAPTURE_TRIGGER_REMOTE     = 4,  ///< Remote MQTT command capture
    AICAM_CAPTURE_TRIGGER_GPIO       = 5,  ///< External GPIO trigger
    AICAM_CAPTURE_TRIGGER_BUTTON     = 6,  ///< Physical button press
    AICAM_CAPTURE_TRIGGER_SCHEDULE   = 7,  ///< Software scheduler
    AICAM_CAPTURE_TRIGGER_MAX
} aicam_capture_trigger_t;

/**
 * @brief Capture modes
 */
typedef enum {
    AICAM_TIMER_CAPTURE_MODE_NONE = 0,
    AICAM_TIMER_CAPTURE_MODE_INTERVAL = 1,
    AICAM_TIMER_CAPTURE_MODE_ABSOLUTE = 2
} aicam_timer_capture_mode_t;

/**
 * @brief Interval mode for timer capture (only used with INTERVAL capture mode)
 */
typedef enum {
    AICAM_TIMER_INTERVAL_MODE_NORMAL = 0,     // Start immediately on save
    AICAM_TIMER_INTERVAL_MODE_SCHEDULED = 1,  // Start from daily start_time
} aicam_timer_interval_mode_t;

/* ==================== Common Structure Definitions ==================== */

/**
 * @brief Version information structure
 */
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint8_t build;
    char version_string[16];
} aicam_version_t;

/**
 * @brief Time structure
 */
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
} aicam_time_t;

/**
 * @brief Memory information structure
 */
typedef struct {
    uint32_t total_size;
    uint32_t used_size;
    uint32_t free_size;
    uint32_t largest_free_block;
    uint32_t allocation_count;
    uint32_t free_count;
} aicam_memory_info_t;

/**
 * @brief Point structure (for image processing)
 */
typedef struct {
    int16_t x;
    int16_t y;
} aicam_point_t;

/**
 * @brief Rectangle structure (for image processing)
 */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} aicam_rect_t;

/**
 * @brief Size structure
 */
typedef struct {
    uint16_t width;
    uint16_t height;
} aicam_size_t;

/**
 * @brief Color structure (RGB)
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;  // Alpha channel
} aicam_color_t;

/* ==================== Callback Function Types ==================== */

/**
 * @brief Generic callback function type
 */
typedef void (*aicam_callback_t)(void *user_data);

/**
 * @brief Event callback function type
 */
typedef void (*aicam_event_callback_t)(uint32_t event_id, void *event_data, void *user_data);

/**
 * @brief Timer callback function type
 */
typedef void (*aicam_timer_callback_t)(void *timer_id, void *user_data);

/**
 * @brief Error handler callback function type
 */
typedef void (*aicam_error_handler_t)(aicam_result_t error_code, const char *error_msg, void *user_data);

/* ==================== Utility Macros ==================== */

/**
 * @brief Get array size
 */
#define AICAM_ARRAY_SIZE(arr)           (sizeof(arr) / sizeof((arr)[0]))

/**
 * @brief Min/Max macros
 */
#define AICAM_MIN(a, b)                 ((a) < (b) ? (a) : (b))
#define AICAM_MAX(a, b)                 ((a) > (b) ? (a) : (b))

/**
 * @brief Clamp value to range
 */
#define AICAM_CLAMP(value, min, max)    (AICAM_MAX(min, AICAM_MIN(max, value)))

/**
 * @brief Align value to boundary
 */
#define AICAM_ALIGN(value, align)       (((value) + (align) - 1) & ~((align) - 1))

/**
 * @brief Check if value is power of 2
 */
#define AICAM_IS_POWER_OF_2(x)          (((x) != 0) && (((x) & ((x) - 1)) == 0))

/**
 * @brief Convert milliseconds to ticks (assuming 1ms tick)
 */
#define AICAM_MS_TO_TICKS(ms)           (ms)

/**
 * @brief Convert seconds to ticks (assuming 1ms tick)
 */
#define AICAM_S_TO_TICKS(s)             ((s) * 1000)

/**
 * @brief Bit manipulation macros
 */
#define AICAM_BIT_SET(reg, bit)         ((reg) |= (1U << (bit)))
#define AICAM_BIT_CLEAR(reg, bit)       ((reg) &= ~(1U << (bit)))
#define AICAM_BIT_TOGGLE(reg, bit)      ((reg) ^= (1U << (bit)))
#define AICAM_BIT_CHECK(reg, bit)       (((reg) >> (bit)) & 1U)

/**
 * @brief Byte order conversion macros (for network protocols)
 */
#define AICAM_SWAP16(x)                 ((uint16_t)(((x) >> 8) | ((x) << 8)))
#define AICAM_SWAP32(x)                 ((uint32_t)(((x) >> 24) | (((x) & 0x00FF0000) >> 8) | \
                                                   (((x) & 0x0000FF00) << 8) | ((x) << 24)))

/* ==================== Memory Alignment Macros ==================== */

/**
 * @brief Memory alignment attributes
 */
#if defined(__GNUC__)
    #define AICAM_ALIGN_4           __attribute__((aligned(4)))
    #define AICAM_ALIGN_8           __attribute__((aligned(8)))
    #define AICAM_ALIGN_16          __attribute__((aligned(16)))
    #define AICAM_ALIGN_32          __attribute__((aligned(32)))
    #define AICAM_PACKED            __attribute__((packed))
#elif defined(__ARMCC_VERSION)
    #define AICAM_ALIGN_4           __align(4)
    #define AICAM_ALIGN_8           __align(8)
    #define AICAM_ALIGN_16          __align(16)
    #define AICAM_ALIGN_32          __align(32)
    #define AICAM_PACKED            __packed
#else
    #define AICAM_ALIGN_4
    #define AICAM_ALIGN_8
    #define AICAM_ALIGN_16
    #define AICAM_ALIGN_32
    #define AICAM_PACKED
#endif

/* ==================== Compiler Attributes ==================== */

/**
 * @brief Function attributes
 */
#if defined(__GNUC__)
    #define AICAM_WEAK              __attribute__((weak))
    #define AICAM_INLINE            __inline__
    #define AICAM_ALWAYS_INLINE     __attribute__((always_inline))
    #define AICAM_NO_RETURN         __attribute__((noreturn))
    #define AICAM_UNUSED(x)         (void)(x)   
    #define AICAM_DEPRECATED        __attribute__((deprecated))
#else
    #define AICAM_WEAK
    #define AICAM_INLINE            inline
    #define AICAM_ALWAYS_INLINE     inline
    #define AICAM_NO_RETURN
    #define AICAM_UNUSED(x)         (void)(x)   
    #define AICAM_DEPRECATED
#endif


/* ==================== Debug and Assert Macros ==================== */

#ifdef DEBUG
    #define AICAM_ASSERT(expr)      do { \
                                        if (!(expr)) { \
                                            while(1); \
                                        } \
                                    } while(0)
#else
    #define AICAM_ASSERT(expr)      ((void)0)
#endif

/**
 * @brief Static assert macro
 */
#define AICAM_STATIC_ASSERT(cond, msg)  _Static_assert(cond, msg)

/* ==================== String and Buffer Utilities ==================== */

/**
 * @brief Safe string copy macro
 */
#define AICAM_SAFE_STRCPY(dest, src, size) do { \
    strncpy(dest, src, size - 1); \
    dest[size - 1] = '\0'; \
} while(0)

/**
 * @brief Zero memory macro
 */
#define AICAM_ZERO_MEMORY(ptr, size)    memset(ptr, 0, size)

/**
 * @brief Zero structure macro
 */
#define AICAM_ZERO_STRUCT(s)            memset(&(s), 0, sizeof(s))


#ifdef __cplusplus
}
#endif

#endif // AICAM_TYPES_H 