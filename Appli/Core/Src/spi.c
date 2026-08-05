/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.c
  * @brief   This file provides code for the configuration
  *          of the SPI instances.
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
#include "spi.h"

/* USER CODE BEGIN 0 */
#include <string.h>
#include "cmsis_os2.h"
#include "rcc_ic_auto.h"
#include "netif_manager.h"
/* USER CODE END 0 */

SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi4;
SPI_HandleTypeDef hspi6;
DMA_HandleTypeDef handle_HPDMA1_Channel9;
DMA_HandleTypeDef handle_HPDMA1_Channel8;
DMA_HandleTypeDef handle_HPDMA1_Channel5;
DMA_HandleTypeDef handle_HPDMA1_Channel4;
DMA_HandleTypeDef handle_HPDMA1_Channel3;
DMA_HandleTypeDef handle_HPDMA1_Channel2;
DMA_HandleTypeDef handle_GPDMA1_Channel8;
DMA_HandleTypeDef handle_GPDMA1_Channel9;
DMA_HandleTypeDef handle_GPDMA1_Channel10;
DMA_HandleTypeDef handle_GPDMA1_Channel11;

osSemaphoreId_t sem_spi2 = NULL;
osSemaphoreId_t sem_spi6 = NULL;

/* SPI2 init function */
void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 0x7;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi2.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi2.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi2.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi2.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi2.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi2.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi2.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi2.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */
  if (sem_spi2 == NULL) {
    sem_spi2 = osSemaphoreNew(1, 0, NULL);
    if (sem_spi2 == NULL) {
      Error_Handler();
    }
  }
  /* USER CODE END SPI2_Init 2 */

}
/* SPI4 init function */
void MX_SPI4_Init(void)
{

  /* USER CODE BEGIN SPI4_Init 0 */

  /* USER CODE END SPI4_Init 0 */

  /* USER CODE BEGIN SPI4_Init 1 */

  /* USER CODE END SPI4_Init 1 */
  hspi4.Instance = SPI4;
  hspi4.Init.Mode = SPI_MODE_MASTER;
  hspi4.Init.Direction = SPI_DIRECTION_2LINES;
  hspi4.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi4.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi4.Init.CLKPhase = SPI_PHASE_1EDGE;
#if SPI4_NSS_IS_USE_SOFT_CTRL
  hspi4.Init.NSS = SPI_NSS_SOFT;
#else
  hspi4.Init.NSS = SPI_NSS_HARD_OUTPUT;
#endif
  hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi4.Init.CRCPolynomial = 0x7;
  hspi4.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi4.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi4.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi4.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi4.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi4.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi4.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi4.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi4.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi4.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI4_Init 2 */

  /* USER CODE END SPI4_Init 2 */

}

/* SPI6 init function */
void MX_SPI6_Init(void)
{

  /* USER CODE BEGIN SPI6_Init 0 */

  /* USER CODE END SPI6_Init 0 */

  /* USER CODE BEGIN SPI6_Init 1 */

  /* USER CODE END SPI6_Init 1 */
  hspi6.Instance = SPI6;
  hspi6.Init.Mode = SPI_MODE_MASTER;
#if NETIF_WIFI_HALOW_IS_ENABLE
  /* ---- Halow: bidirectional SDIO-over-SPI ---- */
  hspi6.Init.Direction = SPI_DIRECTION_2LINES;
  hspi6.Init.NSS = SPI_NSS_SOFT;
  hspi6.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi6.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
#else
  /* ---- SensorExt: TX-only, hardware NSS with pulse ---- */
  hspi6.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  hspi6.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi6.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi6.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
#endif
  hspi6.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi6.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi6.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi6.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi6.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi6.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi6.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi6.Init.CRCPolynomial = 0x7;
  hspi6.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi6.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi6.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi6.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi6.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi6.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi6.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi6.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI6_Init 2 */
  if (sem_spi6 == NULL) {
    sem_spi6 = osSemaphoreNew(1, 0, NULL);
    if (sem_spi6 == NULL) {
      Error_Handler();
    }
  }
  /* USER CODE END SPI6_Init 2 */

}

