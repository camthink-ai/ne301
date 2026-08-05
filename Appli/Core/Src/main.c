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
#include "main.h"
#include "cmsis_os2.h"
#include "adc.h"
#include "csi.h"
#include "dma2d.h"
#include "gpdma.h"
#include "hpdma.h"
#include "i2c.h"
#include "icache.h"
#include "iwdg.h"
#include "ramcfg.h"
#include "rtc.h"
#include "jpeg.h"
#include "sai.h"
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_otg.h"
#include "venc.h"
#include "gpio.h"
#include "npu_cache.h"
#include "stm32n6570_discovery_xspi.h"
#include "usart.h"
#include "debug.h"
#include "framework.h"
#include "driver_core.h"
#include "driver_test.h"
#include "../FSBL/Core/Inc/xspim.h"
#include "rng.h"
#include "mm_hal_common.h"
#include "core_init.h"
#include "service_init.h"
#include "drtc.h"
#include "wdg.h"
#include "wifi.h" 
#include "crc.h"
#ifdef TX_INCLUDE_USER_DEFINE_FILE
#include "tx_user.h"
#endif
#define IS_IRQ_MODE()     (__get_IPSR() != 0U)

extern int __uncached_bss_start__;
extern int __uncached_bss_end__;

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "system_service.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

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
static void SystemIsolation_Config(void);
// static void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

static uint8_t main_tread_stack[1024 * 16];
/* USER CODE END PFP */
/* Definitions for mainTask */
osThreadId_t mainTaskHandle;
const osThreadAttr_t mainTask_attributes = {
  .name = "MainThread",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_mem = main_tread_stack,
  .stack_size = sizeof(main_tread_stack),
};

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


void NPURam_enable()
{
    __HAL_RCC_NPU_CLK_ENABLE();
    __HAL_RCC_NPU_FORCE_RESET();
    __HAL_RCC_NPU_RELEASE_RESET();

    /* Enable NPU RAMs (4x448KB) */
    __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();
    __HAL_RCC_RAMCFG_CLK_ENABLE();
    RAMCFG_HandleTypeDef hramcfg = {0};
    hramcfg.Instance =  RAMCFG_SRAM3_AXI;
    HAL_RAMCFG_EnableAXISRAM(&hramcfg);
    hramcfg.Instance =  RAMCFG_SRAM4_AXI;
    HAL_RAMCFG_EnableAXISRAM(&hramcfg);
    hramcfg.Instance =  RAMCFG_SRAM5_AXI;
    HAL_RAMCFG_EnableAXISRAM(&hramcfg);
    hramcfg.Instance =  RAMCFG_SRAM6_AXI;
    HAL_RAMCFG_EnableAXISRAM(&hramcfg);
}

// #if POWER_MODULE_TEST
void NPURam_disable()
{
    RAMCFG_HandleTypeDef hramcfg = {0};
    hramcfg.Instance =  RAMCFG_SRAM3_AXI;
    HAL_RAMCFG_DisableAXISRAM(&hramcfg);
    hramcfg.Instance =  RAMCFG_SRAM4_AXI;
    HAL_RAMCFG_DisableAXISRAM(&hramcfg);
    hramcfg.Instance =  RAMCFG_SRAM5_AXI;
    HAL_RAMCFG_DisableAXISRAM(&hramcfg);
    hramcfg.Instance =  RAMCFG_SRAM6_AXI;
    HAL_RAMCFG_DisableAXISRAM(&hramcfg);

    __HAL_RCC_AXISRAM3_MEM_CLK_DISABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_DISABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_DISABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_DISABLE();
    __HAL_RCC_RAMCFG_CLK_DISABLE();

    __HAL_RCC_NPU_CLK_DISABLE();
}
// #endif

