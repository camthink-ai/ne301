#ifndef _PWR_H_
#define _PWR_H_

//#include "FreeRTOS.h"
//#include "task.h"
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "power_manager.h"


#define PWR_SENSOR_NAME         "sensor"
#define PWR_BAT_DET_NAME        "bat_det"
#define PWR_CODEC_NAME          "codec"
#define PWR_PIR_NAME            "pir"
#define PWR_USB_NAME            "usb"
#define PWR_CAT1_NAME           "cat1"
/** HaLow (Morse mmx108) shares Cat1 external supply: PWR_CAT1_ON + PWR_EXT_SWITCH_BIT */
#define PWR_HALOW_NAME          PWR_CAT1_NAME
#define PWR_TF_NAME             "tf"
#define PWR_WIFI                "wifi"
#define PWR_IOGROUP             "iogroup"

typedef struct {
    const char* name;
    void (*power_init)(void);
    void (*power_on)(void);
    void (*power_off)(void);
} power_desc;

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osThreadId_t pwr_processId;
    PowerManager *pwr_mgr;
} pwr_t;


void pwr_standby_mode_detect(void);

void pwr_enter_standby_mode(void);

void pwr_stop_mode(void);

void pwr_sleep_mode(void);

PowerHandle pwr_manager_get_handle(const char *name);

int pwr_manager_acquire(PowerHandle handle);

int pwr_manager_release(PowerHandle handle);

void pwr_register(void);


#endif