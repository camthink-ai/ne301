---
title: RTMP Streaming Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# RTMP Streaming Endpoints

RTMP stream configuration and control

Source: [`Custom/Services/Web/api/api_rtmp_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_rtmp_module.c)

**5** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/apps/rtmp/config` | ✅ | `rtmp_config_get_handler` |
| `POST` | `/api/v1/apps/rtmp/config` | ✅ | `rtmp_config_set_handler` |
| `POST` | `/api/v1/apps/rtmp/start` | ✅ | `rtmp_start_handler` |
| `POST` | `/api/v1/apps/rtmp/stop` | ✅ | `rtmp_stop_handler` |
| `GET` | `/api/v1/apps/rtmp/status` | ✅ | `rtmp_status_handler` |
