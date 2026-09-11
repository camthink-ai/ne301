/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32n6xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "main.h"
#include "stm32n6xx_it.h"
#include "netif_manager.h"
#include "cmw_camera.h"
#include "debug.h"
#include "usb_otg.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "spi.h"
#include "i2s.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Fault-frame dump shared by HardFault/MemManage handlers.
 *
 * Both parameters arrive from the naked handler wrapper: at naked entry LR
 * still holds the real EXC_RETURN and SP still points at the top of the
 * hardware-stacked fault frame. Every C-level way of getting them after the
 * fact is broken -- `__asm("lr")` in the handler reads a clobbered code
 * address, scanning the handler stack finds the scanner's own locals first
 * (isr-mode test: deterministic false 0xfffffffd), and once the C bodies run,
 * their prologues have pushed frames BELOW the hardware frame so __get_MSP()
 * no longer lands on it either (isr-mode test: printed its own locals).
 *
 * Frame location: thread-mode fault (EXC_RETURN bit2 set) stacked onto PSP,
 * which nothing else touches -> frame at [PSP]. Handler-mode fault stacked
 * onto MSP at SP-at-entry -> frame at [entry_sp]. Dump that frame plus the
 * other stack's frame as [alt] so a bad frame still leaves the real one. */
static void fault_dump_frames(uint32_t exc_return, uint32_t entry_sp)
{
  uint32_t psp = __get_PSP();
  uint32_t *sps[2];
  int i;

  sps[0] = (uint32_t *)((exc_return & 0x4u) ? psp : entry_sp);
  sps[1] = (uint32_t *)((exc_return & 0x4u) ? entry_sp : psp);
  printf("  EXC_RETURN=0x%08lx (MSP=0x%08lx PSP=0x%08lx)\r\n",
         exc_return, __get_MSP(), psp);

  for (i = 0; i < 2; i++) {
    volatile uint32_t *sp = (volatile uint32_t *)sps[i];
    const char *tag = i ? "  [alt]" : "";
    int j;
    printf("%s SP=0x%08lx PC=0x%08lx LR=0x%08lx\r\n", tag, (uint32_t)sp, sp[6], sp[5]);
    printf("%s R0=0x%08lx R1=0x%08lx R2=0x%08lx R3=0x%08lx\r\n",
           tag, sp[0], sp[1], sp[2], sp[3]);
    /* Walk the stack for OCTOSPI-region (0x90xxxxxx) return addresses so
     * addr2line can resolve frames above the stacked PC/LR. */
    printf("%s BT:", tag);
    for (j = 8; j < (i ? 64 : 256); j++) {
      uint32_t v = sp[j];
      if ((v >> 24) == 0x90u) printf(" 0x%08lx", (unsigned long)(v & ~1u));
    }
    printf("\r\n");
  }
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef handle_HPDMA1_Channel7;
extern DMA_HandleTypeDef handle_HPDMA1_Channel6;
extern DMA_HandleTypeDef handle_HPDMA1_Channel1;
extern DMA_HandleTypeDef handle_HPDMA1_Channel0;
extern JPEG_HandleTypeDef hjpeg;
extern DMA_NodeTypeDef Node_GPDMA1_Channel6;
extern DMA_QListTypeDef List_GPDMA1_Channel6;
extern DMA_HandleTypeDef handle_GPDMA1_Channel6;
extern DMA_NodeTypeDef Node_GPDMA1_Channel5;
extern DMA_QListTypeDef List_GPDMA1_Channel5;
extern DMA_HandleTypeDef handle_GPDMA1_Channel5;
extern SAI_HandleTypeDef hsai_BlockA1;
extern SAI_HandleTypeDef hsai_BlockB1;
extern SD_HandleTypeDef hsd1;
extern DMA_HandleTypeDef handle_HPDMA1_Channel5;
extern DMA_HandleTypeDef handle_HPDMA1_Channel4;
extern DMA_HandleTypeDef handle_HPDMA1_Channel3;
extern DMA_HandleTypeDef handle_HPDMA1_Channel2;
extern DMA_HandleTypeDef handle_GPDMA1_Channel8;
extern DMA_HandleTypeDef handle_GPDMA1_Channel9;
extern DMA_HandleTypeDef handle_GPDMA1_Channel10;
extern DMA_HandleTypeDef handle_GPDMA1_Channel11;

extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi4;
extern DMA_HandleTypeDef handle_GPDMA1_Channel4;
extern DMA_HandleTypeDef handle_GPDMA1_Channel3;
extern DMA_HandleTypeDef handle_GPDMA1_Channel2;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
extern UART_HandleTypeDef huart7;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart9;
extern RTC_HandleTypeDef hrtc;
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
extern PCD_HandleTypeDef usbx_pcd_handle;
#else
#ifdef UX_HCD_ECM_USE_USB_OTG_HS1
extern HCD_HandleTypeDef hhcd_USB_OTG_HS1;
#else
extern PCD_HandleTypeDef hpcd_USB_OTG_HS1;
#endif
#endif
extern HCD_HandleTypeDef hhcd_USB_OTG_HS2;
extern XSPI_HandleTypeDef hxspi1;
extern XSPI_HandleTypeDef hxspi2;

extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi6;
extern DMA_HandleTypeDef handle_HPDMA1_Channel11;
extern DMA_HandleTypeDef handle_HPDMA1_Channel10;
extern DMA_HandleTypeDef handle_HPDMA1_Channel9;
extern DMA_HandleTypeDef handle_HPDMA1_Channel8;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
  printf("NMI_Handler\r\n");
  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

__attribute__((noreturn)) static void hardfault_body(uint32_t exc_return, uint32_t entry_sp)
{
  fault_dump_frames(exc_return, entry_sp);
  printf("HardFault CFSR=0x%08lx BFAR=0x%08lx MMFAR=0x%08lx HFSR=0x%08lx\r\n",
         SCB->CFSR, SCB->BFAR, SCB->MMFAR, SCB->HFSR);
  for (;;) { }
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  /* EXC_RETURN (LR) and fault-frame-top (SP) still live at naked entry --
   * pass them on before the C prologue can clobber them (fault_dump_frames). */
  __asm volatile ("mov r0, lr\n"
                  "mov r1, sp\n"
                  "b hardfault_body");
}

__attribute__((noreturn)) static void memmanage_body(uint32_t exc_return, uint32_t entry_sp)
{
  volatile uint32_t cfsr  = SCB->CFSR;
  volatile uint32_t mmfar = SCB->MMFAR;
  uint32_t mmfsr = cfsr & 0xFFU;   /* low byte of CFSR = MMFSR */
  int stacking_ok = ((mmfsr & ((1U << 4) | (1U << 3))) == 0U);

  if (stacking_ok)
    fault_dump_frames(exc_return, entry_sp);

  printf("MemManage CFSR=0x%08lx MMFAR=0x%08lx\r\n", cfsr, mmfar);

  /* MSP stack-guard (region 1, [&_sstack, &_sstack+0x100)): a faulting access
   * whose address lands in this band is the sentinel catching MSP overflow
   * into the newlib heap. Needs MMARVALID (MMFSR bit7) for a meaningful MMFAR. */
  extern uint32_t _sstack;
  if ((mmfsr & (1U << 7)) &&
      mmfar >= (uint32_t)&_sstack && mmfar < (uint32_t)&_sstack + 0x100U)
    printf("  *** MSP STACK GUARD TRIP (overflow into heap) ***\r\n");

  /* Stacked frame is valid unless a stacking fault (MSTKERR/MUNSTKERR) means it
   * was never stored. PRIV_RO keeps reads of the guard band non-faulting. */
  if (!stacking_ok)
    printf("  (stacking fault: stacked frame unreliable)\r\n");

  for (;;) { }
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
  /* EXC_RETURN (LR) and fault-frame-top (SP) still live at naked entry --
   * pass them on before the C prologue can clobber them (fault_dump_frames). */
  __asm volatile ("mov r0, lr\n"
                  "mov r1, sp\n"
                  "b memmanage_body");
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  printf("BusFault_Handler\r\n");
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

// /**
//   * @brief This function handles Undefined instruction or illegal state.
//   */
// void UsageFault_Handler(void)
// {
//   /* USER CODE BEGIN UsageFault_IRQn 0 */
//   printf("UsageFault_Handler\r\n");
//   /* USER CODE END UsageFault_IRQn 0 */
//   while (1)
//   {
//     /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
//     /* USER CODE END W1_UsageFault_IRQn 0 */
//   }
// }

/**
  * @brief This function handles Secure fault.
  */
void SecureFault_Handler(void)
{
  /* USER CODE BEGIN SecureFault_IRQn 0 */
  printf("SecureFault_Handler\r\n");
  /* USER CODE END SecureFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_SecureFault_IRQn 0 */
    /* USER CODE END W1_SecureFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32N6xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32n6xx.s).                    */
/******************************************************************************/
/**
  * @brief This function handles HPDMA1 Channel 6 global interrupt.
  */
void HPDMA1_Channel6_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel6_IRQn 0 */

  /* USER CODE END HPDMA1_Channel6_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel6);
  /* USER CODE BEGIN HPDMA1_Channel6_IRQn 1 */

  /* USER CODE END HPDMA1_Channel6_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 7 global interrupt.
  */
void HPDMA1_Channel7_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel7_IRQn 0 */

  /* USER CODE END HPDMA1_Channel7_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel7);
  /* USER CODE BEGIN HPDMA1_Channel7_IRQn 1 */

  /* USER CODE END HPDMA1_Channel7_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 8 global interrupt.
  */
void HPDMA1_Channel8_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel8_IRQn 0 */

  /* USER CODE END HPDMA1_Channel8_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel8);
  /* USER CODE BEGIN HPDMA1_Channel8_IRQn 1 */

  /* USER CODE END HPDMA1_Channel8_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 9 global interrupt.
  */
void HPDMA1_Channel9_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel9_IRQn 0 */

  /* USER CODE END HPDMA1_Channel9_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel9);
  /* USER CODE BEGIN HPDMA1_Channel9_IRQn 1 */

  /* USER CODE END HPDMA1_Channel9_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 10 global interrupt.
  */
void HPDMA1_Channel10_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel10_IRQn 0 */

  /* USER CODE END HPDMA1_Channel10_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel10);
  /* USER CODE BEGIN HPDMA1_Channel10_IRQn 1 */

  /* USER CODE END HPDMA1_Channel10_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 11 global interrupt.
  */
void HPDMA1_Channel11_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel11_IRQn 0 */

  /* USER CODE END HPDMA1_Channel11_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel11);
  /* USER CODE BEGIN HPDMA1_Channel11_IRQn 1 */

  /* USER CODE END HPDMA1_Channel11_IRQn 1 */
}

/**
  * @brief This function handles JPEG global interrupt.
  */
void JPEG_IRQHandler(void)
{
  /* USER CODE BEGIN JPEG_IRQn 0 */

  /* USER CODE END JPEG_IRQn 0 */
  HAL_JPEG_IRQHandler(&hjpeg);
  /* USER CODE BEGIN JPEG_IRQn 1 */

  /* USER CODE END JPEG_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 4 global interrupt.
  */
void HPDMA1_Channel4_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel4_IRQn 0 */

  /* USER CODE END HPDMA1_Channel4_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel4);
  /* USER CODE BEGIN HPDMA1_Channel4_IRQn 1 */

  /* USER CODE END HPDMA1_Channel4_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 5 global interrupt.
  */
void HPDMA1_Channel5_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel5_IRQn 0 */

  /* USER CODE END HPDMA1_Channel5_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel5);
  /* USER CODE BEGIN HPDMA1_Channel5_IRQn 1 */

  /* USER CODE END HPDMA1_Channel5_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 2 global interrupt.
  */
void HPDMA1_Channel2_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel2_IRQn 0 */

  /* USER CODE END HPDMA1_Channel2_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel2);
  /* USER CODE BEGIN HPDMA1_Channel2_IRQn 1 */

  /* USER CODE END HPDMA1_Channel2_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 3 global interrupt.
  */
void HPDMA1_Channel3_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel3_IRQn 0 */

  /* USER CODE END HPDMA1_Channel3_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel3);
  /* USER CODE BEGIN HPDMA1_Channel3_IRQn 1 */

  /* USER CODE END HPDMA1_Channel3_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 10 global interrupt.
  */
void GPDMA1_Channel10_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel10_IRQn 0 */

  /* USER CODE END GPDMA1_Channel10_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel10);
  /* USER CODE BEGIN GPDMA1_Channel10_IRQn 1 */

  /* USER CODE END GPDMA1_Channel10_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 11 global interrupt.
  */
void GPDMA1_Channel11_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel11_IRQn 0 */

  /* USER CODE END GPDMA1_Channel11_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel11);
  /* USER CODE BEGIN GPDMA1_Channel11_IRQn 1 */

  /* USER CODE END GPDMA1_Channel11_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 8 global interrupt.
  */
void GPDMA1_Channel8_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel8_IRQn 0 */

  /* USER CODE END GPDMA1_Channel8_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel8);
  /* USER CODE BEGIN GPDMA1_Channel8_IRQn 1 */

  /* USER CODE END GPDMA1_Channel8_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 9 global interrupt.
  */
void GPDMA1_Channel9_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel9_IRQn 0 */
  /* USER CODE END GPDMA1_Channel9_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel9);
  /* USER CODE BEGIN GPDMA1_Channel9_IRQn 1 */
  /* USER CODE END GPDMA1_Channel9_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 0 global interrupt.
  */
void HPDMA1_Channel0_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel0_IRQn 0 */

  /* USER CODE END HPDMA1_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel0);
  /* USER CODE BEGIN HPDMA1_Channel0_IRQn 1 */

  /* USER CODE END HPDMA1_Channel0_IRQn 1 */
}

/**
  * @brief This function handles HPDMA1 Channel 1 global interrupt.
  */
void HPDMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN HPDMA1_Channel1_IRQn 0 */

  /* USER CODE END HPDMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_HPDMA1_Channel1);
  /* USER CODE BEGIN HPDMA1_Channel1_IRQn 1 */

  /* USER CODE END HPDMA1_Channel1_IRQn 1 */
}
/**
* @brief This function handles GPDMA1 Channel 1 global interrupt.
*/
void GPDMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 0 */

  /* USER CODE END GPDMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 1 */

  /* USER CODE END GPDMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 2 global interrupt.
  */
void GPDMA1_Channel2_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 0 */

  /* USER CODE END GPDMA1_Channel2_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel2);
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 1 */

  /* USER CODE END GPDMA1_Channel2_IRQn 1 */
}
/**
  * @brief This function handles GPDMA1 Channel 3 global interrupt.
  */
