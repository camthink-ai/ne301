/**
  ******************************************************************************
  * @file    cmw_vd5943.c
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

#include "cmw_vd5943.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "cmw_camera.h"
#include "vd1943.h"
#include "isp_param_conf.h"
extern const ISP_IQParamTypeDef *user_isp_init_param;

#define container_of(ptr, type, member) (type *) ((unsigned char *)ptr - offsetof(type,member))

#ifndef MIN
#define MIN(a, b)                           ((a) < (b) ?  (a) : (b))
#endif
#define MDECIBEL_TO_LINEAR(mdB)             (pow(10.0, (mdB / 1000.0) / 20.0))
#define LINEAR_TO_MDECIBEL(linearValue)     1000 * (20.0 * log10(linearValue))
#define FLOAT_TO_FP58(x)                    (((uint16_t)(x) << 8) | ((uint16_t)((x - (uint16_t)(x)) * 256.0f) & 0xFF))
#define FP58_TO_FLOAT(fp)                   (((fp) >> 8) + ((fp) & 0xFF) / 256.0f)

#define VD5943_REG_MODEL_ID                           0x0000
#define VD5943_NAME                                   "VD5943"

static int CMW_VD5943_Read8(CMW_VD5943_t *pObj, uint16_t addr, uint8_t *value)
{
  return pObj->ReadReg(pObj->Address, addr, value, 1);
}

static int CMW_VD5943_Read16(CMW_VD5943_t *pObj, uint16_t addr, uint16_t *value)
{
  uint8_t data[2];
  int ret;

  ret = pObj->ReadReg(pObj->Address, addr, data, 2);
  if (ret)
    return ret;

  *value = (data[1] << 8) | data[0];

  return CMW_ERROR_NONE;
}

static int CMW_VD5943_Read32(CMW_VD5943_t *pObj, uint16_t addr, uint32_t *value)
{
  uint8_t data[4];
  int ret;

  ret = pObj->ReadReg(pObj->Address, addr, data, 4);
  if (ret)
    return ret;

  *value = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];

  return 0;
}

static int CMW_VD5943_Write8(CMW_VD5943_t *pObj, uint16_t addr, uint8_t value)
{
  return pObj->WriteReg(pObj->Address, addr, &value, 1);
}

static int CMW_VD5943_Write16(CMW_VD5943_t *pObj, uint16_t addr, uint16_t value)
{
  return pObj->WriteReg(pObj->Address, addr, (uint8_t *) &value, 2);
}

static int CMW_VD5943_Write32(CMW_VD5943_t *pObj, uint16_t addr, uint32_t value)
{
  return pObj->WriteReg(pObj->Address, addr, (uint8_t *) &value, 4);
}

static void VD5943_ShutdownPin(struct VD1943_Ctx *ctx, int value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  p_ctx->ShutdownPin(value);
}

static int VD5943_Read8(struct VD1943_Ctx *ctx, uint16_t addr, uint8_t *value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  return CMW_VD5943_Read8(p_ctx, addr, value);
}

static int VD5943_Read16(struct VD1943_Ctx *ctx, uint16_t addr, uint16_t *value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  return CMW_VD5943_Read16(p_ctx, addr, value);
}

static int VD5943_Read32(struct VD1943_Ctx *ctx, uint16_t addr, uint32_t *value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  return CMW_VD5943_Read32(p_ctx, addr, value);
}

static int VD5943_Write8(struct VD1943_Ctx *ctx, uint16_t addr, uint8_t value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  return CMW_VD5943_Write8(p_ctx, addr, value);
}

static int VD5943_Write16(struct VD1943_Ctx *ctx, uint16_t addr, uint16_t value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  return CMW_VD5943_Write16(p_ctx, addr, value);
}

static int VD5943_Write32(struct VD1943_Ctx *ctx, uint16_t addr, uint32_t value)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  return CMW_VD5943_Write32(p_ctx, addr, value);
}

static int VD5943_WriteArray(struct VD1943_Ctx *ctx, uint16_t addr, uint8_t *data, int data_len)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);
  const unsigned int chunk_size = 128;
  uint16_t sz;
  int ret;

  while (data_len) {
    sz = MIN(data_len, chunk_size);
    ret = p_ctx->WriteReg(p_ctx->Address, addr, data, sz);
    if (ret)
      return ret;
    data_len -= sz;
    addr += sz;
    data += sz;
  }

  return 0;
}

static void VD5943_Delay(struct VD1943_Ctx *ctx, uint32_t delay_in_ms)
{
  CMW_VD5943_t *p_ctx = container_of(ctx, CMW_VD5943_t, ctx_driver);

  p_ctx->Delay(delay_in_ms);
}

static void VD5943_Log(struct VD1943_Ctx *ctx, int lvl, const char *format, va_list ap)
{
#if 1
  const int current_lvl = VD1943_LVL_DBG(2);

  if (lvl > current_lvl)
    return ;

  vprintf(format, ap);
#endif
}

static int CMW_VD5943_GetResType(uint32_t width, uint32_t height, VD1943_Res_t *res)
{
  if (width == 320 && height == 240)
  {
    *res = VD1943_RES_QVGA_320_240;
  }
  else if (width == 640 && height == 480)
  {
    *res = VD1943_RES_VGA_640_480;
  }
  else if (width == 800 && height == 600)
  {
    *res = VD1943_RES_SVGA_800_600;
  }
  else if (width == 1280 && height == 720)
  {
    *res = VD1943_RES_720P_1280_720;
  }
  else if (width == 1920 && height == 1080)
  {
    *res = VD1943_RES_1080P_1920_1080;
  }
  else if (width == 2560 && height == 1984)
  {
    *res = VD1943_RES_FULL_2560_1984;
  }
  else
  {
    return CMW_ERROR_WRONG_PARAM;
  }
  return 0;
}

static int32_t CMW_VD5943_getMirrorFlipConfig(uint32_t Config)
{
  switch (Config) {
    case CMW_MIRRORFLIP_NONE:
      return VD1943_MIRROR_FLIP_NONE;
      break;
    case CMW_MIRRORFLIP_FLIP:
      return VD1943_FLIP;
      break;
    case CMW_MIRRORFLIP_MIRROR:
      return VD1943_MIRROR;
      break;
    case CMW_MIRRORFLIP_FLIP_MIRROR:
      return VD1943_MIRROR_FLIP;
      break;
    default:
      return CMW_ERROR_PERIPH_FAILURE;
  }
}

static int32_t CMW_VD5943_Init(void *io_ctx, CMW_Sensor_Init_t *initSensor)
{
  VD1943_Config_t config = { 0 };
  int ret;
  int i;
  VD1943_MODE_t mode;
  CMW_VD5943_config_t *sensor_config;
  sensor_config = (CMW_VD5943_config_t*)(initSensor->sensor_config);
  if (sensor_config == NULL)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  if (((CMW_VD5943_t *)io_ctx)->IsInitialized)
  {
    return CMW_ERROR_NONE;
  }

  ret = CMW_VD5943_GetResType(initSensor->width, initSensor->height, &config.resolution);
  if (ret)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  config.frame_rate = initSensor->fps;

  switch (sensor_config->pixel_format)
  {
    case CMW_PIXEL_FORMAT_RAW10:
      mode = VD1943_GS_SS1_NATIVE_10;
      break;
    case CMW_PIXEL_FORMAT_DEFAULT:
    case CMW_PIXEL_FORMAT_RAW8:
      mode = VD1943_GS_SS1_NATIVE_8;
      break;
    default:
      return CMW_ERROR_COMPONENT_FAILURE;
      break;
  }

  config.image_processing_mode = mode;
  config.ext_clock_freq_in_hz = 25000000;
  config.flip_mirror_mode = CMW_VD5943_getMirrorFlipConfig(initSensor->mirrorFlip);
  config.patgen = VD1943_PATGEN_DISABLE;
  /* setup csi2 itf */
  config.out_itf.data_rate_in_mps = sensor_config->CSI_PHYBitrate;
  config.out_itf.datalane_nb = 2;
  config.out_itf.logic_lane_mapping[0] = 0;
  config.out_itf.logic_lane_mapping[1] = 1;
  config.out_itf.logic_lane_mapping[2] = 2;
  config.out_itf.logic_lane_mapping[3] = 3;
  config.out_itf.clock_lane_swap_enable = 1;
  config.out_itf.physical_lane_swap_enable[0] = 1;
  config.out_itf.physical_lane_swap_enable[1] = 1;
  /* gpios as input */
  for (i = 0; i < VD1943_GPIO_NB; i++)
  {
    config.gpios[i].gpio_ctrl = VD1943_GPIO_IN;
    config.gpios[i].enable = 0;
  }
  /* Default VT mode*/
  config.sync_mode = VD1943_MASTER;

  ret = VD1943_Init(&((CMW_VD5943_t *)io_ctx)->ctx_driver, &config);
  if (ret)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  /* Ensure mono variant: Bayer pattern must be NONE (= Mono) */
  if (((CMW_VD5943_t *)io_ctx)->ctx_driver.bayer != VD1943_BAYER_NONE)
  {
    VD1943_DeInit(&((CMW_VD5943_t *)io_ctx)->ctx_driver);
    return CMW_ERROR_PERIPH_FAILURE;
  }

  /* This gain / expo combo give correct starting point for 50 Hz flicker free and correct
   * illumination in desk environment.
   */
  ret = VD1943_SetDigitalGain(&((CMW_VD5943_t *)io_ctx)->ctx_driver, 0x100);
  if (ret)
  {
     return CMW_ERROR_COMPONENT_FAILURE;
  }
  ret = VD1943_SetExpo(&((CMW_VD5943_t *)io_ctx)->ctx_driver, 10000);
  if (ret)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ((CMW_VD5943_t *)io_ctx)->IsInitialized = 1;

  return CMW_ERROR_NONE;
}

