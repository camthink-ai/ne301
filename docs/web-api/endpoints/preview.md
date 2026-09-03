---
title: 预览流 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 预览流 端点参考

摄像头实时预览控制

源文件: [`Custom/Services/Web/api/api_preview_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_preview_module.c)

共 **3** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/preview/status` | — | `preview_status_handler` |
| `POST` | `/api/v1/preview/start` | — | `preview_start_handler` |
| `POST` | `/api/v1/preview/stop` | — | `preview_stop_handler` |
