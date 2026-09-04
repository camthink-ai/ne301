# Video Stream Hub Upgrade Plan

## Implementation Status

| Component | File | Status |
|-----------|------|--------|
| Video Hub | `Custom/Services/Video/video_stream_hub.h` | ✅ done |
| Video Hub implementation | `Custom/Services/Video/video_stream_hub.c` | ✅ done |
| RTMP service rework | `Custom/Services/RTMP/rtmp_service_v2.c` | ✅ done |
| Migration guide | `Custom/Services/RTMP/MIGRATION_GUIDE.md` | ✅ done |

## Background

The current system has a device coordination problem:

| Component | Camera Pipe1 | H.264 Encoder | Problem |
|-----------|-------------|---------------|---------|
| WebSocket preview | acquires independently | starts/stops independently | conflicts with RTMP |
| RTMP streaming | acquires independently | starts/stops independently | conflicts with preview |

**Specific issues:**
1. Double encoding - the same video source is encoded twice, wasting resources
2. Resource contention - the encoder is driven by two services at once
3. Inconsistent state - one service stopping the encoder can break the other
4. Config drift - resolution/framerate can go out of sync

## Solution: Video Stream Hub

```
                    ┌─────────────────────────────────────┐
                    │         Video Stream Hub            │
                    │  (unified capture/encode/dispatch)  │
                    └─────────────────────────────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        ▼                           ▼                           ▼
   Camera Pipe1              H.264 Encoder               Subscriber
   (owned by Hub)            (owned by Hub)              dispatch (callbacks)
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
              WebSocket         RTMP Service    (extensible)
              (subscriber)      (subscriber)    HLS/recording
```

## Core Design

### 1. Subscriber model

```c
// RTMP service subscribes
video_hub_subscriber_id_t rtmp_sub_id = video_hub_subscribe(
    VIDEO_HUB_SUBSCRIBER_RTMP,
    rtmp_on_frame,        // frame callback
    rtmp_on_sps_pps,      // SPS/PPS callback
    &rtmp_ctx
);

// WebSocket subscribes
video_hub_subscriber_id_t ws_sub_id = video_hub_subscribe(
    VIDEO_HUB_SUBSCRIBER_WEBSOCKET,
    websocket_on_frame,
    NULL,
    &ws_ctx
);
```

### 2. Automatic lifecycle management

- First subscriber joins → the hub starts automatically
- Last subscriber leaves → the hub stops automatically
- Encode once, fan out to many

### 3. Centralized SPS/PPS handling

- The hub extracts and caches SPS/PPS
- New subscribers receive it automatically on join
- RTMP can fetch it from the hub on reconnect

## Migration Steps

### Phase 1: integrate the Video Stream Hub

1. Add `video_stream_hub.c/h` to the project
2. Register the hub service in `service_init.c`
3. Init order: device_service → video_hub → rtmp_service

### Phase 2: rework the WebSocket preview

**Current code (`video_encoder_node.c`):**
```c
// sends directly to WebSocket
websocket_stream_server_send_frame_with_encoder_info(...);
```

**After rework:**
```c
// receive frames via a hub callback, then send to WebSocket
static aicam_result_t ws_preview_on_frame(const video_hub_frame_t *frame, void *user_data) {
    websocket_frame_type_t type = frame->is_keyframe ?
        WS_FRAME_TYPE_H264_KEY : WS_FRAME_TYPE_H264_DELTA;

    return websocket_stream_server_send_frame(
        frame->data, frame->size,
        frame->timestamp_ms * 1000,
        type, frame->width, frame->height
    );
}

// subscribe to the hub when starting preview
void start_preview(void) {
    g_ws_subscriber_id = video_hub_subscribe(
        VIDEO_HUB_SUBSCRIBER_WEBSOCKET,
        ws_preview_on_frame,
        NULL, NULL
    );
}
```

### Phase 3: rework the RTMP service

**Current code (`rtmp_service.c`):**
```c
// drives camera and encoder directly
fb_len = device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_BUFFER, ...);
device_ioctl(encoder_dev, ENC_CMD_INPUT_BUFFER, ...);
device_ioctl(encoder_dev, ENC_CMD_OUTPUT_FRAME, ...);
rtmp_publisher_send_video_frame(...);
```

