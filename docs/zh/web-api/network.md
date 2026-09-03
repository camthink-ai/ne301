---
title: 网络管理详解
---

<!-- 整理自 Custom/Services/Web/api/NETWORK_API_DOC.md，端点清单请以 [自动生成的端点参考](./endpoints/network.md) 为准。API 行为变更时请同步更新本页。 -->

# 网络通讯 Web API 文档

## API 结构概览

```
基础路径: /api/v1/system/network

┌─ /status                  GET     通讯概览（状态栏用，轻量）
│
├─ /wifi/
│   ├── sta                 GET     WiFi客户端状态 + 网络列表
│   ├── ap                  GET     WiFi热点配置
│   ├── config              POST    配置WiFi（STA/AP）
│   ├── scan                POST    触发网络扫描
│   ├── disconnect          POST    断开WiFi连接
│   └── delete              POST    删除已知网络
│
├─ /comm/
│   ├── types               GET     所有通讯类型详情
│   ├── switch              POST    切换通讯类型
│   ├── prefer              GET/POST 首选类型设置
│   └── priority            POST    应用优先级
│
├─ /cellular/
│   ├── status              GET     蜂窝状态
│   ├── settings            GET/POST 蜂窝设置
│   ├── info                GET     蜂窝详细信息
│   ├── connect             POST    连接蜂窝
│   ├── disconnect          POST    断开蜂窝
│   ├── save                POST    保存蜂窝设置
│   ├── refresh             POST    刷新蜂窝信息
│   └── at                  POST    发送AT指令
│
└─ /poe/
    ├── status              GET     PoE状态
    ├── connect             POST    连接PoE
    └── disconnect          POST    断开PoE
```

---

## 1. 通讯概览（轻量级）

### GET /network/status

**用途**: 状态栏显示、页面路由判断

**特点**: 
- 轻量级，仅返回必要信息
- 不包含 WiFi 扫描结果（需调用 `/wifi/sta`）
- 不包含 AP 配置（需调用 `/wifi/ap`）

**响应示例**:
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

**无连接时**:
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

**前端路由逻辑**:
```javascript
const { current_comm_type } = data;
switch (current_comm_type) {
    case "none":
        showConnectionSelectionPage(data.current_comm_info.suggested_type);
        break;
    case "wifi":
        // 进入WiFi页面时调用 GET /wifi/sta
        loadWiFiPage();
        break;
    case "cellular":
        // 进入蜂窝页面时调用 GET /cellular/status
        loadCellularPage();
        break;
    case "poe":
        // 进入PoE页面时调用 GET /poe/status
        loadPoEPage();
        break;
}
```

---

## 2. WiFi 客户端 API

### GET /network/wifi/sta

**用途**: WiFi管理页面数据

**响应示例**:
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

