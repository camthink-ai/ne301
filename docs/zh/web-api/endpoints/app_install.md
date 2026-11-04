---
title: app_install 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# app_install 端点参考



源文件: [`Custom/Services/Web/api/api_app_install_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_app_install_module.c)

共 **4** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `POST` | `/api/v1/app/install` | ✅ | `app_install_package_handler` |
| `GET` | `/api/v1/app/installed` | ✅ | `app_install_state_handler` |
| `DELETE` | `/api/v1/app/installed` | ✅ | `app_install_remove_handler` |
| `POST` | `/api/v1/app/run` | ✅ | `app_install_run_handler` |
