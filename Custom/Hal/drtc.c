#include <time.h>
#include "drtc.h"
#include "rtc.h"
#include "main.h"
#include "debug.h"
#include "storage.h"
#include "common_utils.h"
#include <sys/time.h>
#include <reent.h> // For reentrant functions, need to include this header

int _gettimeofday_r(struct _reent *reent, struct timeval *tv, void *tz) {
    // Assume we have a function to get time from RTC and convert to Unix timestamp (seconds)
    uint32_t seconds = (uint32_t)rtc_get_timeStamp();

    tv->tv_sec = seconds;
    tv->tv_usec = 0; // If no microsecond precision, set to 0
    
    return 0; // Return 0 on success
}

/* #define RTC_CLOCK_SOURCE_HSE */
/* #define RTC_CLOCK_SOURCE_LSE */
#define RTC_CLOCK_SOURCE_LSI
/*#define RTC_CLOCK_SOURCE_LSE*/

#ifdef RTC_CLOCK_SOURCE_LSI
#define RTC_ASYNCH_PREDIV    0x7F
#define RTC_SYNCH_PREDIV     0xF9
#endif

#ifdef RTC_CLOCK_SOURCE_LSE
#define RTC_ASYNCH_PREDIV  0x7F
#define RTC_SYNCH_PREDIV   0x00FF
#endif

#define BCD_TO_DEC(bcd) ((((bcd)/16)*10)+((bcd)%16))
#define DEC_TO_BCD(dec) ((((dec)/10)*16)+((dec)%10))

#define RTC_BKP_FLAG 0x5A5A5A5A

extern RTC_HandleTypeDef hrtc;

static rtc_t g_rtc = {0};
static uint64_t g_rtc_wakeup_timestamp = 0ULL;
static uint8_t rtc_tread_stack[1024 * 8] ALIGN_32 IN_PSRAM;
const osThreadAttr_t rtcTask_attributes = {
    .name = "rtcTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = rtc_tread_stack,
    .stack_size = sizeof(rtc_tread_stack),
};
uint8_t aShowTime[32] = "yyyy-mm-dd hh:mm:ss weekday"; 

uint64_t time_to_timeStamp(unsigned int year, unsigned int mon, unsigned int day,
                           unsigned int hour, unsigned int min, unsigned int sec);
/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void RTC_init(void)
{
    RTC_PrivilegeStateTypeDef privilegeState = {0};
    RTC_SecureStateTypeDef secureState = {0};
    __attribute__((unused)) RTC_TimeTypeDef sTime = {0};
    __attribute__((unused)) RTC_DateTypeDef sDate = {0};
    __attribute__((unused)) RTC_AlarmTypeDef sAlarm = {0};

    /* USER CODE BEGIN RTC_Init 1 */

    /* USER CODE END RTC_Init 1 */

    /** Initialize RTC Only
    */
    MX_RTC_Init();
    privilegeState.rtcPrivilegeFull = RTC_PRIVILEGE_FULL_NO;
    privilegeState.backupRegisterPrivZone = RTC_PRIVILEGE_BKUP_ZONE_NONE;
    privilegeState.backupRegisterStartZone2 = RTC_BKP_DR0;
    privilegeState.backupRegisterStartZone3 = RTC_BKP_DR0;
    if (HAL_RTCEx_PrivilegeModeSet(&hrtc, &privilegeState) != HAL_OK)
    {
        Error_Handler();
    }
    secureState.rtcSecureFull = RTC_SECURE_FULL_YES;
    secureState.backupRegisterStartZone2 = RTC_BKP_DR0;
    secureState.backupRegisterStartZone3 = RTC_BKP_DR0;
    if (HAL_RTCEx_SecureModeSet(&hrtc, &secureState) != HAL_OK)
    {
        Error_Handler();
    }

#if ENABLE_U0_MODULE
    int ret = u0_module_sync_rtc_time();
    if (ret == 0) {
        HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
        HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
        g_rtc_wakeup_timestamp = time_to_timeStamp(sDate.Year + START_YEARS, sDate.Month, sDate.Date, sTime.Hours, sTime.Minutes, sTime.Seconds);
        // printf("timestamp: %lu\n", (uint32_t)g_rtc_wakeup_timestamp);
    }
#else

    /* USER CODE BEGIN Check_RTC_BKUP */
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_BKP_FLAG) {
        // printf("RTC already inited\n");
        return;
    }
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BKP_FLAG);
    /* USER CODE END Check_RTC_BKUP */

    /** Initialize RTC and set the Time and Date
    */
    sTime.Hours = 0x2;
    sTime.Minutes = 0x20;
    sTime.Seconds = 0x0;
    sTime.SubSeconds = 0x0;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
    {
        Error_Handler();
    }
    sDate.WeekDay = RTC_WEEKDAY_WEDNESDAY;
    sDate.Month = RTC_MONTH_JANUARY;
    sDate.Date = 0x18;
    sDate.Year = 0x24;
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
    {
        Error_Handler();
    }
