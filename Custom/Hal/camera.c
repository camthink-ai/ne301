#include <assert.h>
#include <stdint.h>
#include "cmw_camera.h"
#include "camera.h"
#include "common_utils.h"
#include "debug.h"
#include "mem.h"
#include "stm32n6xx_hal.h"
#include "rcc_ic_auto.h"
#include "isp_param_conf.h"
#include "isp_api.h"
#include "isp_core.h"

// Constant definitions
#define CAMERA_TASK_DELAY_MS            0
#define CAMERA_ISP_SEM_TIMEOUT_MS       50
#define CAMERA_BUFFER_TIMEOUT_MS        50
#define CAMERA_MEMORY_ALIGNMENT         32
#define CAMERA_DEINIT_DELAY_MS          10
#define CAMERA_MAX_READY_BUFFERS        8
#define CAMERA_DEFAULT_STARTUP_SKIP_FRAMES  10   // Default frames to skip on startup for stabilization

static int pipe_buffer_acquire(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq);
static int pipe_buffer_release(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq);

static camera_t g_camera = {0};
const osThreadAttr_t cameraTask_attributes = {
    .name = "cameraTask",
    .priority = (osPriority_t) osPriorityRealtime,
    .stack_size = 4 * 1024
};

static int sensor_width;
static int sensor_height;

typedef enum {
    CAMERA_BUFF_INIT    =  0,
    CAMERA_BUFF_READY,
    CAMERA_BUFF_FREE,
} BUFF_STATE_E;

static const char *sensor_names[] = {
    "CMW_UNKNOWN",
    "CMW_VD66GY",
    "CMW_IMX335",
    "CMW_VD55G1",
    "CMW_OS04C10",
};

#ifdef ISP_MW_TUNING_TOOL_SUPPORT
#ifdef ISP_ENABLE_UVC
#include "usbx_conf.h"
#endif

extern CMW_Sensor_if_t Camera_Drv;
extern DCMIPP_HandleTypeDef hcamera_dcmipp;
ISP_HandleTypeDef hIsp;
static uint8_t isp_is_init = 0, isp_is_start = 0;

/**
* @brief  Get the sensor info
* @param  camera_instance: camera instance
* @param  info: sensor info
* @retval Operation result
*/
ISP_StatusTypeDef GetSensorInfo(uint32_t camera_instance, ISP_SensorInfoTypeDef *info)
{
    UNUSED(camera_instance);

    if (CMW_CAMERA_GetSensorInfo(info) != CMW_ERROR_NONE)
        return ISP_ERR_SENSORINFO;

    // info->width = g_camera.pipe1_param.width;
    // info->height = g_camera.pipe1_param.height;
    return ISP_OK;
}

/**
* @brief  Set the sensor gain
* @param  camera_instance: camera instance
* @param  gain: sensor gain to be applied
* @retval Operation result
*/
ISP_StatusTypeDef SetSensorGain(uint32_t camera_instance, int32_t gain)
{
    UNUSED(camera_instance);

    if (CMW_CAMERA_SetGain(gain) != CMW_ERROR_NONE)
        return ISP_ERR_SENSORGAIN;

    return ISP_OK;
}

/**
* @brief  Get the sensor gain
* @param  camera_instance: camera instance
* @param  gain: current sensor gain
* @retval Operation result
*/
ISP_StatusTypeDef GetSensorGain(uint32_t camera_instance, int32_t *gain)
{
    UNUSED(camera_instance);

    if (CMW_CAMERA_GetGain(gain) != CMW_ERROR_NONE)
        return ISP_ERR_SENSORGAIN;

    return ISP_OK;
}

/**
* @brief  Set the sensor exposure
* @param  Instance: camera instance
* @param  Gain: sensor exposure to be applied
* @retval Operation result
*/
ISP_StatusTypeDef SetSensorExposure(uint32_t camera_instance, int32_t exposure)
{
    UNUSED(camera_instance);

    if (CMW_CAMERA_SetExposure(exposure) != CMW_ERROR_NONE)
        return ISP_ERR_SENSOREXPOSURE;

    return ISP_OK;
}

/**
* @brief  Get the sensor exposure
* @param  camera_instance: camera instance
* @param  gain: current sensor gain
* @retval Operation result
*/
ISP_StatusTypeDef GetSensorExposure(uint32_t camera_instance, int32_t *exposure)
{
    UNUSED(camera_instance);

    if (CMW_CAMERA_GetExposure(exposure) != CMW_ERROR_NONE)
        return ISP_ERR_SENSOREXPOSURE;

    return ISP_OK;
}

/**
  * @brief  Set the sensor test pattern
  * @param  camera_instance: camera instance
  * @param  mode: sensor test pattern to be applied
  * @retval Operation result
  */
ISP_StatusTypeDef SetSensorTestPattern(uint32_t camera_instance, int32_t mode)
{
    UNUSED(camera_instance);

    if (CMW_CAMERA_SetTestPattern(mode) != CMW_ERROR_NONE)
        return ISP_ERR_EINVAL;

    return ISP_OK;
}

/**
* @brief  Helper for ISP to start camera preview
* @param  hDcmipp Pointer to the dcmipp device
* @retval ISP status operation
*/
ISP_StatusTypeDef Camera_StartPreview(void *pDcmipp)
{
    UNUSED(pDcmipp);
    // printf("Camera_StartPreview\r\n");
    return ISP_OK;
}   

/**
* @brief  Helper for ISP to stop camera preview
* @param  hDcmipp Pointer to the dcmipp device
* @retval ISP status operation
*/
ISP_StatusTypeDef Camera_StopPreview(void *pDcmipp)
{
    UNUSED(pDcmipp);
    // printf("Camera_StopPreview\r\n");
    return ISP_OK;
}

#ifdef ISP_ENABLE_UVC
/* UVC frame buffer for RGB888 -> YUV422 (YUYV) conversion */
static uint8_t *uvc_frame_buf = NULL;
static uint32_t uvc_frame_buf_size = 0;

typedef struct {
    osThreadId_t thread_id;
    osSemaphoreId_t sem;
    uint8_t *pipe2_buf;
    int pipe2_size;
} uvc_task_ctx_t;

static uvc_task_ctx_t g_uvc_ctx = {0};

