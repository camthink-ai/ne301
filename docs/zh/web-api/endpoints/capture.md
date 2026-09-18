---
title: 抓拍与上传 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 抓拍与上传 端点参考

抓拍任务、上传队列与记录管理

源文件: [`Custom/Services/Web/api/api_capture_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_capture_module.c)

共 **7** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/capture/upload-config` | ✅ | `upload_config_handler` |
| `POST` | `/api/v1/capture/upload-config` | ✅ | `upload_config_handler` |
| `GET` | `/api/v1/capture/queue` | ✅ | `queue_handler` |
| `GET` | `/api/v1/capture/records` | ✅ | `records_handler` |
| `DELETE` | `/api/v1/capture/records` | ✅ | `records_handler` |
| `POST` | `/api/v1/capture/records/retry` | ✅ | `records_retry_handler` |
| `POST` | `/api/v1/capture/records/delete` | ✅ | `records_delete_batch_handler` |