#endif
    // /** Enable the Alarm A
    // */
    // sAlarm.AlarmTime.Hours = 0x23;
    // sAlarm.AlarmTime.Minutes = 0x59;
    // sAlarm.AlarmTime.Seconds = 0x59;
    // sAlarm.AlarmTime.SubSeconds = 0x56;
    // sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
    // sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    // sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
    // sAlarm.AlarmDateWeekDay = RTC_WEEKDAY_THURSDAY;
    // sAlarm.Alarm = RTC_ALARM_A;
    // if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
    // {
    //     Error_Handler();
    // }

    // /** Enable the Alarm B
    // */
    // sAlarm.Alarm = RTC_ALARM_B;
    // if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
    // {
    //     Error_Handler();
    // }
}

/*
 * Days since 1970-01-01 from (year, month, day). Formula from Howard Hinnant's date algorithms
 * (https://howardhinnant.github.io/date_algorithms.html). O(1), no loops.
 * March-based internally; 719468 aligns serial 0 with 1970-01-01.
 */
static int64_t days_from_civil(unsigned int year, unsigned int mon, unsigned int day)
{
    int y = (int)year;
    y -= (mon <= 2U) ? 1 : 0;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned int yoe = (unsigned int)(y - era * 400);
    unsigned int doy = (153U * (mon > 2U ? mon - 3U : mon + 9U) + 2U) / 5U + day - 1U;
    unsigned int doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

uint64_t time_to_timeStamp(unsigned int year, unsigned int mon, unsigned int day,
                           unsigned int hour, unsigned int min, unsigned int sec)
{
    if (mon < 1 || mon > 12 || day < 1 || day > 31 ||
        hour > 23 || min > 59 || sec > 59) {
        return 0;
    }

    int64_t d = days_from_civil(year, mon, day);
    uint64_t days = (d >= 0) ? (uint64_t)d : 0ULL;

    uint64_t local_sec = ((uint64_t)days * 24U + (uint64_t)hour) * 60U + (uint64_t)min;
    local_sec = local_sec * 60U + (uint64_t)sec;

    int64_t tz_off = (int64_t)g_rtc.timezone * 3600LL;
    int64_t utc = (int64_t)local_sec - tz_off;
    if (utc < 0)
        utc = 0;
    return (uint64_t)utc;
}

void timeStamp_to_time(uint64_t timestamp, RTC_TIME_S *rtc_time)
{
    const uint16_t days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    if (rtc_time == NULL) {
        return;
    }

    /* Convert UTC timestamp to local seconds, guard against underflow */
    int64_t local_seconds = (int64_t)timestamp + (int64_t)g_rtc.timezone * 3600;
    if (local_seconds < 0) {
        local_seconds = 0;
    }

    uint64_t seconds = (uint64_t)local_seconds;
    uint32_t days = seconds / 86400U;
    uint32_t rem  = seconds % 86400U;

    rtc_time->hour   = (uint8_t)(rem / 3600U);
    rem              = rem % 3600U;
    rtc_time->minute = (uint8_t)(rem / 60U);
    rtc_time->second = (uint8_t)(rem % 60U);

    uint16_t year = 1970;
    while (1) {
        uint16_t days_in_year = 365;

        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
            days_in_year = 366;
        if (days >= days_in_year) {
            days -= days_in_year;
            year++;
        } else {
            break;
        }
    }
    /* rtc_time->year is stored as offset from 1970 here */
    rtc_time->year = (uint8_t)(year - 1970);

    uint8_t month = 0;
    while (1) {
        uint16_t dim = days_in_month[month];

        if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
            dim = 29;
        if (days >= dim) {
            days -= dim;
            month++;
        } else {
            break;
        }
    }
    rtc_time->month = (uint8_t)(month + 1U);
    rtc_time->date  = (uint8_t)(days + 1U);

    /* Day of week: 1970-01-01 is Thursday (4). Use LOCAL days, not UTC days. */
    rtc_time->dayOfWeek = (uint8_t)((4 + (seconds / 86400U)) % 7U);
    if (rtc_time->dayOfWeek == 0U) {
        rtc_time->dayOfWeek = 7U;
    }

    rtc_time->subSecond = 0;
    rtc_time->timeStamp = timestamp;
}

uint64_t rtc_get_wakeup_timeStamp(void)
{
    return g_rtc_wakeup_timestamp;
}

uint64_t rtc_get_timeStamp(void)
{
    if(!g_rtc.is_init)
        return 0;

    RTC_DateTypeDef sdatestructureget;
    RTC_TimeTypeDef stimestructureget;
    RTC_TIME_S  rtc_time;

    HAL_RTC_GetTime(&hrtc, &stimestructureget, RTC_FORMAT_BCD);
    HAL_RTC_GetDate(&hrtc, &sdatestructureget, RTC_FORMAT_BCD);
    rtc_time.year      = BCD_TO_DEC(sdatestructureget.Year);
    rtc_time.month     = BCD_TO_DEC(sdatestructureget.Month);
    rtc_time.date      = BCD_TO_DEC(sdatestructureget.Date);
    rtc_time.hour      = BCD_TO_DEC(stimestructureget.Hours);
    rtc_time.minute    = BCD_TO_DEC(stimestructureget.Minutes);
    rtc_time.second    = BCD_TO_DEC(stimestructureget.Seconds);
    //rtc_time.subSecond = (255 - rtc_subsecond_get()) * 1000 / 255;
    rtc_time.dayOfWeek = BCD_TO_DEC(sdatestructureget.WeekDay);
    rtc_time.timeStamp = time_to_timeStamp(rtc_time.year + START_YEARS, rtc_time.month, rtc_time.date, rtc_time.hour,
                                           rtc_time.minute, rtc_time.second);
    return rtc_time.timeStamp;
}

uint16_t rtc_get_timeMs(void)
{
    if(!g_rtc.is_init)
        return 0;

    RTC_TimeTypeDef stimestructureget;

    HAL_RTC_GetTime(&hrtc, &stimestructureget, RTC_FORMAT_BCD);

    // SubSeconds is a decrementing counter (255->0), convert to milliseconds (0-999)
    // Use same formula as rtc_get_time() for consistency
    // Formula: (255 - SubSeconds) * 1000 / 255
    // Note: This assumes SecondFraction = 255, which matches rtc_get_time() implementation
    uint32_t ms = (255 - stimestructureget.SubSeconds) * 1000 / 255;
    
    // Ensure result is within valid range (0-999 ms)
    // The formula can produce 1000 when SubSeconds = 0, clamp to 999
    if (ms > 999) {
        ms = 999;
    }
    
    return (uint16_t)ms;
}

RTC_TIME_S rtc_get_time(void)
{
    RTC_DateTypeDef sdatestructureget;
    RTC_TimeTypeDef stimestructureget;
    RTC_TIME_S  rtc_time;

    HAL_RTC_GetTime(&hrtc, &stimestructureget, RTC_FORMAT_BCD);
    HAL_RTC_GetDate(&hrtc, &sdatestructureget, RTC_FORMAT_BCD);
    rtc_time.year      = BCD_TO_DEC(sdatestructureget.Year);
    rtc_time.month     = BCD_TO_DEC(sdatestructureget.Month);
    rtc_time.date      = BCD_TO_DEC(sdatestructureget.Date);
    rtc_time.hour      = BCD_TO_DEC(stimestructureget.Hours);
    rtc_time.minute    = BCD_TO_DEC(stimestructureget.Minutes);
    rtc_time.second    = BCD_TO_DEC(stimestructureget.Seconds);
    rtc_time.subSecond = (255 - stimestructureget.SubSeconds) * 1000 / 255;
    rtc_time.dayOfWeek = BCD_TO_DEC(sdatestructureget.WeekDay);
    rtc_time.timeStamp = time_to_timeStamp(rtc_time.year + START_YEARS, rtc_time.month, rtc_time.date, rtc_time.hour,
                                           rtc_time.minute, rtc_time.second);
    return rtc_time;
}

static void rtc_mgr_lock(void)
{
    osMutexAcquire(g_rtc.mtx_mgr, osWaitForever);
}

static void rtc_mgr_unlock(void)
{
    osMutexRelease(g_rtc.mtx_mgr);
}

static void set_rtc_alarm(int id, uint64_t wake_time)
{
    RTC_AlarmTypeDef sAlarm = {0};
    RTC_TIME_S rtc_time;
    timeStamp_to_time(wake_time, &rtc_time); 
    LOG_DRV_DEBUG("set_rtc_alarm id:%d %02d-%02d-%02d %02d:%02d:%02d\r\n", id,
        rtc_time.year + 1970, rtc_time.month, rtc_time.date, 
        rtc_time.hour, rtc_time.minute, rtc_time.second);

    // 2. Fill alarm structure (note BCD format)
    sAlarm.AlarmTime.Hours   = DEC_TO_BCD(rtc_time.hour);
    sAlarm.AlarmTime.Minutes = DEC_TO_BCD(rtc_time.minute);
    sAlarm.AlarmTime.Seconds = DEC_TO_BCD(rtc_time.second);

    sAlarm.AlarmMask = RTC_ALARMMASK_NONE; // Accurate to second

    sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    sAlarm.AlarmDateWeekDay = DEC_TO_BCD(rtc_time.date);

    if(id == 1){
        sAlarm.Alarm = RTC_ALARM_A;
    }else if(id == 2){
        sAlarm.Alarm = RTC_ALARM_B;
    }

    HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD);

}

