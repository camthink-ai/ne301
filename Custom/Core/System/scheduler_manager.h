#ifndef SCHEDULER_MANAGER_H
#define SCHEDULER_MANAGER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define WEEKDAY_MON    0x01
#define WEEKDAY_TUE    0x02
#define WEEKDAY_WED    0x04
#define WEEKDAY_THU    0x08
#define WEEKDAY_FRI    0x10
#define WEEKDAY_SAT    0x20
#define WEEKDAY_SUN    0x40

#define WEEKDAYS_WORKDAY   (WEEKDAY_MON | WEEKDAY_TUE | WEEKDAY_WED | WEEKDAY_THU | WEEKDAY_FRI)  // 0x1F
#define WEEKDAYS_WEEKEND   (WEEKDAY_SAT | WEEKDAY_SUN)                                            // 0x60
#define WEEKDAYS_ALL       0x7F

typedef void (*sched_lock_func_t)(void);
typedef void (*sched_unlock_func_t)(void);
typedef uint64_t (*get_time_func_t)(void);
typedef void (*wakeup_set_func_t)(int id, uint64_t wake_time);
typedef void (*wakeup_callback_t)(void);

/* New data structures */
typedef enum {
    REPEAT_ONCE,     // Once
    REPEAT_DAILY,    // Daily
    REPEAT_WEEKLY,    // Weekly (using weekdays bitmask)
    REPEAT_INTERVAL,  // Interval cycle (e.g., every N seconds/minutes/hours/days)
} repeat_type_t;

typedef struct {
    uint32_t start_sec;  // Start seconds of the day (0-86399)
    uint32_t end_sec;    // End seconds of the day (0-86399)
    repeat_type_t repeat;
    union {
        uint8_t weekdays; // Valid for weekly repeat (bit0=Monday...bit6=Sunday)
        uint16_t reserved;
    };
} schedule_period_t;

typedef struct scheduler {
    int id;
    wakeup_set_func_t set_wakeup;
    wakeup_callback_t callback;
} scheduler_t;

typedef enum {
    WAKEUP_TYPE_INTERVAL,
    WAKEUP_TYPE_ABSOLUTE
} wakeup_type_t;

typedef struct wakeup_job {
    char name[32];
    struct scheduler *sched;
    wakeup_type_t type;
    repeat_type_t repeat;
    union {
        struct {
            uint32_t trigger_sec;  // Trigger seconds of the day (0-86399)
            int16_t day_offset;    // Day offset (for cross-day/weekly)
            uint8_t weekdays;
        };
        uint64_t interval;         // Interval seconds
    };
    void (*callback)(void *arg);
    void *arg;
    uint64_t next_trigger;
    struct wakeup_job *next;
} wakeup_job_t;

typedef struct schedule_job {
    char name[32];
    struct scheduler *sched;
    schedule_period_t *periods;
    int period_count;
    uint64_t interval;
    void (*enter_cb)(void *arg);
    void (*exit_cb)(void *arg);
    void *arg;
    uint8_t is_inside;
    uint64_t next_trigger;
    struct schedule_job *next;
} schedule_job_t;

typedef struct {
    get_time_func_t get_time;
    get_time_func_t get_wakeup_time;
    scheduler_t *schedulers;
    int num_sched;
    wakeup_job_t *wake_jobs;
    schedule_job_t *schedule_jobs;
    sched_lock_func_t lock;
    sched_unlock_func_t unlock;
    bool thread_safe;
    int timezone;
} scheduler_manager_t;

/* New query data structure */
typedef enum {
    QUERY_BY_SCHEDULER,
    QUERY_BY_NAME,
    QUERY_BY_TYPE
} query_type_t;

typedef struct {
    const char *name;
    query_type_t type;
    union {
        int sched_id;
        struct {
            bool wakeup_absolute;
            bool wakeup_interval;
            bool schedule;
        };
    };
} query_filter_t;

typedef struct {
    char name[64];
    enum { WAKEUP_JOB, SCHEDULE_JOB } type;
    union {
        struct {
            wakeup_type_t wake_type;
            repeat_type_t repeat;
            union {
                uint64_t absolute_time;
                uint64_t interval;
            };
        };
        struct {
            int period_count;
            schedule_period_t *periods;
        };
    };
} query_result_t;


int register_wakeup_ex(scheduler_manager_t *mgr, int sched_id,
                      const char *name, wakeup_type_t type,
                      uint32_t day_sec, int16_t day_offset,
                      repeat_type_t repeat, uint8_t weekdays,
                      void (*cb)(void*), void *arg);

// Register without locking — caller must already hold scheduler lock (for use within callbacks)
int register_wakeup_ex_locked(scheduler_manager_t *mgr, int sched_id,
                              const char *name, wakeup_type_t type,
                              uint32_t day_sec, int16_t day_offset,
                              repeat_type_t repeat, uint8_t weekdays,
                              void (*cb)(void*), void *arg);

int register_schedule_ex(scheduler_manager_t *mgr, int sched_id,
                        const char *name, schedule_period_t *periods, int period_count,
                        void (*enter)(void*), void (*exit)(void*), void *arg);

int unregister_task_by_name(scheduler_manager_t *mgr, const char *name);

int query_tasks(scheduler_manager_t *mgr, query_filter_t filter,query_result_t **results, int *count);

void free_query_results(query_result_t *results, int count);

void scheduler_handle_event(scheduler_t *sched, scheduler_manager_t *mgr);

void scheduler_init(scheduler_manager_t *mgr, get_time_func_t get_time, get_time_func_t get_wakeup_time, 
                   scheduler_t *scheds, int num_sched,
                   sched_lock_func_t lock, sched_unlock_func_t unlock) ;
#endif