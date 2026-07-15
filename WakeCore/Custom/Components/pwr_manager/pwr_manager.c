#include "gpio.h"
#include "iwdg.h"
#include "rtc.h"
#include "pir.h"
#include "usart.h"
#include "pwr_manager.h"

static char pwr_state_buf[128] = {0};
static uint8_t rtc_wake_up_flag = 0, rtc_alarm_a_flag = 0, rtc_alarm_b_flag = 0;
static uint16_t stop2_wakeup_falling_pins = 0;
static uint16_t stop2_wakeup_rising_pins = 0;
static uint32_t global_wakeup_flags = 0;

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    stop2_wakeup_falling_pins |= GPIO_Pin;
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    stop2_wakeup_rising_pins |= GPIO_Pin;
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc)
{
	HAL_NVIC_DisableIRQ(RTC_TAMP_IRQn);
    HAL_RTCEx_DeactivateWakeUpTimer(hrtc);
    rtc_wake_up_flag = 1;
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
	HAL_NVIC_DisableIRQ(RTC_TAMP_IRQn);
    HAL_RTC_DeactivateAlarm(hrtc, RTC_ALARM_A);
    rtc_alarm_a_flag = 1;
}

void HAL_RTCEx_AlarmBEventCallback(RTC_HandleTypeDef *hrtc)
{
	HAL_NVIC_DisableIRQ(RTC_TAMP_IRQn);
    HAL_RTC_DeactivateAlarm(hrtc, RTC_ALARM_B);
    rtc_alarm_b_flag = 1;
}


/**
  * @brief  Configures system clock after wake-up from STOP: enable MSI, PLL
  *         and select PLL as system clock source.
  * @param  None
  * @retval None
  */
static void SYSCLKConfig_STOP(void)
{
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    uint32_t pFLatency = 0;

    /* Enable Power Control clock */
    __HAL_RCC_PWR_CLK_ENABLE();

    /* Get the Oscillators configuration according to the internal RCC registers */
    HAL_RCC_GetOscConfig(&RCC_OscInitStruct);

    /* After wake-up from STOP reconfigure the system clock: Enable HSI and PLL */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Get the Clocks configuration according to the internal RCC registers */
    HAL_RCC_GetClockConfig(&RCC_ClkInitStruct, &pFLatency);

    /* Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2
        clocks dividers */
    RCC_ClkInitStruct.ClockType     = RCC_CLOCKTYPE_SYSCLK;
    RCC_ClkInitStruct.SYSCLKSource  = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, pFLatency) != HAL_OK) {
        Error_Handler();
    }
}

void pwr_ctrl(const char *module, const char *state)
{
    GPIO_PinState PinState = GPIO_PIN_RESET;
    if (module == NULL || state == NULL) return;
    if (strncmp(state, PWR_OFF_NAME, sizeof(PWR_OFF_NAME)) == 0) PinState = GPIO_PIN_RESET;
    else if (strncmp(state, PWR_ON_NAME, sizeof(PWR_ON_NAME)) == 0) PinState = GPIO_PIN_SET;
    else return;

    if (strncmp(module, PWR_ALL_NAME, sizeof(PWR_ALL_NAME)) == 0) {
        HAL_GPIO_WritePin(PWR_3V3_GPIO_Port, PWR_3V3_Pin, PinState);
        // HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, PinState);
        HAL_GPIO_WritePin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin, PinState);
        HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, PinState);
        HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, PinState);
    } else if (strncmp(module, PWR_WIFI_NAME, sizeof(PWR_WIFI_NAME)) == 0) {
        HAL_GPIO_WritePin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin, PinState);
    } else if (strncmp(module, PWR_3V3_NAME, sizeof(PWR_3V3_NAME)) == 0) {
        HAL_GPIO_WritePin(PWR_3V3_GPIO_Port, PWR_3V3_Pin, PinState);
    } else if (strncmp(module, PWR_AON_NAME, sizeof(PWR_AON_NAME)) == 0) {
        HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, PinState);
    } else if (strncmp(module, PWR_N6_NAME, sizeof(PWR_N6_NAME)) == 0) {
        HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, PinState);
    } else if (strncmp(module, PWR_EXT_NAME, sizeof(PWR_EXT_NAME)) == 0) {
        // HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, PinState);
    }
}

