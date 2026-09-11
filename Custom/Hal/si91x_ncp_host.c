/*******************************************************************************
* @file  efx32_ncp_host.c
* @brief 
*******************************************************************************
* # License
* <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
*******************************************************************************
*
* The licensor of this software is Silicon Laboratories Inc. Your use of this
* software is governed by the terms of Silicon Labs Master Software License
* Agreement (MSLA) available at
* www.silabs.com/about-us/legal/master-software-license-agreement. This
* software is distributed to you in Source Code format and is governed by the
* sections of the MSLA applicable to Source Code.
*
******************************************************************************/

#include <stdbool.h>
#include <string.h>
#include "sl_wifi_constants.h"
#include "sl_si91x_host_interface.h"
#include "cmsis_os2.h"
#include "sl_si91x_status.h"
#include "sl_rsi_utility.h"
#include "sli_wifi_utility.h"
#include "sl_constants.h"
#include "sl_status.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_hal_spi.h"
#include "cmsis_gcc.h" 
#include "sl_core.h"
#include "pwr.h"
#include "spi.h"
#include "exti.h"
// #include "tx_port.h"
#include "common_utils.h"

#ifdef SPI_EXTENDED_TX_LEN_2K
    #define SPI_BUFFER_LENGTH 2300
#else
    #define SPI_BUFFER_LENGTH 1616
#endif
#define DMA_ENABLED

// extern void Error_Handler(void);
void gpio_interrupt(void);

extern SPI_HandleTypeDef hspi4;
osMutexId_t mtx_id = NULL;
osSemaphoreId_t sem_spi4 = NULL;
osSemaphoreId_t sem_sta = NULL;
int is_high_spi = 0;

uint8_t spi_tx_buffer[SPI_BUFFER_LENGTH] ALIGN_32 UNCACHED;
uint8_t spi_rx_buffer[SPI_BUFFER_LENGTH] ALIGN_32 UNCACHED;

static void si91x_gpio_interrupt(void);
static void si91x_sta_interrupt(void);

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void si91x_GPIO_Init(void)
{
    // GPIO_InitTypeDef GPIO_InitStruct = {0};

    // /* GPIO Ports Clock Enable */
    // __HAL_RCC_GPIOE_CLK_ENABLE();
    // __HAL_RCC_GPIOD_CLK_ENABLE();
    // __HAL_RCC_GPIOC_CLK_ENABLE();
    // __HAL_RCC_GPIOB_CLK_ENABLE();


    // HAL_GPIO_WritePin(GPIOD, WIFI_RESET_N_Pin, GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(GPIOD, WIFI_ULP_WAKEUP_Pin, GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);


    // GPIO_InitStruct.Pin = WIFI_RESET_N_Pin;
    // GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    // GPIO_InitStruct.Pull = GPIO_NOPULL;
    // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    // HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    // GPIO_InitStruct.Pin = WIFI_ULP_WAKEUP_Pin;
    // GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    // GPIO_InitStruct.Pull = GPIO_NOPULL;
    // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    // HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    // /*Configure GPIO pin : WIFI_STA_Pin */
    // GPIO_InitStruct.Pin = WIFI_STA_Pin;
    // GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    // GPIO_InitStruct.Pull = GPIO_NOPULL;
    // HAL_GPIO_Init(WIFI_STA_GPIO_Port, &GPIO_InitStruct);

    // /*Configure GPIO pin : WIFI_IRQ_Pin */
    // GPIO_InitStruct.Pin = WIFI_IRQ_Pin;
    // GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    // GPIO_InitStruct.Pull = GPIO_NOPULL;
    // HAL_GPIO_Init(WIFI_IRQ_GPIO_Port, &GPIO_InitStruct);
    
    /*Configure the EXTI line attribute */
    HAL_EXTI_ConfigLineAttributes(EXTI_LINE_8, EXTI_LINE_SEC);

    /* EXTI interrupt init*/
    HAL_NVIC_SetPriority(EXTI8_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI8_IRQn);

    osDelay(100);
}

