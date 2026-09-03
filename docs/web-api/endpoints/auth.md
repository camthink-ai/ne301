---
title: 认证登录 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 认证登录 端点参考

设备登录与密码管理

源文件: [`Custom/Services/Web/api/api_auth_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_auth_module.c)

共 **2** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `POST` | `/api/v1/login` | — | `login_handler` |
| `POST` | `/api/v1/change-password` | ✅ | `change_password_handler` |
