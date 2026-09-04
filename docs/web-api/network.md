---
title: Network Management Guide
---

<!-- Curated from Custom/Services/Web/api/NETWORK_API_DOC.md. The authoritative
     endpoint list lives in the [auto-generated reference](./endpoints/network.md).
     Update this page when API behavior changes. -->

# Network Web API

## API Structure Overview

```
Base path: /api/v1/system/network

┌─ /status                  GET     lightweight comm overview (status bar)
│
├─ /wifi/
│   ├── sta                 GET     WiFi client status + network list
│   ├── ap                  GET     WiFi hotspot (AP) configuration
│   ├── config              POST    configure WiFi (STA/AP)
│   ├── scan                POST    trigger a network scan
│   ├── disconnect          POST    disconnect WiFi
│   └── delete              POST    remove a known network
│
├─ /comm/
│   ├── types               GET     details of all comm types
│   ├── switch              POST    switch comm type
│   ├── prefer              GET/POST preferred type setting
│   └── priority            POST    apply priority
│
├─ /cellular/
│   ├── status              GET     cellular status
│   ├── settings            GET/POST cellular settings
│   ├── info                GET     detailed modem info
│   ├── connect             POST    connect cellular
│   ├── disconnect          POST    disconnect cellular
│   ├── save                POST    save cellular settings
│   ├── refresh             POST    refresh modem info
│   └── at                  POST    send an AT command
│
└─ /poe/
    ├── status              GET     PoE status
    ├── connect             POST    connect PoE
    └── disconnect          POST    disconnect PoE
```

---

## 1. Communication Overview (Lightweight)

### GET /network/status

**Purpose**: status bar display and page routing.

**Characteristics**:
- Lightweight, returns only essential information
- Does not include WiFi scan results (call `/wifi/sta` for those)
- Does not include AP configuration (call `/wifi/ap` for that)

**Response example**:
```json
{
  "code": 0,
  "data": {
    "service_state": "running",
    "service_version": "1.0.0",
    "current_comm_type": "wifi",
    "has_connection": true,
    "current_comm_display_name": "WiFi",
    "available_comm_types": [
      {"type": "wifi", "display_name": "WiFi", "is_current": true, "is_connected": true},
      {"type": "cellular", "display_name": "Cellular", "is_current": false, "is_connected": false}
    ],
    "available_comm_count": 2,
    "current_comm_info": {
      "status": "connected",
      "ip_address": "192.168.1.100",
      "signal_strength": -45,
      "ssid": "MyNetwork"
    }
  }
}
```

**When not connected**:
```json
{
  "data": {
    "current_comm_type": "none",
    "has_connection": false,
    "current_comm_display_name": "Not Connected",
    "current_comm_info": {
      "status": "disconnected",
      "ip_address": "",
      "signal_strength": 0,
      "suggested_type": "wifi"
    }
  }
}
```

**Frontend routing logic**:
```javascript
const { current_comm_type } = data;
switch (current_comm_type) {
    case "none":
        showConnectionSelectionPage(data.current_comm_info.suggested_type);
        break;
    case "wifi":
        // call GET /wifi/sta when entering the WiFi page
        loadWiFiPage();
        break;
    case "cellular":
        // call GET /cellular/status when entering the cellular page
        loadCellularPage();
        break;
    case "poe":
        // call GET /poe/status when entering the PoE page
        loadPoEPage();
        break;
}
```

---

## 2. WiFi Client API

### GET /network/wifi/sta

**Purpose**: data for the WiFi management page.

**Response example**:
```json
{
  "code": 0,
  "data": {
    "connected": true,
    "ssid": "MyNetwork",
    "bssid": "AA:BB:CC:DD:EE:FF",
    "rssi": -45,
    "channel": 6,
    "ip_address": "192.168.1.100",
    "mac_address": "11:22:33:44:55:66",
    "state": "up",
    "scan_results": {
      "known_networks": [
        {
          "ssid": "MyNetwork",
          "bssid": "AA:BB:CC:DD:EE:FF",
          "rssi": -45,
          "channel": 6,
          "security": "wpa2_psk",
          "connected": true,
          "is_known": true,
          "last_connected_time": 1699084800
        }
      ],
      "known_count": 1,
      "unknown_networks": [
        {
          "ssid": "GuestWiFi",
          "rssi": -70,
          "security": "open",
          "connected": false,
          "is_known": false
        }
      ],
      "unknown_count": 1
    }
  }
}
```

