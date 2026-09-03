---
title: MQTT 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# MQTT 端点参考

MQTT 连接配置与状态

源文件: [`Custom/Services/Web/api/api_mqtt_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_mqtt_module.c)

共 **8** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/apps/mqtt/config` | ✅ | `mqtt_config_get_handler` |
| `POST` | `/api/v1/apps/mqtt/config` | ✅ | `mqtt_config_set_handler` |
| `POST` | `/api/v1/apps/mqtt/connect` | ✅ | `mqtt_connect_handler` |
| `POST` | `/api/v1/apps/mqtt/disconnect` | ✅ | `mqtt_disconnect_handler` |
| `POST` | `/api/v1/apps/mqtt/publish/data` | ✅ | `mqtt_publish_data_handler` |
| `POST` | `/api/v1/apps/mqtt/publish/status` | ✅ | `mqtt_publish_status_handler` |
| `POST` | `/api/v1/apps/mqtt/publish/json` | ✅ | `mqtt_publish_data_json_handler` |
| `POST` | `/api/v1/device/capture` | ✅ | `mqtt_capture_handler` |
