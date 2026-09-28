#ifndef GPU_RESIZE_H
#define GPU_RESIZE_H

#include <stdint.h>

/* Bring up GPU2D (NemaGFX) + GFXMMU. Idempotent; a failed bring-up is
 * remembered so callers cheaply fall back to the software resize. */
int gpu_resize_init(void);

int gpu_resize_is_available(void);

/* Bilinear RGB888 resize on GPU2D. 0 on success; non-zero -> caller should
 * fall back to the software path. Buffers must be GPU-reachable (external
 * pool) and the destination must fit the 4MB GFXMMU virtual window. */
int gpu_resize_rgb888(const uint8_t *src, uint32_t sw, uint32_t sh,
                      uint8_t *dst, uint32_t dw, uint32_t dh);

/* Diagnostic variant: RGB24 destination bound at the physical address,
 * GFXMMU bypassed. Used by gputest to isolate packing-related issues. */
int gpu_resize_rgb888_direct(const uint8_t *src, uint32_t sw, uint32_t sh,
                             uint8_t *dst, uint32_t dw, uint32_t dh);

/* Diagnostic variant: RGBA8888 source (4B/px) -> RGB24 destination. */
int gpu_resize_rgba8888_to_rgb888(const uint8_t *src, uint32_t sw, uint32_t sh,
                                  uint8_t *dst, uint32_t dw, uint32_t dh);

/* Diagnostic variant: full reference combo (RGBA8888 src, packed window dst). */
int gpu_resize_rgba8888_packed(const uint8_t *src, uint32_t sw, uint32_t sh,
                               uint8_t *dst, uint32_t dw, uint32_t dh);

#endif /* GPU_RESIZE_H */
