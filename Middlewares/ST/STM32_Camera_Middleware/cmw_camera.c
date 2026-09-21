 /**
 ******************************************************************************
 * @file    cmw_camera.c
 * @author  GPM Application Team
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */


/* Includes ------------------------------------------------------------------*/
#include "cmw_camera.h"

#include "isp_api.h"
#include "stm32n6xx_hal_dcmipp.h"
#include "cmw_utils.h"
#include "cmw_io.h"
#if defined(USE_VD55G1_SENSOR)
#include "cmw_vd55g1.h"
#endif
#if defined(USE_VD65G4_SENSOR)
#include "cmw_vd65g4.h"
#endif
#if defined(USE_IMX335_SENSOR)
#include "cmw_imx335.h"
#endif
#if defined(USE_OV5640_SENSOR)
#include "cmw_ov5640.h"
#endif
#if defined(USE_VD66GY_SENSOR)
#include "cmw_vd66gy.h"
#endif
#if defined(USE_VD56G3_SENSOR)
#include "cmw_vd56g3.h"
#endif
#if defined(USE_VD1943_SENSOR)
#include "cmw_vd1943.h"
#endif
#if defined (USE_VD5943_SENSOR)
#include "cmw_vd5943.h"
#endif
#if defined(USE_OS04C10_SENSOR)
#include "cmw_os04c10.h"
#endif
#include "assert.h"
#include "cmsis_os2.h"

typedef struct
{
  uint32_t Resolution;
  uint32_t pixel_format;
  uint32_t LightMode;
  uint32_t ColorEffect;
  int32_t  Brightness;
  int32_t  Saturation;
  int32_t  Contrast;
  int32_t  HueDegree;
  int32_t  Gain;
  int32_t  Exposure;
  int32_t  ExposureMode;
  uint32_t MirrorFlip;
  uint32_t Zoom;
  uint32_t NightMode;
  uint32_t IsMspCallbacksValid;
  uint32_t TestPattern;
} CAMERA_Ctx_t;

CMW_CameraInit_t  camera_conf;
CMW_Sensor_Name_t connected_sensor;
CAMERA_Ctx_t  Camera_Ctx;

DCMIPP_HandleTypeDef hcamera_dcmipp;
static CMW_Sensor_if_t Camera_Drv;

const ISP_IQParamTypeDef *user_isp_init_param = NULL;

static union
{
#if defined(USE_IMX335_SENSOR)
  CMW_IMX335_t imx335_bsp;
#endif
#if defined(USE_VD55G1_SENSOR)
  CMW_VD55G1_t vd55g1_bsp;
#endif
#if defined(USE_VD65G4_SENSOR)
  CMW_VD65G4_t vd65g4_bsp;
#endif
#if defined(USE_VD66GY_SENSOR)
  CMW_VD66GY_t vd66gy_bsp;
#endif
#if defined(USE_VD56G3_SENSOR)
  CMW_VD56G3_t vd56g3_bsp;
#endif
#if defined(USE_OV5640_SENSOR)
  CMW_OV5640_t ov5640_bsp;
#endif
#if defined(USE_VD1943_SENSOR)
  CMW_VD1943_t vd1943_bsp;
#endif
#if defined(USE_VD5943_SENSOR)
  CMW_VD5943_t vd5943_bsp;
#endif
#if defined(USE_OS04C10_SENSOR)
  CMW_OS04C10_t os04c10_bsp;
#endif
} camera_bsp;

int is_camera_init = 0;
int is_camera_started = 0;
int is_pipe1_2_shared = 0;

#if defined(USE_IMX335_SENSOR)
static int32_t CMW_CAMERA_IMX335_Init( CMW_Sensor_Init_t *initSensors_params);
#endif
#if defined(USE_VD55G1_SENSOR)
static int32_t CMW_CAMERA_VD55G1_Init( CMW_Sensor_Init_t *initSensors_params);
#endif
#if defined(USE_VD65G4_SENSOR)
static int32_t CMW_CAMERA_VD65G4_Init( CMW_Sensor_Init_t *initSensors_params);
#endif
#if defined(USE_OV5640_SENSOR)
static int32_t CMW_CAMERA_OV5640_Init( CMW_Sensor_Init_t *initSensors_params);
#endif
#if defined(USE_VD66GY_SENSOR)
static int32_t CMW_CAMERA_VD66GY_Init(CMW_Sensor_Init_t *initValues);
#endif
#if defined(USE_VD56G3_SENSOR)
static int32_t CMW_CAMERA_VD56G3_Init(CMW_Sensor_Init_t *initSensors_params);
#endif
#if defined(USE_VD1943_SENSOR)
static int32_t CMW_CAMERA_VD1943_Init(CMW_Sensor_Init_t *initValues);
#endif
#if defined(USE_VD5943_SENSOR)
static int32_t CMW_CAMERA_VD5943_Init(CMW_Sensor_Init_t *initValues);
#endif
#if defined(USE_OS04C10_SENSOR)
static int32_t CMW_CAMERA_OS04C10_Init( CMW_Sensor_Init_t *initSensors_params);
#endif
static void CMW_CAMERA_EnableGPIOs(void);
static void CMW_CAMERA_PwrDown(void);
static int32_t CMW_CAMERA_SetPipe(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe, CMW_DCMIPP_Conf_t *p_conf, uint32_t *pitch);
static int CMW_CAMERA_Probe_Sensor(CMW_Sensor_Init_t *initValues, CMW_Sensor_Name_t *sensorName);

DCMIPP_HandleTypeDef* CMW_CAMERA_GetDCMIPPHandle(void)
{
    return &hcamera_dcmipp;
}

ISP_HandleTypeDef* CMW_CAMERA_GetISPHandle(void)
{
#if defined(USE_IMX335_SENSOR)
    if (connected_sensor == CMW_IMX335_Sensor)
        return &camera_bsp.imx335_bsp.hIsp;
#endif
#if defined(USE_VD66GY_SENSOR)
    if (connected_sensor == CMW_VD66GY_Sensor)
        return &camera_bsp.vd66gy_bsp.hIsp;
#endif
#if defined(USE_VD55G1_SENSOR)
    if (connected_sensor == CMW_VD55G1_Sensor)
        return &camera_bsp.vd55g1_bsp.hIsp;
#endif
#if defined(USE_OS04C10_SENSOR)
    if (connected_sensor == CMW_OS04C10_Sensor)
        return &camera_bsp.os04c10_bsp.hIsp;
#endif
    return NULL;
}

int32_t CMW_CAMERA_SetISPInitParam(const ISP_IQParamTypeDef *isp_param)
{
    user_isp_init_param = isp_param;
    return CMW_ERROR_NONE;
}

int32_t CMW_CAMERA_SetPipeConfig(uint32_t pipe, CMW_DCMIPP_Conf_t *p_conf, uint32_t *pitch)
{
  return CMW_CAMERA_SetPipe(&hcamera_dcmipp, pipe, p_conf, pitch);
}

/**
  * @brief  Get Sensor name.
  * @param  sensorName  Camera sensor name
  * @retval CMW status
  */
