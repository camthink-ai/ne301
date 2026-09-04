---
title: Authentication Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# Authentication Endpoints

Device login and password management

Source: [`Custom/Services/Web/api/api_auth_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_auth_module.c)

**2** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/login` | — | `login_handler` |
| `POST` | `/api/v1/change-password` | ✅ | `change_password_handler` |