**Page layout**:
```
┌─────────────────────────────────────────────────────────────┐
│  WiFi Management                                            │
├─────────────────────────────────────────────────────────────┤
│  Current connection                                         │
│  ├─ Network: {ssid}                                         │
│  ├─ Signal:  {rssi} dBm                                     │
│  └─ IP:      {ip_address}                                   │
├─────────────────────────────────────────────────────────────┤
│  Known networks ({known_count})                             │
│  └─ {ssid} {rssi}dBm [Connect] [Delete]                     │
├─────────────────────────────────────────────────────────────┤
│  Available networks ({unknown_count})          [Refresh]    │
│  └─ {ssid} {rssi}dBm {security} [Connect]                   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. WiFi Hotspot (AP) API

### GET /network/wifi/ap

**Purpose**: data for the hotspot settings page.

**Response example**:
```json
{
  "code": 0,
  "data": {
    "enabled": true,
    "state": "up",
    "ssid": "AICamera_AP",
    "password_set": true,
    "security": "wpa2_psk",
    "channel": 6,
    "ip_address": "192.168.4.1",
    "mac_address": "AA:BB:CC:DD:EE:FF",
    "ap_sleep_time": 300
  }
}
```

---

## 4. WiFi Configuration API

### POST /network/wifi/config

**Purpose**: connect to WiFi or configure the hotspot.

**Request parameters**:
```json
{
  "interface": "wl",
  "ssid": "NetworkName",
  "password": "password123",
  "bssid": "AA:BB:CC:DD:EE:FF"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| interface | string | yes | `wl` = client (STA), `ap` = hotspot |
| ssid | string | yes | network name (1-31 chars) |
| password | string | no | password (8-63 chars; omit for open networks) |
| bssid | string | no | target BSSID |
| ap_sleep_time | number | no | hotspot sleep time (AP mode only) |

---

### POST /network/wifi/scan

**Purpose**: refresh the network list.

**Request**: `{}`

**Note**: wait 2-3 seconds after calling, then call `GET /wifi/sta` for results.

---

### POST /network/wifi/disconnect

**Request**:
```json
{"interface": "wl"}
```

---

### POST /network/wifi/delete

**Purpose**: remove a saved network.

**Request**:
```json
{
  "ssid": "NetworkName",
  "bssid": "AA:BB:CC:DD:EE:FF"
}
```

---

## 5. Comm Type Management

### GET /network/comm/types

**Purpose**: details of all communication types.

**Response example**:
```json
{
  "code": 0,
  "data": {
    "current_type": "wifi",
    "preferred_type": "none",
    "auto_priority": true,
    "available_types": ["wifi", "cellular"],
    "type_count": 3,
    "types": [
      {
        "type": "wifi",
        "display_name": "WiFi",
        "status": "connected",
        "available": true,
        "connected": true,
        "is_current": true,
        "can_switch": false,
        "ip_address": "192.168.1.100",
        "priority": 2,
        "detail": {"ssid": "MyNetwork", "rssi": -45}
      },
      {
        "type": "cellular",
        "display_name": "Cellular",
        "status": "disconnected",
        "available": true,
        "connected": false,
        "can_switch": true,
        "priority": 1,
        "detail": {"imei": "861234567890123", "apn": "cmnet"}
      },
      {
        "type": "poe",
        "display_name": "PoE/Ethernet",
        "status": "unavailable",
        "available": false,
        "priority": 3,
        "detail": {}
      }
    ],
    "current_type_info": {
      "type": "wifi",
      "status": "connected",
      "ip_address": "192.168.1.100",
      "ssid": "MyNetwork"
    }
  }
}
```

**Priority**:
| Priority | Type | Notes |
|----------|------|-------|
| 3 | PoE | highest |
| 2 | WiFi | medium |
| 1 | Cellular | lowest |

---

### POST /network/comm/switch

**Purpose**: switch the communication type.

**Request**:
```json
{
  "type": "cellular",
  "timeout_ms": 30000
}
```

**Response**:
```json
{
  "code": 0,
  "data": {
    "success": true,
    "from_type": "wifi",
    "to_type": "cellular",
    "switch_time_ms": 5230
  }
}
```

**Frontend switch flow**:
```javascript
async function switchCommType(targetType) {
    // 1. confirmation dialog
    if (!confirm(`Switch to ${targetType}?`)) return;

    // 2. show loading
    showLoading("Switching...");

    // 3. call the API
    const result = await fetch('/api/v1/system/network/comm/switch', {
        method: 'POST',
        body: JSON.stringify({ type: targetType })
    });

    // 4. handle the result
    const data = await result.json();
    hideLoading();

    if (data.data.success) {
        showSuccess(`Switched to ${targetType}`);
        refreshStatus();
    } else {
        showError(data.data.error);
    }
}
```

---

### GET/POST /network/comm/prefer

**GET response**:
```json
{"preferred_type": "none", "auto_priority": true}
```

**POST request**:
```json
{"preferred_type": "wifi", "auto_priority": false}
```

---

## 6. Cellular API

### GET /network/cellular/status

**Response example**:
```json
{
  "code": 0,
  "data": {
    "available": true,
    "status": "connected",
    "imei": "861234567890123",
    "settings": {
      "apn": "cmnet",
      "username": "",
      "password_set": false,
      "pin_set": false,
      "authentication": 0,
      "enable_roaming": false
    }
  }
}
```

---

### GET/POST /network/cellular/settings

**POST request**:
```json
{
  "apn": "cmnet",
  "username": "user",
  "password": "pass123",
  "pin_code": "1234",
  "authentication": 1,
  "enable_roaming": true,
  "save": true
}
```

| Field | Description |
|-------|-------------|
| authentication | 0=None, 1=PAP, 2=CHAP |

---

### GET /network/cellular/info

**Purpose**: cellular detail dialog.

**Response example**:
```json
{
  "code": 0,
  "data": {
    "network_status": "connected",
    "modem_status": "Ready",
    "model": "EG912U",
    "version": "1.0.0",
    "imei": "861234567890123",
    "imsi": "460001234567890",
    "iccid": "89860012345678901234",
    "isp": "China Mobile",
    "network_type": "LTE",
    "register_status": "Registered",
    "plmn_id": "46000",
    "lac": "1234",
    "cell_id": "56789",
    "signal_level": 4,
    "csq": 25,
    "csq_level": 4,
    "rssi": -70,
    "ipv4_address": "10.0.0.1",
    "ipv4_gateway": "10.0.0.254",
    "ipv4_dns": "8.8.8.8",
    "ipv6_address": "2001:db8::1",
    "ipv6_gateway": "2001:db8::ffff",
    "ipv6_dns": "2001:4860:4860::8888",
    "connection_duration_sec": 3600
  }
}
```

---

### POST /network/cellular/at

**Purpose**: send an AT command (for debugging).

**Request**:
```json
{"command": "AT+CSQ", "timeout_ms": 5000}
```

**Response**:
```json
{
  "success": true,
  "command": "AT+CSQ",
  "response": "+CSQ: 25,99\r\n\r\nOK"
}
```

---

## 7. PoE API

### GET /network/poe/status

**Response example**:
```json
{
  "code": 0,
  "data": {
    "available": true,
    "status": "connected",
    "ip_address": "192.168.1.50",
    "connected": true
  }
}
```

**When unavailable**:
```json
{
  "data": {
    "available": false,
    "status": "unavailable",
    "message": "PoE/Ethernet module not available"
  }
}
```

---

## Error Response Format

```json
{
  "code": -1,
  "message": "Error description",
  "data": null
}
```

| Code | Meaning |
|------|---------|
| -1 | generic error |
| -2 | invalid parameter |
| -3 | service unavailable |
| -4 | method not allowed |

---

## Frontend Page → API Mapping

| Page | Primary API | Data used |
|------|-------------|-----------|
| **Status bar** | `GET /status` | `current_comm_type`, dropdown menu |
| **No-connection page** | `GET /status` | `suggested_type` highlight |
| **WiFi networks page** | `GET /wifi/sta` | scan results, connection state |
| **WiFi hotspot page** | `GET /wifi/ap` | hotspot configuration |
| **Cellular settings page** | `GET /cellular/status` | connection settings |
| **Cellular detail dialog** | `GET /cellular/info` | full modem info |
| **PoE page** | `GET /poe/status` | connection state |
| **Type switching** | `POST /comm/switch` | switch operation |

---

## Typical Call Flows

### App startup
```
1. GET /status         → current comm state
2. route to the page matching current_comm_type
3. e.g. current_comm_type="wifi" → GET /wifi/sta
```

### Switching comm type
```
1. GET /comm/types     → list of switchable types
2. user picks a target type
3. show confirmation dialog
4. POST /comm/switch   → perform the switch
5. GET /status         → refresh the status bar
```

### Connecting to a new WiFi network
```
1. GET /wifi/sta       → network list
2. POST /wifi/scan     → refresh scan (optional)
3. wait 2-3 seconds
4. GET /wifi/sta       → latest list
5. POST /wifi/config   → connect to the selected network
6. GET /wifi/sta       → confirm connection state
```
