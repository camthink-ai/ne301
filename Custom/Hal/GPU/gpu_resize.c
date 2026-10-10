/**
******************************************************************************
* @file    gpu_resize.c
* @brief   GPU2D (NemaGFX) accelerated RGB888 resize for the AI input path.
*
*          Follows the pattern of ST's x-cube-n6-ai-hand-landmarks:
*          the destination is written through a GFXMMU virtual buffer
*          (4MB window, MSB-remove packing), the source texture is bound
*          at its physical address, and a plain bilinear blit_rect_fit
*          performs the scale. Fails soft (non-zero) so callers can fall
*          back to the software bilinear resize.
******************************************************************************
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "gpu_resize.h"
#include "buffer_mgr.h"

#include "nema_core.h"
#include "nema_error.h"

/* internal lib entry (not in public headers); the ST reference enables
 * tiled rasterization before its textured blits */
extern void nema_enable_tiling(int);

typedef struct {
    int inited;                 /* bring-up completed */
    int failed;                 /* bring-up failed once: never retry */
    GFXMMU_HandleTypeDef gfxmmu;
    nema_cmdlist_t cl;
    osMutexId_t lock;
} gpu_resize_ctx_t;

static gpu_resize_ctx_t g_gpu;

/* defined in nema_hal.c */
extern GPU2D_HandleTypeDef hgpu2d;
extern int nema_wait_irq_timeout(uint32_t timeout_ms);
extern int nema_last_cl_id(void);

/* bounded completion wait: a GPU fault (RIF filter, bus error) must degrade
 * to the software fallback, never wedge the calling task. Callers hold the
 * context lock, so command lists complete in order: wait for the id that
 * follows the last completed one. Returns 0 ok. */
static int gpu_cl_wait_next(uint32_t timeout_ms)
{
    int done_id = nema_last_cl_id() + 1;
    uint32_t t0 = osKernelGetTickCount();
    while (nema_last_cl_id() < done_id) {
        if (nema_wait_irq_timeout(20) != 0 &&
            (osKernelGetTickCount() - t0) >= timeout_ms) {
            return -1;
        }
    }
    return 0;
}

/* bring up GPU2D + NemaGFX + GFXMMU; returns 0 on success */
int gpu_resize_init(void)
{
    if (g_gpu.inited) {
        return 0;
    }
    if (g_gpu.failed) {
        return -1;
    }

    g_gpu.lock = osMutexNew(NULL);
    if (g_gpu.lock == NULL) {
        g_gpu.failed = 1;
        return -1;
    }

    hgpu2d.Instance = (uint32_t)GPU2D;
    if (HAL_GPU2D_Init(&hgpu2d) != HAL_OK) {
        printf("[gpu] GPU2D init failed\r\n");
        g_gpu.failed = 1;
        return -1;
    }

    nema_init();
    if (nema_get_error()) {
        printf("[gpu] nema_init error %lu\r\n", (unsigned long)nema_get_error());
        g_gpu.failed = 1;
        return -1;
    }

    g_gpu.gfxmmu.Instance = GFXMMU;
    g_gpu.gfxmmu.Init.BlockSize = GFXMMU_12BYTE_BLOCKS;
    g_gpu.gfxmmu.Init.AddressTranslation = DISABLE;
    if (HAL_GFXMMU_Init(&g_gpu.gfxmmu) != HAL_OK) {
        printf("[gpu] GFXMMU init failed\r\n");
        g_gpu.failed = 1;
        return -1;
    }

    GFXMMU_PackingTypeDef packing = {0};
    packing.Buffer0Activation = ENABLE;
    packing.Buffer0Mode = GFXMMU_PACKING_MSB_REMOVE;
    packing.DefaultAlpha = 0xff;
    if (HAL_GFXMMU_ConfigPacking(&g_gpu.gfxmmu, &packing) != HAL_OK) {
        printf("[gpu] GFXMMU packing failed\r\n");
        g_gpu.failed = 1;
        return -1;
    }

    nema_enable_tiling(1);

    /* N6: the GPU2D fetches command lists (and texture data) through the
     * shared ICACHE. Without it enabled+managed, the GPU executes STALE
     * cached command streams - blit output then ignores every parameter
     * we bind (the ST reference invalidates after each completion). */
    if (!HAL_ICACHE_IsEnabled()) {
        if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_4WAYS) != HAL_OK ||
            HAL_ICACHE_Enable() != HAL_OK) {
            printf("[gpu] ICACHE enable failed\r\n");
        }
    }
    (void)HAL_ICACHE_Invalidate();

    g_gpu.inited = 1;
    printf("[gpu] NemaGFX up on %s\r\n", nema_get_sw_device_name());
    return 0;
}

int gpu_resize_is_available(void)
{
    return g_gpu.inited;
}

/* blit core; mode selects the strategy under diagnosis:
 *   1 = src RGB24,  dst RGBA8888 in the GFXMMU virtual window (packing)
 *   0 = src RGB24,  dst RGB24 bound at the physical address
 *   2 = src RGBA,   dst RGB24 direct
 *   3 = src RGBA,   dst RGBA8888 in the virtual window  <- reference combo */