void GPDMA1_Channel3_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel3_IRQn 0 */

  /* USER CODE END GPDMA1_Channel3_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel3);
  /* USER CODE BEGIN GPDMA1_Channel3_IRQn 1 */

  /* USER CODE END GPDMA1_Channel3_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 4 global interrupt.
  */
void GPDMA1_Channel4_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel4_IRQn 0 */

  /* USER CODE END GPDMA1_Channel4_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel4);
  /* USER CODE BEGIN GPDMA1_Channel4_IRQn 1 */

  /* USER CODE END GPDMA1_Channel4_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 5 global interrupt.
  */
void GPDMA1_Channel5_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel5_IRQn 0 */

  /* USER CODE END GPDMA1_Channel5_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel5);
  /* USER CODE BEGIN GPDMA1_Channel5_IRQn 1 */

  /* USER CODE END GPDMA1_Channel5_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 6 global interrupt.
  */
void GPDMA1_Channel6_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel6_IRQn 0 */

  /* USER CODE END GPDMA1_Channel6_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel6);
  /* USER CODE BEGIN GPDMA1_Channel6_IRQn 1 */

  /* USER CODE END GPDMA1_Channel6_IRQn 1 */
}

/**
  * @brief This function handles Serial Audio Interface 1 block A interrupt.
  */
