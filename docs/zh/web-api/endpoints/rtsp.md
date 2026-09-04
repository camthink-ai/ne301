---
title: RTSP 流 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# RTSP 流 端点参考

RTSP 拉流服务管理

源文件: [`Custom/Services/Web/api/api_rtsp_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_rtsp_module.c)

共 **5** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/apps/rtsp/config` | ✅ | `rtsp_config_get_handler` |
| `PUT` | `/api/v1/apps/rtsp/config` | ✅ | `rtsp_config_set_handler` |
| `GET` | `/api/v1/apps/rtsp/status` | ✅ | `rtsp_status_handler` |
| `GET` | `/api/v1/apps/rtsp/clients` | ✅ | `rtsp_clients_get_handler` |
| `DELETE` | `/api/v1/apps/rtsp/clients/*` | ✅ | `rtsp_client_kick_handler` |