int8_t usr_set_rtc_alarm(uint64_t wake_time)
{
    if(!g_rtc.is_init)
        return -1;

    uint64_t current_time = rtc_get_timeStamp();
    rtc_mgr_lock();
    set_rtc_alarm(1, current_time + wake_time);
    rtc_mgr_unlock();

    return 0;
}

scheduler_t scheds[] = {
    {.id = 1, .set_wakeup = set_rtc_alarm, .callback = NULL},
    {.id = 2, .set_wakeup = set_rtc_alarm, .callback = NULL}
};

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    osSemaphoreRelease(g_rtc.sem_sched1);
}

void HAL_RTCEx_AlarmBEventCallback(RTC_HandleTypeDef *hrtc)
{
    osSemaphoreRelease(g_rtc.sem_sched2);
}

static int rtc_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    rtc_t *rtc = (rtc_t *)priv;
    if(!rtc->is_init)
        return -1;
    osMutexAcquire(rtc->mtx_id, osWaitForever);

    osMutexRelease(rtc->mtx_id);
    return 0;
}


int rtc_register_wakeup_ex(rtc_wakeup_t *rtc_wakeup)
{
    return register_wakeup_ex(&g_rtc.sched_manager, 1, rtc_wakeup->name, rtc_wakeup->type, rtc_wakeup->trigger_sec,
                        rtc_wakeup->day_offset, rtc_wakeup->repeat, rtc_wakeup->weekdays, rtc_wakeup->callback, rtc_wakeup->arg);
}