const char *pwr_get_state(const char *module)
{
    if (module == NULL) return NULL;
    memset(pwr_state_buf, 0, sizeof(pwr_state_buf));

    if (strncmp(module, PWR_ALL_NAME, sizeof(PWR_ALL_NAME)) == 0) {
        snprintf(pwr_state_buf, sizeof(pwr_state_buf),
                 "%s: %s\r\n%s: %s\r\n%s: %s\r\n%s: %s\r\n%s: %s\r\n",
                 PWR_3V3_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_3V3_GPIO_Port, PWR_3V3_Pin)),
                 PWR_EXT_NAME, PWR_STATE_STR(0),
                 PWR_WIFI_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin)),
                 PWR_AON_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_AON_GPIO_Port, PWR_AON_Pin)),
                 PWR_N6_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_N6_GPIO_Port, PWR_N6_Pin))
        );
    } else if (strncmp(module, PWR_WIFI_NAME, sizeof(PWR_WIFI_NAME)) == 0) {
        snprintf(pwr_state_buf, sizeof(pwr_state_buf), "%s: %s\r\n",
                 PWR_WIFI_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin))
        );
    } else if (strncmp(module, PWR_3V3_NAME, sizeof(PWR_3V3_NAME)) == 0) {
        snprintf(pwr_state_buf, sizeof(pwr_state_buf), "%s: %s\r\n",
                 PWR_3V3_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_3V3_GPIO_Port, PWR_3V3_Pin))
        );
    } else if (strncmp(module, PWR_AON_NAME, sizeof(PWR_AON_NAME)) == 0) {
        snprintf(pwr_state_buf, sizeof(pwr_state_buf), "%s: %s\r\n",
                 PWR_AON_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_AON_GPIO_Port, PWR_AON_Pin))
        );
    } else if (strncmp(module, PWR_N6_NAME, sizeof(PWR_N6_NAME)) == 0) {
        snprintf(pwr_state_buf, sizeof(pwr_state_buf), "%s: %s\r\n",
                 PWR_N6_NAME, PWR_STATE_STR(HAL_GPIO_ReadPin(PWR_N6_GPIO_Port, PWR_N6_Pin))
        );
    } else if (strncmp(module, PWR_EXT_NAME, sizeof(PWR_EXT_NAME)) == 0) {
        snprintf(pwr_state_buf, sizeof(pwr_state_buf), "%s: %s\r\n",
                 PWR_EXT_NAME, PWR_STATE_STR(0)
        );
    } else {
        return NULL;
    }
    return pwr_state_buf;
}

uint32_t pwr_get_switch_bit(const char *module)
{
    if (module == NULL) return 0;

    if (strncmp(module, PWR_ALL_NAME, sizeof(PWR_ALL_NAME)) == 0) {
        return PWR_ALL_SWITCH_BIT;
    } else if (strncmp(module, PWR_WIFI_NAME, sizeof(PWR_WIFI_NAME)) == 0) {
        return PWR_WIFI_SWITCH_BIT;
    } else if (strncmp(module, PWR_3V3_NAME, sizeof(PWR_3V3_NAME)) == 0) {
        return PWR_3V3_SWITCH_BIT;
    } else if (strncmp(module, PWR_AON_NAME, sizeof(PWR_AON_NAME)) == 0) {
        return PWR_AON_SWITCH_BIT;
    } else if (strncmp(module, PWR_N6_NAME, sizeof(PWR_N6_NAME)) == 0) {
        return PWR_N6_SWITCH_BIT;
    } else if (strncmp(module, PWR_EXT_NAME, sizeof(PWR_EXT_NAME)) == 0) {
        return PWR_EXT_SWITCH_BIT;
    }
    return 0;
}

void pwr_ctrl_bits(uint32_t switch_bits)
{
    if (switch_bits & PWR_3V3_SWITCH_BIT) {
        HAL_GPIO_WritePin(PWR_3V3_GPIO_Port, PWR_3V3_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(PWR_3V3_GPIO_Port, PWR_3V3_Pin, GPIO_PIN_RESET);
    }

    // if (switch_bits & PWR_EXT_SWITCH_BIT) {
    //     HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, GPIO_PIN_SET);
    // } else {
    //     HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, GPIO_PIN_RESET);
    // }

    if (switch_bits & PWR_WIFI_SWITCH_BIT) {
        HAL_GPIO_WritePin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin, GPIO_PIN_RESET);
    }
    
    if (switch_bits & PWR_AON_SWITCH_BIT) {
        HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, GPIO_PIN_RESET);
    }

    if (switch_bits & PWR_N6_SWITCH_BIT) {
        HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, GPIO_PIN_RESET);
    }
}

