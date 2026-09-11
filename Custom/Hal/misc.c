#include "misc.h"
#include "debug.h"
#include "tim.h"
#include "adc.h"
#include "generic_key.h"
#include "generic_led.h"
#include "pwr.h"
#include "main.h"
#include "common_utils.h"
#include "exti.h"

static uint8_t key_read(void);
static int light_get_value(uint8_t *rate);
static int battery_get_value(void);

static uint8_t bat_tread_stack[1024 * 2] ALIGN_32 IN_PSRAM;
static uint8_t led_tread_stack[1024 * 2] ALIGN_32 IN_PSRAM;
static uint8_t key_tread_stack[1024 * 16] ALIGN_32 IN_PSRAM;
const osThreadAttr_t batTask_attributes = {
    .name = "batTask",
    .priority = (osPriority_t) osPriorityLow,
    .stack_mem = bat_tread_stack,
    .stack_size = sizeof(bat_tread_stack),
};
const osThreadAttr_t ledTask_attributes = {
    .name = "ledTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = led_tread_stack,
    .stack_size = sizeof(led_tread_stack),
};

const osThreadAttr_t keyTask_attributes = {
    .name = "keyTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = key_tread_stack,
    .stack_size = sizeof(key_tread_stack),
};

static osThreadId_t led_processId, key_processId, bat_processId;
static misc_t g_key, g_flash, g_ind, g_light, g_battery, g_io, g_ind_ext;

gpio_group_t io_groups[] = {
    {
        .name = IO_ALARM_NAME,
        .pin = GPIO_PIN_12,
        .port = GPIOB,
        .mode = IO_MODE_INTERRUPT,
        .int_type = IO_INT_RISING_EDGE,
        .int_cb = NULL,
        .output_state = IO_OUTPUT_LOW
    }
};
static io_dev_cfg_t g_io_cfg = {0};

static key_instance_t f_key ={
    .config ={
        .read_key_state = key_read,
        .debounce_time = 20,
        .double_click_time = 300,
        .long_press_time = 2000,       // 2s for AP enable
        .super_long_press_time = 10000, // 10s for factory reset
        .short_press_cb = NULL,
        .double_click_cb = NULL,
        .long_press_cb = NULL,
        .super_long_press_cb = NULL 
    }
};

static pwm_cfg_t flash_cfg = {
    .duty = FLASH_DUTY
};