**页面显示**:
```
┌─────────────────────────────────────────────────────────────┐
│  WiFi管理                                                   │
├─────────────────────────────────────────────────────────────┤
│  当前连接                                                   │
│  ├─ 网络名称: {ssid}                                       │
│  ├─ 信号强度: {rssi} dBm                                   │
│  └─ IP地址: {ip_address}                                   │
├─────────────────────────────────────────────────────────────┤
│  已知网络 ({known_count})                                  │
│  └─ {ssid} {rssi}dBm [连接] [删除]                         │
├─────────────────────────────────────────────────────────────┤
│  可用网络 ({unknown_count})              [刷新]            │
│  └─ {ssid} {rssi}dBm {security} [连接]                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. WiFi 热点 API

### GET /network/wifi/ap

**用途**: 热点设置页面数据

**响应示例**:
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

## 4. WiFi 配置 API

### POST /network/wifi/config

**用途**: 连接WiFi或配置热点

**请求参数**:
```json
{
  "interface": "wl",
  "ssid": "NetworkName",
  "password": "password123",
  "bssid": "AA:BB:CC:DD:EE:FF"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| interface | string | 是 | `wl`=客户端, `ap`=热点 |
| ssid | string | 是 | 网络名称 (1-31字符) |
| password | string | 否 | 密码 (8-63字符，开放网络留空) |
| bssid | string | 否 | 目标BSSID |
| ap_sleep_time | number | 否 | 热点休眠时间（仅AP模式） |

---

### POST /network/wifi/scan

**用途**: 刷新网络列表

**请求**: `{}`

**注意**: 调用后等待2-3秒，再调用 `GET /wifi/sta` 获取结果

---

### POST /network/wifi/disconnect

**请求**:
```json
{"interface": "wl"}
```

---

### POST /network/wifi/delete

**用途**: 删除已保存的网络

**请求**:
```json
{
  "ssid": "NetworkName",
  "bssid": "AA:BB:CC:DD:EE:FF"
}
```

---

## 5. 通讯类型管理

### GET /network/comm/types

**用途**: 获取所有通讯类型详情

**响应示例**:
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

**优先级**:
| 优先级 | 类型 | 说明 |
|--------|------|------|
| 3 | PoE | 最高优先级 |
| 2 | WiFi | 中等优先级 |
| 1 | Cellular | 最低优先级 |

---

### POST /network/comm/switch

**用途**: 切换通讯类型

**请求**:
```json
{
  "type": "cellular",
  "timeout_ms": 30000
}
```

**响应**:
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

**前端切换流程**:
```javascript
async function switchCommType(targetType) {
    // 1. 确认对话框
    if (!confirm(`确认切换到 ${targetType}？`)) return;
    
    // 2. 显示加载中
    showLoading("正在切换...");
    
    // 3. 调用API
    const result = await fetch('/api/v1/system/network/comm/switch', {
        method: 'POST',
        body: JSON.stringify({ type: targetType })
    });
    
    // 4. 处理结果
    const data = await result.json();
    hideLoading();
    
    if (data.data.success) {
        showSuccess(`已切换到 ${targetType}`);
        refreshStatus();
    } else {
        showError(data.data.error);
    }
}
```

---

### GET/POST /network/comm/prefer

**GET 响应**:
```json
{"preferred_type": "none", "auto_priority": true}
```

**POST 请求**:
```json
{"preferred_type": "wifi", "auto_priority": false}
```

---

## 6. 蜂窝网络 API

### GET /network/cellular/status

**响应示例**:
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

**POST 请求**:
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

| 字段 | 说明 |
|------|------|
| authentication | 0=None, 1=PAP, 2=CHAP |

---

### GET /network/cellular/info

**用途**: 蜂窝详情弹窗

**响应示例**:
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

**用途**: 发送AT指令（调试用）

**请求**:
```json
{"command": "AT+CSQ", "timeout_ms": 5000}
```

**响应**:
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

**响应示例**:
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

**不可用时**:
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

## 错误响应格式

```json
{
  "code": -1,
  "message": "Error description",
  "data": null
}
```

| 错误码 | 说明 |
|--------|------|
| -1 | 通用错误 |
| -2 | 参数无效 |
| -3 | 服务不可用 |
| -4 | 方法不允许 |

---

## 前端页面与API映射

| 页面 | 主要API | 数据用途 |
|------|---------|----------|
| **状态栏** | `GET /status` | `current_comm_type`, 下拉菜单 |
| **无连接页** | `GET /status` | `suggested_type` 高亮推荐 |
| **WiFi网络页** | `GET /wifi/sta` | 扫描结果、连接状态 |
| **WiFi热点页** | `GET /wifi/ap` | 热点配置 |
| **蜂窝设置页** | `GET /cellular/status` | 连接设置 |
| **蜂窝详情弹窗** | `GET /cellular/info` | 完整模组信息 |
| **PoE页** | `GET /poe/status` | 连接状态 |
| **类型切换** | `POST /comm/switch` | 切换操作 |

---

## 调用流程示例

### 应用启动
```
1. GET /status         → 获取当前通讯状态
2. 根据 current_comm_type 路由到对应页面
3. 如 current_comm_type="wifi" → GET /wifi/sta
```

### 切换通讯类型
```
1. GET /comm/types     → 获取可切换类型列表
2. 用户选择目标类型
3. 显示确认对话框
4. POST /comm/switch   → 执行切换
5. GET /status         → 刷新状态栏
```

### WiFi连接新网络
```
1. GET /wifi/sta       → 获取网络列表
2. POST /wifi/scan     → 刷新扫描（可选）
3. 等待2-3秒
4. GET /wifi/sta       → 获取最新列表
5. POST /wifi/config   → 连接选中网络
6. GET /wifi/sta       → 确认连接状态
```

