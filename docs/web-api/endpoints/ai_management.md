---
title: AI Model Management Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# AI Model Management Endpoints

Upload, switch and configure AI models

Source: [`Custom/Services/Web/api/api_ai_management_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_ai_management_module.c)

**6** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/ai/status` | ✅ | `ai_management_status_handler` |
| `POST` | `/api/v1/ai/toggle` | ✅ | `ai_management_switch_inference_handler` |
| `POST` | `/api/v1/ai/pipeline/start` | ✅ | `ai_management_start_pipeline_handler` |
| `POST` | `/api/v1/ai/pipeline/stop` | ✅ | `ai_management_stop_pipeline_handler` |
| `GET` | `/api/v1/ai/params` | ✅ | `ai_management_get_thresholds_handler` |
| `POST` | `/api/v1/ai/params` | ✅ | `ai_management_set_thresholds_handler` |
