---
layout: home

hero:
  name: NE301
  text: AI Camera Firmware & Web API Docs
  tagline: STM32N6 edge AI camera · firmware build, Web API and frontend integration reference
  actions:
    - theme: brand
      text: Browse Web API Docs
      link: /web-api/
    - theme: alt
      text: 简体中文
      link: /zh/
    - theme: alt
      text: GitHub
      link: https://github.com/camthink-ai/ne301

features:
  - icon: 📡
    title: RESTful Web API
    details: Complete HTTP endpoints across network, capture, streaming, OTA and MQTT modules. The endpoint reference is auto-generated from firmware source and always in sync.
    link: /web-api/
    linkText: Endpoint reference
  - icon: 🔐
    title: Unified Auth & Response Format
    details: HTTP Basic Auth plus a consistent { code, message, data } response structure with business error codes.
    link: /web-api/authentication
    linkText: Quick start
  - icon: 🔄
    title: Docs That Follow the Code
    details: After changes under Custom/Services/Web, CI regenerates the endpoint reference and redeploys to GitHub Pages; PRs are checked for doc sync.
    link: /misc/
    linkText: How sync works
---
