/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    xspim.c
  * @brief   This file provides code for the configuration
  *          of the XSPIM instances.
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
#include "../Inc/xspim.h"
#include "main.h"
#include <stdio.h>

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
XSPI_HandleTypeDef hxspi1;
XSPI_HandleTypeDef hxspi2;
XSPI_NOR_ChipType xspi_nor_chip = XSPI_NOR_CHIP_UNKNOWN;
static void XSPI_NOR_OctalDTRModeCfg(XSPI_HandleTypeDef *hxspi);
uint32_t APS256_WriteReg(XSPI_HandleTypeDef *Ctx, uint32_t Address, uint8_t *Value);
uint32_t APS256_ReadReg(XSPI_HandleTypeDef *Ctx, uint32_t Address, uint8_t *Value, uint32_t LatencyCode);

/* XSPIM init function */
void MX_XSPIM_Init(void)
{


}

/**
  * @brief XSPI2 Initialization Function
  * @param None
  * @retval None
  */
void MX_XSPI2_Init(void)
{

    /* USER CODE BEGIN XSPI2_Init 0 */

    /* USER CODE END XSPI2_Init 0 */

    XSPIM_CfgTypeDef sXspiManagerCfg = {0};

    /* USER CODE BEGIN XSPI2_Init 1 */

    /* USER CODE END XSPI2_Init 1 */
    /* XSPI2 parameter configuration*/
    hxspi2.Instance = XSPI2;
    hxspi2.Init.FifoThresholdByte = 4;
    hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
    hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MICRON;     /* GD55 */
#if defined(BOARD_FLASH_SIZE) && BOARD_FLASH_SIZE == 128
    hxspi2.Init.MemorySize = HAL_XSPI_SIZE_1GB;
#else
    hxspi2.Init.MemorySize = HAL_XSPI_SIZE_512MB;
#endif
    hxspi2.Init.ChipSelectHighTimeCycle = 1;
    hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
    hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
    hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
    hxspi2.Init.ClockPrescaler = 1;
    hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
    hxspi2.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_ENABLE;
    hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
    hxspi2.Init.MaxTran = 0;
    hxspi2.Init.Refresh = 0;
    hxspi2.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
    if (HAL_XSPI_Init(&hxspi2) != HAL_OK)
    {
        Error_Handler();
    }
    
#ifndef XSPI_SKIP_XSPIM_CONFIG
    sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
    sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_2;
    sXspiManagerCfg.Req2AckTime = 1;
    if (HAL_XSPIM_Config(&hxspi2, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }
#endif
    /* USER CODE BEGIN XSPI2_Init 2 */

    /* Step 0: Detect which NOR Flash chip is connected */
    xspi_nor_chip = XSPI_NOR_DetectChip(&hxspi2);
#ifdef XSPI_SKIP_XSPIM_CONFIG
    printf("NOR Flash: %s (MFI=0x%02X)\r\n",
           (xspi_nor_chip == XSPI_NOR_CHIP_MX66) ? "MX66UM1G45G" :
           (xspi_nor_chip == XSPI_NOR_CHIP_GD55) ? "GD55LX01GE" : "UNKNOWN",
           (xspi_nor_chip == XSPI_NOR_CHIP_MX66) ? MFI_MX66 : MFI_GD55);
#endif
    if (xspi_nor_chip == XSPI_NOR_CHIP_UNKNOWN) {
        xspi_nor_chip = XSPI_NOR_CHIP_MX66;
        // Error_Handler();
    }

    /* If MX66 detected, switch MemoryType from MICRON to MACRONIX */
    if (xspi_nor_chip == XSPI_NOR_CHIP_MX66) {
        if (HAL_XSPI_Abort(&hxspi2) != HAL_OK) {
            Error_Handler();
        }
        if (HAL_XSPI_DeInit(&hxspi2) != HAL_OK) {
            Error_Handler();
        }
        hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
        if (HAL_XSPI_Init(&hxspi2) != HAL_OK) {
            Error_Handler();
        }
#ifndef XSPI_SKIP_XSPIM_CONFIG
        if (HAL_XSPIM_Config(&hxspi2, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
            Error_Handler();
        }
#endif
    }

    XSPI_NOR_OctalDTRModeCfg(&hxspi2);

}


void MX_XSPI1_Init(void)
{

    /* USER CODE BEGIN XSPI1_Init 0 */

    /* USER CODE END XSPI1_Init 0 */

    XSPIM_CfgTypeDef sXspiManagerCfg = {0};

    /* USER CODE BEGIN XSPI1_Init 1 */

    /* USER CODE END XSPI1_Init 1 */
    /* XSPI1 parameter configuration*/
    hxspi1.Instance = XSPI1;
    hxspi1.Init.FifoThresholdByte = 4;
    hxspi1.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
    hxspi1.Init.MemoryType = HAL_XSPI_MEMTYPE_APMEM_16BITS;
#if defined(BOARD_PSRAM_SIZE) && BOARD_PSRAM_SIZE == 64
    /* 512 Mbits = 64 MBytes PSRAM */
    hxspi1.Init.MemorySize = HAL_XSPI_SIZE_512MB;
#else
    /* 256 Mbits = 32 MBytes PSRAM */
    hxspi1.Init.MemorySize = HAL_XSPI_SIZE_256MB;
#endif
    hxspi1.Init.ChipSelectHighTimeCycle = 5;
    hxspi1.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
    hxspi1.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
    hxspi1.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
    hxspi1.Init.ClockPrescaler = 1;
    hxspi1.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
    hxspi1.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
    hxspi1.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_16KB;
    hxspi1.Init.MaxTran = 0;
    hxspi1.Init.Refresh = 0;
    hxspi1.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;
    if (HAL_XSPI_Init(&hxspi1) != HAL_OK)
    {
        Error_Handler();
    }
    sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
    sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_1;
    sXspiManagerCfg.Req2AckTime = 1;
    if (HAL_XSPIM_Config(&hxspi1, &sXspiManagerCfg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }
  /* USER CODE BEGIN XSPI1_Init 2 */

  /* USER CODE END XSPI1_Init 2 */

}

/**
* @brief XSPI MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hxspi: XSPI handle pointer
* @retval None
*/
void HAL_XSPI_MspDeInit(XSPI_HandleTypeDef* hxspi)
{
    if(hxspi->Instance==XSPI2)
    {
        /* USER CODE BEGIN XSPI2_MspDeInit 0 */

        /* USER CODE END XSPI2_MspDeInit 0 */
        /* Peripheral clock disable */
        __HAL_RCC_XSPIM_CLK_DISABLE();
        __HAL_RCC_XSPI2_CLK_DISABLE();

        /**XSPI2 GPIO Configuration
        PN4     ------> XSPIM_P2_IO2
        PN6     ------> XSPIM_P2_CLK
        PN8     ------> XSPIM_P2_IO4
        PN0     ------> XSPIM_P2_DQS0
        PN3     ------> XSPIM_P2_IO1
        PN5     ------> XSPIM_P2_IO3
        PN1     ------> XSPIM_P2_NCS1
        PN9     ------> XSPIM_P2_IO5
        PN2     ------> XSPIM_P2_IO0
        PN10     ------> XSPIM_P2_IO6
        PN11     ------> XSPIM_P2_IO7
        */
        HAL_GPIO_DeInit(GPION, GPIO_PIN_4|GPIO_PIN_6|GPIO_PIN_8|GPIO_PIN_0
                                |GPIO_PIN_3|GPIO_PIN_5|GPIO_PIN_1|GPIO_PIN_9
                                |GPIO_PIN_2|GPIO_PIN_10|GPIO_PIN_11);

        /* XSPI2 interrupt DeInit */
        HAL_NVIC_DisableIRQ(XSPI2_IRQn);
        /* USER CODE BEGIN XSPI2_MspDeInit 1 */

        /* USER CODE END XSPI2_MspDeInit 1 */
    }

    if(hxspi->Instance==XSPI1)
    {
        /* USER CODE BEGIN XSPI1_MspDeInit 0 */

        /* USER CODE END XSPI1_MspDeInit 0 */
        /* Peripheral clock disable */
        __HAL_RCC_XSPIM_CLK_DISABLE();
        __HAL_RCC_XSPI1_CLK_DISABLE();

        /**XSPI1 GPIO Configuration
        PP7     ------> XSPIM_P1_IO7
        PP6     ------> XSPIM_P1_IO6
        PP0     ------> XSPIM_P1_IO0
        PP4     ------> XSPIM_P1_IO4
        PP1     ------> XSPIM_P1_IO1
        PP15     ------> XSPIM_P1_IO15
        PP5     ------> XSPIM_P1_IO5
        PP12     ------> XSPIM_P1_IO12
        PP3     ------> XSPIM_P1_IO3
        PP2     ------> XSPIM_P1_IO2
        PP13     ------> XSPIM_P1_IO13
        PO2     ------> XSPIM_P1_DQS0
        PP11     ------> XSPIM_P1_IO11
        PP8     ------> XSPIM_P1_IO8
        PP14     ------> XSPIM_P1_IO14
        PO3     ------> XSPIM_P1_DQS1
        PO0     ------> XSPIM_P1_NCS1
        PP9     ------> XSPIM_P1_IO9
        PP10     ------> XSPIM_P1_IO10
        PO4     ------> XSPIM_P1_CLK
        */
        HAL_GPIO_DeInit(GPIOP, GPIO_PIN_7|GPIO_PIN_6|GPIO_PIN_0|GPIO_PIN_4
                            |GPIO_PIN_1|GPIO_PIN_15|GPIO_PIN_5|GPIO_PIN_12
                            |GPIO_PIN_3|GPIO_PIN_2|GPIO_PIN_13|GPIO_PIN_11
                            |GPIO_PIN_8|GPIO_PIN_14|GPIO_PIN_9|GPIO_PIN_10);

        HAL_GPIO_DeInit(GPIOO, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_4);

        /* USER CODE BEGIN XSPI1_MspDeInit 1 */

        /* USER CODE END XSPI1_MspDeInit 1 */
    }
}

/**
* @brief  Write mode register
* @param  Ctx Component object pointer
* @param  Address Register address
* @param  Value Register value pointer
* @retval error status
*/
uint32_t APS256_WriteReg(XSPI_HandleTypeDef *Ctx, uint32_t Address, uint8_t *Value)
{
    XSPI_RegularCmdTypeDef sCommand1={0};

    /* Initialize the write register command */
    sCommand1.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand1.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand1.InstructionWidth    = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand1.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand1.Instruction        = WRITE_REG_CMD;
    sCommand1.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand1.AddressWidth        = HAL_XSPI_ADDRESS_32_BITS;
    sCommand1.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand1.Address            = Address;
    sCommand1.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand1.DataMode           = HAL_XSPI_DATA_8_LINES;
    sCommand1.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand1.DataLength         = 2;
    sCommand1.DummyCycles        = 0;
    sCommand1.DQSMode            = HAL_XSPI_DQS_DISABLE;

    /* Configure the command */
    if (HAL_XSPI_Command(Ctx, &sCommand1, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Transmission of the data */
    if (HAL_XSPI_Transmit(Ctx, (uint8_t *)(Value), HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
* @brief  Read mode register value
* @param  Ctx Component object pointer
* @param  Address Register address
* @param  Value Register value pointer
* @param  LatencyCode Latency used for the access
* @retval error status
*/
uint32_t APS256_ReadReg(XSPI_HandleTypeDef *Ctx, uint32_t Address, uint8_t *Value, uint32_t LatencyCode)
{
    XSPI_RegularCmdTypeDef sCommand={0};

    /* Initialize the read register command */
    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth    = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.Instruction        = READ_REG_CMD;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth        = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.Address            = Address;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode           = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength            = 2;
    sCommand.DummyCycles        = (LatencyCode - 1U);
    sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

    /* Configure the command */
    if (HAL_XSPI_Command(Ctx, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Reception of the data */
    if (HAL_XSPI_Receive(Ctx, (uint8_t *)Value, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}


/**
* @brief  Switch from Octal Mode to Hexa Mode on the memory
* @param  None
* @retval None
*/
static void Configure_APMemory(void)
{
  /* MR0 register for read and write */
  uint8_t regW_MR0[2]={0x30,0x8D}; /* To configure AP memory Latency Type and drive Strength */ 
  uint8_t regR_MR0[2]={0};

  uint8_t regW_MR4[2]={0x20,0xF0}; /* To configure AP memory, Write Latency=7 up to 200MHz */
  uint8_t regR_MR4[2]={0};

  /* MR8 register for read and write */
  uint8_t regW_MR8[2]={0x4B,0x08}; /* To configure AP memory Burst Type */
  uint8_t regR_MR8[2]={0};

  /*Read Latency */
  uint8_t latency=6;

  /* Configure Read Latency and drive Strength */
  if (APS256_WriteReg(&hxspi1, MR0, regW_MR0) != HAL_OK)
  {
    Error_Handler();
  }

  /* Check MR0 configuration */
  if (APS256_ReadReg(&hxspi1, MR0, regR_MR0, latency ) != HAL_OK)
  {
    Error_Handler();
  }

  /* Check MR0 configuration */
  if (regR_MR0 [0] != regW_MR0 [0])
  {
    Error_Handler() ;
  }

  /* Configure Write Latency */
  if (APS256_WriteReg(&hxspi1, MR4, regW_MR4) != HAL_OK)
  {
    Error_Handler();
  }
  /* Check MR4 configuration */
  if (APS256_ReadReg(&hxspi1, MR4, regR_MR4, latency) != HAL_OK)
  {
    Error_Handler();
  }
  if (regR_MR4[0] != regW_MR4[0])
  {
    Error_Handler() ;
  }

  /* Configure Burst Length */
  if (APS256_WriteReg(&hxspi1, MR8, regW_MR8) != HAL_OK)
  {
    Error_Handler();
  }

  /* Check MR8 configuration */
  if (APS256_ReadReg(&hxspi1, MR8, regR_MR8, 6) != HAL_OK)
  {
    Error_Handler();
  }

  if (regR_MR8[0] != regW_MR8[0])
  {
    Error_Handler() ;
  }
}

/**
* @brief  Transfer Error callback.
* @param  hxspi: XSPI handle
* @retval None
*/
void HAL_XSPI_ErrorCallback(XSPI_HandleTypeDef *hxspi)
{
    Error_Handler();
}

/**
  * @brief  Detect NOR Flash chip type by reading JEDEC Manufacturer ID.
  *         Uses basic SPI 1-line mode (works before OPI configuration).
  * @param  hxspi: XSPI handle
  * @retval XSPI_NOR_ChipType
  */
XSPI_NOR_ChipType XSPI_NOR_DetectChip(XSPI_HandleTypeDef *hxspi)
{
    XSPI_RegularCmdTypeDef sCommand = {0};
    uint8_t id_buf[3] = {0};

    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.Instruction        = READ_ID_CMD;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode           = HAL_XSPI_DATA_1_LINE;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_DISABLE;
    sCommand.DataLength         = 3;
    sCommand.DummyCycles        = 0;
    sCommand.DQSMode            = HAL_XSPI_DQS_DISABLE;

    if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return XSPI_NOR_CHIP_UNKNOWN;
    }
    if (HAL_XSPI_Receive(hxspi, id_buf, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
        return XSPI_NOR_CHIP_UNKNOWN;
    }

    switch (id_buf[0]) {
        case MFI_MX66:  return XSPI_NOR_CHIP_MX66;
        case MFI_GD55:  return XSPI_NOR_CHIP_GD55;
        default:        return XSPI_NOR_CHIP_UNKNOWN;
    }
}

/**
* @brief  This function send a Write Enable and wait it is effective.
* @param  hxspi: XSPI handle
* @retval None
*/
static void XSPI_WriteEnable(XSPI_HandleTypeDef *hxspi)
{
    XSPI_RegularCmdTypeDef  sCommand ={0};
    uint8_t reg[2];

    /* Enable write operations ------------------------------------------ */
    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode           = HAL_XSPI_DATA_NONE;
    sCommand.DummyCycles        = 0;
    sCommand.DQSMode            = HAL_XSPI_DQS_DISABLE;

    if (xspi_nor_chip == XSPI_NOR_CHIP_MX66) {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_16_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
        sCommand.Instruction        = MX66_OCTAL_WRITE_ENABLE_CMD;
    } else {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
        sCommand.Instruction        = GD55_OCTAL_WRITE_ENABLE_CMD;
    }

    if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    /* Configure automatic polling mode to wait for write enabling ---- */
    sCommand.AddressMode    = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth   = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.DataMode       = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode    = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength     = 2;
    sCommand.DQSMode        = HAL_XSPI_DQS_ENABLE;
    sCommand.Address        = 0x0;

    if (xspi_nor_chip == XSPI_NOR_CHIP_MX66) {
        sCommand.Instruction    = MX66_OCTAL_READ_STATUS_REG_CMD;
        sCommand.DummyCycles    = MX66_DUMMY_CLOCK_CYCLES_READ_OCTAL;
    } else {
        sCommand.Instruction    = GD55_OCTAL_READ_STATUS_REG_CMD;
        sCommand.AddressMode    = HAL_XSPI_ADDRESS_NONE;
        sCommand.DummyCycles    = GD55_DUMMY_CLOCK_CYCLES_READ_OCTAL;
    }

    do
    {
        if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }

        if (HAL_XSPI_Receive(hxspi, reg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }
    } while((reg[0] & WRITE_ENABLE_MASK_VALUE) != WRITE_ENABLE_MATCH_VALUE);
}

/**
* @brief  This function read the SR of the memory and wait the EOP.
* @param  hxspi: XSPI handle
* @retval None
*/
static void XSPI_AutoPollingMemReady(XSPI_HandleTypeDef *hxspi)
{
    XSPI_RegularCmdTypeDef  sCommand={0};
    uint8_t reg[2];
    uint32_t start_tick = 0;

    /* Configure automatic polling mode to wait for memory ready ------ */
    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.Address            = 0x0;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode           = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength         = 2;
    sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

    if (xspi_nor_chip == XSPI_NOR_CHIP_MX66) {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_16_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
        sCommand.Instruction        = MX66_OCTAL_READ_STATUS_REG_CMD;
        sCommand.DummyCycles        = MX66_DUMMY_CLOCK_CYCLES_READ_OCTAL;
    } else {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
        sCommand.Instruction        = GD55_OCTAL_READ_STATUS_REG_CMD;
        sCommand.AddressMode        = HAL_XSPI_ADDRESS_NONE;
        sCommand.DummyCycles        = GD55_DUMMY_CLOCK_CYCLES_READ_OCTAL;
    }

    start_tick = HAL_GetTick();
    do
    {
        if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }

        if (HAL_XSPI_Receive(hxspi, reg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }
    } while((reg[0] & MEMORY_READY_MASK_VALUE) != MEMORY_READY_MATCH_VALUE && (HAL_GetTick() - start_tick) < HAL_XSPI_TIMEOUT_DEFAULT_VALUE);

    if ((reg[0] & MEMORY_READY_MASK_VALUE) != MEMORY_READY_MATCH_VALUE)
    {
        Error_Handler();
    }
}

/**
* @brief  This function configure the memory in Octal DTR mode.
* @param  hxspi: XSPI handle
* @retval None
*/
static void XSPI_NOR_OctalDTRModeCfg(XSPI_HandleTypeDef *hxspi)
{
    uint8_t reg = 0;
    XSPI_RegularCmdTypeDef  sCommand = {0};
    XSPI_AutoPollingTypeDef sConfig  = {0};

    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth    = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_DISABLE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_DISABLE;
    sCommand.DummyCycles        = 0;
    sCommand.DQSMode            = HAL_XSPI_DQS_DISABLE;
    sConfig.MatchMode           = HAL_XSPI_MATCH_MODE_AND;
    sConfig.AutomaticStop       = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
    sConfig.IntervalTime        = 0x10;

    /* Step 1: Write Enable (SPI mode, common to both) */
    sCommand.Instruction = WRITE_ENABLE_CMD;
    sCommand.DataMode    = HAL_XSPI_DATA_NONE;
    sCommand.AddressMode = HAL_XSPI_ADDRESS_NONE;

    if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    /* Step 2: Auto-poll WEL (SPI mode, common to both) */
    sConfig.MatchMask           = 0x02;
    sConfig.MatchValue          = 0x02;

    sCommand.Instruction        = READ_STATUS_REG_CMD;
    sCommand.DataMode           = HAL_XSPI_DATA_1_LINE;
    sCommand.DataLength         = 1;

    if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_XSPI_AutoPolling(hxspi, &sConfig, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    if (xspi_nor_chip == XSPI_NOR_CHIP_MX66)
    {
        /* ================================================================
         * MX66: Write Configuration Register 2 (OctaBus)
         *   Command: 0x72, 32-bit address, value=0x02
         *   MX66 preserves WEL after config write — WEL polling still works
         * ================================================================ */
        sCommand.Instruction  = MX66_WRITE_CFG_REG_2_CMD;
        sCommand.AddressMode  = HAL_XSPI_ADDRESS_1_LINE;
        sCommand.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
        sCommand.Address      = MX66_CFG_ADDR_IO_MODE;
        reg = MX66_CFG_VAL_OCTAL_DTR;

        if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }

        if (HAL_XSPI_Transmit(hxspi, &reg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }

        /* Step 4: Wait for write complete (WIP=0, WEL still set on MX66) */
        sConfig.MatchMask   = 0x02;  /* WEL */
        sConfig.MatchValue  = 0x02;

        sCommand.Instruction = READ_STATUS_REG_CMD;
        sCommand.DataMode    = HAL_XSPI_DATA_1_LINE;
        sCommand.DataLength  = 1;
    }
    else /* GD55 */
    {
        /* ================================================================
         * GD55: Write Volatile Configuration Register (JEDEC Xccela)
         *   Command: 0x81, 24-bit address (GD55 default 3-byte addr mode),
         *   value=0xE7.  GD55 clears WEL after config write.
         * ================================================================ */
        sCommand.Instruction  = GD55_WRITE_VOL_CFG_REG_CMD;
        sCommand.AddressMode  = HAL_XSPI_ADDRESS_1_LINE;
        sCommand.AddressWidth = HAL_XSPI_ADDRESS_24_BITS;
        sCommand.Address      = GD55_CFG_ADDR_IO_MODE;
        reg = GD55_CFG_VAL_OCTAL_DTR_DQS;

        if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }

        if (HAL_XSPI_Transmit(hxspi, &reg, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        {
            Error_Handler();
        }

        /* Step 4: Wait for write complete (WIP=0, GD55 clears WEL) */
        sConfig.MatchMask   = 0x01;  /* WIP */
        sConfig.MatchValue  = 0x00;

        sCommand.Instruction = READ_STATUS_REG_CMD;
        sCommand.DataMode    = HAL_XSPI_DATA_1_LINE;
        sCommand.DataLength  = 1;
    }

    if (HAL_XSPI_Command(hxspi, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_XSPI_AutoPolling(hxspi, &sConfig, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
* @brief XSPI MSP Initialization
* This function configures the hardware resources used in this example
* @param hxspi: XSPI handle pointer
* @retval None
*/
void HAL_XSPI_MspInit(XSPI_HandleTypeDef* hxspi)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    if(hxspi->Instance==XSPI2)
    {
    /* USER CODE BEGIN XSPI2_MspInit 0 */
        HAL_PWREx_ConfigVddIORange(PWR_VDDIO3, PWR_VDDIO_RANGE_1V8);
    /* USER CODE END XSPI2_MspInit 0 */

    /** Initializes the peripherals clock
    */
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_XSPI2;
        PeriphClkInitStruct.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
        {
            Error_Handler();
        }

        /* Peripheral clock enable */
        __HAL_RCC_XSPIM_CLK_ENABLE();
        __HAL_RCC_XSPI2_CLK_ENABLE();

        __HAL_RCC_XSPI2_FORCE_RESET();
        __HAL_RCC_XSPI2_RELEASE_RESET();
    
        __HAL_RCC_GPION_CLK_ENABLE();
        /**XSPI2 GPIO Configuration
        PN4     ------> XSPIM_P2_IO2
        PN6     ------> XSPIM_P2_CLK
        PN8     ------> XSPIM_P2_IO4
        PN0     ------> XSPIM_P2_DQS0
        PN3     ------> XSPIM_P2_IO1
        PN5     ------> XSPIM_P2_IO3
        PN1     ------> XSPIM_P2_NCS1
        PN9     ------> XSPIM_P2_IO5
        PN2     ------> XSPIM_P2_IO0
        PN10     ------> XSPIM_P2_IO6
        PN11     ------> XSPIM_P2_IO7
        */
        GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6|GPIO_PIN_8|GPIO_PIN_0
                              |GPIO_PIN_3|GPIO_PIN_5|GPIO_PIN_1|GPIO_PIN_9
                              |GPIO_PIN_2|GPIO_PIN_10|GPIO_PIN_11;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF9_XSPIM_P2;
        HAL_GPIO_Init(GPION, &GPIO_InitStruct);

        /* XSPI2 interrupt Init */
        HAL_NVIC_SetPriority(XSPI2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(XSPI2_IRQn);
    /* USER CODE BEGIN XSPI2_MspInit 1 */

    /* USER CODE END XSPI2_MspInit 1 */

    }

    if(hxspi->Instance==XSPI1)
    {
        /* USER CODE BEGIN XSPI1_MspInit 0 */
        /* XSPI power enable */
        __HAL_RCC_PWR_CLK_ENABLE();
        HAL_PWREx_EnableVddIO2(); // change, IO2 for XSPI1
        HAL_PWREx_ConfigVddIORange(PWR_VDDIO2, PWR_VDDIO_RANGE_1V8);
        /* USER CODE END XSPI1_MspInit 0 */

        /** Initializes the peripherals clock
         */
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_XSPI1;
        PeriphClkInitStruct.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
        {
            Error_Handler();
        }

        /* Peripheral clock enable */
        __HAL_RCC_XSPIM_CLK_ENABLE();
        __HAL_RCC_XSPI1_CLK_ENABLE();

        __HAL_RCC_GPIOP_CLK_ENABLE();
        __HAL_RCC_GPIOO_CLK_ENABLE();
        /**XSPI1 GPIO Configuration
        PP7     ------> XSPIM_P1_IO7
        PP6     ------> XSPIM_P1_IO6
        PP0     ------> XSPIM_P1_IO0
        PP4     ------> XSPIM_P1_IO4
        PP1     ------> XSPIM_P1_IO1
        PP15     ------> XSPIM_P1_IO15
        PP5     ------> XSPIM_P1_IO5
        PP12     ------> XSPIM_P1_IO12
        PP3     ------> XSPIM_P1_IO3
        PP2     ------> XSPIM_P1_IO2
        PP13     ------> XSPIM_P1_IO13
        PO2     ------> XSPIM_P1_DQS0
        PP11     ------> XSPIM_P1_IO11
        PP8     ------> XSPIM_P1_IO8
        PP14     ------> XSPIM_P1_IO14
        PO3     ------> XSPIM_P1_DQS1
        PO0     ------> XSPIM_P1_NCS1
        PP9     ------> XSPIM_P1_IO9
        PP10     ------> XSPIM_P1_IO10
        PO4     ------> XSPIM_P1_CLK
        */
        GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_6|GPIO_PIN_0|GPIO_PIN_4
                                |GPIO_PIN_1|GPIO_PIN_15|GPIO_PIN_5|GPIO_PIN_12
                                |GPIO_PIN_3|GPIO_PIN_2|GPIO_PIN_13|GPIO_PIN_11
                                |GPIO_PIN_8|GPIO_PIN_14|GPIO_PIN_9|GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF9_XSPIM_P1;
        HAL_GPIO_Init(GPIOP, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_0|GPIO_PIN_4;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF9_XSPIM_P1;
        HAL_GPIO_Init(GPIOO, &GPIO_InitStruct);

    /* USER CODE BEGIN XSPI1_MspInit 1 */

    /* USER CODE END XSPI1_MspInit 1 */

    }

}

/**
  * @brief  Enable memory mapped mode for the XSPI memory on DTR mode.
  */
int32_t XSPI_NOR_EnableMemoryMappedMode(void)
{
    XSPI_MemoryMappedTypeDef sMemMappedCfg = {0};
    XSPI_RegularCmdTypeDef sCommand = {0};
    uint16_t instr_width;
    uint32_t instr_dtr;
    uint32_t page_prog_cmd;
    uint32_t read_cmd;
    uint32_t read_dummy;

    if (xspi_nor_chip == XSPI_NOR_CHIP_MX66) {
        instr_width   = HAL_XSPI_INSTRUCTION_16_BITS;
        instr_dtr     = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
        page_prog_cmd = MX66_OCTAL_PAGE_PROG_CMD;
        read_cmd      = MX66_OCTAL_IO_DTR_READ_CMD;
        read_dummy    = MX66_DUMMY_CLOCK_CYCLES_READ;
    } else {
        instr_width   = HAL_XSPI_INSTRUCTION_8_BITS;
        instr_dtr     = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
        page_prog_cmd = GD55_OCTAL_PAGE_PROG_CMD;
        read_cmd      = GD55_OCTAL_IO_DTR_READ_CMD;
        read_dummy    = GD55_DUMMY_CLOCK_CYCLES_READ;
    }

    /* Configure automatic polling mode to wait for end of erase -------- */
    XSPI_AutoPollingMemReady(&hxspi2);

    /* Cached data is not up to date due to indirect write.
        Force new read by invalidating the corresponding cache lines */
    // SCB_InvalidateDCache_by_Addr((void *)(XSPI2_BASE + address), BUFFERSIZE);


    /* Memory-mapped mode configuration ------------------------------- */
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth   = instr_width;
    sCommand.InstructionDTRMode = instr_dtr;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DummyCycles        = 0;
    sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

    sCommand.OperationType = HAL_XSPI_OPTYPE_WRITE_CFG;
    sCommand.Instruction   = page_prog_cmd;
    sCommand.DataMode      = HAL_XSPI_DATA_8_LINES;
    sCommand.DataLength    = 1;
    sCommand.DQSMode       = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    sCommand.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
    sCommand.Instruction   = read_cmd;
    sCommand.DummyCycles   = read_dummy;
    sCommand.DQSMode       = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    sMemMappedCfg.NoPrefetchAXI       = HAL_XSPI_AXI_PREFETCH_ENABLE;
    sMemMappedCfg.NoPrefetchData      = HAL_XSPI_AUTOMATIC_PREFETCH_ENABLE;
    sMemMappedCfg.TimeOutActivation   = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
    sMemMappedCfg.TimeoutPeriodClock  = 0x40;

    if (HAL_XSPI_MemoryMapped(&hxspi2, &sMemMappedCfg) != HAL_OK)
    {
        Error_Handler();
    }
    return 0;
}

int32_t XSPI_PSRAM_EnableMemoryMappedMode(void)
{
    XSPI_RegularCmdTypeDef sCommand = {0};
    XSPI_MemoryMappedTypeDef sMemMappedCfg = {0};
    Configure_APMemory();

        /*Configure Memory Mapped mode*/

    HAL_XSPI_SetClockPrescaler(&hxspi1, 0);// change, XSPI1/PSRAM CLK: 200MHz

    sCommand.OperationType      = HAL_XSPI_OPTYPE_WRITE_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.Instruction        = WRITE_CMD;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.Address            = 0x0;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode           = HAL_XSPI_DATA_16_LINES;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength         = BUFFERSIZE;
    sCommand.DummyCycles        = XSPI1_DUMMY_CLOCK_CYCLES_WRITE;
    sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(&hxspi1, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    sCommand.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
    sCommand.Instruction = READ_CMD;
    sCommand.DummyCycles = XSPI1_DUMMY_CLOCK_CYCLES_READ;
    sCommand.DQSMode     = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(&hxspi1, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        Error_Handler();
    }

    sMemMappedCfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_ENABLE;
    sMemMappedCfg.TimeoutPeriodClock      = 0x34;


    if (HAL_XSPI_MemoryMapped(&hxspi1, &sMemMappedCfg) != HAL_OK)
    {
        Error_Handler();
    }

    return 0;
}
/**
  * @brief  Exit form memory-mapped mode
  */
int32_t XSPI_NOR_DisableMemoryMappedMode(void)
{
    int32_t ret = 0;

    __DSB();
    if (HAL_XSPI_Abort(&hxspi2) != HAL_OK)
    {
        ret = -1;
    }

    /* Return BSP status */
    return ret;
}

/**
  * @brief  Reset NOR Flash from OPI mode back to SPI mode, then DeInit XSPI2.
  *         Reset sequence is chip-specific: GD55 uses JEDEC 66h/99h on 8-line
  *         STR; MX66 OctaBus may need a different command.
  * @param  None
  * @retval None
  */
void XSPI_NOR_ResetAndDeInit(void)
{
    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_NONE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode           = HAL_XSPI_DATA_NONE;
    sCommand.DummyCycles        = 0;
    sCommand.DQSMode            = HAL_XSPI_DQS_DISABLE;

    if (xspi_nor_chip == XSPI_NOR_CHIP_GD55) {
        /* GD55: JEDEC standard software reset — 66h+99h on 8 lines STR */
        sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
        sCommand.Instruction = 0x66;  /* Enable Reset */
        HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);
        sCommand.Instruction = 0x99;  /* Reset Device */
        HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);
    } else {
        /* MX66 OctaBus: 6699h/9966h in OctaBus format (16-bit DTR, 8-line) */
        sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_16_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
        sCommand.Instruction = 0x6699;  /* Enable Reset (2-byte) */
        HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);
        sCommand.Instruction = 0x9966;  /* Reset Device (2-byte) */
        HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE);
    }

    /* De-initialize XSPI2 (clocks off, GPIOs de-init, IRQ disable) */
    if (HAL_XSPI_DeInit(&hxspi2) != HAL_OK) {
        Error_Handler();
    }
}

#define XSPI_SECTOR_SIZE  4096  // 4K

int32_t XSPI_NOR_Erase4K(uint32_t EraseAddr)
{
    XSPI_NOR_ChipType chip = xspi_nor_chip;
    XSPI_WriteEnable(&hxspi2);

    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.Address            = EraseAddr;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataMode           = HAL_XSPI_DATA_NONE;
    sCommand.DummyCycles        = 0;
    sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

    if (chip == XSPI_NOR_CHIP_MX66) {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_16_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
        sCommand.Instruction        = MX66_OCTAL_SECTOR_ERASE_CMD;
    } else {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
        sCommand.Instruction        = GD55_OCTAL_SECTOR_ERASE_CMD;
    }

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;

    // Wait for erase completion
    XSPI_AutoPollingMemReady(&hxspi2);

    return 0;
}

int32_t XSPI_NOR_Write(const uint8_t *pData, uint32_t WriteAddr, uint32_t Size)
{
    uint32_t current_addr = WriteAddr;
    uint32_t bytes_left = Size;
    uint32_t chunk;
    XSPI_RegularCmdTypeDef sCommand = {0};
    XSPI_NOR_ChipType chip = xspi_nor_chip;

    while (bytes_left > 0)
    {
        // Calculate how many bytes to write this time (cannot cross page boundary)
        chunk = XSPI_PAGE_SIZE - (current_addr % XSPI_PAGE_SIZE);
        if (chunk > bytes_left)
            chunk = bytes_left;

        // 1. Enable write
        XSPI_WriteEnable(&hxspi2);

        // 2. Configure write command
        sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
        sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
        sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
        sCommand.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
        sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
        sCommand.Address            = current_addr;
        sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
        sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
        sCommand.DataMode           = HAL_XSPI_DATA_8_LINES;
        sCommand.DataLength         = chunk;
        sCommand.DummyCycles        = 0;
        sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

        if (chip == XSPI_NOR_CHIP_MX66) {
            sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_16_BITS;
            sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
            sCommand.Instruction        = MX66_OCTAL_PAGE_PROG_CMD;
        } else {
            sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
            sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
            sCommand.Instruction        = GD55_OCTAL_PAGE_PROG_CMD;
        }

        if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
            return -1;

        // 3. Send data
        if (HAL_XSPI_Transmit(&hxspi2, (uint8_t *)pData, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
            return -2;

        // 4. Wait for write completion
        XSPI_AutoPollingMemReady(&hxspi2);
        // Update pointer and count
        current_addr += chunk;
        pData += chunk;
        bytes_left -= chunk;
    }

    return 0;
}

int32_t XSPI_NOR_Read(uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
    XSPI_RegularCmdTypeDef sCommand = {0};
    XSPI_NOR_ChipType chip = xspi_nor_chip;

    sCommand.OperationType      = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.AddressMode        = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth       = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode     = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.Address            = ReadAddr;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataDTRMode        = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataMode           = HAL_XSPI_DATA_8_LINES;
    sCommand.DataLength         = Size;
    sCommand.DQSMode            = HAL_XSPI_DQS_ENABLE;

    if (chip == XSPI_NOR_CHIP_MX66) {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_16_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
        sCommand.Instruction        = MX66_OCTAL_IO_DTR_READ_CMD;
        sCommand.DummyCycles        = MX66_DUMMY_CLOCK_CYCLES_READ;
    } else {
        sCommand.InstructionWidth   = HAL_XSPI_INSTRUCTION_8_BITS;
        sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
        sCommand.Instruction        = GD55_OCTAL_IO_DTR_READ_CMD;
        sCommand.DummyCycles        = GD55_DUMMY_CLOCK_CYCLES_READ;
    }

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -1;

    if (HAL_XSPI_Receive(&hxspi2, pData, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return -2;

    return 0;
}


#define PSRAM_BASE_ADDR   ((uint32_t)0x90000000)
#if defined(BOARD_PSRAM_SIZE) && BOARD_PSRAM_SIZE == 64
/* 64MB total PSRAM size (0x90000000 ~ 0x93FFFFFF) */
#define PSRAM_SIZE        (64 * 1024 * 1024)
#else
/* 32MB total PSRAM size (0x90000000 ~ 0x91FFFFFF) */
#define PSRAM_SIZE        (32 * 1024 * 1024)
#endif
#define PSRAM_END_ADDR    (PSRAM_BASE_ADDR + PSRAM_SIZE)
#define PSRAM_TEST_STEP   4


int psram_memory_test(void)
{
    uint32_t *psram_ptr = (uint32_t *)PSRAM_BASE_ADDR;
    uint32_t num_words = PSRAM_SIZE / sizeof(uint32_t);
    uint32_t i;
    uint32_t error_count = 0;

    printf("PSRAM Test: Write Phase...\r\n");

    // Write known data
    for (i = 0; i < num_words; i++) {
        psram_ptr[i] = 0xA5A50000 | (i & 0xFFFF); // Customizable write pattern
    }

    printf("PSRAM Test: Verification Phase...\r\n");

    // Verify data
    for (i = 0; i < num_words; i++) {
        uint32_t expected = 0xA5A50000 | (i & 0xFFFF);
        uint32_t readback = psram_ptr[i];
        if (readback != expected) {
            printf("Address 0x%08lX: Expected 0x%08lX, Actual 0x%08lX\r\n",
                   (uint32_t)&psram_ptr[i], expected, readback);
            error_count++;
            if (error_count > 10) {
                printf("Too many errors, test aborted\r\n");
                return -1;
            }
        }
    }

    if (error_count == 0) {
        printf("PSRAM 32MB test passed!\r\n");
        return 0;
    } else {
        printf("PSRAM test failed, error count: %lu\r\n", error_count);
        return -1;
    }
}