void SAI1_A_IRQHandler(void)
{
  /* USER CODE BEGIN SAI1_A_IRQn 0 */

  /* USER CODE END SAI1_A_IRQn 0 */
  HAL_SAI_IRQHandler(&hsai_BlockA1);
  /* USER CODE BEGIN SAI1_A_IRQn 1 */

  /* USER CODE END SAI1_A_IRQn 1 */
}

/**
  * @brief This function handles Serial Audio Interface 1 block B interrupt.
  */
void SAI1_B_IRQHandler(void)
{
  /* USER CODE BEGIN SAI1_B_IRQn 0 */

  /* USER CODE END SAI1_B_IRQn 0 */
  HAL_SAI_IRQHandler(&hsai_BlockB1);
  /* USER CODE BEGIN SAI1_B_IRQn 1 */

  /* USER CODE END SAI1_B_IRQn 1 */
}

/**
  * @brief This function handles I2C1 Event interrupt.
  */
void I2C1_EV_IRQHandler(void)
{
  /* USER CODE BEGIN I2C1_EV_IRQn 0 */

  /* USER CODE END I2C1_EV_IRQn 0 */
  HAL_I2C_EV_IRQHandler(&hi2c1);
  /* USER CODE BEGIN I2C1_EV_IRQn 1 */

  /* USER CODE END I2C1_EV_IRQn 1 */
}

