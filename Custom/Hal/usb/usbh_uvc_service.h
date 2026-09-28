/**
 * @file    usbh_uvc_service.h
 * @brief   UVC camera service: config-driven MJPEG session lifecycle, probe
 *          and frame access on top of the usbh_uvc_test engine.
 *
 * Active only when camera_config.source == CAMERA_SOURCE_UVC (reboot-apply).
 * The service keeps the stream always-on (the engine watchdog rebuilds the
 * session after transport wedges / port glitches), so consumers (web preview,
 * capture, AI) just read the latest published frame.
 */
#ifndef USBH_UVC_SERVICE_H
#define USBH_UVC_SERVICE_H

#include <stdint.h>
#include <stddef.h>

#define UVC_SVC_MAX_CONFIGS 16

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t fps;      /* 0 = unknown */
    uint8_t bulk;     /* bulk transport (vs iso) */
} uvc_svc_stream_cfg_t;

typedef struct {
    uint16_t vid, pid;
    uint8_t bulk;
    char product[32];
    uint32_t cfg_count;
    uvc_svc_stream_cfg_t cfg[UVC_SVC_MAX_CONFIGS];
} uvc_svc_probe_t;

typedef struct {
    int active;       /* camera_source == uvc for this boot */
    int dev_ready;
    int streaming;
    int is_bulk;
    uint16_t width, height;
    uint32_t fps;             /* negotiated */
    uint32_t fps_measured;    /* assembled intact frames in the last 1s */
    uint32_t frames, frames_bad;
    uint32_t reconnects;
} uvc_svc_status_t;

/* Boot entry (call after json_config is up). No-op when source != uvc. */
int usbh_uvc_service_init(void);
int usbh_uvc_service_is_active(void);

/* Config-direct source check: usable before device_service_init ran (the
 * service state isn't up yet). 1 = camera_config.source == uvc. */
int usbh_uvc_service_source_selected(void);

/* Probe the attached camera WITHOUT starting a stream: powers the USB host
 * if needed, waits up to wait_ms for enumeration, returns the MJPEG config
 * list. 0 ok, -1 no camera. Pair with usbh_uvc_service_probe_release(). */
int usbh_uvc_service_probe(uvc_svc_probe_t *out, uint32_t wait_ms);

/* Recycle the USB host after a probe when the system runs on the native
 * camera: full host deinit only when the 4G NIC ("ue") doesn't own it. */
void usbh_uvc_service_probe_release(void);

/* Wait for the NEXT intact MJPEG frame (optionally skipping `skip` frames
 * for AE settle) and copy it into dst. 0 ok, -1 timeout / not streaming,
 * -2 dst too small. */
int usbh_uvc_service_capture(uint8_t *dst, uint32_t dst_size, uint32_t skip,
                             uint32_t timeout_ms, uint32_t *out_len,
                             uint16_t *w, uint16_t *h);

/* Zero-copy read of the latest published intact frame (valid for ~3 publish
 * periods; copy out before doing anything slow). Returns seq (0 = none). */
uint32_t usbh_uvc_service_latest_frame(const uint8_t **buf, uint32_t *len,
                                       uint16_t *w, uint16_t *h);

void usbh_uvc_service_get_status(uvc_svc_status_t *st);

#endif /* USBH_UVC_SERVICE_H */