static int misc_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    misc_t *misc = (misc_t *)priv;
    MISC_CMD_E misc_cmd = (MISC_CMD_E)cmd;
    int ret = AICAM_OK;
    if(!misc->is_init)
        return AICAM_ERROR_NOT_FOUND;
    
    osMutexAcquire(misc->mtx_id, osWaitForever);

    if (misc->type == MISC_TYPE_LED){
        blink_params_t params = {0};
        switch (misc_cmd)
        {
            case MISC_CMD_LED_ON:
                led_set_state(misc->handle, LED_STATE_ON, 0, 0);
                break;

            case MISC_CMD_LED_OFF:
                led_set_state(misc->handle, LED_STATE_OFF, 0, 0);
                break;

            case MISC_CMD_LED_SET_BLINK:
                memcpy(&params, ubuf, sizeof(blink_params_t));
                led_set_state(misc->handle, LED_STATE_BLINK, params.blink_times, params.interval_ms);
                break;
            
            default:
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
        }
    }else if (misc->type == MISC_TYPE_BUTTON){
        misc_button_cb cb = (misc_button_cb)ubuf;
        button_params_t button_params = {0};
        switch(misc_cmd){
            case MISC_CMD_BUTTON_SET_SP_CB:
                key_regitster_cb((key_instance_t*)misc->handle, KEY_EVENT_SHORT_PRESS, cb);
                break;
            case MISC_CMD_BUTTON_SET_DC_CB:
                key_regitster_cb((key_instance_t*)misc->handle, KEY_EVENT_DOUBLE_CLICK, cb);
                break;
            case MISC_CMD_BUTTON_SET_LP_CB:
                key_regitster_cb((key_instance_t*)misc->handle, KEY_EVENT_LONG_PRESS, cb);
                break;
            case MISC_CMD_BUTTON_SET_SLP_CB:
                key_regitster_cb((key_instance_t*)misc->handle, KEY_EVENT_SUPER_LONG_PRESS, cb);
                break;
            
            case MISC_CMD_BUTTON_GET_PARAMS:
                button_params.debounce_time = ((key_instance_t*)misc->handle)->config.debounce_time;
                button_params.double_click_time = ((key_instance_t*)misc->handle)->config.double_click_time;
                button_params.long_press_time = ((key_instance_t*)misc->handle)->config.long_press_time;
                button_params.super_long_press_time = ((key_instance_t*)misc->handle)->config.super_long_press_time;
                memcpy(ubuf, &button_params, sizeof(button_params_t));
                break;

            case MISC_CMD_BUTTON_SET_PARAMS:
                memcpy(&button_params, ubuf, sizeof(button_params_t));
                ((key_instance_t*)misc->handle)->config.debounce_time = button_params.debounce_time;
                ((key_instance_t*)misc->handle)->config.double_click_time = button_params.double_click_time;
                ((key_instance_t*)misc->handle)->config.long_press_time = button_params.long_press_time;
                ((key_instance_t*)misc->handle)->config.super_long_press_time = button_params.super_long_press_time;
                break;
            default:
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
        }
    }else if (misc->type == MISC_TYPE_PWM){
        blink_params_t pwm_params = {0};
        switch(misc_cmd){
            case MISC_CMD_PWM_ON:
                led_set_state(misc->handle, LED_STATE_ON, 0, 0);
                break;
            
            case MISC_CMD_PWM_OFF:
                led_set_state(misc->handle, LED_STATE_OFF, 0, 0);
                break;
            
            case MISC_CMD_PWM_SET_BLINK:
                memcpy(&pwm_params, ubuf, sizeof(blink_params_t));
                led_set_state(misc->handle, LED_STATE_BLINK, pwm_params.blink_times, pwm_params.interval_ms);
                break;
            
            case MISC_CMD_PWM_SET_DUTY:
                ((pwm_cfg_t *)misc->config)->duty = *ubuf;
                break;
            default:
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
        }
        
    }else if(misc->type == MISC_TYPE_ADC){
        switch(misc_cmd){
            case MISC_CMD_ADC_GET_PERCENT:
                if(strcmp(misc->dev->name, LIGHT_DEVICE_NAME) == 0){
                    if(light_get_value(ubuf) < 0)
                        ret = AICAM_ERROR_NOT_FOUND;
                }else if(strcmp(misc->dev->name, BATTERY_DEVICE_NAME) == 0){
                    battery_data_t *data = (battery_data_t *)misc->data;
                    *(uint8_t *)ubuf = data->rate;
                }else{
                    ret = AICAM_ERROR_NOT_FOUND;
                }
                break;
            default:
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
        }
    }else if (misc->type == MISC_TYPE_IO) {
        io_dev_cfg_t *io_cfg = (io_dev_cfg_t *)misc->config;
        io_group_cfg_t *cfg = (io_group_cfg_t *)ubuf;
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        int idx = -1;
        for (uint8_t i = 0; i < io_cfg->group_num; i++) {
            if (strcmp(io_cfg->groups[i].name, cfg->name) == 0) {
                idx = i;
                break;
            }
        }
        if(idx < 0) {
            ret = AICAM_ERROR_NOT_FOUND;
            osMutexRelease(misc->mtx_id);
            return ret;
        }
        switch(cmd) {
            case MISC_CMD_IO_GET_GROUP_INFO:
                io_group_info_t *info = (io_group_info_t *)ubuf;
                info->group_num = io_cfg->group_num;
                for (uint8_t i = 0; i < io_cfg->group_num; i++) {
                    strncpy(info->groups[i].name, io_cfg->groups[i].name, MAX_GPIO_NAME_LEN);
                    info->groups[i].int_type = io_cfg->groups[i].int_type;
                    info->groups[i].mode = io_cfg->groups[i].mode;
                    info->groups[i].output_state = io_cfg->groups[i].output_state;
                    info->groups[i].int_cb = io_cfg->groups[i].int_cb;
                }
                break;

            case MISC_CMD_IO_SET_MODE:
                if (idx < 0) {
                    ret = AICAM_ERROR_NOT_FOUND;
                    break;
                }
                io_cfg->groups[idx].mode = cfg->mode;
                if(io_cfg->groups[idx].mode == IO_MODE_OUTPUT) {
                    GPIO_InitStruct.Pin = io_cfg->groups[idx].pin;
                    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
                    GPIO_InitStruct.Pull = GPIO_NOPULL;
                    HAL_GPIO_Init(io_cfg->groups[idx].port, &GPIO_InitStruct);
                    HAL_NVIC_DisableIRQ(EXTI12_IRQn);
                    HAL_GPIO_WritePin(io_cfg->groups[idx].port, io_cfg->groups[idx].pin,
                        io_cfg->groups[idx].output_state == IO_OUTPUT_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
                } else if(io_cfg->groups[idx].mode == IO_MODE_INTERRUPT) {
                    GPIO_InitStruct.Pin = io_cfg->groups[idx].pin;
                    if(io_cfg->groups[idx].int_type == IO_INT_RISING_EDGE) {
                        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
                    } else {
                        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
                    }
                    GPIO_InitStruct.Pull = GPIO_NOPULL;
                    HAL_GPIO_Init(io_cfg->groups[idx].port, &GPIO_InitStruct);

                    /* EXTI interrupt init*/
                    if(cfg->int_cb != NULL) {
                        io_cfg->groups[idx].int_cb = cfg->int_cb;
                    }
                    
                    if(io_cfg->groups[idx].int_cb != NULL) {
                        if(io_cfg->groups[idx].pin == GPIO_PIN_12) {
                            HAL_NVIC_SetPriority(EXTI12_IRQn, 5, 0);
                            HAL_NVIC_EnableIRQ(EXTI12_IRQn);
                            exti12_irq_register(io_cfg->groups[idx].int_cb);
                        }
                    }
                }
                break;

            case MISC_CMD_IO_SET_OUTPUT:
                if (idx < 0 || io_cfg->groups[idx].mode != IO_MODE_OUTPUT) {
                    ret = AICAM_ERROR_NOT_SUPPORTED;
                    break;
                }
                io_cfg->groups[idx].output_state = cfg->output_state;
                HAL_GPIO_WritePin(io_cfg->groups[idx].port, io_cfg->groups[idx].pin,
                    cfg->output_state == IO_OUTPUT_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
                break;

            case MISC_CMD_IO_SET_INT_CB:
                if (idx < 0 || io_cfg->groups[idx].mode != IO_MODE_INTERRUPT) {
                    ret = AICAM_ERROR_NOT_SUPPORTED;
                    break;
                }
                io_cfg->groups[idx].int_cb = cfg->int_cb;
                break;

            default:
                ret = AICAM_ERROR_NOT_SUPPORTED;
                break;
        }
    }

    osMutexRelease(misc->mtx_id);
    return ret;
}

static void key_short_press(void)
{
    LOG_DRV_DEBUG("key_short_press\r\n");
}

static void key_long_press(void)
{
    LOG_DRV_DEBUG("key_long_press\r\n");
}

static uint8_t key_read(void)
{
#if ENABLE_U0_MODULE
    uint8_t key_state = (uint8_t)u0_module_get_key_value_ex();
#else
    uint8_t key_state = (uint8_t)HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin);
#endif
    return !key_state;
}

