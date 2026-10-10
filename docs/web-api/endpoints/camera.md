---
title: camera Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# camera Endpoints



Source: [`Custom/Services/Web/api/api_camera_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_camera_module.c)

**4** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/camera/list` | ✅ | `camera_list_handler` |
| `POST` | `/api/v1/camera/detect` | ✅ | `camera_detect_handler` |
| `GET` | `/api/v1/camera/config` | ✅ | `camera_config_get_handler` |
| `PUT` | `/api/v1/camera/config` | ✅ | `camera_config_set_handler` |
