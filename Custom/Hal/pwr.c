#include "main.h"
#include "pwr.h"
#include "debug.h"
#include "common_utils.h"

typedef struct {
    GPIO_TypeDef* GPIOx;
    uint16_t pin;
    uint32_t pull;
} IOGroup;

extern void SystemClock_Config(void);
extern void PeriphCommonClock_Config(void);

static pwr_t g_pwr = {0};
static uint8_t pwr_tread_stack[1024 * 2] ALIGN_32 IN_PSRAM;
const osThreadAttr_t pwrTask_attributes = {
    .name = "pwrTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_mem = pwr_tread_stack,
    .stack_size = sizeof(pwr_tread_stack),
};

void pwr_standby_mode_detect(void)
{
    if(__HAL_PWR_GET_FLAG(PWR_FLAG_SBF) != 0U){
        /* clear standby flag */
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SBF);

        /* Configure the system Power Supply */
        if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
        {
        /* Initialization Error */
        Error_Handler();
        }
        /* Configure Voltage Scaling */
        if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
        {
        /* Initialization Error */
        Error_Handler();
        }
    }
}

void pwr_enter_standby_mode(void)
{
    // NVS writes are now immediate, no flush needed
    osDelay(100);  // Wait for Flash operations to complete
    
    /* enable memory retention top keep application in standby */
    // HAL_PWREx_EnableTCMRetention();
    // HAL_PWREx_EnableTCMFLXRetention();

    // PWREx_WakeupPinTypeDef sWKUPConfigs;

    // sWKUPConfigs.WakeUpPin   = PWR_WAKEUP_PIN2;
    // sWKUPConfigs.PinPolarity = PWR_PIN_POLARITY_HIGH;
    // sWKUPConfigs.PinPull     = PWR_PIN_PULL_DOWN; ;
    // /* Enable the Wake-up pin functionality */
    // HAL_PWREx_EnableWakeUpPin(&sWKUPConfigs);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPION_CLK_ENABLE();
    __HAL_RCC_GPIOO_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_ALL;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
    HAL_GPIO_Init(GPION, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOO, &GPIO_InitStruct);

    __HAL_RCC_GPIOA_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();
    __HAL_RCC_GPIOC_CLK_DISABLE();
    __HAL_RCC_GPIOD_CLK_DISABLE();
    __HAL_RCC_GPIOE_CLK_DISABLE();
    __HAL_RCC_GPIOF_CLK_DISABLE();
    __HAL_RCC_GPIOG_CLK_DISABLE();
    __HAL_RCC_GPIOH_CLK_DISABLE();
    __HAL_RCC_GPION_CLK_DISABLE();
    __HAL_RCC_GPIOO_CLK_DISABLE();

    // HAL_PWREx_DisableVddA();
    HAL_PWREx_DisableVddIO2();
    HAL_PWREx_DisableVddIO3();
    HAL_PWREx_DisableVddIO4();

    HAL_PWR_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
    /* Enter STANDBY mode */
    HAL_PWR_EnterSTANDBYMode();

    /* should never get here */
}

void pwr_stop_mode(void)
{
        /* Enter Stop Mode */
    HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* Check if the system was resumed from Stop mode */
    if(__HAL_PWR_GET_FLAG(PWR_FLAG_STOPF) != RESET)
    {
      /* Clear stop flag */
      __HAL_PWR_CLEAR_FLAG(PWR_FLAG_STOPF);
    }

    /* Configures system clock after wake-up from Stop: enable HSE, PLL and select
    PLL as system clock source (HSE and PLL are disabled in stop mode) */
    SystemClock_Config();
    PeriphCommonClock_Config();
}

void pwr_sleep_mode(void)
{
    /*Suspend Tick increment to prevent wakeup by Systick interrupt.
    Otherwise the Systick interrupt will wake up the device within 1ms (HAL time base)*/
    HAL_SuspendTick();

    /* Enter Sleep Mode , wake up is done once USER push-button is pressed */
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);

    /* Resume Tick interrupt if disabled prior to SLEEP mode entry */
    HAL_ResumeTick();
}

static void sensor_power_init(void)
{
    HAL_GPIO_WritePin(PWR_SENSOR_ON_GPIO_Port, PWR_SENSOR_ON_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_SENSOR_ON_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWR_SENSOR_ON_GPIO_Port, &GPIO_InitStruct);
}