static void keyProcess(void *argument)
{
    static uint32_t last_time = 0;
    misc_t *key = (misc_t *)argument;
    while (key->is_init) {
        key_process((key_instance_t *)key->handle, HAL_GetTick() - last_time);
#if ENABLE_U0_MODULE
        u0_module_callback_process();
#endif
        last_time = HAL_GetTick();
        osDelay(10);
    }
    LOG_DRV_ERROR("keyProcess exit \r\n");
    key_processId = NULL;  // Thread cleans up its own ID
    osThreadExit();
}

static int key_init(void *priv)
{
    misc_t *key = (misc_t *)priv;
#if !defined(ENABLE_U0_MODULE) || (ENABLE_U0_MODULE == 0)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = KEY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(KEY_GPIO_Port, &GPIO_InitStruct);
#endif
    key_module_init(&f_key);
    key_regitster_cb(&f_key, KEY_EVENT_SHORT_PRESS, key_short_press);
    key_regitster_cb(&f_key, KEY_EVENT_LONG_PRESS, key_long_press);
    key->handle = (uint32_t)&f_key;
    key->mtx_id = osMutexNew(NULL);
    key->type = MISC_TYPE_BUTTON;
    key_processId = osThreadNew(keyProcess, key, &keyTask_attributes);
    key->is_init = true;
    return 0;
}

static int key_deinit(void *priv)
{
    misc_t *key = (misc_t *)priv;
    key->is_init = false;
    osDelay(100);
    if (key_processId != NULL) {
        osThreadTerminate(key_processId);
        key_processId = NULL;
    }
    if (key->mtx_id != NULL) {
        osMutexDelete(key->mtx_id);
        key->mtx_id = NULL;
    }
    key->handle = 0;
    return 0;
}

