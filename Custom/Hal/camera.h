#ifndef _CAMERA_H
#define _CAMERA_H

#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "aicam_error.h"
#include "isp_services.h"

/** ISP IQ preset for built-in tuning profiles (see image_config_t.isp_mode on device side). */
typedef enum {
    CAM_IQ_SCENE_INDOOR = 0,
    CAM_IQ_SCENE_OUTDOOR = 1,
} cam_iq_scene_t;

/* Define sensor info */
#define SENSOR_IMX335_WIDTH 2592
#define SENSOR_IMX335_HEIGHT 1944
#define SENSOR_IMX335_FLIP CMW_MIRRORFLIP_MIRROR

#define SENSOR_VD66GY_WIDTH 1120
#define SENSOR_VD66GY_HEIGHT 720
#define SENSOR_VD66GY_FLIP CMW_MIRRORFLIP_FLIP

#define SENSOR_VD55G1_WIDTH 800
#define SENSOR_VD55G1_HEIGHT 600
#define SENSOR_VD55G1_FLIP CMW_MIRRORFLIP_FLIP

#define SENSOR_OS04C10_WIDTH 2688
#define SENSOR_OS04C10_HEIGHT 1520
#define SENSOR_OS04C10_FLIP CMW_MIRRORFLIP_NONE


#define EXPOSURE_MIN 23000   
#define EXPOSURE_MAX 33000  
#define GAIN_MIN     0      
#define GAIN_MAX     15872   

/* Delay display by CAPTURE_DELAY frame number */
#define CAPTURE_DELAY 1
#define CAMERA_FPS 30

#define PIPE1_DEFAULT_WIDTH 1280
#define PIPE1_DEFAULT_HEIGHT 720
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
#define PIPE1_DEFAULT_FORMAT DCMIPP_PIXEL_PACKER_FORMAT_ARGB8888
#define PIPE1_DEFAULT_BPP 4
#else
#define PIPE1_DEFAULT_FORMAT DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1
#define PIPE1_DEFAULT_BPP 2
#endif

#define PIPE2_DEFAULT_WIDTH 224
#define PIPE2_DEFAULT_HEIGHT 224
#define PIPE2_DEFAULT_FORMAT DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1
#define PIPE2_DEFAULT_BPP 3
#define PIPE2_MAX_WIDTH     480
#define PIPE2_MAX_HEIGHT    480

#ifdef ISP_MW_TUNING_TOOL_SUPPORT
#define CAPTURE_BUFFER_NB (CAPTURE_DELAY + 2)
#else
#define CAPTURE_BUFFER_NB (CAPTURE_DELAY + 2)
#endif
#define NN_BUFFER_NB 3

#define CAMERA_CTRL_PIPE1_BIT (1<<1)
#define CAMERA_CTRL_PIPE2_BIT (1<<2)

typedef uint8_t * (*nn_get_buffer)(void);
typedef void (*nn_put_buffer)(void);

typedef enum {
    CAM_CMD_SET_SENSOR_PARAM        = CAMERA_CMD_BASE,
    CAM_CMD_GET_SENSOR_PARAM,
    CAM_CMD_SET_ISP_PARAM,
    CAM_CMD_GET_ISP_PARAM,
    CAM_CMD_SET_PIPE_CTRL,
    CAM_CMD_SET_PIPE1_PARAM,
    CAM_CMD_SET_PIPE2_PARAM,
    CAM_CMD_GET_PIPE1_PARAM,
    CAM_CMD_GET_PIPE2_PARAM,
    CAM_CMD_SET_PIPE1_START,
    CAM_CMD_SET_PIPE1_STOP,
    CAM_CMD_SET_PIPE2_START,
    CAM_CMD_SET_PIPE2_STOP,
    CAM_CMD_GET_PIPE1_BUFFER,
    CAM_CMD_LOCK_PIPE1_BUFFER,
    CAM_CMD_UNLOCK_PIPE1_BUFFER,
    CAM_CMD_GET_PIPE2_BUFFER,
    CAM_CMD_LOCK_PIPE2_BUFFER,
    CAM_CMD_UNLOCK_PIPE2_BUFFER,
    CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID,
    CAM_CMD_GET_PIPE2_BUFFER_WITH_FRAME_ID,
    CAM_CMD_SET_PIPE2_BUFFER_ADDR,
    CAM_CMD_RETURN_PIPE1_BUFFER,
    CAM_CMD_RETURN_PIPE2_BUFFER,
    CAM_CMD_SET_STARTUP_SKIP_FRAMES,    // Set frames to skip on startup for stabilization
    CAM_CMD_GET_STARTUP_SKIP_FRAMES,    // Get current startup skip frames setting
    CAM_CMD_UNSHARE_PIPE1_BUFFER,
    CAM_CMD_UNSHARE_PIPE2_BUFFER,
} CAM_CMD_E;

