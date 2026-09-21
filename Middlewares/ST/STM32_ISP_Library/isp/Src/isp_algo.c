/**
 ******************************************************************************
 * @file    isp_algo.c
 * @author  AIS Application Team
 * @brief   ISP algorithm
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "isp_core.h"
#include "isp_algo.h"
#include "isp_services.h"
#include "isp_ae_algo.h"
#include "isp_awb_algo.h"
#include <limits.h>
#include <math.h>
#include <inttypes.h>

/* Private types -------------------------------------------------------------*/
/* ISP algorithms identifier */
typedef enum
{
  ISP_ALGO_ID_BACKGROUND = 0U,
  ISP_ALGO_ID_BADPIXEL,
  ISP_ALGO_ID_AEC,
  ISP_ALGO_ID_AWB,
  ISP_ALGO_ID_SENSOR_DELAY = 255U,
} ISP_AlgoIDTypeDef;

/* Private constants ---------------------------------------------------------*/
/* Delay (in number of VSYNC) between the time an ISP control (e.g. ColorConv)
 * is updated and the time the frame is actually updated. Typical user = AWB algo. */
#define ALGO_ISP_LATENCY                             2
/* Additional delay to let things getting stable after an AWB update */
#define ALGO_AWB_ADDITIONAL_LATENCY                  3

/* Debug logs control */
//#define ALGO_AEC_DBG_LOGS
//#define ALGO_PERF_DBG_LOGS

#ifdef ALGO_PERF_DBG_LOGS
#define MEAS_ITERATION 30
#endif

/* Max acceptable sensor delay */
#define ALGO_DELAY_MAX               10
/* Number of delay test configurations */
#define ALGO_DELAY_NB_CONFIG         12
/* Minimum Luminance update during delay test measurements */
#define ALGO_DELAY_L_MARGIN          3

