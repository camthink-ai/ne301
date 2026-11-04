---
title: app_session 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# app_session 端点参考



源文件: [`Custom/Services/Web/api/api_app_session_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_session_module.c)

共 **9** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `POST` | `/api/v1/app/session/start` | ✅ | `app_session_start_handler` |
| `POST` | `/api/v1/app/session/stop` | ✅ | `app_session_stop_handler` |
| `GET` | `/api/v1/app/session/status` | ✅ | `app_session_status_handler` |
| `GET` | `/api/v1/app/report/service` | ✅ | `app_report_service_handler` |
| `GET` | `/api/v1/app/sidecar/ui` | ✅ | `app_sidecar_ui_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/grant` | ✅ | `app_bridge_grant_handler` |
| `POST` | `/api/v1/app/sidecar/bridge` | ✅ | `app_bridge_call_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/confirm` | ✅ | `app_bridge_confirm_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/revoke` | ✅ | `app_bridge_revoke_handler` |