int rtc_register_wakeup_ex_locked(rtc_wakeup_t *rtc_wakeup)
{
    return register_wakeup_ex_locked(&g_rtc.sched_manager, 1, rtc_wakeup->name, rtc_wakeup->type, rtc_wakeup->trigger_sec,
                        rtc_wakeup->day_offset, rtc_wakeup->repeat, rtc_wakeup->weekdays, rtc_wakeup->callback, rtc_wakeup->arg);
}

int rtc_register_schedule_ex(rtc_schedule_t *rtc_schedule)
{
    return register_schedule_ex(&g_rtc.sched_manager, 2, rtc_schedule->name, rtc_schedule->periods, 
                            rtc_schedule->period_count, rtc_schedule->enter_cb, rtc_schedule->exit_cb, rtc_schedule->arg);
}

int rtc_unregister_task_by_name(const char *name)
{
    return unregister_task_by_name(&g_rtc.sched_manager, name);
}

static void RTC_TimeShow(uint8_t *showtime)
{
    RTC_DateTypeDef sdatestructureget;
    RTC_TimeTypeDef stimestructureget;

    /* Get the RTC current Time */
    HAL_RTC_GetTime(&hrtc, &stimestructureget, RTC_FORMAT_BIN);
    /* Get the RTC current Date */
    HAL_RTC_GetDate(&hrtc, &sdatestructureget, RTC_FORMAT_BIN);
    /* Display date and time Format : yyyy-mm-dd hh:mm:ss weekday*/
    sprintf((char *)showtime, "%02d-%02d-%02d %02d:%02d:%02d %d", 
        sdatestructureget.Year + START_YEARS, 
        sdatestructureget.Month, 
        sdatestructureget.Date,
        stimestructureget.Hours, 
        stimestructureget.Minutes, 
        stimestructureget.Seconds,
        sdatestructureget.WeekDay);
    LOG_SIMPLE("%s \r\n", showtime);
}

