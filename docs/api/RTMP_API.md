# RTMP Streaming Service Web API

## Overview

The RTMP service provides live video streaming to an RTMP server.

**Base URL:** `/api/v1`
**Authentication:** all endpoints require auth (Authorization header)

> **Note**: RTMP configuration is now part of the video stream mode configuration. Only `url` and `stream_key` need to be set; video parameters use encoder defaults.

---

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/apps/rtmp/config` | Get RTMP configuration |
| POST | `/api/v1/apps/rtmp/config` | Set RTMP configuration |
| POST | `/api/v1/apps/rtmp/start` | Start streaming |
| POST | `/api/v1/apps/rtmp/stop` | Stop streaming |
| GET | `/api/v1/apps/rtmp/status` | Get stream status and statistics |

---

## 1. Get RTMP Configuration

Returns the current RTMP service configuration and status.

### Request

```
GET /api/v1/apps/rtmp/config
```

### Response

**Success (200 OK)**

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

### Response Fields

#### config

| Field | Type | Description |
|-------|------|-------------|
| `enable` | boolean | whether RTMP streaming is enabled |
| `url` | string | RTMP server address, max 256 chars |
| `stream_key` | string | stream key, max 128 chars |

#### status (service state)

| Field | Type | Description |
|-------|------|-------------|
| `initialized` | boolean | whether the service is initialized |
| `streaming` | boolean | whether currently streaming |
| `state` | string | streaming state enum |

**state enum values:**
- `idle` - not streaming
- `connecting` - connecting to the server
- `streaming` - streaming
- `reconnecting` - reconnecting
- `stopping` - stopping
- `error` - error state

---

## 2. Set RTMP Configuration

Updates the RTMP service configuration. The configuration is persisted to NVS.

**Note: the configuration cannot be changed while streaming.**

### Request

```
POST /api/v1/apps/rtmp/config
Content-Type: application/json
```

### Request Body

All fields are optional; only the provided fields are updated.

```json
{
  "enable": true,
  "url": "rtmp://live.example.com/live",
  "stream_key": "your-stream-key"
}
```

### Request Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `enable` | boolean | no | enable/disable RTMP streaming |
| `url` | string | no | RTMP server address |
| `stream_key` | string | no | stream key |

### Response

**Success (200 OK)**

```json
{
  "success": true,
  "message": "RTMP config updated"
}
```

**Error response**

Config cannot be changed while streaming:
```json
{
  "error": "Cannot change config while streaming",
  "code": 400
}
```

---

## 3. Start Streaming

Starts RTMP video streaming.

### Request

```
POST /api/v1/apps/rtmp/start
Content-Type: application/json
```

### Request Body (optional)

The URL and stream_key can be overridden temporarily at start time:

```json
{
  "url": "rtmp://live.example.com/live",
  "stream_key": "temporary-key"
}
```

### Response

**Success (200 OK)**

```json
{
  "success": true,
  "status": "started",
  "state": "streaming"
}
```

**Already streaming (200 OK)**

```json
{
  "success": true,
  "status": "already_streaming",
  "state": "streaming"
}
```

**Error response**

URL not configured:
```json
{
  "success": false,
  "status": "failed",
  "error_code": -5,
  "state": "error"
}
```

---

## 4. Stop Streaming

Stops RTMP video streaming.

### Request

```
POST /api/v1/apps/rtmp/stop
```

### Response

**Success (200 OK)**

```json
{
  "success": true,
  "status": "stopped",
  "state": "idle"
}
```

**Not streaming (200 OK)**

```json
{
  "success": true,
  "status": "already_stopped",
  "state": "idle"
}
```

---

## 5. Get Stream Status and Statistics

Returns the current streaming state and detailed statistics.

### Request

```
GET /api/v1/apps/rtmp/status
```

### Response

**Success (200 OK)**

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

### Statistics Fields

| Field | Type | Description |
|-------|------|-------------|
| `frames_sent` | number | total frames sent |
| `bytes_sent` | number | total bytes sent |
| `keyframes_sent` | number | keyframes sent |
| `dropped_frames` | number | frames dropped |
| `reconnect_count` | number | reconnect attempts |
| `stream_duration_sec` | number | stream duration (seconds) |

---

## Error Codes

| HTTP status | Description |
|------------|-------------|
| 405 | method not allowed |
| 503 | RTMP service not initialized |
| 400 | invalid request parameters, or config change attempted while streaming |
| 500 | internal server error |

---

## Usage Examples

### JavaScript / fetch

```javascript
const API_BASE = '/api/v1/apps/rtmp';
const headers = {
  'Content-Type': 'application/json',
  'Authorization': 'Bearer YOUR_TOKEN'
};

// get configuration
const getConfig = async () => {
  const res = await fetch(`${API_BASE}/config`, { headers });
  return res.json();
};

// set configuration
const setConfig = async (url, streamKey) => {
  const res = await fetch(`${API_BASE}/config`, {
    method: 'POST',
    headers,
    body: JSON.stringify({ enable: true, url, stream_key: streamKey })
  });
  return res.json();
};

// start streaming
const startStream = async () => {
  const res = await fetch(`${API_BASE}/start`, { method: 'POST', headers });
  return res.json();
};

// stop streaming
const stopStream = async () => {
  const res = await fetch(`${API_BASE}/stop`, { method: 'POST', headers });
  return res.json();
};

// get status
const getStatus = async () => {
  const res = await fetch(`${API_BASE}/status`, { headers });
  return res.json();
};
```

### cURL

```bash
# get configuration
curl -X GET http://192.168.1.1/api/v1/apps/rtmp/config \
  -H "Authorization: Bearer YOUR_TOKEN"

# set configuration
curl -X POST http://192.168.1.1/api/v1/apps/rtmp/config \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -d '{"enable": true, "url": "rtmp://live.example.com/live", "stream_key": "your-key"}'

# start streaming
curl -X POST http://192.168.1.1/api/v1/apps/rtmp/start \
  -H "Authorization: Bearer YOUR_TOKEN"

# stop streaming
curl -X POST http://192.168.1.1/api/v1/apps/rtmp/stop \
  -H "Authorization: Bearer YOUR_TOKEN"

# get status
curl -X GET http://192.168.1.1/api/v1/apps/rtmp/status \
  -H "Authorization: Bearer YOUR_TOKEN"
```

---

## Typical Workflow

```
1. Configure RTMP (POST /config) - set url and stream_key
2. Start streaming (POST /start)
3. Poll status (GET /status) - monitor stream quality
4. Stop streaming (POST /stop)
```

## Configuration Notes

| Setting | Storage | Description |
|---------|---------|-------------|
| enable | NVS | RTMP feature toggle |
| url | NVS | RTMP server address |
| stream_key | NVS | stream key |
| video parameters | - | encoder defaults (1280x720@30fps) |

> The RTMP configuration is stored in NVS as part of the video stream mode (`video_stream_mode`).

## Common Streaming URLs

| Platform | URL format |
|----------|------------|
| Generic RTMP | `rtmp://server/app` + stream_key |
| YouTube | `rtmp://a.rtmp.youtube.com/live2` |
| Twitch | `rtmp://live.twitch.tv/app` |
| Bilibili | `rtmp://live-push.bilivideo.com/live-bvc` |
