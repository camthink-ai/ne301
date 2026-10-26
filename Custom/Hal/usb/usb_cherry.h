/**
 * @file    usb_cherry.h
 * @brief   CherryUSB host bring-up seam for NE301 (USB2_OTG_HS).
 *
 * Replaces the former usb_host_ecm wrapper as the single app-side entry to
 * the USB host stack. Class-level consumers (CDC-ECM / RNDIS netif, UVC,
 * ...) are re-added on top of this once the port is verified in the field.
 */
#ifndef USB_CHERRY_H
#define USB_CHERRY_H

#include <stdint.h>
#include "usbh_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time CherryUSB osal bootstrap (byte pool + reaper thread).
 * Shared by the host stack and any device class; safe to call repeatedly.
 * @return 0 on success
 */
int usb_cherry_osal_ensure(void);

/**
 * @brief Power the USB rail and start the CherryUSB host stack on USB2.
 * @param event_cb optional bus/device event callback (may be NULL; events
 *                 are logged either way)
 * @return 0 on success
 */
int usb_cherry_host_init(usbh_event_handler_t event_cb);

/**
 * @brief Stop the host stack (real DWC2 deinit: GINT off, FIFOs flushed,
 *        channels halted) and release the USB rail.
 * @return 0 on success
 */
int usb_cherry_host_deinit(void);

/** @brief Non-zero while the host stack is running. */
int usb_cherry_host_is_inited(void);

/* Composite device (RNDIS NIC + CDC-ACM console) on USB1_OTG_HS (J21
 * Type-C) — Custom/Hal/usb/usb_device_composite.c */
int usb_cherry_device_init(void);
int usb_cherry_device_deinit(void);
int usb_cherry_device_is_inited(void);
/** One-line runtime diagnostics (rx/tx counters, netif ip) for the CLI. */
void usb_cherry_device_diag(char *buf, int buflen);

struct netif;
/** netif_manager ctrl for the PC-facing "ud" netif (INFO/STATE/INIT/UNINIT). */
int usb_dev_netif_ctrl(const char *if_name, int cmd, void *param);

#ifdef __cplusplus
}
#endif

#endif /* USB_CHERRY_H */
