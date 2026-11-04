#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "cmsis_os2.h"
#include "stm32n6xx_hal.h"
#include "video_stream_hub.h"

#define DEV_HOST_RAM_SIZE (256u * 1024u)

uint8_t __app_host_ram_base__[DEV_HOST_RAM_SIZE] __attribute__((aligned(32)));
uint8_t *const __app_host_ram_end__ = &__app_host_ram_base__[DEV_HOST_RAM_SIZE];

typedef struct {
    pthread_t handle;
    void (*fn)(void *);
    void *arg;
    volatile int exited;
} dev_thread_t;

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    uint32_t flags;
} dev_event_t;

static void *dev_thread_trampoline(void *p)
{
    dev_thread_t *t = (dev_thread_t *)p;

    t->fn(t->arg);
    t->exited = 1;
    return NULL;
}

osMutexId_t osMutexNew(const void *attribute)
{
    pthread_mutex_t *mtx;
    pthread_mutexattr_t attr;

    (void)attribute;
    mtx = (pthread_mutex_t *)calloc(1u, sizeof(*mtx));
    if (mtx == NULL) {
        return NULL;
    }
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    return (osMutexId_t)mtx;
}

osStatus_t osMutexDelete(osMutexId_t mutex)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)mutex;

    if (mtx == NULL) {
        return osErrorParameter;
    }
    pthread_mutex_destroy(mtx);
    free(mtx);
    return osOK;
}

osStatus_t osMutexAcquire(osMutexId_t mutex, uint32_t timeout)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)mutex;

    (void)timeout;
    if (mtx == NULL) {
        return osErrorParameter;
    }
    return (pthread_mutex_lock(mtx) == 0) ? osOK : osErrorResource;
}

osStatus_t osMutexRelease(osMutexId_t mutex)
{
    pthread_mutex_t *mtx = (pthread_mutex_t *)mutex;

    if (mtx == NULL) {
        return osErrorParameter;
    }
    return (pthread_mutex_unlock(mtx) == 0) ? osOK : osErrorResource;
}

osThreadId_t osThreadNew(void (*func)(void *argument), void *argument,
                         const osThreadAttr_t *attr)
{
    dev_thread_t *t;

    (void)attr;
    if (func == NULL) {
        return NULL;
    }
    t = (dev_thread_t *)calloc(1u, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    t->fn = func;
    t->arg = argument;
    t->exited = 0;
    if (pthread_create(&t->handle, NULL, dev_thread_trampoline, t) != 0) {
        free(t);
        return NULL;
    }
    return (osThreadId_t)t;
}

osThreadState_t osThreadGetState(osThreadId_t thread_id)
{
    dev_thread_t *t = (dev_thread_t *)thread_id;

    if (t == NULL) {
        return osThreadError;
    }
    return (t->exited != 0) ? osThreadTerminated : osThreadRunning;
}

osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
    dev_thread_t *t = (dev_thread_t *)thread_id;

    if (t == NULL) {
        return osErrorParameter;
    }
    if (t->exited == 0) {
        return osErrorResource;
    }
    pthread_join(t->handle, NULL);
    free(t);
    return osOK;
}

osEventFlagsId_t osEventFlagsNew(const void *attribute)
{
    dev_event_t *evt;

    (void)attribute;
    evt = (dev_event_t *)calloc(1u, sizeof(*evt));
    if (evt == NULL) {
        return NULL;
    }
    pthread_mutex_init(&evt->mtx, NULL);
    pthread_cond_init(&evt->cond, NULL);
    evt->flags = 0u;
    return (osEventFlagsId_t)evt;
}

osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id)
{
    dev_event_t *evt = (dev_event_t *)ef_id;

    if (evt == NULL) {
        return osErrorParameter;
    }
    pthread_mutex_destroy(&evt->mtx);
    pthread_cond_destroy(&evt->cond);
    free(evt);
    return osOK;
}

osStatus_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags)
{
    dev_event_t *evt = (dev_event_t *)ef_id;

    if (evt == NULL) {
        return osErrorParameter;
    }
    pthread_mutex_lock(&evt->mtx);
    evt->flags |= flags;
    pthread_cond_broadcast(&evt->cond);
    pthread_mutex_unlock(&evt->mtx);
    return osOK;
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags,
                          uint32_t options, uint32_t timeout)
{
    dev_event_t *evt = (dev_event_t *)ef_id;
    struct timespec ts;
    uint32_t got;
    int rc;
    int timedout = 0;
    int forever = (timeout == osWaitForever);

    if (evt == NULL) {
        return (uint32_t)osErrorParameter;
    }
    if (!forever) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(timeout / 1000u);
        ts.tv_nsec += (long)(timeout % 1000u) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
    }
    pthread_mutex_lock(&evt->mtx);
    for (;;) {
        got = evt->flags & flags;
        if (((options & osFlagsWaitAll) != 0u) ? (got == flags) : (got != 0u)) {
            if ((options & osFlagsNoClear) == 0u) {
                evt->flags &= ~got;
            }
            pthread_mutex_unlock(&evt->mtx);
            return got;
        }
        if (forever) {
            pthread_cond_wait(&evt->cond, &evt->mtx);
        } else {
            if (timedout != 0) {
                break;
            }
            rc = pthread_cond_timedwait(&evt->cond, &evt->mtx, &ts);
            if (rc != 0) {
                timedout = 1;
            }
        }
    }
    pthread_mutex_unlock(&evt->mtx);
    return (uint32_t)osErrorTimeout;
}

uint32_t osKernelGetTickCount(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

osStatus_t osDelay(uint32_t ticks)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ticks / 1000u);
    ts.tv_nsec = (long)(ticks % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
    return osOK;
}

void SCB_CleanInvalidateDCache_by_Addr(void *addr, int32_t dsize)
{
    (void)addr;
    (void)dsize;
}

void SCB_InvalidateICache_by_Addr(void *addr, int32_t isize)
{
    (void)addr;
    (void)isize;
}

void __DSB(void)
{
}

void __ISB(void)
{
}
