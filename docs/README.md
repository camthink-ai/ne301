# NE301 文档站

基于 [VitePress](https://vitepress.dev/) 的双语文档站（**默认英文**，中文在 `/zh/`，
导航栏可切换），通过 GitHub Actions 部署到
GitHub Pages：<https://camthink-ai.github.io/ne301/>

## 目录结构

```
docs/
├── .vitepress/config.mts   # 站点配置（双 locale、导航 / 侧边栏）
├── index.md                # 英文首页（默认语言，挂根路径）
├── misc/                   # 英文文档索引
├── web-api/                # Web API 文档（英文手写部分）
│   ├── index.md            # API 总览：基础路径、响应格式、错误码
│   ├── authentication.md   # 认证与快速开始
│   ├── network.md          # 网络管理模块详解
│   └── endpoints/          # ⚙ 自动生成（英文），勿手改
├── zh/                     # 简体中文（/zh/ 路径）
│   ├── index.md            # 中文首页
│   ├── misc/
│   └── web-api/            # 中文手写页 + endpoints/（⚙ 自动生成）
├── api/                    # 既有专题 API 文档（中文，两侧边栏共用）
├── design/                 # 设计文档（中文）
└── public/                 # 原样发布的静态文件（logo、manifest 副本）
```

## 本地开发

```bash
cd docs
npm install
npm run dev        # http://localhost:5173/ne301/
```

## Web API 端点参考的自动生成

`docs/web-api/endpoints/`（英文）与 `docs/zh/web-api/endpoints/`（中文）下的
页面由 `Script/gen_web_api_docs.py` 扫描固件源码 `Custom/Services/Web/api/*.c`
中的路由注册表自动生成，包含每个端点的方法、路径、鉴权要求与处理函数，
同时输出 `manifest.json` 供程序化使用（线上副本：
`/ne301/web-api/endpoints-manifest.json`）。模块在上游删除后，其旧端点页会被
自动清理，不会残留。

**修改 Web API 代码后必须同步文档**：

```bash
# 方式一：直接运行脚本
python3 Script/gen_web_api_docs.py

# 方式二：在 docs/ 下
cd docs && npm run sync
```

生成结果确定性输出（不含时间戳），提交后与源码一一对应。

## CI 同步机制（.github/workflows/docs.yml）

| 时机 | 行为 |
|------|------|
| Pull Request | `docs-sync-check` job 重新生成端点参考并与 PR 内容比较：不一致则**失败并阻止合并**；若改了 `Custom/Services/Web` 而 `docs/` 无任何变更，输出**警告**提醒更新手写详解文档 |
| push 到 main / 手动触发 | 重新生成端点参考 → VitePress 构建 → 部署 GitHub Pages，线上页面始终与代码一致 |

### 手写文档维护约定

- 模块详解（`network.md` 等）在 API 行为变化时人工更新，**中英两份需同步修改**，
  页首注明来源源码文件；
- 新增 API 模块（新的 `api_xxx_module.c`）时：
  1. 运行生成脚本（`Module_Meta` 中可选地补充模块中英文名称与简介）；
  2. 在 `docs/web-api/index.md` 与 `docs/zh/web-api/index.md` 的模块表中各补一行；
  3. 双语侧边栏自动收录新生成的端点页，无需改配置。

## 首次启用部署

仓库维护者需执行一次：

1. 推送本目录及 `.github/workflows/docs.yml` 到 `main` 分支；
2. 仓库 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**；
3. 手动触发一次 *Docs (GitHub Pages)* workflow（workflow_dispatch）验证部署。

## 常用命令

```bash
cd docs
npm run dev       # 本地开发服务器
npm run build     # 重新生成端点参考 + 构建（CI 同款命令）
npm run preview   # 本地预览构建产物
npm run sync      # 仅重新生成端点参考
npm run check     # CI 同款检查：端点文档与代码不一致时失败
```
