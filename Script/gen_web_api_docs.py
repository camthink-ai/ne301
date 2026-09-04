#!/usr/bin/env python3
"""
gen_web_api_docs.py - 从 Web API C 源码自动生成端点参考文档（中英双语）

扫描 Custom/Services/Web/api/*.c 中的 api_route_t 路由表（designated
initializer 与 positional 两种形式），提取 path / method / handler /
require_auth 字段，生成 VitePress 端点参考页与 manifest.json。

输出两套页面：
    docs/web-api/endpoints/    英文（站点默认语言）
    docs/zh/web-api/endpoints/ 中文

用法:
    python3 Script/gen_web_api_docs.py [--check]

    默认模式 : 重新生成上述目录下的文件
    --check  : 重新生成并与仓库中已提交的版本比较，不一致时以非零
               退出码结束（供 CI 强制 "代码改动必须同步文档"）

输出是确定性的（不含时间戳），端点集合或源文件变化才会产生 diff。
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
API_SRC_DIR = REPO_ROOT / "Custom" / "Services" / "Web" / "api"
# 英文为站点默认语言（root locale），中文挂在 /zh/
OUT_DIRS = {
    "en": REPO_ROOT / "docs" / "web-api" / "endpoints",
    "zh": REPO_ROOT / "docs" / "zh" / "web-api" / "endpoints",
}
PUBLIC_MANIFEST = REPO_ROOT / "docs" / "public" / "web-api" / "endpoints-manifest.json"

API_PATH_PREFIX = "/api/v1"

# 模块名 -> (英文标题, 中文标题, 英文说明, 中文说明)
# 文件名 api_<key>_module.c 之外的模块不会被扫描
MODULE_META = {
    "ai_management":     ("AI Model Management", "AI 模型管理",
                          "Upload, switch and configure AI models",
                          "AI 模型的上传、切换与推理配置"),
    "auth":              ("Authentication", "认证登录",
                          "Device login and password management",
                          "设备登录与密码管理"),
    "capture":           ("Capture & Upload", "抓拍与上传",
                          "Capture tasks, upload queue and records",
                          "抓拍任务、上传队列与记录管理"),
    "device":            ("Device Management", "设备管理",
                          "Device info, time, logs and maintenance",
                          "设备信息、时间、日志与维护操作"),
    "file":              ("File Management", "文件管理",
                          "File browser and file transfer",
                          "文件浏览器与文件传输"),
    "isp":               ("ISP Tuning", "图像调优 (ISP)",
                          "Image quality parameter get/set",
                          "图像效果参数的读取与设置"),
    "model_validation":  ("Model Validation", "模型校验",
                          "AI model package validation",
                          "AI 模型包的校验与验证"),
    "mqtt":              ("MQTT", "MQTT",
                          "MQTT connection configuration and status",
                          "MQTT 连接配置与状态"),
    "network":           ("Network Management", "网络管理",
                          "WiFi / cellular / PoE configuration and status",
                          "WiFi / 蜂窝 / PoE 网络配置与状态"),
    "ota":               ("OTA Upgrade", "OTA 升级",
                          "Firmware over-the-air upgrade",
                          "固件在线升级"),
    "preview":           ("Live Preview", "预览流",
                          "Camera live preview control",
                          "摄像头实时预览控制"),
    "rtmp":              ("RTMP Streaming", "RTMP 推流",
                          "RTMP stream configuration and control",
                          "RTMP 推流配置与控制"),
    "rtsp":              ("RTSP Streaming", "RTSP 流",
                          "RTSP pull-stream service management",
                          "RTSP 拉流服务管理"),
    "webhook":           ("Webhook", "Webhook",
                          "Event callback notification configuration",
                          "事件回调通知配置"),
    "work_mode":         ("Work Mode", "工作模式",
                          "Device work mode and linkage policies",
                          "设备工作模式与联动策略"),
}

# 按 sidebar 展示顺序排列；新模块自动追加到末尾
MODULE_ORDER = [
    "auth", "network", "device", "work_mode", "capture", "preview",
    "isp", "rtmp", "rtsp", "file", "mqtt", "webhook", "ota",
    "ai_management", "model_validation",
]

# 路由条目两种初始化风格：
#   designated:  { .path = ..., .method = ..., .handler = ..., .require_auth = ... }（字段顺序不定）
#   positional:  { "/api/v1/...", "GET", handler, AICAM_TRUE, NULL }
BLOCK_RE = re.compile(r"\{[^{}]*?\}")
POSITIONAL_RE = re.compile(
    r'\{\s*"(\/[^"]*)"\s*,\s*"(GET|POST|PUT|DELETE|PATCH)"\s*,'
    r'\s*([A-Za-z0-9_]+)\s*,\s*(AICAM_TRUE|AICAM_FALSE)'
)
FIELD_RES = {
    "path": re.compile(r'\.path\s*=\s*([^,]+?),\s'),
    "method": re.compile(r'\.method\s*=\s*"([A-Z]+)"'),
    "handler": re.compile(r'\.handler\s*=\s*([A-Za-z0-9_]+)'),
    "require_auth": re.compile(r'\.require_auth\s*=\s*(AICAM_TRUE|AICAM_FALSE)'),
}


def module_key(filename: str) -> str:
    """api_network_module.c / api_auth_module.c -> network / auth"""
    m = re.match(r"^api_(.+?)_module\.c$", filename)
    return m.group(1) if m else ""


def resolve_path(expr: str) -> str:
    """把路径表达式解析为最终 URL 路径（宏替换为带引号段，再拼接所有字符串）"""
    expr = expr.strip().replace("API_PATH_PREFIX", f'"{API_PATH_PREFIX}"')
    parts = re.findall(r'"([^"]*)"', expr)
    return "".join(parts)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def extract_routes(c_file: Path):
    """提取一个模块 .c 文件中的全部路由（designated 与 positional 两种风格）"""
    text = strip_comments(c_file.read_text(encoding="utf-8", errors="replace"))

    routes = []

    for m in POSITIONAL_RE.finditer(text):
        routes.append({
            "method": m.group(2),
            "path": m.group(1),
            "handler": m.group(3),
            "require_auth": m.group(4) == "AICAM_TRUE",
        })

    for block in BLOCK_RE.finditer(text):
        seg = block.group(0)
        pm = FIELD_RES["method"].search(seg)
        pp = FIELD_RES["path"].search(seg)
        if not (pm and pp):
            continue
        path = resolve_path(pp.group(1))
        if not path.startswith(API_PATH_PREFIX):
            continue
        handler = FIELD_RES["handler"].search(seg)
        auth = FIELD_RES["require_auth"].search(seg)
        routes.append({
            "method": pm.group(1),
            "path": path,
            "handler": handler.group(1) if handler else "-",
            "require_auth": auth.group(1) == "AICAM_TRUE" if auth else None,
        })

    # 保持源码中的注册顺序（positional 在前不影响：两种风格不会混用于同一模块）
    return routes


def meta(key: str, lang: str):
    t_en, t_zh, d_en, d_zh = MODULE_META.get(key, (key, key, "", ""))
    return (t_en, d_en) if lang == "en" else (t_zh, d_zh)


def render_module_page(key: str, routes: list, src_file: str, lang: str) -> str:
    title, desc = meta(key, lang)
    if lang == "en":
        return f"""---
