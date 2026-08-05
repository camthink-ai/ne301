/*
 * Copyright 2021-2024 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <FreeRTOS.h>
//#include <heap_mm.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <timers.h>
#include <stdarg.h>
#include <string.h>
#include "mmosal.h"
#include "mmhal_os.h"
#include "mmlog.h"
#include "errno.h"

/* --------------------------------------------------------------------------------------------- */

/** Fast implementation of _x % _m where _m is a power of 2. */
#define FAST_MOD(_x, _m) ((_x) & ((_m) - 1))

/** Duration to delay before resetting the device on assert. */
#define DELAY_BEFORE_RESET_MS 1000

/** Data structure for assertion information to be preserved. */
struct mmosal_preserved_failure_info
{
    /** Magic number, to check if the info is valid. */
    uint32_t magic;

    /** Number of failures recorded. */
    volatile uint32_t failure_count;

    /** Number of most recently displayed failure. */
    volatile uint32_t displayed_failure_count;

    /** Preserved information from the most recent failure(s). */
    struct mmosal_failure_info info[MMOSAL_MAX_FAILURE_RECORDS];
};

/** Magic number to put in @c mmosal_assert_info.magic to indicate that the assertion info
 *  is valid. */
#define ASSERT_INFO_MAGIC (0xabcd1234)

/* Persistent assertion info. Linker script should put this into memory that is not
 * zeroed on boot. Be careful to update linker script if renaming. */
struct mmosal_preserved_failure_info preserved_failure_info __attribute__((section(".noinit")));

void mmosal_log_failure_info(const struct mmosal_failure_info *info)
{
    uint32_t record_num;

    if (preserved_failure_info.magic != ASSERT_INFO_MAGIC)
    {
        preserved_failure_info.failure_count = 0;
        preserved_failure_info.displayed_failure_count = 0;
    }

    preserved_failure_info.magic = ASSERT_INFO_MAGIC;
    record_num = FAST_MOD(preserved_failure_info.failure_count, MMOSAL_MAX_FAILURE_RECORDS);
    preserved_failure_info.failure_count++;
    memcpy(&preserved_failure_info.info[record_num], info, sizeof(*info));
}

#if !(defined(ENABLE_MANUAL_FAILURE_LOG_PROCESSING) && ENABLE_MANUAL_FAILURE_LOG_PROCESSING)
const char *get_filename(const char *path)
{
    const char *p = strrchr(path, '/');

#ifdef _WIN32
    const char *p2 = strrchr(path, '\\');
    if (p2 && (!p || p2 > p))
        p = p2;
#endif

    return p ? p + 1 : path;
}
static void mmosal_dump_failure_info(void)
{
    unsigned first_failure_num = preserved_failure_info.displayed_failure_count;
    unsigned new_failure_count =
        preserved_failure_info.failure_count - preserved_failure_info.displayed_failure_count;
    unsigned failure_offset;

    if (new_failure_count >= MMOSAL_MAX_FAILURE_RECORDS)
    {
        first_failure_num =
            FAST_MOD(preserved_failure_info.failure_count, MMOSAL_MAX_FAILURE_RECORDS);
        new_failure_count = MMOSAL_MAX_FAILURE_RECORDS;
    }

    for (failure_offset = 0; failure_offset < new_failure_count; failure_offset++)
    {
        unsigned ii;
        unsigned idx = FAST_MOD(first_failure_num + failure_offset, MMOSAL_MAX_FAILURE_RECORDS);
        struct mmosal_failure_info *info = &preserved_failure_info.info[idx];

        printf("Failure %u logged at pc 0x%08lx, lr 0x%08lx, line %ld in %s\n",
               first_failure_num + failure_offset,
               info->pc,
               info->lr,
               info->line,
               get_filename(info->file_name));

        for (ii = 0; ii < sizeof(info->platform_info) / sizeof(info->platform_info[0]); ii++)
        {
            printf("    0x%08lx\n", info->platform_info[ii]);
        }
    }

    preserved_failure_info.displayed_failure_count = preserved_failure_info.failure_count;
}

