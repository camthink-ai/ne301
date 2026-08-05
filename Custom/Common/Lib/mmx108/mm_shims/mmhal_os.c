/*
 * Copyright 2021-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mm_hal_common.h"
#include "mmhal_os.h"
#include "mmosal.h"
#include "mmutils.h"
#include "main.h"

/**
 * Minimum time in milliseconds required to enter deep sleep. The task that takes the most amount of
 * time is synchronizing values between the APB clock domain and the kernel clock domain in the
 * LPTIM. This takes ~2-3 clock cycles of the LPTIM.
 */
#define MIN_DEEP_SLEEP_TIME_MS (5)

/**
 * Maximum time allowed to be in deep sleep. This is set so that we do not set a value in the
 * LPTIM_CMP register that is greater than or equal to the LPTIM_ARR register with enough
 * margin for delays.
 */
#define MAX_POSSIBLE_SUPPRESSED_TICKS (64000)

/** Ticks per second, dependent of the LPTIM clock source. */
#define LPTIM_TICKS_PER_SECOND 8192

/** Macro to transform milliseconds into LPTIM ticks. */
#define MS_TO_LPTIM_TICKS(x) (((x) * LPTIM_TICKS_PER_SECOND) / 1000)

/** Macro to transform LPTIM ticks into milliseconds.  */
#define LPTIM_TICKS_TO_MS(x) (((x) * 1000) / LPTIM_TICKS_PER_SECOND)

const uint32_t mmhal_system_clock = 160000000;

void SystemClock_Config(void);

void mmhal_early_init(void)
{
    mmosal_disable_interrupts();
}

void mmhal_init(void)
{
    mmosal_enable_interrupts();
    mmhal_random_init();
}

enum mmhal_isr_state mmhal_get_isr_state(void)
{
    if (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk)
    {
        return MMHAL_IN_ISR;
    }
    else
    {
        return MMHAL_NOT_IN_ISR;
    }
}

/* Log output via UART. Included by default, unless DISABLE_UART_LOG is defined. */
#ifndef DISABLE_UART_LOG
#if 0
/** Size of the debug log buffer used by mmhal_log_write(). */
#define LOG_BUF_SIZE (512)

/**
 * Debug log buffer data structure. This is a simple circular buffer where empty is indicated
 * by (wr_idx == rd_idx) and full by (wr_idx == rd_idx-1) [mod LOG_BUF_SIZE].
 */
struct log_buf
{
    /** The place where the data goes. */
    uint8_t buf[LOG_BUF_SIZE];
    /** Write index into buf. */
    volatile size_t wr_idx;
    /** Read index into buf. */
    volatile size_t rd_idx;
    /** Flag to indicate if we've already sent a '\r' for the current '\n' */
    volatile bool newline_converted;
};

/** Log buffer instance. */
static struct log_buf log_buf = { 0 };

void LOG_USART_IRQ_HANDLER(void)
{

}

static void mmhal_log_flush_uart(void)
{

}

static void mmhal_log_write_uart(const uint8_t *data, size_t length)
{
    while (length > 0)
    {
        size_t rd_idx = log_buf.rd_idx;
        size_t wr_idx = log_buf.wr_idx;

        /* Calculate maximum length we can copy in one go. */
        size_t space;
        if (wr_idx < rd_idx)
        {
            space = rd_idx - wr_idx - 1;
        }
        else
        {
            space = LOG_BUF_SIZE - wr_idx;
            /* Constrain to size-1 so we don't wrap. */
            if (!rd_idx)
            {
                space--;
            }
        }

        MMOSAL_ASSERT(space < LOG_BUF_SIZE);

        size_t copy_len = space < length ? space : length;
        memcpy(log_buf.buf + log_buf.wr_idx, data, copy_len);
        log_buf.wr_idx += copy_len;
        if (log_buf.wr_idx >= LOG_BUF_SIZE)
        {
            log_buf.wr_idx = 0;
        }
        data += copy_len;
        length -= copy_len;

        /* Enable TX empty interrupt. */
        //LL_USART_EnableIT_TXE_TXFNF(LOG_USART);
        //NVIC_EnableIRQ(LOG_USART_IRQ);
    }
}
#endif
#else

static void mmhal_log_write_uart(const uint8_t *data, size_t length)
{
    /* UART logging is disabled... send the message to a black hole. */
    MM_UNUSED(data);
    MM_UNUSED(length);
}

static void mmhal_log_flush_uart(void)
{
    /* UART logging is disabled... no action required. */
}

#endif

/* Log output via ITM/SWO. Only enabled if ENABLE_ITM_LOG is defined. */
#ifdef ENABLE_ITM_LOG

static void mmhal_log_write_itm(const uint8_t *data, size_t length)
{
    while (length-- > 0)
    {
        ITM_SendChar(*data++);
    }
}

#else

#if 0
/* ITM/SWO logging is disabled... send the message to a black hole. */
static void mmhal_log_write_itm(const uint8_t *data, size_t length)
{
    MM_UNUSED(data);
    MM_UNUSED(length);
}
#endif
#endif

extern UART_HandleTypeDef huart2;
void mmhal_log_write(const uint8_t *data, size_t length)
{
    //mmhal_log_write_uart(data, length);
    //mmhal_log_write_itm(data, length);
    // HAL_UART_Transmit(&huart2, (uint8_t*)data, length, 200);
}

void mmhal_log_flush(void)
{
    //mmhal_log_flush_uart();
}

void mmhal_reset(void)
{
    HAL_NVIC_SystemReset();
    while (1)
    {
    }
}

void LPTIM1_IRQHandler(void)
{

}

void mmhal_sleep_abort(enum mmhal_sleep_state sleep_state)
{
    if (sleep_state == MMHAL_SLEEP_DEEP)
    {
        /* Restart the SysTick */
        SysTick->CTRL |= (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);
        HAL_ResumeTick();
    }

    /* Re-enable interrupts */
    __enable_irq();
}

enum mmhal_sleep_state mmhal_sleep_prepare(uint32_t expected_idle_time_ms)
{
	return MMHAL_SLEEP_DISABLED;
}

extern RNG_HandleTypeDef hrng;

uint32_t mmhal_sleep(enum mmhal_sleep_state sleep_state, uint32_t expected_idle_time_ms)
{
    uint32_t elapsed_ms = 0;

    return elapsed_ms;
}

void mmhal_sleep_cleanup(void)
{
    /* Re-enable interrupts */
    __enable_irq();
}
