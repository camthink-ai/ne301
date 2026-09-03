# Video Stream Hub 升级方案

## 实现状态

| 组件 | 文件 | 状态 |
|------|------|------|
| Video Hub | `Custom/Services/Video/video_stream_hub.h` | ✅ 完成 |
| Video Hub实现 | `Custom/Services/Video/video_stream_hub.c` | ✅ 完成 |
| RTMP服务改造 | `Custom/Services/RTMP/rtmp_service_v2.c` | ✅ 完成 |
| 迁移指南 | `Custom/Services/RTMP/MIGRATION_GUIDE.md` | ✅ 完成 |

## 问题背景

当前系统存在设备协同问题：

| 组件 | Camera Pipe1 | H.264 Encoder | 问题 |
|------|-------------|---------------|------|
| WebSocket预览 | 独立获取 | 独立启动/停止 | 与RTMP冲突 |
| RTMP推流 | 独立获取 | 独立启动/停止 | 与预览冲突 |

**具体问题:**
1. 双重编码 - 同一视频源编码两次，浪费资源
2. 资源竞争 - Encoder被两个服务同时操作
3. 状态不一致 - 一个服务停止encoder可能影响另一个
4. 配置冲突 - 分辨率/帧率可能不同步

## 解决方案：Video Stream Hub

```
                    ┌─────────────────────────────────────┐
                    │         Video Stream Hub            │
                    │  (统一视频流采集、编码、分发中心)     │
                    └─────────────────────────────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        ▼                           ▼                           ▼
   Camera Pipe1              H.264 Encoder               订阅者分发
   (由Hub统一管理)           (由Hub统一管理)              (回调机制)
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
              WebSocket         RTMP Service    (扩展)
              (订阅者)          (订阅者)       HLS/录像
```

## 核心设计

### 1. 订阅者模式

```c
// RTMP服务订阅
video_hub_subscriber_id_t rtmp_sub_id = video_hub_subscribe(
    VIDEO_HUB_SUBSCRIBER_RTMP,
    rtmp_on_frame,        // 帧回调
    rtmp_on_sps_pps,      // SPS/PPS回调
    &rtmp_ctx
);

// WebSocket订阅
video_hub_subscriber_id_t ws_sub_id = video_hub_subscribe(
    VIDEO_HUB_SUBSCRIBER_WEBSOCKET,
    websocket_on_frame,
    NULL,
    &ws_ctx
);
```

### 2. 自动生命周期管理

- 首个订阅者加入 → Hub自动启动
- 最后一个订阅者退出 → Hub自动停止
- 编码一次，多路分发

### 3. SPS/PPS统一管理

- Hub提取SPS/PPS后缓存
- 新订阅者加入时自动推送
- RTMP重连时可从Hub获取

## 迁移步骤

### Phase 1: 集成Video Stream Hub

1. 添加 `video_stream_hub.c/h` 到项目
2. 在 `service_init.c` 中注册Hub服务
3. 初始化顺序: device_service → video_hub → rtmp_service

### Phase 2: 改造WebSocket预览

**当前代码 (`video_encoder_node.c`):**
```c
// 直接发送到WebSocket
websocket_stream_server_send_frame_with_encoder_info(...);
```

**改造后:**
```c
// 通过Hub回调接收帧，然后发送到WebSocket
static aicam_result_t ws_preview_on_frame(const video_hub_frame_t *frame, void *user_data) {
    websocket_frame_type_t type = frame->is_keyframe ? 
        WS_FRAME_TYPE_H264_KEY : WS_FRAME_TYPE_H264_DELTA;
    
    return websocket_stream_server_send_frame(
        frame->data, frame->size, 
        frame->timestamp_ms * 1000,
        type, frame->width, frame->height
    );
}

// 启动预览时订阅Hub
void start_preview(void) {
    g_ws_subscriber_id = video_hub_subscribe(
        VIDEO_HUB_SUBSCRIBER_WEBSOCKET,
        ws_preview_on_frame,
        NULL, NULL
    );
}
```

### Phase 3: 改造RTMP服务

**当前代码 (`rtmp_service.c`):**
```c
// 直接操作Camera和Encoder
fb_len = device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_BUFFER, ...);
device_ioctl(encoder_dev, ENC_CMD_INPUT_BUFFER, ...);
device_ioctl(encoder_dev, ENC_CMD_OUTPUT_FRAME, ...);
rtmp_publisher_send_video_frame(...);
```

