/*
 * Copyright 2021-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HAL SPI6 port aligned with Morse mm-ekh08-h753 mmhal_wlan.c (LL reference):
 * https://github.com/MorseMicro/mm-iot-sdk/blob/b61d4548c55cfd927efe16bffb01b35509d9ef5a/framework/src/platforms/mm-ekh08-h753/mm_shims/mmhal_wlan.c
 */

#include <string.h>

#include "mmhal_wlan.h"
#include "mmosal.h"
#include "mmutils.h"

#include "main.h"
#include "cmsis_os2.h"
#include "common_utils.h"
#include "chip_id_mac.h"
#include "halow_platform_mac.h"

#if defined(MMHAL_WLAN_USE_SOFT_SPI)
#include "mm_soft_spi.h"
#else
#include "spi.h"
#include "stm32n6xx_hal_spi.h"
#endif

/* mm-ekh08-h753: 16 x 0xFF with CS high (>=74 clocks per SDIO spec) */
#define BYTE_TRAIN 16

MM_STATIC_ASSERT((BYTE_TRAIN >= 10), "BYTE_TRAIN must be at least 10.");

static struct mmosal_semb *dma_semb_handle;

#if !defined(MMHAL_WLAN_USE_SOFT_SPI)
extern SPI_HandleTypeDef hspi6;

#define MMHAL_SPI_POLL_TIMEOUT_MS (1000U)
#define DMA_WAIT_TMO_MS           (200U)
/* mm-ekh08-h753: use DMA for transfers >= 16 bytes */
#define DMA_TRANSFER_MIN_LENGTH   (16U)
#define SPI6_BUFFER_LENGTH        (2048U)

uint8_t spi6_tx_buffer[SPI6_BUFFER_LENGTH] ALIGN_32 UNCACHED;
uint8_t spi6_rx_buffer[SPI6_BUFFER_LENGTH] ALIGN_32 UNCACHED;

static void mmhal_spi_hw_abort_if_busy(void)
{
    if (hspi6.State != HAL_SPI_STATE_READY)
    {
        (void)HAL_SPI_Abort(&hspi6);
    }
}

/** One HAL full-duplex frame (TSIZE set once per call). */
static int mmhal_spi_hw_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (len == 0U)
    {
        return 0;
    }

    mmhal_spi_hw_abort_if_busy();

    if (HAL_SPI_TransmitReceive(&hspi6, tx, rx, len, MMHAL_SPI_POLL_TIMEOUT_MS) != HAL_OK)
    {
        (void)HAL_SPI_Abort(&hspi6);
        return -1;
    }
    return 0;
}

static int mmhal_spi_hw_xfer_dma(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    HAL_StatusTypeDef status;

    if (len == 0U)
    {
        return 0;
    }

    mmhal_spi_hw_abort_if_busy();

    status = HAL_SPI_TransmitReceive_DMA(&hspi6, tx, rx, len);
    if (status != HAL_OK)
    {
        (void)HAL_SPI_Abort(&hspi6);
        return -1;
    }

    if (dma_semb_handle == NULL || !mmosal_semb_wait(dma_semb_handle, DMA_WAIT_TMO_MS))
    {
        (void)HAL_SPI_Abort(&hspi6);
        return -1;
    }

    return 0;
}

static void mmhal_spi_hw_read_buf_dma(uint8_t *buf, unsigned len)
{
    if (mmhal_spi_hw_xfer_dma(spi6_tx_buffer, spi6_rx_buffer, (uint16_t)len) == 0)
    {
        memcpy(buf, spi6_rx_buffer, len);
    }
}

static void mmhal_spi_hw_write_buf_dma(const uint8_t *buf, unsigned len)
{
    memcpy(spi6_tx_buffer, buf, len);
    (void)mmhal_spi_hw_xfer_dma(spi6_tx_buffer, spi6_rx_buffer, (uint16_t)len);
}

/** Called from HAL_SPI_TxRxCpltCallback in spi.c when SPI6 DMA completes. */
void mmhal_wlan_hal_dma_complete_from_isr(void)
{
    if (dma_semb_handle != NULL)
    {
        (void)mmosal_semb_give_from_isr(dma_semb_handle);
    }
}
#endif