uint32_t pwr_get_switch_bits(void)
{
    uint32_t switch_bits = 0;

    if (HAL_GPIO_ReadPin(PWR_3V3_GPIO_Port, PWR_3V3_Pin) == GPIO_PIN_SET) switch_bits |= PWR_3V3_SWITCH_BIT;
    // if (HAL_GPIO_ReadPin(PWR_EXT_GPIO_Port, PWR_EXT_Pin) == GPIO_PIN_SET) switch_bits |= PWR_EXT_SWITCH_BIT;
    if (HAL_GPIO_ReadPin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin) == GPIO_PIN_SET) switch_bits |= PWR_WIFI_SWITCH_BIT;
    if (HAL_GPIO_ReadPin(PWR_AON_GPIO_Port, PWR_AON_Pin) == GPIO_PIN_SET) switch_bits |= PWR_AON_SWITCH_BIT;
    if (HAL_GPIO_ReadPin(PWR_N6_GPIO_Port, PWR_N6_Pin) == GPIO_PIN_SET) switch_bits |= PWR_N6_SWITCH_BIT;
    
    return switch_bits;
}

uint32_t pwr_get_wakeup_flags(void)
{
    uint32_t bkp_wakeup_flags = 0;

    if (global_wakeup_flags & PWR_WAKEUP_FLAG_VALID) return global_wakeup_flags;
    else {
        __HAL_RCC_PWR_CLK_ENABLE();
        /* Enable Backup domain access */
        HAL_PWR_EnableBkUpAccess();
        bkp_wakeup_flags = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET) {
            global_wakeup_flags |= PWR_WAKEUP_FLAG_STANDBY;
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
        }
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_STOP2) != RESET) {
            global_wakeup_flags |= PWR_WAKEUP_FLAG_STOP2;
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_STOP2);
        }
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_WUF1) != RESET) {
            global_wakeup_flags |= PWR_WAKEUP_FLAG_CONFIG_KEY;
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF1);
        }
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_WUF3) != RESET) {
            // if (global_wakeup_flags & PWR_WAKEUP_FLAG_STANDBY) {
            //     if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_PIR_HIGH) global_wakeup_flags |= PWR_WAKEUP_FLAG_PIR_HIGH;
            //     else if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_PIR_LOW) global_wakeup_flags |= PWR_WAKEUP_FLAG_PIR_LOW;
            // }
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF3);
        }
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_WUF4) != RESET) {
            if (global_wakeup_flags & PWR_WAKEUP_FLAG_STANDBY) {
                if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_PIR_HIGH) global_wakeup_flags |= PWR_WAKEUP_FLAG_PIR_HIGH;
                else if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_PIR_LOW) global_wakeup_flags |= PWR_WAKEUP_FLAG_PIR_LOW;
            }
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF4);
        }
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_WUFI) != RESET) {
            if (global_wakeup_flags & PWR_WAKEUP_FLAG_STANDBY) {
                if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) {
                    if (__HAL_RTC_GET_FLAG(&hrtc, RTC_FLAG_WUTF) != RESET) {
                        global_wakeup_flags |= PWR_WAKEUP_FLAG_RTC_TIMING;
                    }
                    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
                }
                
                if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A) {
                    if (__HAL_RTC_ALARM_GET_FLAG(&hrtc, RTC_FLAG_ALRAF) != RESET) {
                        global_wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_A;
                    }
                    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
                }

                if (bkp_wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B) {
                    if (__HAL_RTC_ALARM_GET_FLAG(&hrtc, RTC_FLAG_ALRBF) != RESET) {
                        global_wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_B;
                    }
                    HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_B);
                }
            }
            global_wakeup_flags |= PWR_WAKEUP_FLAG_WUFI;
            __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUFI);
        }
        if (global_wakeup_flags & PWR_WAKEUP_FLAG_STOP2) {
            if (stop2_wakeup_falling_pins & CONFIG_KEY_Pin) global_wakeup_flags |= PWR_WAKEUP_FLAG_CONFIG_KEY;
            if (stop2_wakeup_falling_pins & PIR_TRIGGER_Pin) global_wakeup_flags |= PWR_WAKEUP_FLAG_PIR_FALLING;
            if (stop2_wakeup_rising_pins & PIR_TRIGGER_Pin) global_wakeup_flags |= PWR_WAKEUP_FLAG_PIR_RISING;
            // if (stop2_wakeup_rising_pins & NET_WKUP_Pin) global_wakeup_flags |= PWR_WAKEUP_FLAG_NET;
            if (stop2_wakeup_rising_pins & WIFI_SPI_IRQ_Pin) global_wakeup_flags |= PWR_WAKEUP_FLAG_SI91X;
            if (rtc_wake_up_flag) global_wakeup_flags |= PWR_WAKEUP_FLAG_RTC_TIMING;
            if (rtc_alarm_a_flag) global_wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_A;
            if (rtc_alarm_b_flag) global_wakeup_flags |= PWR_WAKEUP_FLAG_RTC_ALARM_B;

            rtc_wake_up_flag = 0;
            rtc_alarm_a_flag = 0;
            rtc_alarm_b_flag = 0;
            stop2_wakeup_falling_pins = 0;
            stop2_wakeup_rising_pins = 0;
        }
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
            global_wakeup_flags |= PWR_WAKEUP_FLAG_IWDG;
            __HAL_RCC_CLEAR_RESET_FLAGS();
        }

        global_wakeup_flags |= PWR_WAKEUP_FLAG_VALID;
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0);

        if (global_wakeup_flags & (PWR_WAKEUP_FLAG_PIR_FALLING | PWR_WAKEUP_FLAG_PIR_RISING | PWR_WAKEUP_FLAG_PIR_LOW | PWR_WAKEUP_FLAG_PIR_HIGH)) {
            pir_trigger_reset();
        }
    }
    return global_wakeup_flags;
}

