---
title: 设备管理 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# 设备管理 端点参考

设备信息、时间、日志与维护操作

源文件: [`Custom/Services/Web/api/api_device_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_device_module.c)

共 **23** 个端点。鉴权列 ✅ 表示需要携带 [认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
| `GET` | `/api/v1/device/info` | ✅ | `device_info_handler` |
| `GET` | `/api/v1/device/firmware-versions` | ✅ | `firmware_versions_handler` |
| `GET` | `/api/v1/device/storage` | ✅ | `device_storage_handler` |
| `POST` | `/api/v1/device/storage/config` | ✅ | `device_storage_config_handler` |
| `GET` | `/api/v1/device/image/config` | ✅ | `device_image_config_handler` |
| `POST` | `/api/v1/device/image/config` | ✅ | `device_image_config_handler` |
| `GET` | `/api/v1/device/sys-clk/config` | ✅ | `device_sys_clk_config_handler` |
| `POST` | `/api/v1/device/sys-clk/config` | ✅ | `device_sys_clk_config_handler` |
| `GET` | `/api/v1/device/light/config` | ✅ | `device_light_config_handler` |
| `POST` | `/api/v1/device/light/config` | ✅ | `device_light_config_handler` |
| `POST` | `/api/v1/device/light/control` | ✅ | `device_light_control_handler` |
| `GET` | `/api/v1/device/camera/config` | ✅ | `device_camera_config_handler` |
| `POST` | `/api/v1/device/camera/config` | ✅ | `device_camera_config_handler` |
| `POST` | `/api/v1/system/time` | ✅ | `system_time_handler` |
| `POST` | `/api/v1/device/name` | ✅ | `device_name_handler` |
| `GET` | `/api/v1/system/logs` | ✅ | `system_logs_handler` |
| `GET` | `/api/v1/system/logs/export` | ✅ | `system_logs_export_handler` |
| `POST` | `/api/v1/system/restart` | ✅ | `system_restart_handler` |
| `POST` | `/api/v1/system/factory-reset` | ✅ | `system_factory_reset_handler` |
| `GET` | `/api/v1/device/config/export` | ✅ | `device_config_export_handler` |
| `POST` | `/api/v1/device/config/import` | ✅ | `device_config_import_handler` |
| `GET` | `/api/v1/device/preference/stream_tab` | ✅ | `device_pref_stream_tab_handler` |
| `POST` | `/api/v1/device/preference/stream_tab` | ✅ | `device_pref_stream_tab_handler` |