/* Clamp helper for conversion */
static inline uint8_t clamp_u8(int32_t v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

/* Convert a RGB888 frame to YUV422 (YUYV) format.
 * src: RGB888 buffer (3 bytes per pixel)
 * dst: YUYV buffer (4 bytes per 2 pixels)
 * width, height: frame resolution
 */
static void rgb888_to_yuv422_yuyv(const uint8_t *src, uint8_t *dst, uint32_t width, uint32_t height)
{
    const uint32_t pixel_count = width * height;
    uint32_t i = 0;
    uint32_t dst_index = 0;

    while (i + 1U < pixel_count) {
        uint32_t src_index0 = i * 3U;
        uint32_t src_index1 = (i + 1U) * 3U;

        int32_t r0 = src[src_index0 + 0];
        int32_t g0 = src[src_index0 + 1];
        int32_t b0 = src[src_index0 + 2];

        int32_t r1 = src[src_index1 + 0];
        int32_t g1 = src[src_index1 + 1];
        int32_t b1 = src[src_index1 + 2];

        /* Fixed-point BT.601 conversion */
        int32_t y0 = ( 77 * r0 + 150 * g0 +  29 * b0) >> 8;
        int32_t y1 = ( 77 * r1 + 150 * g1 +  29 * b1) >> 8;
        int32_t u  = ((-43 * r0 -  85 * g0 + 128 * b0) >> 8) + 128;
        int32_t v  = ((128 * r0 - 107 * g0 -  21 * b0) >> 8) + 128;

        dst[dst_index + 0] = clamp_u8(y0);
        dst[dst_index + 1] = clamp_u8(u);
        dst[dst_index + 2] = clamp_u8(y1);
        dst[dst_index + 3] = clamp_u8(v);

        dst_index += 4U;
        i += 2U;
    }

    /* Handle odd pixel count: duplicate last pixel chroma */
    if (i < pixel_count) {
        uint32_t src_index = i * 3U;
        int32_t r = src[src_index + 0];
        int32_t g = src[src_index + 1];
        int32_t b = src[src_index + 2];

        int32_t y = ( 77 * r + 150 * g +  29 * b) >> 8;
        int32_t u = ((-43 * r -  85 * g + 128 * b) >> 8) + 128;
        int32_t v = ((128 * r - 107 * g -  21 * b) >> 8) + 128;

        dst[dst_index + 0] = clamp_u8(y);
        dst[dst_index + 1] = clamp_u8(u);
        dst[dst_index + 2] = clamp_u8(y);
        dst[dst_index + 3] = clamp_u8(v);
    }
}

static void uvcSendTask(void *argument)
{
    (void)argument;

    for (;;) {
        if (osSemaphoreAcquire(g_uvc_ctx.sem, osWaitForever) != osOK) {
            continue;
        }

        uint8_t *pipe2_buf = g_uvc_ctx.pipe2_buf;
        int buf_size = g_uvc_ctx.pipe2_size;

        if (pipe2_buf == NULL || buf_size <= 0 ||
            g_camera.pipe2_param.width <= 0 || g_camera.pipe2_param.height <= 0) {
            g_uvc_ctx.pipe2_buf = NULL;
            g_uvc_ctx.pipe2_size = 0;
            continue;
        }

        uint32_t width = (uint32_t)g_camera.pipe2_param.width;
        uint32_t height = (uint32_t)g_camera.pipe2_param.height;
        uint32_t required_size = width * height * 2U; /* YUYV: 2 bytes per pixel */

        if (uvc_frame_buf == NULL || uvc_frame_buf_size < required_size) {
            if (uvc_frame_buf != NULL) {
                hal_mem_free(uvc_frame_buf);
                uvc_frame_buf = NULL;
                uvc_frame_buf_size = 0;
            }
            uvc_frame_buf = hal_mem_alloc_aligned(required_size,
                                                  CAMERA_MEMORY_ALIGNMENT,
                                                  MEM_LARGE);
            if (uvc_frame_buf == NULL) {
                LOG_DRV_ERROR("UVC frame buffer alloc failed (size=%lu)\r\n",
                              (unsigned long)required_size);
                device_ioctl(g_camera.dev, CAM_CMD_RETURN_PIPE2_BUFFER, pipe2_buf, 0);
                g_uvc_ctx.pipe2_buf = NULL;
                g_uvc_ctx.pipe2_size = 0;
                continue;
            }
            uvc_frame_buf_size = required_size;
        }

        rgb888_to_yuv422_yuyv(pipe2_buf, uvc_frame_buf, width, height);

        int uvc_ret = usb_uvc_show_frame(uvc_frame_buf, (int)required_size);
        if (uvc_ret != 0) {
            LOG_DRV_DEBUG("usb_uvc_show_frame returned %d\r\n", uvc_ret);
        }

        device_ioctl(g_camera.dev, CAM_CMD_RETURN_PIPE2_BUFFER, pipe2_buf, 0);

        g_uvc_ctx.pipe2_buf = NULL;
        g_uvc_ctx.pipe2_size = 0;
    }
}
#endif

static uint8_t *isp_tool_buf = NULL;

/**
* @brief  Helper for ISP to dump a frame
* @param  hDcmipp Pointer to the dcmipp device
* @param  Pipe    Pipe where to perform the dump ('DUMP'(0) or 'ANCILLARY'(2))
* @param  Config  Dump with the current pipe config, or without downsizing with
*                 a specific pixel format.
* @param  pBuffer Pointer to the address of the dumped buffer (output parameter)
* @param  pMeta   Pointer to buffer meta data (output parameter)
* @retval ISP status operation
*/
ISP_StatusTypeDef Camera_DumpFrame(void *pDcmipp, uint32_t Pipe, ISP_DumpCfgTypeDef Config,
uint32_t **pBuffer, ISP_DumpFrameMetaTypeDef *pMeta)
{
    int ret = 0, try_times = 0, isp_tool_buf_index = 0;
    uint8_t *fb_buffer = NULL;
    DCMIPP_HandleTypeDef *pHdcmipp = (DCMIPP_HandleTypeDef*)pDcmipp;
    /* Check handle validity */
    if ((pHdcmipp == NULL) || (pBuffer == NULL) || (pMeta == NULL))
    {
        return ISP_ERR_EINVAL;
    }

    // printf("Camera_DumpFrame\r\n");
    // printf("Pipe: %d\r\n", Pipe);
    // printf("Config: %d\r\n", Config);

    if (isp_tool_buf == NULL) {
        isp_tool_buf = hal_mem_alloc_aligned(PIPE1_DEFAULT_WIDTH * PIPE1_DEFAULT_HEIGHT * 3, CAMERA_MEMORY_ALIGNMENT, MEM_LARGE);
        if (isp_tool_buf == NULL) {
            return ISP_ERR_DCMIPP_NOMEM;
        }
    }
    
    if (g_camera.dev == NULL) {
        return ISP_ERR_STAT_EINVAL;
    }

    if (g_camera.state.pipe1_state != PIPE_START) {
        ret = device_ioctl(g_camera.dev, CAM_CMD_SET_PIPE1_START, NULL, 0);
        if (ret != AICAM_OK) {
            return ISP_ERR_DCMIPP_STATE;
        }
    }

    do {
        ret = device_ioctl(g_camera.dev, CAM_CMD_GET_PIPE1_BUFFER, (uint8_t *)&fb_buffer, 0);
        if (ret <= 0) {
            if (++try_times > 10) {
                return ISP_ERR_DCMIPP_FRAMESIZE;
            }
            osDelay(1);
        }
    } while (ret <= 0);

    printf("fb_buffer: %p\r\n", fb_buffer);
    if (ret > PIPE1_DEFAULT_WIDTH * PIPE1_DEFAULT_HEIGHT * PIPE1_DEFAULT_BPP) {
        device_ioctl(g_camera.dev, CAM_CMD_RETURN_PIPE1_BUFFER, fb_buffer, 0);
        return ISP_ERR_DCMIPP_FRAMESIZE;
    }

    if (PIPE1_DEFAULT_BPP == 4) {
        // ARGB8888 to RGB888
        for (int i = 0; i < ret; i += 4) {
            isp_tool_buf[isp_tool_buf_index++] = fb_buffer[i];
            isp_tool_buf[isp_tool_buf_index++] = fb_buffer[i + 1];
            isp_tool_buf[isp_tool_buf_index++] = fb_buffer[i + 2];
        }
    } else if (PIPE1_DEFAULT_BPP == 2) {
        // RGB565 to RGB888
        uint16_t rgb565 = 0;
        uint8_t r5 = 0, g6 = 0, b5 = 0;
        for (int i = 0; i < ret; i += 2) {
            rgb565 = (fb_buffer[i + 1] << 8) | fb_buffer[i];
            b5 = (rgb565 >> 11) & 0x1f;
            g6 = (rgb565 >> 5) & 0x3f;
            r5 = (rgb565 >> 0) & 0x1f;
            isp_tool_buf[isp_tool_buf_index++] = (r5 << 3) | (r5 >> 2);
            isp_tool_buf[isp_tool_buf_index++] = (g6 << 2) | (g6 >> 4);
            isp_tool_buf[isp_tool_buf_index++] = (b5 << 3) | (b5 >> 2);
        }
    } else {
        memcpy(isp_tool_buf, fb_buffer, ret);
    }
    device_ioctl(g_camera.dev, CAM_CMD_RETURN_PIPE1_BUFFER, fb_buffer, 0);
    *pBuffer = (uint32_t *)isp_tool_buf;
    pMeta->width = g_camera.pipe1_param.width;
    pMeta->height = g_camera.pipe1_param.height;
    pMeta->pitch = g_camera.pipe1_param.width * 3;
    pMeta->size = pMeta->height * pMeta->pitch;
    pMeta->format = ISP_FORMAT_RGB888;
    
    // printf("Dumped frame meta: width=%d, height=%d, pitch=%d, size=%d, format=%d\r\n",
        // pMeta->width, pMeta->height, pMeta->pitch, pMeta->size, pMeta->format);
    return ISP_OK;
}

ISP_AppliHelpersTypeDef appliHelpers = {
    .GetSensorInfo = GetSensorInfo,
    .SetSensorGain = SetSensorGain,
    .GetSensorGain = GetSensorGain,
    .SetSensorExposure = SetSensorExposure,
    .GetSensorExposure = GetSensorExposure,
    .StartPreview = Camera_StartPreview,
    .StopPreview = Camera_StopPreview,
    .DumpFrame = Camera_DumpFrame,
    .SetSensorTestPattern = SetSensorTestPattern,
};
#endif


void buffer_reset(pipe_buffer_t *bufs, int nb, camera_dq_t *dq)
{
    for (int i = 0; i < nb; ++i) {
        for (int j = 0; j < CAMERA_BUF_MAX_OWNERS; ++j) bufs[i].owner_list[i] = NULL;
        bufs[i].owner_count = 0;
        bufs[i].return_count = 0;
        bufs[i].is_locked = 0;
        bufs[i].frame_id = 0;
        bufs[i].state = BUFFER_IDLE;
    }
}

pipe_buffer_t* buffer_acquire(pipe_buffer_t *bufs, int nb, camera_dq_t *dq)
{
    pipe_buffer_t* latest = NULL;
    uint32_t min_frame_id = 0xFFFFFFFFU;

    for (int i = 0; i < nb; ++i) {
        if (bufs[i].state == BUFFER_IDLE) {
            bufs[i].state = BUFFER_PROCESSING;
            return &bufs[i];
        }
    }

    if (g_camera.mtx_isr == 0) {
        for (int i = 0; i < nb; ++i) {
            if (bufs[i].state == BUFFER_READY) {
                if (bufs[i].frame_id < min_frame_id) {
                    latest = &bufs[i];
                    min_frame_id = bufs[i].frame_id;
                }
            }
        }

        if (latest != NULL) latest->state = BUFFER_PROCESSING;
    }
    
    return latest;
}

pipe_buffer_t* find_processing_buffer(pipe_buffer_t *bufs, int nb)
{
    for (int i = 0; i < nb; ++i) {
        if (bufs[i].state == BUFFER_PROCESSING) {
            return &bufs[i];
        }
    }
    return NULL;
}

void buffer_set_ready_isr(pipe_buffer_t *bufs, camera_dq_t *dq, pipe_buffer_t* buf, uint32_t frame_id)
{
    buf->frame_id = frame_id;
    buf->owner_count = 0;
    buf->return_count = 0;
    buf->is_locked = 0;
    buf->state = BUFFER_READY;
}

void buffer_release_isr(pipe_buffer_t *buf, camera_dq_t *dq)
{
    // printf("release buffer(%p) frame_id=%ld\r\n", buf, buf->frame_id);
    for (int i = 0; i < CAMERA_BUF_MAX_OWNERS; ++i) buf->owner_list[i] = NULL;
    buf->owner_count = 0;
    buf->return_count = 0;
    buf->is_locked = 0;
    buf->state = BUFFER_IDLE;
}

pipe_buffer_t* buffer_get_latest_ready(pipe_buffer_t *bufs, int nb, camera_dq_t *dq)
{
    osThreadId_t requester = osThreadGetId();
    pipe_buffer_t* latest = NULL;
    uint32_t max_frame_id = 0;

    for (int i = 0; i < nb; ++i) {
        if (bufs[i].state >= BUFFER_READY && !bufs[i].is_locked) {
            if (latest == NULL || bufs[i].frame_id > max_frame_id) {
                latest = &bufs[i];
                max_frame_id = bufs[i].frame_id;
            }
        }
    }
    if (latest) {
        if (latest->state == BUFFER_READY) {
            latest->state = BUFFER_IN_USE;
            latest->owner_list[latest->owner_count] = requester;
        } else if (latest->state == BUFFER_IN_USE && latest->owner_count < CAMERA_BUF_MAX_OWNERS) {
            for (int j = 0; j < latest->owner_count; ++j) {
                if (latest->owner_list[j] == requester) {   // already owned
                    latest = NULL;
                    break;
                }
            }
        } else {
            latest = NULL;
        }
        if (latest != NULL) latest->owner_count++;
    }
    // printf("latest buffer(%p) frame_id=%ld, owner_count=%d, requester=%p\r\n", latest, latest ? latest->frame_id : -1, latest ? latest->owner_count : -1, requester);
    return latest;
}


static void camera_copy_default_isp_iq(CMW_Sensor_Name_t sensor_id, ISP_IQParamTypeDef *dst)
{
    if (!dst) {
        return;
    }
    switch (sensor_id) {
        case CMW_IMX335_Sensor:
            memcpy(dst, &ISP_IQParamCacheInit_IMX335, sizeof(ISP_IQParamTypeDef));
            break;
        case CMW_VD66GY_Sensor:
            memcpy(dst, &ISP_IQParamCacheInit_VD66GY, sizeof(ISP_IQParamTypeDef));
            break;
        case CMW_OS04C10_Sensor:
            memcpy(dst, &ISP_IQParamCacheInit_OS04C10, sizeof(ISP_IQParamTypeDef));
            break;
        default:
            memcpy(dst, &ISP_IQParamCacheInit_OS04C10, sizeof(ISP_IQParamTypeDef));
            break;
    }
}

void camera_fill_isp_iq_scene(cam_iq_scene_t scene, ISP_IQParamTypeDef *out_iq)
{
    if (!out_iq) {
        return;
    }
    CMW_Sensor_Name_t sensor = CMW_UNKNOWN_Sensor;
    if (CMW_CAMERA_GetSensorName(&sensor) != CMW_ERROR_NONE) {
        memset(out_iq, 0, sizeof(ISP_IQParamTypeDef));
        return;
    }
    camera_copy_default_isp_iq(sensor, out_iq);
    /* Indoor: IQTune snapshot — contrast curve + statistic region (OS04C10 full frame). */
    if (scene == CAM_IQ_SCENE_INDOOR) {
        out_iq->contrast.enable = 1;
        /* Contrast strength: IQT unit x100 (e.g. 100 = 1.0), from IQTune "Contrast 1" / INDOOR */
        out_iq->contrast.coeff.LUM_0 = 50;
        out_iq->contrast.coeff.LUM_32 = 80;
        out_iq->contrast.coeff.LUM_64 = 94;
        out_iq->contrast.coeff.LUM_96 = 100;
        out_iq->contrast.coeff.LUM_128 = 100;
        out_iq->contrast.coeff.LUM_160 = 102;
        out_iq->contrast.coeff.LUM_192 = 110;
        out_iq->contrast.coeff.LUM_224 = 112;
        out_iq->contrast.coeff.LUM_256 = 120;
        if (sensor == CMW_OS04C10_Sensor) {
            out_iq->statAreaStatic.X0 = 109U;
            out_iq->statAreaStatic.Y0 = 92U;
            out_iq->statAreaStatic.XSize = 2418U;
            out_iq->statAreaStatic.YSize = 1329U;
        }
    }
}

#define CAMERA_ISP_COEFF_UNIT   100000000
#define CAMERA_LUMA_COEFF_R     29900000
#define CAMERA_LUMA_COEFF_G     58700000
#define CAMERA_LUMA_COEFF_B     11400000

void camera_apply_grayscale_iq(ISP_IQParamTypeDef *iq, aicam_bool_t grayscale)
{
    int row;

    if (iq == NULL) {
        return;
    }
    if (grayscale != AICAM_TRUE) {
        return;
    }

    iq->AWBAlgo.enable = 0;
    iq->colorConvStatic.enable = 1;
    for (row = 0; row < 3; row++) {
        iq->colorConvStatic.coeff[row][0] = CAMERA_LUMA_COEFF_R;
        iq->colorConvStatic.coeff[row][1] = CAMERA_LUMA_COEFF_G;
        iq->colorConvStatic.coeff[row][2] = CAMERA_LUMA_COEFF_B;
    }
    if (iq->ispGainStatic.enable) {
        iq->ispGainStatic.ispGainR = CAMERA_ISP_COEFF_UNIT;
        iq->ispGainStatic.ispGainG = CAMERA_ISP_COEFF_UNIT;
        iq->ispGainStatic.ispGainB = CAMERA_ISP_COEFF_UNIT;
    }
}

void camera_configure_pipe1_grayscale(pipe_params_t *pipe1, aicam_bool_t grayscale)
{
    (void)pipe1;
    (void)grayscale;
    /* Grayscale is applied via ISP (camera_apply_grayscale_iq); keep PIPE1 RGB565 so
     * video pipeline / encoder stride (bpp=2) stays consistent. */
}

static void CAM_setSensorInfo(CMW_Sensor_Name_t sensor, camera_t *camera)
{
    CMW_Sensor_Name_t sensor_id = sensor;
    switch (sensor_id) {
        case CMW_VD66GY_Sensor:
            camera->sensor_param.width = SENSOR_VD66GY_WIDTH;
            camera->sensor_param.height = SENSOR_VD66GY_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_VD66GY_FLIP;
            camera->sensor_param.name = sensor_names[1];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        case CMW_IMX335_Sensor:
            camera->sensor_param.width = SENSOR_IMX335_WIDTH;
            camera->sensor_param.height = SENSOR_IMX335_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_IMX335_FLIP;
            camera->sensor_param.name = sensor_names[2];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        case CMW_VD55G1_Sensor:
            camera->sensor_param.width = SENSOR_VD55G1_WIDTH;
            camera->sensor_param.height = SENSOR_VD55G1_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_VD55G1_FLIP;
            camera->sensor_param.name = sensor_names[3];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        case CMW_OS04C10_Sensor:
            camera->sensor_param.width = SENSOR_OS04C10_WIDTH;
            camera->sensor_param.height = SENSOR_OS04C10_HEIGHT;
            camera->sensor_param.mirror_flip = SENSOR_OS04C10_FLIP;
            camera->sensor_param.name = sensor_names[4];
            camera->sensor_param.fps = CAMERA_FPS;
            break;
        default:
            break;
            
    }
    LOG_DRV_DEBUG("Detected %s \r\n", camera->sensor_param.name);
    LOG_DRV_DEBUG("Sensor Image: %dx%d, MirrorFlip: %d ",
    camera->sensor_param.width, camera->sensor_param.height, camera->sensor_param.mirror_flip);

    // Initialize ISP IQ parameters based on detected sensor (stock / indoor profile)
    camera_copy_default_isp_iq(sensor_id, &camera->isp_iq_param);
}

/* Keep display output aspect ratio using crop area */
static void CAM_InitCropConfig(CMW_Manual_roi_area_t *roi, camera_t *camera)
{
    const float ratiox = (float)camera->sensor_param.width / camera->pipe1_param.width;
    const float ratioy = (float)camera->sensor_param.height / camera->pipe1_param.height;
    const float ratio = MIN(ratiox, ratioy);

    roi->width = (uint32_t) MIN(camera->pipe1_param.width * ratio, camera->sensor_param.width);
    roi->height = (uint32_t) MIN(camera->pipe1_param.height * ratio, camera->sensor_param.height);
    roi->offset_x = (camera->sensor_param.width - roi->width + 1) / 2;
    roi->offset_y = (camera->sensor_param.height - roi->height + 1) / 2;
}

HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    HAL_StatusTypeDef ret;

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP|RCC_PERIPHCLK_CSI;
    PeriphClkInitStruct.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
    RCC_IC_FillDCMIPP_PLL3_IC17_IC18(&PeriphClkInitStruct);
    ret = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
    if (ret)
        return ret;

    __HAL_RCC_DCMIPP_CLK_ENABLE();
    __HAL_RCC_CSI_CLK_ENABLE();
    __HAL_RCC_CSI_FORCE_RESET();
    __HAL_RCC_CSI_RELEASE_RESET();

    return HAL_OK;
}