void pwr_clear_wakeup_flags(void)
{
    global_wakeup_flags &= PWR_WAKEUP_FLAG_VALID;
}

void pwr_enter_standby(uint32_t wakeup_flags, pwr_rtc_wakeup_config_t *rtc_wakeup_config)
{
    RTC_AlarmTypeDef sAlarm = {0};
    uint32_t wakeup_time_s = 0;

    if (rtc_wakeup_config != NULL && wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING && rtc_wakeup_config->wakeup_time_s > 0) {
        if (rtc_wakeup_config->wakeup_time_s <= PWR_RTC_WAKEUP_ADV_OFFSET_S) {
            pwr_n6_restart(970, 30);
            global_wakeup_flags = (PWR_WAKEUP_FLAG_VALID | PWR_WAKEUP_FLAG_STANDBY | PWR_WAKEUP_FLAG_RTC_TIMING);
            return;
        }
    }

    if (wakeup_flags & PWR_WAKEUP_FLAG_CONFIG_KEY) {
        HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1_LOW);
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF1);
    }

    if ((wakeup_flags & PWR_WAKEUP_FLAG_PIR_LOW) && (wakeup_flags & PWR_WAKEUP_FLAG_PIR_HIGH) == 0) {
        HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_2);
        HAL_PWREx_EnablePullUpPullDownConfig();
        HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN3_HIGH);
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF4);

    } else if ((wakeup_flags & PWR_WAKEUP_FLAG_PIR_HIGH) && (wakeup_flags & PWR_WAKEUP_FLAG_PIR_LOW) == 0) {
        HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_2);
        HAL_PWREx_EnablePullUpPullDownConfig();
        HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN3_LOW);
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF4);
    }

    // if (wakeup_flags & PWR_WAKEUP_FLAG_NET) {
    //     // HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_2);
    //     // HAL_PWREx_EnablePullUpPullDownConfig();
    //     HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_HIGH);
    //     __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF4);
    // }

    if (rtc_wakeup_config != NULL) {
        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) && rtc_wakeup_config->wakeup_time_s > 0) {
            wakeup_time_s = (rtc_wakeup_config->wakeup_time_s & PWR_RTC_WAKEUP_MAX_TIME_S);
            HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, (wakeup_time_s - PWR_RTC_WAKEUP_ADV_OFFSET_S), RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
        } else {
            HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
        }

        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A) && rtc_wakeup_config->alarm_a.is_valid) {
            sAlarm.AlarmTime.Hours = rtc_wakeup_config->alarm_a.hour;
            sAlarm.AlarmTime.Minutes = rtc_wakeup_config->alarm_a.minute;
            sAlarm.AlarmTime.Seconds = rtc_wakeup_config->alarm_a.second;
            sAlarm.AlarmTime.SubSeconds = 0;
            if (rtc_wakeup_config->alarm_a.week_day > 0 && rtc_wakeup_config->alarm_a.week_day <= 7) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_a.week_day;
            } else if (rtc_wakeup_config->alarm_a.date > 0 && rtc_wakeup_config->alarm_a.date <= 31) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_a.date;
            } else {
                sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
            }
            sAlarm.Alarm = RTC_ALARM_A;
            HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
        } else {
            HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
        }

        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B) && rtc_wakeup_config->alarm_b.is_valid) {
            sAlarm.AlarmTime.Hours = rtc_wakeup_config->alarm_b.hour;
            sAlarm.AlarmTime.Minutes = rtc_wakeup_config->alarm_b.minute;
            sAlarm.AlarmTime.Seconds = rtc_wakeup_config->alarm_b.second;
            sAlarm.AlarmTime.SubSeconds = 0;
            if (rtc_wakeup_config->alarm_b.week_day > 0 && rtc_wakeup_config->alarm_b.week_day <= 7) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_b.week_day;
            } else if (rtc_wakeup_config->alarm_b.date > 0 && rtc_wakeup_config->alarm_b.date <= 31) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_b.date;
            } else {
                sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
            }
            sAlarm.Alarm = RTC_ALARM_B;
            HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
        } else {
            HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_B);
        }

        if (((wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) && rtc_wakeup_config->wakeup_time_s > 0) || ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A) && rtc_wakeup_config->alarm_a.is_valid) || ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B) && rtc_wakeup_config->alarm_b.is_valid)) {
            HAL_NVIC_SetPriority(RTC_TAMP_IRQn, 0, 0);
            HAL_NVIC_EnableIRQ(RTC_TAMP_IRQn);
        }
    }

    /* Enable Backup domain access */
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, wakeup_flags);

    HAL_PWR_EnterSTANDBYMode();
}

