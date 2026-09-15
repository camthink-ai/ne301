---
title: Capture & Upload Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# Capture & Upload Endpoints

Capture tasks, upload queue and records

Source: [`Custom/Services/Web/api/api_capture_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_capture_module.c)

**7** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/capture/upload-config` | ✅ | `upload_config_handler` |
| `POST` | `/api/v1/capture/upload-config` | ✅ | `upload_config_handler` |
| `GET` | `/api/v1/capture/queue` | ✅ | `queue_handler` |
| `GET` | `/api/v1/capture/records` | ✅ | `records_handler` |
| `DELETE` | `/api/v1/capture/records` | ✅ | `records_handler` |
| `POST` | `/api/v1/capture/records/retry` | ✅ | `records_retry_handler` |
| `POST` | `/api/v1/capture/records/delete` | ✅ | `records_delete_batch_handler` |
