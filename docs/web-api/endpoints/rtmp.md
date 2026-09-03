---
title: RTMP 推流 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# RTMP 推流 端点参考

RTMP 推流配置与控制

源文件: [`Custom/Services/Web/api/api_rtmp_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_rtmp_module.c)

共 **5** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/apps/rtmp/config` | ✅ | `rtmp_config_get_handler` |
| `POST` | `/api/v1/apps/rtmp/config` | ✅ | `rtmp_config_set_handler` |
| `POST` | `/api/v1/apps/rtmp/start` | ✅ | `rtmp_start_handler` |
| `POST` | `/api/v1/apps/rtmp/stop` | ✅ | `rtmp_stop_handler` |
| `GET` | `/api/v1/apps/rtmp/status` | ✅ | `rtmp_status_handler` |