static void rtcProcess(void *argument)
{
    rtc_t *rtc = (rtc_t *)argument;
    LOG_DRV_DEBUG("rtcProcess start\r\n");
    for(;;){
        if(rtc->is_init){
            if (osSemaphoreAcquire(rtc->sem_sched1, 10) == osOK) {
                scheduler_handle_event(&scheds[0], &g_rtc.sched_manager);
            }

            if (osSemaphoreAcquire(rtc->sem_sched2, 10) == osOK) {
                scheduler_handle_event(&scheds[1], &g_rtc.sched_manager);
            }
        }
        osDelay(100);
    }
}

uint64_t rtc_get_local_timestamp(void)
{
    return (rtc_get_timeStamp() + g_rtc.timezone * 3600); // Convert to local timestamp
}

uint64_t rtc_get_timestamp_ms(void)
{
    if(!g_rtc.is_init)
        return 0;

    // Get second-level timestamp
    uint64_t timestamp_sec = rtc_get_timeStamp();
    
    // Get milliseconds within current second
    uint16_t ms = rtc_get_timeMs();
    
    // Combine: timestamp_sec * 1000 + ms
    return timestamp_sec * 1000 + ms;
}

uint64_t rtc_get_uptime_ms(void)
{
    // System relative time (uptime) based on system tick
    // Not affected by RTC time modification
    // static uint32_t system_start_tick = 0;
    
    // if (system_start_tick == 0) {
    //     system_start_tick = osKernelGetTickCount();
    // }
    
    // uint32_t current_tick = osKernelGetTickCount();
    // uint32_t elapsed_ticks = current_tick - system_start_tick;
    
    // // Convert ticks to milliseconds
    // // osKernelGetTickFreq() returns ticks per second (Hz)
    // uint32_t tick_freq = osKernelGetTickFreq();
    // uint64_t uptime_ms = ((uint64_t)elapsed_ticks * 1000) / tick_freq;
    
    return HAL_GetTick();
}

static int rtc_init(void *priv)
{
    int ret = 0;
    char tmp[16] = {0};
    LOG_DRV_DEBUG("rtc_init \r\n");
    rtc_t *rtc = (rtc_t *)priv;
    rtc->mtx_id = osMutexNew(NULL);
    rtc->mtx_mgr = osMutexNew(NULL);
    rtc->sem_sched1 = osSemaphoreNew(1, 0, NULL);
    rtc->sem_sched2 = osSemaphoreNew(1, 0, NULL);
    scheduler_init(&rtc->sched_manager, rtc_get_timeStamp, rtc_get_wakeup_timeStamp, scheds, sizeof(scheds) / sizeof(scheds[0]), rtc_mgr_lock, rtc_mgr_unlock);
    // rtc_setup(0x65, 0x6, 0x19, 0x8, 0x0, 0x0, 0x4);
    ret = storage_nvs_read(NVS_USER, TIMEZONE_NVS_KEY, tmp, sizeof(tmp));
    if (ret > 0) {
        rtc->timezone = atoi(tmp);
        rtc->sched_manager.timezone = rtc->timezone;
    } else {
        rtc->timezone = TIMEZONE;
        rtc->sched_manager.timezone = rtc->timezone;
    }
    RTC_init();
    rtc->rtc_processId = osThreadNew(rtcProcess, rtc, &rtcTask_attributes);
    // printf("timezone: %d\r\n", rtc->timezone);
    rtc->is_init = true;
    LOG_DRV_DEBUG("rtc_init end\r\n");
    return 0;
}


