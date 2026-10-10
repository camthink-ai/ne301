# CherryUSB (vendored subset)

Upstream: https://github.com/cherry-embedded/CherryUSB
Pinned commit: `4885555` (2026-09-11, v1.6.1) — the commit whose DWC2 /
ThreadX-osal / class sources were audited in `USBX_CherryUSB_Migration_Feasibility.md`.

License: Apache-2.0 (see LICENSE).

## What is vendored

| Path | Purpose |
|---|---|
| `common/` | headers only (osal, log, defs) |
| `core/usbh_core.c` / `usbd_core.c` | host / device core (OTG core not vendored) |
| `osal/usb_osal_threadx.c` | ThreadX osal (requires `usb_osal_init()` + `TX_BYTE_POOL`) |
| `port/dwc2/usb_hc_dwc2.c` | DWC2 host controller driver (DMA, ISO via patch #9) |
| `port/dwc2/usb_dc_dwc2.c` | DWC2 device controller driver |
| `class/hub/` | root/external hub (mandatory for host) |
| `class/cdc/usbh_cdc_ecm.*` | host CDC-ECM (4G modem uplink) |
| `class/wireless/usbh_rndis.*` | host RNDIS (alternate 4G dongles) |
| `class/video/usbh_video.*` | host UVC (descriptor parse + probe/commit; from
  upstream master 5964438, 2026-09-20) |
| `class/cdc/usbd_cdc_acm.*` / `usbd_cdc_ecm.*` | device classes (dormant, no descriptors yet) |

Not vendored: other controllers (EHCI/MUSB/...), other classes, demos, tests.
Device-side RNDIS/UVC and host UVC are added back on demand.

## N6 integration (project side)

- Config header: `Custom/Hal/usb/usb_config.h`
- N6 glue (params, PHY/clock bring-up, IRQ vectors, dcache): `Custom/Hal/usb/usb_glue_stm32n6.c`
  — upstream `usb_glue_st.c` has no STM32N6 variant; ours uses
  `CONFIG_USB_DWC2_CUSTOM_PARAM`-style own `dwc2_get_user_params()` and the
  PHY sequence previously living in `Appli/Core/Src/usb_otg.c`.
- App seam: `Custom/Hal/usb/usb_cherry.c` (`usb_cherry_host_init/deinit` +
  `usb_cherry_osal_ensure()`: pwr rail + shared osal byte pool in PSRAM).
- Host RNDis netif ("ue", 4G uplink): `Custom/Hal/Network/netif_manager/usb_rndis_netif.c`
  (implements the `usbh_rndis_run/stop/eth_input` class hooks, DHCP client,
  modem identity/SIM/signal over the AT UART incl. RNDIS mode enforcement).
- Composite device (USB1/J21 Type-C): `Custom/Hal/usb/usb_device_composite.c`
  — RNDIS NIC (lwip netif "ud" 192.168.20.1/24 + own mini RFC2131 DHCP server
  handing .100 to the PC; the shared dhcpserver.c is an AP whitelist design
  and drops unknown MACs) + CDC-ACM console.
  Device-side MACs: chip-derived MAC is reported to the host (which adopts
  it); the netif transmits from the same MAC with the last bit flipped —
  Windows NDIS drops frames whose Ethernet source equals its own station
  address (loop detection).
  Console redirect: weak hook `usb_console_output_hook()` called from
  `Gcc/Src/console.c:_write` (printf/stdout) and `debug.c`'s direct-UART
  sinks; strong implementation in the composite. Output is a tee (UART
  always on), gated on CONFIGURED && !SUSPEND && (DTR || host sent data).
  Input injects via `debug_cmdline_input()`.
  TX architecture: queue-decoupled (dedicated `usb_dev_tx` thread at
  osPriorityAboveNormal — do NOT raise it: Realtime starved the IWDG feeder
  under sustained traffic and reset the board). Measured ~16-20 Mbps TCP.
  Lifecycle: CONFIGURED/SUSPEND/RESUME/RESET handled; unplug appears as
  SUSPEND (device IP cannot see host removal; DISCONNECTED never fires).
- CLI: `usb host <init|deinit|status|uvc ...>` / `usb device <init|deinit|status>`;
  netif: `ifconfig ue ...` (NETIF_TYPE_4G) and `ifconfig ud ...` (ETH).
- Host UVC test: `Custom/Hal/usb/usbh_uvc_test.c` — implements the
  `usbh_video_run/stop` class hooks plus ISO and bulk streaming. CLI:
  `usb host uvc <info|open w h [alt]|close|start|stop|stat|capture [ms]|dump [off [len]]|raw|trace [n]|record s [file]>`.
  ISO model (validated 640x480 / 1280x720 / 1920x1080, ~8 MB/s sustained,
  zero steady-state bad frames): the walker matches the measured camera
  format — ONE payload per microframe, header at the slot start, payload
  data (spanning raw 1024B continuation packets; this device does NOT
  repeat the payload header in continuation packets, which breaks any
  1024-stride grid parse above 1012B payloads) runs to the slot end;
  all-zero short packets after a whole 1024B packet are microframe padding
  (trimmed back to the packet boundary). the
  completion callback parses the slot DIRECTLY IN THE USB IRQ (a ~8000
  URBs/s cadence cannot depend on any thread on this RTOS — the network
  stack at ThreadX prio 3..9 starved even an osPriorityRealtime worker
  ~7 slot drops/s) and re-arms from the same IRQ, so the stream is immune
  to thread scheduling entirely (running `usb device init` mid-stream no
  longer disturbs it). Recording hands finished frames through a 2-slot
  PSRAM staging queue to the worker thread, which owns the SD file.
  Slot memory: fixed 32KB uncached block (bulk 2x16KB chunks, multiple of
  512 for the ZLP framing trick; ISO 10x3072 sub-slots).
  Bulk model (validated on two cameras — VID 0x16CB and a 4K AI cam up to
  3840x2160 @ ~10 MB/s): since the last field round BOTH transports parse
  in the completion IRQ (the bulk worker ring was still starved by the
  Realtime web task under load, producing torn frames invisible to the
  SOI/EOI check); the worker thread only paces NAK re-arms and writes
  recorded frames to SD. Frames assemble directly in a triple-buffered
  preview store (512KB slots, zero-copy publish, 100ms reader window). the stream is read in 12KB chunks (multiple of 512 so a
  payload ending exactly at the chunk edge is revealed by its ZLP); a chunk
  completing short marks a payload boundary, so an at_header/continuation
  state machine reconstructs payload framing exactly. Frame delimiting uses
  the EOF header bit ONLY — the camera toggles FID per PAYLOAD (spec
  violation; a FID-based cutter would shred frames mid-stream) and zero
  pads the last payload after EOI (trimmed at frame finish). `record`
  wraps intact frames into a minimal AVI/MJPG container on SD (streamed
  '00dc' chunks, header sizes patched on close, idx1 index; 4096-frame
  index cap ~= 136s @ 30fps). `raw`/`trace` are transport debug commands.
  Bulk NAKs (idle device) are re-armed from the worker at 1ms pace instead
  of spinning INs in the IRQ. Capture/dump hold the last intact frame.
  Web preview: intact frames are additionally published to a lock-free
  triple-buffered latest-frame store (`usbh_uvc_preview_get`); web_server.c
  serves `/uvc.jpg` (snapshot) and `/uvc.mjpg` (multipart MJPEG stream,
  MG_EV_POLL-driven, 10fps throttle + 128KB send backpressure, max 2 stream
  clients, no auth — LAN test endpoints). Frames are sent DIRECTLY from the
  store (no snapshot copy): a reader stalled >3 frame periods mid-send
  could theoretically see a torn slot — accepted trade-off (~2% CPU saved);
  see the comment at uvc_strm_poll() before changing this.
- Host = USB2_OTG_HS (busid 0), device = USB1_OTG_HS (busid 0, dormant).
- Linker: `.usbh_class_info` KEEP section + `__usbh_class_info_start__/end__`
  symbols added to both App linker scripts (class drivers self-register
  there under GCC).
- `TX_TIMER_TICKS_PER_SECOND == 1000` is required by the osal and already
  set in `Appli/Core/Inc/tx_user.h`.
- **USB2 (host) IRQ priority is 4, not 7** (see usb_glue_stm32n6.c): the
  ISO channel re-arm runs in the completion ISR and a >125us blocking by
  the GPDMA/EXTI crowd (prio 5..7, busy while Wi-Fi streams) silently
  LOSES microframes — mid-frame payload loss produces torn image bands
  that pass the SOI/EOI frame check. The stat line carries a completion
  watchdog (`cps`/`miss`) that must stay miss:0 under load. USB1 (device)
  stays at 7: RNDIS/CDC tolerate latency via flow control.

## Local patches against upstream 4885555

Keep these when rebasing the vendored tree (drop only if upstream fixed them):

1. `osal/usb_osal_threadx.c` — add `#include "usb_util.h"` (missing
   `USB_ALIGN_UP`, otherwise link error).
2. `class/hub/usbh_hub.c` — remove `usb_osal_mq_delete(bus->hub_mq)` from the
   hub thread exit path: upstream deletes the mq both in the thread and in
   `usbh_hub_deinitialize()` (double free -> ThreadX byte pool corruption ->
   every following init creates a dead hub thread and deinit hangs forever).
3. `core/usbh_core.c` — `usbh_initialize()` propagates `usbh_hub_initialize()`
   errors instead of always returning 0.
4. `osal/usb_osal_threadx.c` — reaper thread priority 10 -> `TX_MAX_PRIORITIES-1`
   (lowest). Upstream's self-delete protocol ("send TCB to reaper, THEN
   terminate self") races when the reaper can preempt the caller: it
   `tx_byte_release()`s a still-running thread and corrupts the byte pool
   (symptom: 1st init/deinit cycle works, 2nd init has a silent hub thread,
   2nd deinit hangs forever). The reaper also retries `tx_thread_delete()` and
   leaks instead of freeing an un-terminated thread. All CherryUSB thread
   priorities in usb_config.h must stay numerically below the reaper's.
5. `port/dwc2/usb_dc_dwc2.c` — `usbd_ep_start_read()` (non-ep0) stores the
   ROUNDED transfer size (pktcnt*mps, what DOEPTSIZ.XFRSIZ is programmed with,
   like ST HAL) into `xfer_len`. Upstream kept the raw requested length, so the
   XFRC formula `actual = xfer_len - XFRSIZ_residual` wrapped/wrong-sized every
   short-completed OUT transfer: frames whose RNDIS message lands in
   [mps/2, xfer_len) were silently dropped by the class driver (big pings,
   HTTP requests never arrived).
6. `class/wireless/usbh_rndis.c` — the rx thread re-polls on `-USB_ERR_NAK`
   instead of exiting. In dwc2 buffer-dma mode the controller driver
   error-completes bulk URBs on NAK (idle modem NAKs every IN poll), which
   killed the rx thread as soon as the modem went idle.
7. `osal/usb_osal_threadx.c` — thread_create copies the name into the thread
   allocation. ThreadX stores the name POINTER (no copy); upstream callers
   (usbh_hub snprintf's a stack buffer) left dangling pointers — thread names
   showed as garbage in system tools.
8. `class/wireless/usbh_rndis.c` — cross-transfer message reassembly in the
   rx thread. RNDIS is a message stream: messages may span USB transfers and
   batches may end exactly on the 512-byte MPS (no short packet). Upstream
   parsed per-transfer and dropped the whole batch on MPS-aligned lengths
   ("rndis packet overflow") plus everything after a boundary-spanning
   message — halved TCP throughput under load with the EG915 modem.
9. `port/dwc2/usb_hc_dwc2.c` — **host ISO implementation** (upstream ships it
   disabled: `dwc2_iso_urb_init` inside `#if 0`, empty submit/IRQ branches).
   Covers non-split ISO IN/OUT in buffer-DMA mode: one URB carries
   `urb->num_of_iso_packets` service intervals (`mult` HB packets each,
   DPID field = packets-per-(micro)frame - 1); completion reports the total
   byte count only (xferlen - XFRSIZ residual), no per-packet lengths.
   Class layers must re-arm from the completion callback (IRQ context) to
   keep the periodic stream gapless, and must reconstruct packet boundaries
   from the MPS stride (see `usbh_uvc_test.c`). Split ISO (FS/LS device
   behind a HS hub) is rejected with `-USB_ERR_INVAL`.

10. `port/dwc2/usb_hc_dwc2.c` — **XFRC-without-CHH wedge fix**. The core can
   raise XFRC and leave the channel ENABLED without ever halting it
   (observed on bulk AND iso IN, typically while USB1 initializes);
   upstream unmasks only CHHM, so that state raises NO interrupt and the
   URB parks forever (silent wedge: frozen counters, growing urb_age).
   Fix: HCINTMSK also unmasks XFRCM; the IN-channel IRQ handler gained an
   early-completion branch (XFRC && !CHH && !ep0) that finishes the URB,
   halts the channel and hands off — the late CHH is dropped by a new
   urb==NULL guard. ep0/control is excluded (multi-phase state machine).
   The test module additionally carries a silent-wedge watchdog (>1.5s no
   completion -> kill+resubmit) that escalates to a full session re-init
   (close/open/start) when 4 recoveries yield no completion — this heals
   the USB2 port reset that a USB1 cold init can trigger (HPRT PRST
   observed; the camera re-enumerates behind the stream's back).

## Known limitations

- **DWC2 host ISO is a local implementation** (patch #9), not upstream code:
  no split ISO (FS/LS camera behind a HS hub fails fast), bInterval > 1 is
  not throttled, and per-packet boundaries must be inferred by the class
  layer. Additionally, in buffer-DMA mode the controller terminates ISO IN
  transfers on the first SHORT packet (bulk semantics) — multi-interval
  ISO URBs collapse to the first under-filled microframe, so the class
  layer uses one-interval URBs (~8000 re-arms/s at HS, done from the
  completion IRQ). Validated with a bulk camera end-to-end; ISO camera
  bring-up in progress.
- The ThreadX osal timer (`usb_osal_timer`) runs callbacks on the 1 KB
  `_tx_timer_thread` stack — known MSTKERR trap on this project: callbacks
  must only set flags, never do heavy work.