void CMW_VD5943_SetDefaultSensorValues(CMW_VD5943_config_t *vd5943_config)
{
  assert(vd5943_config != NULL);
  vd5943_config->CSI_PHYBitrate = VD1943_DEFAULT_DATARATE;
  vd5943_config->pixel_format = CMW_PIXEL_FORMAT_RAW10;
}

static int32_t CMW_VD5943_Start(void *io_ctx)
{
  int ret = CMW_ERROR_NONE;

  /* Statistic area is provided with null value so that it force the ISP Library to get the statistic
   * area information from the tuning file.
   */
  (void) ISP_IQParamCacheInit; /* unused */
  // Use user-provided ISP init param if available, otherwise use default
  const ISP_IQParamTypeDef *init_param = (user_isp_init_param != NULL) ? user_isp_init_param : &ISP_IQParamCacheInit_VD5943;
  ret = ISP_Init(&((CMW_VD5943_t *)io_ctx)->hIsp, ((CMW_VD5943_t *)io_ctx)->hdcmipp, 0, &((CMW_VD5943_t *)io_ctx)->appliHelpers, init_param);
  if (ret != ISP_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = ISP_Start(&((CMW_VD5943_t *)io_ctx)->hIsp);
  if (ret != ISP_OK)
  {
      return CMW_ERROR_PERIPH_FAILURE;
  }

  ret = VD1943_Start(&((CMW_VD5943_t *)io_ctx)->ctx_driver);
  if (ret) {
    VD1943_DeInit(&((CMW_VD5943_t *)io_ctx)->ctx_driver);
    return CMW_ERROR_PERIPH_FAILURE;
  }
  return CMW_ERROR_NONE;
}

static int32_t CMW_VD5943_Run(void *io_ctx)
{
  int ret;
  ret = ISP_BackgroundProcess(&((CMW_VD5943_t *)io_ctx)->hIsp);
  if (ret != ISP_OK)
  {
      return CMW_ERROR_PERIPH_FAILURE;
  }
  return CMW_ERROR_NONE;
}

static void CMW_VD5943_VsyncEventCallback(void *io_ctx, uint32_t pipe)
{
  /* Update the ISP frame counter and call its statistics handler */
  switch (pipe)
  {
    case DCMIPP_PIPE0 :
      ISP_IncDumpFrameId(&((CMW_VD5943_t *)io_ctx)->hIsp);
      break;
    case DCMIPP_PIPE1 :
      ISP_IncMainFrameId(&((CMW_VD5943_t *)io_ctx)->hIsp);
      ISP_GatherStatistics(&((CMW_VD5943_t *)io_ctx)->hIsp);
      break;
    case DCMIPP_PIPE2 :
      ISP_IncAncillaryFrameId(&((CMW_VD5943_t *)io_ctx)->hIsp);
      break;
  }
}

static void CMW_VD5943_FrameEventCallback(void *io_ctx, uint32_t pipe)
{
}

static int32_t CMW_VD5943_Stop(void *io_ctx)
{
  int ret = CMW_ERROR_NONE;

  ret = VD1943_Stop(&((CMW_VD5943_t *)io_ctx)->ctx_driver);
  if (ret)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }
  return CMW_ERROR_NONE;
}

