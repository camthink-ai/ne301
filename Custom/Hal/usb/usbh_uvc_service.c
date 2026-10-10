/**
 * @file    usbh_uvc_service.c
 * @brief   UVC camera service on top of the usbh_uvc_test engine.
 *
 * Lifecycle:
 *  - init() reads camera_config; when source == uvc it powers the USB host
 *    and spawns a supervisor thread that opens/starts the MJPEG stream at
 *    the configured resolution (auto = largest offered) and re-opens after
 *    unplug/replug. In-session transport faults are healed by the engine's
 *    own watchdog (auto-reconnect), not by this thread.
 *  - probe() powers the host for a native-active system, waits for
 *    enumeration and returns the MJPEG config list without opening a
 *    stream; probe_release() powers the host back down unless the 4G NIC
 *    owns it.
 *  - capture()/latest_frame() read intact frames out of the engine's
 *    published-frame store (always-on stream -> zero warm-up latency).
 */
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"
#include "usbh_uvc_service.h"
#include "usbh_uvc_test.h"
#include "usb_cherry.h"
#include "json_config_mgr.h"
#include "netif_manager.h"

#ifndef IN_PSRAM
#define IN_PSRAM __attribute__((section(".psram_bss")))
#endif
#ifndef ALIGN_32
#define ALIGN_32 __attribute__((aligned(32)))
#endif

static struct {
    volatile int active;      /* camera_source == uvc this boot */
    volatile int streaming;
    uint16_t cfg_w, cfg_h;    /* configured selection (0/0 = auto/largest) */
    uint16_t run_w, run_h;    /* resolution actually opened */
    volatile int run;
    osThreadId_t thread;
    volatile int probe_inited_host; /* probe() powered the host: release it */
} g_uvcsvc;

static uint8_t g_uvcsvc_stack[8192] ALIGN_32 IN_PSRAM;

/* Match the configured resolution against the enumerated MJPEG list; fall
 * back to the largest offered. Returns 0 with w/h set. */
static int uvcsvc_resolve_open_wh(uint16_t *w, uint16_t *h)
{
    struct usbh_uvc_stream_cfg cfg[UVC_SVC_MAX_CONFIGS];
    int n = usbh_uvc_test_enumerate(cfg, UVC_SVC_MAX_CONFIGS);
    if (n <= 0) {
        return -1;
    }

    if (g_uvcsvc.cfg_w != 0 && g_uvcsvc.cfg_h != 0) {
        for (int i = 0; i < n; i++) {
            if (cfg[i].width == g_uvcsvc.cfg_w && cfg[i].height == g_uvcsvc.cfg_h) {
                *w = cfg[i].width;
                *h = cfg[i].height;
                return 0;
            }
        }
        printf("[UVCsvc] configured %ux%u not offered, falling back to largest\r\n",
               g_uvcsvc.cfg_w, g_uvcsvc.cfg_h);
    }

    int best = 0;
    for (int i = 1; i < n; i++) {
        if ((uint32_t)cfg[i].width * cfg[i].height > (uint32_t)cfg[best].width * cfg[best].height) {
            best = i;
        }
    }
    *w = cfg[best].width;
    *h = cfg[best].height;
    return 0;
}

static void uvc_service_thread(void *arg)
{
    (void)arg;

    while (g_uvcsvc.run) {
        /* The 4G NIC shares this host and tears it down on its own init
         * failure; recover by powering it back up after a settle delay. */
        if (!usb_cherry_host_is_inited()) {
            (void)usb_cherry_host_init(NULL);
            osDelay(2000);
            continue;
        }
        if (!usbh_uvc_test_dev_ready()) {
            osDelay(250);
            continue;
        }

        uint16_t w = 0, h = 0;
        if (uvcsvc_resolve_open_wh(&w, &h) != 0) {
            osDelay(1000);
            continue;
        }
        if (usbh_uvc_test_open(w, h, 0xff) != 0) {
            osDelay(1000);
            continue;
        }
        if (usbh_uvc_test_start() != 0) {
            (void)usbh_uvc_test_close();
            osDelay(1000);
            continue;
        }

        g_uvcsvc.run_w = w;
        g_uvcsvc.run_h = h;
        g_uvcsvc.streaming = 1;
        printf("[UVCsvc] streaming %ux%u\r\n", w, h);

        /* In-session faults are healed by the engine watchdog (incl. the
         * bulk dead-pipe rebuild); this loop handles device loss (unplug /
         * port reset re-enumeration) and a worker rebuild that failed to
         * reopen - in both cases the engine leaves streaming and the only
         * way forward is a fresh open. */
        while (g_uvcsvc.run && usbh_uvc_test_dev_ready()) {
            struct usbh_uvc_state est;
            usbh_uvc_test_get_state(&est);
            if (!est.streaming) {
                break;
            }
            osDelay(500);
        }

        g_uvcsvc.streaming = 0;
        (void)usbh_uvc_test_stop();
        (void)usbh_uvc_test_close();
        printf("[UVCsvc] session down, waiting for device\r\n");
    }
}

