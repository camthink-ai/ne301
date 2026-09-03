# 认证与快速开始

## 认证模型

NE301 使用单用户（`admin`）模型：

- **`POST /api/v1/login`**：前端管理界面使用的登录接口，请求体为 `{"password": "..."}`，校验通过仅表示密码正确（用于前端路由判断），不会签发 token。
- **HTTP Basic Auth**：所有 `require_auth = true` 的接口在请求头中直接校验凭据：

```http
Authorization: Basic <base64("admin:<password>")>
```

用户名固定为 `admin`（见 `Custom/Core/Security/auth_mgr.h` 中的 `AUTH_ADMIN_USERNAME`），密码由 `auth_mgr` 管理，可通过 [`POST /api/v1/change-password`](./endpoints/auth.md) 修改（长度 8–32 位）。

::: warning 注意
Basic Auth 每次请求都携带明文 base64 编码的密码，请确保设备仅运行在可信网络中，或通过反向代理 / TLS 保护。
:::

## curl 调用示例

```bash
# 登录（前端页面判断密码是否正确）
curl -X POST http://192.168.1.100/api/v1/login \
  -H "Content-Type: application/json" \
  -d '{"password": "admin12345"}'

# 查询设备信息（需 Basic Auth）
curl -u admin:admin12345 http://192.168.1.100/api/v1/device/info

# WiFi 状态
curl -u admin:admin12345 http://192.168.1.100/api/v1/system/network/wifi/sta
```

## JavaScript / fetch 调用示例

```js
const BASE = 'http://192.168.1.100/api/v1'
const AUTH = 'Basic ' + btoa('admin:admin12345')

// 统一请求封装：自动带凭据、统一处理业务错误码
async function api(path, options = {}) {
  const res = await fetch(BASE + path, {
    ...options,
    headers: {
      'Authorization': AUTH,
      'Content-Type': 'application/json',
      ...options.headers,
    },
  })
  const body = await res.json()
  if (body.code !== 0) {
    throw new Error(`[${body.code}] ${body.message}`)
  }
  return body.data
}

// 示例：获取网络状态
const status = await api('/system/network/status')
console.log(status.current_comm_type, status.has_connection)
```

## Python 调用示例

```python
import requests

BASE = "http://192.168.1.100/api/v1"
auth = ("admin", "admin12345")

resp = requests.get(f"{BASE}/device/info", auth=auth, timeout=5)
body = resp.json()
if body["code"] != 0:
    raise RuntimeError(f"[{body['code']}] {body['message']}")
print(body["data"])
```

## 常见认证错误

| HTTP 状态 | 业务码 | 含义 | 处理建议 |
|-----------|-------|------|---------|
| 401 | — | 缺少 `Authorization` 头或凭据错误 | 检查用户名是否为 `admin`、密码是否正确 |
| 200 | 1001 | 登录密码错误（`/login`） | 确认密码 |
| 200 | 1004 / 1005 | 会话过期 / Token 无效 | 重新登录 |

## 下一步

- 浏览全部[端点参考](./endpoints/)
- 阅读[网络管理模块详解](./network)
