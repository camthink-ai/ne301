#ifndef _DRTC_H_
#define _DRTC_H_

#include "cmsis_os2.h"
#include "dev_manager.h"
#include "scheduler_manager.h"
#include "aicam_error.h"

#define START_YEARS	1960
#define TIMEZONE 8
#define TIMEZONE_NVS_KEY "timezone"

typedef struct {
    uint8_t year;
    uint8_t month;
    uint8_t date;
    uint8_t dayOfWeek;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t subSecond;
    uint64_t timeStamp;
} RTC_TIME_S;

typedef struct{
    char name[64];
    wakeup_type_t type;
    repeat_type_t repeat;
    union {
        struct {
            uint32_t trigger_sec;  // Trigger seconds of the day (0-86399)
            int16_t day_offset;    // Day offset (for cross-day)
            uint8_t weekdays;   
        };
        uint64_t interval;         
    };
    void (*callback)(void *arg);
    void *arg;
} rtc_wakeup_t;

typedef struct{
    char name[64];
    schedule_period_t *periods;
    int period_count;
    void (*enter_cb)(void *arg);
    void (*exit_cb)(void *arg);
    void *arg;
} rtc_schedule_t;

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osMutexId_t mtx_mgr;
    osSemaphoreId_t sem_sched1;
    osSemaphoreId_t sem_sched2;
    osThreadId_t rtc_processId;
    scheduler_manager_t sched_manager;
    int timezone;
} rtc_t;

RTC_TIME_S rtc_get_time(void);
uint64_t rtc_get_wakeup_timeStamp(void);
uint64_t rtc_get_timeStamp(void);
uint64_t rtc_get_local_timestamp(void);
uint64_t rtc_get_timestamp_ms(void);
uint64_t rtc_get_uptime_ms(void);
void timeStamp_to_time(uint64_t timestamp, RTC_TIME_S *rtc_time);
void rtc_setup(int year, int month, int day, int hour, int minute, int second, int weekday);
void rtc_setup_by_timestamp(uint64_t timestamp, int timezone_offset_hours);
void rtc_set_timeStamp(uint64_t timestamp);
void rtc_set_timezone(int timezone_offset_hours);
int rtc_get_timezone(void);
int8_t usr_set_rtc_alarm(uint64_t wake_time);
int rtc_register_wakeup_ex(rtc_wakeup_t *rtc_wakeup);
int rtc_register_wakeup_ex_locked(rtc_wakeup_t *rtc_wakeup);
int rtc_register_schedule_ex(rtc_schedule_t *rtc_schedule);
int rtc_unregister_task_by_name(const char *name);
int rtc_get_next_wakeup_time(int sched_id, uint64_t *next_wakeup);
void rtc_trigger_scheduler_check(int sched_id);
void rtc_register(void);
#endif