---
title: 模型校验 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 模型校验 端点参考

AI 模型包的校验与验证

源文件: [`Custom/Services/Web/api/api_model_validation_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_model_validation_module.c)

共 **2** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `POST` | `/api/v1/model/validation/upload` | ✅ | `model_validation_upload_handler` |
| `POST` | `/api/v1/model/reload` | ✅ | `model_reload_handler` |