static mmhal_irq_handler_t spi_irq_handler = NULL;
static mmhal_irq_handler_t busy_irq_handler = NULL;

void mmhal_wlan_hard_reset(void)
{
    HAL_GPIO_WritePin(MM_HALOW_RESET_GPIO_Port, MM_HALOW_RESET_Pin, GPIO_PIN_RESET);
    /* MM-APPNOTE-51 §3.4 */
    mmosal_task_sleep(80);
    HAL_GPIO_WritePin(MM_HALOW_RESET_GPIO_Port, MM_HALOW_RESET_Pin, GPIO_PIN_SET);
    mmosal_task_sleep(200);
}

#if defined(ENABLE_EXT_XTAL_INIT) && ENABLE_EXT_XTAL_INIT
bool mmhal_wlan_ext_xtal_init_is_required(void)
{
    return true;
}
#endif

void mmhal_wlan_spi_cs_assert(void)
{
    HAL_GPIO_WritePin(MM_HALOW_SPI_CS_GPIO_Port, MM_HALOW_SPI_CS_Pin, GPIO_PIN_RESET);
}

void mmhal_wlan_spi_cs_deassert(void)
{
    HAL_GPIO_WritePin(MM_HALOW_SPI_CS_GPIO_Port, MM_HALOW_SPI_CS_Pin, GPIO_PIN_SET);
}

uint8_t mmhal_wlan_spi_rw(uint8_t data)
{
#if defined(MMHAL_WLAN_USE_SOFT_SPI)
    return mm_soft_spi_transfer_byte(data);
#else
    uint8_t rx = 0xFFU;
    (void)mmhal_spi_hw_xfer(&data, &rx, 1U);
    return rx;
#endif
}

void mmhal_wlan_spi_read_buf(uint8_t *buf, unsigned len)
{
#if defined(MMHAL_WLAN_USE_SOFT_SPI)
    mm_soft_spi_read_buf(buf, len);
#else
    while (len > 0U)
    {
        unsigned chunk = len;

        if (chunk > SPI6_BUFFER_LENGTH)
        {
            chunk = SPI6_BUFFER_LENGTH;
        }

        if (chunk < DMA_TRANSFER_MIN_LENGTH)
        {
            uint8_t tx_stash[DMA_TRANSFER_MIN_LENGTH];

            memset(tx_stash, 0xFF, chunk);
            (void)mmhal_spi_hw_xfer(tx_stash, buf, (uint16_t)chunk);
        }
        else
        {
            memset(spi6_tx_buffer, 0xFF, chunk);
            mmhal_spi_hw_read_buf_dma(buf, chunk);
        }

        buf += chunk;
        len -= chunk;
    }
#endif
}

void mmhal_wlan_spi_write_buf(const uint8_t *buf, unsigned len)
{
#if defined(MMHAL_WLAN_USE_SOFT_SPI)
    mm_soft_spi_write_buf(buf, len);
#else
    while (len > 0U)
    {
        unsigned chunk = len;

        if (chunk > SPI6_BUFFER_LENGTH)
        {
            chunk = SPI6_BUFFER_LENGTH;
        }

        if (chunk < DMA_TRANSFER_MIN_LENGTH)
        {
            uint8_t rx_stash[DMA_TRANSFER_MIN_LENGTH];

            (void)mmhal_spi_hw_xfer(buf, rx_stash, (uint16_t)chunk);
        }
        else
        {
            mmhal_spi_hw_write_buf_dma(buf, chunk);
        }

        buf += chunk;
        len -= chunk;
    }
#endif
}

void mmhal_wlan_send_training_seq(void)
{
#if defined(MMHAL_WLAN_USE_SOFT_SPI)
    uint8_t train_ff[BYTE_TRAIN];

    memset(train_ff, 0xFF, sizeof(train_ff));
    mmhal_wlan_spi_cs_deassert();
    mmhal_wlan_spi_write_buf(train_ff, BYTE_TRAIN);
#else
    uint8_t train_tx[BYTE_TRAIN];
    uint8_t train_rx[BYTE_TRAIN];

    mmhal_wlan_spi_cs_deassert();
    memset(train_tx, 0xFF, sizeof(train_tx));
    (void)mmhal_spi_hw_xfer(train_tx, train_rx, (uint16_t)sizeof(train_tx));
#endif
}

