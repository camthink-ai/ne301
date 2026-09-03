---
title: OTA Upgrade Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# OTA Upgrade Endpoints

Firmware over-the-air upgrade

Source: [`Custom/Services/Web/api/api_ota_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_ota_module.c)

**4** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `POST` | `/api/v1/system/ota/precheck` | ✅ | `ota_precheck_handler` |
| `POST` | `/api/v1/system/ota/upload` | ✅ | `ota_upload_handler` |
| `POST` | `/api/v1/system/ota/upgrade-local` | ✅ | `ota_upgrade_local_handler` |
| `POST` | `/api/v1/system/ota/export` | ✅ | `ota_export_firmware_handler` |