#else
bool mmosal_extract_failure_info(struct mmosal_failure_info *buf, uint32_t *failure_number)
{
    if (preserved_failure_info.magic != ASSERT_INFO_MAGIC)
    {
        return false;
    }

    uint32_t new_failure_count =
        preserved_failure_info.failure_count - preserved_failure_info.displayed_failure_count;

    /* No failures are un-viewed. */
    if (new_failure_count == 0)
    {
        return false;
    }

    /* All failure log entries are un-viewed, set displayed_failure_count to oldest stored entry. */
    if (new_failure_count >= MMOSAL_MAX_FAILURE_RECORDS)
    {
        preserved_failure_info.displayed_failure_count =
            preserved_failure_info.failure_count - MMOSAL_MAX_FAILURE_RECORDS;
    }

    if (failure_number != NULL)
    {
        *failure_number = preserved_failure_info.displayed_failure_count;
    }

    /* Convert the displayed_failure_count into an index for the preserved failure log array. */
    uint32_t oldest_entry_idx =
        FAST_MOD(preserved_failure_info.displayed_failure_count, MMOSAL_MAX_FAILURE_RECORDS);

    memcpy(buf, &preserved_failure_info.info[oldest_entry_idx], sizeof(*buf));

    preserved_failure_info.displayed_failure_count++;

    return true;
}

#endif

void mmosal_impl_assert(void)
{
#ifdef HALT_ON_ASSERT
	printf("mmosal_impl_assert and HALT_ON_ASSERT!!!!!\r\n");
    if (preserved_failure_info.magic == ASSERT_INFO_MAGIC)
    {
#if !(defined(ENABLE_MANUAL_FAILURE_LOG_PROCESSING) && ENABLE_MANUAL_FAILURE_LOG_PROCESSING)
        mmosal_dump_failure_info();
#endif
    }
    mmosal_disable_interrupts();
    mmhal_log_flush();
    MMPORT_BREAKPOINT();
#else
	printf("mmosal_impl_assert and reset!!!!!\r\n");
    mmosal_task_sleep(DELAY_BEFORE_RESET_MS);
    mmhal_reset();
#endif
    while (1)
    {
    }
}

/* --------------------------------------------------------------------------------------------- */

#define INIT_STACK_SIZE_U32 (1024)

static void init_task_main(void *arg)
{
    mmosal_app_init_cb_t app_init_cb = (mmosal_app_init_cb_t)arg;
    mmhal_init();

    if (preserved_failure_info.magic == ASSERT_INFO_MAGIC)
    {
#if !(defined(ENABLE_MANUAL_FAILURE_LOG_PROCESSING) && ENABLE_MANUAL_FAILURE_LOG_PROCESSING)
        mmosal_dump_failure_info();
#endif
    }
    else
    {
        bool all_zeros = true;
        unsigned ii;
        const uint8_t *info = (const uint8_t *)(&preserved_failure_info.info);
        for (ii = 0; ii < sizeof(preserved_failure_info.info); ii++)
        {
            if (info[ii] != 0)
            {
                all_zeros = false;
                break;
            }
        }

        if (all_zeros)
        {
            printf("Warning: preserved_failure_info may be zeroed on boot\n");
        }
    }

    app_init_cb();
    /* This task has completed its work, so delete it. */
    mmosal_task_delete(NULL);
}

/*
 * The following allocation is for testing FreeRTOS Heap 5. This just makes a single block of
 * size configTOTAL_HEAP_SIZE. In practice it is more likely that the addresses and sizes
 * of the heap regions would come from the linker script.
 */
#ifdef HEAP_5
static uint32_t heap[configTOTAL_HEAP_SIZE / sizeof(uint32_t)];
static const HeapRegion_t heap_regions[] = { { (uint8_t *)heap, configTOTAL_HEAP_SIZE },
                                             { NULL, 0 } };
#endif

