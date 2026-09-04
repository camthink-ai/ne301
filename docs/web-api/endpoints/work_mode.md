---
title: Work Mode Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# Work Mode Endpoints

Device work mode and linkage policies

Source: [`Custom/Services/Web/api/api_work_mode_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_work_mode_module.c)

**9** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/work-mode/status` | ✅ | `work_mode_status_handler` |
| `POST` | `/api/v1/work-mode/switch` | ✅ | `work_mode_switch_handler` |
| `GET` | `/api/v1/work-mode/triggers` | ✅ | `work_mode_triggers_get_handler` |
| `POST` | `/api/v1/work-mode/triggers` | ✅ | `work_mode_triggers_set_handler` |
| `POST` | `/api/v1/work-mode/video-stream/config` | ✅ | `work_mode_video_stream_config_handler` |
| `GET` | `/api/v1/power-mode/status` | ✅ | `power_mode_status_handler` |
| `POST` | `/api/v1/power-mode/switch` | ✅ | `power_mode_switch_handler` |
| `GET` | `/api/v1/power-mode/config` | ✅ | `power_mode_config_get_handler` |
| `POST` | `/api/v1/power-mode/config` | ✅ | `power_mode_config_set_handler` |