extern void TIM6_Delay_Init(void);
void pwr_enter_stop2(uint32_t wakeup_flags, uint32_t switch_bits, pwr_rtc_wakeup_config_t *rtc_wakeup_config)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RTC_AlarmTypeDef sAlarm = {0};
    uint32_t usb_in_status = 0;
    uint32_t remain_wakeup_time_s = 0, wakeup_time_s = 0;

    if (rtc_wakeup_config != NULL && wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING && rtc_wakeup_config->wakeup_time_s > 0) {
        if (rtc_wakeup_config->wakeup_time_s <= PWR_RTC_WAKEUP_ADV_OFFSET_S) {
            pwr_n6_restart(970, 30);
            global_wakeup_flags = (PWR_WAKEUP_FLAG_VALID | PWR_WAKEUP_FLAG_STOP2 | PWR_WAKEUP_FLAG_RTC_TIMING);
            return;
        }
    }

    usb_in_status = pwr_usb_is_active();
    HAL_UART_DeInit(&huart1);
	HAL_UART_DeInit(&hlpuart2);
    HAL_NVIC_DisableIRQ(DMA1_Channel2_3_IRQn);
    GPIO_All_Config_Analog();
    if (!(switch_bits & PWR_3V3_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_3V3_GPIO_Port, PWR_3V3_Pin, GPIO_PIN_RESET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_3V3_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_3V3_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_3V3_GPIO_Port, &GPIO_InitStruct);
        }
    }
    // if (!(switch_bits & PWR_EXT_SWITCH_BIT)) {
    //     HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, GPIO_PIN_RESET);
    //     if (usb_in_status == 0) {
    //         GPIO_InitStruct.Pin = PWR_EXT_Pin;
    //         GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    //         GPIO_InitStruct.Pull = GPIO_NOPULL;
    //         HAL_GPIO_DeInit(PWR_EXT_GPIO_Port, GPIO_InitStruct.Pin);
    //         HAL_GPIO_Init(PWR_EXT_GPIO_Port, &GPIO_InitStruct);
    //     }
    // }
    if (!(switch_bits & PWR_WIFI_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin, GPIO_PIN_RESET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_WIFI_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_WIFI_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_WIFI_GPIO_Port, &GPIO_InitStruct);
        }
    }
    if (!(switch_bits & PWR_AON_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, GPIO_PIN_RESET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_AON_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_AON_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_AON_GPIO_Port, &GPIO_InitStruct);
        }
    }
    if (!(switch_bits & PWR_N6_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, GPIO_PIN_RESET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_N6_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_N6_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_N6_GPIO_Port, &GPIO_InitStruct);
        }
    }
    HAL_Delay(10);
    
    stop2_wakeup_falling_pins = 0;
    stop2_wakeup_rising_pins = 0;
    if (wakeup_flags & PWR_WAKEUP_FLAG_CONFIG_KEY) {
        GPIO_InitStruct.Pin = CONFIG_KEY_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_DeInit(CONFIG_KEY_GPIO_Port, GPIO_InitStruct.Pin);
        HAL_GPIO_Init(CONFIG_KEY_GPIO_Port, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(CONFIG_KEY_EXTI_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(CONFIG_KEY_EXTI_IRQn);
    }

    if ((wakeup_flags & PWR_WAKEUP_FLAG_PIR_FALLING) || (wakeup_flags & PWR_WAKEUP_FLAG_PIR_RISING)) {
        GPIO_InitStruct.Pin = PIR_TRIGGER_Pin;
        GPIO_InitStruct.Mode = (wakeup_flags & PWR_WAKEUP_FLAG_PIR_FALLING) ? ((wakeup_flags & PWR_WAKEUP_FLAG_PIR_RISING) ? GPIO_MODE_IT_RISING_FALLING : GPIO_MODE_IT_FALLING) : GPIO_MODE_IT_RISING;
        GPIO_InitStruct.Pull = (wakeup_flags & PWR_WAKEUP_FLAG_PIR_FALLING) ? ((wakeup_flags & PWR_WAKEUP_FLAG_PIR_RISING) ? GPIO_NOPULL : GPIO_PULLUP) : GPIO_PULLDOWN;
        HAL_GPIO_DeInit(PIR_TRIGGER_GPIO_Port, GPIO_InitStruct.Pin);
        HAL_GPIO_Init(PIR_TRIGGER_GPIO_Port, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(PIR_TRIGGER_EXTI_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(PIR_TRIGGER_EXTI_IRQn);
    }

    // if (wakeup_flags & PWR_WAKEUP_FLAG_NET) {
    //     GPIO_InitStruct.Pin = NET_WKUP_Pin;
    //     GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    //     GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    //     HAL_GPIO_DeInit(NET_WKUP_GPIO_Port, GPIO_InitStruct.Pin);
    //     HAL_GPIO_Init(NET_WKUP_GPIO_Port, &GPIO_InitStruct);

    //     HAL_NVIC_SetPriority(NET_WKUP_EXTI_IRQn, 0, 0);
    //     HAL_NVIC_EnableIRQ(NET_WKUP_EXTI_IRQn);
    // }

    if (wakeup_flags & PWR_WAKEUP_FLAG_SI91X) {
        GPIO_InitStruct.Pin = WIFI_SPI_IRQ_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        HAL_GPIO_DeInit(WIFI_SPI_IRQ_GPIO_Port, GPIO_InitStruct.Pin);
        HAL_GPIO_Init(WIFI_SPI_IRQ_GPIO_Port, &GPIO_InitStruct);

        HAL_NVIC_SetPriority(WIFI_SPI_IRQ_EXTI_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(WIFI_SPI_IRQ_EXTI_IRQn);
    }

    rtc_wake_up_flag = 0;
    rtc_alarm_a_flag = 0;
    rtc_alarm_b_flag = 0;
    if (rtc_wakeup_config != NULL) {
        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) && rtc_wakeup_config->wakeup_time_s > 0) {
            if (rtc_wakeup_config->wakeup_time_s > PWR_RTC_WAKEUP_MAX_TIME_S) {
                remain_wakeup_time_s = rtc_wakeup_config->wakeup_time_s - PWR_RTC_WAKEUP_MAX_TIME_S;
                wakeup_time_s = PWR_RTC_WAKEUP_MAX_TIME_S;
            } else wakeup_time_s = rtc_wakeup_config->wakeup_time_s;
            HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, (wakeup_time_s - PWR_RTC_WAKEUP_ADV_OFFSET_S), RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
        } else {
            HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
        }

        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A) && rtc_wakeup_config->alarm_a.is_valid) {
            sAlarm.AlarmTime.Hours = rtc_wakeup_config->alarm_a.hour;
            sAlarm.AlarmTime.Minutes = rtc_wakeup_config->alarm_a.minute;
            sAlarm.AlarmTime.Seconds = rtc_wakeup_config->alarm_a.second;
            sAlarm.AlarmTime.SubSeconds = 0;
            if (rtc_wakeup_config->alarm_a.week_day > 0 && rtc_wakeup_config->alarm_a.week_day <= 7) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_a.week_day;
            } else if (rtc_wakeup_config->alarm_a.date > 0 && rtc_wakeup_config->alarm_a.date <= 31) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_a.date;
            } else {
                sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
            }
            sAlarm.Alarm = RTC_ALARM_A;
            HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
        } else {
            HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
        }

        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B) && rtc_wakeup_config->alarm_b.is_valid) {
            sAlarm.AlarmTime.Hours = rtc_wakeup_config->alarm_b.hour;
            sAlarm.AlarmTime.Minutes = rtc_wakeup_config->alarm_b.minute;
            sAlarm.AlarmTime.Seconds = rtc_wakeup_config->alarm_b.second;
            sAlarm.AlarmTime.SubSeconds = 0;
            if (rtc_wakeup_config->alarm_b.week_day > 0 && rtc_wakeup_config->alarm_b.week_day <= 7) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_b.week_day;
            } else if (rtc_wakeup_config->alarm_b.date > 0 && rtc_wakeup_config->alarm_b.date <= 31) {
                sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
                sAlarm.AlarmDateWeekDay = rtc_wakeup_config->alarm_b.date;
            } else {
                sAlarm.AlarmMask = RTC_ALARMMASK_DATEWEEKDAY;
            }
            sAlarm.Alarm = RTC_ALARM_B;
            HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
        } else {
            HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_B);
        }

        
        if (((wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) && rtc_wakeup_config->wakeup_time_s > 0) || ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A) && rtc_wakeup_config->alarm_a.is_valid) || ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B) && rtc_wakeup_config->alarm_b.is_valid)) {
            HAL_NVIC_SetPriority(RTC_TAMP_IRQn, 0, 0);
            HAL_NVIC_EnableIRQ(RTC_TAMP_IRQn);
        }
    }

    do {
        global_wakeup_flags = 0;
        HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
        
        SYSCLKConfig_STOP();
        TIM6_Delay_Init();
        pwr_get_wakeup_flags();
        
        if ((global_wakeup_flags == (PWR_WAKEUP_FLAG_VALID | PWR_WAKEUP_FLAG_STOP2 | PWR_WAKEUP_FLAG_RTC_TIMING)) && (remain_wakeup_time_s > 0)) {
            rtc_wake_up_flag = 0;
            HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
            wakeup_time_s = (remain_wakeup_time_s > PWR_RTC_WAKEUP_MAX_TIME_S) ? PWR_RTC_WAKEUP_MAX_TIME_S : remain_wakeup_time_s;
            remain_wakeup_time_s -= wakeup_time_s;
            if (wakeup_time_s <= PWR_RTC_WAKEUP_ADV_OFFSET_S) {
                osDelay(PWR_RTC_WAKEUP_ADV_OFFSET_S * 1000);
                break;
            }
            HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, (wakeup_time_s - PWR_RTC_WAKEUP_ADV_OFFSET_S), RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
        } else break;
    } while (1);

    if (((wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) && rtc_wakeup_config->wakeup_time_s > 0) || ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A) && rtc_wakeup_config->alarm_a.is_valid) || ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B) && rtc_wakeup_config->alarm_b.is_valid)) {
        HAL_NVIC_DisableIRQ(RTC_TAMP_IRQn);
        if ((wakeup_flags & PWR_WAKEUP_FLAG_RTC_TIMING) && rtc_wakeup_config->wakeup_time_s > 0) HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
        if (wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_A && rtc_wakeup_config->alarm_a.is_valid) HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
        if (wakeup_flags & PWR_WAKEUP_FLAG_RTC_ALARM_B && rtc_wakeup_config->alarm_b.is_valid) HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_B);
    }
    
    if (wakeup_flags & PWR_WAKEUP_FLAG_CONFIG_KEY) {
        HAL_NVIC_DisableIRQ(CONFIG_KEY_EXTI_IRQn);
    }
    GPIO_InitStruct.Pin = CONFIG_KEY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_DeInit(CONFIG_KEY_GPIO_Port, GPIO_InitStruct.Pin);
    HAL_GPIO_Init(CONFIG_KEY_GPIO_Port, &GPIO_InitStruct);
    if ((wakeup_flags & PWR_WAKEUP_FLAG_PIR_FALLING) || (wakeup_flags & PWR_WAKEUP_FLAG_PIR_RISING)) {
        HAL_NVIC_DisableIRQ(PIR_TRIGGER_EXTI_IRQn);
        GPIO_InitStruct.Pin = PIR_TRIGGER_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_DeInit(PIR_TRIGGER_GPIO_Port, GPIO_InitStruct.Pin);
        HAL_GPIO_Init(PIR_TRIGGER_GPIO_Port, &GPIO_InitStruct);
    }
    // if (wakeup_flags & PWR_WAKEUP_FLAG_NET) {
    //     HAL_NVIC_DisableIRQ(NET_WKUP_EXTI_IRQn);
    //     GPIO_InitStruct.Pin = NET_WKUP_Pin;
    //     GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    //     GPIO_InitStruct.Pull = GPIO_NOPULL;
    //     HAL_GPIO_DeInit(NET_WKUP_GPIO_Port, GPIO_InitStruct.Pin);
    //     HAL_GPIO_Init(NET_WKUP_GPIO_Port, &GPIO_InitStruct);
    // }
    if (wakeup_flags & PWR_WAKEUP_FLAG_SI91X) {
        HAL_NVIC_DisableIRQ(WIFI_SPI_IRQ_EXTI_IRQn);
        GPIO_InitStruct.Pin = WIFI_SPI_IRQ_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_DeInit(WIFI_SPI_IRQ_GPIO_Port, GPIO_InitStruct.Pin);
        HAL_GPIO_Init(WIFI_SPI_IRQ_GPIO_Port, &GPIO_InitStruct);
    }
    
    // IF PWR_WAKEUP_FLAG_CONFIG_KEY, we should wait for the key released
    if (global_wakeup_flags & PWR_WAKEUP_FLAG_CONFIG_KEY) {
        pwr_wait_for_key_release();
    }

    if (!(switch_bits & PWR_3V3_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_3V3_GPIO_Port, PWR_3V3_Pin, GPIO_PIN_SET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_3V3_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_3V3_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_3V3_GPIO_Port, &GPIO_InitStruct);
        }
    }
    if (!(switch_bits & PWR_EXT_SWITCH_BIT)) {
        // HAL_GPIO_WritePin(PWR_EXT_GPIO_Port, PWR_EXT_Pin, GPIO_PIN_SET);
        // if (usb_in_status == 0) {
        //     GPIO_InitStruct.Pin = PWR_EXT_Pin;
        //     GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        //     GPIO_InitStruct.Pull = GPIO_NOPULL;
        //     HAL_GPIO_DeInit(PWR_EXT_GPIO_Port, GPIO_InitStruct.Pin);
        //     HAL_GPIO_Init(PWR_EXT_GPIO_Port, &GPIO_InitStruct);
        // }
    }
    if (!(switch_bits & PWR_WIFI_SWITCH_BIT)) {
        // HAL_GPIO_WritePin(PWR_WIFI_GPIO_Port, PWR_WIFI_Pin, GPIO_PIN_SET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_WIFI_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_WIFI_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_WIFI_GPIO_Port, &GPIO_InitStruct);
        }
    }
    if (!(switch_bits & PWR_AON_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, GPIO_PIN_SET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_AON_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_AON_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_AON_GPIO_Port, &GPIO_InitStruct);
        }
    }
    if (!(switch_bits & PWR_N6_SWITCH_BIT)) {
        HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, GPIO_PIN_SET);
        if (usb_in_status == 0) {
            GPIO_InitStruct.Pin = PWR_N6_Pin;
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            HAL_GPIO_DeInit(PWR_N6_GPIO_Port, GPIO_InitStruct.Pin);
            HAL_GPIO_Init(PWR_N6_GPIO_Port, &GPIO_InitStruct);
        }
    }
    GPIO_InitStruct.Pin = USB_IN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_DeInit(USB_IN_GPIO_Port, GPIO_InitStruct.Pin);
    HAL_GPIO_Init(USB_IN_GPIO_Port, &GPIO_InitStruct);
    MX_USART1_UART_Init();
    MX_LPUART2_UART_Init();
    HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
}

