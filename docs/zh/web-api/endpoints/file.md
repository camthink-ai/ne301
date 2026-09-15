---
title: 文件管理 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 文件管理 端点参考

文件浏览器与文件传输

源文件: [`Custom/Services/Web/api/api_file_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_file_module.c)

共 **8** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/files/list` | ✅ | `file_list_handler` |
| `GET` | `/api/v1/files/download` | ✅ | `file_download_handler` |
| `POST` | `/api/v1/files/upload` | ✅ | `file_upload_handler` |
| `DELETE` | `/api/v1/files` | ✅ | `file_delete_handler` |
| `PUT` | `/api/v1/files/rename` | ✅ | `file_rename_handler` |
| `GET` | `/api/v1/files/preview` | ✅ | `file_preview_handler` |
| `PUT` | `/api/v1/files/edit` | ✅ | `file_edit_handler` |
| `POST` | `/api/v1/files/create` | ✅ | `file_create_handler` |
