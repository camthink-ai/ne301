---
title: 工作模式 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 工作模式 端点参考

设备工作模式与联动策略

源文件: [`Custom/Services/Web/api/api_work_mode_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_work_mode_module.c)

共 **9** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/work-mode/status` | ✅ | `work_mode_status_handler` |
| `POST` | `/api/v1/work-mode/switch` | ✅ | `work_mode_switch_handler` |
| `GET` | `/api/v1/work-mode/triggers` | ✅ | `work_mode_triggers_get_handler` |
| `POST` | `/api/v1/work-mode/triggers` | ✅ | `work_mode_triggers_set_handler` |
| `POST` | `/api/v1/work-mode/video-stream/config` | ✅ | `work_mode_video_stream_config_handler` |
| `GET` | `/api/v1/power-mode/status` | ✅ | `power_mode_status_handler` |
| `POST` | `/api/v1/power-mode/switch` | ✅ | `power_mode_switch_handler` |
| `GET` | `/api/v1/power-mode/config` | ✅ | `power_mode_config_get_handler` |
| `POST` | `/api/v1/power-mode/config` | ✅ | `power_mode_config_set_handler` |
