# Authentication & Quick Start

## Auth Model

NE301 uses a single-user (`admin`) model:

- **`POST /api/v1/login`**: the login endpoint used by the web UI. The request body is `{"password": "..."}`; a successful response only means the password is correct (used for frontend routing) — no token is issued.
- **HTTP Basic Auth**: every endpoint with `require_auth = true` validates credentials directly from the request header:

```http
Authorization: Basic <base64("admin:<password>")>
```

The username is fixed to `admin` (see `AUTH_ADMIN_USERNAME` in `Custom/Core/Security/auth_mgr.h`); the password is managed by `auth_mgr` and can be changed via [`POST /api/v1/change-password`](./endpoints/auth.md) (8–32 characters).

::: warning
Basic Auth sends the base64-encoded password on every request. Keep the device on a trusted network, or protect it with a reverse proxy / TLS.
:::

## curl Examples

```bash
# Login (lets the web UI check the password)
curl -X POST http://192.168.1.100/api/v1/login \
  -H "Content-Type: application/json" \
  -d '{"password": "admin12345"}'

# Device info (requires Basic Auth)
curl -u admin:admin12345 http://192.168.1.100/api/v1/device/info

# WiFi status
curl -u admin:admin12345 http://192.168.1.100/api/v1/system/network/wifi/sta
```

## JavaScript / fetch Example

```js
const BASE = 'http://192.168.1.100/api/v1'
const AUTH = 'Basic ' + btoa('admin:admin12345')

// Unified wrapper: attaches credentials and maps business error codes
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

// Example: network status
const status = await api('/system/network/status')
console.log(status.current_comm_type, status.has_connection)
```

## Python Example

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

## Common Auth Errors

| HTTP status | Business code | Meaning | Suggestion |
|-------------|---------------|---------|------------|
| 401 | — | Missing `Authorization` header or bad credentials | Check the username is `admin` and the password is correct |
| 200 | 1001 | Wrong login password (`/login`) | Verify the password |
| 200 | 1004 / 1005 | Session expired / invalid token | Log in again |

## Next Steps

- Browse the full [endpoint reference](./endpoints/)
- Read the [Network Management module guide](./network)