/**
  * @brief This function handles I2C1 Error interrupt.
  */
void I2C1_ER_IRQHandler(void)
{
  /* USER CODE BEGIN I2C1_ER_IRQn 0 */

  /* USER CODE END I2C1_ER_IRQn 0 */
  HAL_I2C_ER_IRQHandler(&hi2c1);
  /* USER CODE BEGIN I2C1_ER_IRQn 1 */

  /* USER CODE END I2C1_ER_IRQn 1 */
}

/**
  * @brief This function handles SPI2 global interrupt.
  */
void SPI2_IRQHandler(void)
{
  /* USER CODE BEGIN SPI2_IRQn 0 */

  /* USER CODE END SPI2_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi2);
  /* USER CODE BEGIN SPI2_IRQn 1 */

  /* USER CODE END SPI2_IRQn 1 */
}

void SPI4_IRQHandler(void)
{
  /* USER CODE BEGIN SPI2_IRQn 0 */

  /* USER CODE END SPI2_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi4);
  /* USER CODE BEGIN SPI2_IRQn 1 */

  /* USER CODE END SPI2_IRQn 1 */
}

/**
  * @brief This function handles SPI6 global interrupt.
  */
void SPI6_IRQHandler(void)
{
  /* USER CODE BEGIN SPI6_IRQn 0 */

  /* USER CODE END SPI6_IRQn 0 */
  /* SPI6 is shared between SPI mode (TFT) and I2S mode (audio).
   * Dispatch to the correct HAL handler based on which is active. */
  // if (hi2s6.State != HAL_I2S_STATE_RESET)
  // {
  //   HAL_I2S_IRQHandler(&hi2s6);
  // }
  // else
  // {
    HAL_SPI_IRQHandler(&hspi6);
  // }
  /* USER CODE BEGIN SPI6_IRQn 1 */

  /* USER CODE END SPI6_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  debug_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart2);

  /* USER CODE BEGIN USART1_IRQn 1 */
  debug_IRQHandler(&huart2);
  /* USER CODE END USART1_IRQn 1 */
}
/**
* @brief This function handles USART3 global interrupt.
*/
void USART3_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_IRQn 0 */

  /* USER CODE END USART3_IRQn 0 */
  HAL_UART_IRQHandler(&huart3);
  /* USER CODE BEGIN USART3_IRQn 1 */

  /* USER CODE END USART3_IRQn 1 */
}
/**
  * @brief This function handles UART7 global interrupt.
  */
