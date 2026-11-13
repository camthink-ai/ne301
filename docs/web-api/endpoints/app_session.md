---
title: app_session Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# app_session Endpoints



Source: [`Custom/Services/Web/api/api_app_session_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_session_module.c)

**3** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/app/session/start` | ✅ | `app_session_start_handler` |
| `POST` | `/api/v1/app/session/stop` | ✅ | `app_session_stop_handler` |
| `GET` | `/api/v1/app/session/status` | ✅ | `app_session_status_handler` |
