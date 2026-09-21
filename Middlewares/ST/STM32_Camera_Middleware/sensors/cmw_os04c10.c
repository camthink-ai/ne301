/**
******************************************************************************
* @file    cmw_os04c10.c
* @author  MDG Application Team
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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "cmw_os04c10.h"
#include "os04c10_reg.h"
#include "os04c10.h"
#include "isp_param_conf.h"
#include "isp_services.h"

static int isp_is_initialized = 0;
extern const ISP_IQParamTypeDef *user_isp_init_param;

static int CMW_OS04C10_GetResType(uint32_t width, uint32_t height, uint32_t*res)
{
  // if (width == 1920 && height == 1080)
  // {
  //   *res = OS04C10_R1920x1080;
  // }
  // else if (width == 2688 && height == 1520)
  // {
  //   *res = OS04C10_R2688x1520;
  // }
  // else
  // {
  //   return CMW_ERROR_WRONG_PARAM;
  // }

  *res = OS04C10_R2688x1520;
  
  return 0;
}

static int32_t CMW_OS04C10_getMirrorFlipConfig(uint32_t Config)
{
  int32_t ret;

  switch (Config)
  {
    case CMW_MIRRORFLIP_NONE:
      ret = OS04C10_MIRROR_FLIP_NONE;
      break;
    case CMW_MIRRORFLIP_FLIP:
      ret = OS04C10_FLIP;
      break;
    case CMW_MIRRORFLIP_MIRROR:
      ret = OS04C10_MIRROR;
      break;
    case CMW_MIRRORFLIP_FLIP_MIRROR:
    default:
      ret = OS04C10_MIRROR_FLIP;
      break;
  }

  return ret;
}

static int32_t CMW_OS04C10_DeInit(void *io_ctx)
{
  int ret = CMW_ERROR_NONE;
  ret = OS04C10_Stop(&((CMW_OS04C10_t *)io_ctx)->ctx_driver);
  if (ret)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = OS04C10_DeInit(&((CMW_OS04C10_t *)io_ctx)->ctx_driver);
  if (ret)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  
  // int ret;

  ret = ISP_DeInit(&((CMW_OS04C10_t *)io_ctx)->hIsp);
  if (ret != ISP_OK)
  {
      return CMW_ERROR_PERIPH_FAILURE;
  }
  isp_is_initialized = 0;
  return ret;
}

static int32_t CMW_OS04C10_ReadID(void *io_ctx, uint32_t *Id)
{
  return OS04C10_ReadID(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, Id);
}

static int32_t CMW_OS04C10_SetGain(void *io_ctx, int32_t gain)
{
  return OS04C10_SetGain(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, gain);
}

static int32_t CMW_OS04C10_SetExposure(void *io_ctx, int32_t exposure)
{
  return OS04C10_SetExposure(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, exposure);
}

static int32_t CMW_OS04C10_SetFrequency(void *io_ctx, int32_t frequency)
{
  return OS04C10_SetFrequency(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, frequency);
}

static int32_t CMW_OS04C10_SetFramerate(void *io_ctx, int32_t framerate)
{
  return OS04C10_SetFramerate(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, framerate);
}

static int32_t CMW_OS04C10_SetMirrorFlip(void *io_ctx, uint32_t config)
{
  int32_t mirrorFlip = CMW_OS04C10_getMirrorFlipConfig(config);
  return OS04C10_MirrorFlipConfig(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, mirrorFlip);
}

static int32_t CMW_OS04C10_GetSensorInfo(void *io_ctx, ISP_SensorInfoTypeDef *info)
{
  if ((io_ctx ==  NULL) || (info == NULL))
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  if (sizeof(info->name) >= strlen(OS04C10_NAME) + 1)
  {
    strcpy(info->name, OS04C10_NAME);
  }
  else
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  info->bayer_pattern = OS04C10_BAYER_PATTERN;
  info->color_depth = OS04C10_COLOR_DEPTH;
  info->width = OS04C10_WIDTH;
  info->height = OS04C10_HEIGHT;
  info->gain_min = OS04C10_GAIN_MIN;
  info->gain_max = OS04C10_GAIN_MAX;
  info->exposure_min = OS04C10_EXPOSURE_MIN;
  info->exposure_max = OS04C10_EXPOSURE_MAX;

  return CMW_ERROR_NONE;
}

static int32_t CMW_OS04C10_SetTestPattern(void *io_ctx, int32_t mode)
{
  return CMW_ERROR_NONE;
}

/* 
static int32_t CMW_OS04C10_SetAEC(void *io_ctx, uint32_t enable)
{
  ISP_SetAECState(&((CMW_OS04C10_t *)io_ctx)->hIsp, enable);
  return CMW_ERROR_NONE;
}

static int32_t CMW_OS04C10_SetContrast(void *io_ctx, int32_t Saturation)
{
  ISP_IQParamTypeDef *IQParamConfig;
  ISP_ContrastTypeDef contrast;
  int ret;
  IQParamConfig = ISP_SVC_IQParam_Get(&((CMW_OS04C10_t *)io_ctx)->hIsp);

  if(Saturation < 0)
    Saturation = 0;
  if(Saturation > 100)
    Saturation = 100;

  contrast.enable = 1;
  contrast.coeff.LUM_0 = (IQParamConfig->contrast.coeff.LUM_0 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_32 = (IQParamConfig->contrast.coeff.LUM_32 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_64 = (IQParamConfig->contrast.coeff.LUM_64 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_96 = (IQParamConfig->contrast.coeff.LUM_96 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_128 = (IQParamConfig->contrast.coeff.LUM_128 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_160 = (IQParamConfig->contrast.coeff.LUM_160 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_192 = (IQParamConfig->contrast.coeff.LUM_192 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_224 = (IQParamConfig->contrast.coeff.LUM_224 * (100 + Saturation)) / 100;
  contrast.coeff.LUM_256 = (IQParamConfig->contrast.coeff.LUM_256 * (100 + Saturation)) / 100;
  ret = ISP_SVC_ISP_SetContrast(&((CMW_OS04C10_t *)io_ctx)->hIsp, &contrast);
  if (ret != ISP_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  return CMW_ERROR_NONE;
}
*/

