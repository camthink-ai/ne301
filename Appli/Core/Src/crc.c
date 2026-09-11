/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    crc.c
  * @brief   This file provides code for the configuration
  *          of the CRC instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "crc.h"
#include "cmsis_os2.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

CRC_HandleTypeDef hcrc;

osMutexId_t hcrc_mutex_id = NULL;

/* CRC init function */
void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_DISABLE;
  hcrc.Init.GeneratingPolynomial = 0x04C11DB7;
  hcrc.Init.CRCLength = CRC_POLYLENGTH_32B;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_DISABLE;
  hcrc.Init.InitValue = 0xFFFFFFFF;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;

  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */
  hcrc_mutex_id = osMutexNew(NULL);
  if (hcrc_mutex_id == NULL) {
    Error_Handler();
  }
  /* USER CODE END CRC_Init 2 */

}

void HAL_CRC_MspInit(CRC_HandleTypeDef* crcHandle)
{

  if(crcHandle->Instance==CRC)
  {
  /* USER CODE BEGIN CRC_MspInit 0 */

  /* USER CODE END CRC_MspInit 0 */
    /* CRC clock enable */
    __HAL_RCC_CRC_CLK_ENABLE();
  /* USER CODE BEGIN CRC_MspInit 1 */

  /* USER CODE END CRC_MspInit 1 */
  }
}

void HAL_CRC_MspDeInit(CRC_HandleTypeDef* crcHandle)
{

  if(crcHandle->Instance==CRC)
  {
  /* USER CODE BEGIN CRC_MspDeInit 0 */

  /* USER CODE END CRC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CRC_CLK_DISABLE();
  /* USER CODE BEGIN CRC_MspDeInit 1 */

  /* USER CODE END CRC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
uint32_t CRC_Calculate(void *pBuffer, uint32_t BufferLength)
{
  uint32_t crcResult = 0U;

  osMutexAcquire(hcrc_mutex_id, osWaitForever);
  crcResult = HAL_CRC_Calculate(&hcrc, (uint32_t *)pBuffer, BufferLength);
  osMutexRelease(hcrc_mutex_id);

  return crcResult;
}

uint32_t CRC_Accumulate(uint32_t crcValue, void *pBuffer, uint32_t BufferLength)
{
  uint32_t crcResult = 0U;

  osMutexAcquire(hcrc_mutex_id, osWaitForever);
  hcrc.Instance->INIT = crcValue;
  // Use the HAL macro (CR |= CRC_CR_RESET) so only the RESET bit is set.
  // A direct "CRC->CR = CRC_CR_RESET" write clobbers POLYSIZE / REV_IN /
  // REV_OUT / RTYPE config bits programmed during MX_CRC_Init(), corrupting
  // every subsequent CRC computation.
  __HAL_CRC_DR_RESET(&hcrc);
  crcResult = HAL_CRC_Accumulate(&hcrc, (uint32_t *)pBuffer, BufferLength);
  osMutexRelease(hcrc_mutex_id);

  return crcResult;
}
/* USER CODE END 1 */