**After rework:**
```c
// receive encoded frames via a hub callback
static aicam_result_t rtmp_on_frame(const video_hub_frame_t *frame, void *user_data) {
    rtmp_service_context_t *ctx = (rtmp_service_context_t *)user_data;

    if (!rtmp_publisher_is_connected(ctx->publisher)) {
        return AICAM_ERROR;
    }

    int ret = rtmp_publisher_send_video_frame(
        ctx->publisher,
        frame->data, frame->size,
        frame->is_keyframe,
        frame->timestamp_ms
    );

    return (ret == RTMP_PUB_OK) ? AICAM_OK : AICAM_ERROR;
}

static void rtmp_on_sps_pps(const video_hub_sps_pps_t *sps_pps, void *user_data) {
    rtmp_service_context_t *ctx = (rtmp_service_context_t *)user_data;

    rtmp_publisher_send_sps_pps(
        ctx->publisher,
        sps_pps->sps_data, sps_pps->sps_size,
        sps_pps->pps_data, sps_pps->pps_size
    );
}

aicam_result_t rtmp_service_start_stream(void) {
    // 1. connect to the RTMP server
    // 2. subscribe to the video hub
    g_rtmp_ctx.hub_subscriber_id = video_hub_subscribe(
        VIDEO_HUB_SUBSCRIBER_RTMP,
        rtmp_on_frame,
        rtmp_on_sps_pps,
        &g_rtmp_ctx
    );
}

aicam_result_t rtmp_service_stop_stream(void) {
    // 1. unsubscribe
    video_hub_unsubscribe(g_rtmp_ctx.hub_subscriber_id);
    // 2. disconnect from RTMP
}
```

## API Changes

### New APIs

| API | Description |
|-----|-------------|
| `video_hub_init()` | initialize the hub |
| `video_hub_subscribe()` | subscribe to the video stream |
| `video_hub_unsubscribe()` | unsubscribe |
| `video_hub_get_sps_pps()` | get SPS/PPS |
| `video_hub_request_keyframe()` | request a keyframe |

### Deprecated usage (internal)

The RTMP service no longer calls directly:
- `device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_BUFFER, ...)`
- `device_ioctl(encoder_dev, ENC_CMD_INPUT/OUTPUT_*, ...)`
- `device_start/stop(encoder_dev)`

## Compatibility

### Backward compatible

- Existing Web APIs unchanged
- Frontend code unchanged
- CLI commands unchanged

### Improvements

| Scenario | Before | After |
|----------|--------|-------|
| Preview only | encode 1x | encode 1x |
| Stream only | encode 1x | encode 1x |
| Preview + stream | encode 2x | **encode 1x** |
| RTMP reconnect | must re-extract SPS/PPS | served from hub cache |

## Test Plan

1. **Unit tests**
   - hub subscribe / unsubscribe
   - SPS/PPS extraction and dispatch
   - automatic start/stop

2. **Integration tests**
   - WebSocket preview only
   - RTMP streaming only
   - preview + streaming simultaneously
   - dynamic subscriber join/leave

3. **Stress tests**
   - long-run stability
   - frequent subscribe/unsubscribe
   - network disconnect/reconnect

## Performance

### Zero-copy design

```
Camera Buffer → Encoder → dispatch (subscribers share the same data)
                            ├→ WebSocket sends directly
                            └→ RTMP sends directly
```

### Resource comparison

| Metric | Before | After |
|--------|--------|-------|
| Encoder invocations | 2x/frame (preview + stream) | 1x/frame |
| Memory bandwidth | high | ~50% lower |
| CPU usage | high | significantly lower |

## File Layout

```
Custom/Services/Video/
├── video_stream_hub.h      # hub header ✅
├── video_stream_hub.c      # hub implementation ✅
└── README.md               # usage notes

Custom/Services/RTMP/
├── rtmp_service.c          # legacy version (drives camera/encoder directly)
├── rtmp_service_v2.c       # new version (uses the video hub) ✅
└── MIGRATION_GUIDE.md      # migration guide ✅

Files to modify:
├── Services/Web/websocket_stream_server.c # switch to subscriber model (todo)
├── Core/Video/video_encoder_node.c     # optional: via hub or standalone
└── Services/service_init.c             # add hub initialization
```

## Progress

| Phase | Task | Status |
|-------|------|--------|
| 1 | implement the Video Stream Hub | ✅ done |
| 2 | rework the RTMP service | ✅ done |
| 3 | rework the WebSocket preview | ⏳ todo |
| 4 | integration testing | ⏳ todo |
| 5 | documentation | ✅ done |

## Enabling the new RTMP service

```bash
# switch to the new version
cd Custom/Services/RTMP
mv rtmp_service.c rtmp_service_legacy.c
mv rtmp_service_v2.c rtmp_service.c

# or configure in CMakeLists.txt
# set(RTMP_USE_VIDEO_HUB ON)
```

See `Custom/Services/RTMP/MIGRATION_GUIDE.md` for detailed migration instructions.
