---
title: AI 模型管理 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# AI 模型管理 端点参考

AI 模型的上传、切换与推理配置

源文件: [`Custom/Services/Web/api/api_ai_management_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_ai_management_module.c)

共 **6** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/ai/status` | ✅ | `ai_management_status_handler` |
| `POST` | `/api/v1/ai/toggle` | ✅ | `ai_management_switch_inference_handler` |
| `POST` | `/api/v1/ai/pipeline/start` | ✅ | `ai_management_start_pipeline_handler` |
| `POST` | `/api/v1/ai/pipeline/stop` | ✅ | `ai_management_stop_pipeline_handler` |
| `GET` | `/api/v1/ai/params` | ✅ | `ai_management_get_thresholds_handler` |
| `POST` | `/api/v1/ai/params` | ✅ | `ai_management_set_thresholds_handler` |