void MX_SPI6_DeInit(void)
{
  /* USER CODE BEGIN SPI6_DeInit 0 */

  /* USER CODE END SPI6_DeInit 0 */

  (void)HAL_SPI_Abort(&hspi6);
  if (HAL_SPI_DeInit(&hspi6) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN SPI6_DeInit 1 */
  if (sem_spi6 != NULL) {
    osSemaphoreDelete(sem_spi6);
    sem_spi6 = NULL;
  }
  /* USER CODE END SPI6_DeInit 1 */
}

void HAL_SPI_MspInit(SPI_HandleTypeDef* spiHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  DMA_IsolationConfigTypeDef IsolationConfiginput= {0};
  DMA_DataHandlingConfTypeDef DataHandlingConfig = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(spiHandle->Instance==SPI2)
  {
  /* USER CODE BEGIN SPI2_MspInit 0 */

  /* USER CODE END SPI2_MspInit 0 */

  /** Initializes the peripherals clock
  */
    RCC_IC_FillSPI2_PLL_IC8(&PeriphClkInitStruct);
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SPI2 clock enable */
    __HAL_RCC_SPI2_CLK_ENABLE();

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SPI2 GPIO Configuration
    PD6     ------> SPI2_MISO
    PD2     ------> SPI2_MOSI
    PF2     ------> SPI2_SCK
    PB12     ------> SPI2_NSS
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    // GPIO_InitStruct.Pin = GPIO_PIN_12;
    // GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    // GPIO_InitStruct.Pull = GPIO_PULLUP;
    // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    // GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    // HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
#if 1
    /* SPI2 DMA Init */
    /* HPDMA1_REQUEST_SPI2_RX Init */
    handle_HPDMA1_Channel5.Instance = HPDMA1_Channel5;
    handle_HPDMA1_Channel5.Init.Request = HPDMA1_REQUEST_SPI2_RX;
    handle_HPDMA1_Channel5.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel5.Init.Direction = DMA_PERIPH_TO_MEMORY;
    handle_HPDMA1_Channel5.Init.SrcInc = DMA_SINC_FIXED;
    handle_HPDMA1_Channel5.Init.DestInc = DMA_DINC_INCREMENTED;
    handle_HPDMA1_Channel5.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel5.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel5.Init.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    handle_HPDMA1_Channel5.Init.SrcBurstLength = 1;
    handle_HPDMA1_Channel5.Init.DestBurstLength = 1;
    handle_HPDMA1_Channel5.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1|DMA_DEST_ALLOCATED_PORT0;
    handle_HPDMA1_Channel5.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel5.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel5) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_HPDMA1_Channel5, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(spiHandle, hdmarx, handle_HPDMA1_Channel5);

    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel5, DMA_CHANNEL_PRIV|DMA_CHANNEL_SEC
                              |DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel5, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    /* HPDMA1_REQUEST_SPI2_TX Init */
    handle_HPDMA1_Channel4.Instance = HPDMA1_Channel4;
    handle_HPDMA1_Channel4.Init.Request = HPDMA1_REQUEST_SPI2_TX;
    handle_HPDMA1_Channel4.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel4.Init.Direction = DMA_MEMORY_TO_PERIPH;
    handle_HPDMA1_Channel4.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle_HPDMA1_Channel4.Init.DestInc = DMA_DINC_FIXED;
    handle_HPDMA1_Channel4.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel4.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel4.Init.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    handle_HPDMA1_Channel4.Init.SrcBurstLength = 1;
    handle_HPDMA1_Channel4.Init.DestBurstLength = 1;
    handle_HPDMA1_Channel4.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT1;
    handle_HPDMA1_Channel4.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel4.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel4) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_HPDMA1_Channel4, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(spiHandle, hdmatx, handle_HPDMA1_Channel4);

    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel4, DMA_CHANNEL_PRIV|DMA_CHANNEL_SEC
                              |DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel4, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }
#else
    /* SPI2 DMA Init */
    /* GPDMA1_REQUEST_SPI2_RX Init */
    handle_GPDMA1_Channel11.Instance = GPDMA1_Channel11;
    handle_GPDMA1_Channel11.Init.Request = GPDMA1_REQUEST_SPI2_RX;
    handle_GPDMA1_Channel11.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_GPDMA1_Channel11.Init.Direction = DMA_PERIPH_TO_MEMORY;
    handle_GPDMA1_Channel11.Init.SrcInc = DMA_SINC_FIXED;
    handle_GPDMA1_Channel11.Init.DestInc = DMA_DINC_INCREMENTED;
    handle_GPDMA1_Channel11.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel11.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel11.Init.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    handle_GPDMA1_Channel11.Init.SrcBurstLength = 1;
    handle_GPDMA1_Channel11.Init.DestBurstLength = 1;
    handle_GPDMA1_Channel11.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT1;
    handle_GPDMA1_Channel11.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_GPDMA1_Channel11.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_GPDMA1_Channel11) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_GPDMA1_Channel11, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_GPDMA1_Channel11, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle, hdmarx, handle_GPDMA1_Channel11);

    /* set GPDMA1 channel 11 used by SPI2 */
    if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel11,DMA_CHANNEL_SEC|DMA_CHANNEL_PRIV|DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC)!= HAL_OK )
    {
      Error_Handler();
    }

    /* GPDMA1_REQUEST_SPI2_TX Init */
    handle_GPDMA1_Channel10.Instance = GPDMA1_Channel10;
    handle_GPDMA1_Channel10.Init.Request = GPDMA1_REQUEST_SPI2_TX;
    handle_GPDMA1_Channel10.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_GPDMA1_Channel10.Init.Direction = DMA_MEMORY_TO_PERIPH;
    handle_GPDMA1_Channel10.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle_GPDMA1_Channel10.Init.DestInc = DMA_DINC_FIXED;
    handle_GPDMA1_Channel10.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel10.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel10.Init.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    handle_GPDMA1_Channel10.Init.SrcBurstLength = 1;
    handle_GPDMA1_Channel10.Init.DestBurstLength = 1;
    handle_GPDMA1_Channel10.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1|DMA_DEST_ALLOCATED_PORT0;
    handle_GPDMA1_Channel10.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_GPDMA1_Channel10.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_GPDMA1_Channel10) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_GPDMA1_Channel10, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_GPDMA1_Channel10, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle, hdmatx, handle_GPDMA1_Channel10);

    /* set GPDMA1 channel 10 used by SPI2 */
    if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel10,DMA_CHANNEL_SEC|DMA_CHANNEL_PRIV|DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC)!= HAL_OK )
    {
      Error_Handler();
    }

#endif
    /* SPI2 interrupt Init */
    HAL_NVIC_SetPriority(SPI2_IRQn, 8, 0);
    HAL_NVIC_EnableIRQ(SPI2_IRQn);
  /* USER CODE BEGIN SPI2_MspInit 1 */

  /* USER CODE END SPI2_MspInit 1 */
  } else if(spiHandle->Instance==SPI4)
  {
  /* USER CODE BEGIN SPI4_MspInit 0 */

  /* USER CODE END SPI4_MspInit 0 */

  /** Initializes the peripherals clock
  */
    RCC_IC_FillSPI4_PLL_IC9(&PeriphClkInitStruct);
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SPI4 clock enable */
    __HAL_RCC_SPI4_CLK_ENABLE();

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SPI4 GPIO Configuration
    PE11     ------> SPI4_NSS
    PE12     ------> SPI4_SCK
    PB6     ------> SPI4_MISO
    PB7     ------> SPI4_MOSI
    */
#if SPI4_NSS_IS_USE_SOFT_CTRL
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    SPI4_NSS_HIGH();
#else
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
#endif
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

#if 0
    /* SPI4 DMA Init */
    /* HPDMA1_REQUEST_SPI4_RX Init */
    handle_HPDMA1_Channel3.Instance = HPDMA1_Channel3;
    handle_HPDMA1_Channel3.Init.Request = HPDMA1_REQUEST_SPI4_RX;
    handle_HPDMA1_Channel3.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel3.Init.Direction = DMA_PERIPH_TO_MEMORY;
    handle_HPDMA1_Channel3.Init.SrcInc = DMA_SINC_FIXED;
    handle_HPDMA1_Channel3.Init.DestInc = DMA_DINC_INCREMENTED;
    handle_HPDMA1_Channel3.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel3.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel3.Init.Priority = DMA_HIGH_PRIORITY;
    handle_HPDMA1_Channel3.Init.SrcBurstLength = 1;
    handle_HPDMA1_Channel3.Init.DestBurstLength = 1;
    handle_HPDMA1_Channel3.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
    handle_HPDMA1_Channel3.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel3.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel3) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_HPDMA1_Channel3, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(spiHandle, hdmarx, handle_HPDMA1_Channel3);

    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel3, DMA_CHANNEL_PRIV|DMA_CHANNEL_SEC
                              |DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel3, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    /* HPDMA1_REQUEST_SPI4_TX Init */
    handle_HPDMA1_Channel2.Instance = HPDMA1_Channel2;
    handle_HPDMA1_Channel2.Init.Request = HPDMA1_REQUEST_SPI4_TX;
    handle_HPDMA1_Channel2.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel2.Init.Direction = DMA_MEMORY_TO_PERIPH;
    handle_HPDMA1_Channel2.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle_HPDMA1_Channel2.Init.DestInc = DMA_DINC_FIXED;
    handle_HPDMA1_Channel2.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel2.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel2.Init.Priority = DMA_HIGH_PRIORITY;
    handle_HPDMA1_Channel2.Init.SrcBurstLength = 1;
    handle_HPDMA1_Channel2.Init.DestBurstLength = 1;
    handle_HPDMA1_Channel2.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
    handle_HPDMA1_Channel2.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel2.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel2) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_HPDMA1_Channel2, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(spiHandle, hdmatx, handle_HPDMA1_Channel2);

    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel2, DMA_CHANNEL_PRIV|DMA_CHANNEL_SEC
                              |DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel2, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }
