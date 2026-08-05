/**
 * Copyright 2024-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 * @file
 * This File implements platform specific shims for accessing the data-link transport.
 */

#include "mm_hal_common.h"
#include "mmhal_uart.h"
#include "mmutils.h"
#include "main.h"

#ifndef ENABLE_UART_HAL
#define ENABLE_UART_HAL (0)
#endif

#if ENABLE_UART_HAL

/*
 * The implementation of the UART HAL contained herein uses the same UART as the log UART.
 * Therefore log output via UART must be disabled to use this HAL. The reason the same UART
 * is used is that on this platform this is the UART that is connected to the ST-Link. However,
 * if this HAL were to be modified to use another UART then both could be used simultaneously.
 */
#ifndef DISABLE_UART_LOG
#error "Unable to build UART HAL with UART logging enabled. Please define DISABLE_UART_LOG"
#endif

#define UART_INTERFACE   LOG_USART
#define UART_IRQ_HANDLER LOG_USART_IRQ_HANDLER

/** Size of the receive ring buffer. */
#define RX_RINGBUF_SIZE (128)
/** Add the given value to a RX ringbuf index, taking into account wrapping. */
#define RX_RINGBUF_IDX_ADD(_x, _y) (((_x) + (_y)) % RX_RINGBUF_SIZE)

/** RX thread stack size in 32 bit words */
#define RX_THREAD_STACK_SIZE_WORDS (768)
/** RX thread priority */
#define RX_THREAD_PRIORITY (MMOSAL_TASK_PRI_NORM)

/** Data structure UART HAL global state. */
struct mmhal_uart_data
{
    mmhal_uart_rx_cb_t rx_cb;
    void *rx_cb_arg;

    struct
    {
        struct mmosal_task *handle;
        struct mmosal_semb *semb;
        volatile bool run;
        volatile bool complete;
    } rx_thread;

    struct
    {
        uint8_t buf[RX_RINGBUF_SIZE];
        volatile size_t write_idx;
        volatile size_t read_idx;
    } rx_ringbuf;
};

static struct mmhal_uart_data mmhal_uart;

/**
 * Process data in the RX ringbuffer and invoke the callback if there is any data available.
 *
 * @return @c true if there might still be more data in the ringbuf; @c false if all data that
 *         was in the ringbuf at the time this function was called has been processed.
 */
static bool uart_rx_process(void)
{
    size_t snapshot_rx_rd_idx;
    size_t snapshot_rx_wr_idx;
    size_t new_rd_idx;
    ssize_t rx_available;

    snapshot_rx_rd_idx = mmhal_uart.rx_ringbuf.read_idx;
    snapshot_rx_wr_idx = mmhal_uart.rx_ringbuf.write_idx;
    rx_available = snapshot_rx_wr_idx - snapshot_rx_rd_idx;

    if (rx_available != 0)
    {
        /* Account for wrapping */
        if (rx_available < 0)
        {
            rx_available = sizeof(mmhal_uart.rx_ringbuf.buf) - snapshot_rx_rd_idx;
            new_rd_idx = 0;
        }
        else
        {
            new_rd_idx = snapshot_rx_rd_idx + rx_available;
        }

        if (mmhal_uart.rx_cb)
        {
            mmhal_uart.rx_cb(&mmhal_uart.rx_ringbuf.buf[snapshot_rx_rd_idx],
                             rx_available,
                             mmhal_uart.rx_cb_arg);
        }

        mmhal_uart.rx_ringbuf.read_idx = new_rd_idx;
        return true;
    }
    else
    {
        return false;
    }
}

static void uart_rx_ringbuf_put(uint8_t c)
{
    size_t snapshot_rx_wr_idx = mmhal_uart.rx_ringbuf.write_idx;
    size_t snapshot_rx_rd_idx = mmhal_uart.rx_ringbuf.read_idx;
    size_t next_wr_idx = snapshot_rx_wr_idx + 1;
    if (next_wr_idx >= sizeof(mmhal_uart.rx_ringbuf.buf))
    {
        next_wr_idx = 0;
    }

    if (next_wr_idx == snapshot_rx_rd_idx)
    {
        /* RX buffer full; drop incoming data */
        return;
    }

    mmhal_uart.rx_ringbuf.buf[snapshot_rx_wr_idx] = c;
    mmhal_uart.rx_ringbuf.write_idx = next_wr_idx;
}

static void uart_rx_main(void *arg)
{
    MM_UNUSED(arg);

    while (mmhal_uart.rx_thread.run)
    {
        bool more_data_pending = uart_rx_process();
        if (!more_data_pending)
        {
            mmosal_semb_wait(mmhal_uart.rx_thread.semb, UINT32_MAX);
        }
    }

    mmhal_uart.rx_thread.complete = true;
}

