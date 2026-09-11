/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    xspim.h
  * @brief   This file contains all the function prototypes for
  *          the xspim.c file
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
#ifndef __XSPIM_H__
#define __XSPIM_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"

/* ============================================================================
 * Flash commands — GD55LX01GE (JEDEC Xccela OPI)
 * ============================================================================ */
#define GD55_OCTAL_IO_READ_CMD          0xCC   /* 4-Byte Octal I/O Fast Read (STR) */
#define GD55_OCTAL_IO_DTR_READ_CMD      0xFD   /* 4-Byte Octal I/O DTR Fast Read */
#define GD55_OCTAL_PAGE_PROG_CMD        0x8E   /* 4-Byte Extended Octal Page Program */
#define GD55_OCTAL_READ_STATUS_REG_CMD  0x05   /* Read Status Register */
#define GD55_OCTAL_SECTOR_ERASE_CMD     0x21   /* 4-Byte Sector Erase */
#define GD55_OCTAL_WRITE_ENABLE_CMD     0x06   /* Write Enable */
#define GD55_WRITE_VOL_CFG_REG_CMD      0x81   /* Write Volatile Configuration Register */

/* ============================================================================
 * Flash commands — MX66UM1G45G (Macronix OctaBus OPI)
 * ============================================================================ */
#define MX66_OCTAL_IO_DTR_READ_CMD      0xEE11 /* Octal I/O DTR Read */
#define MX66_OCTAL_PAGE_PROG_CMD        0x12ED /* Octal Page Program */
#define MX66_OCTAL_READ_STATUS_REG_CMD  0x05FA /* Read Status Register */
#define MX66_OCTAL_SECTOR_ERASE_CMD     0x21DE /* Sector Erase */
#define MX66_OCTAL_WRITE_ENABLE_CMD     0x06F9 /* Write Enable */
#define MX66_WRITE_CFG_REG_2_CMD        0x72   /* Write Configuration Register 2 */

/* Common SPI commands */
#define READ_STATUS_REG_CMD         0x05
#define WRITE_ENABLE_CMD            0x06
#define WRITE_DISABLE_CMD           0x04
#define READ_ID_CMD                 0x9F   /* JEDEC Read ID */

/* ============================================================================
 * Dummy clock cycles — GD55LX01GE (OPI DTR mode)
 * ============================================================================ */
#define GD55_DUMMY_CLOCK_CYCLES_READ            16  /* DTR Read: Config Byte<1> default = 16 */
#define GD55_DUMMY_CLOCK_CYCLES_READ_OCTAL      8   /* RDSR in DTR OPI: 8 dummy cycles */

/* ============================================================================
 * Dummy clock cycles — MX66UM1G45G (OctaBus DTR mode)
 * ============================================================================ */
#define MX66_DUMMY_CLOCK_CYCLES_READ            6   /* DTR Read: 6 dummy cycles */
#define MX66_DUMMY_CLOCK_CYCLES_READ_OCTAL      6   /* RDSR in OPI DTR: 6 dummy cycles */

/* Auto-polling values */
#define WRITE_ENABLE_MATCH_VALUE    0x02
#define WRITE_ENABLE_MASK_VALUE     0x02

#define MEMORY_READY_MATCH_VALUE    0x00
#define MEMORY_READY_MASK_VALUE     0x01

#define AUTO_POLLING_INTERVAL       0x10

/* ============================================================================
 * Configuration Register values — GD55LX01GE
 * ============================================================================ */
#define GD55_CFG_ADDR_IO_MODE       0x00000000  /* Byte<0>: I/O mode */
#define GD55_CFG_VAL_OCTAL_DTR_DQS  0xE7        /* Octal DTR with DQS */
#define GD55_CFG_ADDR_DUMMY_CYCLE   0x00000001  /* Byte<1>: Dummy cycle config (default 0x10=16) */

/* ============================================================================
 * Configuration Register values — MX66UM1G45G
 * ============================================================================ */
#define MX66_CFG_ADDR_IO_MODE       0x00000000  /* Configuration Register 2 */
#define MX66_CFG_VAL_OCTAL_DTR      0x02        /* Enable Octal DTR mode */

/* Memory delay */
#define MEMORY_REG_WRITE_DELAY      40
#define MEMORY_PAGE_PROG_DELAY      2

