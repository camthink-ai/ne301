---
title: Model Validation Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# Model Validation Endpoints

AI model package validation

Source: [`Custom/Services/Web/api/api_model_validation_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_model_validation_module.c)

**2** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/model/validation/upload` | ✅ | `model_validation_upload_handler` |
| `POST` | `/api/v1/model/reload` | ✅ | `model_reload_handler` |
