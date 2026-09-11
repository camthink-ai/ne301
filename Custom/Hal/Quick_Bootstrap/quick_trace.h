#ifndef __QUICK_TRACE_H__
#define __QUICK_TRACE_H__

/* Shared logging + optional trace/profiling for the Quick_Bootstrap modules
 * (quick_snapshot, quick_storage). Logging is plain printf — these modules log
 * sparsely, so the newlib re-entrancy/garble tradeoff is accepted over a mutex.
 */
#include <stdint.h>
#include <stdio.h>

/* ---- logging (always compiled; gated by level) ---- */
#define QB_LOG_LEVEL_NONE   0
#define QB_LOG_LEVEL_ERROR  1
#define QB_LOG_LEVEL_WARN   2
#define QB_LOG_LEVEL_DEBUG  3
#ifndef QB_LOG_LEVEL
#define QB_LOG_LEVEL        QB_LOG_LEVEL_DEBUG
#endif
#define QB_LOG_TAG          "[QB] "

#if QB_LOG_LEVEL >= QB_LOG_LEVEL_ERROR
#define QB_LOGE(fmt, ...)   printf(QB_LOG_TAG fmt "\r\n", ##__VA_ARGS__)
#else
#define QB_LOGE(fmt, ...)
#endif

#if QB_LOG_LEVEL >= QB_LOG_LEVEL_WARN
#define QB_LOGW(fmt, ...)   printf(QB_LOG_TAG fmt "\r\n", ##__VA_ARGS__)
#else
#define QB_LOGW(fmt, ...)
#endif

#if QB_LOG_LEVEL >= QB_LOG_LEVEL_DEBUG
#define QB_LOGD(fmt, ...)   printf(QB_LOG_TAG fmt "\r\n", ##__VA_ARGS__)
#else
#define QB_LOGD(fmt, ...)
#endif

/* ---- optional lightweight traces/profiling (compile-time only).
 * Keep default OFF to avoid extra runtime overhead in production builds. */
#ifndef QB_TRACE_ENABLE
#define QB_TRACE_ENABLE     0
#endif

#ifndef QB_TIMELOG_ENABLE
#define QB_TIMELOG_ENABLE   0
#endif

#if QB_TRACE_ENABLE
/* tag can be const string OR runtime pointer */
#define QT_TRACE(tag, fmt, ...)  printf("%s" fmt "\r\n", (tag), ##__VA_ARGS__)
#else
#define QT_TRACE(tag, fmt, ...)  do { } while (0)
#endif

/* Step timing logs are gated independently to avoid extra time reads. */
#if QB_TIMELOG_ENABLE
#include "drtc.h"
typedef struct {
    uint64_t t0_ms;
    uint64_t last_ms;
} qt_prof_t;

static inline uint64_t qt_now_ms(void)
{
    return rtc_get_uptime_ms();
}

static inline void qt_prof_init(qt_prof_t *p)
{
    if (!p) return;
    p->t0_ms = qt_now_ms();
    p->last_ms = p->t0_ms;
}

static inline void qt_prof_step(qt_prof_t *p, const char *tag)
{
    if (!p || !tag) return;
    uint64_t now = qt_now_ms();
    QT_TRACE(tag, "+%lums (%lums)",
             (unsigned long)(now - p->last_ms),
             (unsigned long)(now - p->t0_ms));
    p->last_ms = now;
}
#else
typedef struct {
    uint8_t _dummy;
} qt_prof_t;
#define qt_prof_init(p)        do { (void)(p); } while (0)
#define qt_prof_step(p, tag)   do { (void)(p); (void)(tag); } while (0)
#endif

#endif /* __QUICK_TRACE_H__ */
