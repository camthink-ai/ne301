/*
 * Copyright 2021-2024 Morse Micro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdarg.h>
#include <time.h>

#include "mmhal_core.h"
#include "mmosal.h"
#include "hostap_morse_common.h"
#include "utils/os.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

void os_sleep(os_time_t sec, os_time_t usec)
{
    if (sec != 0)
    {
        mmosal_task_sleep(sec * 1000);
    }

    if (usec != 0)
    {
        mmosal_task_sleep(usec / 1000);
    }
}

int os_get_time(struct os_time *t)
{
    uint32_t now = mmosal_get_time_ms();
    t->sec = now / 1000;
    t->usec = now % 1000;
    return 0;
}

int os_get_reltime(struct os_reltime *t)
{
    uint32_t now = mmosal_get_time_ms();
    t->sec = now / 1000;
    t->usec = now % 1000;
    return 0;
}

/*
 * SPDX-SnippetBegin
 * SPDX-License-Identifier: BSD
 * SPDX-SnippetCopyrightText: Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 * SDPX—SnippetName: Functions from os_internal.c
 */
int os_mktime(int year, int month, int day, int hour, int min, int sec, os_time_t *t)
{
    struct tm tm;

    if ((year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) ||
        (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60))
    {
        return -1;
    }

    os_memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    *t = (os_time_t)mktime(&tm);
    return 0;
}

int os_gmtime(os_time_t t, struct os_tm *tm)
{
    struct tm *tm2;
    time_t t2 = t;

    tm2 = gmtime(&t2);
    if (tm2 == NULL)
    {
        return -1;
    }
    tm->sec = tm2->tm_sec;
    tm->min = tm2->tm_min;
    tm->hour = tm2->tm_hour;
    tm->day = tm2->tm_mday;
    tm->month = tm2->tm_mon + 1;
    tm->year = tm2->tm_year + 1900;
    return 0;
}

/*
 *  SPDX-SnippetEnd
 */

int os_daemonize(const char *pid_file)
{
    UNUSED(pid_file);

    return -1;
}

void os_daemonize_terminate(const char *pid_file)
{
    UNUSED(pid_file);
}

int os_get_random(unsigned char *buf, size_t len)
{
    unsigned char *buf_end;
    uint32_t data;

    for (buf_end = buf + len; buf < buf_end; buf += sizeof(data))
    {
        size_t remaining = buf_end - buf;
        data = mmhal_random_u32(0, UINT32_MAX);
        if (remaining > sizeof(data))
        {
            remaining = sizeof(data);
        }
        memcpy(buf, &data, remaining);
    }
    return 0;
}

unsigned long os_random(void) // NOLINT(runtime/int) -- we are just confirming to API
{
    return mmhal_random_u32(0, UINT32_MAX);
}

char *os_rel2abs_path(const char *rel_path)
{
    UNUSED(rel_path);

    return NULL;
}

int os_program_init(void)
{
    return 0;
}

void os_program_deinit(void)
{
}

int os_setenv(const char *name, const char *value, int overwrite)
{
    UNUSED(name);
    UNUSED(value);
    UNUSED(overwrite);

    return -1;
}

int os_unsetenv(const char *name)
{
    UNUSED(name);

    return -1;
}

char *os_readfile(const char *name, size_t *len)
{
    UNUSED(name);
    UNUSED(len);

    return NULL;
}

void *os_zalloc(size_t size)
{
    void *p = os_malloc(size);
    if (p != NULL)
    {
        memset(p, 0, size);
    }
    return p;
}

size_t os_strlcpy(char *dest, const char *src, size_t siz)
{
    const char *siter = src;
    char *diter = dest;

    while ((diter - dest) < (int)(siz - 1))
    {
        *diter = *siter;
        if (*siter == '\0')
        {
            break;
        }
        diter++;
        siter++;
    }

    if (siz != 0 && (diter - dest) == (int)(siz - 1))
    {
        *diter = '\0';
    }

    while (*siter++ != '\0')
    {
    }

    return siter - src - 1;
}

int os_memcmp_const(const void *a, const void *b, size_t len)
{
    return memcmp(a, b, len);
}

void *os_memdup(const void *src, size_t len)
{
    void *r = os_malloc(len);
    if (r != NULL)
    {
        os_memcpy(r, src, len);
    }
    return r;
}

#ifdef OS_NO_C_LIB_DEFINES
void *os_malloc(size_t size)
{
    return mmosal_malloc(size);
}

void *os_realloc(void *ptr, size_t size)
{
    return mmosal_realloc(ptr, size);
}

void os_free(void *ptr)
{
    mmosal_free(ptr);
}

void *os_memcpy(void *dest, const void *src, size_t n)
{
    return memcpy(dest, src, n);
}

void *os_memmove(void *dest, const void *src, size_t n)
{
    return memmove(dest, src, n);
}

void *os_memset(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

int os_memcmp(const void *s1, const void *s2, size_t n)
{
    return memcmp(s1, s2, n);
}

char *os_strdup(const char *s)
{
    /* Do not use stdlib strdup or we run into issues with it using internal stdlib malloc instead
     * of mmosal_malloc(). */
    size_t len = strlen(s);
    char *buf = (char *)mmosal_malloc(len + 1);
    if (buf == NULL)
    {
        return NULL;
    }

    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

size_t os_strlen(const char *s)
{
    return strlen(s);
}

int os_strcasecmp(const char *s1, const char *s2)
{
    /*
     * Ignoring case is not required for main functionality, so just use
     * the case sensitive version of the function.
     */
    return os_strcmp(s1, s2);
}

int os_strncasecmp(const char *s1, const char *s2, size_t n)
{
    /*
     * Ignoring case is not required for main functionality, so just use
     * the case sensitive version of the function.
     */
    return os_strncmp(s1, s2, n);
}

char *os_strchr(const char *s, int c)
{
    return strchr(s, c);
}

char *os_strrchr(const char *s, int c)
{
    return strrchr(s, c);
}

int os_strcmp(const char *s1, const char *s2)
{
    return strcmp(s1, s2);
}

int os_strncmp(const char *s1, const char *s2, size_t n)
{
    return strncmp(s1, s2, n);
}

char *os_strncpy(char *dest, const char *src, size_t n)
{
    return strncpy(dest, src, n);
}

char *os_strstr(const char *haystack, const char *needle)
{
    return strstr(haystack, needle);
}

int os_snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

#endif /* OS_NO_C_LIB_DEFINES */