/* ============================================================================
 * Chip type detection
 * ============================================================================ */
#define MFI_MX66                    0xC2  /* Macronix Manufacturer ID */
#define MFI_GD55                    0xC8  /* GigaDevice Manufacturer ID */

typedef enum {
    XSPI_NOR_CHIP_UNKNOWN = 0,
    XSPI_NOR_CHIP_MX66,
    XSPI_NOR_CHIP_GD55,
} XSPI_NOR_ChipType;

/* Size of the flash */
#define XSPI_FLASH_SIZE             26
#define XSPI_PAGE_SIZE              256

/* End address of the SPI memory */
#define XSPI_END_ADDR               (1 << XSPI_FLASH_SIZE)



#define XSPI1_CLK_ENABLE()                 __HAL_RCC_XSPI1_CLK_ENABLE()
#define XSPI1_CLK_DISABLE()                __HAL_RCC_XSPI1_CLK_DISABLE()

#define XSPI1_CLK_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_DQS0_GPIO_CLK_ENABLE()       __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_DQS1_GPIO_CLK_ENABLE()       __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_CS_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_D0_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D1_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D2_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D3_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D4_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D5_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D6_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D7_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D8_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D9_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D10_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D11_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D12_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D13_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D14_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D15_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOP_CLK_ENABLE()

#define XSPI1_CLK_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_DQS0_GPIO_CLK_DISABLE()       __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_DQS1_GPIO_CLK_DISABLE()       __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_CS_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D0_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D1_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D2_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D3_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D4_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D5_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D6_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D7_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D8_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D9_GPIO_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D10_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D11_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D12_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D13_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D14_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()
#define XSPI1_D15_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_FORCE_RESET()                __HAL_RCC_XSPI1_FORCE_RESET()
#define XSPI1_RELEASE_RESET()              __HAL_RCC_XSPI1_RELEASE_RESET()

  /* Definition for HSPI Pins */
  /* HSPI_CLK */
#define XSPI1_CLK_PIN                      GPIO_PIN_4
#define XSPI1_CLK_GPIO_PORT                GPIOO
#define XSPI1_CLK_PIN_AF                   GPIO_AF9_XSPIM_P1
  /* HSPI_DQS0 */
#define XSPI1_DQS0_PIN                     GPIO_PIN_2
#define XSPI1_DQS0_GPIO_PORT               GPIOO
#define XSPI1_DQS0_PIN_AF                  GPIO_AF9_XSPIM_P1
  /* HSPI_DQS1 */
#define XSPI1_DQS1_PIN                     GPIO_PIN_3
#define XSPI1_DQS1_GPIO_PORT               GPIOO
#define XSPI1_DQS1_PIN_AF                  GPIO_AF9_XSPIM_P1
  /* HSPI_CS */
#define XSPI1_CS_PIN                       GPIO_PIN_0
#define XSPI1_CS_GPIO_PORT                 GPIOO
#define XSPI1_CS_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D0 */
#define XSPI1_D0_PIN                       GPIO_PIN_0
#define XSPI1_D0_GPIO_PORT                 GPIOP
#define XSPI1_D0_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D1 */
#define XSPI1_D1_PIN                       GPIO_PIN_1
#define XSPI1_D1_GPIO_PORT                 GPIOP
#define XSPI1_D1_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D2 */
#define XSPI1_D2_PIN                       GPIO_PIN_2
#define XSPI1_D2_GPIO_PORT                 GPIOP
#define XSPI1_D2_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D3 */
#define XSPI1_D3_PIN                       GPIO_PIN_3
#define XSPI1_D3_GPIO_PORT                 GPIOP
#define XSPI1_D3_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D4 */
#define XSPI1_D4_PIN                       GPIO_PIN_4
#define XSPI1_D4_GPIO_PORT                 GPIOP
#define XSPI1_D4_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D5 */
#define XSPI1_D5_PIN                       GPIO_PIN_5
#define XSPI1_D5_GPIO_PORT                 GPIOP
#define XSPI1_D5_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D6 */
#define XSPI1_D6_PIN                       GPIO_PIN_6
#define XSPI1_D6_GPIO_PORT                 GPIOP
#define XSPI1_D6_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D7 */
#define XSPI1_D7_PIN                       GPIO_PIN_7
#define XSPI1_D7_GPIO_PORT                 GPIOP
#define XSPI1_D7_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D8 */
#define XSPI1_D8_PIN                       GPIO_PIN_8
#define XSPI1_D8_GPIO_PORT                 GPIOP
#define XSPI1_D8_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D9 */
#define XSPI1_D9_PIN                       GPIO_PIN_9
#define XSPI1_D9_GPIO_PORT                 GPIOP
#define XSPI1_D9_PIN_AF                    GPIO_AF9_XSPIM_P1
  /* HSPI_D10 */