static int date_cmd(int argc, char* argv[]) 
{
    RTC_TimeShow(aShowTime);
    return 0;
}

static int setdate_cmd(int argc, char* argv[]) 
{
    if(argc != 8){
        LOG_SIMPLE("Usage: setdate yyyy mm dd hh mm ss weekday\r\n");
        return -1;
    }
    int year = atoi(argv[1]);
    int month = atoi(argv[2]);
    int day = atoi(argv[3]);
    int hour = atoi(argv[4]);
    int minute = atoi(argv[5]);
    int second = atoi(argv[6]);
    int weekday = atoi(argv[7]);

    // Convert to BCD format
    year = DEC_TO_BCD(year - START_YEARS);
    month = DEC_TO_BCD(month);
    day = DEC_TO_BCD(day);
    hour = DEC_TO_BCD(hour);
    minute = DEC_TO_BCD(minute);
    second = DEC_TO_BCD(second);
    weekday = DEC_TO_BCD(weekday);

    rtc_setup(year, month, day, hour, minute, second, weekday);

    return 0;
}

static int settimestamp_cmd(int argc, char* argv[]) 
{
    if(argc != 2){
        LOG_SIMPLE("Usage: settimestamp timestamp\r\n");
        return -1;
    }
    rtc_set_timeStamp(atoi(argv[1]));
    return 0;
}

static int settimezone_cmd(int argc, char* argv[]) 
{
    if(argc != 2){
        LOG_SIMPLE("Usage: settimezone timezone\r\n");
        return -1;
    }
    rtc_set_timezone(atoi(argv[1]));
    return 0;
}

debug_cmd_reg_t rtc_cmd_table[] = {
    {"date",      "The current time",      date_cmd},
    {"setdate",   "Set RTC time",          setdate_cmd},
    {"settimestamp", "Set timestamp",      settimestamp_cmd},
    {"settimezone", "Set timezone",      settimezone_cmd},
};


static void rtc_cmd_register(void)
{
    debug_cmdline_register(rtc_cmd_table, sizeof(rtc_cmd_table) / sizeof(rtc_cmd_table[0]));
}

void rtc_setup(int year, int month, int day, int hour, int minute, int second, int weekday)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    /** Initialize RTC and set the Time and Date
    */
    sTime.Hours = hour;
    sTime.Minutes = minute;
    sTime.Seconds = second;
    sTime.SubSeconds = 0x0;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
    {
        Error_Handler();
    }
    sDate.WeekDay = weekday;
    sDate.Month = month;
    sDate.Date = day;
    sDate.Year = year;
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
    {
        Error_Handler();
    }
#if ENABLE_U0_MODULE
    u0_module_update_rtc_time();
#endif
}

void rtc_setup_by_timestamp(uint64_t timestamp, int timezone_offset_hours) 
{
    char tmp[16] = {0};
    RTC_TIME_S rtc_time;

    if (timezone_offset_hours != g_rtc.timezone) {
        g_rtc.timezone = timezone_offset_hours;
        g_rtc.sched_manager.timezone = g_rtc.timezone;
        snprintf(tmp, sizeof(tmp), "%d", g_rtc.timezone);
        storage_nvs_write(NVS_USER, TIMEZONE_NVS_KEY, tmp, strlen(tmp) + 1);
    }
    timeStamp_to_time(timestamp, &rtc_time); 

    int year, month, day, hour,minute, second, weekday;

    year = DEC_TO_BCD(rtc_time.year + 1970 - START_YEARS);
    month = DEC_TO_BCD(rtc_time.month);
    day = DEC_TO_BCD(rtc_time.date);
    hour = DEC_TO_BCD(rtc_time.hour);
    minute = DEC_TO_BCD(rtc_time.minute);
    second = DEC_TO_BCD(rtc_time.second);
    weekday = DEC_TO_BCD(rtc_time.dayOfWeek);

    rtc_setup(year, month, day, hour, minute, second, weekday);
}