static int DCMIPP_Pipe1Init(camera_t *camera)
{
    CMW_DCMIPP_Conf_t dcmipp_conf;
    uint32_t hw_pitch;
    int ret;

    dcmipp_conf.output_width = camera->pipe1_param.width;
    dcmipp_conf.output_height = camera->pipe1_param.height;
    dcmipp_conf.output_format = camera->pipe1_param.format;
    dcmipp_conf.output_bpp = camera->pipe1_param.bpp;
    dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
    dcmipp_conf.enable_swap = 0;
    dcmipp_conf.enable_gamma_conversion = 0;
    CAM_InitCropConfig(&dcmipp_conf.manual_conf, camera);
    ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE1, &dcmipp_conf, &hw_pitch);
    if(ret != HAL_OK || hw_pitch != dcmipp_conf.output_width * dcmipp_conf.output_bpp){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

static int DCMIPP_Pipe2Init(camera_t *camera)
{
    CMW_DCMIPP_Conf_t dcmipp_conf;
    uint32_t hw_pitch;
    int ret;

    dcmipp_conf.output_width = camera->pipe2_param.width;
    dcmipp_conf.output_height = camera->pipe2_param.height;
    dcmipp_conf.output_format = camera->pipe2_param.format;
    dcmipp_conf.output_bpp = camera->pipe2_param.bpp;
    dcmipp_conf.mode = CMW_Aspect_ratio_manual_roi;
    dcmipp_conf.enable_swap = 1;
    dcmipp_conf.enable_gamma_conversion = 0;
    CAM_InitCropConfig(&dcmipp_conf.manual_conf, camera);
    ret = CMW_CAMERA_SetPipeConfig(DCMIPP_PIPE2, &dcmipp_conf, &hw_pitch);
    if(ret != HAL_OK || hw_pitch != dcmipp_conf.output_width * dcmipp_conf.output_bpp){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

static int DCMIPP_IpPlugInit(DCMIPP_HandleTypeDef *hdcmipp)
{
    DCMIPP_IPPlugConfTypeDef ipplug_conf = { 0 };
    int ret;

    ipplug_conf.MemoryPageSize = DCMIPP_MEMORY_PAGE_SIZE_256BYTES;

    ipplug_conf.Client = DCMIPP_CLIENT2; /* aux pipe */
    ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
    ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_NONE;
    ipplug_conf.DPREGStart = 0;
    ipplug_conf.DPREGEnd = 559; /* (4480 bytes / one line) */
    ipplug_conf.WLRURatio = 15; /* 16 parts of BW */
    ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }

    ipplug_conf.Client = DCMIPP_CLIENT5; /* main rgb pipe */
    ipplug_conf.Traffic = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
    ipplug_conf.MaxOutstandingTransactions = DCMIPP_OUTSTANDING_TRANSACTION_3;
    ipplug_conf.DPREGStart = 560;
    ipplug_conf.DPREGEnd = 639;
    ipplug_conf.WLRURatio = 0; /* 1 parts of BW */
    ret = HAL_DCMIPP_SetIPPlugConfig(hdcmipp, &ipplug_conf);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

static int DCMIPP_ReduceSpurious(DCMIPP_HandleTypeDef *hdcmipp)
{
    int ret;

    ret = HAL_DCMIPP_PIPE_EnableLineEvent(hdcmipp, DCMIPP_PIPE1, DCMIPP_MULTILINE_128_LINES);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }
    ret = HAL_DCMIPP_PIPE_DisableLineEvent(hdcmipp, DCMIPP_PIPE1);
    if(ret != HAL_OK){
        return CMW_ERROR_WRONG_PARAM;
    }
    return CMW_ERROR_NONE;
}

int CAM_Init(camera_t *camera)
{
    CMW_CameraInit_t cam_conf = {0};
    CMW_Sensor_Name_t sensor;
    ISP_SensorInfoTypeDef info = {0};
    int ret;

    /* Let camera middleware auto-configure sensor; provide basic runtime preferences */
    cam_conf.width = 0;
    cam_conf.height = 0;
    cam_conf.fps = CAMERA_FPS;
    cam_conf.mirror_flip = CMW_MIRRORFLIP_NONE;

    ret = CMW_CAMERA_Init(&cam_conf, NULL);
    if(ret != CMW_ERROR_NONE){
        return ret;
    }

    /* Query actual sensor resolution from middleware */
    ret = CMW_CAMERA_GetSensorInfo(&info);
    if (ret != CMW_ERROR_NONE){
        return ret;
    }
    sensor_width = (int)info.width;
    sensor_height = (int)info.height;

    ret = CMW_CAMERA_GetSensorName(&sensor);
    if(ret != CMW_ERROR_NONE){
        return ret;
    }
    CAM_setSensorInfo(sensor, camera);

    DCMIPP_IpPlugInit(CMW_CAMERA_GetDCMIPPHandle());
    DCMIPP_Pipe1Init(camera);
    DCMIPP_Pipe2Init(camera);
    DCMIPP_ReduceSpurious(CMW_CAMERA_GetDCMIPPHandle());
    return CMW_ERROR_NONE;
}

static void main_pipe_frame_event()
{
    int ret;
    pipe_buffer_t *buffer,*buffer1;
    bool frame_is_valid = (g_camera.skip_frame_counter == 0);
    
    // check pipe state and buffer validity
    if(g_camera.pipe1_buffer == NULL || g_camera.state.pipe1_state != PIPE_START){
        return;
    }

    if(g_camera.skip_frame_counter > 0){
        g_camera.skip_frame_counter--;
    }
    
    buffer1 = find_processing_buffer(g_camera.pipe1_buffer, g_camera.pipe1_param.buffer_nb);
    if(buffer1 != NULL){
        if(frame_is_valid){
            buffer_set_ready_isr(g_camera.pipe1_buffer, &g_camera.pipe1_dq, buffer1, g_camera.current_frame_id);
        }
        else{
            buffer_release_isr(buffer1, &g_camera.pipe1_dq);
        }
    }

    buffer = buffer_acquire(g_camera.pipe1_buffer, g_camera.pipe1_param.buffer_nb, &g_camera.pipe1_dq);
    if(buffer != NULL && buffer->data != NULL){
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE1,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer->data);
        if(ret == HAL_OK){
            if(frame_is_valid){
                osSemaphoreRelease(g_camera.sem_pipe1);
            }
        }else{
            buffer_release_isr(buffer, &g_camera.pipe1_dq);
        }
    }else if(buffer1 != NULL && buffer1->data != NULL){
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE1,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer1->data);
        if(ret == HAL_OK){
            if(frame_is_valid){
                osSemaphoreRelease(g_camera.sem_pipe1);
            }
        }
    }
}

