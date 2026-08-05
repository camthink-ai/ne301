/*
 * Copyright 2021-2022 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define MMPORT_BREAKPOINT() __asm("bkpt 0\n\t")
#define MMPORT_GET_LR()     __builtin_return_address(0)
#define MMPORT_GET_PC(_a)   __asm volatile ("mov %0, pc" : "=r" (_a))
#define MMPORT_MEM_SYNC()   __sync_synchronize()
