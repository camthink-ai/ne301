/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include <stdio.h>
#include "main.h"
#include "adc.h"
#include "gpdma.h"
#include "tim.h"
#include "usart.h"
#include "iwdg.h"
#include "gpio.h"
#include "crc.h"
#include "xspim.h"
#include "boot.h"
#include "sysclk.h"
#include "fsbl_app_common.h"

/* Private typedefs ----------------------------------------------------------*/
/* Private defines -----------------------------------------------------------*/
// static const sys_clk_config_t *sys_clk_config = (sys_clk_config_t *)SYS_CLK_CONFIG_SAVE_FLASH_BASE;
// static const quick_snapshot_config_t *quick_snapshot_config = (quick_snapshot_config_t *)QUICK_SNAPSHOT_CONFIG_SAVE_FLASH_BASE;
// static quick_snapshot_result_t *quick_snapshot_result = (quick_snapshot_result_t *)QUICK_SNAPSHOT_RESULT_PSRAM_BASE;
/* Private macros ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
void Error_Handler(void);

#ifndef NO_OTP_FUSE
static int32_t OTP_Config(void);
#endif
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint32_t hw_crc32_func(void *data, size_t size)
{
  return HAL_CRC_Calculate(&hcrc, (uint32_t*)data, size);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  sys_clk_config_t sys_clk_config = {0};

  /* Power on ICACHE */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_ICACTIVE_Msk;

  /* Disable error related to UNALIGN memory structures*/
  SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;

  /* Set back system and CPU clock source to HSI */
  __HAL_RCC_CPUCLK_CONFIG(RCC_CPUCLKSOURCE_HSI);
  __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_HSI);

  SCB_EnableICache();

#if defined(USE_DCACHE)
  /* Power on DCACHE */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk;
  SCB_EnableDCache();
#endif

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* Configure the startup system clock */
  SysClk_Config(SYSCLK_PROFILE_HSI_800MHZ);
  SysClk_PeriphCommonConfig();

  /* FIXME : can not be set currently under boot from flash due to bootrom lock */
#ifndef NO_OTP_FUSE
  /* Set OTP fuses for XSPI IO pins speed optimization */
  if(OTP_Config() != 0){
    Error_Handler();
  }
#endif /* NO_OTP_FUSE */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN Init */
  MX_IWDG_Init();
  MX_CRC_Init();
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  
  MX_XSPI1_Init();
  XSPI_PSRAM_EnableMemoryMappedMode();

  MX_XSPI2_Init();
  XSPI_NOR_EnableMemoryMappedMode();
  /* USER CODE END Init */
  
  /* Copy application from flash to SRAM/PSRAM */
  BOOT_Copy_Application();
  
  /* Configure the config system clock (profile: SYSCLK_PROFILE_DEFAULT, see sysclk.h) */
  memcpy(&sys_clk_config, (void *)SYS_CLK_CONFIG_SAVE_FLASH_BASE, sizeof(sys_clk_config_t));
  XSPI_NOR_DisableMemoryMappedMode();
  XSPI_NOR_ResetAndDeInit();
  if (sys_clk_config.sys_clk_profile > SYSCLK_PROFILE_MIN && sys_clk_config.sys_clk_profile < SYSCLK_PROFILE_MAX && sys_clk_config.sys_clk_profile != SYSCLK_PROFILE_HSI_800MHZ && sys_clk_config.crc32 == hw_crc32_func((void *)&sys_clk_config, sizeof(sys_clk_config_t) - sizeof(sys_clk_config.crc32))) {
    if (sys_clk_config.sys_clk_profile <= SYSCLK_PROFILE_HSE_400MHZ) {
      __HAL_RCC_PWR_CLK_ENABLE();
      HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
    }
    SysClk_Config(sys_clk_config.sys_clk_profile);
  }

  /* Jump to application */
  HAL_IWDG_Refresh(&hiwdg);
  printf("FSBL: %lu ms\r\n", HAL_GetTick());
  BOOT_Jump_Application();
  
  /* We should never get here as execution is now from user application */
  while(1)
  {
    __NOP();
  }
}
/* USER CODE BEGIN CLK 1 */
/* USER CODE END CLK 1 */

#ifndef NO_OTP_FUSE
/**
  * @brief  User OTP fuse Configuration
  *         The User Option Bytes are configured as follows :
  *            VDDIO_HSLV = 1 (enable the configuration of pads below 2.5V,
  *                            I/O speed otpmization at low-voltage allowed)
  *            XSPI1_HSLV = 1 (enable I/O XSPIM Port 1 high-speed option)
  *            XSPI2_HSLV = 1 (enable I/O XSPIM Port 2 high-speed option)
  *            Other User Option Bytes remain unchanged
  * @retval None
  */
static int32_t OTP_Config(void)
{
  #define BSEC_HW_CONFIG_ID        124U
  #define BSEC_HWS_HSLV_VDDIO3     (1U<<15)
  #define BSEC_HWS_HSLV_VDDIO2     (1U<<16)
#if PWR_USE_1V8
  #define BSEC_HWS_HSLV_VDD        (1U<<17)
#endif

  uint32_t fuse_id, bit_mask, data;
  BSEC_HandleTypeDef sBsecHandler;
  int32_t retr = 0;

  /* Enable BSEC & SYSCFG clocks to ensure BSEC data accesses */
  __HAL_RCC_BSEC_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();

  sBsecHandler.Instance = BSEC;

  /* Read current value of fuse */
  fuse_id = BSEC_HW_CONFIG_ID;
  if (HAL_BSEC_OTP_Read(&sBsecHandler, fuse_id, &data) == HAL_OK)
  {
    /* Check if bit has already been set */
#if PWR_USE_1V8
    bit_mask = BSEC_HWS_HSLV_VDDIO3 | BSEC_HWS_HSLV_VDDIO2 | BSEC_HWS_HSLV_VDD;
#else
    bit_mask = BSEC_HWS_HSLV_VDDIO3 | BSEC_HWS_HSLV_VDDIO2;
#endif
    if ((data & bit_mask) != bit_mask)
    {
      data |= bit_mask;
      /* Bitwise programming of lower bits */
      if (HAL_BSEC_OTP_Program(&sBsecHandler, fuse_id, data, HAL_BSEC_NORMAL_PROG) == HAL_OK)
      {
        /* Read lower bits to verify the correct programming */
        if (HAL_BSEC_OTP_Read(&sBsecHandler, fuse_id, &data) == HAL_OK)
        {
          if ((data & bit_mask) != bit_mask)
          {
            /* Error : Fuse programming not taken in account */
            retr = -1;
          }
        }
        else
        {
          /* Error : Fuse read unsuccessful */
          retr = -2;
        }
      }
      else
      {
        /* Error : Fuse programming unsuccessful */
        retr = -3;
      }
    }
  }
  else
  {
    /* Error  : Fuse read unsuccessful */
    retr = -4;
  }
  return retr;
}
#endif

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