void sl_si91x_host_hold_in_reset(void)
{
    HAL_GPIO_WritePin(WIFI_POC_IN_GPIO_Port, WIFI_POC_IN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(WIFI_RESET_N_GPIO_Port, WIFI_RESET_N_Pin, GPIO_PIN_RESET);
}

void sl_si91x_host_release_from_reset(void)
{
    HAL_GPIO_WritePin(WIFI_POC_IN_GPIO_Port, WIFI_POC_IN_Pin, GPIO_PIN_SET);
    osDelay(5);
    HAL_GPIO_WritePin(WIFI_RESET_N_GPIO_Port, WIFI_RESET_N_Pin, GPIO_PIN_SET);
}

static sl_si91x_host_init_configuration_t bus_config = { 0 };

sl_status_t sl_si91x_host_init(const sl_si91x_host_init_configuration_t *config)
{
    // 4.1.x bus model: the driver passes rx_irq/rx_done; the GPIO ISR must call
    // rx_irq (sli_si91x_bus_rx_irq_handler) instead of the legacy set_event.
    bus_config.rx_irq      = config->rx_irq;
    bus_config.rx_done     = config->rx_done;
    bus_config.boot_option = config->boot_option;
    // printf("sl_si91x_host_init\r\n");
    pwr_manager_acquire(pwr_manager_get_handle(PWR_WIFI));
    if (sem_spi4 == NULL) {
        sem_spi4 = osSemaphoreNew(1, 0, NULL);
    }

    if (mtx_id == NULL) {
        mtx_id = osMutexNew(NULL);
    }

    if (sem_sta == NULL) {
        sem_sta = osSemaphoreNew(1, 0, NULL);
    }
    //! Initialize the host platform GPIOs
    si91x_GPIO_Init();
    
    //! Initialize SPI
    MX_SPI4_Init();
    
    exti8_irq_register(si91x_gpio_interrupt);
    exti5_irq_register(si91x_sta_interrupt);

    HAL_NVIC_EnableIRQ(EXTI5_IRQn);
    return SL_STATUS_OK;
}

sl_status_t sl_si91x_host_deinit(void)
{
    // printf("sl_si91x_host_deinit\r\n");
    if (mtx_id != NULL) osMutexAcquire(mtx_id, osWaitForever);
    HAL_NVIC_DisableIRQ(EXTI5_IRQn);
    HAL_SPI_Abort(&hspi4);
    HAL_SPI_DeInit(&hspi4);
    pwr_manager_release(pwr_manager_get_handle(PWR_WIFI));
    is_high_spi = 0;
    if (mtx_id != NULL) osMutexRelease(mtx_id);
    return SL_STATUS_OK;
}

/*==================================================================*/
/**
 * @fn         sl_status_t sl_si91x_host_spi_transfer(const void *tx_buffer, void *rx_buffer, uint16_t buffer_length)
 * @param[in]  uint8_t *tx_buff, pointer to the buffer with the data to be transferred
 * @param[in]  uint8_t *rx_buff, pointer to the buffer to store the data received
 * @param[in]  uint16_t transfer_length, Number of bytes to send and receive
 * @param[in]  uint8_t mode, To indicate mode 8 BIT/32 BIT mode transfers.
 * @param[out] None
 * @return     0, 0=success
 * @section description
 * This API is used to transfer/receive data to the Wi-Fi module through the SPI interface.
 */
// CLI fault injection ("ifconfig wl error_test dma_once|dma_ff|dma_off"):
//   dma_once — next DMA transfer takes the semaphore-timeout path once (transient
//              "sem_spi4 dma failed"), then auto-clears.
//   dma_ff   — every transfer "succeeds" but returns all-0xFF data (stuck MISO /
//              unresponding chip). C1/C2 then polls to its 1s timeout and the
//              firmware-error recovery chain fires; recovery exhausts its 10
//              retries by design. Clear with dma_off, then re-up the netif.
//   dma_off  — clear injection.
#ifdef SIMULATION_SPI4_DMA_ERROR
static uint8_t sim_spi4_dma_mode = 0; // 0 off, 1 fail-sem-once, 2 rx all-0xFF

void sl_si91x_host_sim_spi4_dma(uint8_t mode)
{
    sim_spi4_dma_mode = mode;
}
#endif

void sl_si91x_host_restore_spi4(void)
{
    if (is_high_spi) {
        sl_si91x_host_enable_high_speed_bus();
        printf("Restore high speed spi4\r\n");
    } else {
        HAL_SPI_DeInit(&hspi4);
        MX_SPI4_Init();
        printf("Restore low speed spi4\r\n");
    }
}

sl_status_t sl_si91x_host_spi_transfer(const void *tx_buffer, void *rx_buffer, uint16_t buffer_length)
{
    HAL_StatusTypeDef ret = HAL_OK;
    osStatus_t sem_status = osOK;

    if (mtx_id == NULL) return SL_STATUS_INVALID_STATE;
    osMutexAcquire(mtx_id, osWaitForever);

    if (buffer_length < 1 || buffer_length > SPI_BUFFER_LENGTH) {
        printf("[si91x]Invalid buffer length: %d\r\n", buffer_length);
        osMutexRelease(mtx_id);
        return SL_STATUS_INVALID_PARAMETER;
    }

    if (rx_buffer != NULL) {
        memset(spi_rx_buffer, 0x00, buffer_length);
    }

    if (tx_buffer == NULL) {
        memset(spi_tx_buffer, 0xff, buffer_length);
    } else {
        memcpy(spi_tx_buffer, tx_buffer, buffer_length);
    }

    // printf("[si91x] => ");
    // for (uint16_t i = 0; i < buffer_length; i++) {
    //     printf("%02X ", ((uint8_t *)spi_tx_buffer)[i]);
    // }
    // printf("\r\n");

    if (buffer_length < 4) {
        ret = HAL_SPI_TransmitReceive(&hspi4, (uint8_t *)spi_tx_buffer, (uint8_t *)spi_rx_buffer, buffer_length, 200);
        if (ret != HAL_OK) {
            printf("[si91x]transmit failed(ret = %d)!\r\n", ret);
            HAL_SPI_Abort(&hspi4);
            sl_si91x_host_restore_spi4();
            osMutexRelease(mtx_id);
            return SL_STATUS_ABORT;
        }
    } else {

#ifdef DMA_ENABLED
        ret = HAL_SPI_TransmitReceive_DMA(&hspi4, (uint8_t *)spi_tx_buffer, (uint8_t *)spi_rx_buffer, buffer_length);
        if (ret == HAL_OK) {
            sem_status = osSemaphoreAcquire(sem_spi4, 200);
        #ifdef SIMULATION_SPI4_DMA_ERROR
            if (sim_spi4_dma_mode == 1) {
                sim_spi4_dma_mode = 0;        // one-shot: next transfer is healthy again
                sem_status     = (osStatus_t)osErrorTimeout; // fake the acquire failure
            }
        #endif
            if (sem_status != osOK) {
                printf("[si91x]Wait DMA failed(ret = %d)!\r\n", (int)sem_status);
                HAL_SPI_Abort(&hspi4);
                sl_si91x_host_restore_spi4();
                osMutexRelease(mtx_id);
                return SL_STATUS_TIMEOUT;
            }
        #ifdef SIMULATION_SPI4_DMA_ERROR
            if (sim_spi4_dma_mode == 2) {
                memset(spi_rx_buffer, 0xFF, buffer_length);
            }
        #endif
        } else {
            printf("[si91x]DMA transmit failed(ret = %d)!\r\n", ret);
            HAL_SPI_Abort(&hspi4);
            sl_si91x_host_restore_spi4();
            osMutexRelease(mtx_id);
            return SL_STATUS_ABORT;
        }
#else
        HAL_SPI_TransmitReceive(&hspi4, (uint8_t *)spi_tx_buffer, (uint8_t *)spi_rx_buffer, buffer_length, 200);
#endif
    }

    // printf("[si91x] <= ");
    // for (uint16_t i = 0; i < buffer_length; i++) {
    //     printf("%02X ", ((uint8_t *)spi_rx_buffer)[i]);
    // }
    // printf("\r\n");

    if (rx_buffer != NULL) memcpy(rx_buffer, spi_rx_buffer, buffer_length);
    osMutexRelease(mtx_id);
    return SL_STATUS_OK;
}

void sl_si91x_host_enable_high_speed_bus()
{
    is_high_spi = true;
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
    hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi4.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi4.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi4.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi4.Init.CRCPolynomial = 0x7;
    hspi4.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi4.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    hspi4.Init.FifoThreshold = SPI_FIFO_THRESHOLD_04DATA;
    hspi4.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi4.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi4.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi4.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi4.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    hspi4.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
    hspi4.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;

    HAL_SPI_DeInit(&hspi4);
    if (HAL_SPI_Init(&hspi4) != HAL_OK) {
        Error_Handler();
    }
}

void sl_si91x_host_spi_cs_assert()
{
    SPI4_NSS_LOW();
}

void sl_si91x_host_spi_cs_deassert()
{
    SPI4_NSS_HIGH();
}

void sl_si91x_host_enable_bus_interrupt(void)
{
    HAL_NVIC_EnableIRQ(EXTI8_IRQn);
}

void sl_si91x_host_disable_bus_interrupt(void)
{
    HAL_NVIC_DisableIRQ(EXTI8_IRQn);
}

// Sleep-confirm pin handshake. 4.1.1 toggles this per SPI transaction
// (sli_si91x_req_wakeup sets, post-tx/rx clears) regardless of performance
// profile — 4.0.2 gated the toggle on != HIGH_PERFORMANCE, so the old
// "wup 0/1" debug only fired in low-power mode. 4.1.1 fires it every
// transaction by design (the reference STM32 port does the same). The toggle
// is harmless: the NCP firmware decides whether to actually sleep. Debug
// printf removed to stop per-transaction log spam; re-add here if debugging
// power-save.
void sl_si91x_host_set_sleep_indicator(void)
{
    HAL_GPIO_WritePin(WIFI_ULP_WAKEUP_GPIO_Port, WIFI_ULP_WAKEUP_Pin, GPIO_PIN_SET);
}

void sl_si91x_host_clear_sleep_indicator(void)
{
    HAL_GPIO_WritePin(WIFI_ULP_WAKEUP_GPIO_Port, WIFI_ULP_WAKEUP_Pin, GPIO_PIN_RESET);
}

uint32_t sl_si91x_host_get_wake_indicator(void)
{
    uint32_t awake = HAL_GPIO_ReadPin(WIFI_STA_GPIO_Port, WIFI_STA_Pin);
    // Already awake (HIGH_PERFORMANCE never sleeps): return immediately, zero
    // per-transaction overhead. Only when sleeping do we block on the WIFI_STA
    // rising-edge semaphore (EXTI5) for up to 10ms — this yields the RT1 thread
    // so a wedged chip can't starve the scheduler (console dies / no reboot
    // because wdgTask RT7 keeps feeding IWDG), and returns the instant the chip
    // actually asserts WIFI_STA. req_wakeup() loops this up to 5s. 4.0.2 form,
    // but gated on !awake so HIGH_PERFORMANCE keeps the old 10ms-per-tx hit off.
    if (!awake) {
        osSemaphoreAcquire(sem_sta, 10);
        awake = HAL_GPIO_ReadPin(WIFI_STA_GPIO_Port, WIFI_STA_Pin);
    }
    return awake;
}

static void si91x_gpio_interrupt(void)
{
    // Trigger SiWx91x BUS Event (4.1.x rx_irq callback model)
    if (bus_config.rx_irq != NULL) {
        bus_config.rx_irq();
    }
}

static void si91x_sta_interrupt(void)
{
    osSemaphoreRelease(sem_sta);
}

bool sl_si91x_host_is_in_irq_context(void)
{
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U;
}


typedef uint32_t CORE_irqState_t;

CORE_irqState_t CORE_EnterAtomic(void)
{
    CORE_irqState_t state = __get_PRIMASK(); // Get current interrupt state
    __disable_irq(); // Disable interrupts
    return state;
}

void CORE_ExitAtomic(CORE_irqState_t state)
{
    __set_PRIMASK(state); // Restore interrupt state
}

CORE_irqState_t CORE_EnterCritical(void)
{
    CORE_irqState_t state = __get_PRIMASK();  // Get current interrupt state
    __disable_irq();  // Disable interrupts
    return state;
}

void CORE_ExitCritical(CORE_irqState_t state)
{
    __set_PRIMASK(state);  // Restore interrupt state
}
