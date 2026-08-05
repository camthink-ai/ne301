#include "scheduler_manager.h"
#include "buffer_mgr.h"

#define LOCK(mgr) do { if ((mgr)->thread_safe) (mgr)->lock(); } while(0)
#define UNLOCK(mgr) do { if ((mgr)->thread_safe) (mgr)->unlock(); } while(0)


static uint64_t get_midnight(uint64_t now)
{
    time_t t = (time_t)now;
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return (uint64_t)mktime(&tm);
}

static uint64_t day_offset_timestamp(uint64_t now, int day_offset, uint32_t day_sec)
{
    uint64_t midnight = get_midnight(now);
    return midnight + day_offset * 86400 + day_sec;
}

static uint64_t convert_day_seconds_to_timestamp(uint64_t now, uint32_t day_sec, int16_t day_offset)
{
    return day_offset_timestamp(now, day_offset, day_sec);
}


// Find scheduler
static scheduler_t* find_scheduler(scheduler_manager_t *mgr, int sched_id) 
{
    for (int i = 0; i < mgr->num_sched; i++) {
        if (mgr->schedulers[i].id == sched_id) {
            return &mgr->schedulers[i];
        }
    }
    return NULL;
}

// Update scheduler wakeup time
static void update_scheduler_wakeup(scheduler_t *sched, scheduler_manager_t *mgr) 
{
    uint64_t min_time = UINT64_MAX;
    // uint64_t now = mgr->get_time();

    // Find minimum time for wakeup jobs
    for (wakeup_job_t *job = mgr->wake_jobs; job; job = job->next) {
        if (job->sched == sched && job->next_trigger < min_time) {
            min_time = job->next_trigger;
        }
    }

    // Find minimum time for schedule jobs
    for (schedule_job_t *job = mgr->schedule_jobs; job; job = job->next) {
        if (job->sched == sched && job->next_trigger < min_time) {
            min_time = job->next_trigger;
        }
    }

    if (min_time != UINT64_MAX) {
        min_time -= (mgr->timezone * 3600);
        sched->set_wakeup(sched->id, min_time);
    }
}


static uint64_t calculate_wakeup_trigger(wakeup_job_t *job, uint64_t now) 
{
    switch (job->repeat) {
        case REPEAT_ONCE:
            return convert_day_seconds_to_timestamp(now, job->trigger_sec, job->day_offset);
            
        case REPEAT_DAILY: 
            uint64_t today_trigger = convert_day_seconds_to_timestamp(now, job->trigger_sec, 0);
            if (now < today_trigger)
                return today_trigger;
            else
                return today_trigger + 86400; // Tomorrow
    
        case REPEAT_WEEKLY: 
            time_t t_now = (time_t)now;
            struct tm tm;
            localtime_r(&t_now, &tm);
            int today_wday = (tm.tm_wday + 6) % 7; // 0=Monday, ..., 6=Sunday

            for (int i = 0; i < 7; ++i) {
                int wday = (today_wday + i) % 7;
                if (job->weekdays & (1 << wday)) {
                    uint64_t trigger = convert_day_seconds_to_timestamp(now, job->trigger_sec, i);
                    if (trigger >= now)
                        return trigger;
                }
            }
            // If all are less than now, find the next closest one
            for (int i = 1; i <= 7; ++i) {
                int wday = (today_wday + i) % 7;
                if (job->weekdays & (1 << wday)) {
                    return convert_day_seconds_to_timestamp(now, job->trigger_sec, i);
                }
            }
            return UINT64_MAX;
        case REPEAT_INTERVAL:
            return now + job->interval;
    }
    return UINT64_MAX;
}
// Process wakeup jobs
static void process_wakeup_jobs(scheduler_t *sched, scheduler_manager_t *mgr)
{
    uint64_t now = mgr->get_time() + (mgr->timezone * 3600);
    wakeup_job_t **prev = &mgr->wake_jobs;
    wakeup_job_t *job = mgr->wake_jobs;

    while (job) {
        if (job->sched == sched && job->next_trigger <= now) {
            if (job->repeat == REPEAT_ONCE) {
                // Remove from list BEFORE callback so the callback can safely
                // register new jobs without corrupting the traversal.
                *prev = job->next;
                wakeup_job_t *once_job = job;
                job = *prev;

                if (once_job->callback) once_job->callback(once_job->arg);
                buffer_free(once_job);
                continue;
            }

            // Execute callback for non-ONCE jobs
            if (job->callback) job->callback(job->arg);

            // Calculate next trigger time
            uint64_t next_trigger = UINT64_MAX;
            switch (job->repeat) {
                case REPEAT_DAILY:
                case REPEAT_WEEKLY:
                    next_trigger = calculate_wakeup_trigger(job, now + 1);  // Pass now+1 to prevent infinite loop
                    break;
                case REPEAT_INTERVAL:
                    // Skip all intervals until future
                    do {
                        job->next_trigger += job->interval;
                    } while (job->next_trigger <= now);
                    next_trigger = job->next_trigger;
                    break;
                default:
                    break;
            }
            job->next_trigger = next_trigger;
        }
        prev = &job->next;
        job = job->next;
    }
}

