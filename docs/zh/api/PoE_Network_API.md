# PoE/以太网网络 API 文档

## 概述

本文档描述 PoE/以太网网络管理相关的 RESTful API 接口，供前端开发参考。

**基础路径**: `/api/v1/system/network/poe`

**认证**: 所有接口均需要认证 (`require_auth: true`)

**Content-Type**: `application/json`

---

## 统一响应格式

### 成功响应

```json
{
  "success": true,
  "message": "操作成功消息",
  "data": {
    // 实际业务数据
  }
}
```

### 失败响应

```json
{
  "success": false,
  "error_code": "ERROR_CODE_STRING",
  "message": "错误描述信息"
}
```

### 响应字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `success` | boolean | 是 | 操作是否成功 |
| `error_code` | string | 否 | 错误码字符串（仅失败时出现） |
| `message` | string | 否 | 操作结果消息（可选） |
| `data` | object | 否 | 业务数据（仅成功时出现） |

### 错误码 (error_code)

| 错误码字符串 | 数值 | 说明 |
|--------------|------|------|
| `INVALID_REQUEST` | 400 | 无效请求 |
| `UNAUTHORIZED` | 401 | 未授权 |
| `FORBIDDEN` | 403 | 禁止访问 |
| `NOT_FOUND` | 404 | 未找到 |
| `METHOD_NOT_ALLOWED` | 405 | 方法不允许 |
| `TIMEOUT` | 408 | 超时 |
| `TOO_MANY_REQUESTS` | 429 | 请求过多 |
| `INTERNAL_ERROR` | 500 | 内部错误 |
| `BAD_GATEWAY` | 502 | 网关错误 |
| `SERVICE_UNAVAILABLE` | 503 | 服务不可用 |
| `GATEWAY_TIMEOUT` | 504 | 网关超时 |

---

## PoE 状态码定义

| 状态码 | 名称 | 说明 |
|--------|------|------|
| 0 | `POE_STATUS_OFFLINE` | PoE 离线/未供电 |
| 1 | `POE_STATUS_LINK_DOWN` | 网线未连接 |
| 2 | `POE_STATUS_CONNECTING` | 正在连接（DHCP进行中） |
| 3 | `POE_STATUS_CONNECTED` | 已连接，IP有效 |
| 4 | `POE_STATUS_DHCP_FAILED` | DHCP 获取IP失败 |
| 5 | `POE_STATUS_STATIC_CONFIG_ERROR` | 静态IP配置错误 |
| 6 | `POE_STATUS_IP_CONFLICT` | IP地址冲突 |
| 7 | `POE_STATUS_GATEWAY_UNREACHABLE` | 网关不可达 |
| 8 | `POE_STATUS_DNS_ERROR` | DNS解析错误 |
| 9 | `POE_STATUS_ERROR` | 通用错误 |

---

## 1. 获取 PoE 状态

### 请求

```http
GET /api/v1/system/network/poe/status
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE status retrieved successfully",
  "data": {
    "available": true,
    "status": "Connected",
    "ip_address": "192.168.60.100",
    "connected": true
  }
}
```

### data 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `available` | boolean | PoE/以太网模块是否可用 |
| `status` | string | 连接状态: `Unavailable`, `Disconnected`, `Connecting`, `Connected`, `Failed`, `Switching` |
| `ip_address` | string | 当前IP地址（仅在connected时有效） |
| `connected` | boolean | 是否已连接 |

### 模块不可用时的响应

```json
{
  "success": true,
  "message": "PoE status retrieved successfully",
  "data": {
    "available": false,
    "status": "unavailable",
    "message": "PoE/Ethernet module not available"
  }
}
```

---

## 2. 获取 PoE 详细信息

### 请求

