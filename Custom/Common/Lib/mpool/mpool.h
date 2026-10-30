
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>


typedef intptr_t        ngx_int_t;
typedef uintptr_t       ngx_uint_t;
typedef intptr_t        ngx_flag_t;
typedef unsigned char   u_char;
typedef struct ngx_slab_page_s  ngx_slab_page_t;

#define ngx_memzero(buf, n)       (void) memset(buf, 0, n)
#define ngx_memset(buf, c, n)     (void) memset(buf, c, n)
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
#define ngx_align_ptr(p, a)    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

#define LV_TRACE 0x01
#define LV_DEBUG 0x02
#define LV_INFO  0x04
#define LV_ERROR 0x08
#define LV_ALERT 0x10
#define LOG_LEVEL   (LV_ERROR | LV_INFO)

#define log(level, format, ...) \
    do { \
        if ( level & LOG_LEVEL ) {\
            fprintf(stderr, "[%c] [%s:%d] "format"\r\n", \
                    #level[3],  __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#define trace(format, ...) log(LV_TRACE, format, ##__VA_ARGS__)
#define debug(format, ...) log(LV_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...)  log(LV_INFO , format, ##__VA_ARGS__)
#define error(format, ...) log(LV_ERROR, format, ##__VA_ARGS__)
#define alert(format, ...) log(LV_ALERT, format, ##__VA_ARGS__)


typedef struct ngx_slab_page_s {
    uintptr_t         slab;
    ngx_slab_page_t  *next;
    uintptr_t         prev;
} ngx_slab_page_t;


typedef struct {
    ngx_uint_t        total;
    ngx_uint_t        used;

    ngx_uint_t        reqs;
    ngx_uint_t        fails;
} ngx_slab_stat_t;


typedef struct {
    size_t            min_size;
    size_t            min_shift;
    size_t            page_size;

    ngx_slab_page_t  *pages;
    ngx_slab_page_t  *last;
    ngx_slab_page_t   free;

    ngx_slab_stat_t  *stats;
    ngx_uint_t        pfree;

    u_char           *start;
    u_char           *end;

    void             *addr;
} ngx_slab_pool_t;

static inline bool ngx_slab_ptr_in_pool(const ngx_slab_pool_t *pool, const void *p, size_t size)
{
    uintptr_t addr = (uintptr_t)p;
    uintptr_t start = (uintptr_t)pool->start;
    uintptr_t end = (uintptr_t)pool->end;
    return addr >= start && addr < end && size <= end - start && addr <= end - size;
}

typedef struct {
    size_t         pool_size, used_size, used_pct;
    size_t         pages, free_page;
    size_t         p_small, p_exact, p_big, p_page; /* Number of pages occupied by four types of slabs */
    size_t         b_small, b_exact, b_big, b_page; /* Number of bytes occupied by four types of slabs */
    size_t         max_free_pages;                  /* Maximum number of consecutive free pages */
} ngx_slab_info_t;


void ngx_slab_init(ngx_slab_pool_t *pool);
void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_aligned(ngx_slab_pool_t *pool, size_t size, size_t alignment);
void *ngx_slab_alloc_aligned_locked(ngx_slab_pool_t *pool, size_t size, size_t alignment);
void *ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size);
ngx_int_t ngx_slab_free(ngx_slab_pool_t *pool, void *p);
ngx_int_t ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);
void ngx_slab_stat(ngx_slab_pool_t *pool, ngx_slab_info_t *stat_info);
bool ngx_slab_contains(ngx_slab_pool_t *pool, void *ptr);
bool ngx_slab_validate(ngx_slab_pool_t *pool);

#endif /* _NGX_SLAB_H_INCLUDED_ */

