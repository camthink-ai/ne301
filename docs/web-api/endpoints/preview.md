---
title: Live Preview Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# Live Preview Endpoints

Camera live preview control

Source: [`Custom/Services/Web/api/api_preview_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_preview_module.c)

**3** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/preview/status` | — | `preview_status_handler` |
| `POST` | `/api/v1/preview/start` | — | `preview_start_handler` |
| `POST` | `/api/v1/preview/stop` | — | `preview_stop_handler` |
