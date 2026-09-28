/**
 * @file    usb_cherry.c
 * @brief   CherryUSB host bring-up seam for NE301 (USB2_OTG_HS).
 */
#include <string.h>
#include "usb_cherry.h"
#include "usb_osal.h"
#include "pwr.h"
#include "common_utils.h"
#include "stm32n6xx_hal.h"
#include "tx_api.h"
#include "usbh_cdc_ecm.h"
#include "usbh_rndis.h"

/* osal bootstrap (ThreadX-specific, not declared in usb_osal.h). */
extern void usb_osal_init(uint8_t *mem, uint32_t mem_size);

/* Backing store for the osal's global byte pool (see usb_osal_threadx.c). */
TX_BYTE_POOL usb_byte_pool;

/*
 * Class-lifecycle hooks: CherryUSB host classes call usbh_<class>_run/_stop
 * from their connect/disconnect paths and expect the application to wire a
 * consumer (rx thread, netif, ...). usbh_rndis_run/stop are implemented by
 * the 'ue' netif (usb_rndis_netif.c); CDC-ECM stays dormant until a consumer
 * returns, so it gets a weak no-op to keep the link clean.
 */
__WEAK void usbh_cdc_ecm_run(struct usbh_cdc_ecm *cdc_ecm_class)
{
    (void)cdc_ecm_class;
}

__WEAK void usbh_cdc_ecm_stop(struct usbh_cdc_ecm *cdc_ecm_class)
{
    (void)cdc_ecm_class;
}

/* ThreadX byte pool backing every CherryUSB allocation (threads, semaphores,
 * mutexes, queues). 32 KB in PSRAM replaces the former 96 KB cached + 20 KB
 * uncached USBX pools. Created once and never destroyed: the osal reaper
 * thread has to outlive any usbh deinit/init cycle. */
#define USB_CHERRY_OSAL_POOL_SIZE (32 * 1024)

static ALIGN_32 IN_PSRAM uint8_t usb_cherry_osal_pool[USB_CHERRY_OSAL_POOL_SIZE];
static uint8_t usb_cherry_osal_inited;
static uint8_t usb_cherry_host_inited;

static const char *usb_cherry_event_name(uint8_t event)
{
    switch (event) {
        case USBH_EVENT_DEVICE_RESET: return "reset";
        case USBH_EVENT_DEVICE_CONNECTED: return "connected";
        case USBH_EVENT_DEVICE_DISCONNECTED: return "disconnected";
        case USBH_EVENT_DEVICE_CONFIGURED: return "configured";
        case USBH_EVENT_DEVICE_WAKEUP: return "wakeup";
        case USBH_EVENT_DEVICE_SUSPEND: return "suspend";
        case USBH_EVENT_DEVICE_RESUME: return "resume";
        case USBH_EVENT_INTERFACE_UNSUPPORTED: return "intf-unsupported";
        case USBH_EVENT_INTERFACE_START: return "intf-start";
        case USBH_EVENT_INTERFACE_STOP: return "intf-stop";
        case USBH_EVENT_INIT: return "init";
        case USBH_EVENT_DEINIT: return "deinit";
        default: return "unknown";
    }
}

static void usb_cherry_default_event_handler(uint8_t busid, uint8_t hub_index,
                                             uint8_t hub_port, uint8_t intf,
                                             uint8_t event)
{
    printf("[USB] bus%u hub%u port%u intf%u: %s\r\n",
           busid, hub_index, hub_port, intf, usb_cherry_event_name(event));
}

static usbh_event_handler_t usb_cherry_dispatch_event;

/* Single trampoline: logs everything, forwards to the app callback. */
static void usb_cherry_event_handler(uint8_t busid, uint8_t hub_index,
                                     uint8_t hub_port, uint8_t intf,
                                     uint8_t event)
{
    usb_cherry_default_event_handler(busid, hub_index, hub_port, intf, event);
    if (usb_cherry_dispatch_event) {
        usb_cherry_dispatch_event(busid, hub_index, hub_port, intf, event);
    }
}

int usb_cherry_osal_ensure(void)
{
    if (!usb_cherry_osal_inited) {
        usb_osal_init(usb_cherry_osal_pool, sizeof(usb_cherry_osal_pool));
        usb_cherry_osal_inited = 1;
    }
    return 0;
}

int usb_cherry_host_init(usbh_event_handler_t event_cb)
{
    if (usb_cherry_host_inited) {
        /* A late consumer (4G NIC coming up after the uvc service powered
         * the host) adopts the event stream; the default logger stays for
         * anything it doesn't handle. */
        if (event_cb != NULL) {
            usb_cherry_dispatch_event = event_cb;
        }
        return 0;
    }

    usb_cherry_osal_ensure();

    /* Power the external USB device (PB13 rail) and let it settle,
     * mirroring the former usb_host_ecm_init() timing. */
    pwr_manager_acquire(pwr_manager_get_handle(PWR_USB_NAME));
    osDelay(100);

    usb_cherry_dispatch_event = event_cb;
    int ret = usbh_initialize(0, (uintptr_t)USB2_OTG_HS_BASE, usb_cherry_event_handler);
    if (ret != 0) {
        printf("[USB] host init failed: %d\r\n", ret);
        pwr_manager_release(pwr_manager_get_handle(PWR_USB_NAME));
        usb_cherry_dispatch_event = NULL;
        return ret;
    }
    usb_cherry_host_inited = 1;
    return 0;
}

int usb_cherry_host_deinit(void)
{
    if (!usb_cherry_host_inited) {
        return 0;
    }
    usbh_deinitialize(0);
    usb_cherry_dispatch_event = NULL;
    pwr_manager_release(pwr_manager_get_handle(PWR_USB_NAME));
    usb_cherry_host_inited = 0;
    return 0;
}

int usb_cherry_host_is_inited(void)
{
    return usb_cherry_host_inited;
}
