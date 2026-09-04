---
title: ISP Tuning Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# ISP Tuning Endpoints

Image quality parameter get/set

Source: [`Custom/Services/Web/api/api_isp_module.c`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/api_isp_module.c)

**35** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
| `GET` | `/api/v1/isp/params` | ✅ | `api_isp_get_all_params` |
| `GET` | `/api/v1/isp/sensor` | ✅ | `api_isp_get_sensor_info` |
| `GET` | `/api/v1/isp/statistics` | ✅ | `api_isp_get_statistics` |
| `GET` | `/api/v1/isp/aec` | ✅ | `api_isp_get_aec` |
| `PUT` | `/api/v1/isp/aec` | ✅ | `api_isp_set_aec` |
| `GET` | `/api/v1/isp/aec/manual` | ✅ | `api_isp_get_manual_exposure` |
| `PUT` | `/api/v1/isp/aec/manual` | ✅ | `api_isp_set_manual_exposure` |
| `GET` | `/api/v1/isp/awb` | ✅ | `api_isp_get_awb` |
| `PUT` | `/api/v1/isp/awb` | ✅ | `api_isp_set_awb` |
| `GET` | `/api/v1/isp/demosaicing` | ✅ | `api_isp_get_demosaicing` |
| `PUT` | `/api/v1/isp/demosaicing` | ✅ | `api_isp_set_demosaicing` |
| `GET` | `/api/v1/isp/stat_removal` | ✅ | `api_isp_get_stat_removal` |
| `PUT` | `/api/v1/isp/stat_removal` | ✅ | `api_isp_set_stat_removal` |
| `GET` | `/api/v1/isp/black_level` | ✅ | `api_isp_get_black_level` |
| `PUT` | `/api/v1/isp/black_level` | ✅ | `api_isp_set_black_level` |
| `GET` | `/api/v1/isp/bad_pixel` | ✅ | `api_isp_get_bad_pixel` |
| `PUT` | `/api/v1/isp/bad_pixel` | ✅ | `api_isp_set_bad_pixel` |
| `GET` | `/api/v1/isp/gain` | ✅ | `api_isp_get_gain` |
| `PUT` | `/api/v1/isp/gain` | ✅ | `api_isp_set_gain` |
| `GET` | `/api/v1/isp/color_conv` | ✅ | `api_isp_get_color_conv` |
| `PUT` | `/api/v1/isp/color_conv` | ✅ | `api_isp_set_color_conv` |
| `GET` | `/api/v1/isp/contrast` | ✅ | `api_isp_get_contrast` |
| `PUT` | `/api/v1/isp/contrast` | ✅ | `api_isp_set_contrast` |
| `GET` | `/api/v1/isp/gamma` | ✅ | `api_isp_get_gamma` |
| `PUT` | `/api/v1/isp/gamma` | ✅ | `api_isp_set_gamma` |
| `GET` | `/api/v1/isp/stat_area` | ✅ | `api_isp_get_stat_area` |
| `PUT` | `/api/v1/isp/stat_area` | ✅ | `api_isp_set_stat_area` |
| `GET` | `/api/v1/isp/lux_ref` | ✅ | `api_isp_get_lux_ref` |
| `PUT` | `/api/v1/isp/lux_ref` | ✅ | `api_isp_set_lux_ref` |
| `GET` | `/api/v1/isp/sensor_delay` | ✅ | `api_isp_get_sensor_delay` |
| `PUT` | `/api/v1/isp/sensor_delay` | ✅ | `api_isp_set_sensor_delay` |
| `POST` | `/api/v1/isp/config/save` | ✅ | `api_isp_save_config` |
| `POST` | `/api/v1/isp/config/load` | ✅ | `api_isp_load_config` |
| `GET` | `/api/v1/isp/config/export` | ✅ | `api_isp_export_config` |
| `POST` | `/api/v1/isp/config/import` | ✅ | `api_isp_import_config` |
