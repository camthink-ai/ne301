#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mpool.h"
#include "mem.h"

static int g_failures = 0;

#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) {                                                          \
            printf("PASS %s\n", name);                                       \
        } else {                                                             \
            printf("FAIL %s\n", name);                                       \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static void *g_inject_ptr = NULL;

static char g_last_diag[256];
static int g_diag_count = 0;

void mem_guard_record_diag(const char *line)
{
    snprintf(g_last_diag, sizeof(g_last_diag), "%s", line);
    g_diag_count++;
}

void ngx_slab_init(ngx_slab_pool_t *pool)
{
    pool->page_size = 64;
    pool->start = (u_char *)pool + sizeof(ngx_slab_pool_t);
}

void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size)
{
    (void)pool;
    (void)size;
    return g_inject_ptr;
}

void *ngx_slab_alloc_aligned(ngx_slab_pool_t *pool, size_t size, size_t alignment)
{
    (void)pool;
    (void)size;
    (void)alignment;
    return g_inject_ptr;
}

ngx_int_t ngx_slab_free(ngx_slab_pool_t *pool, void *p)
{
    (void)pool;
    (void)p;
    return 0;
}

void ngx_slab_stat(ngx_slab_pool_t *pool, ngx_slab_info_t *info)
{
    (void)pool;
    (void)info;
}

static void test_predicate_interval_semantics(void)
{
    static uint8_t region[4096];
    ngx_slab_pool_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.start = region + 64;
    sp.end = region + sizeof(region);

    CHECK(ngx_slab_ptr_in_pool(&sp, region + 128, 32), "predicate: in-pool accepted");
    CHECK(!ngx_slab_ptr_in_pool(&sp, region, 32), "predicate: below start rejected");
    CHECK(!ngx_slab_ptr_in_pool(&sp, region + sizeof(region), 1), "predicate: at end rejected");
    CHECK(!ngx_slab_ptr_in_pool(&sp, region + sizeof(region) - 8, 64),
          "predicate: allocation tail beyond end rejected");
    CHECK(!ngx_slab_ptr_in_pool(&sp, region + 128, (size_t)-1),
          "predicate: huge size rejected without wraparound");

    ngx_slab_pool_t corrupt;
    memset(&corrupt, 0, sizeof(corrupt));
    corrupt.start = region + 2048;
    corrupt.end = region;
    CHECK(!ngx_slab_ptr_in_pool(&corrupt, region + 100, 8),
          "predicate: corrupted start/end order rejected");

    ngx_slab_pool_t uninit;
    memset(&uninit, 0, sizeof(uninit));
    CHECK(!ngx_slab_ptr_in_pool(&uninit, region, 8),
          "predicate: null pool bounds rejected");
}

static void expect_diag(const char *needle, const char *name)
{
    CHECK(g_diag_count == 1 && strstr(g_last_diag, needle) != NULL, name);
    g_diag_count = 0;
}

static void test_alloc_rejects_out_of_pool_injection(void)
{
    static uint8_t int_buf[512 * 1024];
    static uint8_t ext_buf[256 * 1024];

    CHECK(hal_mem_init(int_buf, ext_buf) == MEM_OK, "alloc: pools initialize");

    ngx_slab_pool_t *sp = (ngx_slab_pool_t *)int_buf;
    u_char *start = sp->start;
    u_char *end = sp->end;

    g_diag_count = 0;
    g_inject_ptr = end - 8;
    CHECK(hal_mem_alloc(64, MEM_FAST) == NULL, "alloc: tail-overrun pointer rejected");
    expect_diag("slab alloc out of pool", "alloc: rejection diagnostic preserved");

    g_inject_ptr = start + 4096;
    CHECK(hal_mem_alloc(64, MEM_FAST) == g_inject_ptr, "alloc: in-pool pointer passes");

    g_inject_ptr = ext_buf + 200 * 1024 + 4096;
    CHECK(hal_mem_alloc(64, MEM_FAST) == NULL, "alloc: foreign pointer rejected");
    expect_diag("slab alloc out of pool", "alloc: foreign rejection diagnostic");

    g_inject_ptr = end - 8;
    CHECK(hal_mem_alloc_aligned(64, 32, MEM_FAST) == NULL,
          "aligned: tail-overrun pointer rejected");
    expect_diag("slab alloc aligned out of pool", "aligned: rejection diagnostic preserved");

    g_inject_ptr = start + 8192;
    CHECK(hal_mem_alloc_aligned(64, 32, MEM_FAST) == g_inject_ptr,
          "aligned: in-pool pointer passes");

    g_inject_ptr = (u_char *)int_buf - 4096;
    CHECK(hal_mem_alloc_aligned(64, 32, MEM_FAST) == NULL,
          "aligned: below-pool pointer rejected");
    expect_diag("slab alloc aligned out of pool", "aligned: below-pool diagnostic");

    g_inject_ptr = NULL;
    hal_mem_deinit();
}

int main(void)
{
    test_predicate_interval_semantics();
    test_alloc_rejects_out_of_pool_injection();
    printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
