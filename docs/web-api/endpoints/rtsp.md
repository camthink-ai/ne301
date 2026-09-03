---
title: RTSP Streaming Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# RTSP Streaming Endpoints

RTSP pull-stream service management

Source: [`Custom/Services/Web/api/api_rtsp_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_rtsp_module.c)

**5** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/apps/rtsp/config` | ✅ | `rtsp_config_get_handler` |
| `PUT` | `/api/v1/apps/rtsp/config` | ✅ | `rtsp_config_set_handler` |
| `GET` | `/api/v1/apps/rtsp/status` | ✅ | `rtsp_status_handler` |
| `GET` | `/api/v1/apps/rtsp/clients` | ✅ | `rtsp_clients_get_handler` |
| `DELETE` | `/api/v1/apps/rtsp/clients/*` | ✅ | `rtsp_client_kick_handler` |