```http
GET /api/v1/system/network/poe/info
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE info retrieved successfully",
  "data": {
    "available": true,
    "network_status": "Connected",
    "status_code": 3,
    "status_message": "Connected",
    "ip_mode": "dhcp",
    "ip_address": "192.168.60.100",
    "netmask": "255.255.255.0",
    "gateway": "192.168.60.1",
    "dns_primary": "8.8.8.8",
    "dns_secondary": "223.5.5.5",
    "hostname": "aicam-poe",
    "mac_address": "00:11:22:33:44:55",
    "interface_name": "wn",
    "link_up": true,
    "poe_powered": true,
    "connection_duration_sec": 3600,
    "connection_start_time": 1703318400,
    "dhcp_lease_time": 86400,
    "dhcp_lease_remaining": 43200,
    "connect_count": 5,
    "disconnect_count": 2,
    "dhcp_fail_count": 0,
    "last_error_code": 0
  }
}
```

### data 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `available` | boolean | PoE模块是否可用 |
| `network_status` | string | 网络状态文本 |
| `status_code` | number | 状态码（参见PoE状态码定义表） |
| `status_message` | string | 状态码对应的消息 |
| `ip_mode` | string | IP模式: `dhcp` 或 `static` |
| `ip_address` | string | IPv4地址 |
| `netmask` | string | 子网掩码 |
| `gateway` | string | 默认网关 |
| `dns_primary` | string | 主DNS服务器 |
| `dns_secondary` | string | 备用DNS服务器 |
| `hostname` | string | 主机名 |
| `mac_address` | string | MAC地址 |
| `interface_name` | string | 网络接口名称 |
| `link_up` | boolean | 物理链路状态 |
| `poe_powered` | boolean | PoE供电状态 |
| `connection_duration_sec` | number | 连接持续时间（秒） |
| `connection_start_time` | number | 连接开始时间戳 |
| `dhcp_lease_time` | number | DHCP租约时间（秒） |
| `dhcp_lease_remaining` | number | DHCP租约剩余时间（秒） |
| `connect_count` | number | 连接尝试次数 |
| `disconnect_count` | number | 断开连接次数 |
| `dhcp_fail_count` | number | DHCP失败次数 |
| `last_error_code` | number | 最后错误码 |

---

## 3. 获取 PoE 配置

### 请求

```http
GET /api/v1/system/network/poe/config
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE config operation completed",
  "data": {
    "ip_mode": "dhcp",
    "ip_address": "192.168.60.232",
    "netmask": "255.255.255.0",
    "gateway": "192.168.60.1",
    "dns_primary": "8.8.8.8",
    "dns_secondary": "223.5.5.5",
    "hostname": "aicam-poe",
    "dhcp_timeout_ms": 30000,
    "dhcp_retry_count": 3,
    "dhcp_retry_interval_ms": 5000,
    "power_recovery_delay_ms": 5000,
    "auto_reconnect": true,
    "persist_last_ip": true,
    "validate_gateway": true,
    "detect_ip_conflict": true
  }
}
```

### data 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `ip_mode` | string | IP模式: `dhcp` 或 `static` |
| `ip_address` | string | 静态IPv4地址 |
| `netmask` | string | 子网掩码 |
| `gateway` | string | 默认网关 |
| `dns_primary` | string | 主DNS服务器 |
| `dns_secondary` | string | 备用DNS服务器 |
| `hostname` | string | 主机名 |
| `dhcp_timeout_ms` | number | DHCP超时时间（毫秒） |
| `dhcp_retry_count` | number | DHCP重试次数 |
| `dhcp_retry_interval_ms` | number | DHCP重试间隔（毫秒） |
| `power_recovery_delay_ms` | number | 上电恢复延迟（毫秒） |
| `auto_reconnect` | boolean | 自动重连开关 |
| `persist_last_ip` | boolean | 持久化最后IP |
| `validate_gateway` | boolean | 验证网关可达性 |
| `detect_ip_conflict` | boolean | 检测IP冲突 |

---

## 4. 设置 PoE 配置

### 请求

```http
POST /api/v1/system/network/poe/config
Content-Type: application/json
```

### 请求体

```json
{
  "ip_mode": "static",
  "ip_address": "192.168.60.200",
  "netmask": "255.255.255.0",
  "gateway": "192.168.60.1",
  "dns_primary": "8.8.8.8",
  "dns_secondary": "223.5.5.5",
  "hostname": "my-device",
  "dhcp_timeout_ms": 30000,
  "dhcp_retry_count": 3,
  "auto_reconnect": true,
  "validate_gateway": true,
  "detect_ip_conflict": true
}
```

