/**
******************************************************************************
* @file    nema_hal.c
* @author  GPM Application Team / NE301 port
* @brief   NemaGFX platform glue for STM32N6 GPU2D, CMSIS-RTOS2 (ThreadX).
*
*          Ported from ST's x-cube-n6-ai-hand-landmarks nema_hal_freertos.c:
*          allocations go through the NE301 external-memory pool
*          (buffer_mgr) instead of tsi_malloc, and the OS primitives are
*          CMSIS-RTOS2 (osMutex/osSemaphore over ThreadX).
*
*          Only the raster blit subset used by gpu_resize is exercised;
*          NemaVG (vector) surfaces are intentionally not linked.
******************************************************************************
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "nema_sys_defs.h"
#include "nema_core.h"

#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "buffer_mgr.h"

/* see nema_sys_defs.h: multiple threads may call NemaGFX concurrently and
 * the ring buffer pushes are serialized through nema_mutex_lock(MUTEX_RB) */
#define NEMA_MULTI_THREAD
#define NEMA_WAIT_IRQ_BINARY_SEMAPHORE
#define NEMA_CACHED_MEMORY

#define RING_SIZE 1024

static nema_ringbuffer_t ring_buffer_str;
static volatile int last_cl_id = -1;

GPU2D_HandleTypeDef hgpu2d;

static osMutexId_t s_nema_mutexes[MUTEX_MAX + 1];
static osSemaphoreId_t s_nema_irq_sem;

void HAL_GPU2D_CommandListCpltCallback(GPU2D_HandleTypeDef *hgpu2d_, uint32_t CmdListID)
{
    (void)hgpu2d_;
    last_cl_id = CmdListID;
#if defined(NEMA_WAIT_IRQ_BINARY_SEMAPHORE)
    if (s_nema_irq_sem != NULL) {
        (void)osSemaphoreRelease(s_nema_irq_sem);
    }
#endif
}

void GPU2D_IRQHandler(void)
{
    HAL_GPU2D_IRQHandler(&hgpu2d);
}

void GPU2D_ER_IRQHandler(void)
{
    HAL_GPU2D_IRQHandler(&hgpu2d);
}

int32_t nema_sys_init(void)
{
    for (int i = 0; i <= MUTEX_MAX; i++) {
        s_nema_mutexes[i] = osMutexNew(NULL);
        if (s_nema_mutexes[i] == NULL) {
            return -1;
        }
    }
#if defined(NEMA_WAIT_IRQ_BINARY_SEMAPHORE)
    s_nema_irq_sem = osSemaphoreNew(1, 0, NULL);
    if (s_nema_irq_sem == NULL) {
        return -1;
    }
#endif

    ring_buffer_str.bo = nema_buffer_create(RING_SIZE);
    if (ring_buffer_str.bo.base_virt == NULL) {
        return -1;
    }
    (void)nema_buffer_map(&ring_buffer_str.bo);

    if (nema_rb_init(&ring_buffer_str, 1) < 0) {
        return -1;
    }
    last_cl_id = 0;
    return 0;
}

int nema_wait_irq(void)
{
#if defined(NEMA_WAIT_IRQ_BINARY_SEMAPHORE)
    if (osSemaphoreAcquire(s_nema_irq_sem, osWaitForever) != osOK) {
        return -1;
    }
#endif
    return 0;
}

/* bounded variant for fault-tolerant callers: 0 = IRQ arrived, -1 = timeout */
int nema_wait_irq_timeout(uint32_t timeout_ms)
{
#if defined(NEMA_WAIT_IRQ_BINARY_SEMAPHORE)
    return (osSemaphoreAcquire(s_nema_irq_sem, timeout_ms) == osOK) ? 0 : -1;
#else
    (void)timeout_ms;
    return 0;
#endif
}

/* last command-list id reported complete by the GPU2D IRQ */
int nema_last_cl_id(void)
{
    return last_cl_id;
}

int nema_wait_irq_cl(int cl_id)
{
    while (last_cl_id < cl_id) {
        if (nema_wait_irq() != 0) {
            return -1;
        }
    }
    return 0;
}

