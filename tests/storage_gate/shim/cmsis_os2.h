#ifndef SHIM_CMSIS_OS2_H
#define SHIM_CMSIS_OS2_H
#include <stdint.h>
#include <stddef.h>
typedef void *osMutexId_t;
typedef void *osThreadId_t;
typedef void *osSemaphoreId_t;
typedef void *osMessageQueueId_t;
typedef void *osEventFlagsId_t;
typedef void *osTimerId_t;
typedef void *osPipeId_t;
typedef void *osMemPoolId_t;
#define osWaitForever 0xFFFFFFFFU
#define osOK 0
osMutexId_t osMutexNew(const void *attribute);
int32_t osMutexDelete(osMutexId_t mutex);
int32_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout);
int32_t osMutexRelease(osMutexId_t mutex);
uint32_t osKernelGetTickCount(void);
int osDelay(uint32_t ticks);
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
#define osPriorityHigh 48
#define osPriorityNormal 40
#define osPriorityLow 8
#define osPriorityNone 0
#endif
