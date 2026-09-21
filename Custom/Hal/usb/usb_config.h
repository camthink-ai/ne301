/**
 * @file    usb_config.h
 * @brief   CherryUSB stack configuration for NE301 (STM32N657).
 *
 * Host bus 0 -> USB2_OTG_HS (DWC2, internal HS PHY, DMA)  [4G modem uplink]
 * Device bus 0 -> USB1_OTG_HS (dormant skeleton; CDC-ACM / CDC-ECM / RNDIS /
 *                UVC device classes are to be added back after the host path
 *                is verified in the field).
 *
 * The vendored library lives in Middlewares/CherryUSB (upstream commit
 * 4885555, 2026-09-11); see Middlewares/CherryUSB/PORTING.md for the N6
 * glue notes. Keep this file in sync with cherryusb_config_template.h when
 * upgrading the library.
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include <stdio.h>

/* ================ USB common Configuration ================ */

#define CONFIG_USB_PRINTF(...) printf(__VA_ARGS__)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

/* Plain serial console: no ANSI colors in USB logs. */
/* #define CONFIG_USB_PRINTF_COLOR_ENABLE */

/* The App runs with the D-cache enabled; the DWC2 driver then performs
 * cache maintenance around DMA (usb_dcache_* helpers in the N6 glue). */
#define CONFIG_USB_DCACHE_ENABLE
#define CONFIG_USB_ALIGN_SIZE 32

/* DMA-reachable buffers go to the existing uncached linker section
 * (AXI_SRAM_UNCACHED, ".uncached_bss" NOLOAD in the Appli linker scripts). */
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".uncached_bss")))

/* Use default memcpy path (arm libc workaround for odd sizes). */
/* #define CONFIG_USB_MEMCPY_DISABLE */

/* ================= USB Device Stack Configuration ================ */

/* USB1 runs on the internal HS PHY: 512-byte bulk MPS + device qualifier
 * support (see usbd_core.c). */
#define CONFIG_USB_HS

#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

#ifndef CONFIG_USBDEV_MAX_BUS
#define CONFIG_USBDEV_MAX_BUS 1
#endif

#ifndef CONFIG_USBDEV_EP0_PRIO
#define CONFIG_USBDEV_EP0_PRIO 12
#endif

#ifndef CONFIG_USBDEV_EP0_STACKSIZE
#define CONFIG_USBDEV_EP0_STACKSIZE 2048
#endif

/* Composite device: RNDIS NIC (lwip pbuf IO) + CDC-ACM console. */
#define CONFIG_USBDEV_RNDIS_USING_LWIP

#ifndef CONFIG_USBDEV_RNDIS_RESP_BUFFER_SIZE
#define CONFIG_USBDEV_RNDIS_RESP_BUFFER_SIZE 156
#endif

#ifndef CONFIG_USBDEV_RNDIS_ETH_MAX_FRAME_SIZE
#define CONFIG_USBDEV_RNDIS_ETH_MAX_FRAME_SIZE 1580
#endif

#ifndef CONFIG_USBDEV_RNDIS_VENDOR_ID
#define CONFIG_USBDEV_RNDIS_VENDOR_ID 0x0000ffff
#endif

#ifndef CONFIG_USBDEV_RNDIS_VENDOR_DESC
#define CONFIG_USBDEV_RNDIS_VENDOR_DESC "NE301"
#endif

/* ================= USB HOST Stack Configuration ================== */

#ifndef CONFIG_USBHOST_MAX_BUS
#define CONFIG_USBHOST_MAX_BUS 1
#endif

#define CONFIG_USBHOST_MAX_RHPORTS          1
#define CONFIG_USBHOST_MAX_EXTHUBS          1
#define CONFIG_USBHOST_MAX_EHPORTS          4
#define CONFIG_USBHOST_MAX_INTERFACES       8
/* UVC streaming interfaces expose up to ~9 altsettings (bandwidth ladder,
 * measured camera: bAlternateSetting 0..8); overflow aborts enumeration. */
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 16
/* CDC-ECM/RNDIS need 3 endpoints; keep headroom for UVC later. */
#define CONFIG_USBHOST_MAX_ENDPOINTS        8

/* Host UVC (class/video/usbh_video.c) */
#define CONFIG_USBHOST_MAX_VIDEO_CLASS      1
/* Generous: usbh_video.c USB_ASSERTs (hard hang) when a camera reports more
 * formats/frames than configured. */
#define CONFIG_USBHOST_VIDEO_MAX_FORMATS    8
#define CONFIG_USBHOST_VIDEO_MAX_FRAMES     24

#define CONFIG_USBHOST_DEV_NAMELEN 16

/* ThreadX numeric priorities (0 = highest). Mid range, below time-critical
 * app tasks. CONSTRAINT: every CherryUSB thread priority must be numerically
 * BELOW the osal reaper (patched to TX_MAX_PRIORITIES-1 = 31) — if a
 * self-deleting thread were preempted by the reaper, the reaper would free
 * its TCB before termination and corrupt the osal byte pool. Stacks come
 * from the shared usb byte pool. */
#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO 12
#endif
#ifndef CONFIG_USBHOST_PSC_STACKSIZE
#define CONFIG_USBHOST_PSC_STACKSIZE 2048
#endif

/* Ep0 max transfer buffer. MUST exceed any attached device's config
 * descriptor: UVC cameras report 700B..2KB (all altsettings + format
 * descriptors); enumeration aborts on overflow. */
#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN 2048
#endif

#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 5000
#endif

/* #define CONFIG_USBHOST_GET_STRING_DESC */

/* Ethernet frame buffers for host CDC-ECM / RNDIS (must cover TCP_WND). */
#ifndef CONFIG_USBHOST_CDC_ECM_ETH_MAX_RX_SIZE
#define CONFIG_USBHOST_CDC_ECM_ETH_MAX_RX_SIZE (2048)
#endif
#ifndef CONFIG_USBHOST_CDC_ECM_ETH_MAX_TX_SIZE
#define CONFIG_USBHOST_CDC_ECM_ETH_MAX_TX_SIZE (2048)
#endif
#ifndef CONFIG_USBHOST_RNDIS_ETH_MAX_RX_SIZE
#define CONFIG_USBHOST_RNDIS_ETH_MAX_RX_SIZE (2048)
#endif
#ifndef CONFIG_USBHOST_RNDIS_ETH_MAX_TX_SIZE
#define CONFIG_USBHOST_RNDIS_ETH_MAX_TX_SIZE (2048)
#endif

#endif /* CHERRYUSB_CONFIG_H */