> **注意**: 所有字段均为可选，只传递需要修改的字段即可。未传递的字段保持原值。

### 请求字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `ip_mode` | string | 否 | `dhcp` 或 `static` |
| `ip_address` | string | 否 | 静态IPv4地址 |
| `netmask` | string | 否 | 子网掩码 |
| `gateway` | string | 否 | 默认网关 |
| `dns_primary` | string | 否 | 主DNS服务器 |
| `dns_secondary` | string | 否 | 备用DNS服务器 |
| `hostname` | string | 否 | 主机名（最大31字符） |
| `dhcp_timeout_ms` | number | 否 | DHCP超时（毫秒） |
| `dhcp_retry_count` | number | 否 | DHCP重试次数 |
| `auto_reconnect` | boolean | 否 | 自动重连开关 |
| `validate_gateway` | boolean | 否 | 验证网关可达性 |
| `detect_ip_conflict` | boolean | 否 | 检测IP冲突 |

### 成功响应

```json
{
  "success": true,
  "message": "PoE config operation completed",
  "data": {
    "message": "PoE configuration updated successfully",
    "ip_mode": "static"
  }
}
```

---

## 5. 验证静态IP配置

### 请求

```http
POST /api/v1/system/network/poe/validate
Content-Type: application/json
```

### 请求体

```json
{
  "ip_address": "192.168.60.200",
  "netmask": "255.255.255.0",
  "gateway": "192.168.60.1",
  "dns_primary": "8.8.8.8",
  "dns_secondary": "223.5.5.5",
  "hostname": "my-device",
  "check_gateway": true,
  "check_conflict": true
}
```

### 请求字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `ip_address` | string | 是 | 待验证的IPv4地址 |
| `netmask` | string | 是 | 子网掩码 |
| `gateway` | string | 是 | 默认网关 |
| `dns_primary` | string | 否 | 主DNS服务器 |
| `dns_secondary` | string | 否 | 备用DNS服务器 |
| `hostname` | string | 否 | 主机名 |
| `check_gateway` | boolean | 否 | 是否检测网关可达性 |
| `check_conflict` | boolean | 否 | 是否检测IP冲突 |

### 成功响应（验证通过）

```json
{
  "success": true,
  "message": "PoE configuration validation completed",
  "data": {
    "valid": true,
    "errors": [],
    "warnings": [],
    "gateway_reachable": true,
    "ip_conflict": false
  }
}
```

### 验证失败的响应

```json
{
  "success": true,
  "message": "PoE configuration validation completed",
  "data": {
    "valid": false,
    "errors": [
      "Invalid IP address format",
      "IP address conflict detected"
    ],
    "warnings": [
      "Gateway may not be reachable"
    ],
    "gateway_reachable": false,
    "ip_conflict": true
  }
}
```

### data 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `valid` | boolean | 配置是否有效 |
| `errors` | array | 错误信息数组 |
| `warnings` | array | 警告信息数组 |
| `gateway_reachable` | boolean | 网关是否可达 |
| `ip_conflict` | boolean | 是否存在IP冲突 |

---

## 6. 应用 PoE 配置

应用当前保存的配置并连接网络。

### 请求

```http
POST /api/v1/system/network/poe/apply
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE configuration applied successfully",
  "data": {
    "success": true,
    "status": "Connected",
    "ip_mode": "dhcp"
  }
}
```

### 失败响应

```json
{
  "success": false,
  "error_code": "INTERNAL_ERROR",
  "message": "Failed to apply PoE configuration"
}
```

### data 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | boolean | 操作是否成功 |
| `status` | string | 当前网络状态 |
| `ip_mode` | string | 当前IP模式 |

---

## 7. 保存 PoE 配置

将当前配置保存到持久化存储（NVS），设备重启后自动恢复。

### 请求

```http
POST /api/v1/system/network/poe/save
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE configuration saved successfully",
  "data": {
    "success": true
  }
}
```

### 失败响应