static void sensor_power_on(void)
{
    HAL_GPIO_WritePin(PWR_SENSOR_ON_GPIO_Port, PWR_SENSOR_ON_Pin, GPIO_PIN_SET);
}

static void sensor_power_off(void)
{
    HAL_GPIO_WritePin(PWR_SENSOR_ON_GPIO_Port, PWR_SENSOR_ON_Pin, GPIO_PIN_RESET);
}

static void bat_det_power_init(void)
{
    HAL_GPIO_WritePin(PWR_BAT_DET_ON_GPIO_Port, PWR_BAT_DET_ON_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_BAT_DET_ON_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWR_BAT_DET_ON_GPIO_Port, &GPIO_InitStruct);
}

static void bat_det_power_on(void)
{
    HAL_GPIO_WritePin(PWR_BAT_DET_ON_GPIO_Port, PWR_BAT_DET_ON_Pin, GPIO_PIN_SET);
}

static void bat_det_power_off(void)
{
    HAL_GPIO_WritePin(PWR_BAT_DET_ON_GPIO_Port, PWR_BAT_DET_ON_Pin, GPIO_PIN_RESET);
}

static void codec_power_init(void)
{
    HAL_GPIO_WritePin(PWR_COEDC_GPIO_Port, PWR_COEDC_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_COEDC_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWR_COEDC_GPIO_Port, &GPIO_InitStruct);
}

static void codec_power_on(void)
{
    HAL_GPIO_WritePin(PWR_COEDC_GPIO_Port, PWR_COEDC_Pin, GPIO_PIN_SET);
}

static void codec_power_off(void)
{
    HAL_GPIO_WritePin(PWR_COEDC_GPIO_Port, PWR_COEDC_Pin, GPIO_PIN_RESET);
}

static void pir_power_init(void)
{
    HAL_GPIO_WritePin(PWR_PIR_ON_GPIO_Port, PWR_PIR_ON_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_PIR_ON_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWR_PIR_ON_GPIO_Port, &GPIO_InitStruct);
}

static void pir_power_on(void)
{
    HAL_GPIO_WritePin(PWR_PIR_ON_GPIO_Port, PWR_PIR_ON_Pin, GPIO_PIN_SET);
}

static void pir_power_off(void)
{
    HAL_GPIO_WritePin(PWR_PIR_ON_GPIO_Port, PWR_PIR_ON_Pin, GPIO_PIN_RESET);
}

