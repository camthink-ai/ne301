/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CONFIG_KEY_Pin GPIO_PIN_0
#define CONFIG_KEY_GPIO_Port GPIOA
#define CONFIG_KEY_EXTI_IRQn EXTI0_1_IRQn
#define PIR_TRIGGER_Pin GPIO_PIN_2
#define PIR_TRIGGER_GPIO_Port GPIOA
#define PIR_TRIGGER_EXTI_IRQn EXTI2_3_IRQn
// #define NET_WKUP_Pin GPIO_PIN_2
// #define NET_WKUP_GPIO_Port GPIOA
// #define NET_WKUP_EXTI_IRQn EXTI2_3_IRQn
#define PIR_SERIAL_Pin GPIO_PIN_0
#define PIR_SERIAL_GPIO_Port GPIOB
#define PWR_WIFI_Pin GPIO_PIN_4
#define PWR_WIFI_GPIO_Port GPIOA
#define PWR_3V3_Pin GPIO_PIN_5
#define PWR_3V3_GPIO_Port GPIOA
#define PWR_AON_Pin GPIO_PIN_6
#define PWR_AON_GPIO_Port GPIOA
#define PWR_N6_Pin GPIO_PIN_7
#define PWR_N6_GPIO_Port GPIOA
// #define PWR_EXT_Pin GPIO_PIN_0
// #define PWR_EXT_GPIO_Port GPIOB
#define USB_IN_Pin GPIO_PIN_8
#define USB_IN_GPIO_Port GPIOA
#define U1_TX_Pin GPIO_PIN_9
#define U1_TX_GPIO_Port GPIOA
#define U1_RX_Pin GPIO_PIN_10
#define U1_RX_GPIO_Port GPIOA
#define WIFI_SPI_IRQ_Pin GPIO_PIN_11
#define WIFI_SPI_IRQ_GPIO_Port GPIOA
#define WIFI_SPI_IRQ_EXTI_IRQn EXTI4_15_IRQn
#define WIFI_SLEEP_STA_Pin GPIO_PIN_12
#define WIFI_SLEEP_STA_GPIO_Port GPIOA
#define SPI3_CS_Pin GPIO_PIN_15
#define SPI3_CS_GPIO_Port GPIOA
#define SPI3_CLK_Pin GPIO_PIN_3
#define SPI3_CLK_GPIO_Port GPIOB
#define SPI3_MISO_Pin GPIO_PIN_4
#define SPI3_MISO_GPIO_Port GPIOB
#define SPI3_MOSI_Pin GPIO_PIN_5
#define SPI3_MOSI_GPIO_Port GPIOB
#define LU2_TX_Pin GPIO_PIN_6
#define LU2_TX_GPIO_Port GPIOB
#define LU2_RX_Pin GPIO_PIN_7
#define LU2_RX_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