**改造后:**
```c
// 通过Hub回调接收编码帧
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
    // 1. 连接RTMP服务器
    // 2. 订阅Video Hub
    g_rtmp_ctx.hub_subscriber_id = video_hub_subscribe(
        VIDEO_HUB_SUBSCRIBER_RTMP,
        rtmp_on_frame,
        rtmp_on_sps_pps,
        &g_rtmp_ctx
    );
}

aicam_result_t rtmp_service_stop_stream(void) {
    // 1. 取消订阅
    video_hub_unsubscribe(g_rtmp_ctx.hub_subscriber_id);
    // 2. 断开RTMP连接
}
```

## API变更

### 新增API

| API | 描述 |
|-----|------|
| `video_hub_init()` | 初始化Hub |
| `video_hub_subscribe()` | 订阅视频流 |
| `video_hub_unsubscribe()` | 取消订阅 |
| `video_hub_get_sps_pps()` | 获取SPS/PPS |
| `video_hub_request_keyframe()` | 请求关键帧 |

### 废弃API (内部使用)

RTMP服务不再直接调用:
- `device_ioctl(camera_dev, CAM_CMD_GET_PIPE1_BUFFER, ...)`
- `device_ioctl(encoder_dev, ENC_CMD_INPUT/OUTPUT_*, ...)`
- `device_start/stop(encoder_dev)`

## 兼容性

### 向后兼容

- 现有Web API无需修改
- 前端代码无需修改
- CLI命令保持不变

### 功能增强

| 场景 | 改造前 | 改造后 |
|------|--------|--------|
| 仅预览 | 编码1次 | 编码1次 |
| 仅推流 | 编码1次 | 编码1次 |
| 预览+推流 | 编码2次 | **编码1次** |
| 推流断线重连 | 需重新提取SPS/PPS | Hub缓存可用 |

## 测试计划

1. **单元测试**
   - Hub订阅/取消订阅
   - SPS/PPS提取和分发
   - 自动启停

2. **集成测试**
   - 仅WebSocket预览
   - 仅RTMP推流
   - 同时预览+推流
   - 动态加入/退出订阅者

3. **压力测试**
   - 长时间运行稳定性
   - 频繁订阅/取消
   - 网络断开重连

## 性能优化

### 零拷贝设计

```
Camera Buffer → Encoder → 分发(多个订阅者共享同一份数据)
                            ├→ WebSocket直接发送
                            └→ RTMP直接发送
```

### 资源占用对比

| 指标 | 改造前 | 改造后 |
|------|--------|--------|
| Encoder调用 | 2次/帧 (预览+推流) | 1次/帧 |
| 内存带宽 | 高 | 降低50% |
| CPU占用 | 高 | 显著降低 |

## 文件结构

```
Custom/Services/Video/
├── video_stream_hub.h      # Hub头文件 ✅
├── video_stream_hub.c      # Hub实现 ✅
└── README.md               # 使用说明

Custom/Services/RTMP/
├── rtmp_service.c          # 原版本 (直接操作Camera/Encoder)
├── rtmp_service_v2.c       # 新版本 (使用Video Hub) ✅
└── MIGRATION_GUIDE.md      # 迁移指南 ✅

待修改文件:
├── Services/Web/websocket_stream_server.c # 改为订阅模式 (待实现)
├── Core/Video/video_encoder_node.c     # 可选: 通过Hub或独立使用
└── Services/service_init.c             # 添加Hub初始化
```

## 实施进度

| 阶段 | 任务 | 状态 |
|------|------|------|
| 1 | 实现Video Stream Hub | ✅ 完成 |
| 2 | 改造RTMP服务 | ✅ 完成 |
| 3 | 改造WebSocket预览 | ⏳ 待实现 |
| 4 | 集成测试 | ⏳ 待测试 |
| 5 | 文档更新 | ✅ 完成 |

## 启用新版RTMP服务

```bash
# 切换到新版本
cd Custom/Services/RTMP
mv rtmp_service.c rtmp_service_legacy.c
mv rtmp_service_v2.c rtmp_service.c

# 或在CMakeLists.txt中配置
# set(RTMP_USE_VIDEO_HUB ON)
```

详细迁移说明见: `Custom/Services/RTMP/MIGRATION_GUIDE.md`