title: {title} Endpoints
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# {title} Endpoints

{desc}

Source: [`Custom/Services/Web/api/{src_file}`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/{src_file})

**{len(routes)}** endpoints. The ✅ marker in the Auth column means the request must carry [credentials](../authentication.md).

| Method | Path | Auth | Handler |
|--------|------|:----:|---------|
""" + "\n".join(
            f'| `{r["method"]}` | `{r["path"]}` | {"✅" if r["require_auth"] else "—" if r["require_auth"] is not None else "?"} | `{r["handler"]}` |'
            for r in routes
        ) + "\n"

    return f"""---
title: {title} 端点参考
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# {title} 端点参考

{desc}

源文件: [`Custom/Services/Web/api/{src_file}`](https://github.com/camthink-ai/ne301/blob/main/Custom/Services/Web/api/{src_file})

共 **{len(routes)}** 个端点。鉴权列 ✅ 表示需要携带[认证凭据](../authentication.md)。

| 方法 | 路径 | 鉴权 | 处理函数 |
|------|------|:----:|----------|
""" + "\n".join(
        f'| `{r["method"]}` | `{r["path"]}` | {"✅" if r["require_auth"] else "—" if r["require_auth"] is not None else "?"} | `{r["handler"]}` |'
        for r in routes
    ) + "\n"


def render_index_page(modules: dict, lang: str) -> str:
    keys = sorted(modules.keys(), key=lambda k: (MODULE_ORDER.index(k) if k in MODULE_ORDER else 999, k))
    total = sum(len(r) for r in modules.values())

    if lang == "en":
        rows = "\n".join(
            f"| [{meta(k, 'en')[0]}](./{k}.md) | {meta(k, 'en')[1]} | {len(modules[k])} |"
            for k in keys
        )
        return f"""---
title: API Endpoint Reference
---

<!-- GENERATED FILE - do not edit manually. Regenerate with Script/gen_web_api_docs.py -->

# API Endpoint Reference

All **{total}** endpoints grouped by module. Data is extracted directly from
the route registration tables in the firmware source, so it always matches the
code. Click a module to see methods, paths and auth requirements.

| Module | Description | Endpoints |
|--------|-------------|----------:|
{rows}
"""

    rows = "\n".join(
        f"| [{meta(k, 'zh')[0]}](./{k}.md) | {meta(k, 'zh')[1]} | {len(modules[k])} |"
        for k in keys
    )
    return f"""---
title: API 端点总览
---

<!-- GENERATED FILE - 由 Script/gen_web_api_docs.py 自动生成，请勿手工编辑 -->

# API 端点总览

全部 **{total}** 个端点，按模块分组。数据直接提取自固件源码中的路由注册表，
与代码保持同步。点击模块查看方法、路径与鉴权要求。

| 模块 | 说明 | 端点数 |
|------|------|-------:|
{rows}
"""


def generate() -> dict:
    if not API_SRC_DIR.is_dir():
        sys.exit(f"error: API source directory not found: {API_SRC_DIR}")

    modules = {}
    for c_file in sorted(API_SRC_DIR.glob("api_*_module.c")):
        key = module_key(c_file.name)
        if not key:
            continue
        routes = extract_routes(c_file)
        if routes:
            modules[key] = routes

    for out_dir in OUT_DIRS.values():
        out_dir.mkdir(parents=True, exist_ok=True)
        # 清理孤儿文件：模块被删除后，其旧端点页不能残留在仓库与站点里
        for stale in list(out_dir.glob("*.md")):
            stale.unlink(missing_ok=True)

    manifest_modules = {}
    for key, routes in modules.items():
        src_file = f"api_{key}_module.c"
        for lang, out_dir in OUT_DIRS.items():
            (out_dir / f"{key}.md").write_text(
                render_module_page(key, routes, src_file, lang), encoding="utf-8"
            )
        manifest_modules[key] = {
            "source": f"Custom/Services/Web/api/{src_file}",
            "source_sha256": hashlib.sha256(
                (API_SRC_DIR / src_file).read_bytes()
            ).hexdigest()[:16],
            "routes": routes,
        }

    for lang, out_dir in OUT_DIRS.items():
        (out_dir / "index.md").write_text(render_index_page(modules, lang), encoding="utf-8")

    manifest = {
        "base_path": API_PATH_PREFIX,
        "total_endpoints": sum(len(r) for r in modules.values()),
        "modules": manifest_modules,
    }
    manifest_text = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"
    # manifest 无语言文案，仅英文侧保留一份 + public/ 副本（原样发布到站点）
    (OUT_DIRS["en"] / "manifest.json").unlink(missing_ok=True)
    (OUT_DIRS["en"] / "manifest.json").write_text(manifest_text, encoding="utf-8")
    PUBLIC_MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    PUBLIC_MANIFEST.write_text(manifest_text, encoding="utf-8")
    return manifest


def check() -> int:
    """与 git 中已提交的生成文件比较，返回差异文件数"""
    import subprocess

    generate()
    # public/ 下的 manifest 副本也是生成产物，同样纳入同步检查
    paths = [str(d.relative_to(REPO_ROOT)) for d in OUT_DIRS.values()]
    paths.append(str(PUBLIC_MANIFEST.relative_to(REPO_ROOT)))
    diff_args = ["git", "diff", "--name-only", "--"] + paths
    changed = [l for l in subprocess.run(
        diff_args, cwd=REPO_ROOT, capture_output=True, text=True,
    ).stdout.splitlines() if l.strip()]
    untracked = [l for l in subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard", "--"] + paths,
        cwd=REPO_ROOT, capture_output=True, text=True,
    ).stdout.splitlines() if l.strip()]

    stale = changed + untracked
    if stale:
        print("::error::Web API 已变更，但文档端点参考未同步：")
        for f in stale:
            print(f"  - {f}")
        print("请在仓库根目录运行以下命令后重新提交：")
        print("    (cd docs && npm run sync)   # 或 python3 Script/gen_web_api_docs.py")
        return 1
    print("OK: Web API 端点文档与代码同步。")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="生成后与已提交版本比较，不一致则失败（CI 用）")
    args = parser.parse_args()

    if args.check:
        sys.exit(check())
    manifest = generate()
    print(f"generated {manifest['total_endpoints']} endpoints "
          f"across {len(manifest['modules'])} modules (en + zh) -> {OUT_DIRS['en']}, {OUT_DIRS['zh']}")


if __name__ == "__main__":
    main()
