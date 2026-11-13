---
title: app_sidecar_bridge Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# app_sidecar_bridge Endpoints



Source: [`Custom/Services/Web/api/api_app_sidecar_bridge_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_sidecar_bridge_module.c)

**5** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/app/sidecar/ui` | ✅ | `app_sidecar_ui_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/grant` | ✅ | `app_bridge_grant_handler` |
| `POST` | `/api/v1/app/sidecar/bridge` | ✅ | `app_bridge_call_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/confirm` | ✅ | `app_bridge_confirm_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/revoke` | ✅ | `app_bridge_revoke_handler` |