static int gpu_blit(const uint8_t *src, uint32_t sw, uint32_t sh,
                    uint8_t *dst, uint32_t dw, uint32_t dh, int mode)
{
    if (src == NULL || dst == NULL || sw == 0 || sh == 0 || dw == 0 || dh == 0) {
        return -1;
    }
    if (gpu_resize_init() != 0) {
        return -1;
    }
    if ((size_t)dw * dh * 4u > 4u * 1024u * 1024u) {
        return -1;
    }

    if (osMutexAcquire(g_gpu.lock, 5000) != osOK) {
        return -1;
    }

    /* Fresh, non-circular command list per blit: sharing one circular CL
     * across the AI task and diagnostics made the GPU execute stale ring
     * content (output ignored every bind). A per-call CL is ~100 words -
     * negligible at inference cadence - and removes all reuse semantics. */
    nema_cmdlist_t cl = nema_cl_create();
    if (cl.bo.base_virt == NULL) {
        printf("[gpu] cl create failed\r\n");
        (void)osMutexRelease(g_gpu.lock);
        return -1;
    }
    nema_cl_bind(&cl);

    if ((mode & 1) != 0) {
        GFXMMU_BuffersTypeDef buffers = {0};
        buffers.Buf0Address = (uint32_t)(uintptr_t)dst;
        if (HAL_GFXMMU_ModifyBuffers(&g_gpu.gfxmmu, &buffers) != HAL_OK) {
            nema_cl_destroy(&cl);
            (void)osMutexRelease(g_gpu.lock);
            return -1;
        }
        nema_bind_dst_tex(GFXMMU_VIRTUAL_BUFFER0_BASE, dw, dh, NEMA_RGBA8888, -1);
    } else {
        nema_bind_dst_tex((uintptr_t)dst, dw, dh, NEMA_RGB24, -1);
    }
    nema_set_clip(0, 0, dw, dh);
    nema_clear(0);
    nema_bind_src_tex((uintptr_t)src, sw, sh,
                      (mode >= 2) ? NEMA_RGBA8888 : NEMA_RGB24, -1,
                      NEMA_FILTER_BL);
    nema_set_blend_blit(NEMA_BL_SRC);
    /* Stretch the full texture over the destination rect via the
     * quad_fit path - the one ST's N6 reference exercises. */
    nema_blit_quad_fit(0.f, 0.f,
                       (float)dw, 0.f,
                       (float)dw, (float)dh,
                       0.f, (float)dh);

    /* source lines may be dirty in the D-cache (written by the CPU or the
     * previous pass): clean so the GPU reads memory, not stale cache */
    {
        uint32_t src_bpp = (mode >= 2) ? 4u : 3u;
        SCB_CleanDCache_by_Addr((void *)(uintptr_t)src,
                                (int32_t)(sw * sh * src_bpp + 31u) & ~31);
    }

    nema_cl_submit(&cl);
    if (gpu_cl_wait_next(200) != 0) {
        printf("[gpu] blit completion timeout - latching software fallback\r\n");
        g_gpu.failed = 1;
        nema_cl_destroy(&cl);
        (void)osMutexRelease(g_gpu.lock);
        return -1;
    }
    /* drop possibly-stale ICACHE lines so the next GPU fetch re-reads
     * memory (reference: HAL_ICACHE_Invalidate after every completion) */
    (void)HAL_ICACHE_Invalidate();
    nema_cl_destroy(&cl);

    /* GPU wrote via bus/translation: drop stale D-cache lines for the
     * destination before the CPU/NPU reads it */
    SCB_InvalidateDCache_by_Addr((void *)(uintptr_t)dst,
                                 (int32_t)(dw * dh * 3u + 31u) & ~31);

    int err = nema_get_error();
    (void)osMutexRelease(g_gpu.lock);
    return err ? -1 : 0;
}

/* bilinear RGB888 resize src(sw x sh) -> dst(dw x dh) on GPU2D; 0 on success */
int gpu_resize_rgb888(const uint8_t *src, uint32_t sw, uint32_t sh,
                      uint8_t *dst, uint32_t dw, uint32_t dh)
{
    return gpu_blit(src, sw, sh, dst, dw, dh, 1);
}

/* diagnostic variant: direct RGB24 destination, no GFXMMU */
int gpu_resize_rgb888_direct(const uint8_t *src, uint32_t sw, uint32_t sh,
                             uint8_t *dst, uint32_t dw, uint32_t dh)
{
    return gpu_blit(src, sw, sh, dst, dw, dh, 0);
}

/* diagnostic variant: RGBA8888 source -> RGB24 destination, no GFXMMU */
int gpu_resize_rgba8888_to_rgb888(const uint8_t *src, uint32_t sw, uint32_t sh,
                                  uint8_t *dst, uint32_t dw, uint32_t dh)
{
    return gpu_blit(src, sw, sh, dst, dw, dh, 2);
}

/* diagnostic variant: full reference combo - RGBA8888 source, RGBA8888 dst
 * in the GFXMMU virtual window with MSB-remove packing. */
int gpu_resize_rgba8888_packed(const uint8_t *src, uint32_t sw, uint32_t sh,
                               uint8_t *dst, uint32_t dw, uint32_t dh)
{
    return gpu_blit(src, sw, sh, dst, dw, dh, 3);
}
