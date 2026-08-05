/*
 * FreeRTOS Kernel V10.2.1
 * Copyright (C) 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "mmosal.h"

/**
 * Architecture specifics.
 */
#define portSTACK_GROWTH                   ( -1 )
#define portTICK_PERIOD_MS                 ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT                 8
#define portNOP()
#define portINLINE                         __inline
#ifndef portFORCE_INLINE
    #define portFORCE_INLINE               inline __attribute__( ( always_inline ) )
#endif
#define portHAS_STACK_OVERFLOW_CHECKING    1

#define configSUPPORT_DYNAMIC_ALLOCATION    1
#define configENABLE_BACKWARD_COMPATIBILITY 1

#if portBYTE_ALIGNMENT == 32
    #define portBYTE_ALIGNMENT_MASK    ( 0x001f )
#elif portBYTE_ALIGNMENT == 16
    #define portBYTE_ALIGNMENT_MASK    ( 0x000f )
#elif portBYTE_ALIGNMENT == 8
    #define portBYTE_ALIGNMENT_MASK    ( 0x0007 )
#elif portBYTE_ALIGNMENT == 4
    #define portBYTE_ALIGNMENT_MASK    ( 0x0003 )
#elif portBYTE_ALIGNMENT == 2
    #define portBYTE_ALIGNMENT_MASK    ( 0x0001 )
#elif portBYTE_ALIGNMENT == 1
    #define portBYTE_ALIGNMENT_MASK    ( 0x0000 )
#else /* if portBYTE_ALIGNMENT == 32 */
    #error "Invalid portBYTE_ALIGNMENT definition"
#endif /* if portBYTE_ALIGNMENT == 32 */

#define traceMALLOC( pvAddress, uiSize )
#define traceFREE( pvAddress, uiSize )

#define portYIELD() 								vPortYield()
#define portNVIC_INT_CTRL_REG	  ( *( ( volatile uint32_t * ) 0xe000ed04 ) )
#define portNVIC_PENDSVSET_BIT	  ( 1UL << 28UL )
#define portEND_SWITCHING_ISR( xSwitchRequired )	do { if( xSwitchRequired ) portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT; } while( 0 )
#define portYIELD_FROM_ISR( x ) 					portEND_SWITCHING_ISR( x )



/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

// extern const uint32_t mmhal_system_clock;
extern uint32_t SystemCoreClock;

// #define configCPU_CLOCK_HZ (mmhal_system_clock)
#define configCPU_CLOCK_HZ (SystemCoreClock)
#ifndef configUSE_TICKLESS_IDLE
#define configUSE_TICKLESS_IDLE 0
#endif
#define configTICK_RATE_HZ       ((TickType_t)1000)

#define configUSE_PREEMPTION     1
#define configUSE_TIME_SLICING   1
#define configUSE_IDLE_HOOK      1
#define configUSE_TICK_HOOK      0
#define configMAX_PRIORITIES     (64)
#define configMINIMAL_STACK_SIZE ((unsigned short)200)

#ifndef configTOTAL_HEAP_SIZE
#ifndef APP_HEAP
#define APP_HEAP 0
#endif
#ifndef IP_STACK_HEAP
#define IP_STACK_HEAP 0
#endif
#ifndef AP_MODE_HEAP
#define AP_MODE_HEAP 0
#endif
#define configTOTAL_HEAP_SIZE \
    ((size_t)((256 * 1024) + (APP_HEAP) + (IP_STACK_HEAP) + (AP_MODE_HEAP)))
#endif

#define configMAX_TASK_NAME_LEN (16)

#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY 0
#endif

#define configUSE_16_BIT_TICKS  0
#define configIDLE_SHOULD_YIELD 0
#define configUSE_MUTEXES       1

/* The queue registry has no purpose unless you are using a RTOS kernel aware debugger.
 * https://www.freertos.org/vQueueAddToRegistry.html */
#ifndef configQUEUE_REGISTRY_SIZE
#define configQUEUE_REGISTRY_SIZE 0
#endif

#define configCHECK_FOR_STACK_OVERFLOW        2
#define configUSE_RECURSIVE_MUTEXES           1
#define configUSE_APPLICATION_TASK_TAG        0
#define configUSE_COUNTING_SEMAPHORES         1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 4

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES (2)

/* Software timer definitions. */

#define configUSE_TIMERS             1
#define configTIMER_TASK_PRIORITY    (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH     5
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/* Set the following definitions to 1 to include the API function, or zero
 *  to exclude the API function. */
#define INCLUDE_vTaskPrioritySet         1
#define INCLUDE_uxTaskPriorityGet        1
#define INCLUDE_vTaskDelete              1
#define INCLUDE_vTaskCleanUpResources    1
#define INCLUDE_vTaskSuspend             1
#define INCLUDE_vTaskDelayUntil          1
#define INCLUDE_vTaskDelay               1
#define INCLUDE_eTaskGetState            1
#define INCLUDE_xSemaphoreGetMutexHolder 1

#define configENABLE_FPU                 1
#define configENABLE_MPU                 0
#define configENABLE_TRUSTZONE           0

/* Cortex-M specific definitions. */
#ifdef __NVIC_PRIO_BITS
/* __NVIC_PRIO_BITS will be specified when CMSIS is being used. */
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4 /* 15 priority levels */
#endif

/* The lowest interrupt priority that can be used in a call to a "set priority"
 * function. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0xf

/* The highest interrupt priority that can be used by any interrupt service
 *  routine that makes calls to interrupt safe FreeRTOS API functions.  DO NOT CALL
 *  INTERRUPT SAFE FREERTOS API FUNCTIONS FROM ANY INTERRUPT THAT HAS A HIGHER
 *  PRIORITY THAN THIS! (higher priorities are lower numeric values). */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* For more info on interrupt priority
 * See https://www.freertos.org/a00110.html#kernel_priority */

/* Interrupt priorities used by the kernel port layer itself.  These are generic
 * to all Cortex-M ports, and do not rely on any particular library functions. */
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to zero !!!!
 * See http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Define configBREAK() to trigger a breakpoint
 * NOTE: this implementation is specific to GCC and ARM. */
#if defined(__GNUC__) && (defined(__ARMEL__) || defined(__ARMEB__))
#define configBREAK() __asm("bkpt 0 \n\t")
#define configABORT()             \
    do {                          \
        taskDISABLE_INTERRUPTS(); \
        configBREAK();            \
        for (;;);                 \
    } while (0)
#endif

#define configASSERT(x) MMOSAL_ASSERT(x)

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS
 *  standard names. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
#include "trcRecorder.h"
#endif

#endif /* FREERTOS_CONFIG_H */
