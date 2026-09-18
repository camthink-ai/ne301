---
title: File Management Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# File Management Endpoints

File browser and file transfer

Source: [`Custom/Services/Web/api/api_file_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_file_module.c)

**8** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/files/list` | ✅ | `file_list_handler` |
| `GET` | `/api/v1/files/download` | ✅ | `file_download_handler` |
| `POST` | `/api/v1/files/upload` | ✅ | `file_upload_handler` |
| `DELETE` | `/api/v1/files` | ✅ | `file_delete_handler` |
| `PUT` | `/api/v1/files/rename` | ✅ | `file_rename_handler` |
| `GET` | `/api/v1/files/preview` | ✅ | `file_preview_handler` |
| `PUT` | `/api/v1/files/edit` | ✅ | `file_edit_handler` |
| `POST` | `/api/v1/files/create` | ✅ | `file_create_handler` |