static int32_t CMW_OS04C10_Init(void *io_ctx, CMW_Sensor_Init_t *initSensor)
{
  int ret = CMW_ERROR_NONE;
  uint32_t resolution;
  CMW_OS04C10_config_t *sensor_config;
  sensor_config = (CMW_OS04C10_config_t*)(initSensor->sensor_config);
  if (sensor_config == NULL)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = CMW_OS04C10_GetResType(initSensor->width, initSensor->height, &resolution);
  if (ret)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = CMW_OS04C10_SetMirrorFlip(io_ctx, initSensor->mirrorFlip);
  if (ret)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = OS04C10_Init(&((CMW_OS04C10_t *)io_ctx)->ctx_driver, resolution, sensor_config->pixel_format);
  if (ret != OS04C10_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  return CMW_ERROR_NONE;
}

void CMW_OS04C10_SetDefaultSensorValues(CMW_OS04C10_config_t *os04c10_config)
{
  assert(os04c10_config != NULL);
  os04c10_config->pixel_format = CMW_PIXEL_FORMAT_RAW10;
}

static int32_t CMW_OS04C10_Start(void *io_ctx)
{
  int ret;
  /* Statistic area is provided with null value so that it force the ISP Library to get the statistic
    * area information from the tuning file.
    */
  __attribute__((unused)) ISP_StatAreaTypeDef isp_stat_area = {0};
  (void) ISP_IQParamCacheInit; /* unused */
  if (isp_is_initialized == 0) {
    // Use user-provided ISP init param if available, otherwise use default
    const ISP_IQParamTypeDef *init_param = (user_isp_init_param != NULL) ? user_isp_init_param : &ISP_IQParamCacheInit_OS04C10;
    ret = ISP_Init(&((CMW_OS04C10_t *)io_ctx)->hIsp, ((CMW_OS04C10_t *)io_ctx)->hdcmipp, 0, &((CMW_OS04C10_t *)io_ctx)->appliHelpers,/* &isp_stat_area,*/ init_param);
    if (ret != ISP_OK)
    {
      return CMW_ERROR_COMPONENT_FAILURE;
    }

    ret = ISP_Start(&((CMW_OS04C10_t *)io_ctx)->hIsp);
    if (ret != ISP_OK)
    {
        return CMW_ERROR_PERIPH_FAILURE;
    }
    isp_is_initialized = 1;
  }
  return OS04C10_Start(&((CMW_OS04C10_t *)io_ctx)->ctx_driver);
  // return CMW_ERROR_NONE;
}

static int32_t CMW_OS04C10_Stop(void *io_ctx)
{
  return OS04C10_Stop(&((CMW_OS04C10_t *)io_ctx)->ctx_driver);
  // return CMW_ERROR_NONE;
}

static int32_t CMW_OS04C10_Run(void *io_ctx)
{
  int ret;
  ret = ISP_BackgroundProcess(&((CMW_OS04C10_t *)io_ctx)->hIsp);
  if (ret != ISP_OK)
  {
      return CMW_ERROR_PERIPH_FAILURE;
  }
  return CMW_ERROR_NONE;
}

static void CMW_OS04C10_PowerOn(CMW_OS04C10_t *io_ctx)
{
  io_ctx->ShutdownPin(0); 
  io_ctx->EnablePin(1);
  io_ctx->Delay(5);
  io_ctx->ShutdownPin(1);
  // io_ctx->Delay(50);
}

static void CMW_OS04C10_VsyncEventCallback(void *io_ctx, uint32_t pipe)
{
  /* Update the ISP frame counter and call its statistics handler */
  switch (pipe)
  {
    case DCMIPP_PIPE0 :
      ISP_IncDumpFrameId(&((CMW_OS04C10_t *)io_ctx)->hIsp);
      break;
    case DCMIPP_PIPE1 :
      ISP_IncMainFrameId(&((CMW_OS04C10_t *)io_ctx)->hIsp);
      ISP_GatherStatistics(&((CMW_OS04C10_t *)io_ctx)->hIsp);
      break;
    case DCMIPP_PIPE2 :
      ISP_IncAncillaryFrameId(&((CMW_OS04C10_t *)io_ctx)->hIsp);
      break;
  }

}

static void CMW_OS04C10_FrameEventCallback(void *io_ctx, uint32_t pipe)
{
  
}

int CMW_OS04C10_Probe(CMW_OS04C10_t *io_ctx, CMW_Sensor_if_t *os04c10_if)
{
  int ret = CMW_ERROR_NONE;
  uint32_t id;
  io_ctx->ctx_driver.IO.Address = io_ctx->Address;
  io_ctx->ctx_driver.IO.Init = io_ctx->Init;
  io_ctx->ctx_driver.IO.DeInit = io_ctx->DeInit;
  io_ctx->ctx_driver.IO.GetTick = io_ctx->GetTick;
  io_ctx->ctx_driver.IO.ReadReg = io_ctx->ReadReg;
  io_ctx->ctx_driver.IO.WriteReg = io_ctx->WriteReg;
  io_ctx->ctx_driver.IO.Delay = (OS04C10_Delay_Func)io_ctx->Delay;

  // printf("CMW_OS04C10_Probe \r\n");
  CMW_OS04C10_PowerOn(io_ctx);

  ret = OS04C10_RegisterBusIO(&io_ctx->ctx_driver, &io_ctx->ctx_driver.IO);
  if (ret != OS04C10_OK)
  {
    // printf("OS04C10_RegisterBusIO  failed\r\n");
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = OS04C10_ReadID(&io_ctx->ctx_driver, &id);
  if (ret != OS04C10_OK)
  {
    // printf("OS04C10_ReadID fialed \r\n");
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  if (id != OS04C10_ID)
  {
      // printf("read ID :%lx \r\n", id);
      ret = CMW_ERROR_UNKNOWN_COMPONENT;
  }

  memset(os04c10_if, 0, sizeof(*os04c10_if));
  os04c10_if->Init = CMW_OS04C10_Init;
  os04c10_if->Start = CMW_OS04C10_Start;
  os04c10_if->DeInit = CMW_OS04C10_DeInit;
  os04c10_if->Run = CMW_OS04C10_Run;
  os04c10_if->Stop = CMW_OS04C10_Stop;
  os04c10_if->VsyncEventCallback = CMW_OS04C10_VsyncEventCallback;
  os04c10_if->FrameEventCallback = CMW_OS04C10_FrameEventCallback;
  os04c10_if->ReadID = CMW_OS04C10_ReadID;
  os04c10_if->SetGain = CMW_OS04C10_SetGain;
  os04c10_if->SetExposure = CMW_OS04C10_SetExposure;
  os04c10_if->SetFrequency = CMW_OS04C10_SetFrequency;
  os04c10_if->SetFramerate = CMW_OS04C10_SetFramerate;
  os04c10_if->SetMirrorFlip = CMW_OS04C10_SetMirrorFlip;
  os04c10_if->GetSensorInfo = CMW_OS04C10_GetSensorInfo;
  os04c10_if->SetTestPattern = CMW_OS04C10_SetTestPattern;
  // os04c10_if->SetAEC = CMW_OS04C10_SetAEC;
  // os04c10_if->SetContrast = CMW_OS04C10_SetContrast;
  return ret;
}
