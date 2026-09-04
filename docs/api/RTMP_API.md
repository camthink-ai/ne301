# RTMP 推流服务 Web API 文档

## 概述

RTMP服务提供视频直播推流功能，支持连接到RTMP服务器进行视频推流。

**Base URL:** `/api/v1`  
**认证:** 所有接口需要认证 (Authorization Header)

> **注意**: RTMP配置现已整合到视频流模式配置中，仅需配置 `url` 和 `stream_key`，视频参数使用编码器默认值。

---

## API 端点列表

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/v1/apps/rtmp/config` | 获取RTMP配置 |
| POST | `/api/v1/apps/rtmp/config` | 设置RTMP配置 |
| POST | `/api/v1/apps/rtmp/start` | 开始推流 |
| POST | `/api/v1/apps/rtmp/stop` | 停止推流 |
| GET | `/api/v1/apps/rtmp/status` | 获取推流状态和统计 |

---

## 1. 获取RTMP配置

获取当前RTMP服务的配置信息和状态。

### 请求

```
GET /api/v1/apps/rtmp/config
```

### 响应

**成功 (200 OK)**

```json
{
  "config": {
    "enable": true,
    "url": "rtmp://live.example.com/live",
    "stream_key": "your-stream-key"
  },
  "status": {
    "initialized": true,
    "streaming": false,
    "state": "idle"
  }
}
```

### 响应字段说明

#### config (配置)

| 字段 | 类型 | 描述 |
|------|------|------|
| `enable` | boolean | RTMP推流是否启用 |
| `url` | string | RTMP服务器地址，最大256字符 |
| `stream_key` | string | 推流密钥，最大128字符 |

#### status (服务状态)

| 字段 | 类型 | 描述 |
|------|------|------|
| `initialized` | boolean | 服务是否已初始化 |
| `streaming` | boolean | 是否正在推流 |
| `state` | string | 推流状态枚举 |

**state 枚举值:**
- `idle` - 空闲，未推流
- `connecting` - 正在连接服务器
- `streaming` - 正在推流
- `reconnecting` - 正在重连
- `stopping` - 正在停止
- `error` - 错误状态

---

## 2. 设置RTMP配置

更新RTMP服务配置。配置会持久化存储到NVS。

**注意：推流过程中无法修改配置。**

### 请求

```
POST /api/v1/apps/rtmp/config
Content-Type: application/json
```

### 请求体

所有字段均为可选，仅更新提供的字段。

```json
{
  "enable": true,
  "url": "rtmp://live.example.com/live",
  "stream_key": "your-stream-key"
}
```

### 请求字段说明

| 字段 | 类型 | 必填 | 描述 |
|------|------|------|------|
| `enable` | boolean | 否 | 启用/禁用RTMP推流功能 |
| `url` | string | 否 | RTMP服务器地址 |
| `stream_key` | string | 否 | 推流密钥 |

### 响应

**成功 (200 OK)**

```json
{
  "success": true,
  "message": "RTMP config updated"
}
```

**错误响应**

推流中无法修改配置:
```json
{
  "error": "Cannot change config while streaming",
  "code": 400
}
```

---

## 3. 开始推流

启动RTMP视频推流。

### 请求

```
POST /api/v1/apps/rtmp/start
Content-Type: application/json
```

### 请求体 (可选)

可以在启动时临时覆盖URL和stream_key:

```json
{
  "url": "rtmp://live.example.com/live",
  "stream_key": "temporary-key"
}
```

### 响应

**成功 (200 OK)**

```json
{
  "success": true,
  "status": "started",
  "state": "streaming"
}
```

**已在推流 (200 OK)**

```json
{
  "success": true,
  "status": "already_streaming",
  "state": "streaming"
}
```

**错误响应**

未配置URL:
```json
{
  "success": false,
  "status": "failed",
  "error_code": -5,
  "state": "error"
}
```

---

## 4. 停止推流

停止RTMP视频推流。

### 请求

```
POST /api/v1/apps/rtmp/stop
```

### 响应

**成功 (200 OK)**

```json
{
  "success": true,
  "status": "stopped",
  "state": "idle"
}
```

**未在推流 (200 OK)**

```json
{
  "success": true,
  "status": "already_stopped",
  "state": "idle"
}
```

---

## 5. 获取推流状态和统计

获取当前推流状态和详细统计信息。

### 请求

```
GET /api/v1/apps/rtmp/status
```

### 响应

**成功 (200 OK)**

```json
{
  "status": {
    "streaming": true,
    "state": "streaming"
  },
  "statistics": {
    "frames_sent": 18000,
    "bytes_sent": 45000000,
    "keyframes_sent": 300,
    "dropped_frames": 5,
    "reconnect_count": 0,
    "stream_duration_sec": 600
  }
}
```

### 统计字段说明

| 字段 | 类型 | 描述 |
|------|------|------|
| `frames_sent` | number | 已发送的总帧数 |
| `bytes_sent` | number | 已发送的总字节数 |
| `keyframes_sent` | number | 已发送的关键帧数 |
| `dropped_frames` | number | 丢弃的帧数 |
| `reconnect_count` | number | 重连次数 |
| `stream_duration_sec` | number | 推流持续时间(秒) |

---

## 错误码说明

| HTTP状态码 | 描述 |
|-----------|------|
| 405 | 请求方法不允许 |
| 503 | RTMP服务未初始化 |
| 400 | 无效的请求参数或推流中无法修改配置 |
| 500 | 内部服务器错误 |

---

## 使用示例

### JavaScript/Fetch

```javascript
const API_BASE = '/api/v1/apps/rtmp';
const headers = {
  'Content-Type': 'application/json',
  'Authorization': 'Bearer YOUR_TOKEN'
};