static void ancillary_pipe_frame_event()
{
    pipe_buffer_t *buffer,*buffer1;
    int ret;
    bool frame_is_valid = (g_camera.skip_frame_counter == 0);

    // check pipe state and buffer validity
    if(g_camera.pipe2_buffer == NULL || g_camera.state.pipe2_state != PIPE_START){
        return;
    }

    // Use the same frame_id as PIPE1 to keep synchronization
    buffer1 = find_processing_buffer(g_camera.pipe2_buffer, g_camera.pipe2_param.buffer_nb);
    if(buffer1 != NULL){
        if(frame_is_valid){
            buffer_set_ready_isr(g_camera.pipe2_buffer, &g_camera.pipe2_dq, buffer1, g_camera.current_frame_id);
        }
        else{
            buffer_release_isr(buffer1, &g_camera.pipe2_dq);
        }
    }

    buffer = buffer_acquire(g_camera.pipe2_buffer, g_camera.pipe2_param.buffer_nb, &g_camera.pipe2_dq);
    if(buffer != NULL && buffer->data != NULL){
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE2,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer->data);
        if(ret == HAL_OK){
            if(frame_is_valid){
                osSemaphoreRelease(g_camera.sem_pipe2);
            }
        }else{
            buffer_release_isr(buffer, &g_camera.pipe2_dq);
        }
    }else if(buffer1 != NULL && buffer1->data != NULL){
        ret = HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(), DCMIPP_PIPE2,
                                                DCMIPP_MEMORY_ADDRESS_0, (uint32_t) buffer1->data);
        if(ret == HAL_OK){
            if(frame_is_valid){
                osSemaphoreRelease(g_camera.sem_pipe2);
            }
        }
    }
}

static void app_main_pipe_vsync_event()
{
    osSemaphoreRelease(g_camera.sem_isp);
}

int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
    if (pipe == DCMIPP_PIPE1)
        main_pipe_frame_event();
    else if (pipe == DCMIPP_PIPE2){
        ancillary_pipe_frame_event();
    }

    return HAL_OK;
}

int CMW_CAMERA_PIPE_VsyncEventCallback(uint32_t pipe)
{
  if (pipe == DCMIPP_PIPE1) {
    g_camera.current_frame_id++;
    app_main_pipe_vsync_event();
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
    ISP_IncMainFrameId(&hIsp);
    ISP_GatherStatistics(&hIsp);
    ISP_OutputMeta(&hIsp);
#endif
  }
    
  return HAL_OK;
}

void CMW_CAMERA_PIPE_ErrorCallback(uint32_t pipe)
{
    /* Handle DCMIPP pipe error without asserting.
     * For now just log and keep running; detailed recovery can be added if needed. */
}

