/*
 * This file provides implementations of C standard library functions (such as printf and puts)
 * for log output.
 *
 * Copyright 2021-2022 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// #include <stdarg.h>
#include <stdio.h>
#include "mmlog.h"
#include "mmosal.h"
#include "mmhal_os.h"
#include "mmhal_app.h"
#include "mmutils.h"

/** Timeout when  acquiring log mutex (in milliseconds). */
#define LOG_TIMEOUT_MS (5000)

/** Mutex to ensure only one thread at a time writes to the log. */
static struct mmosal_mutex *log_mutex = NULL;

static bool morse_debug_mutex_take(void)
{
    if (log_mutex != NULL)
    {
        return mmosal_mutex_get(log_mutex, LOG_TIMEOUT_MS);
    }
    else
    {
        return true;
    }
}

static void morse_debug_mutex_release(void)
{
    if (log_mutex != NULL)
    {
        MMOSAL_MUTEX_RELEASE(log_mutex);
    }
}

void mm_logging_init(void)
{
    log_mutex = mmosal_mutex_create("log");
    MMOSAL_ASSERT(log_mutex != NULL);
}

/* Mutex must have been acquired before this function is invoked. */
// static int vprintf_protected(const char *fmt, va_list arg)
// {
//     static char buf[512];

//     int len = vsnprintf(buf, sizeof(buf), fmt, arg);
//     if (len < 0)
//     {
//         return len;
//     }

//     mmhal_log_write((const uint8_t *)buf, len);

//     return len;
// }

// int vprintf(const char *fmt, va_list arg)
// {
//     /* If it takes too long to get the mutex then we give up on this log message. */
//     bool ok = morse_debug_mutex_take();
//     if (!ok)
//     {
//         return -1;
//     }

//     int ret = vprintf_protected(fmt, arg);

//     morse_debug_mutex_release();

//     return ret;
// }

/* Mutex must have been acquired before this function is invoked. */
// static int printf_protected(const char *fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     vprintf_protected(fmt, args);
//     va_end(args);
//     return 0;
// }

// int printf(const char *fmt, ...)
// {
//     va_list args;
//     va_start(args, fmt);
//     vprintf(fmt, args);
//     va_end(args);
//     return 0;
// }

// int putchar(int c)
// {
//     unsigned char uc = c;

//     bool ok = morse_debug_mutex_take();
//     if (!ok)
//     {
//         return -1;
//     }

//     mmhal_log_write(&uc, 1);

//     morse_debug_mutex_release();
//     return uc;
// }

// int puts(const char *str)
// {
//     const uint8_t newline = '\n';
//     size_t len = strlen(str);

//     bool ok = morse_debug_mutex_take();
//     if (!ok)
//     {
//         return -1;
//     }

//     mmhal_log_write((const uint8_t *)str, len);
//     mmhal_log_write(&newline, 1);

//     morse_debug_mutex_release();
//     return len;
// }

int setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    MM_UNUSED(stream);
    MM_UNUSED(buffer);
    MM_UNUSED(mode);
    MM_UNUSED(size);
    return 0;
}

/** Maximum length of buffer to dump inline before going to multi-line mode */
#define DUMP_INLINE_MAXLEN    (8)
#define DUMP_OCTETS_PER_GROUP (8)
#define DUMP_OCTETS_PER_LINE  (16)

void mm_hexdump(char level,
                const char *function,
                unsigned line_number,
                const char *title,
                const uint8_t *buf,
                size_t len)
{
    /* If it takes too long to get the mutex then we give up on this log message. */
    bool ok = morse_debug_mutex_take();
    if (!ok)
    {
        return;
    }

    printf("%c %s %s[%d] %s", level, mmosal_task_name(), function, line_number, title);

    if (len <= DUMP_INLINE_MAXLEN)
    {
        while (len--)
        {
            printf(" %02x", *buf++);
        }
        printf("\n");
    }
    else
    {
        unsigned ii;
        for (ii = 0; ii < len; ii++)
        {
            if ((ii % DUMP_OCTETS_PER_LINE) == 0)
            {
                printf("\n");
            }
            else if ((ii % DUMP_OCTETS_PER_GROUP) == 0)
            {
                printf(" ");
            }

            printf(" %02x", buf[ii]);
        }
        printf("\n");
    }

    morse_debug_mutex_release();
}