int usbh_uvc_service_init(void)
{
    camera_source_config_t cc;

    if (json_config_get_camera_config(&cc) != AICAM_OK) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    g_uvcsvc.active = (cc.source == CAMERA_SOURCE_UVC);
    if (!g_uvcsvc.active) {
        return AICAM_OK;
    }

    g_uvcsvc.cfg_w = (uint16_t)cc.uvc_stream_width;
    g_uvcsvc.cfg_h = (uint16_t)cc.uvc_stream_height;

    if (!usb_cherry_host_is_inited()) {
        if (usb_cherry_host_init(NULL) != 0) {
            return AICAM_ERROR;
        }
    }

    g_uvcsvc.run = 1;
    static const osThreadAttr_t attr = {
        .name = "uvc_svc",
        .stack_mem = g_uvcsvc_stack,
        .stack_size = sizeof(g_uvcsvc_stack),
        .priority = osPriorityHigh,
    };
    g_uvcsvc.thread = osThreadNew(uvc_service_thread, NULL, &attr);
    if (g_uvcsvc.thread == NULL) {
        g_uvcsvc.run = 0;
        return AICAM_ERROR_NO_MEMORY;
    }
    return AICAM_OK;
}

int usbh_uvc_service_is_active(void)
{
    return g_uvcsvc.active;
}

int usbh_uvc_service_source_selected(void)
{
    camera_source_config_t cc;

    /* Config-direct (json_config is up before any service): callers that
     * run before device_service_init cannot rely on the service state. */
    if (json_config_get_camera_config(&cc) != AICAM_OK) {
        return 0;
    }
    return (cc.source == CAMERA_SOURCE_UVC) ? 1 : 0;
}

int usbh_uvc_service_probe(uvc_svc_probe_t *out, uint32_t wait_ms)
{
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    if (!usb_cherry_host_is_inited()) {
        if (usb_cherry_host_init(NULL) != 0) {
            return -1;
        }
        g_uvcsvc.probe_inited_host = 1;
    }

    uint32_t waited = 0;
    while (!usbh_uvc_test_dev_ready() && waited < wait_ms) {
        osDelay(100);
        waited += 100;
    }
    if (!usbh_uvc_test_dev_ready()) {
        return -1;
    }

    struct usbh_uvc_devinfo di;
    if (usbh_uvc_test_get_devinfo(&di) == 0) {
        out->vid = di.vid;
        out->pid = di.pid;
        out->bulk = (uint8_t)(di.is_bulk ? 1 : 0);
        memcpy(out->product, di.product, sizeof(out->product));
    }

    struct usbh_uvc_stream_cfg cfg[UVC_SVC_MAX_CONFIGS];
    int n = usbh_uvc_test_enumerate(cfg, UVC_SVC_MAX_CONFIGS);
    for (int i = 0; i < n && i < UVC_SVC_MAX_CONFIGS; i++) {
        out->cfg[out->cfg_count].width = cfg[i].width;
        out->cfg[out->cfg_count].height = cfg[i].height;
        out->cfg[out->cfg_count].fps = cfg[i].interval_100ns
            ? (uint8_t)(10000000u / cfg[i].interval_100ns) : 0;
        out->cfg[out->cfg_count].bulk = out->bulk;
        out->cfg_count++;
    }
    return 0;
}

void usbh_uvc_service_probe_release(void)
{
    if (!g_uvcsvc.probe_inited_host) {
        return; /* host was already up (4G NIC or uvc session owns it) */
    }
    g_uvcsvc.probe_inited_host = 0;

    /* Full host power-down only when the 4G RNDIS NIC isn't using it. When
     * the NIC owns the host stack, keep it enumerated: a non-streaming UVC
     * device generates no periodic traffic. */
    if (nm_get_netif_state(NETIF_NAME_USB_RNDIS) == NETIF_STATE_DEINIT) {
        usb_cherry_host_deinit();
    }
}

int usbh_uvc_service_capture(uint8_t *dst, uint32_t dst_size, uint32_t skip,
                             uint32_t timeout_ms, uint32_t *out_len,
                             uint16_t *w, uint16_t *h)
{
    if (dst == NULL || out_len == NULL) {
        return -1;
    }
    if (!g_uvcsvc.streaming) {
        return -1;
    }

    const uint8_t *p;
    uint32_t len;
    uint32_t last_seq = usbh_uvc_service_latest_frame(&p, &len, w, h);
    uint32_t target = last_seq + skip + 1;
    uint32_t t0 = osKernelGetTickCount();

    for (;;) {
        if (!g_uvcsvc.streaming) {
            return -1;
        }
        uint32_t seq = usbh_uvc_service_latest_frame(&p, &len, w, h);
        if (seq >= target && len > 0) {
            if (len > dst_size) {
                return -2;
            }
            memcpy(dst, p, len);
            *out_len = len;
            return 0;
        }
        if ((osKernelGetTickCount() - t0) >= timeout_ms) {
            return -1;
        }
        osDelay(10);
    }
}

uint32_t usbh_uvc_service_latest_frame(const uint8_t **buf, uint32_t *len,
                                       uint16_t *w, uint16_t *h)
{
    return usbh_uvc_preview_get(buf, len, w, h);
}

void usbh_uvc_service_get_status(uvc_svc_status_t *st)
{
    struct usbh_uvc_state es;

    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->active = g_uvcsvc.active;

    usbh_uvc_test_get_state(&es);
    st->dev_ready = es.dev_ready;
    st->is_bulk = es.is_bulk;
    st->frames = es.frames;
    st->frames_bad = es.frames_bad;
    st->reconnects = es.reconnects;

    if (g_uvcsvc.streaming) {
        st->streaming = 1;
        st->width = g_uvcsvc.run_w;
        st->height = g_uvcsvc.run_h;
        st->fps = es.fps;
        st->fps_measured = es.fps_measured;
    }
}