int32_t CMW_CAMERA_GetSensorName(CMW_Sensor_Name_t *sensorName)
{
  int32_t ret = CMW_ERROR_NONE;
  CMW_Sensor_Init_t initValues = {0};

  if (is_camera_init != 0)
  {
    *sensorName = connected_sensor;
    return CMW_ERROR_NONE;
  }

  initValues.width = 0;
  initValues.height = 0;
  initValues.fps = 30;
  initValues.mirrorFlip = CMW_MIRRORFLIP_NONE;
  initValues.sensor_config = NULL;

  /* Set DCMIPP instance */
  hcamera_dcmipp.Instance = DCMIPP;

  /* Configure DCMIPP clock */
  ret = MX_DCMIPP_ClockConfig(&hcamera_dcmipp);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }
  /* Enable DCMIPP clock */
  ret = HAL_DCMIPP_Init(&hcamera_dcmipp);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  CMW_CAMERA_EnableGPIOs();

  ret = CMW_CAMERA_Probe_Sensor(&initValues, &connected_sensor);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_UNKNOWN_COMPONENT;
  }
  *sensorName = connected_sensor;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Set White Balance mode.
  * @param  Automatic  If not null, set automatic white balance mode
  * @param  RefColorTemp  If automatic is null, set white balance mode
  * @retval CMW status
  */
int32_t CMW_CAMERA_SetWBRefMode(uint8_t Automatic, uint32_t RefColorTemp)
{
  int ret;

  ret = Camera_Drv.SetWBRefMode(&camera_bsp, Automatic, RefColorTemp);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = CMW_ERROR_NONE;
  /* Return CMW status */
  return ret;
}

/**
  * @brief  Get White Balance reference modes list.
  * @param  RefColorTemp  White Balance reference modes
  * @retval CMW status
  */
int32_t CMW_CAMERA_ListWBRefModes(uint32_t RefColorTemp[])
{
  int ret;

  ret = Camera_Drv.ListWBRefModes(&camera_bsp, RefColorTemp);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = CMW_ERROR_NONE;
  /* Return CMW status */
  return ret;
}

/**
  * @brief  Probe camera sensor.
  * @param  initValues  Initialization values for the sensor
  * @param  sensorName  Camera sensor name
  * @retval CMW status
  */
static int CMW_CAMERA_Probe_Sensor(CMW_Sensor_Init_t *initValues, CMW_Sensor_Name_t *sensorName)
{
  int ret;
#if defined(USE_OV5640_SENSOR)
  ret = CMW_CAMERA_OV5640_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_OV5640_Sensor;
    return ret;
  }
#endif
#if defined(USE_VD55G1_SENSOR)
  ret = CMW_CAMERA_VD55G1_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_VD55G1_Sensor;
    return ret;
  }
#endif
#if defined(USE_VD65G4_SENSOR)
  ret = CMW_CAMERA_VD65G4_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_VD65G4_Sensor;
    return ret;
  }
#endif
#if defined(USE_VD66GY_SENSOR)
  ret = CMW_CAMERA_VD66GY_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_VD66GY_Sensor;
    return ret;
  }
#endif
#if defined(USE_VD56G3_SENSOR)
  ret = CMW_CAMERA_VD56G3_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_VD56G3_Sensor;
    return ret;
  }
#endif
#if defined(USE_VD1943_SENSOR)
  ret = CMW_CAMERA_VD1943_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_VD1943_Sensor;
    return ret;
  }
#endif
#if defined(USE_VD5943_SENSOR)
  ret = CMW_CAMERA_VD5943_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_VD5943_Sensor;
    return ret;
  }
#endif
#if defined(USE_IMX335_SENSOR)
  ret = CMW_CAMERA_IMX335_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_IMX335_Sensor;
    return ret;
  }
#endif
#if defined(USE_OS04C10_SENSOR)
  ret = CMW_CAMERA_OS04C10_Init(initValues);
  if (ret == CMW_ERROR_NONE)
  {
    *sensorName = CMW_OS04C10_Sensor;
    return ret;
  }
#endif
  else
  {
    return CMW_ERROR_UNKNOWN_COMPONENT;
  }
}



/**
  * @brief  Initializes the camera.
  * @param  initConf  Mandatory: General camera config
  * @param  advanced_config  Optional: Sensor specific configuration
  * @retval CMW status
  */
int32_t CMW_CAMERA_Init(CMW_CameraInit_t *initConf, CMW_Advanced_Config_t *advanced_config)
{
  int32_t ret = CMW_ERROR_NONE;
  CMW_Sensor_Init_t initValues = {0};
  ISP_SensorInfoTypeDef info = {0};

  initValues.width = initConf->width;
  initValues.height = initConf->height;
  initValues.fps = initConf->fps;
  initValues.mirrorFlip = initConf->mirror_flip;

  if ((advanced_config != NULL) && (advanced_config->selected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* Assume The sensor is the one selected by the application. Check during probe */
    connected_sensor = advanced_config->selected_sensor;
    initValues.sensor_config = (void *) &advanced_config->config_sensor;
  }
  else
  {
    connected_sensor = CMW_UNKNOWN_Sensor;
    initValues.sensor_config = NULL;
  }

  if (is_camera_init != 0)
  {
    return CMW_ERROR_NONE;
  }

  /* Set DCMIPP instance */
  hcamera_dcmipp.Instance = DCMIPP;

  /* Configure DCMIPP clock */
  ret = MX_DCMIPP_ClockConfig(&hcamera_dcmipp);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }
  /* Enable DCMIPP clock */
  ret = HAL_DCMIPP_Init(&hcamera_dcmipp);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  CMW_CAMERA_EnableGPIOs();

  ret = CMW_CAMERA_Probe_Sensor(&initValues, &connected_sensor);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_UNKNOWN_COMPONENT;
  }

  /* Configure exposure and gain for a more suitable quality */
  ret = CMW_CAMERA_GetSensorInfo(&info);
  if (ret == CMW_ERROR_COMPONENT_FAILURE)
  {
    return CMW_ERROR_UNKNOWN_COMPONENT;
  }
  ret = CMW_CAMERA_SetExposure(info.exposure_min);
  if (ret == CMW_ERROR_COMPONENT_FAILURE)
  {
    return CMW_ERROR_UNKNOWN_COMPONENT;
  }
  ret = CMW_CAMERA_SetGain(info.gain_min);
  if (ret == CMW_ERROR_COMPONENT_FAILURE)
  {
    return CMW_ERROR_UNKNOWN_COMPONENT;
  }

  /* Write back the initValue width and height that might be changed */
  initConf->width = initValues.width;
  initConf->height = initValues.height ;
  camera_conf = *initConf;

  is_camera_init++;
  /* CMW status */
  ret = CMW_ERROR_NONE;
  return ret;
}

/**
  * @brief  Set the camera Mirror/Flip.
  * @param  MirrorFlip CMW_MIRRORFLIP_NONE CMW_MIRRORFLIP_FLIP CMW_MIRRORFLIP_MIRROR CMW_MIRRORFLIP_FLIP_MIRROR
  * @retval CMW status
*/
int32_t CMW_CAMERA_SetMirrorFlip(int32_t MirrorFlip)
{
  int ret;

  if (Camera_Drv.SetMirrorFlip == NULL)
  {
    return CMW_ERROR_FEATURE_NOT_SUPPORTED;
  }

  ret = Camera_Drv.SetMirrorFlip(&camera_bsp, MirrorFlip);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  camera_conf.mirror_flip = MirrorFlip;
  ret = CMW_ERROR_NONE;
  /* Return CMW status */
  return ret;
}

