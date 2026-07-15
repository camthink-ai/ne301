#ifndef MISC_H
#define MISC_H

//#include "FreeRTOS.h"
//#include "task.h"
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "aicam_error.h"

#define FLASH_DUTY 50

#define BATTERY_MIN_VOLTAGE (3600)  /* Minimum battery voltage in mV */
#define BATTERY_MAX_VOLTAGE (6000)  /* Maximum battery voltage in mV */

#define LIGHT_MIN_SENS      (0)    /* Minimum light sensor value */
#define LIGHT_MAX_SENS      (2500) /* Maximum light sensor value */

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#define MAX_GPIO_GROUPS 4
#define MAX_GPIO_NAME_LEN 16


#define IO_ALARM_NAME "ALARM"


typedef void (*misc_button_cb)(void);
typedef void (*io_interrupt_cb_t)(void);
/*
 * Miscellaneous types
*/
typedef enum {
    MISC_TYPE_LED         =  0,
    MISC_TYPE_BUTTON,
    MISC_TYPE_PWM,
    MISC_TYPE_ADC,
    MISC_TYPE_IO,
    MISC_TYPE_MAX,
} MISC_TYPE_E;


/*
 * Miscellaneous commands
*/
typedef enum {
    MISC_CMD_LED_ON             = MISC_CMD_BASE,
    MISC_CMD_LED_OFF,
    MISC_CMD_LED_SET_BLINK,

    MISC_CMD_BUTTON_SET_SP_CB   = MISC_CMD_BASE + 0x10,// Button  short press callback
    MISC_CMD_BUTTON_SET_DC_CB,  // Button double click callback
    MISC_CMD_BUTTON_SET_LP_CB,  // Button long press callback
    MISC_CMD_BUTTON_SET_SLP_CB, // Button super long press callback
    MISC_CMD_BUTTON_GET_PARAMS, // Get button parameters
    MISC_CMD_BUTTON_SET_PARAMS, // Set button parameters

    MISC_CMD_PWM_ON = MISC_CMD_BASE + 0x20,            // PWM on 
    MISC_CMD_PWM_OFF,           // PWM off
    MISC_CMD_PWM_SET_DUTY,      // PWM set duty percent (0-100)
    MISC_CMD_PWM_SET_BLINK,     // PWM set blink
    MISC_CMD_PWM_GET_DUTY,      // PWM get duty percent (0-100)

    MISC_CMD_ADC_GET_PERCENT = MISC_CMD_BASE + 0x30,       // Get ADC percent
    MISC_CMD_USB_GET_STATUS, // Get USB status

    MISC_CMD_IO_GET_GROUP_INFO = MISC_CMD_BASE + 0x40,
    MISC_CMD_IO_SET_MODE,
    MISC_CMD_IO_SET_OUTPUT,
    MISC_CMD_IO_SET_INT_CB,   
} MISC_CMD_E;


typedef enum {
    IO_MODE_OUTPUT = 0,
    IO_MODE_INTERRUPT
} io_mode_t;

typedef enum {
    IO_INT_RISING_EDGE = 0,
    IO_INT_FALLING_EDGE,
    IO_INT_MAX
} io_int_type_t;

typedef enum {
    IO_OUTPUT_LOW = 0,
    IO_OUTPUT_HIGH
} io_output_state_t;

/*
 * Button configuration structure
 */
typedef struct {
    uint32_t debounce_time;         // Debounce time
    uint32_t double_click_time;     // Double-click time
    uint32_t long_press_time;       // Long press time
    uint32_t super_long_press_time; // Super long press time
} button_params_t;

typedef struct {
    int blink_times;  // blink times
    int interval_ms;  // interval ms
} blink_params_t;

typedef struct {
    uint8_t duty;
} pwm_cfg_t;


typedef struct {
    char name[MAX_GPIO_NAME_LEN];
    uint16_t pin;
    void *port;
    io_mode_t mode;
    io_int_type_t int_type;
    io_interrupt_cb_t int_cb;
    io_output_state_t output_state;
} gpio_group_t;

typedef struct {
    uint8_t group_num;
    gpio_group_t *groups;
} io_dev_cfg_t;


typedef struct {
    char name[MAX_GPIO_NAME_LEN];
    io_mode_t mode;
    io_int_type_t int_type;
    io_output_state_t output_state;
    io_interrupt_cb_t int_cb;
} io_group_cfg_t;

typedef struct {
    uint8_t group_num;
    io_group_cfg_t groups[MAX_GPIO_GROUPS];
} io_group_info_t;

typedef struct {
    uint32_t voltage;
    uint8_t rate;
    uint8_t usb_status;
    char power_supply_type[16];
} battery_data_t;

/*
 * Miscellaneous structure
*/
typedef struct {
    bool is_init;
    bool state;
    device_t *dev;
    MISC_TYPE_E type;
    osMutexId_t mtx_id;
    uint32_t handle;        // handle
    PowerHandle pwr_handle; // power handle
    void *config;
    void *data;
} misc_t;

int misc_register(void);
int misc_unregister(void);

#endif
