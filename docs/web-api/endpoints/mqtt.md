---
title: MQTT Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# MQTT Endpoints

MQTT connection configuration and status

Source: [`Custom/Services/Web/api/api_mqtt_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_mqtt_module.c)

**8** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/apps/mqtt/config` | ✅ | `mqtt_config_get_handler` |
| `POST` | `/api/v1/apps/mqtt/config` | ✅ | `mqtt_config_set_handler` |
| `POST` | `/api/v1/apps/mqtt/connect` | ✅ | `mqtt_connect_handler` |
| `POST` | `/api/v1/apps/mqtt/disconnect` | ✅ | `mqtt_disconnect_handler` |
| `POST` | `/api/v1/apps/mqtt/publish/data` | ✅ | `mqtt_publish_data_handler` |
| `POST` | `/api/v1/apps/mqtt/publish/status` | ✅ | `mqtt_publish_status_handler` |
| `POST` | `/api/v1/apps/mqtt/publish/json` | ✅ | `mqtt_publish_data_json_handler` |
| `POST` | `/api/v1/device/capture` | ✅ | `mqtt_capture_handler` |