void mmhal_uart_init(mmhal_uart_rx_cb_t rx_cb, void *rx_cb_arg)
{
    memset(&mmhal_uart, 0, sizeof(mmhal_uart));
    mmhal_uart.rx_cb = rx_cb;
    mmhal_uart.rx_cb_arg = rx_cb_arg;

    mmhal_uart.rx_thread.semb = mmosal_semb_create("uartsemb");
    MMOSAL_ASSERT(mmhal_uart.rx_thread.semb != NULL);

    mmhal_uart.rx_thread.run = true;
    mmhal_uart.rx_thread.handle = mmosal_task_create(uart_rx_main,
                                                     NULL,
                                                     RX_THREAD_PRIORITY,
                                                     RX_THREAD_STACK_SIZE_WORDS,
                                                     "uart");

    /* Enable RX interrupts */
    LL_USART_ClearFlag_ORE(UART_INTERFACE);
    LL_USART_EnableIT_RXNE(UART_INTERFACE);
}

void mmhal_uart_deinit(void)
{
    if (mmhal_uart.rx_thread.handle != NULL)
    {
        mmhal_uart.rx_thread.run = false;
        mmosal_semb_give(mmhal_uart.rx_thread.semb);
        while (!mmhal_uart.rx_thread.complete)
        {
            mmosal_task_sleep(3);
        }
        mmhal_uart.rx_thread.handle = NULL;
    }

    if (mmhal_uart.rx_thread.semb != NULL)
    {
        mmosal_semb_delete(mmhal_uart.rx_thread.semb);
        mmhal_uart.rx_thread.semb = NULL;
    }

    LL_USART_DisableIT_RXNE(UART_INTERFACE);
}

void mmhal_uart_tx(const uint8_t *tx_data, size_t length)
{
    while (length-- != 0)
    {
        while (!LL_USART_IsActiveFlag_TXE(UART_INTERFACE))
        {
        }
        LL_USART_TransmitData8(UART_INTERFACE, *tx_data++);
    }
}

bool mmhal_uart_set_deep_sleep_mode(enum mmhal_uart_deep_sleep_mode mode)
{
    if (mode == MMHAL_UART_DEEP_SLEEP_ONE_SHOT)
    {
        /* Setup USART_RX line as interrupt */
        LL_EXTI_InitTypeDef EXTI_InitStruct = { 0 };
        LL_EXTI_SetEXTISource(LOG_USART_RX_EXTI_Port, LOG_USART_RX_EXTI_Line);
        EXTI_InitStruct.Line_0_31 = LOG_USART_RX_IRQ_LINE;
        EXTI_InitStruct.LineCommand = ENABLE;
        EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
        EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_FALLING;
        LL_EXTI_Init(&EXTI_InitStruct);
        NVIC_SetPriority(LOG_USART_RX_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));
        LL_EXTI_ClearFallingFlag_0_31(LOG_USART_RX_IRQ_LINE);
        NVIC_EnableIRQ(LOG_USART_RX_IRQn);
        mmhal_clear_deep_sleep_veto(MMHAL_VETO_ID_HAL_UART);
        return true;
    }
    else if (mode == MMHAL_UART_DEEP_SLEEP_DISABLED)
    {
        mmhal_set_deep_sleep_veto(MMHAL_VETO_ID_HAL_UART);
        NVIC_DisableIRQ(LOG_USART_RX_IRQn);
        LL_EXTI_ClearFallingFlag_0_31(LOG_USART_RX_IRQ_LINE);
        return true;
    }
    return false;
}

/**
 * This function handles UART RX falling edge interrupt.
 */
#ifndef LOG_USART_RX_IRQ_HANDLER
#error Must define alias macro
#endif
void LOG_USART_RX_IRQ_HANDLER(void)
{
    if (LL_EXTI_IsActiveFallingFlag_0_31(LOG_USART_RX_IRQ_LINE) != RESET)
    {
        mmhal_uart_set_deep_sleep_mode(MMHAL_UART_DEEP_SLEEP_DISABLED);
    }
}

void UART_IRQ_HANDLER(void)
{
    while (LL_USART_IsEnabledIT_RXNE(UART_INTERFACE) && LL_USART_IsActiveFlag_RXNE(UART_INTERFACE))
    {
        uart_rx_ringbuf_put(LL_USART_ReceiveData8(UART_INTERFACE));
        if (mmhal_uart.rx_thread.handle != NULL)
        {
            mmosal_semb_give_from_isr(mmhal_uart.rx_thread.semb);
        }
    }
}

#else

void mmhal_uart_init(mmhal_uart_rx_cb_t rx_cb, void *rx_cb_arg)
{
    MM_UNUSED(rx_cb);
    MM_UNUSED(rx_cb_arg);
    printf("UART HAL not supported as UART is in use for logs\n");
    printf("Please rebuild with DISABLE_UART_LOG defined\n");
}

void mmhal_uart_deinit(void)
{
}

void mmhal_uart_tx(const uint8_t *data, size_t length)
{
    MM_UNUSED(data);
    MM_UNUSED(length);
}

bool mmhal_uart_set_deep_sleep_mode(enum mmhal_uart_deep_sleep_mode mode)
{
    MM_UNUSED(mode);
    return false;
}

#endif
