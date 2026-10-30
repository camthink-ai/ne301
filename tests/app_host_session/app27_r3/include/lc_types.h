#ifndef LC_TYPES_H
#define LC_TYPES_H

#include <stdint.h>

typedef struct { float x, y; } lc_point_t;

#define LC_BIT_IN  0x01u
#define LC_BIT_OUT 0x02u

#define LC_K_MAX         16u
#define LC_MAX_TRACKS   64u

typedef enum {
    LC_SEG_DEPARTED = 0,
    LC_SEG_CROSSING = 1
} lc_seg_end_t;

typedef enum {
    LC_CROSS_NONE = 0,
    LC_CROSS_IN   = 1,
    LC_CROSS_OUT  = 2
} lc_cross_event_t;

typedef struct {
    uint32_t       track_id;
    uint32_t       segment_id;
    uint32_t       entered_at_ms;
    uint32_t       seg_start_ms;
    uint32_t       seg_end_ms;
    lc_seg_end_t   seg_end_type;
    uint8_t        events;
    uint8_t        nb_points;
    lc_point_t     points[1];
} lc_track_record_t;

#define LC_TRACK_RECORD_SIZE(nb) \
    (sizeof(lc_track_record_t)                                       \
     + ((nb) > 0u ? ((nb) - 1u) * sizeof(lc_point_t) : 0u)           \
     + ((nb) > 0u ?  (nb)        * sizeof(uint32_t)   : 0u))

static inline uint32_t* lc_track_record_point_ts(lc_track_record_t* r) {
    return (uint32_t*)((uint8_t*)r + sizeof(lc_track_record_t)
                       + (r->nb_points > 0u ? (r->nb_points - 1u) * sizeof(lc_point_t) : 0u));
}
static inline const uint32_t* lc_track_record_point_ts_const(const lc_track_record_t* r) {
    return (const uint32_t*)((const uint8_t*)r + sizeof(lc_track_record_t)
                             + (r->nb_points > 0u ? (r->nb_points - 1u) * sizeof(lc_point_t) : 0u));
}

/*
 * Ported (ne301 counting@de25a6f1): the only platform seam in the
 * original code was heap allocation ("mem.h" / hal_mem_alloc_any /
 * hal_mem_free on NE301).  This port defaults to the C standard
 * library; an embedder may substitute a platform allocator without
 * touching algorithm code by defining LC_MALLOC / LC_FREE before
 * including any lc_*.h header.
 */
#ifndef LC_MALLOC
    #include <stdlib.h>
    #define LC_MALLOC(sz)     malloc(sz)
#endif
#ifndef LC_FREE
    #define LC_FREE(p)        free(p)
#endif

static inline uint8_t lc_serial_newer(uint32_t candidate, uint32_t current)
{
    if (candidate == current) return 0u;
    return ((int32_t)(candidate - current) > 0) ? 1u : 0u;
}

static inline uint32_t lc_serial_next(uint32_t seq)
{
    uint32_t next = seq + 1u;
    return next ? next : 1u;
}


#endif