int mmosal_main(mmosal_app_init_cb_t app_init_cb)
{
    mmhal_early_init();

    /*
     * The following function call sets up the regions for FreeRTOS Heap 5. (This is not required
     * for Heap 4.)
     *
     * Note: _malloc_r() and _calloc_r() may be called from __libc_init_array()
     * in C++ applications when running through the global constructor list.
     * Calling pvPortMalloc_() before vPortDefineHeapRegions() is called is not
     * allowed when using HEAP5. So C++ applications must use HEAP4 and not HEAP5.
     */
#ifdef HEAP_5
    vPortDefineHeapRegions(heap_regions);
#endif

#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    vTraceEnable(TRC_START);
#endif

    //mm_logging_init();

    struct mmosal_task *init_task = mmosal_task_create(init_task_main,
                                                       (void *)app_init_cb,
                                                       MMOSAL_TASK_PRI_LOW,
                                                       INIT_STACK_SIZE_U32,
                                                       "init");
    configASSERT(init_task != NULL);
    vTaskStartScheduler();
    /* We should never get here. */
    return -1;
}

void *mmosal_malloc_(size_t size)
{
    //return pvPortMalloc_(size);
    return pvPortMalloc(size);
}

#ifdef MMOSAL_TRACK_ALLOCATIONS
void *mmosal_malloc_dbg(size_t size, const char *name, unsigned line_number)
{
    return pvPortMalloc_dbg(size, name, line_number);
}

#else
void *mmosal_malloc_dbg(size_t size, const char *name, unsigned line_number)
{
    (void)name;
    (void)line_number;
    //return pvPortMalloc_(size);
    return pvPortMalloc(size);
}

#endif

void mmosal_free(void *p)
{
    //vPortFree_(p);
    vPortFree(p);
}

void *mmosal_realloc(void *ptr, size_t size)
{
    return pvPortRealloc(ptr, size);
}

void *mmosal_calloc(size_t nitems, size_t size)
{
    //void *ptr = pvPortMalloc_(nitems * size);
    void *ptr = pvPortMalloc(nitems * size);
    if (ptr != NULL)
    {
        memset(ptr, 0, nitems * size);
    }
    return ptr;
}

#if 0
/*
 * Note: This function is defined in stdlib.h.
 * It is called internally by malloc()
 */
void *_malloc_r(struct _reent *r, size_t size) _NOTHROW
{
    /* Note: This function may be called from __libc_init_array()
     * in C++ applications when running through the global constructor list.
     * It is not safe to call pvPortMalloc_() before FreeRTOS is initialised when
     * using HEAP5. So C++ applications must use HEAP4 and not HEAP5.
     */
    void *ret = mmosal_malloc(size);
    if (ret == NULL)
    {
        r->_errno = ENOMEM;
    }
    return ret;
}

/*
 * Note: This function is defined in stdlib.h.
 * It is called internally by calloc()
 */
void *_calloc_r(struct _reent *r, size_t nitems, size_t size)
{
    /* Note: This function may be called from __libc_init_array()
     * in C++ applications when running through the global constructor list.
     * It is not safe to call pvPortMalloc_() before FreeRTOS is initialised when
     * using HEAP5. So C++ applications must use HEAP4 and not HEAP5.
     */
    void *ret = mmosal_calloc(nitems, size);
    if (ret == NULL)
    {
        r->_errno = ENOMEM;
    }
    return ret;
}

/*
 * Note: This function is defined in stdlib.h
 * It is called internally by realloc()
 */
void *_realloc_r(struct _reent *r, void *bp, size_t size) _NOTHROW
{
    void *ret = mmosal_realloc(bp, size);
    if (ret == NULL)
    {
        r->_errno = ENOMEM;
    }
    return ret;
}

/*
 * Note: This function is defined in stdlib.h
 * It is called internally by free()
 */
void _free_r(struct _reent *r, void *bp) _NOTHROW
{
    (void)r;
    mmosal_free(bp);
}
#endif
/* --------------------------------------------------------------------------------------------- */

struct mmosal_task_arg
{
    mmosal_task_fn_t task_fn;
    void *task_fn_arg;
};

void mmosal_task_main(void *arg)
{
    struct mmosal_task_arg task_arg = *(struct mmosal_task_arg *)arg;
    mmosal_free(arg);
    task_arg.task_fn(task_arg.task_fn_arg);
    mmosal_task_delete(NULL);
}

