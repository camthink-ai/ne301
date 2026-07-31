# stedgeai-use.sh - STEdgeAI Variant Switcher

Switch the NE301 build between STEdgeAI **2.2**, **3.0**, and **4.0** runtimes in Git Bash (Windows), Linux, or macOS.

Firmware and model packages **must use the same variant**. The vendored NPU runtime is linked at build time; the `stedgeai` CLI used for `make model` must match.

## Overview

| Item | Description |
|------|-------------|
| Script | `Script/stedgeai-use.sh` |
| Local config | `Script/stedgeai-use.local.sh` (gitignored, copy from `.example`) |
| Sets | `STEDGEAI_VARIANT`, `STEDGEAI_CORE_DIR` |
| Validates | `Script/check_stedgeai_toolchain.py` (when `STEDGEAI_CORE_DIR` is set) |

### Variant reference

| `STEDGEAI_VARIANT` | NPU runtime lib | Expected `stedgeai` | Model OTA prefix |
|--------------------|-----------------|---------------------|------------------|
| `2.2` | NetworkRuntime1020 | v2.2 | `2.x.x.x` |
| `4.0` (default) | NetworkRuntime1200 | v4.0.1 | `4.x.x.x` |
| `3.0` | NetworkRuntime1100 | v3.0.0 | `3.x.x.x` |

Model version is `$(STEDGEAI_BIT).$(MODEL_VERSION_OVERRIDE)` — see `version.mk` and `stedgeai.mk`.

---

## Quick Start

### 1. One-time path configuration

```bash
cd /f/projects/ct-github/ne301   # project root
cp Script/stedgeai-use.local.sh.example Script/stedgeai-use.local.sh
```

Edit `Script/stedgeai-use.local.sh` with your ST Edge AI install directories:

```bash
# Git Bash: H:\stm32\...  ->  /h/stm32/...
# Git Bash: C:\Users\...  ->  /c/Users/...
STEDGEAI_22_DIR="/h/stm32/STEdgeAI/2.2"
STEDGEAI_30_DIR="/h/stm32/STEdgeAI/3.0"
STEDGEAI_40_DIR="/h/stm32/STEdgeAI/4.0/4.0"
```

Alternatively, export `STEDGEAI_22_DIR` / `STEDGEAI_30_DIR` / `STEDGEAI_40_DIR` in `~/.bashrc`.

### 2. Load and switch (every new terminal)

```bash
source Script/stedgeai-use.sh
stedgeai-use 4.0
make flash
```

**Important:** use `source` so variables stay in the current shell. Running `./Script/stedgeai-use.sh` only affects child processes.

### 3. Verify

```bash
stedgeai-use show
make version    # shows STEdgeAI variant in project Makefile output
```

---

## Commands

| Command | Description |
|---------|-------------|
| `stedgeai-use 2.2` | Switch to STEdgeAI 2.2 |
| `stedgeai-use 4.0` | Switch to 4.0 (project default) |
| `stedgeai-use 3.0` | Switch to 3.0 |
| `stedgeai-use 22` / `30` / `40` | Short aliases for the above |
| `stedgeai-use show` | Print current `STEDGEAI_VARIANT` and `STEDGEAI_CORE_DIR` |
| `stedgeai-use paths` | List configured install directories |
| `stedgeai-use help` | Print usage summary |

---

## Typical workflows

### Firmware only (no model rebuild)

```bash
source Script/stedgeai-use.sh
stedgeai-use 4.0
make clean      # recommended when changing variant
make app
make flash
```

### Firmware + model

```bash
source Script/stedgeai-use.sh
stedgeai-use 3.0
make clean
make all        # includes model; runs toolchain check in Model/Makefile
```

### Without the helper script

```bash
export STEDGEAI_VARIANT=4.0
export STEDGEAI_CORE_DIR="/h/stm32/STEdgeAI/4.0/4.0"
make all STEDGEAI_VARIANT=4.0   # make arg also works per command
```

---

## Auto-load in Git Bash (optional)

Add to `~/.bashrc`:

```bash
_ne301_stedgeai_use() {
  local root="/f/projects/ct-github/ne301"
  [[ -f "$root/Script/stedgeai-use.sh" ]] && source "$root/Script/stedgeai-use.sh"
}
# call when entering project, or on every shell:
# _ne301_stedgeai_use
```

---

## Troubleshooting

### Toolchain does not match build variant

```
ERROR: ST Edge AI toolchain does not match build variant
  STEDGEAI_VARIANT=2.2 (requires tool version containing 'v2.2')
  Installed: ST Edge AI Core v4.0.1-...
```

**Cause:** `STEDGEAI_VARIANT` and `STEDGEAI_CORE_DIR` point to different installs (often because `STEDGEAI_22_DIR` was not set and an old 4.0 path remained).

**Fix:**

1. Configure `Script/stedgeai-use.local.sh` with the correct path for that variant.
2. `source Script/stedgeai-use.sh && stedgeai-use 2.2`
3. Confirm with `stedgeai-use show` — both variant and `CORE_DIR` must match.

If you only have one ST Edge AI version installed, use that variant only (e.g. `stedgeai-use 4.0`).

### `STEDGEAI_22_DIR not set; cleared STEDGEAI_CORE_DIR`

The script clears a stale `STEDGEAI_CORE_DIR` when the variant-specific path is missing. Firmware builds (`make app`, `make flash`) still work; set the path in `stedgeai-use.local.sh` before `make model`.

### `./Script/stedgeai-use.sh` has no effect on `make`

Use `source Script/stedgeai-use.sh` then `stedgeai-use <variant>` in the **same** shell session before `make`.

### Path does not exist

Use Git Bash path form (`/h/...`, `/c/...`). Check:

```bash
ls -la /h/stm32/STEdgeAI/4.0/4.0/Utilities/windows/stedgeai.exe
```

### Switching variant without `make clean`

Object files from another variant may remain under `build/` or `Appli/build/`. After switching:

```bash
make clean
make all
```

---

## Related files

| File | Role |
|------|------|
| `stedgeai.mk` | Variant → lib paths, `MODEL_STEDGEAI_VERSION`, `MODEL_VERSION` |
| `Script/check_stedgeai_toolchain.py` | Build-time `stedgeai` vs variant check |
| `Script/generate-reloc-model.sh` | Resolves `stedgeai` from `STEDGEAI_CORE_DIR` |
| `README.md` (repo root) | Build matrix and Docker image tags (`v2.2` / `v3.0` / `v4.0`) |

---

**See also:** [MODEL_PACK.md](MODEL_PACK.md) for model generation and packaging.
