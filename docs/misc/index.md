# Doc Index

This site is deployed on GitHub Pages from the `docs/` directory of the repository.

## Web API Documentation

- [API Overview](/web-api/) — base path, authentication, response format and error codes
- [Authentication & Quick Start](/web-api/authentication) — login flow and calling examples (curl / JS / Python)
- [Endpoint Reference (auto-generated)](/web-api/endpoints/) — every endpoint, extracted from the firmware source by `Script/gen_web_api_docs.py`
- Module guides: [Network Management](/web-api/network)

## Topic Docs (Chinese)

- [拍照上传流程](/拍照上传流程) — full pipeline of button/RTC-wakeup capture and MQTT upload
- [RTMP API](/api/RTMP_API) · [PIR Sensor API](/api/PIR_SENSOR_API) · [PoE Network API](/api/PoE_Network_API)
- [视频流中台升级设计](/design/VIDEO_STREAM_HUB_UPGRADE)

## Maintenance & Sync Mechanism

The site is built with [VitePress](https://vitepress.dev/). See [docs/README.md](https://github.com/camthink-ai/ne301/blob/main/docs/README.md) for local development and build instructions. The site is bilingual — English is the default at the root path, 简体中文 lives under [`/zh/`](/zh/).

**The Web API endpoint reference is auto-generated**: the script scans the route registration tables in `Custom/Services/Web/api/*.c` and produces the pages under `web-api/endpoints/` (both languages). Therefore:

- After changing API routes (adding/removing endpoints, changing paths/methods/auth), run `python3 Script/gen_web_api_docs.py` and commit the result together with your change;
- CI runs the same script with `--check` on every PR — stale endpoint docs **block merging**;
- On push to `main`, GitHub Actions regenerates the endpoint docs and redeploys Pages, so the live site always reflects the code.

Hand-written module guides should be updated manually when API behavior changes; the PR check emits a warning whenever Web API code changes without any change under `docs/`.