static void key_register(void)
{
    static dev_ops_t key_ops = {
        .init = key_init, 
        .deinit = key_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_key.dev = dev;
    strcpy(dev->name, KEY_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &key_ops;
    dev->priv_data = &g_key;
    device_register(g_key.dev);

}

static void key_unregister(void)
{
    if (g_key.dev) {
        device_unregister(g_key.dev);
        hal_mem_free(g_key.dev);
        g_key.dev = NULL;
    }
}

static void flash_on(void)
{
    pwm_cfg_t * pwm_cfg = (pwm_cfg_t *)g_flash.config;
    if(g_flash.state != true){
        pwr_manager_acquire(g_flash.pwr_handle);
    }
    TIM_set_duty(pwm_cfg->duty);
    g_flash.state = true;
}

static void flash_off(void)
{
    TIM_set_duty(0);
    if(g_flash.state != false){
        pwr_manager_release(g_flash.pwr_handle);
    }
    g_flash.state = false;
}

static void flash_lock(bool lock)
{
    if(lock){
        osMutexAcquire(g_flash.mtx_id, osWaitForever);
    }else{
        osMutexRelease(g_flash.mtx_id);
    }
}

static int flash_init(void *priv)
{
    misc_t *flash = (misc_t *)priv;
    MX_TIM3_Init();

    flash->config = (void *)&flash_cfg;
    flash->mtx_id = osMutexNew(NULL);
    flash->handle = led_register(flash_on, flash_off, flash_lock, HAL_GetTick);
    flash->pwr_handle = pwr_manager_get_handle(PWR_SENSOR_NAME);
    flash->type = MISC_TYPE_PWM;
    flash->is_init = true;
    return 0;
}

static int flash_deinit(void *priv)
{
    misc_t *flash = (misc_t *)priv;
    flash->is_init = false;
    flash_off();
    if (flash->mtx_id != NULL) {
        osMutexDelete(flash->mtx_id);
        flash->mtx_id = NULL;
    }
    if (flash->pwr_handle != 0) {
        pwr_manager_release(flash->pwr_handle);
        flash->pwr_handle = 0;
    }
    MX_TIM3_DeInit();
    return 0;
}

static void flash_register(void)
{
    static dev_ops_t flash_ops = {
        .init = flash_init,
        .deinit = flash_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_flash.dev = dev;
    strcpy(dev->name, FLASH_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &flash_ops;
    dev->priv_data = &g_flash;
    device_register(g_flash.dev);
}

static void flash_unregister(void)
{
    if (g_flash.dev) {
        device_unregister(g_flash.dev);
        hal_mem_free(g_flash.dev);
        g_flash.dev = NULL;
    }
}

static void ind_on(void)
{
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

static void ind_off(void)
{
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}

static void ind_lock(bool lock)
{
    if(lock){
        osMutexAcquire(g_ind.mtx_id, osWaitForever);
    }else{
        osMutexRelease(g_ind.mtx_id);
    }
}

static int ind_init(void *priv)
{
    misc_t *ind = (misc_t *)priv;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitStruct.Pin = LED_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

    ind->mtx_id = osMutexNew(NULL);
    ind->type = MISC_TYPE_LED;
    ind->handle = led_register(ind_on, ind_off, ind_lock, HAL_GetTick);
    ind->is_init = true;
    return 0;
}

static int ind_deinit(void *priv)
{
    misc_t *ind = (misc_t *)priv;
    ind->is_init = false;
    ind_off();
    if (ind->mtx_id != NULL) {
        osMutexDelete(ind->mtx_id);
        ind->mtx_id = NULL;
    }
    return 0;
}

static void ind_register(void)
{
    static dev_ops_t ind_ops = {
        .init = ind_init,
        .deinit = ind_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_ind.dev = dev;
    strcpy(dev->name, IND_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &ind_ops;
    dev->priv_data = &g_ind;
    device_register(g_ind.dev);
}

static void ind_unregister(void)
{
    if (g_ind.dev) {
        device_unregister(g_ind.dev);
        hal_mem_free(g_ind.dev);
        g_ind.dev = NULL;
    }
}

static void ind_ext_on(void)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
}

static void ind_ext_off(void)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
}

static void ind_ext_lock(bool lock)
{
    if(lock){
        osMutexAcquire(g_ind_ext.mtx_id, osWaitForever);
    }else{
        osMutexRelease(g_ind_ext.mtx_id);
    }
}

static int ind_ext_init(void *priv)
{
    misc_t *ind_ext = (misc_t *)priv;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOF_CLK_ENABLE();

    GPIO_InitStruct.Pin = LED1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED1_GPIO_Port, &GPIO_InitStruct);
    ind_ext->mtx_id = osMutexNew(NULL);
    ind_ext->type = MISC_TYPE_LED;
    ind_ext->handle = led_register(ind_ext_on, ind_ext_off, ind_ext_lock, HAL_GetTick);
    ind_ext->is_init = true;
    return 0;
}

static int ind_ext_deinit(void *priv)
{
    misc_t *ind_ext = (misc_t *)priv;
    ind_ext->is_init = false;
    ind_ext_off();
    if (ind_ext->mtx_id != NULL) {
        osMutexDelete(ind_ext->mtx_id);
        ind_ext->mtx_id = NULL;
    }
    return 0;
}

static void ind_ext_register(void)
{
    static dev_ops_t ind_ext_ops = {
        .init = ind_ext_init,
        .deinit = ind_ext_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_ind_ext.dev = dev;
    strcpy(dev->name, IND_EXT_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &ind_ext_ops;
    dev->priv_data = &g_ind_ext;
    device_register(g_ind_ext.dev);
}

static void ind_ext_unregister(void)
{
    if (g_ind_ext.dev) {
        device_unregister(g_ind_ext.dev);
        hal_mem_free(g_ind_ext.dev);
        g_ind_ext.dev = NULL;
    }
}

static void ledProcess(void *argument)
{
    while (1) {
        led_service();
        osDelay(20);
    }
    osThreadExit();
}

static int light_get_value(uint8_t *rate)
{
    uint32_t voltage = 0;
    if(!g_light.is_init)
        return -1;
    pwr_manager_acquire(g_light.pwr_handle);
    osDelay(1000);
    ADC_get_value(&voltage, 1);
    pwr_manager_release(g_light.pwr_handle);
    LOG_SIMPLE("light  get  voltage :%ld \r\n",voltage);
    voltage = MIN(MAX(voltage, LIGHT_MIN_SENS), LIGHT_MAX_SENS);
    *rate = (uint8_t)((voltage - LIGHT_MIN_SENS) * 100 / (LIGHT_MAX_SENS - LIGHT_MIN_SENS));
    return 0;
}

static int light_init(void *priv)
{
    misc_t *light = (misc_t *)priv;
    MX_ADC1_Init();
    light->mtx_id = osMutexNew(NULL);
    light->type = MISC_TYPE_ADC;
    light->pwr_handle = pwr_manager_get_handle(PWR_SENSOR_NAME);
    light->is_init = true;
    return 0;
}

static int light_deinit(void *priv)
{
    misc_t *light = (misc_t *)priv;
    light->is_init = false;
    if (light->mtx_id != NULL) {
        osMutexDelete(light->mtx_id);
        light->mtx_id = NULL;
    }
    if (light->pwr_handle != 0) {
        pwr_manager_release(light->pwr_handle);
        light->pwr_handle = 0;
    }
    MX_ADC1_DeInit();
    return 0;
}

__attribute__((unused)) static void light_register(void)
{
    static dev_ops_t light_ops = {
        .init = light_init, 
        .deinit = light_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_light.dev = dev;
    strcpy(dev->name, LIGHT_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &light_ops;
    dev->priv_data = &g_light;
    device_register(g_light.dev);
}

__attribute__((unused)) static void light_unregister(void)
{
    if (g_light.dev) {
        device_unregister(g_light.dev);
        hal_mem_free(g_light.dev);
        g_light.dev = NULL;
    }
}

static int battery_get_value(void)
{
    uint8_t rate = 0;
    uint32_t voltage = 0;

    pwr_manager_acquire(g_battery.pwr_handle);
    osDelay(100);
    ADC_get_value(&voltage, 2);
    pwr_manager_release(g_battery.pwr_handle);

    voltage *= 4;
    // LOG_SIMPLE("batv: %ld \r\n",voltage);
    if (voltage < BATTERY_MIN_VOLTAGE / 2) {
        // maybe typec inserted
        rate = 255;
    } else {
        voltage = MIN(MAX(voltage, BATTERY_MIN_VOLTAGE), BATTERY_MAX_VOLTAGE);
        rate = (uint8_t)((voltage - BATTERY_MIN_VOLTAGE) * 100 / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE));
    }

    osMutexAcquire(g_battery.mtx_id, osWaitForever);
    ((battery_data_t *)g_battery.data)->voltage = voltage;
    ((battery_data_t *)g_battery.data)->rate = rate;
    osMutexRelease(g_battery.mtx_id);
    return 0;
}

static void batteryProcess(void *argument)
{
    uint8_t times = 0;

    while (g_battery.is_init) {
        if (times == 0) osDelay(500);
        else osDelay(15000);
        battery_get_value();
        times++;
    }
    bat_processId = NULL;
    osThreadExit();
}

static int battery_init(void *priv)
{
    misc_t *battery = (misc_t *)priv;
    battery->pwr_handle = pwr_manager_get_handle(PWR_BAT_DET_NAME);
    MX_ADC2_Init();
    battery->type = MISC_TYPE_ADC;
    battery->mtx_id = osMutexNew(NULL);
    battery->data = hal_mem_alloc_fast(sizeof(battery_data_t));
    battery->is_init = true;
    bat_processId = osThreadNew(batteryProcess, NULL, &batTask_attributes);
    return 0;
}

static int battery_deinit(void *priv)
{
    misc_t *battery = (misc_t *)priv;
    battery->is_init = false;
    if (battery->mtx_id != NULL) {
        osMutexDelete(battery->mtx_id);
        battery->mtx_id = NULL;
    }
    if (battery->pwr_handle != 0) {
        pwr_manager_release(battery->pwr_handle);
        battery->pwr_handle = 0;
    }
    if (battery->data != NULL) {
        hal_mem_free(battery->data);
        battery->data = NULL;
    }
    MX_ADC2_DeInit();
    return 0;
}

static void battery_register(void)
{
    static dev_ops_t battery_ops = {
        .init = battery_init, 
        .deinit = battery_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_battery.dev = dev;
    strcpy(dev->name, BATTERY_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &battery_ops;
    dev->priv_data = &g_battery;
    device_register(g_battery.dev);
}

static void battery_unregister(void)
{
    if (g_battery.dev) {
        device_unregister(g_battery.dev);
        hal_mem_free(g_battery.dev);
        g_battery.dev = NULL;
    }
}

static int io_init(void *priv)
{
    misc_t *io = (misc_t *)priv;
    __HAL_RCC_GPIOB_CLK_ENABLE();

    io->mtx_id = osMutexNew(NULL);
    io->config = &g_io_cfg;
    g_io_cfg.group_num = sizeof(io_groups) / sizeof(io_groups[0]);
    g_io_cfg.groups = io_groups;
    io->type = MISC_TYPE_IO;
    io->is_init = true;
    HAL_EXTI_ConfigLineAttributes(EXTI_LINE_12, EXTI_LINE_SEC);
    return 0;
}

static int io_deinit(void *priv)
{
    misc_t *io = (misc_t *)priv;
    io->is_init = false;
    if (io->mtx_id != NULL) {
        osMutexDelete(io->mtx_id);
        io->mtx_id = NULL;
    }
    io->config = NULL;
    return 0;
}

static void io_register(void)
{
    static dev_ops_t io_ops = {
        .init = io_init,
        .deinit = io_deinit,
        .ioctl = misc_ioctl
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_io.is_init = false;
    g_io.dev = dev;
    strcpy(dev->name, IO_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &io_ops;
    dev->priv_data = &g_io;
    device_register(g_io.dev);
}

static void io_unregister(void)
{
    if (g_io.dev) {
        device_unregister(g_io.dev);
        hal_mem_free(g_io.dev);
        g_io.dev = NULL;
    }
}

int misc_register(void)
{
    led_module_init();
    key_register();
    flash_register();
    ind_register();
    ind_ext_register();
    // light_register();
    battery_register();
    io_register();

    ind_on();
    // led_set_state(g_ind.handle, LED_STATE_BLINK, 1000000, 500);
    led_processId = osThreadNew(ledProcess, NULL, &ledTask_attributes);
    LOG_DRV_DEBUG("misc_register  end\r\n");
    return AICAM_OK;
}  

int misc_unregister(void)
{
    if (led_processId != NULL) {
        osThreadTerminate(led_processId);
        led_processId = NULL;
    }

    // Unregister all submodules
    battery_unregister();
    // light_unregister();
    ind_unregister();
    ind_ext_unregister();
    flash_unregister();
    key_unregister();
    io_unregister();
    return AICAM_OK;
}