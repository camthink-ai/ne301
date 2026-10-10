---
title: camera 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# camera 端点参考



源文件: [`Custom/Services/Web/api/api_camera_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_camera_module.c)

共 **4** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/camera/list` | ✅ | `camera_list_handler` |
| `POST` | `/api/v1/camera/detect` | ✅ | `camera_detect_handler` |
| `GET` | `/api/v1/camera/config` | ✅ | `camera_config_get_handler` |
| `PUT` | `/api/v1/camera/config` | ✅ | `camera_config_set_handler` |