/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
ISP_StatusTypeDef ISP_Algo_BadPixel_Init(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_BadPixel_DeInit(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_BadPixel_Process(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_AEC_Init(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_AEC_DeInit(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_AEC_Process(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_AWB_Init(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_AWB_DeInit(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_AWB_Process(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_Background_Init(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_Background_DeInit(void *hIsp, void *pAlgo);
ISP_StatusTypeDef ISP_Algo_Background_Process(void *hIsp, void *pAlgo);

/* Private variables ---------------------------------------------------------*/
/* Bad Pixel algorithm handle */
ISP_AlgoTypeDef ISP_Algo_BadPixel = {
    .id = ISP_ALGO_ID_BADPIXEL,
    .Init = ISP_Algo_BadPixel_Init,
    .DeInit = ISP_Algo_BadPixel_DeInit,
    .Process = ISP_Algo_BadPixel_Process,
};

#ifdef ISP_MW_SW_AEC_ALGO_SUPPORT
/* AEC algorithm handle */
ISP_AlgoTypeDef ISP_Algo_AEC = {
    .id = ISP_ALGO_ID_AEC,
    .Init = ISP_Algo_AEC_Init,
    .DeInit = ISP_Algo_AEC_DeInit,
    .Process = ISP_Algo_AEC_Process,
};
#endif /* ISP_MW_SW_AEC_ALGO_SUPPORT */

#ifdef ISP_MW_SW_AWB_ALGO_SUPPORT
/* AWB algorithm handle */
ISP_AlgoTypeDef ISP_Algo_AWB = {
    .id = ISP_ALGO_ID_AWB,
    .Init = ISP_Algo_AWB_Init,
    .DeInit = ISP_Algo_AWB_DeInit,
    .Process = ISP_Algo_AWB_Process,
};
#endif /* ISP_MW_SW_AWB_ALGO_SUPPORT */


/* Background algorithm handle for statistics update */
ISP_AlgoTypeDef ISP_Algo_Background = {
    .id = ISP_ALGO_ID_BACKGROUND,
    .Init = ISP_Algo_Background_Init,
    .DeInit = ISP_Algo_Background_DeInit,
    .Process = ISP_Algo_Background_Process,
};

/* Registered algorithm list */
ISP_AlgoTypeDef *ISP_Algo_List[] = {
    &ISP_Algo_Background,
    &ISP_Algo_BadPixel,
#ifdef ISP_MW_SW_AEC_ALGO_SUPPORT
    &ISP_Algo_AEC,
#endif /* ISP_MW_SW_AEC_ALGO_SUPPORT */
#ifdef ISP_MW_SW_AWB_ALGO_SUPPORT
    &ISP_Algo_AWB,
#endif /* ISP_MW_SW_AWB_ALGO_SUPPORT */
};

/* Global variables ----------------------------------------------------------*/
ISP_MetaTypeDef Meta = {0};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  ISP_Algo_BadPixel_Init
  *         Initialize the BadPixel algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_BadPixel_Init(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */

  ((ISP_AlgoTypeDef *)pAlgo)->state = ISP_ALGO_STATE_INIT;

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_BadPixel_DeInit
  *         Deinitialize the BadPixel algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_BadPixel_DeInit(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */
  (void)pAlgo; /* unused */

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_BadPixel_Process
  *         Process the BadPixel algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_BadPixel_Process(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */
  (void)pAlgo; /* unused */
  static uint32_t BadPixelCount, LastFrameId;
  static int8_t Step;
  uint32_t CurrentFrameId;
  ISP_BadPixelTypeDef BadPixelConfig;
  ISP_IQParamTypeDef *IQParamConfig;
  ISP_StatusTypeDef ret;

  IQParamConfig = ISP_SVC_IQParam_Get(hIsp);

  if (IQParamConfig->badPixelAlgo.enable == false)
  {
    return ISP_OK;
  }

  /* Wait for a new frame */
  CurrentFrameId = ISP_SVC_Misc_GetMainFrameId(hIsp);
  if (CurrentFrameId == LastFrameId)
  {
    return ISP_OK;
  }
  LastFrameId = CurrentFrameId;

  if (Step++ >= 0)
  {
    /* Measure the number of bad pixels */
    ret = ISP_SVC_ISP_GetBadPixel(hIsp, &BadPixelConfig);
    if (ret != ISP_OK)
    {
      return ret;
    }
    BadPixelCount += BadPixelConfig.count;
  }

  if (Step == 10)
  {
    /* All measures done : make an average and compare with threshold */
    BadPixelCount /= 10;

    if ((BadPixelCount > IQParamConfig->badPixelAlgo.threshold) && (BadPixelConfig.strength > 0))
    {
      /* Bad pixel is above target : decrease strength */
      BadPixelConfig.strength--;
    }
    else if ((BadPixelCount < IQParamConfig->badPixelAlgo.threshold) && (BadPixelConfig.strength < ISP_BADPIXEL_STRENGTH_MAX - 1))
    {
      /* Bad pixel is below target : increase strength. (exclude ISP_BADPIXEL_STRENGTH_MAX which gives weird results) */
      BadPixelConfig.strength++;
    }

    /* Set updated Strength */
    BadPixelConfig.enable = 1;
    ret = ISP_SVC_ISP_SetBadPixel(hIsp, &BadPixelConfig);
    if (ret != ISP_OK)
    {
      return ret;
    }

    /* Set Step to -1 to wait for an extra frame before a new measurement (the ISP HW needs one frame to update after reconfig) */
    Step = -1;
    BadPixelCount = 0;
  }

  return ISP_OK;
}

#ifdef ISP_MW_SW_AEC_ALGO_SUPPORT
/**
  * @brief  ISP_Algo_AEC_Init
  *         Initialize the AEC algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AEC_Init(void *hIsp, void *pAlgo)
{
  ISP_HandleTypeDef *pIsp_handle = (ISP_HandleTypeDef*) hIsp;
  ISP_AlgoTypeDef *algo = (ISP_AlgoTypeDef *)pAlgo;
  ISP_SensorExposureTypeDef exposureConfig;
  ISP_SensorGainTypeDef gainConfig;
  ISP_IQParamTypeDef *IQParamConfig;
  ISP_RestartStateTypeDef *pRestartState;

  IQParamConfig = ISP_SVC_IQParam_Get(hIsp);
  pRestartState = ISP_SVC_GetRestartState(hIsp);

  if (IQParamConfig->sensorDelay.delay == 0)
  {
    /* A value of 0 is invalid, it would break the AEC algo */
    IQParamConfig->sensorDelay.delay = 1;
  }

  isp_ae_init(pIsp_handle);

  /* Initialize exposure and gain values */
  if (IQParamConfig->AECAlgo.enable == true)
  {
    if (pRestartState && pRestartState->sensorConfigured)
    {
      /* Resume from the latest applied config */
      exposureConfig.exposure = pRestartState->sensorExposure;
      gainConfig.gain = pRestartState->sensorGain;
    }
    else
    {
      /* Start from black frame */
      exposureConfig.exposure = pIsp_handle->sensorInfo.exposure_min;
      gainConfig.gain = pIsp_handle->sensorInfo.gain_min;
    }

    if ((ISP_SVC_Sensor_SetExposure(hIsp, &exposureConfig) != ISP_OK) || (ISP_SVC_Sensor_SetGain(hIsp, &gainConfig) != ISP_OK))
    {
      return ISP_ERR_ALGO;
    }
  }

  /* Update State */
  algo->state = ISP_ALGO_STATE_INIT;

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_AEC_DeInit
  *         Deinitialize the AEC algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AEC_DeInit(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */
  (void)pAlgo; /* unused */

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_AEC_StatCb
  *         Callback informing that statistics are available
  * @param  pAlgo: ISP algorithm handle.
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AEC_StatCb(ISP_AlgoTypeDef *pAlgo)
{
  /* Update State */
  pAlgo->state = ISP_ALGO_STATE_STAT_READY;

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_AEC_Process
  *         Process the AEC algorithm. This basic algorithm controls the sensor exposure
  *         in order to reach an average luminance of exposureTarget.
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AEC_Process(void *hIsp, void *pAlgo)
{
  static ISP_SVC_StatStateTypeDef stats;
  static ISP_SVC_StatLocation statLocation = ISP_STAT_LOC_DOWN;
  ISP_AlgoTypeDef *algo = (ISP_AlgoTypeDef *)pAlgo;
  ISP_IQParamTypeDef *IQParamConfig;
  ISP_RestartStateTypeDef *pRestartState;
  ISP_StatusTypeDef ret = ISP_OK;
  ISP_SensorGainTypeDef gainConfig;
  ISP_SensorExposureTypeDef exposureConfig;
  uint32_t avgL, newExposure, newGain;
#ifdef ALGO_AEC_DBG_LOGS
  static uint32_t currentL;
#endif
  int32_t estimated_lux;
  ISP_HandleTypeDef *pIsp_handle = (ISP_HandleTypeDef *)hIsp;

  IQParamConfig = ISP_SVC_IQParam_Get(hIsp);
  if (IQParamConfig->AECAlgo.enable == false)
  {
    return ISP_OK;
  }

  switch(algo->state)
  {
  case ISP_ALGO_STATE_INIT:
    /* Update Sensor Info in case calculated exposure limits are changed*/
    ret = ISP_SVC_Sensor_GetInfo(hIsp, &pIsp_handle->sensorInfo);
    if (ret != ISP_OK)
    {
      return ret;
    }

    if (pIsp_handle->appliHelpers.GetExternalStatistics != NULL)
    {
      statLocation = ISP_STAT_LOC_EXT;
    }

  case ISP_ALGO_STATE_NEED_STAT:
    /* Ask for stats */
    ret = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_AEC_StatCb, pAlgo, &stats, statLocation,
                                ISP_STAT_TYPE_AVG, IQParamConfig->sensorDelay.delay);
    if (ret != ISP_OK)
    {
      return ret;
    }

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  case ISP_ALGO_STATE_WAITING_STAT:
    /* Do nothing */
    break;

  case ISP_ALGO_STATE_STAT_READY:
#ifdef ALGO_PERF_DBG_LOGS
    static float sum_calc, sum_process = 0;
    static uint32_t iter = 0;
    uint32_t end_algo_calc, end_algo_process = 20;
    uint32_t start_algo = DWT->CYCCNT;
#endif
    /* Use weighted averageL from external stats if available and callback is defined */
    if (pIsp_handle->appliHelpers.GetExternalStatistics != NULL &&
        stats.extStats.nbAreas > 0 && stats.extStats.stats != NULL)
    {
      avgL = ISP_SVC_Stats_WeightedAverageL(&stats.extStats);
    }
    else
    {
      avgL = stats.down.averageL;
    }
#ifdef ALGO_AEC_DBG_LOGS
    if (avgL != currentL)
    {
      printf("L = %"PRIu32"\r\n", avgL);
      currentL = avgL;
    }
#endif
    /* Read the current sensor gain */
    ret = ISP_SVC_Sensor_GetGain(hIsp, &gainConfig);
    if (ret != ISP_OK)
    {
      return ret;
    }

    ret = ISP_SVC_Sensor_GetExposure(hIsp, &exposureConfig);
    if (ret != ISP_OK)
    {
      return ret;
    }

    estimated_lux = ISP_SVC_Misc_GetEstimatedLux(hIsp, (uint8_t)avgL);

#ifdef ALGO_AEC_DBG_LOGS
    printf("Lux = %"PRIu32", L = %"PRIu32", E = %"PRIu32", G = %"PRIu32"\r\n", estimated_lux, avgL, exposureConfig.exposure, gainConfig.gain);
#endif

    if (estimated_lux >= 0)
    {
      isp_ae_get_new_exposure((uint32_t)estimated_lux, avgL, &newExposure, &newGain, exposureConfig.exposure, gainConfig.gain);
#ifdef ALGO_PERF_DBG_LOGS
      end_algo_calc = DWT->CYCCNT;
#endif
      if (gainConfig.gain != newGain)
      {
        /* Set new gain */
        gainConfig.gain = newGain;

        ret = ISP_SVC_Sensor_SetGain(hIsp, &gainConfig);
        if (ret != ISP_OK)
        {
          return ret;
        }

#ifdef ALGO_AEC_DBG_LOGS
        printf("New gain = %"PRIu32"\r\n", gainConfig.gain);
#endif
      }

      if (exposureConfig.exposure != newExposure)
      {
        /* Set new exposure */
        exposureConfig.exposure = newExposure;

        ret = ISP_SVC_Sensor_SetExposure(hIsp, &exposureConfig);
        if (ret != ISP_OK)
        {
          return ret;
        }

#ifdef ALGO_AEC_DBG_LOGS
        printf("New exposure = %"PRIu32"\r\n", exposureConfig.exposure);
#endif
      }

      /* Update the restart state config */
      pRestartState = ISP_SVC_GetRestartState(hIsp);
      if (pRestartState)
      {
        pRestartState->sensorGain = newGain;
        pRestartState->sensorExposure = newExposure;
        pRestartState->sensorConfigured = 1;
      }
    }
    else
    {
      ret = ISP_ERR_ALGO;
      printf("ERROR: Lux value of the scene cannot be estimated\r\n");
    }

#ifdef ALGO_PERF_DBG_LOGS
    end_algo_process = DWT->CYCCNT;
    sum_calc += (float)(end_algo_calc - start_algo) / (SystemCoreClock / 1e6);
    sum_process += (float)(end_algo_process - start_algo) / (SystemCoreClock / 1e6);
    iter++;

    if (iter == MEAS_ITERATION)
    {
      printf("AEC time  = %.3f us (calc in %.3f us)\r\n", sum_process / MEAS_ITERATION, sum_calc / MEAS_ITERATION);
      sum_process = 0;
      sum_calc = 0;
      iter = 0;
    }
#endif

    /* Ask for stats */
    ret = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_AEC_StatCb, pAlgo, &stats, statLocation,
                                ISP_STAT_TYPE_AVG, IQParamConfig->sensorDelay.delay);

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  default:
    printf("WARNING: Unknown AE algo state\r\n");
    /* Reset state to ISP_ALGO_STATE_INIT */
    algo->state = ISP_ALGO_STATE_INIT;
    break;

  }

  return ret;
}
#endif /* ISP_MW_SW_AEC_ALGO_SUPPORT */

#ifdef ISP_MW_SW_AWB_ALGO_SUPPORT
/**
  * @brief  ISP_Algo_ApplyGammaInverse
  *         Apply Gamma 1/2.2 correction to a component value
  * @param  hIsp:  ISP device handle.
  * @param  comp: component value
  * @retval gamma corrected value
  */
double ISP_Algo_ApplyGammaInverse(ISP_HandleTypeDef *hIsp, uint32_t comp)
{
  double out;

  /* Check if gamma is enabled */
  if (ISP_SVC_Misc_IsGammaEnabled(hIsp, 1 /*main pipe*/)) {
    out = 255 * pow((float)comp / 255, 1.0 / 2.2);
  }
  else
  {
    out = (double) comp;
  }
  return out;
}

/**
  * @brief  ISP_Algo_ApplyCConv
  *         Apply Color Conversion matrix to RGB components, clamping output values to [0-255]
  * @param  hIsp:  ISP device handle.
  * @param  inR: Red component value
  * @param  inG: Green component value
  * @param  inB: Blue component value
  * @param  outR: pointer to Red component value after color conversion
  * @param  outG: pointer to Green component value after color conversion
  * @param  outB: pointer to Blue component value after color conversion
  * @retval None
  */
void ISP_Algo_ApplyCConv(ISP_HandleTypeDef *hIsp, uint32_t inR, uint32_t inG, uint32_t inB, uint32_t *outR, uint32_t *outG, uint32_t *outB)
{
  ISP_ColorConvTypeDef colorConv;
  int64_t ccR, ccG, ccB;

  if ((ISP_SVC_ISP_GetColorConv(hIsp, &colorConv) == ISP_OK) && (colorConv.enable == 1))
  {
    /* Apply ColorConversion matrix to the input components */
    ccR = (int64_t) inR * colorConv.coeff[0][0] + (int64_t) inG * colorConv.coeff[0][1] + (int64_t) inB * colorConv.coeff[0][2];
    ccG = (int64_t) inR * colorConv.coeff[1][0] + (int64_t) inG * colorConv.coeff[1][1] + (int64_t) inB * colorConv.coeff[1][2];
    ccB = (int64_t) inR * colorConv.coeff[2][0] + (int64_t) inG * colorConv.coeff[2][1] + (int64_t) inB * colorConv.coeff[2][2];

    ccR /= ISP_CCM_PRECISION_FACTOR;
    ccG /= ISP_CCM_PRECISION_FACTOR;
    ccB /= ISP_CCM_PRECISION_FACTOR;

    /* Clamp values to 0-255 */
    ccR = (ccR < 0) ? 0 : (ccR > 255) ? 255 : ccR;
    ccG = (ccG < 0) ? 0 : (ccG > 255) ? 255 : ccG;
    ccB = (ccB < 0) ? 0 : (ccB > 255) ? 255 : ccB;

    *outR = (uint32_t) ccR;
    *outG = (uint32_t) ccG;
    *outB = (uint32_t) ccB;
  }
  else
  {
    *outR = inR;
    *outG = inG;
    *outB = inB;
  }
}

/**
  * @brief  ISP_Algo_AWB_Init
  *         Initialize the AWB algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AWB_Init(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */
  ISP_AlgoTypeDef *algo = (ISP_AlgoTypeDef *)pAlgo;
  ISP_RestartStateTypeDef *pRestartState;

  pRestartState = ISP_SVC_GetRestartState(hIsp);
  if (pRestartState && pRestartState->awbConfigured)
  {
    /* Resume from the latest applied config */
    ISP_SVC_ISP_SetColorConv(hIsp, &pRestartState->colorConv);
    ISP_SVC_ISP_SetGain(hIsp, &pRestartState->ISPGain);
  }

  /* Continue the initialization in ISP_Algo_AWB_Process() function when state is ISP_ALGO_STATE_INIT.
   * This allows to read the IQ params after an algo stop/start cycle */
  algo->state = ISP_ALGO_STATE_INIT;

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_AWB_DeInit
  *         Deinitialize the AWB algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AWB_DeInit(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */
  (void)pAlgo; /* unused */

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_AWB_StatCb
  *         Callback informing that statistics are available
  * @param  pAlgo: ISP algorithm handle.
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AWB_StatCb(ISP_AlgoTypeDef *pAlgo)
{
  /* Update State */
  if (pAlgo->state != ISP_ALGO_STATE_INIT)
  {
    pAlgo->state = ISP_ALGO_STATE_STAT_READY;
  }

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_AWB_Process
  *         Process the AWB algorithm. This algorithm controls the ISP gain and color conversion
  *         in order to output realistic colors (white balance).
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_AWB_Process(void *hIsp, void *pAlgo)
{
  static ISP_SVC_StatStateTypeDef stats;
  static uint8_t enableCurrent = false;
  static uint8_t reconfigureRequest = false;
  static uint32_t currentColorTemp = 0;
  ISP_IQParamTypeDef *IQParamConfig;
  ISP_RestartStateTypeDef *pRestartState;
  ISP_ColorConvTypeDef ColorConvConfig;
  ISP_ISPGainTypeDef ISPGainConfig;
  ISP_AlgoTypeDef *algo = (ISP_AlgoTypeDef *)pAlgo;
  ISP_StatusTypeDef ret_stat, ret = ISP_OK;
  uint32_t estimatedColorTemp = 0;

  IQParamConfig = ISP_SVC_IQParam_Get(hIsp);

  if (IQParamConfig->AWBAlgo.enable == false)
  {
    enableCurrent = false;
    return ISP_OK;
  }
  else if ((enableCurrent == false) || (IQParamConfig->AWBAlgo.enable == ISP_AWB_ENABLE_RECONFIGURE))
  {
    /* Start or resume algo : set state to INIT in order to read the IQ params */
    algo->state = ISP_ALGO_STATE_INIT;
    IQParamConfig->AWBAlgo.enable = true;
    reconfigureRequest = true;
    enableCurrent = true;
  }

  switch(algo->state)
  {
  case ISP_ALGO_STATE_INIT:
    ret = ISP_AWB_Init(&IQParamConfig->AWBAlgo);
    if (ret != ISP_OK)
    {
      return ret;
    }

    /* Ask for stats */
    ret = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_AWB_StatCb, pAlgo, &stats, ISP_STAT_LOC_DOWN,
                                ISP_STAT_TYPE_AVG, ALGO_ISP_LATENCY + ALGO_AWB_ADDITIONAL_LATENCY);
    if (ret != ISP_OK)
    {
      return ret;
    }

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  case ISP_ALGO_STATE_NEED_STAT:
    ret = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_AWB_StatCb, pAlgo, &stats, ISP_STAT_LOC_DOWN,
                                ISP_STAT_TYPE_AVG, ALGO_ISP_LATENCY + ALGO_AWB_ADDITIONAL_LATENCY);
    if (ret != ISP_OK)
    {
      return ret;
    }

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  case ISP_ALGO_STATE_WAITING_STAT:
    /* Do nothing */
    break;

  case ISP_ALGO_STATE_STAT_READY:
#ifdef ALGO_PERF_DBG_LOGS
    static float sum_calc, sum_process = 0;
    static uint32_t iter = 0;
    uint32_t end_algo_calc, end_algo_process = 20;
    uint32_t start_algo = DWT->CYCCNT;
#endif
    /* Optimization: do not ask for Up stats, but evaluate them from the down stats */
    ISP_SVC_Stats_EvaluateUp(hIsp, &stats.down, &stats.up);

    ret = ISP_AWB_GetConfig(&stats.up, &ColorConvConfig, &ISPGainConfig, &estimatedColorTemp);
#ifdef ALGO_PERF_DBG_LOGS
      end_algo_calc = DWT->CYCCNT;
#endif
    if (ret == ISP_OK)
    {
      if (estimatedColorTemp != currentColorTemp || reconfigureRequest == true)
      {
        /* Apply Color Conversion */
        ret = ISP_SVC_ISP_SetColorConv(hIsp, &ColorConvConfig);
        if (ret == ISP_OK)
        {
          /* Apply gain */
          ret = ISP_SVC_ISP_SetGain(hIsp, &ISPGainConfig);
          if (ret == ISP_OK)
          {
            Meta.colorTemp = estimatedColorTemp;
            currentColorTemp = estimatedColorTemp ;

            /* Update the restart state */
            pRestartState = ISP_SVC_GetRestartState(hIsp);
            if (pRestartState)
            {
              pRestartState->awbConfigured = 1;
              pRestartState->colorConv = ColorConvConfig;
              pRestartState->ISPGain = ISPGainConfig;
            }
          }
        }
      }
    }
    if (ret != ISP_OK)
    {
      ret = ISP_ERR_ALGO;
    }

    /* Reset reconfigureRequest */
    reconfigureRequest = false;

#ifdef ALGO_PERF_DBG_LOGS
    end_algo_process = DWT->CYCCNT;
    sum_calc += (float)(end_algo_calc - start_algo) / (SystemCoreClock / 1e6);
    sum_process += (float)(end_algo_process - start_algo) / (SystemCoreClock / 1e6);
    iter++;

    if (iter == MEAS_ITERATION)
    {
      printf("AWB time  = %.3f us (calc in %.3f us)\r\n", sum_process / MEAS_ITERATION, sum_calc / MEAS_ITERATION);
      sum_process = 0;
      sum_calc = 0;
      iter = 0;
    }
#endif

    /* Ask for stats */
    ret_stat = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_AWB_StatCb, pAlgo, &stats, ISP_STAT_LOC_DOWN,
                                     ISP_STAT_TYPE_AVG, ALGO_ISP_LATENCY + ALGO_AWB_ADDITIONAL_LATENCY);
    ret = (ret != ISP_OK) ? ret : ret_stat;

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  default:
    printf("WARNING: Unknown AWB algo state\r\n");
    /* Reset state to ISP_ALGO_STATE_INIT */
    algo->state = ISP_ALGO_STATE_INIT;
    break;
  }

  return ret;
}
#endif /* ISP_MW_SW_AWB_ALGO_SUPPORT */



/**
  * @brief  ISP_Algo_Background_StatCb
  *         Callback informing that statistics are available
  * @param  pAlgo: ISP algorithm handle.
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_Background_StatCb(ISP_AlgoTypeDef *pAlgo)
{
  /* Update State */
  pAlgo->state = ISP_ALGO_STATE_STAT_READY;

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_Background_Init
  *         Initialize the Background algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_Background_Init(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */

  ((ISP_AlgoTypeDef *)pAlgo)->state = ISP_ALGO_STATE_INIT;

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_Background_DeInit
  *         Deinitialize the Background algorithm
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_Background_DeInit(void *hIsp, void *pAlgo)
{
  (void)hIsp; /* unused */
  (void)pAlgo; /* unused */

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_Background_Process
  *         Process called in case no algorithm is enabled to collect average statistics
  *         for regular update
  * @param  hIsp:  ISP device handle. To cast in (ISP_HandleTypeDef *).
  * @param  pAlgo: ISP algorithm handle. To cast in (ISP_AlgoTypeDef *).
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_Background_Process(void *hIsp, void *pAlgo)
{
  static ISP_SVC_StatStateTypeDef stats;
  static ISP_SVC_StatLocation statLocation = ISP_STAT_LOC_DOWN;
  ISP_HandleTypeDef *pIsp_handle = (ISP_HandleTypeDef *)hIsp;
  ISP_AlgoTypeDef *algo = (ISP_AlgoTypeDef *)pAlgo;
  ISP_IQParamTypeDef *IQParamConfig;
  ISP_StatusTypeDef ret = ISP_OK;

  IQParamConfig = ISP_SVC_IQParam_Get(hIsp);

#ifdef ISP_MW_SW_AEC_ALGO_SUPPORT
  if (IQParamConfig->AECAlgo.enable == true)
  {
    /* No need to collect other statistics for regular update*/
    return ISP_OK;
  }
#endif /* ISP_MW_SW_AEC_ALGO_SUPPORT */
#ifdef ISP_MW_SW_AWB_ALGO_SUPPORT
  if (IQParamConfig->AWBAlgo.enable == true)
  {
    /* No need to collect other statistics for regular update*/
    return ISP_OK;
  }
#endif /* ISP_MW_SW_AWB_ALGO_SUPPORT */

  switch(algo->state)
  {
  case ISP_ALGO_STATE_INIT:

    if (pIsp_handle->appliHelpers.GetExternalStatistics != NULL)
    {
      statLocation = ISP_STAT_LOC_EXT;
    }
  case ISP_ALGO_STATE_NEED_STAT:
    /* Ask for stats */
    ret = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_Background_StatCb, pAlgo, &stats, statLocation,
                                ISP_STAT_TYPE_AVG, IQParamConfig->sensorDelay.delay);
    if (ret != ISP_OK)
    {
      return ret;
    }

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  case ISP_ALGO_STATE_WAITING_STAT:
    /* Do nothing */
    break;

  case ISP_ALGO_STATE_STAT_READY:
    /* Ask for stats */
    ret = ISP_SVC_Stats_GetNext(hIsp, &ISP_Algo_Background_StatCb, pAlgo, &stats, statLocation,
                                ISP_STAT_TYPE_AVG, IQParamConfig->sensorDelay.delay);

    /* Wait for stats to be ready */
    algo->state = ISP_ALGO_STATE_WAITING_STAT;
    break;

  default:
    printf("WARNING: Unknown background algo state\r\n");
    /* Reset state to ISP_ALGO_STATE_INIT */
    algo->state = ISP_ALGO_STATE_INIT;
    break;

  }

  return ret;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  ISP_Algo_Init
  *         Register and initialize all the algorithms
  * @param  hIsp: ISP device handle
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_Init(ISP_HandleTypeDef *hIsp)
{
  ISP_AlgoTypeDef *algo;
  ISP_StatusTypeDef ret;
  uint8_t i;

  hIsp->algorithm = ISP_Algo_List;

  for (i = 0; i < sizeof(ISP_Algo_List) / sizeof(*ISP_Algo_List); i++)
  {
    algo = hIsp->algorithm[i];
    if ((algo != NULL) && (algo->Init != NULL))
    {
      ret = algo->Init((void*)hIsp, (void*)algo);
      if (ret != ISP_OK)
      {
        return ret;
      }
    }
  }

#ifdef ALGO_PERF_DBG_LOGS
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_DeInit
  *         Deinitialize all the algorithms
  * @param  hIsp: ISP device handle
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_DeInit(ISP_HandleTypeDef *hIsp)
{
  ISP_AlgoTypeDef *algo;
  ISP_StatusTypeDef ret;
  uint8_t i;

  for (i = 0; i < sizeof(ISP_Algo_List) / sizeof(*ISP_Algo_List); i++)
  {
    algo = hIsp->algorithm[i];
    if ((algo != NULL) && (algo->DeInit != NULL))
    {
      ret = algo->DeInit((void*)hIsp, (void*)algo);
      if (ret != ISP_OK)
      {
        return ret;
      }
    }
  }

  return ISP_OK;
}

/**
  * @brief  ISP_Algo_Process
  *         Process all the algorithms
  * @param  hIsp: ISP device handle
  * @retval operation result
  */
ISP_StatusTypeDef ISP_Algo_Process(ISP_HandleTypeDef *hIsp)
{
  ISP_AlgoTypeDef *algo;
  ISP_StatusTypeDef ret;
  uint8_t i;

  for (i = 0; i < sizeof(ISP_Algo_List) / sizeof(*ISP_Algo_List); i++)
  {
    algo = hIsp->algorithm[i];
    if ((algo != NULL) && (algo->Process != NULL))
    {
      ret = algo->Process((void*)hIsp, (void*)algo);
      if (ret != ISP_OK)
      {
        return ret;
      }
    }
  }

  return ISP_OK;
}