typedef enum {
    PIPE_STOP    =  0,
    PIPE_START,
    PIPE_SUSPEND,
    PIPE_RESUME,
} PIPE_STATE_E;

typedef enum {
    CAMERA_STOP    =  0,
    CAMERA_START,
} CAMERA_STATE_E;

typedef enum {
    BUFFER_IDLE = 0,      // Idle
    BUFFER_PROCESSING,    // Processing
    BUFFER_READY,         // Ready
    BUFFER_IN_USE         // In use
} BUFFER_STATE_E;

#define CAMERA_BUF_MAX_OWNERS 3
typedef struct {
    uint8_t* data;
    BUFFER_STATE_E state;
    uint32_t frame_id;
    osThreadId_t owner_list[CAMERA_BUF_MAX_OWNERS];
    uint8_t is_locked;
    uint8_t owner_count;
    uint8_t return_count;
} pipe_buffer_t;

typedef struct {
    uint8_t* buffer;        // Buffer pointer
    uint32_t frame_id;      // Frame ID
    uint32_t size;          // Buffer size in bytes
} camera_buffer_with_frame_id_t;

typedef struct {
    osMessageQueueId_t ready_queue;
    osSemaphoreId_t idle_sem;
} camera_dq_t;

typedef struct {
    const char *name;
    int width;
    int height;
    int mirror_flip;
    int fps;
    int brightness;
    int contrast;
    uint32_t aec;
} sensor_params_t;

typedef struct{
    int width;
    int height;
    int format;
    int bpp;
    int fps;
    int buffer_nb;
    int extbuffer_flag;
    uint8_t *extbuffer;
} pipe_params_t;

typedef struct{
    CAMERA_STATE_E camera_state;
    PIPE_STATE_E pipe1_state;
    PIPE_STATE_E pipe2_state;
} camera_state_t;

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    uint8_t mtx_isr;
    osSemaphoreId_t sem_init;
    osSemaphoreId_t sem_isp;
    osSemaphoreId_t sem_pipe1;
    osSemaphoreId_t sem_pipe2;
    sensor_params_t sensor_param;
    ISP_IQParamTypeDef isp_iq_param;
    pipe_params_t pipe1_param;
    pipe_params_t pipe2_param;
    pipe_buffer_t *pipe1_buffer;
    camera_dq_t pipe1_dq;
    pipe_buffer_t *pipe2_buffer;
    camera_dq_t pipe2_dq;
    uint8_t device_ctrl_pipe;
    int current_frame_id;
    int skip_frame_counter;
    int startup_skip_frames;        // Configurable frames to skip on startup
    osThreadId_t camera_processId;
    camera_state_t state;
    PowerHandle pwr_handle;
} camera_t;

int camera_register(void);
int camera_deinit_but_not_unregister(void);
void camera_free_unshared_buffer(uint8_t *buffer);
int camera_unregister(void);

/**
 * @brief Get ISP handle for ISP API module
 * @return ISP handle pointer, or NULL if not initialized
 */
ISP_HandleTypeDef* camera_get_isp_handle(void);

/**
 * @brief Fill ISP IQ parameters for a built-in scene (indoor vs outdoor stock).
 * @param scene CAM_IQ_SCENE_INDOOR applies IQTune contrast + stat region (OS04C10); outdoor uses sensor defaults only.
 * @param out_iq Output buffer (must not be NULL).
 */
void camera_fill_isp_iq_scene(cam_iq_scene_t scene, ISP_IQParamTypeDef *out_iq);

/**
 * @brief Overlay grayscale (BT.601 luma) ISP settings on an IQ profile.
 * @param iq IQ buffer to modify in place.
 * @param grayscale AICAM_TRUE: disable AWB and use luma color matrix; AICAM_FALSE: no change.
 */
void camera_apply_grayscale_iq(ISP_IQParamTypeDef *iq, aicam_bool_t grayscale);

/**
 * @brief Reserved: PIPE1 stays RGB565; grayscale is ISP-only (see camera_apply_grayscale_iq).
 */
void camera_configure_pipe1_grayscale(pipe_params_t *pipe1, aicam_bool_t grayscale);

#endif