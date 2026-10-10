/**
 * @file    usbh_uvc_test.h
 * @brief   Host UVC (USB camera) streaming test module for NE301.
 *
 * Flow (all via CLI "usb host uvc <cmd>"):
 *   1. plug camera + "usb host init"      -> class driver registers /dev/video0
 *   2. usb host uvc info                  -> formats / resolutions / altsettings
 *   3. usb host uvc open [w h [alt]]      -> probe/commit + SET_INTERFACE
 *   4. usb host uvc start                 -> ISO IN stream (stats)
 *   5. usb host uvc stat / capture / dump -> validation
 *
 * The dwc2 host driver (NE301 patch #9) completes ISO URBs with the total
 * byte count only; per-packet boundaries are reconstructed here from the
 * MPS stride (greedy-fill assumption: every packet is full except the last
 * one of a frame, zero-length packets are byte-invisible).
 */
#ifndef USBH_UVC_TEST_H
#define USBH_UVC_TEST_H

#include <stdint.h>

int usbh_uvc_test_info(void);
/* alt = 0xff selects the highest (max bandwidth) altsetting */
int usbh_uvc_test_open(uint16_t width, uint16_t height, uint8_t alt);
int usbh_uvc_test_close(void);
int usbh_uvc_test_start(void);
int usbh_uvc_test_stop(void);
int usbh_uvc_test_stat(void);
/* wait for the next intact MJPEG frame into the capture buffer */
int usbh_uvc_test_capture(uint32_t timeout_ms);
/* hexdump the capture buffer */
int usbh_uvc_test_dump(uint32_t offset, uint32_t len);
/* hexdump 3 consecutive raw (pre-parse) stream chunks - transport debugging */
int usbh_uvc_test_raw(void);
/* dump the last 8 header-desync events (slot len/pos/first bytes) */
int usbh_uvc_test_hdrdbg(void);
/* scan 512 slots for header-like patterns at 1024B packet boundaries */
int usbh_uvc_test_isodbg(void);
/* set (fps>=0, 0=uncapped) / print web preview send statistics */
int usbh_uvc_test_webfps(int fps);
/* print state machine decisions for the next `count` chunks (bulk debug) */
int usbh_uvc_test_trace(uint32_t count);
/* record `seconds` of intact MJPEG frames into an AVI/MJPG file on SD */
int usbh_uvc_test_record(uint32_t seconds, const char *filename);
/* snapshot the latest published frame for the web preview; returns the
 * sequence number (0 = no frame yet) */
uint32_t usbh_uvc_preview_get(const uint8_t **buf, uint32_t *len, uint16_t *w, uint16_t *h);
/* preview watcher refcount: the worker only publishes while watched */
void usbh_uvc_preview_acquire(void);
void usbh_uvc_preview_release(void);

/* ---- service layer (usbh_uvc_service) introspection ---- */

struct usbh_uvc_stream_cfg {
    uint16_t width;
    uint16_t height;
    uint32_t interval_100ns;   /* dwDefaultFrameInterval (0 = unknown) */
};

struct usbh_uvc_devinfo {
    uint16_t vid, pid;
    int is_bulk;               /* bulk data interface (vs iso) */
    char product[32];          /* USB product string, may be empty */
};

struct usbh_uvc_state {
    int dev_ready;             /* camera enumerated */
    int opened;
    int streaming;
    int is_bulk;
    uint16_t width, height;    /* current session resolution */
    uint32_t fps;              /* negotiated frame rate */
    uint32_t fps_measured;     /* assembled intact frames in the last 1s */
    uint32_t frames, frames_bad;
    uint32_t reconnects;       /* watchdog session rebuilds */
};

/* 1 when a UVC camera is enumerated on the host bus */
int usbh_uvc_test_dev_ready(void);
/* static device identity of the enumerated camera (0 ok, -1 none) */
int usbh_uvc_test_get_devinfo(struct usbh_uvc_devinfo *info);
/* list the MJPEG stream configurations; returns count (0..max), -1 = no dev */
int usbh_uvc_test_enumerate(struct usbh_uvc_stream_cfg *out, int max);
/* engine session state snapshot */
void usbh_uvc_test_get_state(struct usbh_uvc_state *st);

#endif /* USBH_UVC_TEST_H */