struct mmosal_task *mmosal_task_create(mmosal_task_fn_t task_fn,
                                       void *argument,
                                       enum mmosal_task_priority priority,
                                       unsigned stack_size_u32,
                                       const char *name)
{
    xTaskHandle handle;
    UBaseType_t freertos_priority = tskIDLE_PRIORITY + priority;

    struct mmosal_task_arg *task_arg = (struct mmosal_task_arg *)mmosal_malloc(sizeof(*task_arg));
    if (task_arg == NULL)
    {
        return NULL;
    }
    task_arg->task_fn = task_fn;
    task_arg->task_fn_arg = argument;

    BaseType_t result =
        xTaskCreate(mmosal_task_main, name, stack_size_u32, task_arg, freertos_priority, &handle);
    if (result == pdFAIL)
    {
        mmosal_free(task_arg);
        return NULL;
    }

    return (struct mmosal_task *)handle;
}

void mmosal_task_delete(struct mmosal_task *task)
{
    vTaskDelete((xTaskHandle)task);
}

/*
 * Warning: this function should not be used since eTaskGetState() is not a reliable
 * means of testing whether a task has completed.
 *
 * This function will be removed in future.
 */
void mmosal_task_join(struct mmosal_task *task)
{
    while (eTaskGetState((xTaskHandle)task) != eDeleted)
    {
        mmosal_task_sleep(10);
    }
}

struct mmosal_task *mmosal_task_get_active(void)
{
    return (struct mmosal_task *)xTaskGetCurrentTaskHandle();
}

void mmosal_task_yield(void)
{
    taskYIELD();
}

void mmosal_task_sleep(uint32_t duration_ms)
{
    vTaskDelay(duration_ms / portTICK_RATE_MS);
}

void mmosal_task_enter_critical(void)
{
    taskENTER_CRITICAL();
}

void mmosal_task_exit_critical(void)
{
    taskEXIT_CRITICAL();
}

void mmosal_disable_interrupts(void)
{
    taskDISABLE_INTERRUPTS();
}

void mmosal_enable_interrupts(void)
{
    taskENABLE_INTERRUPTS();
}

const char *mmosal_task_name(void)
{
    xTaskHandle t = xTaskGetCurrentTaskHandle();
    return pcTaskGetName(t);
}

bool mmosal_task_wait_for_notification(uint32_t timeout_ms)
{
    TickType_t wait = portMAX_DELAY;
    if (timeout_ms < UINT32_MAX)
    {
        wait = pdMS_TO_TICKS(timeout_ms);
    }
    uint32_t ret = ulTaskNotifyTake(pdTRUE, /* Act as binary semaphore */
                                    wait);
    return (ret != 0);
}

void mmosal_task_notify(struct mmosal_task *task)
{
    xTaskNotifyGive((xTaskHandle)task);
}

void mmosal_task_notify_from_isr(struct mmosal_task *task)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR((xTaskHandle)task, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_mutex *mmosal_mutex_create(const char *name)
{
    struct mmosal_mutex *mutex = (struct mmosal_mutex *)xSemaphoreCreateMutex();
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL)
    {
        vTraceSetMutexName(mutex, name);
    }
#else
    (void)name;
#endif

#if (configQUEUE_REGISTRY_SIZE > 0)
    vQueueAddToRegistry((QueueHandle_t)mutex, name);
#endif

    return mutex;
}

void mmosal_mutex_delete(struct mmosal_mutex *mutex)
{
    if (mutex != NULL)
    {
        vSemaphoreDelete((SemaphoreHandle_t)mutex);
    }
}

bool mmosal_mutex_get(struct mmosal_mutex *mutex, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX)
    {
        timeout_ticks = timeout_ms / portTICK_RATE_MS;
    }
	
    return (xSemaphoreTake((xSemaphoreHandle)mutex, timeout_ticks) == pdPASS);
}

bool mmosal_mutex_release(struct mmosal_mutex *mutex)
{
    return (xSemaphoreGive((xSemaphoreHandle)mutex) == pdPASS);
}

bool mmosal_mutex_is_held_by_active_task(struct mmosal_mutex *mutex)
{
    return xSemaphoreGetMutexHolder((xSemaphoreHandle)mutex) == xTaskGetCurrentTaskHandle();
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_sem *mmosal_sem_create(unsigned max_count, unsigned initial_count, const char *name)
{
    struct mmosal_sem *sem =
        (struct mmosal_sem *)xSemaphoreCreateCounting(max_count, initial_count);
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL)
    {
        vTraceSetSemaphoreName(sem, name);
    }
#else
    (void)name;
#endif

#if (configQUEUE_REGISTRY_SIZE > 0)
    vQueueAddToRegistry((QueueHandle_t)sem, name);
#endif

    return sem;
}