void mmhal_wlan_register_spi_irq_handler(mmhal_irq_handler_t handler)
{
    spi_irq_handler = handler;
}

bool mmhal_wlan_spi_irq_is_asserted(void)
{
    return HAL_GPIO_ReadPin(MM_HALOW_SPI_IRQ_GPIO_Port, MM_HALOW_SPI_IRQ_Pin) == GPIO_PIN_RESET;
}

void mmhal_wlan_set_spi_irq_enabled(bool enabled)
{
    if (enabled)
    {
        HAL_NVIC_SetPriority(MM_HALOW_SPI_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(MM_HALOW_SPI_IRQn);

        if (mmhal_wlan_spi_irq_is_asserted() && spi_irq_handler != NULL)
        {
            spi_irq_handler();
        }
    }
    else
    {
        HAL_NVIC_DisableIRQ(MM_HALOW_SPI_IRQn);
    }
}

void mmhal_wlan_init(void)
{
#if defined(MMHAL_WLAN_USE_SOFT_SPI)
    mm_soft_spi_init();
#endif
    if (dma_semb_handle != NULL) {
        mmosal_semb_delete(dma_semb_handle);
        dma_semb_handle = NULL;
    }
    dma_semb_handle = mmosal_semb_create("dma_semb_handle");
    HAL_GPIO_WritePin(MM_HALOW_RESET_GPIO_Port, MM_HALOW_RESET_Pin, GPIO_PIN_SET);
}

void mmhal_wlan_deinit(void)
{
    if (dma_semb_handle != NULL) {
        mmosal_semb_delete(dma_semb_handle);
        dma_semb_handle = NULL;
    }
}

void mmhal_wlan_wake_assert(void)
{
    HAL_GPIO_WritePin(MM_HALOW_WAKE_GPIO_Port, MM_HALOW_WAKE_Pin, GPIO_PIN_SET);
}

void mmhal_wlan_wake_deassert(void)
{
    HAL_GPIO_WritePin(MM_HALOW_WAKE_GPIO_Port, MM_HALOW_WAKE_Pin, GPIO_PIN_RESET);
}

bool mmhal_wlan_busy_is_asserted(void)
{
    return HAL_GPIO_ReadPin(MM_HALOW_BUSY_GPIO_Port, MM_HALOW_BUSY_Pin) == GPIO_PIN_SET;
}

void mmhal_wlan_register_busy_irq_handler(mmhal_irq_handler_t handler)
{
    busy_irq_handler = handler;
}

void mmhal_wlan_set_busy_irq_enabled(bool enabled)
{
    if (enabled)
    {
        HAL_NVIC_SetPriority(MM_HALOW_BUSY_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(MM_HALOW_BUSY_IRQn);

        if (mmhal_wlan_busy_is_asserted() && busy_irq_handler != NULL)
        {
            busy_irq_handler();
        }
    }
    else
    {
        HAL_NVIC_DisableIRQ(MM_HALOW_BUSY_IRQn);
    }
}

void MM_HALOW_BUSY_IRQ_HANDLER(void)
{
    HAL_GPIO_EXTI_IRQHandler(MM_HALOW_BUSY_Pin);

    if (busy_irq_handler != NULL)
    {
        busy_irq_handler();
    }
}

void MM_HALOW_SPI_IRQ_HANDLER(void)
{
    HAL_GPIO_EXTI_IRQHandler(MM_HALOW_SPI_IRQ_Pin);

    if (spi_irq_handler != NULL)
    {
        spi_irq_handler();
    }
}

static uint8_t s_platform_mac[6];
static uint8_t s_platform_mac_valid;

void mmhal_wlan_use_platform_mac(const uint8_t *mac)
{
    if (mac != NULL) {
        memcpy(s_platform_mac, mac, 6U);
        s_platform_mac_valid = 1U;
    } else {
        s_platform_mac_valid = 0U;
    }
}

void mmhal_read_mac_addr(uint8_t *mac_addr)
{
    if (s_platform_mac_valid) {
        memcpy(mac_addr, s_platform_mac, 6U);
        return;
    }
    netif_chip_id_get_mac(mac_addr, NETIF_CHIP_MAC_HALOW);
}

const struct mmhal_chip *mmhal_get_chip(void)
{
    return &mmhal_mm6108;
}