static int pipe_start_common(camera_t *camera, uint32_t pipe_id, pipe_buffer_t **pipe_buffer, 
                            pipe_params_t *pipe_param, camera_dq_t *dq, PIPE_STATE_E *pipe_state)
{
    LOG_DRV_DEBUG("camera pipe%lu start", pipe_id);
    int32_t ret = CMW_ERROR_NONE;
    pipe_buffer_t *buffer;
    
    if(!camera->is_init)
        return AICAM_ERROR_NOT_FOUND;
        
    if(*pipe_state == PIPE_STOP){
        *pipe_buffer = hal_mem_alloc_fast(sizeof(pipe_buffer_t) * pipe_param->buffer_nb);
        if(*pipe_buffer == NULL){
            return AICAM_ERROR_NO_MEMORY;
        }
        
        ret = pipe_buffer_acquire(*pipe_buffer, pipe_param, dq);
        if(ret != 0){
            LOG_DRV_ERROR("pipe%lu buffer acquire failed \r\n", pipe_id);
            pipe_buffer_release(*pipe_buffer, pipe_param, dq);
            hal_mem_free(*pipe_buffer);
            *pipe_buffer = NULL;
            return AICAM_ERROR_NO_MEMORY;
        }
        
        buffer = buffer_acquire(*pipe_buffer, pipe_param->buffer_nb, dq);
        if(buffer != NULL){
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
            if (pipe_id == DCMIPP_PIPE2) {
                #ifdef ISP_ENABLE_UVC
                    /* Define the preview size and fps for the UVC streaming */
                    usb_uvc_init(pipe_param->width, pipe_param->height, pipe_param->fps);
                #endif
            
                if(!isp_is_init){
                    (void) ISP_IQParamCacheInit; /* unused */
                    ret = ISP_Init(&hIsp, &hcamera_dcmipp, 0, &appliHelpers, &camera->isp_iq_param);
                    if (ret) LOG_DRV_ERROR("ISP_Init error: %d\r\n", ret);
                    isp_is_init = 1;
                }
                if(!isp_is_start){
                    ret = ISP_Start(&hIsp);
                    if (ret) LOG_DRV_ERROR("ISP start failed: %d\r\n", ret);
                    isp_is_start = 1;
                }
            }
#endif
            camera->skip_frame_counter = camera->startup_skip_frames;
            // clear possible residual hardware error flags before startup
            DCMIPP_HandleTypeDef *hdcmipp = CMW_CAMERA_GetDCMIPPHandle();
            if (hdcmipp != NULL) {
                // clear Pipe Overrun flag
                if (pipe_id == DCMIPP_PIPE1) {
                    __HAL_DCMIPP_CLEAR_FLAG(hdcmipp, DCMIPP_FLAG_PIPE1_OVR);
                } else if (pipe_id == DCMIPP_PIPE2) {
                    __HAL_DCMIPP_CLEAR_FLAG(hdcmipp, DCMIPP_FLAG_PIPE2_OVR);
                }
                
                // clear AXI Transfer Error flag
                __HAL_DCMIPP_CLEAR_FLAG(hdcmipp, DCMIPP_FLAG_AXI_TRANSFER_ERROR);
                
                // clear CSI error flags (CRC, Sync, Watchdog, etc.)
                // CSI is a CMSIS macro, pointing to the CSI register base address
                __HAL_DCMIPP_CSI_CLEAR_FLAG(CSI_S, DCMIPP_CSI_FLAG_CRCERR | 
                                                  DCMIPP_CSI_FLAG_SYNCERR | 
                                                  DCMIPP_CSI_FLAG_WDERR | 
                                                  DCMIPP_CSI_FLAG_SPKTERR | 
                                                  DCMIPP_CSI_FLAG_IDERR);
            }
            ret = CMW_CAMERA_Start(pipe_id, buffer->data, CMW_MODE_CONTINUOUS);
            if(ret == CMW_ERROR_NONE){
                *pipe_state = PIPE_START;
                return AICAM_OK;
            }else{
                // Clean up resources on startup failure
                buffer_release_isr(buffer, dq);
                pipe_buffer_release(*pipe_buffer, pipe_param, dq);
                hal_mem_free(*pipe_buffer);
                *pipe_buffer = NULL;
                LOG_DRV_ERROR("pipe%lu start failed: %d\r\n", pipe_id, ret);
                return AICAM_ERROR;
            }
        }else{
            // Clean up resources when buffer cannot be acquired
            pipe_buffer_release(*pipe_buffer, pipe_param, dq);
            hal_mem_free(*pipe_buffer);
            *pipe_buffer = NULL;
            return AICAM_ERROR_BUSY;
        }
    }
    LOG_DRV_DEBUG("pipe%lu already start \r\n", pipe_id);
    return AICAM_OK;
}

static int pipe1_start(camera_t *camera)
{
    return pipe_start_common(camera, DCMIPP_PIPE1, &camera->pipe1_buffer, 
                           &camera->pipe1_param, &camera->pipe1_dq, &camera->state.pipe1_state);
}

// Common pipe stop function
static int pipe_stop_common(camera_t *camera, uint32_t pipe_id, pipe_buffer_t **pipe_buffer, 
                           pipe_params_t *pipe_param, camera_dq_t *dq, PIPE_STATE_E *pipe_state)
{
    LOG_DRV_DEBUG("camera pipe%lu stop", pipe_id);
    int32_t ret = HAL_OK;
    DCMIPP_HandleTypeDef *hdcmipp = CMW_CAMERA_GetDCMIPPHandle();
    
    if(!camera->is_init)
        return AICAM_ERROR_NOT_FOUND;
        
    if(*pipe_state == PIPE_START){
        // disable frame interrupt
        if (hdcmipp != NULL) {
            if (pipe_id == DCMIPP_PIPE1) {
                __HAL_DCMIPP_DISABLE_IT(hdcmipp, DCMIPP_IT_PIPE1_FRAME | DCMIPP_IT_PIPE1_VSYNC);
            } else if (pipe_id == DCMIPP_PIPE2) {
                __HAL_DCMIPP_DISABLE_IT(hdcmipp, DCMIPP_IT_PIPE2_FRAME | DCMIPP_IT_PIPE2_VSYNC);
            }
        }
        
        // stop pipe
        ret = HAL_DCMIPP_CSI_PIPE_Stop(hdcmipp, pipe_id, DCMIPP_VIRTUAL_CHANNEL0);
        if(ret == HAL_OK){
            // clear memory address register, prevent DCMIPP from accessing released memory
            if (hdcmipp != NULL) {
                if (pipe_id == DCMIPP_PIPE1) {
                    WRITE_REG(hdcmipp->Instance->P1PPM0AR1, 0U);
                    if ((hdcmipp->Instance->P1PPCR & DCMIPP_P1PPCR_DBM) == DCMIPP_P1PPCR_DBM) {
                        WRITE_REG(hdcmipp->Instance->P1PPM0AR2, 0U);
                    }
                } else if (pipe_id == DCMIPP_PIPE2) {
                    WRITE_REG(hdcmipp->Instance->P2PPM0AR1, 0U);
                    if ((hdcmipp->Instance->P2PPCR & DCMIPP_P2PPCR_DBM) == DCMIPP_P2PPCR_DBM) {
                        WRITE_REG(hdcmipp->Instance->P2PPM0AR2, 0U);
                    }
                }
                
                // clear AXI Transfer Error flag
                __HAL_DCMIPP_CLEAR_FLAG(hdcmipp, DCMIPP_FLAG_AXI_TRANSFER_ERROR);
            }
            
            // update state and release resources
            buffer_reset(*pipe_buffer, pipe_param->buffer_nb, dq);
            *pipe_state = PIPE_STOP;
            pipe_buffer_release(*pipe_buffer, pipe_param, dq);
            hal_mem_free(*pipe_buffer);
            *pipe_buffer = NULL;
            return AICAM_OK;
        }else{
            LOG_DRV_ERROR("pipe%lu stop failed: %d\r\n", pipe_id, ret);
            return AICAM_ERROR;
        }
    }
    LOG_DRV_DEBUG("pipe%lu already stop \r\n", pipe_id);
    return AICAM_OK;
}

static int pipe1_stop(camera_t *camera)
{
    return pipe_stop_common(camera, DCMIPP_PIPE1, &camera->pipe1_buffer, 
                          &camera->pipe1_param, &camera->pipe1_dq, &camera->state.pipe1_state);
}

static int pipe2_start(camera_t *camera)
{
    return pipe_start_common(camera, DCMIPP_PIPE2, &camera->pipe2_buffer, 
                           &camera->pipe2_param, &camera->pipe2_dq, &camera->state.pipe2_state);
}

static int pipe2_stop(camera_t *camera)
{
    return pipe_stop_common(camera, DCMIPP_PIPE2, &camera->pipe2_buffer, 
                          &camera->pipe2_param, &camera->pipe2_dq, &camera->state.pipe2_state);
}

static int camera_start(void *priv)
{
    LOG_DRV_DEBUG("camera_start \r\n");
    camera_t *camera = (camera_t *)priv;
    int ret;
    if(!camera->is_init){
        if (camera->sem_init == NULL || osSemaphoreAcquire(camera->sem_init, 2000) != osOK || !camera->is_init) {
            return AICAM_ERROR_NOT_FOUND;
        }
    }

    osMutexAcquire(camera->mtx_id, osWaitForever);
    camera->mtx_isr = 1;
    if(camera->state.camera_state == CAMERA_START){
        LOG_DRV_DEBUG("camera already start \r\n");
        camera->mtx_isr = 0;
        osMutexRelease(camera->mtx_id);
        return AICAM_OK;
    }

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
    // Set ISP initialization parameters before starting camera
    CMW_CAMERA_SetISPInitParam(&camera->isp_iq_param);
#endif

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE1_BIT) != 0){
        ret = pipe1_start(camera);
        if(ret != AICAM_OK){
            camera->mtx_isr = 0;
            osMutexRelease(camera->mtx_id);
            return ret;
        }
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE2_BIT) != 0){
        ret = pipe2_start(camera);
        if(ret != AICAM_OK){
            camera->mtx_isr = 0;
            osMutexRelease(camera->mtx_id);
            return ret;
        }
    }

    camera->state.camera_state = CAMERA_START;
    camera->mtx_isr = 0;
    osMutexRelease(camera->mtx_id);
    return AICAM_OK;
}

