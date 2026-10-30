
#include "lc_arena.h"
#include "lc_compat.h"

typedef union lc_block {
    struct {
        uint32_t size;
        uint32_t used;
    } h;
    uint64_t align;
} lc_block_t;

static uint8_t  lc_arena_pool[LC_ARENA_SIZE];
static uint8_t  lc_arena_ready;
static size_t   lc_arena_used_now;
static size_t   lc_arena_high;

#define LC_ARENA_ALIGN 8u

static size_t lc_round_up(size_t v, size_t a)
{
    return (v + a - 1u) / a * a;
}

void lcapp_arena_init(void)
{
    lc_block_t *b = (lc_block_t *)lc_arena_pool;
    b->h.size = (uint32_t)(LC_ARENA_SIZE - sizeof(lc_block_t));
    b->h.used = 0u;
    lc_arena_ready = 1u;
    lc_arena_used_now = 0u;
    lc_arena_high = 0u;
}

static lc_block_t *lc_next(lc_block_t *b, lc_block_t *end)
{
    uint8_t *p = (uint8_t *)b + sizeof(lc_block_t) + b->h.size;
    if (p >= (uint8_t *)end) return NULL;
    return (lc_block_t *)p;
}

void *lcapp_alloc(size_t sz)
{
    if (!lc_arena_ready) lcapp_arena_init();
    if (sz == 0u) sz = 1u;
    sz = lc_round_up(sz, LC_ARENA_ALIGN);

    lc_block_t *end = (lc_block_t *)(lc_arena_pool + LC_ARENA_SIZE);
    lc_block_t *b = (lc_block_t *)lc_arena_pool;
    while (b != NULL) {
        if (!b->h.used && b->h.size >= sz) {
            uint32_t rem = b->h.size - (uint32_t)sz;
            if (rem >= sizeof(lc_block_t) + LC_ARENA_ALIGN) {

                lc_block_t *nb = (lc_block_t *)((uint8_t *)b + sizeof(lc_block_t) + sz);
                nb->h.size = rem - (uint32_t)sizeof(lc_block_t);
                nb->h.used = 0u;
                b->h.size = (uint32_t)sz;
            }
            b->h.used = 1u;
            lc_arena_used_now += sizeof(lc_block_t) + b->h.size;
            if (lc_arena_used_now > lc_arena_high) lc_arena_high = lc_arena_used_now;
            return (void *)((uint8_t *)b + sizeof(lc_block_t));
        }
        b = lc_next(b, end);
    }
    return NULL;
}

void lcapp_free(void *p)
{
    if (p == NULL) return;
    lc_block_t *b = (lc_block_t *)((uint8_t *)p - sizeof(lc_block_t));
    if ((uint8_t *)b < lc_arena_pool ||
        (uint8_t *)b >= lc_arena_pool + LC_ARENA_SIZE) {
        return;
    }
    b->h.used = 0u;
    lc_arena_used_now -= sizeof(lc_block_t) + b->h.size;

    lc_block_t *end = (lc_block_t *)(lc_arena_pool + LC_ARENA_SIZE);
    for (;;) {
        lc_block_t *n = lc_next(b, end);
        if (n == NULL || n->h.used) break;
        b->h.size += (uint32_t)(sizeof(lc_block_t) + n->h.size);
    }

    lc_block_t *prev = (lc_block_t *)lc_arena_pool;
    while (prev != b) {
        lc_block_t *n = lc_next(prev, end);
        if (n == b) {
            if (!prev->h.used) {
                prev->h.size += (uint32_t)(sizeof(lc_block_t) + b->h.size);
            }
            break;
        }
        if (n == NULL) break;
        prev = n;
    }
}

size_t lcapp_arena_used(void) { return lc_arena_used_now; }
size_t lcapp_arena_high_water(void) { return lc_arena_high; }