/**
  * @brief  Get the camera Mirror/Flip.
  * @param  MirrorFlip CMW_MIRRORFLIP_NONE CMW_MIRRORFLIP_FLIP CMW_MIRRORFLIP_MIRROR CMW_MIRRORFLIP_FLIP_MIRROR
  * @retval CMW status
*/
int32_t CMW_CAMERA_GetMirrorFlip(int32_t *MirrorFlip)
{
  *MirrorFlip = camera_conf.mirror_flip;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Starts the camera capture in the selected mode.
  * @param  pipe  DCMIPP Pipe
  * @param  pbuff pointer to the camera output buffer
  * @param  mode  CMW_MODE_CONTINUOUS or CMW_MODE_SNAPSHOT
  * @retval CMW status
  */
int32_t CMW_CAMERA_Start(uint32_t pipe, uint8_t *pbuff, uint32_t mode)
{
  int32_t ret = CMW_ERROR_NONE;

  if (pipe >= DCMIPP_NUM_OF_PIPES)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = HAL_DCMIPP_CSI_PIPE_Start(&hcamera_dcmipp, pipe, DCMIPP_VIRTUAL_CHANNEL0, (uint32_t)pbuff, mode);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  if (!is_camera_started)
  {
    ret = Camera_Drv.Start(&camera_bsp);
    if (ret != CMW_ERROR_NONE)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
    is_camera_started++;
  }

  /* Return CMW status */
  return ret;
}

#if defined (STM32N657xx)
/**
  * @brief  Starts the camera capture in the selected mode.
  * @param  pipe  DCMIPP Pipe
  * @param  pbuff1 pointer to the first camera output buffer
  * @param  pbuff2 pointer to the second camera output buffer
  * @param  mode  CMW_MODE_CONTINUOUS or CMW_MODE_SNAPSHOT
  * @retval CMW status
  */
int32_t CMW_CAMERA_DoubleBufferStart(uint32_t pipe, uint8_t *pbuff1, uint8_t *pbuff2, uint32_t Mode)
{
  int32_t ret = CMW_ERROR_NONE;

  if (pipe >= DCMIPP_NUM_OF_PIPES)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  if (HAL_DCMIPP_CSI_PIPE_DoubleBufferStart(&hcamera_dcmipp, pipe, DCMIPP_VIRTUAL_CHANNEL0, (uint32_t)pbuff1,
                                            (uint32_t)pbuff2, Mode) != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  if (!is_camera_started)
  {
    ret = Camera_Drv.Start(&camera_bsp);
    if (ret != CMW_ERROR_NONE)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
    is_camera_started++;
  }

  /* Return CMW status */
  return ret;
}
#endif




/**
  * @brief  DCMIPP Clock Config for DCMIPP.
  * @param  hdcmipp  DCMIPP Handle
  *         Being __weak it can be overwritten by the application
  * @retval HAL_status
  */
__weak HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
  UNUSED(hdcmipp);

  return HAL_OK;
}

/**
  * @brief  DeInitializes the camera.
  * @retval CMW status
  */
int32_t CMW_CAMERA_DeInit(void)
{
  int32_t ret = CMW_ERROR_NONE;

  if (HAL_DCMIPP_PIPE_GetState(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_DCMIPP_PIPE_STATE_RESET)
  {
    ret = HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  if (HAL_DCMIPP_PIPE_GetState(&hcamera_dcmipp, DCMIPP_PIPE2) != HAL_DCMIPP_PIPE_STATE_RESET)
  {
    ret = HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  ret = HAL_DCMIPP_DeInit(&hcamera_dcmipp);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  if (is_camera_init <= 0)
  {
    return CMW_ERROR_NONE;
  }

  /* De-initialize the camera module */
  ret = Camera_Drv.DeInit(&camera_bsp);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  /* Set Camera in Power Down */
  CMW_CAMERA_PwrDown();

  /* Update DCMIPPInit counter */
  is_camera_init = 0;
  is_camera_started = 0;
  is_pipe1_2_shared = 0;

  /* Return CMW status */
  ret = CMW_ERROR_NONE;
  return ret;
}

/**
  * @brief  Suspend the CAMERA capture on selected pipe
  * @param  pipe Dcmipp pipe.
  * @retval CMW status
  */
int32_t CMW_CAMERA_Suspend(uint32_t pipe)
{
  HAL_DCMIPP_PipeStateTypeDef state = hcamera_dcmipp.PipeState[pipe];

  if (state == HAL_DCMIPP_PIPE_STATE_SUSPEND)
  {
    return CMW_ERROR_NONE;
  }
  else if (state > HAL_DCMIPP_PIPE_STATE_READY)
  {
    if (HAL_DCMIPP_PIPE_Suspend(&hcamera_dcmipp, pipe) != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  /* Return CMW status */
  return CMW_ERROR_NONE;
}

/**
  * @brief  Resume the CAMERA capture on selected pipe
  * @param  pipe Dcmipp pipe.
  * @retval CMW status
  */
int32_t CMW_CAMERA_Resume(uint32_t pipe)
{
  HAL_DCMIPP_PipeStateTypeDef state = hcamera_dcmipp.PipeState[pipe];

  if (state == HAL_DCMIPP_PIPE_STATE_BUSY)
  {
    return CMW_ERROR_NONE;
  }
  else if (state > HAL_DCMIPP_PIPE_STATE_BUSY)
  {
    if (HAL_DCMIPP_PIPE_Resume(&hcamera_dcmipp, pipe) != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  /* Return CMW status */
  return CMW_ERROR_NONE;
}

/**
  * @brief  Enable the Restart State. When enabled, at system restart, the ISP middleware configuration
  *         is restored from the last update before the restart.
  * @param  ISP_RestartState pointer to ISP Restart State. To use this mode in a Low Power use case, where
  *         the ISP state is applied at system wake up, this pointer must be in some retention memory.
  * @retval CMW status
  */
int32_t CMW_CAMERA_EnableRestartState(ISP_RestartStateTypeDef *ISP_RestartState)
{
  if (ISP_EnableRestartState(NULL, ISP_RestartState) != ISP_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Return CMW status */
  return CMW_ERROR_NONE;
}

/**
  * @brief  Disable the Restart State
  * @retval CMW status
  */
int32_t CMW_CAMERA_DisableRestartState()
{
  if (ISP_DisableRestartState(NULL) != ISP_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Return CMW status */
  return CMW_ERROR_NONE;
}

/**
  * @brief  Set the camera gain.
  * @param  Gain     Gain in mdB
  * @retval CMW status
  */
int CMW_CAMERA_SetGain(int32_t Gain)
{
  int ret;
  if(Camera_Drv.SetGain == NULL)
  {
    return CMW_ERROR_FEATURE_NOT_SUPPORTED;
  }

  ret = Camera_Drv.SetGain(&camera_bsp, Gain);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  Camera_Ctx.Gain = Gain;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Get the camera gain.
  * @param  Gain     Gain in mdB
  * @retval CMW status
  */
int CMW_CAMERA_GetGain(int32_t *Gain)
{
  *Gain = Camera_Ctx.Gain;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Set the camera exposure.
  * @param  exposure exposure in microseconds
  * @retval CMW status
  */
int CMW_CAMERA_SetExposure(int32_t exposure)
{
  int ret;

  if(Camera_Drv.SetExposure == NULL)
  {
    return CMW_ERROR_FEATURE_NOT_SUPPORTED;
  }

  ret = Camera_Drv.SetExposure(&camera_bsp, exposure);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  Camera_Ctx.Exposure = exposure;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Get the camera exposure.
  * @param  exposure exposure in microseconds
  * @retval CMW status
  */
int CMW_CAMERA_GetExposure(int32_t *exposure)
{
  *exposure = Camera_Ctx.Exposure;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Set the camera exposure mode.
  * @param  exposureMode Exposure mode CMW_EXPOSUREMODE_AUTO, CMW_EXPOSUREMODE_AUTOFREEZE, CMW_EXPOSUREMODE_MANUAL
  * @retval CMW status
  */
int32_t CMW_CAMERA_SetExposureMode(int32_t exposureMode)
{
  int ret;

  if(Camera_Drv.SetExposureMode == NULL)
  {
    return CMW_ERROR_FEATURE_NOT_SUPPORTED;
  }

  ret = Camera_Drv.SetExposureMode(&camera_bsp, exposureMode);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  Camera_Ctx.ExposureMode = exposureMode;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Get the camera exposure mode.
  * @param  exposureMode Exposure mode CAMERA_EXPOSURE_AUTO, CAMERA_EXPOSURE_AUTOFREEZE, CAMERA_EXPOSURE_MANUAL
  * @retval CMW status
  */
int32_t CMW_CAMERA_GetExposureMode(int32_t *exposureMode)
{
  *exposureMode = Camera_Ctx.ExposureMode;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Set (Enable/Disable and Configure) the camera test pattern
  * @param  mode Pattern mode (sensor specific value) to be configured. '-1' means disable.
  * @retval CMW status
  */
int32_t CMW_CAMERA_SetTestPattern(int32_t mode)
{
  int32_t ret;

  if(Camera_Drv.SetTestPattern == NULL)
  {
    return CMW_ERROR_FEATURE_NOT_SUPPORTED;
  }

  ret = Camera_Drv.SetTestPattern(&camera_bsp, mode);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  Camera_Ctx.TestPattern = mode;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Get the camera test pattern
  * @param  mode Pattern mode (sensor specific value) to be returned. '-1' means disable.
  * @retval CMW status
  */
int32_t CMW_CAMERA_GetTestPattern(int32_t *mode)
{
  *mode = Camera_Ctx.TestPattern;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Get the Camera Sensor info.
  * @param  info  pointer to sensor info
  * @note   This function should be called after the init. This to get Capabilities
  *         from the camera sensor
  * @retval Component status
  */
int32_t CMW_CAMERA_GetSensorInfo(ISP_SensorInfoTypeDef *info)
{

  int32_t ret;

  if(Camera_Drv.GetSensorInfo == NULL)
  {
    return CMW_ERROR_FEATURE_NOT_SUPPORTED;
  }

  ret = Camera_Drv.GetSensorInfo(&camera_bsp, info);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  return CMW_ERROR_NONE;
}



int32_t CMW_CAMERA_Run()
{
  if(Camera_Drv.Run != NULL && is_camera_started)
  {
      return Camera_Drv.Run(&camera_bsp);
  }
  return CMW_ERROR_NONE;
}

/**
 * @brief  Vsync Event callback on pipe
 * @param  Pipe  Pipe receiving the callback
 * @retval None
 */
__weak int CMW_CAMERA_PIPE_VsyncEventCallback(uint32_t pipe)
{
  UNUSED(pipe);

  return CMW_ERROR_NONE;
}

/**
 * @brief  Frame Event callback on pipe
 * @param  Pipe  Pipe receiving the callback
 * @retval None
 */
__weak int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
  UNUSED(pipe);

  return CMW_ERROR_NONE;
}

/**
 * @brief  Error callback on pipe
 * @param  Pipe  Pipe receiving the callback
 * @retval None
 */
__weak void CMW_CAMERA_PIPE_ErrorCallback(uint32_t pipe)
{
  assert(0);
}

/**
 * @brief  Vsync Event callback on pipe
 * @param  hdcmipp DCMIPP device handle
 *         Pipe    Pipe receiving the callback
 * @retval None
 */
void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  UNUSED(hdcmipp);
  if(Camera_Drv.VsyncEventCallback != NULL)
  {
      Camera_Drv.VsyncEventCallback(&camera_bsp, Pipe);
  }
  CMW_CAMERA_PIPE_VsyncEventCallback(Pipe);
}

/**
 * @brief  Frame Event callback on pipe
 * @param  hdcmipp DCMIPP device handle
 *         Pipe    Pipe receiving the callback
 * @retval None
 */
void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  UNUSED(hdcmipp);
  if(Camera_Drv.FrameEventCallback != NULL)
  {
      Camera_Drv.FrameEventCallback(&camera_bsp, Pipe);
  }
  CMW_CAMERA_PIPE_FrameEventCallback(Pipe);
}

/**
  * @brief  Initializes the DCMIPP MSP.
  * @param  hdcmipp  DCMIPP handle
  * @retval None
  */
void HAL_DCMIPP_MspInit(DCMIPP_HandleTypeDef *hdcmipp)
{
  UNUSED(hdcmipp);

  /*** Enable peripheral clock ***/
  /* Enable DCMIPP clock */
  __HAL_RCC_DCMIPP_CLK_ENABLE();
  __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();
  __HAL_RCC_DCMIPP_FORCE_RESET();
  __HAL_RCC_DCMIPP_RELEASE_RESET();

  /*** Configure the NVIC for DCMIPP ***/
  /* NVIC configuration for DCMIPP transfer complete interrupt */
  HAL_NVIC_SetPriority(DCMIPP_IRQn, 0x07, 0);
  HAL_NVIC_EnableIRQ(DCMIPP_IRQn);

  /*** Enable peripheral clock ***/
  /* Enable CSI clock */
  __HAL_RCC_CSI_CLK_ENABLE();
  __HAL_RCC_CSI_CLK_SLEEP_ENABLE();
  __HAL_RCC_CSI_FORCE_RESET();
  __HAL_RCC_CSI_RELEASE_RESET();

  /*** Configure the NVIC for CSI ***/
  /* NVIC configuration for CSI transfer complete interrupt */
  HAL_NVIC_SetPriority(CSI_IRQn, 0x07, 0);
  HAL_NVIC_EnableIRQ(CSI_IRQn);

}

/**
  * @brief  DeInitializes the DCMIPP MSP.
  * @param  hdcmipp  DCMIPP handle
  * @retval None
  */
void HAL_DCMIPP_MspDeInit(DCMIPP_HandleTypeDef *hdcmipp)
{
  UNUSED(hdcmipp);

  __HAL_RCC_DCMIPP_FORCE_RESET();
  __HAL_RCC_DCMIPP_RELEASE_RESET();

  /* Disable NVIC  for DCMIPP transfer complete interrupt */
  HAL_NVIC_DisableIRQ(DCMIPP_IRQn);

  /* Disable DCMIPP clock */
  __HAL_RCC_DCMIPP_CLK_DISABLE();

  __HAL_RCC_CSI_FORCE_RESET();
  __HAL_RCC_CSI_RELEASE_RESET();

  /* Disable NVIC  for DCMIPP transfer complete interrupt */
  HAL_NVIC_DisableIRQ(CSI_IRQn);

  /* Disable DCMIPP clock */
  __HAL_RCC_CSI_CLK_DISABLE();
}

/**
  * @brief  CAMERA hardware reset
  * @retval CMW status
  */
static void CMW_CAMERA_EnableGPIOs(void)
{
  GPIO_InitTypeDef gpio_init_structure = {0};

  /* Enable GPIO clocks */
  EN_CAM_GPIO_ENABLE_VDDIO();
  EN_CAM_GPIO_CLK_ENABLE();
  // NRST_CAM_GPIO_ENABLE_VDDIO();
  // NRST_CAM_GPIO_CLK_ENABLE();

  gpio_init_structure.Pin       = EN_CAM_PIN;
  gpio_init_structure.Pull      = GPIO_NOPULL;
  gpio_init_structure.Mode      = GPIO_MODE_OUTPUT_PP;
  gpio_init_structure.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(EN_CAM_PORT, &gpio_init_structure);

  // gpio_init_structure.Pin       = NRST_CAM_PIN;
  // gpio_init_structure.Pull      = GPIO_NOPULL;
  // gpio_init_structure.Mode      = GPIO_MODE_OUTPUT_PP;
  // gpio_init_structure.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  // HAL_GPIO_Init(NRST_CAM_PORT, &gpio_init_structure);
}

/**
  * @brief  CAMERA power down
  * @retval CMW status
  */
static void CMW_CAMERA_PwrDown(void)
{
  GPIO_InitTypeDef gpio_init_structure = {0};

  gpio_init_structure.Pin       = EN_CAM_PIN;
  gpio_init_structure.Pull      = GPIO_NOPULL;
  gpio_init_structure.Mode      = GPIO_MODE_OUTPUT_PP;
  HAL_GPIO_Init(EN_CAM_PORT, &gpio_init_structure);

  // gpio_init_structure.Pin       = NRST_CAM_PIN;
  // gpio_init_structure.Pull      = GPIO_NOPULL;
  // gpio_init_structure.Mode      = GPIO_MODE_OUTPUT_PP;
  // HAL_GPIO_Init(NRST_CAM_PORT, &gpio_init_structure);

  /* Camera power down sequence */
  /* Assert the camera Enable pin (active high) */
  HAL_GPIO_WritePin(EN_CAM_PORT, EN_CAM_PIN, GPIO_PIN_RESET);

  /* De-assert the camera NRST pin (active low) */
  // HAL_GPIO_WritePin(NRST_CAM_PORT, NRST_CAM_PIN, GPIO_PIN_RESET);

}

static void CMW_CAMERA_ShutdownPin(int value)
{
  // HAL_GPIO_WritePin(NRST_CAM_PORT, NRST_CAM_PIN, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void CMW_CAMERA_EnablePin(int value)
{
  HAL_GPIO_WritePin(EN_CAM_PORT, EN_CAM_PIN, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

#if defined(USE_VD66GY_SENSOR) || defined(USE_IMX335_SENSOR) || defined(USE_VD5943_SENSOR) || defined(USE_VD1943_SENSOR) || defined(USE_OS04C10_SENSOR)
static ISP_StatusTypeDef CB_ISP_SetSensorGain(uint32_t camera_instance, int32_t gain)
{
  if (CMW_CAMERA_SetGain(gain) != CMW_ERROR_NONE)
    return ISP_ERR_SENSORGAIN;

  return ISP_OK;
}

static ISP_StatusTypeDef CB_ISP_GetSensorGain(uint32_t camera_instance, int32_t *gain)
{
  if (CMW_CAMERA_GetGain(gain) != CMW_ERROR_NONE)
    return ISP_ERR_SENSORGAIN;

  return ISP_OK;
}

static ISP_StatusTypeDef CB_ISP_SetSensorExposure(uint32_t camera_instance, int32_t exposure)
{
  if (CMW_CAMERA_SetExposure(exposure) != CMW_ERROR_NONE)
    return ISP_ERR_SENSOREXPOSURE;

  return ISP_OK;
}

static ISP_StatusTypeDef CB_ISP_GetSensorExposure(uint32_t camera_instance, int32_t *exposure)
{
  if (CMW_CAMERA_GetExposure(exposure) != CMW_ERROR_NONE)
    return ISP_ERR_SENSOREXPOSURE;

  return ISP_OK;
}

static ISP_StatusTypeDef CB_ISP_GetSensorInfo(uint32_t camera_instance, ISP_SensorInfoTypeDef *Info)
{
  if(Camera_Drv.GetSensorInfo != NULL)
  {
    if (Camera_Drv.GetSensorInfo(&camera_bsp, Info) != CMW_ERROR_NONE)
      return ISP_ERR_SENSOREXPOSURE;
  }
  return ISP_OK;
}
#endif

#if defined(USE_VD55G1_SENSOR)
static int32_t CMW_CAMERA_VD55G1_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_VD55G1_config_t default_sensor_config;
  CMW_VD55G1_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.vd55g1_bsp.Address     = CAMERA_VD55G1_ADDRESS;
  camera_bsp.vd55g1_bsp.ClockInHz   = CAMERA_VD55G1_FREQ_IN_HZ;
  camera_bsp.vd55g1_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.vd55g1_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.vd55g1_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.vd55g1_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.vd55g1_bsp.Delay       = HAL_Delay;
  camera_bsp.vd55g1_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.vd55g1_bsp.EnablePin   = CMW_CAMERA_EnablePin;

  ret = CMW_VD55G1_Probe(&camera_bsp.vd55g1_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_VD55G1_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than VD55G1 */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    initSensors_params->width = VD55G1_MAX_WIDTH;
    initSensors_params->height = VD55G1_MAX_HEIGHT;
  }

  CMW_VD55G1_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_VD55G1_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_ONE_DATA_LANE;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_800;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW10:
    case CMW_PIXEL_FORMAT_DEFAULT:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_VD65G4_SENSOR)
static int32_t CMW_CAMERA_VD65G4_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_VD65G4_config_t default_sensor_config;
  CMW_VD65G4_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.vd65g4_bsp.Address     = CAMERA_VD65G4_ADDRESS;
  camera_bsp.vd65g4_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.vd65g4_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.vd65g4_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.vd65g4_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.vd65g4_bsp.Delay       = HAL_Delay;
  camera_bsp.vd65g4_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.vd65g4_bsp.EnablePin   = CMW_CAMERA_EnablePin;

  ret = CMW_VD65G4_Probe(&camera_bsp.vd65g4_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_VD65G4_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than VD65G4 */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0U) || (initSensors_params->height == 0U))
  {
    initSensors_params->width = VD55G1_MAX_WIDTH;
    initSensors_params->height = VD55G1_MAX_HEIGHT;
  }

  CMW_VD65G4_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_VD65G4_config_t*)(initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_ONE_DATA_LANE;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_800;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW10:
    case CMW_PIXEL_FORMAT_DEFAULT:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0U;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_OV5640_SENSOR)
static int32_t CMW_CAMERA_OV5640_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
 CMW_OV5640_config_t default_sensor_config;
 CMW_OV5640_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.ov5640_bsp.Address     = CAMERA_OV5640_ADDRESS;
  camera_bsp.ov5640_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.ov5640_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.ov5640_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.ov5640_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.ov5640_bsp.GetTick     = BSP_GetTick;
  camera_bsp.ov5640_bsp.Delay       = HAL_Delay;
  camera_bsp.ov5640_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.ov5640_bsp.EnablePin   = CMW_CAMERA_EnablePin;

  ret = CMW_OV5640_Probe(&camera_bsp.ov5640_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_OV5640_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than OV5640 */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    ISP_SensorInfoTypeDef sensor_info;
    Camera_Drv.GetSensorInfo(&camera_bsp, &sensor_info);
    initSensors_params->width = sensor_info.width;
    initSensors_params->height = sensor_info.height;
  }

  CMW_OV5640_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_OV5640_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_250;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {

    case CMW_PIXEL_FORMAT_YUV422_8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_YUV422_8;
      break;
    }
    case CMW_PIXEL_FORMAT_DEFAULT:
    case CMW_PIXEL_FORMAT_RGB565:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RGB565;
      break;
    }
    case CMW_PIXEL_FORMAT_RGB888:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RGB888;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
      break;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_VD66GY_SENSOR)
static int32_t CMW_CAMERA_VD66GY_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_VD66GY_config_t default_sensor_config;
  CMW_VD66GY_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.vd66gy_bsp.Address     = CAMERA_VD66GY_ADDRESS;
  camera_bsp.vd66gy_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.vd66gy_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.vd66gy_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.vd66gy_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.vd66gy_bsp.Delay       = HAL_Delay;
  camera_bsp.vd66gy_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.vd66gy_bsp.EnablePin   = CMW_CAMERA_EnablePin;
  camera_bsp.vd66gy_bsp.hdcmipp     = &hcamera_dcmipp;
  camera_bsp.vd66gy_bsp.appliHelpers.SetSensorGain = CB_ISP_SetSensorGain;
  camera_bsp.vd66gy_bsp.appliHelpers.GetSensorGain = CB_ISP_GetSensorGain;
  camera_bsp.vd66gy_bsp.appliHelpers.SetSensorExposure = CB_ISP_SetSensorExposure;
  camera_bsp.vd66gy_bsp.appliHelpers.GetSensorExposure = CB_ISP_GetSensorExposure;
  camera_bsp.vd66gy_bsp.appliHelpers.GetSensorInfo = CB_ISP_GetSensorInfo;

  ret = CMW_VD66GY_Probe(&camera_bsp.vd66gy_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_VD66GY_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than VD66GY */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    ISP_SensorInfoTypeDef sensor_info;
    Camera_Drv.GetSensorInfo(&camera_bsp, &sensor_info);
    initSensors_params->width = sensor_info.width;
    initSensors_params->height = sensor_info.height;
  }

  CMW_VD66GY_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_VD66GY_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_800;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW10:
    case CMW_PIXEL_FORMAT_DEFAULT:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_VD56G3_SENSOR)
static int32_t CMW_CAMERA_VD56G3_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_VD56G3_config_t default_sensor_config;
  CMW_VD56G3_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.vd56g3_bsp.Address     = CAMERA_VD56G3_ADDRESS;
  camera_bsp.vd56g3_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.vd56g3_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.vd56g3_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.vd56g3_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.vd56g3_bsp.Delay       = HAL_Delay;
  camera_bsp.vd56g3_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.vd56g3_bsp.EnablePin   = CMW_CAMERA_EnablePin;

  ret = CMW_VD56G3_Probe(&camera_bsp.vd56g3_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_VD56G3_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    ISP_SensorInfoTypeDef sensor_info;
    Camera_Drv.GetSensorInfo(&camera_bsp, &sensor_info);
    initSensors_params->width = sensor_info.width;
    initSensors_params->height = sensor_info.height;
  }

  CMW_VD56G3_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_VD56G3_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_800;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW10:
    case CMW_PIXEL_FORMAT_DEFAULT:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_VD1943_SENSOR)
static int32_t CMW_CAMERA_VD1943_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_VD1943_config_t default_sensor_config;
  int32_t csi_phybitrate_i = -1;
  CMW_VD1943_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.vd1943_bsp.Address     = CAMERA_VD1943_ADDRESS;
  camera_bsp.vd1943_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.vd1943_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.vd1943_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.vd1943_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.vd1943_bsp.Delay       = HAL_Delay;
  camera_bsp.vd1943_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.vd1943_bsp.EnablePin   = CMW_CAMERA_EnablePin;
  camera_bsp.vd1943_bsp.hdcmipp     = &hcamera_dcmipp;
  camera_bsp.vd1943_bsp.appliHelpers.SetSensorGain = CB_ISP_SetSensorGain;
  camera_bsp.vd1943_bsp.appliHelpers.GetSensorGain = CB_ISP_GetSensorGain;
  camera_bsp.vd1943_bsp.appliHelpers.SetSensorExposure = CB_ISP_SetSensorExposure;
  camera_bsp.vd1943_bsp.appliHelpers.GetSensorExposure = CB_ISP_GetSensorExposure;
  camera_bsp.vd1943_bsp.appliHelpers.GetSensorInfo = CB_ISP_GetSensorInfo;

  ret = CMW_VD1943_Probe(&camera_bsp.vd1943_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_VD1943_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than VD1943 */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    initSensors_params->width = VD1943_MAX_WIDTH;
    initSensors_params->height = VD1943_MAX_HEIGHT;
  }

  CMW_VD1943_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_VD1943_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_phybitrate_i = CMW_UTILS_getClosest_HAL_PHYBitrate(sensor_config->CSI_PHYBitrate);
  if (csi_phybitrate_i < 0)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = csi_phybitrate_i;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW10:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW12:
    case CMW_PIXEL_FORMAT_DEFAULT:
    {
      dt_format = DCMIPP_CSI_DT_BPP12;
      dt = DCMIPP_DT_RAW12;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_VD5943_SENSOR)
static int32_t CMW_CAMERA_VD5943_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_VD5943_config_t default_sensor_config;
  int32_t csi_phybitrate_i = -1;
  CMW_VD5943_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.vd5943_bsp.Address     = CAMERA_VD1943_ADDRESS;
  camera_bsp.vd5943_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.vd5943_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.vd5943_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.vd5943_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.vd5943_bsp.Delay       = HAL_Delay;
  camera_bsp.vd5943_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.vd5943_bsp.EnablePin   = CMW_CAMERA_EnablePin;
  camera_bsp.vd5943_bsp.hdcmipp     = &hcamera_dcmipp;
  camera_bsp.vd5943_bsp.appliHelpers.SetSensorGain = CB_ISP_SetSensorGain;
  camera_bsp.vd5943_bsp.appliHelpers.GetSensorGain = CB_ISP_GetSensorGain;
  camera_bsp.vd5943_bsp.appliHelpers.SetSensorExposure = CB_ISP_SetSensorExposure;
  camera_bsp.vd5943_bsp.appliHelpers.GetSensorExposure = CB_ISP_GetSensorExposure;
  camera_bsp.vd5943_bsp.appliHelpers.GetSensorInfo = CB_ISP_GetSensorInfo;

  ret = CMW_VD5943_Probe(&camera_bsp.vd5943_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_VD5943_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than VD5943 */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    ISP_SensorInfoTypeDef sensor_info;
    Camera_Drv.GetSensorInfo(&camera_bsp, &sensor_info);
    initSensors_params->width = sensor_info.width;
    initSensors_params->height = sensor_info.height;
  }

  CMW_VD5943_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_VD5943_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_phybitrate_i = CMW_UTILS_getClosest_HAL_PHYBitrate(sensor_config->CSI_PHYBitrate);
  if (csi_phybitrate_i < 0)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = CMW_UTILS_getClosest_HAL_PHYBitrate(sensor_config->CSI_PHYBitrate);
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW8:
    {
      dt_format = DCMIPP_CSI_DT_BPP8;
      dt = DCMIPP_DT_RAW8;
      break;
    }
    case CMW_PIXEL_FORMAT_RAW10:
    case CMW_PIXEL_FORMAT_DEFAULT:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return CMW_ERROR_NONE;
}
#endif

#if defined(USE_IMX335_SENSOR)
static int32_t CMW_CAMERA_IMX335_Init(CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  CMW_IMX335_config_t default_sensor_config;
  CMW_IMX335_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.imx335_bsp.Address     = CAMERA_IMX335_ADDRESS;
  camera_bsp.imx335_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.imx335_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.imx335_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.imx335_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.imx335_bsp.GetTick     = BSP_GetTick;
  camera_bsp.imx335_bsp.Delay       = HAL_Delay;
  camera_bsp.imx335_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.imx335_bsp.EnablePin   = CMW_CAMERA_EnablePin;
  camera_bsp.imx335_bsp.hdcmipp     = &hcamera_dcmipp;
  camera_bsp.imx335_bsp.appliHelpers.SetSensorGain = CB_ISP_SetSensorGain;
  camera_bsp.imx335_bsp.appliHelpers.GetSensorGain = CB_ISP_GetSensorGain;
  camera_bsp.imx335_bsp.appliHelpers.SetSensorExposure = CB_ISP_SetSensorExposure;
  camera_bsp.imx335_bsp.appliHelpers.GetSensorExposure = CB_ISP_GetSensorExposure;
  camera_bsp.imx335_bsp.appliHelpers.GetSensorInfo = CB_ISP_GetSensorInfo;

  ret = CMW_IMX335_Probe(&camera_bsp.imx335_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if ((connected_sensor != CMW_IMX335_Sensor) && (connected_sensor != CMW_UNKNOWN_Sensor))
  {
    /* If the selected sensor in the application side has selected a different sensors than IMX335 */
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    ISP_SensorInfoTypeDef sensor_info;
    Camera_Drv.GetSensorInfo(&camera_bsp, &sensor_info);
    initSensors_params->width = sensor_info.width;
    initSensors_params->height = sensor_info.height;
  }

  CMW_IMX335_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_IMX335_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = Camera_Drv.SetFrequency(&camera_bsp, IMX335_INCK_24MHZ);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = Camera_Drv.SetFramerate(&camera_bsp, initSensors_params->fps);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_DEFAULT:
    case CMW_PIXEL_FORMAT_RAW10:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_1600;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }


  return ret;
}
#endif

#if defined(USE_OS04C10_SENSOR)
static int32_t CMW_CAMERA_OS04C10_Init( CMW_Sensor_Init_t *initSensors_params)
{
  int32_t ret = CMW_ERROR_NONE;
  uint32_t dt_format = 0;
  uint32_t dt = 0;
  DCMIPP_CSI_ConfTypeDef csi_conf = { 0 };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_conf = { 0 };
  CMW_OS04C10_config_t default_sensor_config = { 0 };
  CMW_OS04C10_config_t *sensor_config;

  memset(&camera_bsp, 0, sizeof(camera_bsp));
  camera_bsp.os04c10_bsp.Address     = CAMERA_OS04C10_ADDRESS;
  camera_bsp.os04c10_bsp.Init        = CMW_I2C_INIT;
  camera_bsp.os04c10_bsp.DeInit      = CMW_I2C_DEINIT;
  camera_bsp.os04c10_bsp.ReadReg     = CMW_I2C_READREG16;
  camera_bsp.os04c10_bsp.WriteReg    = CMW_I2C_WRITEREG16;
  camera_bsp.os04c10_bsp.GetTick     = BSP_GetTick;
  camera_bsp.os04c10_bsp.Delay       = (void (*)(uint32_t))osDelay;
  camera_bsp.os04c10_bsp.ShutdownPin = CMW_CAMERA_ShutdownPin;
  camera_bsp.os04c10_bsp.EnablePin   = CMW_CAMERA_EnablePin;
  camera_bsp.os04c10_bsp.hdcmipp     = &hcamera_dcmipp;
  camera_bsp.os04c10_bsp.appliHelpers.SetSensorGain = CB_ISP_SetSensorGain;
  camera_bsp.os04c10_bsp.appliHelpers.GetSensorGain = CB_ISP_GetSensorGain;
  camera_bsp.os04c10_bsp.appliHelpers.SetSensorExposure = CB_ISP_SetSensorExposure;
  camera_bsp.os04c10_bsp.appliHelpers.GetSensorExposure = CB_ISP_GetSensorExposure;
  camera_bsp.os04c10_bsp.appliHelpers.GetSensorInfo = CB_ISP_GetSensorInfo;

  ret = CMW_OS04C10_Probe(&camera_bsp.os04c10_bsp, &Camera_Drv);
  if (ret != CMW_ERROR_NONE)
  {
    // printf("CMW_OS04C10_Probe failed :%ld \r\n", ret);
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  /* Special case: when resolution is not specified take the full sensor resolution */
  if ((initSensors_params->width == 0) || (initSensors_params->height == 0))
  {
    ISP_SensorInfoTypeDef sensor_info;
    Camera_Drv.GetSensorInfo(&camera_bsp, &sensor_info);
    initSensors_params->width = sensor_info.width;
    initSensors_params->height = sensor_info.height;
  }

  CMW_OS04C10_SetDefaultSensorValues(&default_sensor_config);
  initSensors_params->sensor_config = initSensors_params->sensor_config ? initSensors_params->sensor_config : &default_sensor_config;
  sensor_config = (CMW_OS04C10_config_t*) (initSensors_params->sensor_config);

  ret = Camera_Drv.Init(&camera_bsp, initSensors_params);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_DEFAULT:
    case CMW_PIXEL_FORMAT_RAW10:
    {
      dt_format = DCMIPP_CSI_DT_BPP10;
      dt = DCMIPP_DT_RAW10;
      break;
    }
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
  }

  csi_conf.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
  csi_conf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csi_conf.PHYBitrate = DCMIPP_CSI_PHY_BT_1600;
  ret = HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  ret = HAL_DCMIPP_CSI_SetVCConfig(&hcamera_dcmipp, DCMIPP_VIRTUAL_CHANNEL0, dt_format);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  csi_pipe_conf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  csi_pipe_conf.DataTypeIDA = dt;
  csi_pipe_conf.DataTypeIDB = 0;
  /* Pre-initialize CSI config for all the pipes */
  for (uint32_t i = DCMIPP_PIPE0; i <= DCMIPP_PIPE2; i++)
  {
    ret = HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, i, &csi_pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_PERIPH_FAILURE;
    }
  }

  return ret;
}
#endif

static int32_t CMW_CAMERA_SetPipe(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe, CMW_DCMIPP_Conf_t *p_conf, uint32_t *pitch)
{
  DCMIPP_DecimationConfTypeDef dec_conf = { 0 };
  DCMIPP_PipeConfTypeDef pipe_conf = { 0 };
  DCMIPP_DownsizeTypeDef down_conf = { 0 };
  DCMIPP_CropConfTypeDef crop_conf = { 0 };
  int ret;

  /* specific case for pipe0 which is only a dump pipe */
  if (pipe == DCMIPP_PIPE0)
  {
    /*  TODO: properly configure the dump pipe with decimation and crop */
    pipe_conf.FrameRate = DCMIPP_FRAME_RATE_ALL;
    ret = HAL_DCMIPP_PIPE_SetConfig(hdcmipp, pipe, &pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }

    return CMW_ERROR_NONE;
  }

  CMW_UTILS_GetPipeConfig(camera_conf.width, camera_conf.height, p_conf, &crop_conf, &dec_conf, &down_conf);

  if (crop_conf.VSize != 0 || crop_conf.HSize != 0)
  {
    ret = HAL_DCMIPP_PIPE_SetCropConfig(hdcmipp, pipe, &crop_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }

    ret = HAL_DCMIPP_PIPE_EnableCrop(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }
  else
  {
    ret = HAL_DCMIPP_PIPE_DisableCrop(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }

  if (dec_conf.VRatio != 0 || dec_conf.HRatio != 0)
  {
    ret = HAL_DCMIPP_PIPE_SetDecimationConfig(hdcmipp, pipe, &dec_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }

    ret = HAL_DCMIPP_PIPE_EnableDecimation(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }
  else
  {
    ret = HAL_DCMIPP_PIPE_DisableDecimation(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }

  ret = HAL_DCMIPP_PIPE_SetDownsizeConfig(hdcmipp, pipe, &down_conf);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = HAL_DCMIPP_PIPE_EnableDownsize(hdcmipp, pipe);
  if (ret != HAL_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  if (p_conf->enable_swap)
  {
    /* Config pipe */
    ret = HAL_DCMIPP_PIPE_EnableRedBlueSwap(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }
  else
  {
    ret = HAL_DCMIPP_PIPE_DisableRedBlueSwap(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }

  /* Ignore the configuration of gamma if -1
   * Activation is then done by the ISP Library
   */
  if (p_conf->enable_gamma_conversion > -1)
  {
    if (p_conf->enable_gamma_conversion)
    {
      ret = HAL_DCMIPP_PIPE_EnableGammaConversion(hdcmipp, pipe);
      if (ret != HAL_OK)
      {
        return CMW_ERROR_COMPONENT_FAILURE;
      }
    }
    else
    {
      ret = HAL_DCMIPP_PIPE_DisableGammaConversion(hdcmipp, pipe);
      if (ret != HAL_OK)
      {
        return CMW_ERROR_COMPONENT_FAILURE;
      }
    }
  }

  if (pipe == DCMIPP_PIPE2)
  {
    if (!is_pipe1_2_shared)
    {
      ret = HAL_DCMIPP_PIPE_CSI_EnableShare(hdcmipp, pipe);
      if (ret != HAL_OK)
      {
        return CMW_ERROR_COMPONENT_FAILURE;
      }
      is_pipe1_2_shared++;
    }
  }

  pipe_conf.FrameRate = DCMIPP_FRAME_RATE_ALL;
  pipe_conf.PixelPipePitch = p_conf->output_width * p_conf->output_bpp;
  /* Hardware constraint, pitch must be multiple of 16 */
  pipe_conf.PixelPipePitch = (pipe_conf.PixelPipePitch + 15) & (uint32_t) ~15;
  pipe_conf.PixelPackerFormat = p_conf->output_format;

  /* Support of YUV pixel format */
  if (pipe_conf.PixelPackerFormat == DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1)
  {
    if (pipe != DCMIPP_PIPE1)
    {
      /* Only pipe 1 support YUV conversion */
      return CMW_ERROR_FEATURE_NOT_SUPPORTED;
    }

    #define N10(val) (((val) ^ 0x7FF) + 1)
    DCMIPP_ColorConversionConfTypeDef yuv_color_conf = {
    .ClampOutputSamples = ENABLE,
    .OutputSamplesType = 0,
    .RR = 131,     .RG = N10(110), .RB = N10(21), .RA = 128,
    .GR = 77,      .GG = 150,      .GB = 29,      .GA = 0,
    .BR = N10(44), .BG = N10(87),  .BB = 131,     .BA = 128,
    };

    ret = HAL_DCMIPP_PIPE_SetYUVConversionConfig(hdcmipp, pipe, &yuv_color_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
    ret = HAL_DCMIPP_PIPE_EnableYUVConversion(hdcmipp, pipe);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }

  if (hcamera_dcmipp.PipeState[pipe] == HAL_DCMIPP_PIPE_STATE_RESET)
  {
    ret = HAL_DCMIPP_PIPE_SetConfig(hdcmipp, pipe, &pipe_conf);
    if (ret != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }
  else
  {
    if (HAL_DCMIPP_PIPE_SetPixelPackerFormat(hdcmipp, pipe, pipe_conf.PixelPackerFormat) != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }

    if (HAL_DCMIPP_PIPE_SetPitch(hdcmipp, pipe, pipe_conf.PixelPipePitch) != HAL_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }
  }

  /* Update the pitch field so that application can use this information for
   * buffer alignement */
  *pitch = pipe_conf.PixelPipePitch;

  return CMW_ERROR_NONE;
}

int32_t CMW_CAMERA_SetDefaultSensorValues( CMW_Advanced_Config_t *advanced_config )
{
  if (advanced_config == NULL)
  {
    return CMW_ERROR_WRONG_PARAM;
  }
  switch (advanced_config->selected_sensor)
  {
#if defined(USE_VD66GY_SENSOR)
  case CMW_VD66GY_Sensor:
    CMW_VD66GY_SetDefaultSensorValues(&advanced_config->config_sensor.vd66gy_config);
    break;
#endif
#if defined(USE_VD55G1_SENSOR)
  case CMW_VD55G1_Sensor:
    CMW_VD55G1_SetDefaultSensorValues(&advanced_config->config_sensor.vd55g1_config);
    break;
#endif
#if defined(USE_VD65G4_SENSOR)
  case CMW_VD65G4_Sensor:
    CMW_VD65G4_SetDefaultSensorValues(&advanced_config->config_sensor.vd65g4_config);
    break;
#endif
#if defined(USE_IMX335_SENSOR)
  case CMW_IMX335_Sensor:
    CMW_IMX335_SetDefaultSensorValues(&advanced_config->config_sensor.imx335_config);
    break;
#endif
#if defined(USE_OV5640_SENSOR)
  case CMW_OV5640_Sensor:
    CMW_OV5640_SetDefaultSensorValues(&advanced_config->config_sensor.ov5640_config);
    break;
#endif
#if defined(USE_VD1943_SENSOR)
  case CMW_VD1943_Sensor:
    CMW_VD1943_SetDefaultSensorValues(&advanced_config->config_sensor.vd1943_config);
    break;
#endif
#if defined(USE_VD5943_SENSOR)
  case CMW_VD5943_Sensor:
    CMW_VD5943_SetDefaultSensorValues(&advanced_config->config_sensor.vd5943_config);
    break;
#endif
#if defined(USE_OS04C10_SENSOR)
  case CMW_OS04C10_Sensor:
    CMW_OS04C10_SetDefaultSensorValues(&advanced_config->config_sensor.os04c10_config);
    break;
#endif
  default:
    return CMW_ERROR_WRONG_PARAM;
    break;
  }

  return CMW_ERROR_NONE;
}

/**
  * @brief  Error callback on the pipe. Occurs when overrun occurs on the pipe.
  * @param  hdcmipp  Pointer to DCMIPP handle
  * @param  Pipe     Specifies the DCMIPP pipe, can be a value from @ref DCMIPP_Pipes
  * @retval None
  */
void HAL_DCMIPP_PIPE_ErrorCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  UNUSED(hdcmipp);

  CMW_CAMERA_PIPE_ErrorCallback(Pipe);
}