/* Time calculation function */
static uint64_t calculate_schedule_trigger(const schedule_period_t *period, 
                                         uint64_t now) 
{
    uint64_t midnight = get_midnight(now);
    uint64_t start_today = midnight + period->start_sec;
    uint64_t end_today = midnight + period->end_sec;

    if (period->end_sec < period->start_sec) {
        end_today += 86400; // Cross-day
    }
    
    switch (period->repeat) {
        case REPEAT_ONCE:
            return (now < start_today) ? start_today : UINT64_MAX;

        case REPEAT_DAILY:
            if (now < start_today)
                return start_today;
            else if (now < end_today)
                return end_today;
            else
                return midnight + 86400 + period->start_sec;

        case REPEAT_WEEKLY: {
            time_t t_now = (time_t)now;
            struct tm tm_now;
            localtime_r(&t_now, &tm_now);
            int today_wday = (tm_now.tm_wday + 6) % 7;

            if (period->weekdays & (1 << today_wday)) {
                if (now < start_today)
                    return start_today;
                else if (now < end_today)
                    return end_today;
            }
            // Find next valid weekday
            for (int i = 1; i <= 7; i++) {
                int next_day = (today_wday + i) % 7;
                if (period->weekdays & (1 << next_day)) {
                    return midnight + 86400 * i + period->start_sec;
                }
            }
            break;
        }
        default:
            break;
    }
    return UINT64_MAX;
}

/* Modify schedule task processing logic */
static void process_schedule_jobs(scheduler_t *sched, scheduler_manager_t *mgr) 
{
    uint64_t now = mgr->get_time() + (mgr->timezone * 3600);
    schedule_job_t *job = mgr->schedule_jobs;

    while (job) {
        if (job->sched == sched && job->next_trigger <= now) {
            uint64_t next_trigger = UINT64_MAX;
            bool should_be_inside = false;

            // Check all time periods
            for (int i = 0; i < job->period_count; i++) {
                uint64_t trigger = calculate_schedule_trigger(&job->periods[i], now);
                if (trigger < next_trigger) {
                    next_trigger = trigger;
                }
                
                // Check if currently within this time period
                uint64_t period_start = convert_day_seconds_to_timestamp(now, job->periods[i].start_sec, 0);
                uint64_t period_end = convert_day_seconds_to_timestamp(now, job->periods[i].end_sec, 0);
                
                if (job->periods[i].end_sec < job->periods[i].start_sec) {
                    period_end += 86400; // Handle cross-day
                }
                
                should_be_inside = (now >= period_start && now < period_end);

            }
            
            if (should_be_inside && !job->is_inside) {
                if (job->enter_cb) job->enter_cb(job->arg);
                job->is_inside = true;
            } else if (!should_be_inside && job->is_inside) {
                if (job->exit_cb) job->exit_cb(job->arg);
                job->is_inside = false;
            }
            
            job->next_trigger = next_trigger;
        }
        job = job->next;
    }
}