static void Setup_Mpu()
{
    MPU_Attributes_InitTypeDef attr;
    MPU_Region_InitTypeDef region;

    attr.Number = MPU_ATTRIBUTES_NUMBER0;
    attr.Attributes = MPU_NOT_CACHEABLE;
    HAL_MPU_ConfigMemoryAttributes(&attr);

    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = (uint32_t)&__uncached_bss_start__;
    region.LimitAddress = (uint32_t)&__uncached_bss_end__ - 1;
    region.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
    region.AccessPermission = MPU_REGION_ALL_RW;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void NPUCache_config()
{
    npu_cache_enable();
}

static void IAC_Config(void)
{
    /* Configure IAC to trap illegal access events */
    __HAL_RCC_IAC_CLK_ENABLE();
    __HAL_RCC_IAC_FORCE_RESET();
    __HAL_RCC_IAC_RELEASE_RESET();
}

#if defined(TX_INCLUDE_USER_DEFINE_FILE)
/* ThreadX SysTick reload is fixed in tx_initialize_low_level.S from SYSTEM_CLOCK;
 * reprogram after RCC reconfiguration so tick matches actual CPU frequency. */
static void threadx_resync_systick_from_rcc(void)
{
  SystemCoreClockUpdate();
  uint32_t cpu = SystemCoreClock;
  uint32_t tick_hz = TX_TIMER_TICKS_PER_SECOND;
  if (tick_hz == 0U)
    tick_hz = 1000U;
  uint64_t r = ((uint64_t)cpu / (uint64_t)tick_hz);
  if (r == 0ULL)
    return;
  r -= 1ULL;
  uint32_t reload = (r > 0xFFFFFFULL) ? 0xFFFFFFUL : (uint32_t)r;
  SysTick->LOAD = reload;
  SysTick->VAL = 0U;
  SysTick->CTRL = 7U;
}
#endif

void IAC_IRQHandler(void)
{
    printf("IAC_IRQHandler\r\n");
    while (1)
    {
    }
}

static void PLATFORM_Config(void)
{
    
    // at fsbl, the system clock is configured by fsbl
    // __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SBF);
    // SystemClock_Config();
    // PeriphCommonClock_Config();
    MX_GPDMA1_Init();
    MX_HPDMA1_Init();
    MX_GPIO_Init();

    // MX_DMA2D_Init();
    Fuse_Programming();

#ifdef  STM32N6_DK_BOARD
    MX_USART1_UART_Init();
#else
    MX_USART2_UART_Init();
#endif
    MX_RNG_Init();
    MX_CRC_Init();

#if !defined(POWER_MODULE_TEST) || !POWER_MODULE_TEST
    // NPURam_enable();
    // NPUCache_config();
#endif
    /*** External RAM and NOR Flash *********************************************/
    // BSP_XSPI_RAM_Init(0);
    // BSP_XSPI_RAM_EnableMemoryMappedMode(0);
    MX_XSPI2_Init();
    XSPI_NOR_EnableMemoryMappedMode();

    /* Set all required IPs as secure privileged */
    SystemIsolation_Config();
    /* Configure IAC */
    IAC_Config();

    // LL_BUS_EnableClockLowPower(~0);
    // LL_MEM_EnableClockLowPower(~0);
    // LL_AHB1_GRP1_EnableClockLowPower(~0);
    // LL_AHB2_GRP1_EnableClockLowPower(~0);
    // LL_AHB3_GRP1_EnableClockLowPower(~0);
    // LL_AHB4_GRP1_EnableClockLowPower(~0);
    // LL_AHB5_GRP1_EnableClockLowPower(~0);
    // LL_APB1_GRP1_EnableClockLowPower(~0);
    // LL_APB1_GRP2_EnableClockLowPower(~0);
    // LL_APB2_GRP1_EnableClockLowPower(~0);
    // LL_APB4_GRP1_EnableClockLowPower(~0);
    // LL_APB4_GRP2_EnableClockLowPower(~0);
    // LL_APB5_GRP1_EnableClockLowPower(~0);
    // LL_MISC_EnableClockLowPower(~0);
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
* @brief Function implementing the mainTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDefaultTask */
void StartMainTask(void *argument)
{
    // Start time measurement using new uptime API
    // uint64_t total_start_time_ms = rtc_get_uptime_ms();
    // uint64_t step_start_time_ms, step_end_time_ms, step_duration_ms;

#if defined(TX_INCLUDE_USER_DEFINE_FILE)
    threadx_resync_systick_from_rcc();
#endif
    // The serial port has not been initialized and will not print
    // printf("\r\n========== SYSTEM BOOT TIME MEASUREMENT ==========\r\n");
    
    // Step 1: Platform configuration
    // step_start_time_ms = rtc_get_uptime_ms();
    PLATFORM_Config();
    // step_end_time_ms = rtc_get_uptime_ms();
    // step_duration_ms = step_end_time_ms - step_start_time_ms;
    // printf("[BOOT] Step 1 - PLATFORM_Config: %lu ms\r\n", (unsigned long)step_duration_ms);
    
    // printf("StartMainTask\r\n");
    // printf("\r\n-------------- CLK INFO --------------\r\n");
    // printf("CPU: %lu MHz\r\n", HAL_RCC_GetCpuClockFreq() / 1000000UL);
    // printf("SYS: %lu MHz\r\n", HAL_RCC_GetSysClockFreq() / 1000000UL);
    // printf("NPU: %lu MHz\r\n", HAL_RCC_GetNPUClockFreq() / 1000000UL);
    // printf("NPURAM: %lu MHz\r\n", HAL_RCC_GetNPURAMSClockFreq() / 1000000UL);
    // printf("HCLK: %lu MHz\r\n", HAL_RCC_GetHCLKFreq() / 1000000UL);
    // printf("-------------------------------------\r\n");

    // Step 2: Framework initialization
    // step_start_time_ms = rtc_get_uptime_ms();
    framework_init();
    // step_end_time_ms = rtc_get_uptime_ms();
    // step_duration_ms = step_end_time_ms - step_start_time_ms;
    // printf("[BOOT] Step 2 - framework_init: %lu ms\r\n", (unsigned long)step_duration_ms);
    
    // Step 3: Driver core initialization
    // step_start_time_ms = rtc_get_uptime_ms();
    driver_core_init();
    // step_end_time_ms = rtc_get_uptime_ms();
    // step_duration_ms = step_end_time_ms - step_start_time_ms;
    // printf("[BOOT] Step 3 - driver_core_init: %lu ms\r\n", (unsigned long)step_duration_ms);

    // Step 4: Core system initialization
    // step_start_time_ms = rtc_get_uptime_ms();
    core_system_init();
    // step_end_time_ms = rtc_get_uptime_ms();
    // step_duration_ms = step_end_time_ms - step_start_time_ms;
    printf("BOOT: %lu ms\r\n", (unsigned long)rtc_get_uptime_ms());

#if POWER_MODULE_TEST
    for (;;) {
        osDelay(1000);
    }
#endif

    if (is_wifi_ant()) {
      for (;;) {
          osDelay(100);
      }
    }

    // Step 5: Service initialization
    // step_start_time_ms = rtc_get_uptime_ms();
    service_init();
    // step_end_time_ms = rtc_get_uptime_ms();
    // step_duration_ms = step_end_time_ms - step_start_time_ms;
    // printf("[BOOT] Step 5 - service_init: %lu ms\r\n", (unsigned long)step_duration_ms);
    
    // printf("[MAIN] All systems initialized successfully\r\n");

    // Step 6: Process wakeup event
    // step_start_time_ms = rtc_get_uptime_ms();
    // printf("[MAIN] Processing wakeup event...\r\n");
    aicam_result_t result = system_service_process_wakeup_event();
    // step_end_time_ms = rtc_get_uptime_ms();
    // step_duration_ms = step_end_time_ms - step_start_time_ms;
    if (result != AICAM_OK) {
        LOG_WARN("[MAIN] Failed to process wakeup event: %d", result);
    } 
    // else {
    //     printf("[MAIN] Wakeup event processed successfully\r\n");
    // }
    // printf("[BOOT] Step 6 - process_wakeup_event: %lu ms\r\n", (unsigned long)step_duration_ms);
    
    // Calculate and print total boot time
    // uint64_t total_end_time_ms = rtc_get_uptime_ms();
    // uint64_t total_duration_ms = total_end_time_ms - total_start_time_ms;
    // printf("[BOOT] ============================================\r\n");
    // printf("[BOOT] TOTAL BOOT TIME: %lu ms (%.2f seconds)\r\n", 
    //        (unsigned long)total_duration_ms, total_duration_ms / 1000.0f);
    // printf("[BOOT] ============================================\r\n\r\n");
    
    // wdg_task_change_priority(osPriorityNormal);
    // printf("[MAIN] Entering main loop\r\n");

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
        LOG_WARN("[MAIN] System reset due to IWDG reset\r\n");
        __HAL_RCC_CLEAR_RESET_FLAGS();
    }
    
    printf("MAIN: %lu ms\r\n", (unsigned long)rtc_get_uptime_ms());
    
    /* Infinite loop */
    uint32_t flush_poll_div = 0;  /* counts osDelay(100) ticks; poll flush every ~150 (15s) */
    for(;;)
    {
        // Periodic scheduled-flush poll: catches upload nodes that arrive while
        // awake (FULL_SPEED mode, or LOW_POWER awake periods spanning a node).
        // The wake path only fires on cold-boot wake; this covers the gap.
        if (++flush_poll_div >= 150) {
            flush_poll_div = 0;
            (void)system_service_poll_scheduled_flush();
        }

        // Check if system needs to enter sleep mode
        aicam_bool_t sleep_pending = AICAM_FALSE;
        result = system_service_is_sleep_pending(&sleep_pending);

        if (result == AICAM_OK && sleep_pending == AICAM_TRUE) {
            LOG_INFO("[MAIN] Sleep pending detected, entering sleep mode...\r\n");

            // Execute sleep operation
            result = system_service_execute_pending_sleep();
            if (result == AICAM_OK) {
                // Note: After entering sleep, system will reset upon wakeup
                // Execution will restart from main() -> StartMainTask()
                // This line should not be reached
                printf("SLEEP: %lu ms\r\n", (unsigned long)rtc_get_uptime_ms());
                osDelay(1000);  
                // Normally, it wouldn't run over here
                printf("[MAIN] Resetting system...\r\n");
                HAL_NVIC_SystemReset();
            } else {
                // printf("[MAIN] Failed to enter sleep mode: %d, continuing...\r\n", result);
                LOG_WARN("[MAIN] Failed to enter sleep mode: %d", result);
                osDelay(100); // Wait before retry
            }
        }

        // Sleep 100ms to avoid busy waiting
        osDelay(100);
    }
    /* USER CODE END mainTask */
}

/**
* @brief  The application entry point.
* @retval int
*/
int main(void)
{
    MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_ICACTIVE_Msk;

    // /* Set back system and CPU clock source to HSI */
    // __HAL_RCC_CPUCLK_CONFIG(RCC_CPUCLKSOURCE_HSI);
    // __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_HSI);

    /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();

    Setup_Mpu();

    SCB_EnableICache();
#if defined(USE_DCACHE)
    MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk;
    SCB_EnableDCache();
#endif

    osKernelInitialize();  /* Init code for STM32_WPAN */
    mainTaskHandle = osThreadNew(StartMainTask, NULL, &mainTask_attributes);
    /* Start scheduler */
    osKernelStart();
    /* USER CODE BEGIN 2 */

    /* USER CODE END RIF_Init 1 */
    /* USER CODE BEGIN RIF_Init 2 */
    assert(0);

    return -1;
    /* USER CODE END RIF_Init 2 */

}

// /**
// * @brief Peripherals Common Clock Configuration
// * @retval None
// */
// static void PeriphCommonClock_Config(void)
// {
//     RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};


//     /* XSPI1 kernel clock (ck_ker_xspi1) = HCLK = 200MHz */
//     PeriphClkInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_XSPI1;
//     PeriphClkInitStruct.Xspi1ClockSelection = RCC_XSPI1CLKSOURCE_HCLK;

//     PeriphClkInitStruct.PeriphClockSelection |= RCC_PERIPHCLK_XSPI2;
//     PeriphClkInitStruct.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_HCLK;

//     PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_TIM;
//     PeriphClkInitStruct.TIMPresSelection = RCC_TIMPRES_DIV1;
//     if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
//     {
//       Error_Handler();
//     }
// }

/**
* @brief RIF Initialization Function
* @param None
* @retval None
*/
static void SystemIsolation_Config(void)
{

/* USER CODE BEGIN RIF_Init 0 */

/* USER CODE END RIF_Init 0 */

    /* set all required IPs as secure privileged */
    __HAL_RCC_RIFSC_CLK_ENABLE();
    RIMC_MasterConfig_t RIMC_master = {0};
    RIMC_master.MasterCID = RIF_CID_1;
    RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

    /*RIMC configuration*/
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &RIMC_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU, &RIMC_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_SDMMC1, &RIMC_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_VENC, &RIMC_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_OTG1, &RIMC_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_OTG2, &RIMC_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);
    
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RCC_PERIPH_INDEX_HPDMA1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_JPEG, RIF_ATTRIBUTE_PRIV | RIF_ATTRIBUTE_SEC);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_NPU , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DMA2D , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SDMMC1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_OTG1HS , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_OTG2HS , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_ADC12, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_VENC   , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SAI1   , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SAES    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_RNG    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_PKA    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CRYP    , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

    /* set up GPIO configuration */
    HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_7,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOA,GPIO_PIN_12,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_13,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOB,GPIO_PIN_15,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOC,GPIO_PIN_12,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_0,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_12,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOD,GPIO_PIN_15,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_5,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_10,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_13,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_14,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_3,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_4,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_6,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_7,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOF,GPIO_PIN_9,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_1,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_8,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_11,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_12,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOG,GPIO_PIN_15,GPIO_PIN_SEC|GPIO_PIN_NPRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOH,GPIO_PIN_2,GPIO_PIN_SEC|GPIO_PIN_NPRIV);

/* USER CODE BEGIN RIF_Init 1 */

/* USER CODE END RIF_Init 1 */
/* USER CODE BEGIN RIF_Init 2 */

/* USER CODE END RIF_Init 2 */

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void _Error_Handler_(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