void mmosal_sem_delete(struct mmosal_sem *sem)
{
    vSemaphoreDelete((SemaphoreHandle_t)sem);
}

bool mmosal_sem_give(struct mmosal_sem *sem)
{
    return xSemaphoreGive((xSemaphoreHandle)sem);
}

bool mmosal_sem_give_from_isr(struct mmosal_sem *sem)
{
    BaseType_t task_woken = false;
    BaseType_t ret = xSemaphoreGiveFromISR((xSemaphoreHandle)sem, &task_woken);
    if (ret == pdPASS)
    {
        portYIELD_FROM_ISR(task_woken);
        return true;
    }
    else
    {
        return false;
    }
}

bool mmosal_sem_wait(struct mmosal_sem *sem, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX)
    {
        timeout_ticks = timeout_ms / portTICK_RATE_MS;
    }
	
    return (xSemaphoreTake((xSemaphoreHandle)sem, timeout_ticks) == pdPASS);
}

uint32_t mmosal_sem_get_count(struct mmosal_sem *sem)
{
    return uxSemaphoreGetCount((xSemaphoreHandle)sem);
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_semb *mmosal_semb_create(const char *name)
{
    struct mmosal_semb *semb = (struct mmosal_semb *)xSemaphoreCreateBinary();
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL)
    {
        vTraceSetSemaphoreName(semb, name);
    }
#else
    (void)name;
#endif

#if (configQUEUE_REGISTRY_SIZE > 0)
    vQueueAddToRegistry((QueueHandle_t)semb, name);
#endif

    return semb;
}

void mmosal_semb_delete(struct mmosal_semb *semb)
{
    if (semb != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)semb);
    }
}

bool mmosal_semb_give(struct mmosal_semb *semb)
{
    if (semb == NULL) {
        return false;
    }

    return (xSemaphoreGive((xSemaphoreHandle)semb) == pdPASS);
}

bool mmosal_semb_give_from_isr(struct mmosal_semb *semb)
{
    BaseType_t task_woken = pdFALSE;
    BaseType_t ret;

    if (semb == NULL) {
        return false;
    }

    ret = xSemaphoreGiveFromISR((xSemaphoreHandle)semb, &task_woken);
    if (ret == pdPASS)
    {
        portYIELD_FROM_ISR(task_woken);
        return true;
    }
    else
    {
        return false;
    }
}

bool mmosal_semb_wait(struct mmosal_semb *semb, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX)
    {
        timeout_ticks = timeout_ms / portTICK_RATE_MS;
    }
	
    return (xSemaphoreTake((xSemaphoreHandle)semb, timeout_ticks) == pdPASS);
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_queue *mmosal_queue_create(size_t num_items, size_t item_size, const char *name)
{
    struct mmosal_queue *queue = (struct mmosal_queue *)xQueueCreate(num_items, item_size);
#if (configUSE_TRACE_FACILITY == 1) && defined(ENABLE_TRACEALYZER) && ENABLE_TRACEALYZER
    if (name != NULL)
    {
        vTraceSetQueueName(queue, name);
    }
#else
    (void)name;
#endif

#if (configQUEUE_REGISTRY_SIZE > 0)
    vQueueAddToRegistry((QueueHandle_t)queue, name);
#endif

    return queue;
}

void mmosal_queue_delete(struct mmosal_queue *queue)
{
    vQueueDelete((QueueHandle_t)queue);
}

bool mmosal_queue_pop(struct mmosal_queue *queue, void *item, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX)
    {
        timeout_ticks = timeout_ms / portTICK_RATE_MS;
    }
    return (xQueueReceive((QueueHandle_t)queue, item, timeout_ticks) == pdPASS);
}

bool mmosal_queue_push(struct mmosal_queue *queue, const void *item, uint32_t timeout_ms)
{
    uint32_t timeout_ticks = portMAX_DELAY;
    if (timeout_ms != UINT32_MAX)
    {
        timeout_ticks = timeout_ms / portTICK_RATE_MS;
    }
    return (xQueueSendToBack((QueueHandle_t)queue, item, timeout_ticks) == pdPASS);
}