// Register wakeup job
// NOTE: Keep register_wakeup_ex_locked() in sync when modifying this function
int register_wakeup_ex(scheduler_manager_t *mgr, int sched_id,
                      const char *name, wakeup_type_t type,
                      uint32_t day_sec, int16_t day_offset,
                      repeat_type_t repeat, uint8_t weekdays,
                      void (*cb)(void*), void *arg) 
{
    LOCK(mgr);

    scheduler_t *sched = find_scheduler(mgr, sched_id);
    if (!sched) {
        UNLOCK(mgr);
        return -1;
    }

    wakeup_job_t *job = buffer_calloc(1, sizeof(*job));
    memset(job, 0, sizeof(*job));
    strncpy(job->name, name, sizeof(job->name)-1);
    job->sched = sched;
    job->type = type;
    job->repeat = repeat;
    job->callback = cb;
    job->arg = arg;

    uint64_t now = mgr->get_time() + (mgr->timezone * 3600);
    uint64_t wk_time = mgr->get_wakeup_time() + (mgr->timezone * 3600);
    // printf("now=%lu\n", (uint32_t)now);
    // printf("wk_time=%lu\n", (uint32_t)wk_time);

    if (type == WAKEUP_TYPE_ABSOLUTE) {
        job->trigger_sec = day_sec % 86400;
        job->day_offset = day_offset;
        if (repeat == REPEAT_WEEKLY) {
            job->weekdays = weekdays;
        }
        job->next_trigger = calculate_wakeup_trigger(job, now);
    } else if (type == WAKEUP_TYPE_INTERVAL) {
        job->interval = day_sec;
        if (wk_time != 0 && (now - wk_time) < (job->interval - 10)) {
            job->next_trigger = wk_time + job->interval;
        } else {
            job->next_trigger = now + job->interval;
        }
        // printf("next_trigger=%lu\n", (uint32_t)job->next_trigger);
    }

    job->next = mgr->wake_jobs;
    mgr->wake_jobs = job;
    update_scheduler_wakeup(sched, mgr);

    UNLOCK(mgr);
    return 0;
}

// Internal: register without locking (caller must hold lock)
// NOTE: Keep in sync with register_wakeup_ex() above — only difference is no LOCK/UNLOCK
int register_wakeup_ex_locked(scheduler_manager_t *mgr, int sched_id,
                              const char *name, wakeup_type_t type,
                              uint32_t day_sec, int16_t day_offset,
                              repeat_type_t repeat, uint8_t weekdays,
                              void (*cb)(void*), void *arg)
{
    scheduler_t *sched = find_scheduler(mgr, sched_id);
    if (!sched) return -1;

    wakeup_job_t *job = buffer_calloc(1, sizeof(*job));
    memset(job, 0, sizeof(*job));
    strncpy(job->name, name, sizeof(job->name)-1);
    job->sched = sched;
    job->type = type;
    job->repeat = repeat;
    job->callback = cb;
    job->arg = arg;

    uint64_t now = mgr->get_time() + (mgr->timezone * 3600);

    if (type == WAKEUP_TYPE_ABSOLUTE) {
        job->trigger_sec = day_sec % 86400;
        job->day_offset = day_offset;
        if (repeat == REPEAT_WEEKLY) {
            job->weekdays = weekdays;
        }
        job->next_trigger = calculate_wakeup_trigger(job, now);
    } else if (type == WAKEUP_TYPE_INTERVAL) {
        job->interval = day_sec;
        job->next_trigger = now + job->interval;
    }

    job->next = mgr->wake_jobs;
    mgr->wake_jobs = job;
    update_scheduler_wakeup(sched, mgr);

    return 0;
}

// Register schedule task (supports multiple time periods)
int register_schedule_ex(scheduler_manager_t *mgr, int sched_id, 
                        const char *name, schedule_period_t *periods, int period_count,
                        void (*enter)(void*), void (*exit)(void*), void *arg) 
{
    scheduler_t *sched = find_scheduler(mgr, sched_id);
    if (!sched || !periods || period_count <= 0) return -1;

    LOCK(mgr);
    schedule_job_t *job = buffer_calloc(1, sizeof(*job));
    memset(job, 0, sizeof(*job));
    strncpy(job->name, name, sizeof(job->name)-1);
    
    job->periods = buffer_calloc(period_count, sizeof(schedule_period_t));
    memcpy(job->periods, periods, sizeof(schedule_period_t)*period_count);
    job->period_count = period_count;
    
    // Calculate initial trigger time
    uint64_t now = mgr->get_time() + (mgr->timezone * 3600);
    uint64_t min_trigger = UINT64_MAX;
    for (int i = 0; i < period_count; i++) {
        uint64_t next = calculate_schedule_trigger(&periods[i], now);
        if (next < min_trigger) min_trigger = next;
    }
    job->next_trigger = min_trigger;

    // Insert into linked list
    job->next = mgr->schedule_jobs;
    mgr->schedule_jobs = job;
    update_scheduler_wakeup(sched, mgr);
    UNLOCK(mgr);
    return 0;
}

