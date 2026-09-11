/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.h
  * @brief   This file contains all the function prototypes for
  *          the spi.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SPI_H__
#define __SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#define SPI4_NSS_IS_USE_SOFT_CTRL               (0)
#if SPI4_NSS_IS_USE_SOFT_CTRL
#define SPI4_NSS_HIGH()                         HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_SET);
#define SPI4_NSS_LOW()                          HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_RESET);
#else
#define SPI4_NSS_HIGH()                         
#define SPI4_NSS_LOW()                          
#endif
/* USER CODE END Includes */

extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi4;
extern SPI_HandleTypeDef hspi6;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_SPI2_Init(void);
void MX_SPI4_Init(void);
void MX_SPI6_Init(void);
void MX_SPI6_DeInit(void);

/* USER CODE BEGIN Prototypes */
uint8_t SPI2_ReadWriteByte(uint8_t wByte);
int SPI2_WriteBytes(uint8_t *wBytes, uint16_t wLength, uint32_t timeout);
int SPI2_ReadBytes(uint8_t *rBytes, uint16_t rLength, uint32_t timeout);
int SPI6_WriteBytes(const uint8_t *data, uint32_t length, uint32_t timeout);
int SPI6_WriteBytesDMA(const uint8_t *data, uint32_t length, uint32_t timeout_ms);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __SPI_H__ */