```json
{
  "success": false,
  "error_code": "INTERNAL_ERROR",
  "message": "Failed to save PoE configuration"
}
```

---

## 8. 连接 PoE 网络

### 请求

```http
POST /api/v1/system/network/poe/connect
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE connection initiated",
  "data": {
    "success": true,
    "status": "Connecting"
  }
}
```

### 失败响应

```json
{
  "success": false,
  "error_code": "INTERNAL_ERROR",
  "message": "Failed to connect PoE network"
}
```

---

## 9. 断开 PoE 网络

### 请求

```http
POST /api/v1/system/network/poe/disconnect
```

### 成功响应

```json
{
  "success": true,
  "message": "PoE disconnected successfully",
  "data": {
    "success": true,
    "status": "Disconnected"
  }
}
```

### 失败响应

```json
{
  "success": false,
  "error_code": "INTERNAL_ERROR",
  "message": "Failed to disconnect PoE network"
}
```

---

## 通用错误响应

### 服务未运行

```json
{
  "success": false,
  "error_code": "SERVICE_UNAVAILABLE",
  "message": "Communication service is not running"
}
```

### PoE 模块不可用

```json
{
  "success": false,
  "error_code": "SERVICE_UNAVAILABLE",
  "message": "PoE/Ethernet module not available"
}
```

### 请求方法错误

```json
{
  "success": false,
  "error_code": "METHOD_NOT_ALLOWED",
  "message": "Only GET method is allowed"
}
```

### 请求体解析错误

```json
{
  "success": false,
  "error_code": "INVALID_REQUEST",
  "message": "Invalid JSON request body"
}
```

---

## 前端集成示例

### TypeScript 类型定义

```typescript
// 统一响应类型
interface ApiResponse<T = any> {
  success: boolean;
  error_code?: string;
  message?: string;
  data?: T;
}

// PoE状态
interface PoeStatus {
  available: boolean;
  status: string;
  ip_address?: string;
  connected?: boolean;
  message?: string;
}

// PoE详细信息
interface PoeDetailInfo {
  available: boolean;
  network_status: string;
  status_code: number;
  status_message: string;
  ip_mode: 'dhcp' | 'static';
  ip_address: string;
  netmask: string;
  gateway: string;
  dns_primary: string;
  dns_secondary: string;
  hostname: string;
  mac_address: string;
  interface_name: string;
  link_up: boolean;
  poe_powered: boolean;
  connection_duration_sec: number;
  connection_start_time: number;
  dhcp_lease_time: number;
  dhcp_lease_remaining: number;
  connect_count: number;
  disconnect_count: number;
  dhcp_fail_count: number;
  last_error_code: number;
}

// PoE配置
interface PoeConfig {
  ip_mode: 'dhcp' | 'static';
  ip_address: string;
  netmask: string;
  gateway: string;
  dns_primary: string;
  dns_secondary: string;
  hostname: string;
  dhcp_timeout_ms: number;
  dhcp_retry_count: number;
  dhcp_retry_interval_ms: number;
  power_recovery_delay_ms: number;
  auto_reconnect: boolean;
  persist_last_ip: boolean;
  validate_gateway: boolean;
  detect_ip_conflict: boolean;
}

// 验证结果
interface ValidationResult {
  valid: boolean;
  errors: string[];
  warnings: string[];
  gateway_reachable: boolean;
  ip_conflict: boolean;
}
```

### 封装请求函数

```typescript
const API_BASE = '/api/v1/system/network/poe';

async function request<T>(
  endpoint: string,
  options: RequestInit = {}
): Promise<ApiResponse<T>> {
  const response = await fetch(`${API_BASE}${endpoint}`, {
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${getToken()}`
    },
    ...options
  });
  return response.json();
}