static int camera_stop(void *priv)
{
    LOG_DRV_DEBUG("camera_stop \r\n");
    camera_t *camera = (camera_t *)priv;
    if(!camera->is_init)
        return AICAM_ERROR_NOT_FOUND;

    osMutexAcquire(camera->mtx_id, osWaitForever);
    camera->mtx_isr = 1;
    if(camera->state.camera_state == CAMERA_STOP){
        LOG_DRV_DEBUG("camera already stop \r\n");
        camera->mtx_isr = 0;
        osMutexRelease(camera->mtx_id);
        return AICAM_OK;
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE1_BIT) != 0){
        pipe1_stop(camera);
    }

    if((camera->device_ctrl_pipe & CAMERA_CTRL_PIPE2_BIT) != 0){
        pipe2_stop(camera);
    }

    camera->state.camera_state = CAMERA_STOP;
    camera->mtx_isr = 0;
    osMutexRelease(camera->mtx_id);
    return AICAM_OK;
}

static void cameraProcess(void *argument)
{
    camera_t *camera = (camera_t *)argument;
    int ret;

#if CAMERA_TASK_DELAY_MS > 0
    osDelay(CAMERA_TASK_DELAY_MS);
#endif
    ret = CAM_Init(camera);
    // printf("camera init end, %lu ms\r\n", HAL_GetTick());
    if(ret != CMW_ERROR_NONE){
        pwr_manager_release(camera->pwr_handle);
        LOG_DRV_ERROR("camera init failed \r\n");
        osThreadExit();
        return;
    }
    camera->is_init = true;
    osSemaphoreRelease(camera->sem_init);
    while (camera->is_init) {
        if (osSemaphoreAcquire(camera->sem_isp, CAMERA_ISP_SEM_TIMEOUT_MS) == osOK) {
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
            if (isp_is_start) {
            #ifdef ISP_ENABLE_UVC
                /* When UVC is enabled, just fetch latest PIPE2 frame and notify
                 * UVC task. Conversion and usb_uvc_show_frame are done in the
                 * dedicated UVC thread. */
                if (g_camera.dev != NULL &&
                    g_camera.state.pipe2_state == PIPE_START &&
                    g_uvc_ctx.sem != NULL &&
                    g_uvc_ctx.pipe2_buf == NULL) {
                    uint8_t *pipe2_buf = NULL;
                    int buf_size = device_ioctl(g_camera.dev,
                                                CAM_CMD_GET_PIPE2_BUFFER,
                                                (uint8_t *)&pipe2_buf, 0);

                    if (buf_size > 0 && pipe2_buf != NULL) {
                        g_uvc_ctx.pipe2_buf = pipe2_buf;
                        g_uvc_ctx.pipe2_size = buf_size;
                        osSemaphoreRelease(g_uvc_ctx.sem);
                    }
                }
            #endif
                ret = ISP_BackgroundProcess(&hIsp);
                if (ret != ISP_OK) {
                    LOG_DRV_ERROR("ISP background process failed: %d\r\n", ret);
                }
            }
#else
            //osMutexAcquire(camera->mtx_id, osWaitForever);
            CMW_CAMERA_Run();
            //osMutexRelease(camera->mtx_id);
#endif
        }
        // if (g_camera.current_frame_id % 30 == 0) {
        //     printf("id: %ld\r\n", g_camera.current_frame_id);
        // }
    }
    osThreadExit();
}