void rtc_set_timeStamp(uint64_t timestamp) 
{
    rtc_setup_by_timestamp(timestamp, g_rtc.timezone);
}

void rtc_set_timezone(int timezone_offset_hours) 
{
    uint64_t timestamp = rtc_get_timeStamp();
    
    rtc_setup_by_timestamp(timestamp, timezone_offset_hours);
}

int rtc_get_timezone(void) 
{
    return g_rtc.timezone;
}

int rtc_get_next_wakeup_time(int sched_id, uint64_t *next_wakeup)
{
    if (!g_rtc.is_init || !next_wakeup) {
        return -1;
    }
    
    if (sched_id < 0 || sched_id >= (int)(sizeof(scheds) / sizeof(scheds[0]))) {
        return -1;
    }
    
    rtc_mgr_lock();
    
    // Snapshot current time before taking the lock (rtc_get_timeStamp only
    // reads hardware RTC, no manager lock -> no recursion).
    uint64_t now = rtc_get_timeStamp();

    // Find minimum next_trigger for the specified scheduler. Only accept
    // triggers strictly in the future: a trigger at/before now (e.g. a job
    // whose next_trigger hasn't been advanced past its just-fired time, or one
    // stranded in the past by an NTP time correction) would arm an RTC alarm
    // that fires immediately on standby entry -> U0 wakes/resets and the host
    // never loses power ("never sleeps"). See enter_sleep_mode() alarm path.
    uint64_t min_trigger = UINT64_MAX;
    bool found = false;
    
    // Check wakeup jobs
    wakeup_job_t *job = g_rtc.sched_manager.wake_jobs;
    while (job) {
        if (job->sched && job->sched->id == sched_id) {
            if (job->next_trigger > (now + 3) && job->next_trigger < min_trigger) {
                min_trigger = job->next_trigger;
                found = true;
            }
        }
        job = job->next;
    }
    
    // Check schedule jobs
    schedule_job_t *sched_job = g_rtc.sched_manager.schedule_jobs;
    while (sched_job) {
        if (sched_job->sched && sched_job->sched->id == sched_id) {
            if (sched_job->next_trigger > (now + 3) && sched_job->next_trigger < min_trigger) {
                min_trigger = sched_job->next_trigger;
                found = true;
            }
        }
        sched_job = sched_job->next;
    }
    
    rtc_mgr_unlock();
    
    if (found) {
        *next_wakeup = min_trigger;
        return 0;
    }
    
    return -1;
}

void rtc_trigger_scheduler_check(int sched_id)
{
    if (!g_rtc.is_init) {
        return;
    }
    
    if (sched_id < 0 || sched_id >= (int)(sizeof(scheds) / sizeof(scheds[0]))) {
        return;
    }
    
    // Trigger scheduler by releasing semaphore
    if (sched_id == 1) {
        osSemaphoreRelease(g_rtc.sem_sched1);
        LOG_DRV_DEBUG("RTC scheduler 1 (Alarm A) check triggered\n");
    } else if (sched_id == 2) {
        osSemaphoreRelease(g_rtc.sem_sched2);
        LOG_DRV_DEBUG("RTC scheduler 2 (Alarm B) check triggered\n");
    }
}

void rtc_register(void)
{
    static dev_ops_t rtc_ops = {
        .init = rtc_init, 
        .ioctl = rtc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_rtc.dev = dev;
    strcpy(dev->name, DRTC_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &rtc_ops;
    dev->priv_data = &g_rtc;

    device_register(g_rtc.dev);

    driver_cmd_register_callback(DRTC_DEVICE_NAME, rtc_cmd_register);
}