// API函数
export const poeApi = {
  // 获取状态
  getStatus: () => request<PoeStatus>('/status'),

  // 获取详细信息
  getInfo: () => request<PoeDetailInfo>('/info'),

  // 获取配置
  getConfig: () => request<PoeConfig>('/config'),

  // 设置配置
  setConfig: (config: Partial<PoeConfig>) =>
    request<{ message: string; ip_mode: string }>('/config', {
      method: 'POST',
      body: JSON.stringify(config)
    }),

  // 验证配置
  validate: (config: {
    ip_address: string;
    netmask: string;
    gateway: string;
    dns_primary?: string;
    dns_secondary?: string;
    hostname?: string;
    check_gateway?: boolean;
    check_conflict?: boolean;
  }) => request<ValidationResult>('/validate', {
    method: 'POST',
    body: JSON.stringify(config)
  }),

  // 应用配置
  apply: () => request<{ success: boolean; status: string; ip_mode: string }>('/apply', {
    method: 'POST'
  }),

  // 保存配置
  save: () => request<{ success: boolean }>('/save', {
    method: 'POST'
  }),

  // 连接
  connect: () => request<{ success: boolean; status: string }>('/connect', {
    method: 'POST'
  }),

  // 断开
  disconnect: () => request<{ success: boolean; status: string }>('/disconnect', {
    method: 'POST'
  })
};
```

### 使用示例

```typescript
// 获取PoE详细信息
async function loadPoeInfo() {
  const res = await poeApi.getInfo();
  if (res.success && res.data) {
    console.log('IP地址:', res.data.ip_address);
    console.log('连接状态:', res.data.network_status);
    console.log('PoE供电:', res.data.poe_powered);
  } else {
    console.error('获取失败:', res.message);
  }
}

// 设置静态IP并保存
async function setStaticIP(config: {
  ip: string;
  netmask: string;
  gateway: string;
  dns1: string;
  dns2: string;
}) {
  // 1. 验证配置
  const validateRes = await poeApi.validate({
    ip_address: config.ip,
    netmask: config.netmask,
    gateway: config.gateway,
    dns_primary: config.dns1,
    dns_secondary: config.dns2,
    check_gateway: true,
    check_conflict: true
  });

  if (!validateRes.success) {
    throw new Error(validateRes.message || '验证请求失败');
  }

  if (!validateRes.data?.valid) {
    throw new Error(validateRes.data?.errors.join(', ') || '配置无效');
  }

  // 2. 设置配置
  const setRes = await poeApi.setConfig({
    ip_mode: 'static',
    ip_address: config.ip,
    netmask: config.netmask,
    gateway: config.gateway,
    dns_primary: config.dns1,
    dns_secondary: config.dns2
  });

  if (!setRes.success) {
    throw new Error(setRes.message || '设置配置失败');
  }

  // 3. 应用配置
  const applyRes = await poeApi.apply();
  if (!applyRes.success || !applyRes.data?.success) {
    throw new Error(applyRes.message || '应用配置失败');
  }

  // 4. 保存到持久化存储
  const saveRes = await poeApi.save();
  if (!saveRes.success || !saveRes.data?.success) {
    console.warn('保存到NVS失败，重启后配置可能丢失');
  }

  return applyRes.data;
}

// 切换到DHCP模式
async function switchToDHCP() {
  const setRes = await poeApi.setConfig({ ip_mode: 'dhcp' });
  if (!setRes.success) throw new Error(setRes.message);

  const applyRes = await poeApi.apply();
  if (!applyRes.success) throw new Error(applyRes.message);

  const saveRes = await poeApi.save();
  if (!saveRes.success) console.warn('保存失败');
}
```

---

## 典型工作流程

### 1. 页面加载时

```
GET /poe/info     → 获取当前状态和配置
GET /poe/config   → 获取完整配置（用于表单回填）
```

### 2. 用户修改静态IP配置

```
POST /poe/validate  → 验证配置有效性（可选）
POST /poe/config    → 保存配置到内存
POST /poe/apply     → 应用配置
POST /poe/save      → 持久化到NVS
```

### 3. 用户切换DHCP/静态模式

```
POST /poe/config { "ip_mode": "dhcp" }
POST /poe/apply
POST /poe/save
```

### 4. 轮询状态更新

```
GET /poe/status   → 轻量级状态查询（建议5秒间隔）
GET /poe/info     → 完整信息查询（仅在需要详细信息时）
```

---

## 版本信息

- API版本: v1
- 文档更新日期: 2024-12-23