void UART7_IRQHandler(void)
{
  /* USER CODE BEGIN UART7_IRQn 0 */

  /* USER CODE END UART7_IRQn 0 */
  HAL_UART_IRQHandler(&huart7);
  /* USER CODE BEGIN UART7_IRQn 1 */

  /* USER CODE END UART7_IRQn 1 */
}

extern void u0_module_IRQHandler(UART_HandleTypeDef *huart);
/**
  * @brief This function handles UART9 global interrupt.
  */
void UART9_IRQHandler(void)
{
  /* USER CODE BEGIN UART9_IRQn 0 */
  HAL_UART_IRQHandler(&huart9);
  // u0_module_IRQHandler(&huart9);
  /* USER CODE END UART9_IRQn 0 */
  /* USER CODE BEGIN UART9_IRQn 1 */

  /* USER CODE END UART9_IRQn 1 */
}

/**
  * @brief This function handles SDMMC1 global interrupt.
  */
void SDMMC1_IRQHandler(void)
{
  /* USER CODE BEGIN SDMMC1_IRQn 0 */

  /* USER CODE END SDMMC1_IRQn 0 */
  HAL_SD_IRQHandler(&hsd1);
  /* USER CODE BEGIN SDMMC1_IRQn 1 */

  /* USER CODE END SDMMC1_IRQn 1 */
}