int unregister_task_by_name(scheduler_manager_t *mgr, const char *name) 
{
    if (!mgr || !name) return -1;
    
    LOCK(mgr);
    int found = 0;
    
    /* Unregister wakeup task */
    wakeup_job_t **wprev = &mgr->wake_jobs;
    wakeup_job_t *wjob = mgr->wake_jobs;
    while (wjob) {
        if (strcmp(wjob->name, name) == 0) {
            *wprev = wjob->next;
            buffer_free(wjob);
            wjob = *wprev;
            found++;
            continue;
        }
        wprev = &wjob->next;
        wjob = wjob->next;
    }
    
    /* Unregister schedule task */
    schedule_job_t **sprev = &mgr->schedule_jobs;
    schedule_job_t *sjob = mgr->schedule_jobs;
    while (sjob) {
        if (strcmp(sjob->name, name) == 0) {
            *sprev = sjob->next;
            if (sjob->periods) buffer_free(sjob->periods);
            buffer_free(sjob);
            sjob = *sprev;
            found++;
            continue;
        }
        sprev = &sjob->next;
        sjob = sjob->next;
    }
    
    /* Update wakeup time for all affected schedulers */
    for (int i = 0; i < mgr->num_sched; i++) {
        update_scheduler_wakeup(&mgr->schedulers[i], mgr);
    }
    
    UNLOCK(mgr);
    return found ? 0 : -1; // Return 0 for success, -1 for not found
}

int query_tasks(scheduler_manager_t *mgr, query_filter_t filter, 
               query_result_t **results, int *count) 
{
    LOCK(mgr);
    int capacity = 16;
    *results = buffer_calloc((size_t)capacity, sizeof(query_result_t));
    *count = 0;

    // Traverse wakeup tasks
    for (wakeup_job_t *wj = mgr->wake_jobs; wj; wj = wj->next) {
        bool match = false;
        switch (filter.type) {
            case QUERY_BY_SCHEDULER:
                match = (wj->sched->id == filter.sched_id);
                break;
            case QUERY_BY_NAME:
                match = (strcmp(wj->name, filter.name) == 0);
                break;
            case QUERY_BY_TYPE:
                match = (wj->type == WAKEUP_TYPE_ABSOLUTE && filter.wakeup_absolute) ||
                        (wj->type == WAKEUP_TYPE_INTERVAL && filter.wakeup_interval);
                break;
        }
        if (match) {
            if (*count >= capacity) {
                capacity *= 2;
                query_result_t *old_results = *results;
                *results = buffer_calloc(capacity, sizeof(query_result_t));
                if(old_results) {
                    memcpy(*results, old_results, (*count) * sizeof(query_result_t));
                    buffer_free(old_results);
                }
                buffer_free(old_results);
            }
            query_result_t *r = &(*results)[(*count)++];
            strncpy(r->name, wj->name, sizeof(r->name));
            r->type = WAKEUP_JOB;
            r->wake_type = wj->type;
            r->repeat = wj->repeat;
            if (wj->type == WAKEUP_TYPE_ABSOLUTE) {
                r->absolute_time = wj->trigger_sec;
            } else {
                r->interval = wj->interval;
            }
        }
    }

    // Traverse schedule tasks
    for (schedule_job_t *sj = mgr->schedule_jobs; sj; sj = sj->next) {
        bool match = false;
        switch (filter.type) {
            case QUERY_BY_SCHEDULER:
                match = (sj->sched->id == filter.sched_id);
                break;
            case QUERY_BY_NAME:
                match = (strcmp(sj->name, filter.name) == 0);
                break;
            case QUERY_BY_TYPE:
                match = filter.schedule;
                break;
        }
        if (match) {
            if (*count >= capacity) {
                capacity *= 2;
                //use buffer_calloc to reallocate, copy the old data to the new memory
                query_result_t *old_results = *results;
                *results = buffer_calloc(capacity, sizeof(query_result_t));
                memcpy(*results, old_results, (*count) * sizeof(query_result_t));
                buffer_free(old_results);
            }
            query_result_t *r = &(*results)[(*count)++];
            strncpy(r->name, sj->name, sizeof(r->name));
            r->type = SCHEDULE_JOB;
            r->period_count = sj->period_count;
            r->periods = buffer_calloc(sj->period_count, sizeof(schedule_period_t));
            memcpy(r->periods, sj->periods, sizeof(schedule_period_t)*sj->period_count);
        }
    }
    UNLOCK(mgr);
    return 0;
}

