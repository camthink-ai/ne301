---
title: 网络管理 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 网络管理 端点参考

WiFi / 蜂窝 / PoE 网络配置与状态

源文件: [`Custom/Services/Web/api/api_network_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_network_module.c)

共 **34** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/system/network/status` | ✅ | `network_status_handler` |
| `GET` | `/api/v1/system/network/wifi/sta` | ✅ | `network_wifi_sta_handler` |
| `GET` | `/api/v1/system/network/wifi/ap` | ✅ | `network_wifi_ap_handler` |
| `POST` | `/api/v1/system/network/wifi/config` | ✅ | `network_wifi_config_handler` |
| `POST` | `/api/v1/system/network/wifi/scan` | ✅ | `network_scan_refresh_handler` |
| `POST` | `/api/v1/system/network/wifi/disconnect` | ✅ | `network_disconnect_handler` |
| `POST` | `/api/v1/system/network/wifi/delete` | ✅ | `network_delete_known_handler` |
| `POST` | `/api/v1/system/network/wifi` | ✅ | `network_wifi_config_handler` |
| `POST` | `/api/v1/system/network/scan` | ✅ | `network_scan_refresh_handler` |
| `POST` | `/api/v1/system/network/disconnect` | ✅ | `network_disconnect_handler` |
| `POST` | `/api/v1/system/network/delete` | ✅ | `network_delete_known_handler` |
| `GET` | `/api/v1/system/network/comm/types` | ✅ | `network_comm_types_handler` |
| `POST` | `/api/v1/system/network/comm/switch` | ✅ | `network_comm_switch_handler` |
| `GET` | `/api/v1/system/network/comm/prefer` | ✅ | `network_comm_prefer_handler` |
| `POST` | `/api/v1/system/network/comm/prefer` | ✅ | `network_comm_prefer_handler` |
| `POST` | `/api/v1/system/network/comm/priority` | ✅ | `network_comm_priority_handler` |
| `GET` | `/api/v1/system/network/cellular/status` | ✅ | `network_cellular_status_handler` |
| `GET` | `/api/v1/system/network/cellular/settings` | ✅ | `network_cellular_settings_handler` |
| `POST` | `/api/v1/system/network/cellular/settings` | ✅ | `network_cellular_settings_handler` |
| `POST` | `/api/v1/system/network/cellular/save` | ✅ | `network_cellular_save_handler` |
| `POST` | `/api/v1/system/network/cellular/connect` | ✅ | `network_cellular_connect_handler` |
| `POST` | `/api/v1/system/network/cellular/disconnect` | ✅ | `network_cellular_disconnect_handler` |
| `GET` | `/api/v1/system/network/cellular/info` | ✅ | `network_cellular_info_handler` |
| `POST` | `/api/v1/system/network/cellular/refresh` | ✅ | `network_cellular_refresh_handler` |
| `POST` | `/api/v1/system/network/cellular/at` | ✅ | `network_cellular_at_handler` |
| `GET` | `/api/v1/system/network/poe/status` | ✅ | `network_poe_status_handler` |
| `POST` | `/api/v1/system/network/poe/connect` | ✅ | `network_poe_connect_handler` |
| `POST` | `/api/v1/system/network/poe/disconnect` | ✅ | `network_poe_disconnect_handler` |
| `GET` | `/api/v1/system/network/poe/info` | ✅ | `network_poe_info_handler` |
| `GET` | `/api/v1/system/network/poe/config` | ✅ | `network_poe_config_handler` |
| `POST` | `/api/v1/system/network/poe/config` | ✅ | `network_poe_config_handler` |
| `POST` | `/api/v1/system/network/poe/validate` | ✅ | `network_poe_validate_handler` |
| `POST` | `/api/v1/system/network/poe/apply` | ✅ | `network_poe_apply_handler` |
| `POST` | `/api/v1/system/network/poe/save` | ✅ | `network_poe_save_handler` |
