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
     PC14-OSC32_IN   ------> RCC_OSC32_IN
     PC15-OSC32_OUT   ------> RCC_OSC32_OUT
     PA13 (SWDIO)   ------> DEBUG_JTMS-SWDIO
     PA14 (SWCLK)   ------> DEBUG_JTCK-SWCLK
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN 3 */
  HAL_PWREx_DisablePullUpPullDownConfig();
  /* USER CODE END 3 */

  /*Configure GPIO pin Output Level */
  // HAL_GPIO_WritePin(GPIOA, PWR_WIFI_Pin|PWR_3V3_Pin|PWR_AON_Pin|PWR_N6_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  // HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PF2 PF3 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pin : CONFIG_KEY_Pin */
  GPIO_InitStruct.Pin = CONFIG_KEY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CONFIG_KEY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PIR_TRIGGER_Pin */
  GPIO_InitStruct.Pin = PIR_TRIGGER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PIR_TRIGGER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : WIFI_SPI_IRQ_Pin */
  GPIO_InitStruct.Pin = WIFI_SPI_IRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_IN_Pin */
  GPIO_InitStruct.Pin = USB_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PWR_WIFI_Pin PWR_3V3_Pin PWR_AON_Pin PWR_N6_Pin*/
  GPIO_InitStruct.Pin = PWR_WIFI_Pin|PWR_3V3_Pin|PWR_AON_Pin|PWR_N6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PIR_SERIAL_Pin */
  GPIO_InitStruct.Pin = PIR_SERIAL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(PIR_SERIAL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PWR_EXT_Pin */
  // GPIO_InitStruct.Pin = PWR_EXT_Pin;
  // GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  // GPIO_InitStruct.Pull = GPIO_NOPULL;
  // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  // HAL_GPIO_Init(PWR_EXT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  // HAL_NVIC_SetPriority(EXTI0_1_IRQn, 0, 0);
  // HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);

  // HAL_NVIC_SetPriority(EXTI2_3_IRQn, 0, 0);
  // HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);

  // HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  // HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

}

/* USER CODE BEGIN 2 */
void GPIO_All_Config_Analog(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Enable all GPIO clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    
    // Unified configuration parameters
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    
    // Batch configure all ports
    GPIO_InitStruct.Pin = GPIO_PIN_ALL ^ (PWR_WIFI_Pin | PWR_3V3_Pin | PWR_AON_Pin | PWR_N6_Pin | PIR_TRIGGER_Pin);// | GPIO_PIN_13 | GPIO_PIN_14 | U1_TX_Pin | U1_RX_Pin);
    HAL_GPIO_DeInit(GPIOA, GPIO_InitStruct.Pin);
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_ALL ^ PIR_SERIAL_Pin;
    HAL_GPIO_DeInit(GPIOB, GPIO_InitStruct.Pin);
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_ALL;
    HAL_GPIO_DeInit(GPIOF, GPIO_InitStruct.Pin);
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
}
/* USER CODE END 2 */
