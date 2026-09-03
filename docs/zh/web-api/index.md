# Web API 总览

NE301 设备内置 HTTP 服务，提供完整的 RESTful API 用于设备配置、状态查询与流媒体控制。前端管理界面（`Web/`）即基于这些 API 构建。

## 基础信息

| 项目 | 说明 |
|------|------|
| 基础路径 | `/api/v1` |
| 数据格式 | 请求与响应均为 `application/json`（文件上传等除外） |
| 认证方式 | [HTTP Basic Auth](./authentication.md) |
| 端点总数 | [自动提取自源码，见端点参考](./endpoints/) |

## 统一响应格式

所有 API 返回统一的 JSON 结构：

```json
{
  "code": 0,
  "message": "Success",
  "data": { }
}
```

- `code`：`0` 表示成功，非 `0` 为业务错误码，见下表；
- `message`：人类可读的描述；
- `data`：业务数据，结构因接口而异。

## 业务错误码

错误码定义于 `Custom/Services/Web/api/api_business_error.h`，从 1000 起避免与系统错误码冲突。

| 区间 | 分类 | 常见错误码 |
|------|------|-----------|
| 0 | 成功 | `0` 无错误 |
| 1001–1099 | 认证与授权 | `1001` 密码无效 · `1003` 未授权 · `1004` 会话过期 · `1005` Token 无效 · `1007` 权限不足 |
| 1101–1199 | 参数校验 | `1101` 参数无效 · `1102` 缺少参数 · `1103` 参数越界 · `1104` 格式错误 |
| 1201–1299 | 设备与硬件 | `1201` 设备离线 · `1202` 设备忙 · `1204` 摄像头错误 · `1206` 硬件错误 |
| 1301–1399 | 网络通讯 | `1301` 网络错误 · `1302` 网络超时 · `1304` MQTT 未连接 · `1305` WiFi 未连接 |
| 1401–1499 | 存储 | `1401` 存储已满 · `1403` 文件不存在 · `1407` 空间不足 · `1408` 目录非空 |
| 1501–1599 | AI 与模型 | `1501` 模型未加载 · `1502` 模型无效 · `1503` 模型重载失败 · `1505` 推理超时 |
| 1601–1699 | 配置 | `1601` 配置无效 · `1602` 配置不存在 · `1604` 配置更新失败 |
| 1701–1799 | 操作 | `1701` 操作超时 · `1702` 操作失败 · `1705` 操作进行中 · `1706` AT 指令失败 |
| 1801–1899 | OTA 与固件 | `1801` 固件无效 · `1804` OTA 进行中 · `1805` OTA 失败 · `1806` 固件头校验失败 |
| 1901–1999 | 资源 | `1901` 资源不存在 · `1902` 资源忙 |
| 9999 | 未知错误 | `9999` |

## 模块一览

| 模块 | 说明 |
|------|------|
| [认证登录](./endpoints/auth.md) | 设备登录与密码管理 |
| [网络管理](./endpoints/network.md) | WiFi / HaLow / 蜂窝 / PoE 配置与状态 |
| [设备管理](./endpoints/device.md) | 设备信息、时间、日志与维护 |
| [工作模式](./endpoints/work_mode.md) | 设备工作模式与联动策略 |
| [预览流](./endpoints/preview.md) | 摄像头实时预览控制 |
| [图像调优 (ISP)](./endpoints/isp.md) | 图像效果参数的读取与设置 |
| [RTMP 推流](./endpoints/rtmp.md) | RTMP 推流配置与控制 |
| [RTSP 流](./endpoints/rtsp.md) | RTSP 拉流服务管理 |
| [MQTT](./endpoints/mqtt.md) | MQTT 连接配置与状态 |
| [Webhook](./endpoints/webhook.md) | 事件回调通知配置 |
| [OTA 升级](./endpoints/ota.md) | 固件在线升级 |
| [AI 模型管理](./endpoints/ai_management.md) | AI 模型的上传、切换与推理配置 |
| [模型校验](./endpoints/model_validation.md) | AI 模型包的校验与验证 |

> 端点参考页面由 `Script/gen_web_api_docs.py` 自动生成，与固件源码中的路由注册表逐条对应。页面内容随代码变更自动更新（详见[文档同步机制](/zh/misc/)）。

## 快速开始

```bash
# 1. 登录（前端页面用）
curl -X POST http://<device-ip>/api/v1/login \
  -H "Content-Type: application/json" \
  -d '{"password": "<your-password>"}'

# 2. 携带 Basic Auth 调用受保护接口
curl -u admin:<your-password> \
  http://<device-ip>/api/v1/device/info
```

详细认证说明见[认证与快速开始](./authentication.md)。