// 获取配置
const getConfig = async () => {
  const res = await fetch(`${API_BASE}/config`, { headers });
  return res.json();
};

// 设置配置
const setConfig = async (url, streamKey) => {
  const res = await fetch(`${API_BASE}/config`, {
    method: 'POST',
    headers,
    body: JSON.stringify({ enable: true, url, stream_key: streamKey })
  });
  return res.json();
};

// 开始推流
const startStream = async () => {
  const res = await fetch(`${API_BASE}/start`, { method: 'POST', headers });
  return res.json();
};

// 停止推流
const stopStream = async () => {
  const res = await fetch(`${API_BASE}/stop`, { method: 'POST', headers });
  return res.json();
};

// 获取状态
const getStatus = async () => {
  const res = await fetch(`${API_BASE}/status`, { headers });
  return res.json();
};
```

### cURL

```bash
# 获取配置
curl -X GET http://192.168.1.1/api/v1/apps/rtmp/config \
  -H "Authorization: Bearer YOUR_TOKEN"

# 设置配置
curl -X POST http://192.168.1.1/api/v1/apps/rtmp/config \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -d '{"enable": true, "url": "rtmp://live.example.com/live", "stream_key": "your-key"}'

# 开始推流
curl -X POST http://192.168.1.1/api/v1/apps/rtmp/start \
  -H "Authorization: Bearer YOUR_TOKEN"

# 停止推流
curl -X POST http://192.168.1.1/api/v1/apps/rtmp/stop \
  -H "Authorization: Bearer YOUR_TOKEN"

# 获取状态
curl -X GET http://192.168.1.1/api/v1/apps/rtmp/status \
  -H "Authorization: Bearer YOUR_TOKEN"
```

---

## 典型工作流程

```
1. 配置RTMP (POST /config) - 设置 url 和 stream_key
2. 开始推流 (POST /start)
3. 轮询状态 (GET /status) - 监控推流质量
4. 停止推流 (POST /stop)
```

## 配置说明

| 配置项 | 存储位置 | 描述 |
|--------|----------|------|
| enable | NVS | RTMP功能开关 |
| url | NVS | RTMP服务器地址 |
| stream_key | NVS | 推流密钥 |
| 视频参数 | - | 使用编码器默认值 (1280x720@30fps) |

> RTMP配置作为视频流模式(`video_stream_mode`)的一部分存储在NVS中。

## 常见推流地址格式

| 平台 | URL格式 |
|------|---------|
| 通用RTMP | `rtmp://server/app` + stream_key |
| YouTube | `rtmp://a.rtmp.youtube.com/live2` |
| Twitch | `rtmp://live.twitch.tv/app` |
| Bilibili | `rtmp://live-push.bilivideo.com/live-bvc` |
