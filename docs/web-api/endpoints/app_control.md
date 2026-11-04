---
title: app_control Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# app_control Endpoints



Source: [`Custom/Services/Web/api/api_app_control_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_control_module.c)

**4** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/app/control/config` | ✅ | `app_control_config_handler` |
| `POST` | `/api/v1/app/control/reset` | ✅ | `app_control_reset_handler` |
| `GET` | `/api/v1/app/control/status` | ✅ | `app_control_status_handler` |
| `GET` | `/api/v1/app/business/snapshot` | ✅ | `app_control_snapshot_handler` |
