/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, PWR_PIR_ON_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, WIFI_RESET_N_Pin|PIR_INT_OUT_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOD, WIFI_ULP_WAKEUP_Pin, GPIO_PIN_SET);
  
  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, WIFI_POC_IN_Pin|PWR_WIFI_ON_Pin|PWR_USB_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, PIR_Serial_IN_Pin|PWR_SENSOR_ON_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, LED_Pin|PWR_COEDC_Pin|PWR_USB_3V3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, PWR_BAT_DET_ON_Pin|PWR_TF_ON_Pin, GPIO_PIN_RESET);

  /*Configure IR-CUT GPIO output level: High (day) */
  HAL_GPIO_WritePin(IR_CUT_A_GPIO_Port, IR_CUT_A_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : PWR_PIR_ON_Pin */
  GPIO_InitStruct.Pin = PWR_PIR_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : WIFI_RESET_N_Pin */
  GPIO_InitStruct.Pin = WIFI_RESET_N_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(WIFI_RESET_N_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : WIFI_ULP_WAKEUP_Pin PIR_INT_OUT_Pin */
  GPIO_InitStruct.Pin = WIFI_ULP_WAKEUP_Pin|PIR_INT_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : WIFI_IRQ_Pin */
  GPIO_InitStruct.Pin = WIFI_IRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(WIFI_IRQ_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : WIFI_POC_IN_Pin */
  GPIO_InitStruct.Pin = WIFI_POC_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(WIFI_POC_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PWR_WIFI_ON_Pin */
  GPIO_InitStruct.Pin = PWR_WIFI_ON_Pin|PWR_USB_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : WIFI_STA_Pin */
  GPIO_InitStruct.Pin = WIFI_STA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(WIFI_STA_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TF_INT_Pin */
  GPIO_InitStruct.Pin = TF_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(TF_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PIR_Serial_IN_Pin PWR_SENSOR_ON_Pin */
  GPIO_InitStruct.Pin = PIR_Serial_IN_Pin|PWR_SENSOR_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_Pin PWR_COEDC_Pin */
  GPIO_InitStruct.Pin = LED_Pin|LED2_Pin|PWR_COEDC_Pin|PWR_USB_3V3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pins : PWR_BAT_DET_ON_Pin PWR_TF_ON_Pin */
  GPIO_InitStruct.Pin = PWR_BAT_DET_ON_Pin|PWR_TF_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2);

  /*Configure IR-CUT GPIO pin(s) */
  GPIO_InitStruct.Pin = IR_CUT_A_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(IR_CUT_A_GPIO_Port, &GPIO_InitStruct);

#if defined(STM32N6_DK_BOARD)
  HAL_GPIO_WritePin(PWR_WIFI_ON_GPIO_Port, PWR_WIFI_ON_Pin, GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(PWR_USB_3V3_GPIO_Port, PWR_USB_3V3_Pin, GPIO_PIN_SET);
#endif

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