bool mmosal_queue_pop_from_isr(struct mmosal_queue *queue, void *item)
{
    BaseType_t task_woken = pdFALSE;
    if (xQueueReceiveFromISR((QueueHandle_t)queue, item, &task_woken) == pdTRUE)
    {
        portYIELD_FROM_ISR(task_woken);
        return true;
    }
    else
    {
        return false;
    }
}

bool mmosal_queue_push_from_isr(struct mmosal_queue *queue, const void *item)
{
    BaseType_t task_woken = pdFALSE;
    if (xQueueSendToBackFromISR((QueueHandle_t)queue, item, &task_woken) == pdTRUE)
    {
        portYIELD_FROM_ISR(task_woken);
        return true;
    }
    else
    {
        return false;
    }
}

/* --------------------------------------------------------------------------------------------- */

uint32_t mmosal_get_time_ms(void)
{
    return xTaskGetTickCount() * portTICK_RATE_MS;
}

uint32_t mmosal_get_time_ticks(void)
{
    return xTaskGetTickCount();
}

uint32_t mmosal_ticks_per_second(void)
{
    return portTICK_RATE_MS * 1000;
}

/* --------------------------------------------------------------------------------------------- */

struct mmosal_timer {
    TimerHandle_t handle;
    void *arg;
    mm_timer_callback_t cb;
};

static void mmosal_timer_trampoline(TimerHandle_t timer)
{
    struct mmosal_timer *mt = (struct mmosal_timer *)pvTimerGetTimerID(timer);
    if (mt != NULL && mt->cb != NULL)
    {
        mt->cb(mt);
    }
}

struct mmosal_timer *mmosal_timer_create(const char *name, uint32_t timer_period,
                                         bool auto_reload, void *arg,
                                         mm_timer_callback_t callback)
{
    struct mmosal_timer *mt = mmosal_calloc(1U, sizeof(*mt));
    if (mt == NULL) 
	{
        return NULL;
    }

    mt->arg = arg;
    mt->cb = callback;
    mt->handle = xTimerCreate(name, pdMS_TO_TICKS(timer_period),
                              auto_reload ? pdTRUE : pdFALSE,
                              mt, mmosal_timer_trampoline);
    if (mt->handle == NULL) 
	{
        mmosal_free(mt);
        return NULL;
    }
	
    return mt;
}


void mmosal_timer_delete(struct mmosal_timer *timer)
{
    if (timer == NULL)
    {
        return;
    }

    if (timer->handle != NULL)
    {
        (void)xTimerStop(timer->handle, portMAX_DELAY);
        BaseType_t ret = xTimerDelete(timer->handle, portMAX_DELAY);
        timer->handle = NULL;
        configASSERT(ret == pdPASS);
    }

    mmosal_free(timer);
}

bool mmosal_timer_start(struct mmosal_timer *timer)
{
    if (timer == NULL || timer->handle == NULL)
    {
        return false;
    }

    return (xTimerStart(timer->handle, portMAX_DELAY) == pdPASS);
}

bool mmosal_timer_stop(struct mmosal_timer *timer)
{
    if (timer == NULL || timer->handle == NULL)
    {
        return false;
    }

    return (xTimerStop(timer->handle, portMAX_DELAY) == pdPASS);
}

bool mmosal_timer_change_period(struct mmosal_timer *timer, uint32_t new_period)
{
    if (timer == NULL || timer->handle == NULL)
    {
        return false;
    }

    return (xTimerChangePeriod(timer->handle, pdMS_TO_TICKS(new_period), portMAX_DELAY) == pdPASS);
}

void *mmosal_timer_get_arg(struct mmosal_timer *timer)
{
    if (timer == NULL)
    {
        return NULL;
    }

    return timer->arg;
}

bool mmosal_is_timer_active(struct mmosal_timer *timer)
{
    if (timer == NULL || timer->handle == NULL)
    {
        return false;
    }

    return (xTimerIsTimerActive(timer->handle) != pdFALSE);
}

int mmosal_printf(const char *format, ...)
{
    int ret;
    va_list args;
    va_start(args, format);
    ret = vprintf(format, args);
    va_end(args);
    return ret;
}