static int32_t CMW_VD5943_DeInit(void *io_ctx)
{
  int ret = CMW_ERROR_NONE;

  ret = VD1943_Stop(&((CMW_VD5943_t *)io_ctx)->ctx_driver);
  if (ret)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  ret = VD1943_DeInit(&((CMW_VD5943_t *)io_ctx)->ctx_driver);
  if (ret)
  {
    return CMW_ERROR_PERIPH_FAILURE;
  }

  ((CMW_VD5943_t *)io_ctx)->IsInitialized = 0;
  return CMW_ERROR_NONE;
}

/**
  * @brief  Set the gain
  * @param  pObj  pointer to component object
  * @param  Gain Gain in mdB
  * @retval Component status
  */
int32_t CMW_VD5943_SetGain(void *io_ctx, int32_t gain)
{
  int32_t ret;
  uint32_t again_min_mdB, again_max_mdB;
  uint32_t dgain_min_mdB, dgain_max_mdB;
  double analog_linear_gain, digital_linear_gain;
  double again_reg_f;
  unsigned int again_reg;

  again_min_mdB = LINEAR_TO_MDECIBEL(16.0 / (16.0 - VD1943_ANALOG_GAIN_MIN));
  again_max_mdB = LINEAR_TO_MDECIBEL(16.0 / (16.0 - VD1943_ANALOG_GAIN_MAX));
  dgain_min_mdB = LINEAR_TO_MDECIBEL(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MIN));
  dgain_max_mdB = LINEAR_TO_MDECIBEL(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MAX));

  if ((gain < dgain_min_mdB + again_min_mdB)
      || (gain > dgain_max_mdB + again_max_mdB))
    return -1;

  if (gain <= again_max_mdB)
  {
    /* Use analog gain only and set digital gain to its minimum */
    analog_linear_gain = MDECIBEL_TO_LINEAR((double)(gain - dgain_min_mdB));
    digital_linear_gain = MDECIBEL_TO_LINEAR((double)dgain_min_mdB);
    /* Rounding to reach full analog register when needed */
    again_reg_f = 16.0 - (16.0 / analog_linear_gain);
    again_reg = (unsigned int)(again_reg_f + 0.5);
    if (again_reg > VD1943_ANALOG_GAIN_MAX)
      again_reg = VD1943_ANALOG_GAIN_MAX;
  }
  else
  {
    /* Analog saturated, remainder goes to digital */
    again_reg = VD1943_ANALOG_GAIN_MAX;
    digital_linear_gain = MDECIBEL_TO_LINEAR((double)(gain - again_max_mdB));
  }

  ret = VD1943_SetAnalogGain(&((CMW_VD5943_t *)io_ctx)->ctx_driver, again_reg);
  if (ret)
    return ret;

  ret = VD1943_SetDigitalGain(&((CMW_VD5943_t *)io_ctx)->ctx_driver, FLOAT_TO_FP58(digital_linear_gain));
  if (ret)
    return ret;

  return 0;
}