/**
  * @brief This function handles USB1 OTG HS interrupt.
  */
void USB2_OTG_HS_IRQHandler(void)
{
  /* USER CODE BEGIN USB1_OTG_HS_IRQn 0 */

  /* USER CODE END USB1_OTG_HS_IRQn 0 */
  HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS2);
  /* USER CODE BEGIN USB1_OTG_HS_IRQn 1 */

  /* USER CODE END USB1_OTG_HS_IRQn 1 */
}

void XSPI2_IRQHandler(void)
{
    /* USER CODE BEGIN XSPI2_IRQn 0 */

    /* USER CODE END XSPI2_IRQn 0 */
    HAL_XSPI_IRQHandler(&hxspi2);
    /* USER CODE BEGIN XSPI2_IRQn 1 */

    /* USER CODE END XSPI2_IRQn 1 */
}
/**
  * @brief This function handles USB2 OTG HS interrupt.
  */
void USB1_OTG_HS_IRQHandler(void)
{
  /* USER CODE BEGIN USB2_OTG_HS_IRQn 0 */

  /* USER CODE END USB2_OTG_HS_IRQn 0 */
  // HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS1);
  /* USER CODE BEGIN USB2_OTG_HS_IRQn 1 */
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
  HAL_PCD_IRQHandler(&usbx_pcd_handle);
#else
#ifdef UX_HCD_ECM_USE_USB_OTG_HS1
  HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS1);
#endif
#endif
  /* USER CODE END USB2_OTG_HS_IRQn 1 */
}

/**
  * @brief This function handles RTC secure interrupt.
  */
void RTC_S_IRQHandler(void)
{
    /* USER CODE BEGIN RTC_S_IRQn 0 */

    /* USER CODE END RTC_S_IRQn 0 */
    HAL_RTC_AlarmIRQHandler(&hrtc);
    /* USER CODE BEGIN RTC_S_IRQn 1 */
  
    /* USER CODE END RTC_S_IRQn 1 */
}
/* USER CODE BEGIN 1 */
/**
  * @brief This function handles EXTI8 global interrupt.
  */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

/* USER CODE BEGIN 1 */
/**
  * @brief This function handles EXTI8 global interrupt.
  */
void EXTI5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
}

/**
  * @brief This function handles EXTI8 global interrupt.
  */
void EXTI8_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
}

void EXTI12_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
}

/* EXTI15_IRQHandler is owned by the Morse HaLow driver (see
 * MM_HALOW_BUSY_IRQ_HANDLER in mmhal_wlan.c, which #defines to
 * EXTI15_IRQHandler). Keep this Cube-generated copy disabled to avoid a
 * multiple-definition link error; mmhal_wlan.c's version also forwards to the
 * HaLow busy callback, so it is the correct handler. */
#if 0
void EXTI15_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}
#endif
void CSI_IRQHandler(void)
{
    HAL_DCMIPP_CSI_IRQHandler(CMW_CAMERA_GetDCMIPPHandle());
}

void DCMIPP_IRQHandler(void)
{
    HAL_DCMIPP_IRQHandler(CMW_CAMERA_GetDCMIPPHandle());
}
/* USER CODE END 1 */
