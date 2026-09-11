/*
 * Copyright 2021-2025 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mmhal_app.h"
#include "mm_hal_common.h"
#include "mmosal.h"
#include "mmutils.h"

#include "main.h"

#define BCD_TO_DEC(bcd) ((((bcd)/16)*10)+((bcd)%16))
#define DEC_TO_BCD(dec) ((((dec)/10)*16)+((dec)%10))

extern RTC_HandleTypeDef hrtc;

static mmhal_button_state_cb_t mmhal_button_state_cb = NULL;

void mmhal_set_led(uint8_t led, uint8_t level)
{

}

void mmhal_set_error_led(bool state)
{

}

enum mmhal_button_state mmhal_get_button(enum mmhal_button_id button_id)
{
    /* Not implemented on this platform */
    MM_UNUSED(button_id);
    return BUTTON_RELEASED;
}

bool mmhal_set_button_callback(enum mmhal_button_id button_id,
                               mmhal_button_state_cb_t button_state_cb)
{
    if (button_id != BUTTON_ID_USER0)
    {
        return false;
    }

    mmhal_button_state_cb = button_state_cb;
    return true;
}

mmhal_button_state_cb_t mmhal_get_button_callback(enum mmhal_button_id button_id)
{
    if (button_id != BUTTON_ID_USER0)
    {
        return NULL;
    }

    return mmhal_button_state_cb;
}

time_t mmhal_get_time()
{
    struct tm t;
	RTC_DateTypeDef sdatestructureget;
    RTC_TimeTypeDef stimestructureget;

	HAL_RTC_GetTime(&hrtc, &stimestructureget, RTC_FORMAT_BCD);
    HAL_RTC_GetDate(&hrtc, &sdatestructureget, RTC_FORMAT_BCD);
	
    t.tm_year      = BCD_TO_DEC(sdatestructureget.Year);
    t.tm_mon     = BCD_TO_DEC(sdatestructureget.Month);
    t.tm_mday      = BCD_TO_DEC(sdatestructureget.Date);
    t.tm_hour      = BCD_TO_DEC(stimestructureget.Hours);
    t.tm_min    = BCD_TO_DEC(stimestructureget.Minutes);
    t.tm_sec    = BCD_TO_DEC(stimestructureget.Seconds);

    /* ignored */
    t.tm_yday = 0;
    t.tm_isdst = 0;
    t.tm_yday = 0;

    /* Now convert to epoch and return */
    return mktime(&t);
}

void mmhal_set_time(time_t epoch)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
	struct tm *t;
    struct tm result;

	t = gmtime_r(&epoch, &result);

    sTime.Hours = t->tm_hour;
    sTime.Minutes = t->tm_min;
    sTime.Seconds = t->tm_sec;
    sTime.SubSeconds = 0x0;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
    {
        printf("HAL_RTC_SetTime failed!\r\n");
    }
	
    sDate.WeekDay = t->tm_wday;
    sDate.Month = t->tm_mon;
    sDate.Date = t->tm_mday;
    sDate.Year = t->tm_year - 100;
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
    {
        printf("HAL_RTC_SetDate failed!\r\n");
    }

}

void mmhal_set_debug_pins(uint32_t mask, uint32_t values)
{

}

bool mmhal_get_hardware_version(char *version_buffer, size_t version_buffer_length)
{
    /* Note: You need to identify the correct hardware and or version
     *       here using whatever means available (GPIO's, version number stored in EEPROM, etc)
     *       and return the correct string here. */
    return !mmosal_safer_strcpy(version_buffer, MMHAL_HARDWARE_VERSION, version_buffer_length);
}