/**
  * @brief  Set the exposure
  * @param  pObj  pointer to component object
  * @param  Exposure Exposure in micro seconds
  * @retval Component status
  */
int32_t CMW_VD5943_SetExposure(void *io_ctx, int32_t exposure)
{
  return VD1943_SetExpo(&((CMW_VD5943_t *)io_ctx)->ctx_driver, exposure);
}

/**
  * @brief  Get the sensor info
  * @param  pObj  pointer to component object
  * @param  pInfo pointer to sensor info structure
  * @retval Component status
  */
int32_t CMW_VD5943_GetSensorInfo(void *io_ctx, ISP_SensorInfoTypeDef *info)
{
  uint32_t again_min_mdB, again_max_mdB;
  uint32_t dgain_min_mdB, dgain_max_mdB;
  unsigned int min_us, max_us, pixel_depth;
  int32_t ret;

  /* Define dummy values */
  const unsigned int INVALID_PIXEL_DEPTH = 0;
  const unsigned int INVALID_MIN_US = 0XFF;
  const unsigned int INVALID_MAX_US = 0;

  if ((!io_ctx) || (info == NULL))
    return CMW_ERROR_WRONG_PARAM;

  /* Sensor name */
  if (sizeof(info->name) >= strlen(VD5943_NAME) + 1)
  {
    strcpy(info->name, VD5943_NAME);
  }
  else
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  /* Isp bayer pattern info */
  info->bayer_pattern = 0x04; /* Monochrome */

  /* Pixel depth */
  ret = VD1943_GetPixelDepth(&((CMW_VD5943_t *)io_ctx)->ctx_driver, &pixel_depth);
  if (ret)
  {
    pixel_depth = INVALID_PIXEL_DEPTH;
  }
  info->color_depth = (uint8_t) pixel_depth;

  /* Resolution info */
  info->width = VD1943_MAX_WIDTH;
  info->height = VD1943_MAX_HEIGHT;

  /* Gain range */
  again_min_mdB = LINEAR_TO_MDECIBEL(16.0 / (16.0 - VD1943_ANALOG_GAIN_MIN));
  again_max_mdB = LINEAR_TO_MDECIBEL(16.0 / (16.0 - VD1943_ANALOG_GAIN_MAX));
  dgain_min_mdB = LINEAR_TO_MDECIBEL(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MIN));
  dgain_max_mdB = LINEAR_TO_MDECIBEL(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MAX));

  info->gain_min = again_min_mdB + dgain_min_mdB;
  info->gain_max = again_max_mdB + dgain_max_mdB;
  info->again_max = again_max_mdB;

  /* Get exposure range */
  ret = VD1943_GetExposureRange(&((CMW_VD5943_t *)io_ctx)->ctx_driver, &min_us, &max_us);
  if (ret)
  {
    min_us = INVALID_MIN_US;
    max_us = INVALID_MAX_US;
  }
  info->exposure_min = (uint32_t) min_us;
  info->exposure_max = (uint32_t) max_us;

  return CMW_ERROR_NONE;
}

