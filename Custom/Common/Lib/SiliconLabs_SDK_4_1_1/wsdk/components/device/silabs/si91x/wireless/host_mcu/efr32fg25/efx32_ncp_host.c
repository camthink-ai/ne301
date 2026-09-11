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
#include "sli_hal_si91x.h"
#include "sl_wifi_constants.h"
#include "sl_si91x_host_interface.h"
#include "sl_board_configuration.h"
#include "sl_status.h"
#include "em_eusart.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "cmsis_os2.h"
#include "sl_dma_manager.h"
#include "sl_dma_channel.h"
#include "gpiointerrupt.h"
#include "sl_si91x_status.h"
#include "sl_rsi_utility.h"
#include "sl_constants.h"
#include <stdbool.h>
#include <string.h>

// Maximum SPI transfer size handled in one call. Used to size the dummy
// buffer used when the caller passes NULL for tx_buffer / rx_buffer.
#define SLI_NCP_SPI_MAX_TRANSFER_LENGTH 4096
#define PACKET_PENDING_INT_PRI          3
#define SLI_SPI_BIT_RATE                10000000
#define SLI_SPI_TRANSFER_TIMEOUT_MS     1000

static void sli_dma_callback(sl_dma_channel_handle_t *dma_channel_handle, void *user_data, bool error, bool aborted);

// DMA Driver 2.0 channel resources. Handles are caller-allocated stack
// structs; the channel driver does not allocate them. The DMA Manager is
// auto-initialized via SL Main, so no manual sl_dma_manager_init() is
// required.
static uint8_t rx_dma_channel_nbr = 0;
static uint8_t tx_dma_channel_nbr = 0;
static sl_dma_channel_handle_t rx_dma_handle;
static sl_dma_channel_handle_t tx_dma_handle;

osMutexId_t spi_transfer_mutex = NULL;

static uint8_t host_initialized             = 0;
static volatile bool spi_dma_transfer_error = false;

// Dummy buffer used as tx source / rx sink when the caller does not supply
// a buffer. Sized to cover the maximum supported transfer length so the
// new DMA driver can auto-segment without overrunning it.
static uint8_t dummy_buffer[SLI_NCP_SPI_MAX_TRANSFER_LENGTH];

static osSemaphoreId_t transfer_done_semaphore = NULL;

static void sli_dma_callback(sl_dma_channel_handle_t *dma_channel_handle, void *user_data, bool error, bool aborted)
{
  UNUSED_PARAMETER(dma_channel_handle);
  UNUSED_PARAMETER(user_data);
  spi_dma_transfer_error = (error || aborted);
  osSemaphoreRelease(transfer_done_semaphore);
}

static void gpio_interrupt(uint8_t interrupt_number)
{
  UNUSED_PARAMETER(interrupt_number);
  sli_hal_si91x_notify_events(SLI_HAL_SI91X_RX_EVENT);
  //  GPIO_IntClear(0xAAAA);
}

void sl_si91x_host_set_sleep_indicator(void)
{
  GPIO_PinOutSet(SLEEP_CONFIRM_PIN.port, SLEEP_CONFIRM_PIN.pin);
}

void sl_si91x_host_clear_sleep_indicator(void)
{
  GPIO_PinOutClear(SLEEP_CONFIRM_PIN.port, SLEEP_CONFIRM_PIN.pin);
}

uint32_t sl_si91x_host_get_wake_indicator(void)
{
  return GPIO_PinInGet(WAKE_INDICATOR_PIN.port, WAKE_INDICATOR_PIN.pin);
}

