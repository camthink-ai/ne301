/**
  ******************************************************************************
  * @file    cmw_os04c10.h
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

#ifndef CMW_OS04C10
#define CMW_OS04C10

#ifdef __cplusplus
  extern "C" {
#endif
  
#include <stdint.h>
#include "cmw_sensors_if.h"
#include "cmw_errno.h"
#include "os04c10.h"
#include "isp_api.h"
#include "cmw_camera.h"
  
typedef struct
{
  uint16_t Address;
  uint32_t ClockInHz;
  OS04C10_Object_t ctx_driver;
  ISP_HandleTypeDef hIsp;
  ISP_AppliHelpersTypeDef appliHelpers;
  DCMIPP_HandleTypeDef *hdcmipp;
  uint8_t IsInitialized;
  int32_t (*Init)(void);
  int32_t (*DeInit)(void);
  int32_t (*WriteReg)(uint16_t, uint16_t, uint8_t*, uint16_t);
  int32_t (*ReadReg) (uint16_t, uint16_t, uint8_t*, uint16_t);
  int32_t (*GetTick) (void);
  void (*Delay)(uint32_t delay_in_ms);
  void (*ShutdownPin)(int value);
  void (*EnablePin)(int value);
} CMW_OS04C10_t;
  
int CMW_OS04C10_Probe(CMW_OS04C10_t *io_ctx, CMW_Sensor_if_t *vd55g1_if);
void CMW_OS04C10_SetDefaultSensorValues(CMW_OS04C10_config_t *os04c10_config);

/* Runtime exposure-cap override (motion-blur test): 0 = driver default max */
void CMW_OS04C10_SetExposureCapOverride(uint32_t cap_us);
uint32_t CMW_OS04C10_GetExposureCapOverride(void);

#ifdef __cplusplus
}
#endif

#endif