---
title: Webhook Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# Webhook Endpoints

Event callback notification configuration

Source: [`Custom/Services/Web/api/api_webhook_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_webhook_module.c)

**6** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/apps/webhook/config` | ✅ | `webhook_config_get_handler` |
| `POST` | `/api/v1/apps/webhook/config` | ✅ | `webhook_config_set_handler` |
| `POST` | `/api/v1/apps/webhook/test` | ✅ | `webhook_test_handler` |
| `GET` | `/api/v1/apps/webhook/ca-cert` | ✅ | `webhook_ca_cert_get_handler` |
| `POST` | `/api/v1/apps/webhook/ca-cert` | ✅ | `webhook_ca_cert_set_handler` |
| `DELETE` | `/api/v1/apps/webhook/ca-cert` | ✅ | `webhook_ca_cert_delete_handler` |