void pwr_n6_restart(uint32_t low_ms, uint32_t high_ms)
{
    HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, GPIO_PIN_RESET);
    osDelay(low_ms);
    HAL_GPIO_WritePin(PWR_N6_GPIO_Port, PWR_N6_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PWR_AON_GPIO_Port, PWR_AON_Pin, GPIO_PIN_SET);
    osDelay(high_ms);
}

extern void delay_us(uint16_t us);
uint32_t pwr_usb_is_active(void)
{
    if (HAL_GPIO_ReadPin(USB_IN_GPIO_Port, USB_IN_Pin) == GPIO_PIN_SET) {
        delay_us(100);
        if (HAL_GPIO_ReadPin(USB_IN_GPIO_Port, USB_IN_Pin) == GPIO_PIN_SET) {
            return 1;
        }
    }
    return 0;
}

void pwr_wait_for_key_release(void)
{
    uint32_t start_tick = 0, end_tick = 0, diff_tick = 0;
        
    start_tick = HAL_GetTick();
    do {
        if (HAL_GPIO_ReadPin(CONFIG_KEY_GPIO_Port, CONFIG_KEY_Pin) == GPIO_PIN_SET) {
            delay_us(100);
            if (HAL_GPIO_ReadPin(CONFIG_KEY_GPIO_Port, CONFIG_KEY_Pin) == GPIO_PIN_SET) {
                break;
            }
        }
        HAL_Delay(10);
        HAL_IWDG_Refresh(&hiwdg);
        end_tick = HAL_GetTick();
        diff_tick = (end_tick >= start_tick) ? (end_tick - start_tick) : (end_tick + (0xFFFFFFFFU - start_tick));
    } while (diff_tick < PWR_WAKEUP_KEY_MAX_PRESS_MS);
    if (diff_tick >= PWR_WAKEUP_KEY_MAX_PRESS_MS) {
        global_wakeup_flags |= PWR_WAKEUP_FLAG_KEY_MAX_PRESS;
    } else if (diff_tick >= PWR_WAKEUP_KEY_LONG_PRESS_MS) {
        global_wakeup_flags |= PWR_WAKEUP_FLAG_KEY_LONG_PRESS;
    }
}