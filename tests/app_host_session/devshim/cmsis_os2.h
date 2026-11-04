#ifndef DEVSHIM_CMSIS_OS2_H
#define DEVSHIM_CMSIS_OS2_H

#include <stddef.h>
#include <stdint.h>

typedef void *osMutexId_t;
typedef void *osThreadId_t;
typedef void *osSemaphoreId_t;
typedef void *osMessageQueueId_t;
typedef void *osEventFlagsId_t;
typedef void *osTimerId_t;

typedef enum {
    osThreadInactive = 0,
    osThreadReady = 1,
    osThreadRunning = 2,
    osThreadBlocked = 3,
    osThreadTerminated = 4,
    osThreadError = -1
} osThreadState_t;

typedef enum {
    osOK = 0,
    osError = -1,
    osErrorTimeout = -2,
    osErrorResource = -3,
    osErrorParameter = -4
} osStatus_t;

#define osThreadDetached 0x00000001

typedef struct {
    const char *name;
    uint32_t attr_bits;
    void *cb_mem;
    size_t cb_size;
    void *stack_mem;
    size_t stack_size;
    int32_t priority;
    int32_t tz_module;
    uint32_t reserved;
} osThreadAttr_t;

#define osPriorityNone 0
#define osPriorityLow 8
#define osPriorityNormal 40
#define osPriorityHigh 48

#define osFlagsWaitAny 0x00000000u
#define osFlagsWaitAll 0x00000001u
#define osFlagsNoClear 0x00000002u
#define osWaitForever 0xFFFFFFFFu

osMutexId_t osMutexNew(const void *attribute);
osStatus_t osMutexDelete(osMutexId_t mutex);
osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout);
osStatus_t osMutexRelease(osMutexId_t mutex);

osThreadId_t osThreadNew(void (*func)(void *argument), void *argument,
                         const osThreadAttr_t *attr);
osThreadState_t osThreadGetState(osThreadId_t thread_id);
osStatus_t osThreadTerminate(osThreadId_t thread_id);

osEventFlagsId_t osEventFlagsNew(const void *attribute);
osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id);
osStatus_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags);
uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags,
                          uint32_t options, uint32_t timeout);

uint32_t osKernelGetTickCount(void);
osStatus_t osDelay(uint32_t ticks);

#endif