int nema_wait_irq_brk(int brk_id)
{
    (void)brk_id;
    while (nema_reg_read(GPU2D_BREAKPOINT) == 0U) {
        if (nema_wait_irq() != 0) {
            return -1;
        }
    }
    return 0;
}

uint32_t nema_reg_read(uint32_t reg)
{
    return HAL_GPU2D_ReadRegister(&hgpu2d, reg);
}

void nema_reg_write(uint32_t reg, uint32_t value)
{
    HAL_GPU2D_WriteRegister(&hgpu2d, reg, value);
}

nema_buffer_t nema_buffer_create(int size)
{
    nema_buffer_t bo;
    memset(&bo, 0, sizeof(bo));

    nema_mutex_lock(MUTEX_MALLOC);
    /* GPU-visible external pool; 32B alignment matches GPU2D expectations */
    bo.base_virt = buffer_malloc_aligned((size_t)size, 32);
    nema_mutex_unlock(MUTEX_MALLOC);

    if (bo.base_virt == NULL) {
        return bo;
    }
    bo.base_phys = (uintptr_t)bo.base_virt;
    bo.size = size;
    return bo;
}

nema_buffer_t nema_buffer_create_pool(int pool, int size)
{
    (void)pool;
    return nema_buffer_create(size);
}

void *nema_buffer_map(nema_buffer_t *bo)
{
    return bo->base_virt;
}

void nema_buffer_unmap(nema_buffer_t *bo)
{
    (void)bo;
}

void nema_buffer_destroy(nema_buffer_t *bo)
{
    if (bo == NULL || bo->base_virt == NULL) {
        return;
    }
    nema_mutex_lock(MUTEX_MALLOC);
    buffer_free(bo->base_virt);
    nema_mutex_unlock(MUTEX_MALLOC);
    bo->base_virt = NULL;
}

uintptr_t nema_buffer_phys(nema_buffer_t *bo)
{
    return bo->base_phys;
}

void nema_buffer_flush(nema_buffer_t *bo)
{
#if defined(NEMA_CACHED_MEMORY)
    if (bo->base_virt != NULL) {
        nema_mutex_lock(MUTEX_FLUSH);
        SCB_CleanInvalidateDCache_by_Addr((void *)bo->base_virt, bo->size);
        nema_mutex_unlock(MUTEX_FLUSH);
    }
#else
    (void)bo;
#endif
}

void *nema_host_malloc(unsigned size)
{
    return buffer_malloc_aligned((size_t)size, 32);
}

void nema_host_free(void *ptr)
{
    if (ptr != NULL) {
        buffer_free(ptr);
    }
}

int nema_mutex_lock(int mutex_id)
{
    if (mutex_id >= 0 && mutex_id <= MUTEX_MAX && s_nema_mutexes[mutex_id] != NULL) {
        return (osMutexAcquire(s_nema_mutexes[mutex_id], osWaitForever) == osOK) ? 0 : -1;
    }
    return 0;
}

int nema_mutex_unlock(int mutex_id)
{
    if (mutex_id >= 0 && mutex_id <= MUTEX_MAX && s_nema_mutexes[mutex_id] != NULL) {
        return (osMutexRelease(s_nema_mutexes[mutex_id]) == osOK) ? 0 : -1;
    }
    return 0;
}

void HAL_GPU2D_MspInit(GPU2D_HandleTypeDef *hgpu2d_)
{
    if (hgpu2d_->Instance == (GPU2D_TypeDef)GPU2D) {
        __HAL_RCC_GPU2D_FORCE_RESET();
        __HAL_RCC_GPU2D_RELEASE_RESET();
        __HAL_RCC_GPU2D_CLK_ENABLE();

        NVIC_SetPriority(GPU2D_IRQn, 6);
        NVIC_EnableIRQ(GPU2D_IRQn);
        NVIC_SetPriority(GPU2D_ER_IRQn, 5);
        NVIC_EnableIRQ(GPU2D_ER_IRQn);
    }
}

void HAL_GFXMMU_MspInit(GFXMMU_HandleTypeDef *hgfxmmu)
{
    (void)hgfxmmu;
    __HAL_RCC_GFXMMU_CLK_ENABLE();
    NVIC_SetPriority(GFXMMU_IRQn, 5);
    NVIC_EnableIRQ(GFXMMU_IRQn);
}
