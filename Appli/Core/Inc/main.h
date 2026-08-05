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

#if defined ( __ICCARM__ )
#  define CMSE_NS_CALL  __cmse_nonsecure_call
#  define CMSE_NS_ENTRY __cmse_nonsecure_entry
#else
#  define CMSE_NS_CALL  __attribute((cmse_nonsecure_call))
#  define CMSE_NS_ENTRY __attribute((cmse_nonsecure_entry))
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* Function pointer declaration in non-secure*/
#if defined ( __ICCARM__ )
typedef void (CMSE_NS_CALL *funcptr)(void);
#else
typedef void CMSE_NS_CALL (*funcptr)(void);
#endif

/* typedef for non-secure callback functions */
typedef funcptr funcptr_NS;

/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void _Error_Handler_(void);

#define Error_Handler() do { printf("Error_Handler, file: %s line: %d\n", __FILE__, __LINE__); _Error_Handler_(); } while(0)
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PWR_PIR_ON_Pin GPIO_PIN_6
#define PWR_PIR_ON_GPIO_Port GPIOC
#define WIFI_ULP_WAKEUP_Pin GPIO_PIN_12
#define WIFI_ULP_WAKEUP_GPIO_Port GPIOD
#define WIFI_IRQ_Pin GPIO_PIN_8
#define WIFI_IRQ_GPIO_Port GPIOE
#define WIFI_POC_IN_Pin GPIO_PIN_15
#define WIFI_POC_IN_GPIO_Port GPIOB
#define WIFI_STA_Pin GPIO_PIN_5
#define WIFI_STA_GPIO_Port GPIOD
#define PWR_WIFI_ON_Pin GPIO_PIN_9
#define PWR_WIFI_ON_GPIO_Port GPIOB
#define WIFI_RESET_N_Pin GPIO_PIN_11
#define WIFI_RESET_N_GPIO_Port GPIOD
#define TF_INT_Pin GPIO_PIN_0
#define TF_INT_GPIO_Port GPIOD
#define PIR_INT_OUT_Pin GPIO_PIN_8
#define PIR_INT_OUT_GPIO_Port GPIOD
#define PWR_USB_Pin GPIO_PIN_13
#define PWR_USB_GPIO_Port GPIOB
#define PIR_Serial_IN_Pin GPIO_PIN_13
#define PIR_Serial_IN_GPIO_Port GPIOE
#define PWR_SENSOR_ON_Pin GPIO_PIN_9
#define PWR_SENSOR_ON_GPIO_Port GPIOF
#define LED_Pin GPIO_PIN_9
#define LED_GPIO_Port GPIOG
#define LED1_Pin GPIO_PIN_3
#define LED1_GPIO_Port GPIOF
#define LED2_Pin GPIO_PIN_10
#define LED2_GPIO_Port GPIOG
#define PWR_CAT1_ON_Pin GPIO_PIN_8
#define PWR_CAT1_ON_GPIO_Port GPIOG
#define PWR_COEDC_Pin GPIO_PIN_15
#define PWR_COEDC_GPIO_Port GPIOG
#define PWR_BAT_DET_ON_Pin GPIO_PIN_11
#define PWR_BAT_DET_ON_GPIO_Port GPIOA
#define PWR_TF_ON_Pin GPIO_PIN_1
#define PWR_TF_ON_GPIO_Port GPIOA
#define ALA_IN_Pin GPIO_PIN_12
#define ALA_IN_GPIO_Port GPIOB
#define KEY_Pin GPIO_PIN_13
#define KEY_GPIO_Port GPIOC
#define TFT_BL_Pin GPIO_PIN_2
#define TFT_BL_GPIO_Port GPIOB
#define TFT_RST_Pin GPIO_PIN_0
#define TFT_RST_GPIO_Port GPIOB
#define TFT_CLK_Pin GPIO_PIN_5
#define TFT_CLK_GPIO_Port GPIOA
#define TFT_CS_Pin GPIO_PIN_0
#define TFT_CS_GPIO_Port GPIOA
#define TFT_MOSI_Pin GPIO_PIN_7
#define TFT_MOSI_GPIO_Port GPIOA
#define TFT_DC_Pin GPIO_PIN_4
#define TFT_DC_GPIO_Port GPIOB

#define PWR_USB_3V3_Pin GPIO_PIN_13
#define PWR_USB_3V3_GPIO_Port GPIOG

////////////////////////////////////////////////////////////
//clk
#define MM_HALOW_SPI_CLK_Pin GPIO_PIN_5
#define MM_HALOW_SPI_CLK_GPIO_Port GPIOA
//cs
#define MM_HALOW_SPI_CS_Pin GPIO_PIN_0
#define MM_HALOW_SPI_CS_GPIO_Port GPIOA
//mosi
#define MM_HALOW_SPI_MOSI_Pin GPIO_PIN_7
#define MM_HALOW_SPI_MOSI_GPIO_Port GPIOA
//miso
#define MM_HALOW_SPI_MISO_Pin GPIO_PIN_4
#define MM_HALOW_SPI_MISO_GPIO_Port GPIOB

#define MM_HALOW_SPI_IRQ_Pin GPIO_PIN_4
#define MM_HALOW_SPI_IRQ_GPIO_Port GPIOA
#define MM_HALOW_BUSY_Pin GPIO_PIN_15
#define MM_HALOW_BUSY_GPIO_Port GPIOA

#define MM_HALOW_RESET_Pin GPIO_PIN_0
#define MM_HALOW_RESET_GPIO_Port GPIOB
#define MM_HALOW_WAKE_Pin GPIO_PIN_2
#define MM_HALOW_WAKE_GPIO_Port GPIOB

#define MM_HALOW_SPI_IRQn            (EXTI4_IRQn)
#define MM_HALOW_SPI_IRQ_HANDLER     EXTI4_IRQHandler

#define MM_HALOW_BUSY_IRQn           (EXTI15_IRQn)
#define MM_HALOW_BUSY_IRQ_HANDLER    EXTI15_IRQHandler


////////////////////////////////////////////////////////////


/* USER CODE BEGIN Private defines */
void SystemClock_Config(void);
void Fuse_Programming(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
