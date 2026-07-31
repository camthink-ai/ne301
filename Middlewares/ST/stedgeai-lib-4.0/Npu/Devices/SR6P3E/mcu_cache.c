/**
  ******************************************************************************
  * @file    mcu_cache.c
  * @brief   Implementation of MCU cache-handling functions
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

#include "mcu_cache.h"
#include "cache.h"
#include <cache_conf.h>

static inline uint32_t mcu_cache_enabled(void) {
  /* On sr6p3e the caches (I/D) are enabled by using DCACHE_EL1_EN and ICACHE_EL1_EN macros in EL1 mode */
#if defined (DCACHE_EL1_EN) && (DCACHE_EL1_EN != 0U)
  return 1U;
#else
  return 0U;
#endif
}

void mcu_cache_invalidate(void) {
  if(mcu_cache_enabled() != 0U) {
	invalidate_dcache();
  }
}

void mcu_cache_clean(void) {
  if(mcu_cache_enabled() != 0U) {
	clean_dcache();
  }
}

void mcu_cache_clean_invalidate(void) {
  if(mcu_cache_enabled() != 0U) {
	flush_dcache();
  }
}

void mcu_cache_invalidate_range(uint32_t start_addr, uint32_t end_addr) {
  if(mcu_cache_enabled() != 0U) {
    invalidate_dcache_range((uint32_t)start_addr, (uint32_t)(end_addr - start_addr));
  }
}

void mcu_cache_clean_range(uint32_t start_addr, uint32_t end_addr) {
  if(mcu_cache_enabled() != 0U) {
    clean_dcache_range((uint32_t)start_addr, (uint32_t)(end_addr - start_addr));
  }
}

void mcu_cache_clean_invalidate_range(uint32_t start_addr, uint32_t end_addr) {
  if(mcu_cache_enabled() != 0U) {
    flush_dcache_range((uint32_t)start_addr, (uint32_t)(end_addr - start_addr));
  }
}