#else
    /* SPI4 DMA Init */
    /* GPDMA1_REQUEST_SPI4_RX Init */
    handle_GPDMA1_Channel9.Instance = GPDMA1_Channel9;
    handle_GPDMA1_Channel9.Init.Request = GPDMA1_REQUEST_SPI4_RX;
    handle_GPDMA1_Channel9.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_GPDMA1_Channel9.Init.Direction = DMA_PERIPH_TO_MEMORY;
    handle_GPDMA1_Channel9.Init.SrcInc = DMA_SINC_FIXED;
    handle_GPDMA1_Channel9.Init.DestInc = DMA_DINC_INCREMENTED;
    handle_GPDMA1_Channel9.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel9.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel9.Init.Priority = DMA_HIGH_PRIORITY;
    handle_GPDMA1_Channel9.Init.SrcBurstLength = 1;
    handle_GPDMA1_Channel9.Init.DestBurstLength = 1;
    handle_GPDMA1_Channel9.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1|DMA_DEST_ALLOCATED_PORT0;
    handle_GPDMA1_Channel9.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_GPDMA1_Channel9.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_GPDMA1_Channel9) != HAL_OK)
    {
      Error_Handler();
    }
    
    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_GPDMA1_Channel9, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_GPDMA1_Channel9, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle, hdmarx, handle_GPDMA1_Channel9);

    /* set GPDMA1 channel 9 used by SPI4 */
    if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel9,DMA_CHANNEL_SEC|DMA_CHANNEL_PRIV|DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC)!= HAL_OK )
    {
      Error_Handler();
    }

    /* GPDMA1_REQUEST_SPI4_TX Init */
    handle_GPDMA1_Channel8.Instance = GPDMA1_Channel8;
    handle_GPDMA1_Channel8.Init.Request = GPDMA1_REQUEST_SPI4_TX;
    handle_GPDMA1_Channel8.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_GPDMA1_Channel8.Init.Direction = DMA_MEMORY_TO_PERIPH;
    handle_GPDMA1_Channel8.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle_GPDMA1_Channel8.Init.DestInc = DMA_DINC_FIXED;
    handle_GPDMA1_Channel8.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel8.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_GPDMA1_Channel8.Init.Priority = DMA_HIGH_PRIORITY;
    handle_GPDMA1_Channel8.Init.SrcBurstLength = 1;
    handle_GPDMA1_Channel8.Init.DestBurstLength = 1;
    handle_GPDMA1_Channel8.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT1;
    handle_GPDMA1_Channel8.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_GPDMA1_Channel8.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_GPDMA1_Channel8) != HAL_OK)
    {
      Error_Handler();
    }

    DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    if (HAL_DMAEx_ConfigDataHandling(&handle_GPDMA1_Channel8, &DataHandlingConfig) != HAL_OK)
    {
      Error_Handler();
    }

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_GPDMA1_Channel8, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle, hdmatx, handle_GPDMA1_Channel8);

    /* set GPDMA1 channel 8 used by SPI4 */
    if (HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel8,DMA_CHANNEL_SEC|DMA_CHANNEL_PRIV|DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC)!= HAL_OK )
    {
      Error_Handler();
    }

