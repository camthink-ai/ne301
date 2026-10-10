/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   This file provides code for the configuration
  *          of the IWDG instances.
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
#include "iwdg.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

IWDG_HandleTypeDef hiwdg;

/* IWDG init function */
void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Window = 2048;
  hiwdg.Init.Reload = 2048;       // 32kHz / 256 = 128Hz -> 7.8125ms
                                  // 2048 * 7.8125ms = 16s
  hiwdg.Init.EWI = 32; /* EWI comparator: fires ~250ms before the reset -
                       * freeze forensics dump (see HAL_IWDG_EarlyWakeupCallback) */
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */
  HAL_NVIC_SetPriority(IWDG_IRQn, 0, 0); /* highest: must preempt a storm */
  HAL_NVIC_EnableIRQ(IWDG_IRQn);
  /* USER CODE END IWDG_Init 2 */

}

/* USER CODE BEGIN 1 */

/* Freeze forensics: the overnight soak hit three total-scheduler-death
 * freezes (22-27s of silence incl. the 16s wake heartbeat, then IWDG).
 * The EWI fires one counter period (~16s) before the reset: dump the
 * currently running thread and every active NVIC interrupt so the next
 * occurrence names the wedging context. Runs in ISR context - printf is
 * acceptable (worst case it deadlocks with the already-frozen logger and
 * the reset still comes). */
#include "stm32n6xx_hal.h"
#include "tx_api.h"

void HAL_IWDG_EarlyWakeupCallback(IWDG_HandleTypeDef *hiwdg_p)
{
    (void)hiwdg_p;
    TX_THREAD *th = tx_thread_identify();
    printf("\r\n[IWDG-EWI] freeze detected! thread=%s\r\n",
           (th != NULL) ? th->tx_thread_name : "(handler/idle)");
    printf("[IWDG-EWI] ICSR=%08lx VECTACTIVE=%lu\r\n",
           (unsigned long)SCB->ICSR,
           (unsigned long)((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) >> SCB_ICSR_VECTACTIVE_Pos));
    for (int i = 0; i < 3; i++) {
        /* IRQn 0..95 pending state: which interrupt is storming/hogging */
        printf("[IWDG-EWI] ISPR%d=%08lx\r\n", i,
               (unsigned long)NVIC->ISPR[i]);
    }
}

/* USER CODE END 1 */
