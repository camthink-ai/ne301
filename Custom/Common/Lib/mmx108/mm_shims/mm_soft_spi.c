/*
 * Copyright 2025 Morse Micro (porting layer)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Software SPI per MM-APPNOTE-51 §3.3 (Mode 0, asymmetric phase delays).
 *
 * Delays are NOP loop counts (not CPU cycles). On NE301, symmetric
 * MM_SOFT_SPI_DELAY_LOOPS=100 per half-bit was stable (~0.9 MHz @ 800 MHz CPU).
 */

#include "mm_soft_spi.h"

#include "main.h"
#include "stm32n6xx_hal.h"

/*
 * Per-bit NOP loop counts (MM-APPNOTE-51 §3.3):
 *   MOSI --[SETUP/tISU]--> SCLK↑ --[SAMPLE/tODLY]--> read MISO --> SCLK↓ --[LOW]-->
 * NE301: keep SAMPLE_LOOPS at ~100; reduce SETUP/LOW only when tuning for speed.
 */
#ifndef MM_SOFT_SPI_SETUP_LOOPS
#define MM_SOFT_SPI_SETUP_LOOPS  (5U)
#endif
#ifndef MM_SOFT_SPI_SAMPLE_LOOPS
#define MM_SOFT_SPI_SAMPLE_LOOPS (5U)
#endif
#ifndef MM_SOFT_SPI_LOW_LOOPS
#define MM_SOFT_SPI_LOW_LOOPS    (00U)
#endif

#define MM_SOFT_SPI_CLK_HIGH() \
    (MM_HALOW_SPI_CLK_GPIO_Port->BSRR = (uint32_t)MM_HALOW_SPI_CLK_Pin)

#define MM_SOFT_SPI_CLK_LOW() \
    (MM_HALOW_SPI_CLK_GPIO_Port->BSRR = (uint32_t)MM_HALOW_SPI_CLK_Pin << 16U)

#define MM_SOFT_SPI_MOSI_HIGH() \
    (MM_HALOW_SPI_MOSI_GPIO_Port->BSRR = (uint32_t)MM_HALOW_SPI_MOSI_Pin)

#define MM_SOFT_SPI_MOSI_LOW() \
    (MM_HALOW_SPI_MOSI_GPIO_Port->BSRR = (uint32_t)MM_HALOW_SPI_MOSI_Pin << 16U)

#define MM_SOFT_SPI_MISO_READ() \
    ((MM_HALOW_SPI_MISO_GPIO_Port->IDR & MM_HALOW_SPI_MISO_Pin) != 0U)

static void mm_soft_spi_delay_loops(uint32_t loops)
{
    volatile uint32_t n = loops;

    while (n-- != 0U)
    {
        __NOP();
    }
}

static uint32_t mm_soft_spi_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

static void mm_soft_spi_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t mm_soft_spi_transfer_bit(uint8_t mosi_bit, uint8_t rx_accum, int bit_index)
{
    if (mosi_bit != 0U)
    {
        MM_SOFT_SPI_MOSI_HIGH();
    }
    else
    {
        MM_SOFT_SPI_MOSI_LOW();
    }

    mm_soft_spi_delay_loops(MM_SOFT_SPI_SETUP_LOOPS);
    
    if (MM_SOFT_SPI_MISO_READ())
    {
        rx_accum |= (uint8_t)(1U << bit_index);
    }

    MM_SOFT_SPI_CLK_HIGH();

    mm_soft_spi_delay_loops(MM_SOFT_SPI_SAMPLE_LOOPS);
    MM_SOFT_SPI_CLK_LOW();

#if MM_SOFT_SPI_LOW_LOOPS > 0
    mm_soft_spi_delay_loops(MM_SOFT_SPI_LOW_LOOPS);
#endif
    return rx_accum;
}

void mm_soft_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_DeInit(MM_HALOW_SPI_CLK_GPIO_Port, MM_HALOW_SPI_CLK_Pin);
    HAL_GPIO_DeInit(MM_HALOW_SPI_MOSI_GPIO_Port, MM_HALOW_SPI_MOSI_Pin);
    HAL_GPIO_DeInit(MM_HALOW_SPI_MISO_GPIO_Port, MM_HALOW_SPI_MISO_Pin);
    HAL_GPIO_DeInit(MM_HALOW_SPI_CS_GPIO_Port, MM_HALOW_SPI_CS_Pin);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio.Pin = MM_HALOW_SPI_CLK_Pin | MM_HALOW_SPI_MOSI_Pin;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = MM_HALOW_SPI_CS_Pin;
    HAL_GPIO_Init(MM_HALOW_SPI_CS_GPIO_Port, &gpio);

    gpio.Pin = MM_HALOW_SPI_MISO_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MM_HALOW_SPI_MISO_GPIO_Port, &gpio);

    MM_SOFT_SPI_CLK_LOW();
    MM_SOFT_SPI_MOSI_HIGH();
    HAL_GPIO_WritePin(MM_HALOW_SPI_CS_GPIO_Port, MM_HALOW_SPI_CS_Pin, GPIO_PIN_SET);
}

uint8_t mm_soft_spi_transfer_byte(uint8_t tx)
{
    uint8_t rx = 0;
    const uint32_t primask = mm_soft_spi_enter_critical();

    for (int bit = 7; bit >= 0; bit--)
    {
        const uint8_t mosi_bit = (tx & (uint8_t)(1U << bit)) != 0U ? 1U : 0U;

        rx = mm_soft_spi_transfer_bit(mosi_bit, rx, bit);
    }

    mm_soft_spi_exit_critical(primask);
    return rx;
}

void mm_soft_spi_write_buf(const uint8_t *tx, unsigned len)
{
    const uint32_t primask = mm_soft_spi_enter_critical();

    for (unsigned i = 0; i < len; i++)
    {
        const uint8_t byte = (tx != NULL) ? tx[i] : 0xFFU;

        for (int bit = 7; bit >= 0; bit--)
        {
            const uint8_t mosi_bit = (byte & (uint8_t)(1U << bit)) != 0U ? 1U : 0U;

            (void)mm_soft_spi_transfer_bit(mosi_bit, 0, bit);
        }
    }

    mm_soft_spi_exit_critical(primask);
}

void mm_soft_spi_read_buf(uint8_t *rx, unsigned len)
{
    const uint32_t primask = mm_soft_spi_enter_critical();

    MM_SOFT_SPI_MOSI_HIGH();

    for (unsigned i = 0; i < len; i++)
    {
        uint8_t byte = 0;

        for (int bit = 7; bit >= 0; bit--)
        {
            byte = mm_soft_spi_transfer_bit(1U, byte, bit);
        }

        rx[i] = byte;
    }

    mm_soft_spi_exit_critical(primask);
}
