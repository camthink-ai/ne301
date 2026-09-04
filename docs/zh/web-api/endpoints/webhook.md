---
title: Webhook 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# Webhook 端点参考

事件回调通知配置

源文件: [`Custom/Services/Web/api/api_webhook_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_webhook_module.c)

共 **6** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/apps/webhook/config` | ✅ | `webhook_config_get_handler` |
| `POST` | `/api/v1/apps/webhook/config` | ✅ | `webhook_config_set_handler` |
| `POST` | `/api/v1/apps/webhook/test` | ✅ | `webhook_test_handler` |
| `GET` | `/api/v1/apps/webhook/ca-cert` | ✅ | `webhook_ca_cert_get_handler` |
| `POST` | `/api/v1/apps/webhook/ca-cert` | ✅ | `webhook_ca_cert_set_handler` |
| `DELETE` | `/api/v1/apps/webhook/ca-cert` | ✅ | `webhook_ca_cert_delete_handler` |