static int camera_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    camera_t *camera = (camera_t *)priv;
    CAM_CMD_E cam_cmd = (CAM_CMD_E)cmd;
    pipe_buffer_t *buffer = NULL;
    uint8_t *new_buffer = NULL; // buffer to replace the unshared buffer
    int ret = AICAM_OK;
    int i = 0;

    if(!camera->is_init){
        if (camera->sem_init == NULL || osSemaphoreAcquire(camera->sem_init, 2000) != osOK || !camera->is_init) {
            return AICAM_ERROR_NOT_FOUND;
        }
    }
    osMutexAcquire(camera->mtx_id, osWaitForever);
    camera->mtx_isr = 1;
    switch (cam_cmd)
    {
        case CAM_CMD_SET_SENSOR_PARAM:
            if(ubuf == NULL || arg != sizeof(sensor_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            sensor_params_t temp_param;
            memcpy(&temp_param, ubuf, sizeof(sensor_params_t));

            if(temp_param.mirror_flip != camera->sensor_param.mirror_flip){
                if(CMW_CAMERA_SetMirrorFlip(temp_param.mirror_flip) != CMW_ERROR_NONE){
                    ret = AICAM_ERROR_INVALID_PARAM;
                    break;
                }else{
                    camera->sensor_param.mirror_flip = temp_param.mirror_flip;
                }
            }

            ret = AICAM_OK;
            break;
        
        case CAM_CMD_GET_SENSOR_PARAM:
            if(ubuf == NULL || arg != sizeof(sensor_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }

            memcpy(ubuf, &camera->sensor_param, sizeof(sensor_params_t));
            ret = AICAM_OK;
            break;

        case CAM_CMD_SET_ISP_PARAM:
            if(ubuf == NULL || arg != sizeof(ISP_IQParamTypeDef)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&camera->isp_iq_param, ubuf, sizeof(ISP_IQParamTypeDef));
            ret = AICAM_OK;
            // #include "crc.h"
            // uint32_t crc32 = CRC_Calculate(ubuf, sizeof(ISP_IQParamTypeDef));
            // printf("isp crc32: 0x%08lX\r\n", crc32);
            break;

        case CAM_CMD_GET_ISP_PARAM:
            if(ubuf == NULL || arg != sizeof(ISP_IQParamTypeDef)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &camera->isp_iq_param, sizeof(ISP_IQParamTypeDef));
            ret = AICAM_OK;
            break;

        case CAM_CMD_SET_PIPE_CTRL:
            if(camera->state.camera_state == CAMERA_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            camera->device_ctrl_pipe = *ubuf & (CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT);
            ret = AICAM_OK;
            break;
        case CAM_CMD_SET_PIPE1_PARAM:
            if(camera->state.pipe1_state != PIPE_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&camera->pipe1_param, ubuf, sizeof(pipe_params_t));
            ret = DCMIPP_Pipe1Init(camera);
            break;

        case CAM_CMD_SET_PIPE2_PARAM:
            if(camera->state.pipe2_state != PIPE_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(&camera->pipe2_param, ubuf, sizeof(pipe_params_t));
            ret = DCMIPP_Pipe2Init(camera);
            break;

        case CAM_CMD_GET_PIPE1_PARAM:
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &camera->pipe1_param, sizeof(pipe_params_t));
            ret = AICAM_OK;
            break;

        case CAM_CMD_GET_PIPE2_PARAM:
            if(ubuf == NULL || arg != sizeof(pipe_params_t)){
                ret = AICAM_ERROR_INVALID_PARAM;
                break;
            }
            memcpy(ubuf, &camera->pipe2_param, sizeof(pipe_params_t));
            ret = AICAM_OK;
            break;

        case CAM_CMD_SET_PIPE1_START:
            if(camera->state.camera_state == CAMERA_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe1_start(camera);
            break;

        case CAM_CMD_SET_PIPE1_STOP:
            if(camera->state.camera_state == CAMERA_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe1_stop(camera);
            break;

        case CAM_CMD_SET_PIPE2_START:
            if(camera->state.camera_state == CAMERA_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe2_start(camera);
            break;

        case CAM_CMD_SET_PIPE2_STOP:
            if(camera->state.camera_state == CAMERA_STOP){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            ret = pipe2_stop(camera);
            break;

        case CAM_CMD_GET_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            buffer = buffer_get_latest_ready(camera->pipe1_buffer, camera->pipe1_param.buffer_nb, &camera->pipe1_dq);
            if (buffer == NULL) {
                // Wait for skip frames to complete plus normal buffer timeout
                uint32_t timeout = CAMERA_BUFFER_TIMEOUT_MS;
                if (camera->skip_frame_counter > 0) {
                    timeout += (camera->skip_frame_counter * 1000 / camera->pipe1_param.fps) + CAMERA_BUFFER_TIMEOUT_MS;
                }
                osSemaphoreAcquire(camera->sem_pipe1, timeout);
                buffer = buffer_get_latest_ready(camera->pipe1_buffer, camera->pipe1_param.buffer_nb, &camera->pipe1_dq);
            }
            if (buffer != NULL) {
                *((unsigned char **)ubuf) = buffer->data;
                ret = camera->pipe1_param.width * camera->pipe1_param.height * camera->pipe1_param.bpp;
            } else {
                ret = AICAM_ERROR_NOT_FOUND;
            }
            break;
        
        case CAM_CMD_GET_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            if (camera->pipe2_param.extbuffer_flag == 1) {
                if(camera->pipe2_param.extbuffer != NULL) {
                    *((unsigned char **)ubuf) = camera->pipe2_param.extbuffer;
                    ret = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                } else {
                    ret = AICAM_ERROR_NOT_FOUND;
                }
            } else {
                buffer = buffer_get_latest_ready(camera->pipe2_buffer, camera->pipe2_param.buffer_nb, &camera->pipe2_dq);
                if (buffer == NULL) {
                    // Wait for skip frames to complete plus normal buffer timeout
                    uint32_t timeout = CAMERA_BUFFER_TIMEOUT_MS;
                    if (camera->skip_frame_counter > 0) {
                        timeout += (camera->skip_frame_counter * 1000 / camera->pipe2_param.fps) + CAMERA_BUFFER_TIMEOUT_MS;
                    }
                    osSemaphoreAcquire(camera->sem_pipe2, timeout);
                    buffer = buffer_get_latest_ready(camera->pipe2_buffer, camera->pipe2_param.buffer_nb, &camera->pipe2_dq);
                }
                if (buffer != NULL) {
                    *((unsigned char **)ubuf) = buffer->data;
                    ret = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                } else {
                    ret = AICAM_ERROR_NOT_FOUND;
                }
            }
            break;

        case CAM_CMD_GET_PIPE1_BUFFER_WITH_FRAME_ID:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            buffer = buffer_get_latest_ready(camera->pipe1_buffer, camera->pipe1_param.buffer_nb, &camera->pipe1_dq);
            if (buffer == NULL) {
                // Wait for skip frames to complete plus normal buffer timeout
                uint32_t timeout = CAMERA_BUFFER_TIMEOUT_MS;
                if (camera->skip_frame_counter > 0) {
                    timeout += (camera->skip_frame_counter * 1000 / camera->pipe1_param.fps) + CAMERA_BUFFER_TIMEOUT_MS;
                }
                osSemaphoreAcquire(camera->sem_pipe1, timeout);
                buffer = buffer_get_latest_ready(camera->pipe1_buffer, camera->pipe1_param.buffer_nb, &camera->pipe1_dq);
            }
            if (buffer != NULL) {
                camera_buffer_with_frame_id_t *result = (camera_buffer_with_frame_id_t *)ubuf;
                result->buffer = buffer->data;
                result->frame_id = buffer->frame_id;
                result->size = camera->pipe1_param.width * camera->pipe1_param.height * camera->pipe1_param.bpp;
                ret = AICAM_OK;
            } else {
                ret = AICAM_ERROR_NOT_FOUND;
            }
            break;

        case CAM_CMD_GET_PIPE2_BUFFER_WITH_FRAME_ID:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            if (camera->pipe2_param.extbuffer_flag == 1) {
                if(camera->pipe2_param.extbuffer != NULL) {
                    camera_buffer_with_frame_id_t *result = (camera_buffer_with_frame_id_t *)ubuf;
                    result->buffer = camera->pipe2_param.extbuffer;
                    result->frame_id = camera->current_frame_id;  // Use current_frame_id for extbuffer
                    result->size = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                    ret = AICAM_OK;
                } else {
                    ret = AICAM_ERROR_NOT_FOUND;
                }
            } else {
                buffer = buffer_get_latest_ready(camera->pipe2_buffer, camera->pipe2_param.buffer_nb, &camera->pipe2_dq);
                if (buffer == NULL) {
                    // Wait for skip frames to complete plus normal buffer timeout
                    uint32_t timeout = CAMERA_BUFFER_TIMEOUT_MS;
                    if (camera->skip_frame_counter > 0) {
                        timeout += (camera->skip_frame_counter * 1000 / camera->pipe2_param.fps) + CAMERA_BUFFER_TIMEOUT_MS;
                    }
                    osSemaphoreAcquire(camera->sem_pipe2, timeout);
                    buffer = buffer_get_latest_ready(camera->pipe2_buffer, camera->pipe2_param.buffer_nb, &camera->pipe2_dq);
                }
                if(buffer != NULL){
                    camera_buffer_with_frame_id_t *result = (camera_buffer_with_frame_id_t *)ubuf;
                    result->buffer = buffer->data;
                    result->frame_id = buffer->frame_id;
                    result->size = camera->pipe2_param.width * camera->pipe2_param.height * camera->pipe2_param.bpp;
                    ret = AICAM_OK;
                }else{
                    ret = AICAM_ERROR_NOT_FOUND;
                }
            }
            break;

        case CAM_CMD_RETURN_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_FOUND;
                break;
            }
            // printf("Return PIPE1 buffer: 0x%p, osThreadGetId: 0x%p\r\n", ubuf, osThreadGetId());
            for (i = 0; i < camera->pipe1_param.buffer_nb; ++i) {
                if (camera->pipe1_buffer[i].data == ubuf) {
                    camera->pipe1_buffer[i].return_count++;
                    if (camera->pipe1_buffer[i].return_count >= camera->pipe1_buffer[i].owner_count) {
                        buffer_release_isr(&camera->pipe1_buffer[i], &camera->pipe1_dq);
                    }
                    ret = AICAM_OK;
                    break;
                }
            }
            break;

        case CAM_CMD_RETURN_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_FOUND;
                break;
            }
            // printf("Return PIPE2 buffer: 0x%p, osThreadGetId: 0x%p\r\n", ubuf, osThreadGetId());
            for (i = 0; i < camera->pipe2_param.buffer_nb; ++i) {
                if (camera->pipe2_buffer[i].data == ubuf) {
                    camera->pipe2_buffer[i].return_count++;
                    if (camera->pipe2_buffer[i].return_count >= camera->pipe2_buffer[i].owner_count) {
                        buffer_release_isr(&camera->pipe2_buffer[i], &camera->pipe2_dq);
                    }
                    ret = AICAM_OK;
                    break;
                }
            }
            break;
            
        case CAM_CMD_LOCK_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            ret = AICAM_ERROR_BUSY;
            for (i = 0; i < camera->pipe1_param.buffer_nb; ++i) {
                if (camera->pipe1_buffer[i].data == ubuf && camera->pipe1_buffer[i].owner_count == 1) {
                    camera->pipe1_buffer[i].is_locked = 1;
                    ret = AICAM_OK;
                    break;
                }
            }
            break;
        
        case CAM_CMD_LOCK_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            ret = AICAM_ERROR_BUSY;
            for (i = 0; i < camera->pipe2_param.buffer_nb; ++i) {
                if (camera->pipe2_buffer[i].data == ubuf && camera->pipe2_buffer[i].owner_count == 1) {
                    camera->pipe2_buffer[i].is_locked = 1;
                    ret = AICAM_OK;
                    break;
                }
            }
            break;
        
        case CAM_CMD_UNLOCK_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            ret = AICAM_ERROR_NOT_FOUND;
            for (i = 0; i < camera->pipe1_param.buffer_nb; ++i) {
                if (camera->pipe1_buffer[i].data == ubuf && camera->pipe1_buffer[i].is_locked == 1) {
                    camera->pipe1_buffer[i].is_locked = 0;
                    ret = AICAM_OK;
                    break;
                }
            }
            break;
        
        case CAM_CMD_UNLOCK_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            ret = AICAM_ERROR_NOT_FOUND;
            for (i = 0; i < camera->pipe2_param.buffer_nb; ++i) {
                if (camera->pipe2_buffer[i].data == ubuf && camera->pipe2_buffer[i].is_locked == 1) {
                    camera->pipe2_buffer[i].is_locked = 0;
                    ret = AICAM_OK;
                    break;
                }
            }
            break;

        case CAM_CMD_SET_STARTUP_SKIP_FRAMES:
            if (arg >= 0 && arg <= 300) {  // Limit to reasonable range (max ~10s at 30fps)
                camera->startup_skip_frames = (int)arg;
                ret = AICAM_OK;
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;

        case CAM_CMD_GET_STARTUP_SKIP_FRAMES:
            if (ubuf != NULL) {
                *((int *)ubuf) = camera->startup_skip_frames;
                ret = AICAM_OK;
            } else {
                ret = AICAM_ERROR_INVALID_PARAM;
            }
            break;

        case CAM_CMD_SET_PIPE2_BUFFER_ADDR:
            if(camera->state.pipe2_state == PIPE_START){
                ret = AICAM_ERROR_BUSY;
                break;
            }
            if(camera->pipe2_buffer != NULL){
                pipe_buffer_release(camera->pipe2_buffer, &camera->pipe2_param, &camera->pipe2_dq);
                hal_mem_free(camera->pipe2_buffer);
                camera->pipe2_buffer = NULL;
            }

            camera->pipe2_param.buffer_nb = 1;
            camera->pipe2_param.extbuffer_flag = 1;
            camera->pipe2_param.extbuffer = (unsigned char *)ubuf;
            ret = AICAM_OK;
            break;

        case CAM_CMD_UNSHARE_PIPE1_BUFFER:
            if(camera->state.pipe1_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            ret = AICAM_ERROR_NOT_FOUND;
            for (i = 0; i < camera->pipe1_param.buffer_nb; ++i) {
                if (camera->pipe1_buffer[i].data == ubuf && camera->pipe1_buffer[i].state == BUFFER_IN_USE && camera->pipe1_buffer[i].owner_count == 1 && camera->pipe1_buffer[i].owner_list[0] == osThreadGetId()) {
                    new_buffer = hal_mem_alloc_aligned(g_camera.pipe1_param.width * g_camera.pipe1_param.height * g_camera.pipe1_param.bpp, CAMERA_MEMORY_ALIGNMENT, MEM_LARGE);
                    if(new_buffer == NULL){
                        ret = AICAM_ERROR_OUT_OF_MEMORY;
                        break;
                    }
                    camera->pipe1_buffer[i].data = new_buffer;
                    buffer_release_isr(&camera->pipe1_buffer[i], &camera->pipe1_dq);
                    ret = AICAM_OK;
                    break;
                }
            }
            break;
        case CAM_CMD_UNSHARE_PIPE2_BUFFER:
            if(camera->state.pipe2_state != PIPE_START){
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
            }
            ret = AICAM_ERROR_NOT_FOUND;
            for (i = 0; i < camera->pipe2_param.buffer_nb; ++i) {
                if (camera->pipe2_buffer[i].data == ubuf && camera->pipe2_buffer[i].state == BUFFER_IN_USE && camera->pipe2_buffer[i].owner_count == 1 && camera->pipe2_buffer[i].owner_list[0] == osThreadGetId()) {
                    new_buffer = hal_mem_alloc_aligned(g_camera.pipe2_param.width * g_camera.pipe2_param.height * g_camera.pipe2_param.bpp, CAMERA_MEMORY_ALIGNMENT, MEM_LARGE);
                    if(new_buffer == NULL){
                        ret = AICAM_ERROR_OUT_OF_MEMORY;
                        break;
                    }
                    camera->pipe2_buffer[i].data = new_buffer;
                    buffer_release_isr(&camera->pipe2_buffer[i], &camera->pipe2_dq);
                    ret = AICAM_OK;
                    break;
                }
            }
            break;
        default:
            ret = AICAM_ERROR_NOT_SUPPORTED;
            break;
    }
    camera->mtx_isr = 0;
    osMutexRelease(camera->mtx_id);
    return ret;
}
static int pipe_buffer_acquire(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq)
{
    for (int i = 0; i < pipe_param->buffer_nb; i++) {
        pipe_buffer[i].state = BUFFER_IDLE;
        pipe_buffer[i].frame_id = 0;
        if(pipe_param->extbuffer_flag == 1){
            pipe_buffer[i].data = pipe_param->extbuffer;
        }else{
            pipe_buffer[i].data = hal_mem_alloc_aligned(pipe_param->width * pipe_param->height * pipe_param->bpp, CAMERA_MEMORY_ALIGNMENT, MEM_LARGE);
            LOG_DRV_DEBUG("pipe buffer alloc address 0x%x size %d\r\n", pipe_buffer[i].data, pipe_param->width * pipe_param->height * pipe_param->bpp);
        }
        if(pipe_buffer[i].data == NULL){
            LOG_DRV_ERROR("pipe buffer alloc failed \r\n");
            return -1;
        }
    }
    // dq->ready_queue = osMessageQueueNew(pipe_param->buffer_nb, sizeof(uint32_t), NULL);
    // dq->idle_sem = osSemaphoreNew(pipe_param->buffer_nb, pipe_param->buffer_nb, NULL);

    return 0;
}


static int pipe_buffer_release(pipe_buffer_t *pipe_buffer, pipe_params_t *pipe_param, camera_dq_t *dq)
{
    
    // if (dq->ready_queue != NULL) {
    //     osMessageQueueDelete(dq->ready_queue);
    //     dq->ready_queue = NULL;
    // }

    // if (dq->idle_sem != NULL) {
    //     osSemaphoreDelete(dq->idle_sem);
    //     dq->idle_sem = NULL;
    // }

    if(pipe_param->extbuffer_flag == 1){
        return 0;
    }
    for (int i = 0; i < pipe_param->buffer_nb; i++) {
        if (pipe_buffer[i].data != NULL) {
            hal_mem_free(pipe_buffer[i].data);
        }
    }

    return 0;
}
static int camera_init(void *priv)
{
    // printf("camera init start, %lu ms\r\n", HAL_GetTick());
    camera_t *camera = (camera_t *)priv;
    camera->pwr_handle = pwr_manager_get_handle(PWR_SENSOR_NAME);
    pwr_manager_acquire(camera->pwr_handle);

    camera->pipe1_param.width = PIPE1_DEFAULT_WIDTH;
    camera->pipe1_param.height = PIPE1_DEFAULT_HEIGHT;
    camera->pipe1_param.format = PIPE1_DEFAULT_FORMAT;
    camera->pipe1_param.bpp = PIPE1_DEFAULT_BPP;
    camera->pipe1_param.fps = CAMERA_FPS;
    camera->pipe1_param.buffer_nb = CAPTURE_BUFFER_NB;

    camera->pipe2_param.width = PIPE2_DEFAULT_WIDTH;
    camera->pipe2_param.height = PIPE2_DEFAULT_HEIGHT;
    camera->pipe2_param.format = PIPE2_DEFAULT_FORMAT;
    camera->pipe2_param.bpp = PIPE2_DEFAULT_BPP;
    camera->pipe2_param.fps = CAMERA_FPS;
    camera->pipe2_param.buffer_nb = NN_BUFFER_NB;


    camera->mtx_id = osMutexNew(NULL);
    camera->current_frame_id = 0;
    camera->startup_skip_frames = CAMERA_DEFAULT_STARTUP_SKIP_FRAMES;
    camera->sem_init = osSemaphoreNew(1, 0, NULL);
    camera->sem_isp = osSemaphoreNew(1, 0, NULL);
    camera->sem_pipe1 = osSemaphoreNew(1, 0, NULL);
    camera->sem_pipe2 = osSemaphoreNew(1, 0, NULL);
    camera->state.camera_state = CAMERA_STOP;
    camera->state.pipe1_state = PIPE_STOP;
    camera->state.pipe2_state = PIPE_STOP;

    camera->device_ctrl_pipe = CAMERA_CTRL_PIPE1_BIT | CAMERA_CTRL_PIPE2_BIT;
    camera->camera_processId = osThreadNew(cameraProcess, camera, &cameraTask_attributes);
#ifdef ISP_ENABLE_UVC
    g_uvc_ctx.sem = osSemaphoreNew(1, 0, NULL);
    if (g_uvc_ctx.sem != NULL) {
        const osThreadAttr_t uvcTask_attributes = {
            .name = "uvcSendTask",
            .priority = (osPriority_t)osPriorityBelowNormal,
            .stack_size = 2 * 1024
        };
        g_uvc_ctx.thread_id = osThreadNew(uvcSendTask, NULL, &uvcTask_attributes);
    }
#endif
    return 0;
}

static int camera_deinit(void *priv)
{
    camera_t *camera = (camera_t *)priv;

    if (camera->is_init == false) {
        return AICAM_OK;
    }
    CMW_CAMERA_DeInit();
    camera->is_init = false;
    pwr_manager_release(camera->pwr_handle);
    osSemaphoreRelease(camera->sem_isp);
    osDelay(CAMERA_DEINIT_DELAY_MS);
    if (camera->camera_processId != NULL) {
        osThreadTerminate(camera->camera_processId);
        camera->camera_processId = NULL;
    }

#ifdef ISP_ENABLE_UVC
    if (g_uvc_ctx.thread_id != NULL) {
        osThreadTerminate(g_uvc_ctx.thread_id);
        g_uvc_ctx.thread_id = NULL;
    }
    if (g_uvc_ctx.sem != NULL) {
        osSemaphoreDelete(g_uvc_ctx.sem);
        g_uvc_ctx.sem = NULL;
    }
    if (uvc_frame_buf != NULL) {
        hal_mem_free(uvc_frame_buf);
        uvc_frame_buf = NULL;
        uvc_frame_buf_size = 0;
    }
#endif

    if (camera->pwr_handle != 0) {
        pwr_manager_release(camera->pwr_handle);
        camera->pwr_handle = 0;
    }

    if (camera->sem_init != NULL) {
        osSemaphoreRelease(camera->sem_init);
        osSemaphoreDelete(camera->sem_init);
        camera->sem_init = NULL;
    }

    if (camera->sem_isp != NULL) {
        osSemaphoreRelease(camera->sem_isp);
        osSemaphoreDelete(camera->sem_isp);
        camera->sem_isp = NULL;
    }

    if (camera->sem_pipe1 != NULL) {
        osSemaphoreDelete(camera->sem_pipe1);
        camera->sem_pipe1 = NULL;
    }

    if (camera->sem_pipe2 != NULL) {
        osSemaphoreDelete(camera->sem_pipe2);
        camera->sem_pipe2 = NULL;
    }

    if (camera->mtx_id != NULL) {
        osMutexDelete(camera->mtx_id);
        camera->mtx_id = NULL;
    }
    if (camera->pipe1_buffer) {
        pipe_buffer_release(camera->pipe1_buffer, &camera->pipe1_param, &camera->pipe1_dq);
        hal_mem_free(camera->pipe1_buffer);
        camera->pipe1_buffer = NULL;
    }
    if (camera->pipe2_buffer) {
        pipe_buffer_release(camera->pipe2_buffer, &camera->pipe2_param, &camera->pipe2_dq);
        hal_mem_free(camera->pipe2_buffer);
        camera->pipe2_buffer = NULL;
    }

    return 0;
}

int camera_register(void)
{
    static dev_ops_t camera_ops ={
        .init = camera_init, 
        .deinit = camera_deinit, 
        .start = camera_start,
        .stop = camera_stop,
        .ioctl = camera_ioctl
    };

    if(g_camera.is_init == true){
        return AICAM_OK;
    }
    int ret;
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_camera.dev = dev;
    strcpy(dev->name, CAMERA_DEVICE_NAME);
    dev->type = DEV_TYPE_VIDEO;
    dev->ops = &camera_ops;
    dev->priv_data = &g_camera;

    ret = device_register(g_camera.dev);
    if(ret != 0){
        hal_mem_free(g_camera.dev);
        g_camera.dev = NULL;
        return AICAM_ERROR;
    }
    return AICAM_OK;
}

int camera_unregister(void)
{
    if (g_camera.dev) {
        device_unregister(g_camera.dev);
        hal_mem_free(g_camera.dev);
        g_camera.dev = NULL;
    }
    return AICAM_OK;
}

/* This function is used to deinit the camera but not unregister */
int camera_deinit_but_not_unregister(void)
{
    return camera_deinit(&g_camera);
}

void camera_free_unshared_buffer(uint8_t *buffer)
{
    if(buffer == NULL){
        return;
    }
    hal_mem_free(buffer);
}

#ifdef ISP_MW_TUNING_TOOL_SUPPORT
ISP_HandleTypeDef* camera_get_isp_handle(void)
{
    if (isp_is_init) {
        return &hIsp;
    }
    return NULL;
}
#else
ISP_HandleTypeDef* camera_get_isp_handle(void)
{
    if (g_camera.is_init == false || (g_camera.state.pipe2_state != PIPE_START && g_camera.state.pipe1_state != PIPE_START)) {
        LOG_DRV_ERROR("ISP not initialized or pipe not started \r\n");
        return NULL;
    }
    return CMW_CAMERA_GetISPHandle();
}
#endif
