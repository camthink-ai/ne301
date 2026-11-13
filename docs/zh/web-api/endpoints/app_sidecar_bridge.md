---
title: app_sidecar_bridge 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# app_sidecar_bridge 端点参考



源文件: [`Custom/Services/Web/api/api_app_sidecar_bridge_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_sidecar_bridge_module.c)

共 **5** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/app/sidecar/ui` | ✅ | `app_sidecar_ui_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/grant` | ✅ | `app_bridge_grant_handler` |
| `POST` | `/api/v1/app/sidecar/bridge` | ✅ | `app_bridge_call_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/confirm` | ✅ | `app_bridge_confirm_handler` |
| `POST` | `/api/v1/app/sidecar/bridge/revoke` | ✅ | `app_bridge_revoke_handler` |