void free_query_results(query_result_t *results, int count) 
{
    if (!results) return;
    for (int i = 0; i < count; i++) {
        if (results[i].type == SCHEDULE_JOB && results[i].periods) {
            buffer_free(results[i].periods);
        }
    }
    buffer_free(results);
}
void scheduler_handle_event(scheduler_t *sched, scheduler_manager_t *mgr) 
{
    LOCK(mgr);
    process_wakeup_jobs(sched, mgr);
    process_schedule_jobs(sched, mgr);
    update_scheduler_wakeup(sched, mgr);
    UNLOCK(mgr);
}

// Initialize scheduler manager
void scheduler_init(scheduler_manager_t *mgr, get_time_func_t get_time, get_time_func_t get_wakeup_time,
                   scheduler_t *scheds, int num_sched,
                   sched_lock_func_t lock, sched_unlock_func_t unlock) 
{
    mgr->get_time = get_time;
    mgr->get_wakeup_time = get_wakeup_time;
    mgr->schedulers = scheds;
    mgr->num_sched = num_sched;
    mgr->wake_jobs = NULL;
    mgr->schedule_jobs = NULL;
    
    /* Set thread safety callbacks */
    if (lock && unlock) {
        mgr->lock = lock;
        mgr->unlock = unlock;
        mgr->thread_safe = true;
    } else {
        mgr->thread_safe = false;
    }
}


#if 0

/* Usage example */
void print_results(query_result_t *results, int count) 
{
    for (int i = 0; i < count; i++) {
        printf("Task: %s\n", results[i].name);
        if (results[i].type == WAKEUP_JOB) {
            printf("Type: %s\n", results[i].wake_type == WAKEUP_TYPE_ABSOLUTE ? 
                   "Absolute" : "Interval");
            // Print detailed information...
        } else {
            printf("Schedule with %d periods\n", results[i].period_count);
            // Print time period information...
        }
    }
}

// Register daily wakeup task at 8:30
register_wakeup_ex(&mgr, 1, "Morning Wake", WAKEUP_TYPE_ABSOLUTE,
                    8.5*3600, 0, REPEAT_DAILY, 0, morning_cb, NULL);

// Register weekly wakeup task at 9:00 on Monday
register_wakeup_ex(&mgr, 1, "Weekly Meeting", WAKEUP_TYPE_ABSOLUTE,
                    9*3600, 0, REPEAT_WEEKLY, 0x01, meeting_cb, NULL);

// Monthly task at 9:00 on the 1st (implemented via day_offset)
register_wakeup_ex(&mgr, 1, "Monthly Report", WAKEUP_TYPE_ABSOLUTE,
                  9*3600, 30, REPEAT_ONCE, 0, report_cb, NULL);

// Register cross-day task (2:00 next day)
register_wakeup_ex(&mgr, 1, "Night Backup", WAKEUP_TYPE_ABSOLUTE,
                    2*3600, 1, REPEAT_DAILY, 0, backup_cb, NULL);

// Register interval task (every 1 minute)
register_wakeup_ex(&mgr, 1, "Periodic Check", WAKEUP_TYPE_INTERVAL,
                    1*60, 0, REPEAT_DAILY, 0, check_cb, NULL);

schedule_period_t periods[] = {
    {Daily 9:00-10:00},
    {Weekdays 18:00-20:00}
};
register_schedule_ex(..., "Office Hours", periods, 2, ...);

int main() {
    // Initialize scheduler
    scheduler_t scheds[] = {
        {.id = 1, .set_wakeup = set_hw_wakeup, .callback = NULL}
    };
    scheduler_manager_t mgr;
    scheduler_init(&mgr, get_timestamp, scheds, 1);

    // Register wakeup task (triggered every 10 seconds)
    register_wakeup(&mgr, 1, WAKEUP_TYPE_INTERVAL, 10, my_wake_cb, NULL);

    schedule_period_t periods[] = {
        {.start = 9*3600, .end = 11*3600, .repeat = REPEAT_DAILY},
        {.start = 10*3600, .end = 12*3600, .repeat = REPEAT_WEEKLY, .weekdays = 0x3F} // Weekdays
    };
    register_schedule_ex(&mgr, 1, "Office Hours", periods, 2, 0, enter_cb, exit_cb, NULL);

    // Query example
    query_filter_t filter = {.type = QUERY_BY_NAME, .name = "Office Hours"};
    query_result_t *results;
    int count;
    query_tasks(&mgr, filter, &results, &count);
    print_results(results, count);

    return 0;
}


#endif
