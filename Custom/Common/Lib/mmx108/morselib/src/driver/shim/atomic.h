/*
 * Copyright 2021 Morse Micro
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include "driver_types.h"
#include "stdatomic.h"

#ifdef __STDC_NO_ATOMICS__
#error Compiler does not support atomics
#endif



static inline unsigned long atomic_test_bit(
    unsigned bit_num,
    volatile atomic_ulong *var)
{
    return (*var & (1ul << bit_num));
}

static inline unsigned long atomic_test_and_set_bit_lock(
    unsigned bit_num,
    volatile atomic_ulong *var)
{
    unsigned long mask = (1ul << bit_num);
    return atomic_fetch_or(var, mask) & mask;
}

static inline unsigned long atomic_test_and_clear_bit(
    unsigned bit_num,
    volatile atomic_ulong *var)
{
    unsigned long mask = (1ul << bit_num);
    return atomic_fetch_and(var, ~mask) & mask;
}

static inline unsigned long atomic_test_and_clear_bit_no_mask(
    unsigned bit_num,
    volatile atomic_ulong *var)
{
    unsigned long mask = (1ul << bit_num);
    return atomic_fetch_and(var, ~mask);
}

static inline void atomic_set_bit(
    unsigned bit_num,
    volatile atomic_ulong *var)
{
    unsigned long mask = (1ul << bit_num);
    atomic_fetch_or(var, mask);
}

static inline void atomic_clear_bit_unlock(
    unsigned bit_num,
    volatile atomic_ulong *var)
{
    unsigned long mask = (1ul << bit_num);
    *var &= ~mask;
}



#ifdef ENABLE_SPINLOCK_TRACE
#include "mmtrace.h"
extern mmtrace_channel spinlock_channel_handle;
#define SPINLOCK_TRACE_DECLARE    mmtrace_channel spinlock_channel_handle;
#define SPINLOCK_TRACE_INIT()     spinlock_channel_handle = mmtrace_register_channel("spinlock")
#define SPINLOCK_TRACE(_fmt, ...) mmtrace_printf(spinlock_channel_handle, _fmt, ##__VA_ARGS__)
#else
#define SPINLOCK_TRACE_DECLARE
#define SPINLOCK_TRACE_INIT() \
    do {                      \
    } while (0)
#define SPINLOCK_TRACE(_fmt, ...) \
    do {                          \
    } while (0)
#endif

struct spinlock
{
    volatile atomic_uint locked;
};

static inline void spin_lock_init(struct spinlock *spinlock)
{
    SPINLOCK_TRACE_INIT();
    SPINLOCK_TRACE("init %x", (uint32_t)spinlock);
    spinlock->locked = 0;
}

static inline void spin_lock(struct spinlock *spinlock)
{
    while (atomic_fetch_or(&(spinlock->locked), 1))
    {

        mmosal_task_sleep(1);
    }
    SPINLOCK_TRACE("locked %x", (uint32_t)spinlock);
}

static inline void spin_unlock(struct spinlock *spinlock)
{
    SPINLOCK_TRACE("unlocked %x", (uint32_t)spinlock);
    spinlock->locked = 0;
}