#define XSPI1_D10_PIN                      GPIO_PIN_10
#define XSPI1_D10_GPIO_PORT                GPIOP
#define XSPI1_D10_PIN_AF                   GPIO_AF9_XSPIM_P1
  /* HSPI_D11 */
#define XSPI1_D11_PIN                      GPIO_PIN_11
#define XSPI1_D11_GPIO_PORT                GPIOP
#define XSPI1_D11_PIN_AF                   GPIO_AF9_XSPIM_P1
  /* HSPI_D12 */
#define XSPI1_D12_PIN                      GPIO_PIN_12
#define XSPI1_D12_GPIO_PORT                GPIOP
#define XSPI1_D12_PIN_AF                   GPIO_AF9_XSPIM_P1
  /* HSPI_D13 */
#define XSPI1_D13_PIN                      GPIO_PIN_13
#define XSPI1_D13_GPIO_PORT                GPIOP
#define XSPI1_D13_PIN_AF                   GPIO_AF9_XSPIM_P1
  /* HSPI_D14 */
#define XSPI1_D14_PIN                      GPIO_PIN_14
#define XSPI1_D14_GPIO_PORT                GPIOP
#define XSPI1_D14_PIN_AF                   GPIO_AF9_XSPIM_P1
  /* HSPI_D15 */
#define XSPI1_D15_PIN                      GPIO_PIN_15
#define XSPI1_D15_GPIO_PORT                GPIOP
#define XSPI1_D15_PIN_AF                   GPIO_AF9_XSPIM_P1

  /* Aps256xx APMemory memory */

  /* Read Operations */
#define READ_CMD                                0x00
#define READ_LINEAR_BURST_CMD                   0x20
#define READ_HYBRID_BURST_CMD                   0x3F

  /* Write Operations */
#define WRITE_CMD                               0x80
#define WRITE_LINEAR_BURST_CMD                  0xA0
#define WRITE_HYBRID_BURST_CMD                  0xBF

  /* Reset Operations */
#define RESET_CMD                               0xFF

  /* Registers definition */
#define MR0                                     0x00000000
#define MR1                                     0x00000001
#define MR2                                     0x00000002
#define MR3                                     0x00000003
#define MR4                                     0x00000004
#define MR8                                     0x00000008

  /* Register Operations */
#define READ_REG_CMD                            0x40
#define WRITE_REG_CMD                           0xC0

  /* Default dummy clocks cycles */
#define XSPI1_DUMMY_CLOCK_CYCLES_READ                 6
#define XSPI1_DUMMY_CLOCK_CYCLES_WRITE                6

  /* Size of buffers */
#define BUFFERSIZE                              10240
#define KByte                                   1024

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_XSPIM_Init(void);

void MX_XSPI1_Init(void);
void MX_XSPI2_Init(void);
int32_t XSPI_PSRAM_EnableMemoryMappedMode(void);
int psram_memory_test(void);
int32_t XSPI_NOR_EnableMemoryMappedMode(void);
int32_t XSPI_NOR_DisableMemoryMappedMode(void);
void     XSPI_NOR_ResetAndDeInit(void);

int32_t XSPI_NOR_Erase4K(uint32_t EraseAddr);
int32_t XSPI_NOR_Write(const uint8_t *pData, uint32_t WriteAddr, uint32_t Size);
int32_t XSPI_NOR_Read(uint8_t *pData, uint32_t ReadAddr, uint32_t Size);

XSPI_NOR_ChipType XSPI_NOR_DetectChip(XSPI_HandleTypeDef *hxspi);
extern XSPI_NOR_ChipType xspi_nor_chip;
/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __XSPIM_H__ */

