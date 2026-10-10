/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    hpdma.c
  * @brief   This file provides code for the configuration
  *          of the HPDMA instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "hpdma.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* HPDMA1 init function */
void MX_HPDMA1_Init(void)
{

    /* Peripheral clock enable */
    __HAL_RCC_HPDMA1_CLK_ENABLE();

    /* HPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(HPDMA1_Channel0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel0_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel1_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel1_IRQn);

    HAL_NVIC_SetPriority(HPDMA1_Channel2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel2_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel3_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel3_IRQn);
	  HAL_NVIC_SetPriority(HPDMA1_Channel4_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel4_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel5_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel5_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel6_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel6_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel7_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel7_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel8_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel8_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel9_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel9_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel10_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel10_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel11_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel11_IRQn);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