#endif
    /* SPI4 interrupt Init */
    HAL_NVIC_SetPriority(SPI4_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(SPI4_IRQn);
  /* USER CODE BEGIN SPI4_MspInit 1 */

  /* USER CODE END SPI4_MspInit 1 */
  }
  else if(spiHandle->Instance==SPI6)
  {
    /* USER CODE BEGIN SPI6_MspInit 0 */

    /* USER CODE END SPI6_MspInit 0 */

    /** Initializes the peripherals clock
    */
#if NETIF_WIFI_HALOW_IS_ENABLE
    RCC_IC_FillSPI6_PLL_IC9(&PeriphClkInitStruct);
#else
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI6;
    PeriphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_CLKP;
#endif
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SPI6 clock enable */
    __HAL_RCC_SPI6_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();

#if NETIF_WIFI_HALOW_IS_ENABLE
	  __HAL_RCC_GPIOB_CLK_ENABLE();

    /**SPI6 GPIO Configuration
    PA5     ------> SPI6_SCK
    PA0     ------> SPI6_NSS
    PA7     ------> SPI6_MOSI
    PB4     ------> SPI6_MISO
    */
    GPIO_InitStruct.Pin = MM_HALOW_SPI_CLK_Pin | MM_HALOW_SPI_MOSI_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_SPI6;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = MM_HALOW_SPI_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(MM_HALOW_SPI_CS_GPIO_Port, &GPIO_InitStruct);
	  HAL_GPIO_WritePin(MM_HALOW_SPI_CS_GPIO_Port, MM_HALOW_SPI_CS_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = MM_HALOW_SPI_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_SPI6;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
#else
    /**SPI6 GPIO Configuration (SensorExt — TX only, hardware NSS)
    PA5     ------> SPI6_SCK
    PA0     ------> SPI6_NSS
    PA7     ------> SPI6_MOSI
    */
    GPIO_InitStruct.Pin = TFT_CLK_Pin | TFT_MOSI_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_SPI6;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = TFT_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI6;
    HAL_GPIO_Init(TFT_CS_GPIO_Port, &GPIO_InitStruct);
#endif

    /* SPI6 DMA Init */
    /* HPDMA1_REQUEST_SPI6_RX Init */
    handle_HPDMA1_Channel9.Instance = HPDMA1_Channel9;
    handle_HPDMA1_Channel9.Init.Request = HPDMA1_REQUEST_SPI6_RX;
    handle_HPDMA1_Channel9.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel9.Init.Direction = DMA_PERIPH_TO_MEMORY;
    handle_HPDMA1_Channel9.Init.SrcInc = DMA_SINC_FIXED;
    handle_HPDMA1_Channel9.Init.DestInc = DMA_DINC_INCREMENTED;
    handle_HPDMA1_Channel9.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel9.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel9.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    handle_HPDMA1_Channel9.Init.SrcBurstLength = 1;
    handle_HPDMA1_Channel9.Init.DestBurstLength = 1;
    handle_HPDMA1_Channel9.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1|DMA_DEST_ALLOCATED_PORT0;
    handle_HPDMA1_Channel9.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel9.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel9) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle, hdmarx, handle_HPDMA1_Channel9);

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel9, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    /* set GPDMA1 channel 9 used by SPI6 */
    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel9,DMA_CHANNEL_SEC|DMA_CHANNEL_PRIV|DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC)!= HAL_OK )
    {
      Error_Handler();
    }

    /* HPDMA1_REQUEST_SPI6_TX Init */
    handle_HPDMA1_Channel8.Instance = HPDMA1_Channel8;
    handle_HPDMA1_Channel8.Init.Request = HPDMA1_REQUEST_SPI6_TX;
    handle_HPDMA1_Channel8.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel8.Init.Direction = DMA_MEMORY_TO_PERIPH;
    handle_HPDMA1_Channel8.Init.SrcInc = DMA_SINC_INCREMENTED;
    handle_HPDMA1_Channel8.Init.DestInc = DMA_DINC_FIXED;
    handle_HPDMA1_Channel8.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel8.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    handle_HPDMA1_Channel8.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    handle_HPDMA1_Channel8.Init.SrcBurstLength = 1;
    handle_HPDMA1_Channel8.Init.DestBurstLength = 1;
    handle_HPDMA1_Channel8.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT1;
    handle_HPDMA1_Channel8.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel8.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel8) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle, hdmatx, handle_HPDMA1_Channel8);

    IsolationConfiginput.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel8, &IsolationConfiginput) != HAL_OK)
    {
      Error_Handler();
    }

    /* set GPDMA1 channel 8 used by SPI6 */
    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel8,DMA_CHANNEL_SEC|DMA_CHANNEL_PRIV|DMA_CHANNEL_SRC_SEC|DMA_CHANNEL_DEST_SEC)!= HAL_OK )
    {
      Error_Handler();
    }

    /* SPI6 interrupt Init */
    HAL_NVIC_SetPriority(SPI6_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SPI6_IRQn);
    /* USER CODE BEGIN SPI6_MspInit 1 */

    /* USER CODE END SPI6_MspInit 1 */
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef* spiHandle)
{

  if(spiHandle->Instance==SPI2)
  {
  /* USER CODE BEGIN SPI2_MspDeInit 0 */

  /* USER CODE END SPI2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI2_CLK_DISABLE();

    /**SPI2 GPIO Configuration
    PD6     ------> SPI2_MISO
    PD2     ------> SPI2_MOSI
    PF2     ------> SPI2_SCK
    PB12     ------> SPI2_NSS
    */
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_6|GPIO_PIN_2);

    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_2);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12);

    /* SPI2 DMA DeInit */
    HAL_DMA_DeInit(spiHandle->hdmarx);
    HAL_DMA_DeInit(spiHandle->hdmatx);

    /* SPI2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI2_IRQn);
  /* USER CODE BEGIN SPI2_MspDeInit 1 */
    if (sem_spi2 != NULL) {
      osSemaphoreDelete(sem_spi2);
      sem_spi2 = NULL;
    }
  /* USER CODE END SPI2_MspDeInit 1 */
  }
  else if(spiHandle->Instance==SPI4)
  {
  /* USER CODE BEGIN SPI4_MspDeInit 0 */

  /* USER CODE END SPI4_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI4_CLK_DISABLE();

    /**SPI4 GPIO Configuration
    PE11     ------> SPI4_NSS
    PE12     ------> SPI4_SCK
    PB6     ------> SPI4_MISO
    PB7     ------> SPI4_MOSI
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_11|GPIO_PIN_12);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6|GPIO_PIN_7);

    /* SPI4 DMA DeInit */
    HAL_DMA_DeInit(spiHandle->hdmarx);
    HAL_DMA_DeInit(spiHandle->hdmatx);
    /* SPI4 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI4_IRQn);
  /* USER CODE BEGIN SPI4_MspDeInit 1 */

  /* USER CODE END SPI4_MspDeInit 1 */
  }
  else if(spiHandle->Instance==SPI6)
  {
  /* USER CODE BEGIN SPI6_MspDeInit 0 */

  /* USER CODE END SPI6_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI6_CLK_DISABLE();

    /**SPI6 GPIO Configuration
    PA5     ------> SPI6_SCK
    PA0     ------> SPI6_NSS
    PA7     ------> SPI6_MOSI
    */
#if NETIF_WIFI_HALOW_IS_ENABLE
    HAL_GPIO_DeInit(GPIOA, MM_HALOW_SPI_CLK_Pin | MM_HALOW_SPI_CS_Pin | MM_HALOW_SPI_MOSI_Pin);
    HAL_GPIO_DeInit(GPIOB, MM_HALOW_SPI_MISO_Pin);
#else
    HAL_GPIO_DeInit(GPIOA, TFT_CLK_Pin | TFT_CS_Pin | TFT_MOSI_Pin);
#endif

    /* SPI6 DMA DeInit */
    HAL_DMA_DeInit(spiHandle->hdmarx);
    HAL_DMA_DeInit(spiHandle->hdmatx);

    /* SPI6 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI6_IRQn);
  /* USER CODE BEGIN SPI6_MspDeInit 1 */

  /* USER CODE END SPI6_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
extern osSemaphoreId_t sem_spi4;
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI4) {
    if (sem_spi4 != NULL) osSemaphoreRelease(sem_spi4);
  } else if (hspi->Instance == SPI2) {
    if (sem_spi2 != NULL) osSemaphoreRelease(sem_spi2);
  } else if (hspi->Instance == SPI6) {
#if NETIF_WIFI_HALOW_IS_ENABLE
    extern void mmhal_wlan_hal_dma_complete_from_isr(void);
    mmhal_wlan_hal_dma_complete_from_isr();
#endif
    if (sem_spi6 != NULL) {
      osSemaphoreRelease(sem_spi6);
    }
  }
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI6 && sem_spi6 != NULL) {
    osSemaphoreRelease(sem_spi6);
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI4) {
    printf("SPI4 Error=%lx\r\n", hspi->ErrorCode);
  } else if (hspi->Instance == SPI2) {
    printf("SPI2 Error=%lx\r\n", hspi->ErrorCode);
  } else if (hspi->Instance == SPI6) {
    printf("SPI6 Error=%lx\r\n", hspi->ErrorCode);
#if NETIF_WIFI_HALOW_IS_ENABLE
    extern void mmhal_wlan_hal_dma_complete_from_isr(void);
    mmhal_wlan_hal_dma_complete_from_isr();
#endif
  }
}

// SPI2 read/write one byte data
uint8_t SPI2_ReadWriteByte(uint8_t wByte)
{
	HAL_StatusTypeDef status = HAL_OK;
  uint8_t rByte = 0;

	status = HAL_SPI_TransmitReceive(&hspi2, &wByte, &rByte, 1, 10);
  if (status != HAL_OK) printf("SPI2_ReadWriteByte Failed(status = %d)!\r\n", status);
  
  return rByte;
}

#include "common_utils.h"

#define SPI2_BUFFER_LENGTH       2048
#define SPI2_DMA_ENABLED         (1)
uint8_t spi2_tx_buffer[SPI2_BUFFER_LENGTH] ALIGN_32 UNCACHED;
uint8_t spi2_rx_buffer[SPI2_BUFFER_LENGTH] ALIGN_32 UNCACHED;

int spi2_transfer(void *tx_buffer, void *rx_buffer, uint16_t buffer_length, uint32_t timeout)
{
    int ret = HAL_OK;

    // if ((tx_buffer == NULL && rx_buffer == NULL) || buffer_length == 0) {
    //   return HAL_ERROR;
    // }
    if (buffer_length < 1 || buffer_length > SPI2_BUFFER_LENGTH) {
        printf("Invalid buffer length: %d\r\n", buffer_length);
        return HAL_ERROR;
    }

    if (rx_buffer == NULL) {
        // rx_buffer = tx_buffer;
        rx_buffer = spi2_rx_buffer;
    }
    if (tx_buffer == NULL) {
        // tx_buffer = rx_buffer;
        tx_buffer = spi2_tx_buffer;
    }
    
    // printf("Transmitting data: ");
    // for (uint16_t i = 0; i < buffer_length; i++) {
    //     printf("%02X ", ((uint8_t *)tx_buffer)[i]);
    // }
    // printf("\r\n");
    if (buffer_length < 8) {
        ret = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx_buffer, (uint8_t *)rx_buffer, buffer_length, timeout);
        if (ret != HAL_OK) {
            printf("HAL_SPI_TransmitReceive failed(ret = %d)!\r\n", ret);
            return ret;
        }
    } else {
#ifdef SPI2_DMA_ENABLED
        memcpy(spi2_tx_buffer, tx_buffer, buffer_length);
        memset(spi2_rx_buffer, 0x00, buffer_length);
        // printf("Transmit\r\n");
        ret = HAL_SPI_TransmitReceive_DMA(&hspi2, (uint8_t *)spi2_tx_buffer, (uint8_t *)spi2_rx_buffer, buffer_length);
        if (ret == HAL_OK) {
            ret = osSemaphoreAcquire(sem_spi2, timeout);
            if (ret != osOK) {
                printf("sem_spi2 failed(ret = %d)!\r\n", ret);
                HAL_SPI_Abort(&hspi2);
                return HAL_ERROR;
            }
            memcpy(rx_buffer, spi2_rx_buffer, buffer_length);
        } else {
            printf("HAL_SPI_TransmitReceive_DMA failed(ret = %d)!\r\n", ret);
            return ret;
        }
#else
        HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx_buffer, (uint8_t *)rx_buffer, buffer_length, timeout);
#endif
    }
    // printf("Received data: ");
    // for (uint16_t i = 0; i < buffer_length; i++) {
    //     printf("%02X ", ((uint8_t *)rx_buffer)[i]);
    // }
    // printf("\r\n");
    return ret;
}

// SPI2 write a group of bytes data
int SPI2_WriteBytes(uint8_t *wBytes, uint16_t wLength, uint32_t timeout)
{
	// HAL_StatusTypeDef status = HAL_OK;
  int ret = 0, wlen = 0;

  do {
    wlen = (wLength > SPI2_BUFFER_LENGTH) ? SPI2_BUFFER_LENGTH : wLength;
    ret = spi2_transfer(wBytes, NULL, wlen, timeout);
    if (ret != 0) break;
    wLength -= wlen;
    wBytes += wlen;
  } while (wLength > 0);
  return ret;

  // return spi2_transfer(wBytes, NULL, wLength, timeout);

  // osSemaphoreAcquire(sem_spi2, 0);
	// status = HAL_SPI_Transmit_DMA(&hspi2, wBytes, wLength);
  // if (status != HAL_OK) {
  //   HAL_SPI_Abort(&hspi2);
  //   printf("SPI2_Transmit_DMA Failed(status = %d)!\r\n", status);
  //   return status;
  // }

  // if (osSemaphoreAcquire(sem_spi2, timeout) != osOK) {
  //   HAL_SPI_Abort(&hspi2);
  //   printf("SPI2_Transmit_DMA TIMEOUT!\r\n");
  //   return HAL_TIMEOUT;
  // }

  // return status;
}

// SPI2 read a group of bytes data
int SPI2_ReadBytes(uint8_t *rBytes, uint16_t rLength, uint32_t timeout)
{
	// HAL_StatusTypeDef status = HAL_OK;
  int ret = 0, rlen = 0;

  do {
    rlen = (rLength > SPI2_BUFFER_LENGTH) ? SPI2_BUFFER_LENGTH : rLength;
    ret = spi2_transfer(NULL, rBytes, rlen, timeout);
    if (ret != 0) break;
    rLength -= rlen;
    rBytes += rlen;
  } while (rLength > 0);
  return ret;

  // return spi2_transfer(NULL, rBytes, rLength, timeout);

  // osSemaphoreAcquire(sem_spi2, 0);
	// status = HAL_SPI_Receive_DMA(&hspi2, rBytes, rLength);
  // if (status != HAL_OK) {
  //   HAL_SPI_Abort(&hspi2);
  //   printf("SPI2_Receive_DMA Failed(status = %d)!\r\n", status);
  //   return status;
  // }

  // if (osSemaphoreAcquire(sem_spi2, timeout) != osOK) {
  //   HAL_SPI_Abort(&hspi2);
  //   printf("SPI2_Receive_DMA TIMEOUT!\r\n");
  //   return HAL_TIMEOUT;
  // }
    
  // return status;
}
/* USER CODE END 1 */

/* USER CODE BEGIN Additional_SPI6_Wrapper */

/**
 * @brief Simple blocking transmit helper for SPI6.
 *
 * This wrapper provides a consistent interface similar to SPI2 helpers,
 * but currently uses blocking HAL transfers for robustness. It is
 * intended to be used by high‑level drivers such as the ST7789VW TFT
 * driver. DMA optimization can be added later if needed.
 *
 * @param data    Pointer to TX buffer.
 * @param length  Number of bytes to transmit.
 * @param timeout Timeout in RTOS ticks or milliseconds (passed to HAL).
 * @return 0 on success, negative on error.
 */
int SPI6_WriteBytes(const uint8_t *data, uint32_t length, uint32_t timeout)
{
  HAL_StatusTypeDef status;
  uint32_t offset = 0U;

  if (data == NULL || length == 0U) {
    return 0;
  }

  while (offset < length) {
    uint32_t remaining = length - offset;
    /* HAL_SPI_Transmit size parameter is uint16_t. */
    uint16_t chunk = (remaining > 0xFFFFU) ? 0xFFFFU : (uint16_t)remaining;

    status = HAL_SPI_Transmit(&hspi6,
                              (uint8_t *)(data + offset),
                              chunk,
                              timeout);
    if (status != HAL_OK) {
      printf("SPI6_WriteBytes failed(status = %d)!\r\n", status);
      return -1;
    }

    offset += chunk;
  }

  return 0;
}

/**
 * @brief SPI6 transmit using DMA (blocking until complete or error).
 *
 * Uses HAL_SPI_Transmit_DMA and waits on sem_spi6. Intended for sensor extension board
 * bulk data (e.g. fill_rect / draw_bitmap line buffer). Buffer must
 * remain valid until the function returns.
 *
 * @param data    Pointer to TX buffer.
 * @param length  Number of bytes to transmit (max 65535 per call).
 * @param timeout Timeout in ms for semaphore wait.
 * @return 0 on success, -1 on error.
 */
int SPI6_WriteBytesDMA(const uint8_t *data, uint32_t length, uint32_t timeout)
{
  HAL_StatusTypeDef status;

  if (data == NULL || length == 0U || sem_spi6 == NULL) {
    return (length == 0U) ? 0 : -1;
  }
  if (length > 0xFFFFU) {
    return -1;
  }

  status = HAL_SPI_Transmit_DMA(&hspi6, (uint8_t *)data, (uint16_t)length);
  if (status != HAL_OK) {
    return -1;
  }
  if (osSemaphoreAcquire(sem_spi6, timeout) != osOK) {
    (void)HAL_SPI_Abort(&hspi6);
    return -1;
  }
  return 0;
}

/* USER CODE END Additional_SPI6_Wrapper */