int32_t VD5943_RegisterBusIO(CMW_VD5943_t *io_ctx)
{
  int ret;

  if (!io_ctx)
    return CMW_ERROR_COMPONENT_FAILURE;

  if (!io_ctx->Init)
    return CMW_ERROR_COMPONENT_FAILURE;

  ret = io_ctx->Init();

  return ret;
}

int32_t VD5943_ReadID(CMW_VD5943_t *io_ctx, uint32_t *Id)
{
  uint32_t reg32;
  int32_t ret;

  ret = CMW_VD5943_Read32(io_ctx, VD5943_REG_MODEL_ID, &reg32);
  if (ret)
    return ret;

  *Id = reg32;

  return CMW_ERROR_NONE;
}

static void CMW_VD5943_PowerOn(CMW_VD5943_t *io_ctx)
{
  /* Camera sensor Power-On sequence */
  /* Assert the camera  NRST pins */
  io_ctx->EnablePin(1);
  io_ctx->ShutdownPin(0);  /* Disable MB1723 2V8 signal  */
  HAL_Delay(200);   /* NRST signals asserted during 200ms */
  /* De-assert the camera STANDBY pin (active high) */
  io_ctx->ShutdownPin(1);  /* Disable MB1723 2V8 signal  */
	HAL_Delay(20);     /* NRST de-asserted during 20ms */
}

int CMW_VD5943_Probe(CMW_VD5943_t *io_ctx, CMW_Sensor_if_t *vd5943_if)
{
  int ret = CMW_ERROR_NONE;
  uint32_t id;

  io_ctx->ctx_driver.shutdown_pin = VD5943_ShutdownPin;
  io_ctx->ctx_driver.read8 = VD5943_Read8;
  io_ctx->ctx_driver.read16 = VD5943_Read16;
  io_ctx->ctx_driver.read32 = VD5943_Read32;
  io_ctx->ctx_driver.write8 = VD5943_Write8;
  io_ctx->ctx_driver.write16 = VD5943_Write16;
  io_ctx->ctx_driver.write32 = VD5943_Write32;
  io_ctx->ctx_driver.write_array = VD5943_WriteArray;
  io_ctx->ctx_driver.delay = VD5943_Delay;
  io_ctx->ctx_driver.log = VD5943_Log;

  CMW_VD5943_PowerOn(io_ctx);

  ret = VD5943_RegisterBusIO(io_ctx);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = VD5943_ReadID(io_ctx, &id);
  if (ret != CMW_ERROR_NONE)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  if (id != VD5943_CUT1_3_CHIP_ID && id != VD5943_CUT1_4_CHIP_ID)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  memset(vd5943_if, 0, sizeof(*vd5943_if));
  vd5943_if->Init = CMW_VD5943_Init;
  vd5943_if->DeInit = CMW_VD5943_DeInit;
  vd5943_if->Run = CMW_VD5943_Run;
  vd5943_if->VsyncEventCallback = CMW_VD5943_VsyncEventCallback;
  vd5943_if->FrameEventCallback = CMW_VD5943_FrameEventCallback;
  vd5943_if->Start = CMW_VD5943_Start;
  vd5943_if->Stop = CMW_VD5943_Stop;
  vd5943_if->SetGain = CMW_VD5943_SetGain;
  vd5943_if->SetExposure = CMW_VD5943_SetExposure;
  vd5943_if->GetSensorInfo = CMW_VD5943_GetSensorInfo;

  return ret;
}
