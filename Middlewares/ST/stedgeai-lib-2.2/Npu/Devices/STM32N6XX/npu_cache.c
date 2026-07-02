/**
  ******************************************************************************
  * @file    npu_cache.c
  * @brief   Implementation of NPU-cache-handling functions (CACHEAXI)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "npu_cache.h"
#include "stm32n6xx_hal_cacheaxi.h"

static CACHEAXI_HandleTypeDef hcacheaxi_s;

void npu_cache_init(void)
{
  if (hcacheaxi_s.Instance != CACHEAXI) {
    hcacheaxi_s.Instance = CACHEAXI;
    HAL_CACHEAXI_Init(&hcacheaxi_s);
  }
}

void npu_cache_enable(void)
{
  HAL_StatusTypeDef status;

  npu_cache_init();
  do
  {
    status = HAL_CACHEAXI_Enable(&hcacheaxi_s);
  } while (status == HAL_BUSY);
}

void npu_cache_disable(void)
{
  HAL_StatusTypeDef status;

  if (hcacheaxi_s.Instance != CACHEAXI) {
    return;
  }

  do
  {
    status = HAL_CACHEAXI_Disable(&hcacheaxi_s);
  } while (status == HAL_BUSY);

  HAL_CACHEAXI_DeInit(&hcacheaxi_s);
  hcacheaxi_s.Instance = NULL;
}

void npu_cache_invalidate(void)
{
  if (hcacheaxi_s.Instance == CACHEAXI) {
    HAL_CACHEAXI_Invalidate(&hcacheaxi_s);
  }
}

void npu_cache_clean_range(uint32_t start_addr, uint32_t end_addr)
{
  if (hcacheaxi_s.Instance == CACHEAXI && start_addr < end_addr) {
    HAL_CACHEAXI_CleanByAddr(&hcacheaxi_s, (uint32_t *)start_addr, end_addr - start_addr);
  }
}

void npu_cache_clean_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
  if (hcacheaxi_s.Instance == CACHEAXI && start_addr < end_addr) {
    HAL_CACHEAXI_CleanInvalidByAddr(&hcacheaxi_s, (uint32_t *)start_addr, end_addr - start_addr);
  }
}

void HAL_CACHEAXI_MspInit(CACHEAXI_HandleTypeDef *hcacheaxi)
{
  (void)hcacheaxi;
  npu_cache_enable_clocks_and_reset();
}

void HAL_CACHEAXI_MspDeInit(CACHEAXI_HandleTypeDef *hcacheaxi)
{
  (void)hcacheaxi;
  npu_cache_disable_clocks_and_reset();
}

__weak void npu_cache_enable_clocks_and_reset(void) {}
__weak void npu_cache_disable_clocks_and_reset(void) {}

void NPU_CACHE_IRQHandler(void)
{
  __NOP();
}