sl_status_t sl_si91x_host_init(const sl_si91x_host_init_configuration_t *config)
{
  sl_status_t dma_status = SL_STATUS_OK;
  UNUSED_PARAMETER(config);
  if (!host_initialized) {
    // Enable clock (not needed on xG21)
    CMU_ClockEnable(cmuClock_GPIO, true);

    // Configure SPI bus pins
    GPIO_PinModeSet(SPI_MISO_PIN.port, SPI_MISO_PIN.pin, gpioModeInput, 0);
    GPIO_PinModeSet(SPI_MOSI_PIN.port, SPI_MOSI_PIN.pin, gpioModePushPull, 0);
    GPIO_PinModeSet(SPI_CLOCK_PIN.port, SPI_CLOCK_PIN.pin, gpioModePushPullAlternate, 0);
    GPIO_PinModeSet(SPI_CS_PIN.port, SPI_CS_PIN.pin, gpioModePushPull, 1);
    // Enable clock (not needed on xG21)
    CMU_ClockEnable(SPI_EUSART_CMU_CLOCK, true);

    // Default asynchronous initializer (master mode, 1 Mbps, 8-bit data)
    EUSART_SpiAdvancedInit_TypeDef adv = EUSART_SPI_ADVANCED_INIT_DEFAULT;

    adv.msbFirst = true; // SPI standard MSB first

    EUSART_SpiInit_TypeDef init = EUSART_SPI_MASTER_INIT_DEFAULT_HF;

    init.bitRate          = SLI_SPI_BIT_RATE; // 10 MHz shift clock
    init.advancedSettings = &adv;             // Advanced settings structure

    /*
   * Route EUSART RX, TX, and CLK to the specified pins.  Note that CS is
   * not controlled by EUSART so there is no write to the corresponding
   * USARTROUTE register to do this.
   */
    GPIO->EUSARTROUTE[SPI_EUSART_ROUTE_INDEX].RXROUTE = (SPI_MISO_PIN.port << _GPIO_EUSART_RXROUTE_PORT_SHIFT)
                                                        | (SPI_MISO_PIN.pin << _GPIO_EUSART_RXROUTE_PIN_SHIFT);
    GPIO->EUSARTROUTE[SPI_EUSART_ROUTE_INDEX].TXROUTE = (SPI_MOSI_PIN.port << _GPIO_EUSART_TXROUTE_PORT_SHIFT)
                                                        | (SPI_MOSI_PIN.pin << _GPIO_EUSART_TXROUTE_PIN_SHIFT);
    GPIO->EUSARTROUTE[SPI_EUSART_ROUTE_INDEX].SCLKROUTE = (SPI_CLOCK_PIN.port << _GPIO_EUSART_SCLKROUTE_PORT_SHIFT)
                                                          | (SPI_CLOCK_PIN.pin << _GPIO_EUSART_SCLKROUTE_PIN_SHIFT);
    GPIO->EUSARTROUTE[SPI_EUSART_ROUTE_INDEX].CSROUTE = (SPI_CS_PIN.port << _GPIO_EUSART_CSROUTE_PORT_SHIFT)
                                                        | (SPI_CS_PIN.pin << _GPIO_EUSART_CSROUTE_PIN_SHIFT);

    // Enable USART interface pins
    GPIO->EUSARTROUTE[SPI_EUSART_ROUTE_INDEX].ROUTEEN = GPIO_EUSART_ROUTEEN_RXPEN | // MISO
                                                        GPIO_EUSART_ROUTEEN_TXPEN | // MOSI
                                                        GPIO_EUSART_ROUTEEN_SCLKPEN | GPIO_EUSART_ROUTEEN_CSPEN;

    // Set slew rate for alternate usage pins
    GPIO_SlewrateSet(SPI_CLOCK_PIN.port, 7, 7);

    EUSART1->TIMINGCFG |= /*USART_TIMING_TXDELAY_ONE | USART_TIMING_CSSETUP_ONE |*/ EUSART_TIMINGCFG_CSHOLD_ONE;

    EUSART_SpiInit(SPI_EUSART, &init);

    if (transfer_done_semaphore == NULL) {
      transfer_done_semaphore = osSemaphoreNew(1, 0, NULL);
    }

    if (spi_transfer_mutex == NULL) {
      spi_transfer_mutex = osMutexNew(NULL);
    }

    // DMA Driver 2.0 init: allocate one channel per direction, bind a
    // handle to each and program the EUSART peripheral signals once. The
    // DMA Manager itself is auto-initialized by SL Main. The TX channel
    // does not need a callback (completion is observed via the RX-side
    // semaphore), so NULL is registered.
    dma_status = sl_dma_manager_allocate_channel(NULL, &rx_dma_channel_nbr);
    if (dma_status != SL_STATUS_OK) {
      return dma_status;
    }
    dma_status = sl_dma_manager_allocate_channel(NULL, &tx_dma_channel_nbr);
    if (dma_status != SL_STATUS_OK) {
      return dma_status;
    }

    dma_status = sl_dma_channel_init(&rx_dma_handle, SL_PERIPHERAL_LDMA0, rx_dma_channel_nbr, sli_dma_callback, NULL);
    if (dma_status != SL_STATUS_OK) {
      return dma_status;
    }
    dma_status = sl_dma_channel_init(&tx_dma_handle, SL_PERIPHERAL_LDMA0, tx_dma_channel_nbr, NULL, NULL);
    if (dma_status != SL_STATUS_OK) {
      return dma_status;
    }

    dma_status = sl_dma_channel_set_peripheral_signal(&tx_dma_handle, SPI_EUSART_LDMA_TX);
    if (dma_status != SL_STATUS_OK) {
      return dma_status;
    }
    dma_status = sl_dma_channel_set_peripheral_signal(&rx_dma_handle, SPI_EUSART_LDMA_RX);
    if (dma_status != SL_STATUS_OK) {
      return dma_status;
    }

    // Start reset line low
    GPIO_PinModeSet(RESET_PIN.port, RESET_PIN.pin, gpioModeWiredAnd, 0);

    // configure packet pending interrupt priority
    NVIC_SetPriority(GPIO_ODD_IRQn, PACKET_PENDING_INT_PRI);

    // Configure interrupt, sleep and wake confirmation pins
    GPIOINT_CallbackRegister(INTERRUPT_PIN.pin, gpio_interrupt);
    GPIO_PinModeSet(INTERRUPT_PIN.port, INTERRUPT_PIN.pin, gpioModeInputPullFilter, 0);
    GPIO_ExtIntConfig(INTERRUPT_PIN.port, INTERRUPT_PIN.pin, INTERRUPT_PIN.pin, true, false, true);
    GPIO_PinModeSet(SLEEP_CONFIRM_PIN.port, SLEEP_CONFIRM_PIN.pin, gpioModeWiredOrPullDown, 1);
    GPIO_PinModeSet(WAKE_INDICATOR_PIN.port, WAKE_INDICATOR_PIN.pin, gpioModeWiredOrPullDown, 0);
    host_initialized = 1;
  } else {
    if (spi_transfer_mutex == NULL) {
      spi_transfer_mutex = osMutexNew(NULL);
    }
  }

  return SL_STATUS_OK;
}

