---
title: OTA 升级 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# OTA 升级 端点参考

固件在线升级

源文件: [`Custom/Services/Web/api/api_ota_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_ota_module.c)

共 **4** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `POST` | `/api/v1/system/ota/precheck` | ✅ | `ota_precheck_handler` |
| `POST` | `/api/v1/system/ota/upload` | ✅ | `ota_upload_handler` |
| `POST` | `/api/v1/system/ota/upgrade-local` | ✅ | `ota_upgrade_local_handler` |
| `POST` | `/api/v1/system/ota/export` | ✅ | `ota_export_firmware_handler` |
