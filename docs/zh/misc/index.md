# 文档索引

本站点部署在 GitHub Pages，源文件位于仓库 `docs/` 目录。

## Web API 文档

- [API 总览](/zh/web-api/) — 基础路径、认证、响应格式与错误码
- [认证与快速开始](/zh/web-api/authentication) — 登录流程与调用示例
- [端点参考（自动生成）](/zh/web-api/endpoints/) — 全部端点清单由 `Script/gen_web_api_docs.py` 从固件源码提取
- 模块详解：[网络管理](/zh/web-api/network) · [RTMP 推流](/api/RTMP_API) · [PIR 传感器](/api/PIR_SENSOR_API) · [PoE 配电](/api/PoE_Network_API)

## 专题文档

- [拍照上传流程](/拍照上传流程) — 按键/唤醒拍照与 MQTT 上传的完整链路
- [视频流中台升级设计](/design/VIDEO_STREAM_HUB_UPGRADE)

## 文档维护与同步机制

文档站使用 [VitePress](https://vitepress.dev/)，本地开发与构建方式见 [docs/README.md](https://github.com/camthink-ai/ne301/blob/main/docs/README.md)。

**Web API 端点参考是自动生成的**：脚本扫描 `Custom/Services/Web/api/*.c` 中的路由注册表，生成 `docs/web-api/endpoints/` 下的页面。因此：

- 修改 API 路由（增删端点、改路径/方法/鉴权）后，需运行 `python3 Script/gen_web_api_docs.py` 重新生成并提交；
- CI 会在 PR 中运行 `--check` 检查，端点文档未同步会**阻止合并**；
- push 到 `main` 后，GitHub Actions 自动重新生成端点文档并部署到 Pages，线上永远展示最新代码的端点。

模块详解等手写文档在 API 行为变化时需人工更新，PR 检查会在 Web API 代码变更而 `docs/` 无变更时给出提醒。
