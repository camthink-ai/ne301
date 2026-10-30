---
title: app_install Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# app_install Endpoints



Source: [`Custom/Services/Web/api/api_app_install_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_install_module.c)

**4** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/app/install` | ✅ | `app_install_package_handler` |
| `GET` | `/api/v1/app/installed` | ✅ | `app_install_state_handler` |
| `DELETE` | `/api/v1/app/installed` | ✅ | `app_install_remove_handler` |
| `POST` | `/api/v1/app/run` | ✅ | `app_install_run_handler` |
