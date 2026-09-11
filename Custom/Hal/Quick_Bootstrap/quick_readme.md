# Quick capture helpers (time-optimized wake path)

This directory holds the **fast-capture** helpers used when the device wakes for a
capture (RTC / PIR / config-key) in time-optimized mode. It is intentionally **not**
a self-contained capture→upload→sleep pipeline: upload, MQTT, communication and sleep
are handled by the regular service layer, not by anything here.

## What lives here

| Component | Files | Role |
|-----------|-------|------|
| `quick_snapshot` | `quick_snapshot.c/.h` | Camera/JPEG (+ optional AI) capture pipeline. The only module that runs on the fast path. |
| `quick_storage` | `quick_storage.c/.h` | NVS config readers + ISP IQ builder used by `quick_snapshot`. |
| logging/trace | `quick_trace.h` | Shared `QB_LOG*` / `QT_TRACE` macros (plain printf, no mutex). |

## Real call path (what actually runs on wake)

1. `driver_core_init()` (`Custom/Hal/driver_core.c`): when woken via
   `QUICK_SNAPSHOT_CARE_WAKEUP_FLAG_MASK`, registers only `camera`/`jpegc` and calls
   **`quick_snapshot_init()`** — draw/nn/full init is skipped.
2. `system_service`'s `requires_time_optimized_mode` branch
   (`Custom/Services/System/system_service.c`) is the orchestrator on this path: it
   calls `quick_snapshot_wait_capture_jpeg` / `quick_snapshot_wait_ai_result` /
   `quick_snapshot_wait_ai_jpeg` / `quick_snapshot_get_frame_id` directly, then builds
   JSON and reports through the normal `mqtt_service` / `communication_service`, and
   enters sleep via `pwr` / `u0_module`.

So: **capture → `quick_snapshot_*`; config read → `quick_storage_read_snapshot_config`
/ `quick_storage_fill_isp_iq_param`; everything else (network, MQTT publish, sleep) is
outside this directory.**

## Removed (do not assume a local orchestrator)

`quick_bootstrap` (a standalone capture+network+MQTT+sleep orchestrator) and
`quick_network` (a local netif/MQTT thread) previously lived here but were never on the
wake path — `quick_bootstrap_run()` was commented out in `driver_core_init`, and
`quick_network_*` was only called from it. Both were deleted to avoid the false
impression that wake capture runs through a dedicated network/upload pipeline here.

## Note on unused `quick_storage` readers

`quick_storage` also exposes readers that the deleted modules consumed and that the
current wake path does **not** use: `quick_storage_read_netif_config`,
`quick_storage_read_mqtt_all_config`, `quick_storage_read_known_wifi_networks`,
`quick_storage_read_comm_pref_type`, `quick_storage_read_work_mode_config`,
`quick_storage_read_device_info`, and the async write-task queue
(`quick_storage_init` / `quick_storage_add_write_task`). They are kept only as NVS
helpers; do not treat them as part of the capture path. Trim them if no consumer reappears.