static void usb_power_init(void)
{
    HAL_GPIO_WritePin(PWR_USB_GPIO_Port, PWR_USB_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_USB_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

static void usb_power_on(void)
{
    HAL_GPIO_WritePin(PWR_USB_GPIO_Port, PWR_USB_Pin, GPIO_PIN_SET);
}

static void usb_power_off(void)
{
    HAL_GPIO_WritePin(PWR_USB_GPIO_Port, PWR_USB_Pin, GPIO_PIN_RESET);
}

static void cat1_power_init(void)
{
    // HAL_GPIO_WritePin(PWR_CAT1_ON_GPIO_Port, PWR_CAT1_ON_Pin, GPIO_PIN_RESET);
    // GPIO_InitTypeDef GPIO_InitStruct = {0};
    // GPIO_InitStruct.Pin = PWR_CAT1_ON_Pin;
    // GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    // GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    // HAL_GPIO_Init(PWR_CAT1_ON_GPIO_Port, &GPIO_InitStruct);
}

static void cat1_power_on(void)
{
#if ENABLE_U0_MODULE
    uint32_t switch_bits = 0;
    if (u0_module_get_power_status(&switch_bits) == 0) {
        switch_bits |= PWR_EXT_SWITCH_BIT;
        u0_module_power_control(switch_bits);
    }
#endif
    // HAL_GPIO_WritePin(PWR_CAT1_ON_GPIO_Port, PWR_CAT1_ON_Pin, GPIO_PIN_SET);
}

static void cat1_power_off(void)
{
#if ENABLE_U0_MODULE
    uint32_t switch_bits = 0;
    if (u0_module_get_power_status(&switch_bits) == 0) {
        switch_bits &= ~PWR_EXT_SWITCH_BIT;
        u0_module_power_control(switch_bits);
    }
#endif
    // HAL_GPIO_WritePin(PWR_CAT1_ON_GPIO_Port, PWR_CAT1_ON_Pin, GPIO_PIN_RESET);
}

static void tf_power_init(void)
{
    HAL_GPIO_WritePin(PWR_TF_ON_GPIO_Port, PWR_TF_ON_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_TF_ON_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWR_TF_ON_GPIO_Port, &GPIO_InitStruct);
}

static void tf_power_on(void)
{
    HAL_GPIO_WritePin(PWR_TF_ON_GPIO_Port, PWR_TF_ON_Pin, GPIO_PIN_SET);
}

static void tf_power_off(void)
{
    HAL_GPIO_WritePin(PWR_TF_ON_GPIO_Port, PWR_TF_ON_Pin, GPIO_PIN_RESET);
}

static void wifi_power_init(void)
{
    HAL_GPIO_WritePin(PWR_WIFI_ON_GPIO_Port, PWR_WIFI_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(WIFI_POC_IN_GPIO_Port, WIFI_POC_IN_Pin, GPIO_PIN_RESET);
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = PWR_WIFI_ON_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWR_WIFI_ON_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = WIFI_POC_IN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WIFI_POC_IN_GPIO_Port, &GPIO_InitStruct);
}

static void wifi_power_on(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = WIFI_IRQ_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
     GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(WIFI_IRQ_GPIO_Port, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = WIFI_STA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(WIFI_STA_GPIO_Port, &GPIO_InitStruct);
    
    HAL_GPIO_WritePin(WIFI_ULP_WAKEUP_GPIO_Port, WIFI_ULP_WAKEUP_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WIFI_RESET_N_GPIO_Port, WIFI_RESET_N_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WIFI_POC_IN_GPIO_Port, WIFI_POC_IN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PWR_WIFI_ON_GPIO_Port, PWR_WIFI_ON_Pin, GPIO_PIN_SET);
#if ENABLE_U0_MODULE
    uint32_t switch_bits = 0;
    if (u0_module_get_power_status(&switch_bits) == 0) {
        switch_bits |= PWR_WIFI_SWITCH_BIT;
        u0_module_power_control(switch_bits);
    }
#endif
}

static void wifi_power_off(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(WIFI_POC_IN_GPIO_Port, WIFI_POC_IN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWR_WIFI_ON_GPIO_Port, PWR_WIFI_ON_Pin, GPIO_PIN_RESET);
#if ENABLE_U0_MODULE
    uint32_t switch_bits = 0;
    if (u0_module_get_power_status(&switch_bits) == 0) {
        switch_bits &= ~PWR_WIFI_SWITCH_BIT;
        u0_module_power_control(switch_bits);
    }
#endif
    HAL_GPIO_WritePin(WIFI_RESET_N_GPIO_Port, WIFI_RESET_N_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(WIFI_ULP_WAKEUP_GPIO_Port, WIFI_ULP_WAKEUP_Pin, GPIO_PIN_RESET);
    
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = WIFI_IRQ_Pin;
    HAL_GPIO_Init(WIFI_IRQ_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(WIFI_IRQ_GPIO_Port, WIFI_IRQ_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = WIFI_STA_Pin;
    HAL_GPIO_Init(WIFI_STA_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(WIFI_STA_GPIO_Port, WIFI_STA_Pin, GPIO_PIN_RESET);
}


static IOGroup iogroup_list[] = {
    {GPIOA, GPIO_PIN_0, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_3, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_4, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_5, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_7, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_10, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_11, GPIO_NOPULL},
    {GPIOA, GPIO_PIN_15, GPIO_NOPULL},

    {GPIOB, GPIO_PIN_0, GPIO_NOPULL},
    {GPIOB, GPIO_PIN_2, GPIO_NOPULL},
    {GPIOB, GPIO_PIN_3, GPIO_NOPULL},
    {GPIOB, GPIO_PIN_4, GPIO_NOPULL},
    {GPIOB, GPIO_PIN_12, GPIO_NOPULL},

    {GPIOD, GPIO_PIN_2, GPIO_NOPULL},
    {GPIOD, GPIO_PIN_6, GPIO_NOPULL},
    {GPIOD, GPIO_PIN_8, GPIO_NOPULL},
    {GPIOD, GPIO_PIN_14, GPIO_NOPULL},
    {GPIOD, GPIO_PIN_15, GPIO_NOPULL},

    {GPIOE, GPIO_PIN_0, GPIO_NOPULL},
    {GPIOE, GPIO_PIN_1, GPIO_NOPULL},
    {GPIOE, GPIO_PIN_2, GPIO_NOPULL},
    {GPIOE, GPIO_PIN_5, GPIO_NOPULL},
    {GPIOE, GPIO_PIN_6, GPIO_NOPULL},

    {GPIOF, GPIO_PIN_2, GPIO_NOPULL},
    {GPIOF, GPIO_PIN_4, GPIO_NOPULL},

    {GPIOG, GPIO_PIN_1, GPIO_NOPULL},
    {GPIOG, GPIO_PIN_2, GPIO_NOPULL},
    {GPIOG, GPIO_PIN_8, GPIO_NOPULL},
    {GPIOG, GPIO_PIN_11, GPIO_NOPULL},
    {GPIOG, GPIO_PIN_12, GPIO_NOPULL},
};

static void __attribute__((unused)) iogroup_power_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    for(int i = 0; i < sizeof(iogroup_list) / sizeof(iogroup_list[0]); i++) {
        GPIO_InitStruct.Pin = iogroup_list[i].pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = iogroup_list[i].pull;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(iogroup_list[i].GPIOx, &GPIO_InitStruct);
    }
}

static void __attribute__((unused)) iogroup_power_on(void)
{
    for(int i = 0; i < sizeof(iogroup_list) / sizeof(iogroup_list[0]); i++) {
        HAL_GPIO_WritePin(iogroup_list[i].GPIOx, iogroup_list[i].pin, GPIO_PIN_SET);
    }
}

static void __attribute__((unused)) iogroup_power_off(void)
{
    for(int i = 0; i < sizeof(iogroup_list) / sizeof(iogroup_list[0]); i++) {
        HAL_GPIO_WritePin(iogroup_list[i].GPIOx, iogroup_list[i].pin, GPIO_PIN_RESET);
    }
}

power_desc pwr_descs[] = {
    {PWR_SENSOR_NAME, sensor_power_init, sensor_power_on, sensor_power_off},
    {PWR_BAT_DET_NAME, bat_det_power_init, bat_det_power_on, bat_det_power_off},
    {PWR_CODEC_NAME, codec_power_init, codec_power_on, codec_power_off},
    {PWR_PIR_NAME, pir_power_init, pir_power_on, pir_power_off},
    {PWR_USB_NAME, usb_power_init, usb_power_on, usb_power_off},
    {PWR_CAT1_NAME, cat1_power_init, cat1_power_on, cat1_power_off},
    {PWR_TF_NAME, tf_power_init, tf_power_on, tf_power_off},
    {PWR_WIFI, wifi_power_init, wifi_power_on, wifi_power_off},
    // {PWR_IOGROUP, iogroup_power_init, iogroup_power_on, iogroup_power_off},
};


static void pwrProcess(void *argument)
{
    pwr_t *pwr = (pwr_t *)argument;
    LOG_DRV_DEBUG("pwrProcess start\r\n");

    for(;;){
        if(pwr->is_init){

        }
        osDelay(1000);
    }
}


static int pwr_init(void *priv)
{
    LOG_DRV_DEBUG("pwr_init \r\n");
    pwr_t *pwr = (pwr_t *)priv;

    pwr->mtx_id = osMutexNew(NULL);
    pwr->sem_id = osSemaphoreNew(1, 0, NULL);
    pwr->pwr_processId = osThreadNew(pwrProcess, pwr, &pwrTask_attributes);

    pwr->pwr_mgr = power_manager_create();

#ifdef STM32N6_DK_BOARD
    /* Nucleo DK: pinout differs from product; skip all rail GPIO init/register. */
    pwr->is_init = true;
    LOG_DRV_DEBUG("pwr_init end (DK: no power IO)\r\n");
    return 0;
#else
    const int n = sizeof(pwr_descs) / sizeof(pwr_descs[0]);
    PowerHandle handles[n];

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    for (int i = 0; i < n; ++i) {
        pwr_descs[i].power_init();
        handles[i] = power_manager_register(pwr->pwr_mgr, pwr_descs[i].name, pwr_descs[i].power_on, pwr_descs[i].power_off);
        LOG_DRV_DEBUG("Registered power %s, handle=%d \r\n", pwr_descs[i].name, handles[i]);
    }

    pwr->is_init = true;
    LOG_DRV_DEBUG("pwr_init end\r\n");
    return 0;
#endif
}

PowerHandle pwr_manager_get_handle(const char *name)
{
    return power_manager_get_handle(g_pwr.pwr_mgr, name);
}

int pwr_manager_acquire(PowerHandle handle)
{
#ifdef STM32N6_DK_BOARD
    (void)handle;
    return 0;
#else
    return power_manager_acquire_by_handle(g_pwr.pwr_mgr, handle);
#endif
}

int pwr_manager_release(PowerHandle handle)
{
#ifdef STM32N6_DK_BOARD
    (void)handle;
    return 0;
#else
    return power_manager_release_by_handle(g_pwr.pwr_mgr, handle);
#endif
}

static void power_manager_print_all_states(PowerManager *manager)
{
    if (!manager) return;
    osMutexAcquire(manager->lock, osWaitForever);
    LOG_SIMPLE("---- PowerManager States ----");
    for (size_t i = 0; i < manager->count; i++) {
        PowerState *ps = manager->powers[i];
        // Lock each PowerState separately for protection
        osMutexAcquire(ps->lock, osWaitForever);

        // Safely copy name using local array
        char name_buf[POWER_NAME_MAX_LEN + 1] = {0}; // Extra 1 byte for \0
        strncpy(name_buf, ps->name, POWER_NAME_MAX_LEN);
        name_buf[POWER_NAME_MAX_LEN] = '\0'; // Ensure termination

        LOG_SIMPLE("Power[%d]: name=%s, is_on=%d, ref_count=%d, handle=%u",
               i, name_buf, ps->is_on, ps->ref_count, ps->handle);
        osMutexRelease(ps->lock);
    }
    LOG_SIMPLE("----------------------------\r\n");
    osMutexRelease(manager->lock);
}

static int pwr_cmd(int argc, char* argv[]) 
{
    if (argc < 2) {
        LOG_SIMPLE("Usage:");
        LOG_SIMPLE("  pwr all");
        LOG_SIMPLE("  pwr <name> on");
        LOG_SIMPLE("  pwr <name> off");
        return -1;
    }

    if (strcmp(argv[1], "all") == 0) {
        power_manager_print_all_states(g_pwr.pwr_mgr);
        return 0;
    }

    // Control module power switch
    if (argc >= 3) {
        const char *name = argv[1];
        const char *action = argv[2];
        PowerHandle handle = pwr_manager_get_handle(name);

        if (handle <= 0) {
            LOG_SIMPLE("Power module '%s' not found", name);
            return -2;
        }

        if (strcmp(action, "on") == 0) {
            int ret = pwr_manager_acquire(handle);
            LOG_SIMPLE("Power[%s] ON: %s", name, ret == 0 ? "OK" : "FAIL");
            return ret;
        } else if (strcmp(action, "off") == 0) {
            int ret = pwr_manager_release(handle);
            LOG_SIMPLE("Power[%s] OFF: %s", name, ret == 0 ? "OK" : "FAIL");
            return ret;
        } else {
            LOG_SIMPLE("Unknown action '%s', use 'on' or 'off'", action);
            return -3;
        }
    }
    return -1;
}

debug_cmd_reg_t pwr_cmd_table[] = {
    {"pwr",      "power state",      pwr_cmd},
};

// 
static void pwr_cmd_register(void)
{
    debug_cmdline_register(pwr_cmd_table, sizeof(pwr_cmd_table) / sizeof(pwr_cmd_table[0]));
}

void pwr_register(void)
{
    static dev_ops_t pwr_ops = {
        .init = pwr_init
    };
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_pwr.dev = dev;
    strcpy(dev->name, PWR_DEVICE_NAME);
    dev->type = DEV_TYPE_MISC;
    dev->ops = &pwr_ops;
    dev->priv_data = &g_pwr;

    device_register(g_pwr.dev);
    
    
    driver_cmd_register_callback(PWR_DEVICE_NAME, pwr_cmd_register);
}