sl_status_t sl_si91x_host_deinit(void)
{
  if (spi_transfer_mutex != NULL) {
    osMutexDelete(spi_transfer_mutex);
    spi_transfer_mutex = NULL;
  }
  return SL_STATUS_OK;
}

void sl_si91x_host_enable_high_speed_bus()
{
}

__WEAK void sl_si91x_host_spi_cs_assert()
{
  EUSART_BaudrateSet(SPI_EUSART, 0, SLI_SPI_BIT_RATE);
  GPIO_PinOutClear(SPI_CS_PIN.port, SPI_CS_PIN.pin);
}

__WEAK void sl_si91x_host_spi_cs_deassert()
{
  GPIO_PinOutSet(SPI_CS_PIN.port, SPI_CS_PIN.pin);
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
sl_status_t sl_si91x_host_spi_transfer(const void *tx_buffer, void *rx_buffer, uint16_t buffer_length)
{
  sl_status_t dma_status = SL_STATUS_OK;
  if ((host_initialized == 0U) || (spi_transfer_mutex == NULL) || (transfer_done_semaphore == NULL)) {
    return SL_STATUS_NOT_INITIALIZED;
  }
  if (((tx_buffer == NULL) || (rx_buffer == NULL)) && (buffer_length > SLI_NCP_SPI_MAX_TRANSFER_LENGTH)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  osMutexAcquire(spi_transfer_mutex, 0xFFFFFFFFUL);
  spi_dma_transfer_error = false;
  while (osSemaphoreAcquire(transfer_done_semaphore, 0) == osOK) {
  }

  // Substitute a dummy buffer when the caller does not supply one. The DMA
  // Driver 2.0 m2p/p2m helpers always increment the memory side, so we need
  // a backing buffer at least buffer_length bytes long.
  const void *tx = (tx_buffer != NULL) ? tx_buffer : (const void *)dummy_buffer;
  void *rx       = (rx_buffer != NULL) ? rx_buffer : (void *)dummy_buffer;

  // Submit RX first so the receive path is armed before TX starts clocking
  // bytes onto the bus. The new driver auto-segments transfers larger than
  // a single descriptor, so no manual descriptor chaining is required.
  dma_status = sl_dma_channel_submit_transfer_p2m(&rx_dma_handle,
                                                  (void *)&(SPI_EUSART->RXDATA),
                                                  rx,
                                                  buffer_length,
                                                  SL_DMA_CTRL_SIZE_BYTE,
                                                  NULL);
  if (dma_status != SL_STATUS_OK) {
    osMutexRelease(spi_transfer_mutex);
    return dma_status;
  }

  dma_status = sl_dma_channel_submit_transfer_m2p(&tx_dma_handle,
                                                  (void *)tx,
                                                  (void *)&(SPI_EUSART->TXDATA),
                                                  buffer_length,
                                                  SL_DMA_CTRL_SIZE_BYTE,
                                                  NULL);
  if (dma_status != SL_STATUS_OK) {
    (void)sl_dma_channel_abort(&rx_dma_handle);
    osMutexRelease(spi_transfer_mutex);
    return dma_status;
  }

  // Wait for RX completion (signaled by sli_dma_callback). The TX side
  // necessarily finishes first, so RX completion implies the full transfer
  // has clocked through.
  if (osSemaphoreAcquire(transfer_done_semaphore, SLI_SPI_TRANSFER_TIMEOUT_MS) != osOK) {
    osMutexRelease(spi_transfer_mutex);
    return SL_STATUS_BUS_ERROR;
  }
  if (spi_dma_transfer_error) {
    osMutexRelease(spi_transfer_mutex);
    return SL_STATUS_FAIL;
  }

  osMutexRelease(spi_transfer_mutex);

  return SL_STATUS_OK;
}

void sl_si91x_host_hold_in_reset(void)
{
  GPIO_PinOutClear(RESET_PIN.port, RESET_PIN.pin);
}

void sl_si91x_host_release_from_reset(void)
{
  GPIO_PinOutSet(RESET_PIN.port, RESET_PIN.pin);
}

void sl_si91x_host_enable_bus_interrupt(void)
{
  NVIC_EnableIRQ(GPIO_ODD_IRQn);
}

void sl_si91x_host_disable_bus_interrupt(void)
{
  NVIC_DisableIRQ(GPIO_ODD_IRQn);
}

bool sl_si91x_host_is_in_irq_context(void)
{
  return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0U;
}
