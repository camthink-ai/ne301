---
layout: home

hero:
  name: NE301
  text: AI 摄像头固件与 Web API 文档
  tagline: STM32N6 边缘 AI 摄像头 · 固件构建、Web API 与前端集成参考
  actions:
    - theme: brand
      text: 浏览 Web API 文档
      link: /web-api/
    - theme: alt
      text: GitHub
      link: https://github.com/camthink-ai/ne301

features:
  - icon: 📡
    title: RESTful Web API
    details: 142 个 HTTP 端点，覆盖网络、抓拍、推流、OTA、MQTT 等模块，端点参考由源码自动生成，与固件保持同步。
    link: /web-api/
    linkText: 查看端点总览
  - icon: 🔐
    title: 统一认证与响应格式
    details: HTTP Basic Auth + 统一 { code, message, data } 响应结构与业务错误码。
    link: /web-api/authentication
    linkText: 快速开始
  - icon: 🔄
    title: 文档随代码更新
    details: 修改 Custom/Services/Web 下的 API 代码后，CI 自动重新生成端点参考并部署到 GitHub Pages，PR 中强制检查文档同步。
    link: /misc/
    linkText: 了解同步机制
---
