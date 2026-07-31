# NE301 Development Scripts - Complete Guide

> **Documentation Index**: See [DOC_INDEX.md](DOC_INDEX.md)

This directory contains essential development tools for the NE301 STM32N6570 project, including model generation, OTA packaging, and device flashing.

## 📚 Table of Contents

- [Quick Start](#quick-start)
- [Script Overview](#script-overview)
- [STEdgeAI Variant Switching](#stedgeai-variant-switching)
- [Complete Workflows](#complete-workflows)
- [Troubleshooting](#troubleshooting)
- [Support Resources](#support-resources)

---

## Quick Start

### ⚠️ Important Notice

**All firmware must be packaged with OTA header before flashing!**

The device expects all firmware to have a 1KB OTA header at the beginning. Direct flashing of raw binary files will cause boot failures.

```
✓ Correct:   [OTA Header 1KB][Firmware Data]
✗ Incorrect: [Firmware Data] (without header)
```

### Prerequisites

```bash
# 1. Install required tools
# STM32CubeProgrammer (for flashing)
# ARM GCC Toolchain (for binary conversion)
# Python 3.6+ (for OTA and model packaging)
# ST Edge AI Tools (for model generation)

# 2. Set environment variables
export STEDGEAI_CORE_DIR="/path/to/stedgeai/installation"
export STM32_PROGRAMMER_CLI="STM32_Programmer_CLI"  # Or full path

# 3. Verify installations
python --version
arm-none-eabi-objcopy --version
STM32_Programmer_CLI --version
```
---

## Script Overview

### 1. **maker.sh** - Device Flashing & Utilities

| Command | Description |
|---------|-------------|
| `flash` | Flash firmware via SWD (ST-LINK) |
| `sign` | Sign binary for secure boot |
| `hex` | Convert binary to hex |

### 2. **ota_packer.py** - OTA Package Creator

| Mode | Description |
|------|-------------|
| Single | Create one OTA package (all parameters required) |
| Clean | Remove generated packages |

**Required Parameters**:
- `-o` / `--output`: Output file path
- `-t` / `--type`: Firmware type (fsbl/app/web/ai_model)
- `-n` / `--name`: Firmware name
- `-d` / `--desc`: Firmware description
- `-v` / `--version`: Version (format: major.minor.patch.build)

### 3. **pack_all_ota.sh** - Batch OTA Packaging

- Configurable version for each firmware type
- Automatic output organization

### 4. **generate-reloc-model.sh** - AI Model Generator

Convert TensorFlow Lite models to deployable binaries.

### 5. **model_packager.py** - Model Package Manager

Create, validate, and extract AI model packages.

### 6. **stedgeai-use.sh** - STEdgeAI Variant Switcher

Switch `STEDGEAI_VARIANT` (2.2 / 3.0 / 4.0) and `STEDGEAI_CORE_DIR` for local Git Bash / Linux builds.

| Command | Description |
|---------|-------------|
| `source Script/stedgeai-use.sh` | Load helper into current shell |
| `stedgeai-use 4.0` | Select variant and toolchain path |
| `stedgeai-use show` | Show current selection |

**Full guide:** [STEDGEAI_USE.md](STEDGEAI_USE.md)

---

## STEdgeAI Variant Switching

NE301 shares application code across STEdgeAI toolchains. Select the runtime at build time:

```bash
source Script/stedgeai-use.sh
stedgeai-use 4.0
make clean && make all
```

Or per command: `make all STEDGEAI_VARIANT=4.0`

See **[STEDGEAI_USE.md](STEDGEAI_USE.md)** for path configuration, troubleshooting, and variant table.

---

## Complete Workflows

### Workflow 1: Application Development & Deployment

```bash
# Step 1: Build Application
cd ../Appli/Debug
make clean all

# Step 2: Sign Binary (for secure boot)
cd ../../Script
./maker.sh sign ../Appli/Debug/ne301_Appli.bin
# Output: ../Appli/Debug/ne301_Appli_signed.bin

# Step 3: Create OTA Package (REQUIRED! ALL parameters required!)
python ota_packer.py ../Appli/Debug/ne301_Appli_signed.bin \
    -t app \
    -n "NE301_APP" \
    -d "NE301 Main Application" \
    -v 1.0.0.1 \
    -o ../Appli/Debug/ne301_Appli_signed_v1.0.0.1_ota.bin

# Step 4: Flash to Device
./maker.sh flash ../Appli/Debug/ne301_Appli_signed_v1.0.0.1_ota.bin 0x70100000
```

### Workflow 2: Web Development & Deployment

```bash
# Step 1: Build Web UI
cd ../Web

# Install dependencies (first time only)
pnpm install

# Build web assets to binary
pnpm build:bin
# Output: firmware_assets/web-assets.bin

# Step 2: Create OTA Package (REQUIRED!)
cd ../Script
python ota_packer.py ../Web/firmware_assets/web-assets.bin \
    -o ../Web/firmware_assets/web-assets_v1.5.0.88_ota.bin \
    -t web \
    -n "NE301_WEB" \
    -d "NE301 Web User Interface" \
    -v 1.5.0.88

# Step 3: Verify OTA Package
python verify_ota_package.py ../Web/firmware_assets/web-assets_v1.5.0.88_ota.bin

# Step 4: Deploy
# Method A: Flash directly (recommended for development)
./maker.sh flash ../Web/firmware_assets/web-assets_v1.5.0.88_ota.bin 0x71900000

# Method B: Deploy via network OTA (for production)
# (Upload to device via web interface or OTA server)
```

### Workflow 3: AI Model Generation & Deployment

```bash
cd Script

# Step 0: Select matching STEdgeAI variant (see STEDGEAI_USE.md)
source stedgeai-use.sh
stedgeai-use 3.0

# Step 1: Generate Model Binary
./generate-reloc-model.sh \
    -m ../Model/weights/yolov8n_256_quant_pc_uf_pose_coco-st.tflite \
    -f reloc-ne301@../Model/neural_art_reloc.json \
    -o network_rel.bin

# Step 2: Create Model Package
python model_packager.py create \
    --model network_rel.bin \
    --config ../Model/weights/yolov8n_256_quant_pc_uf_pose_coco-st.json \
    --output model_yolov8_uf_mpe.bin

# Step 3: Create OTA Package for Model (REQUIRED!)

python ota_packer.py model_yolov8_uf_mpe.bin \
    -t ai_model \
    -n "YOLOv8_Pose" \
    -d "YOLOv8 Pose Estimation Model" \
    -v 3.0.2.45 \
    -o model_yolov8_uf_mpe_v3.0.2.45_ota.bin

# Step 4: Flash Model to Device (use OTA packaged file)

./maker.sh flash model_yolov8_uf_mpe_v3.0.2.45_ota.bin 0x70900000

# Or deploy via OTA
# (Copy to device filesystem or upload via network)
```

### Workflow 4: Complete System Update

```bash
cd Script

# Step 1: Prepare all firmware
# FSBL, APP, Web, AI Model
# ⚠️ NOTE: All source binaries must be signed before OTA packaging


# Step 2: Batch create OTA packages
# Edit pack_all_ota.sh to set versions
vim pack_all_ota.sh
# Set: FSBL_VERSION="1.0.0.2"
#      APP_VERSION="2.3.1.125"
#      WEB_VERSION="1.5.0.88"
#      AI_MODEL_VERSION="3.0.2.45"

# Run batch script (adds OTA header to ALL firmware)

./pack_all_ota.sh

# Step 3: Verify all packages
cd ota_packages
for file in *_ota.bin; do
    python ../../Script/verify_ota_package.py "$file"
done

# Step 4: Flash in order (ALL files have OTA header)

cd ../../Script

# Flash FSBL first
./maker.sh flash ota_packages/ne301_FSBL_v1.0.0.2_ota.bin 0x70000000

# Flash APP
./maker.sh flash ota_packages/ne301_Appli_v2.3.1.125_ota.bin 0x70100000

# Flash Web Assets
./maker.sh flash ota_packages/web-assets_v1.5.0.88_ota.bin 0x71900000

# Flash AI Model
./maker.sh flash ota_packages/model_yolov8_uf_mpe_v3.0.2.45_ota.bin 0x70900000

# Step 5: Verify system
# Reset device and check functionality
```
---

## Troubleshooting

### Common Issues

#### 1. Flash Connection Failed

```bash
# Check device connection
STM32_Programmer_CLI -l

# Try USB DFU mode
./maker.sh flash-usb firmware.bin 0x08000000

# Check external loader
ls -la ExternalLoader/
```

#### 2. OTA Package Verification Failed

```bash
# Re-create package
python ota_packer.py firmware.bin \
    -o firmware_v1.0.0.1_ota.bin \
    -t app \
    -n "APP" \
    -d "Main Application" \
    -v 1.0.0.1

# Check file integrity
md5sum firmware.bin
hexdump -C firmware_v1.0.0.1_ota.bin | head -n 20
```

#### 3. Model Generation Failed

```bash
# Check environment variable
echo $STEDGEAI_CORE_DIR

# Verify model file
file model.tflite

# Clean and retry
./generate-reloc-model.sh --clean
./generate-reloc-model.sh -m model.tflite
```

### Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| `STM32_Programmer_CLI not found` | Tool not installed | Install STM32CubeProgrammer |
| `arm-none-eabi-objcopy not found` | Toolchain missing | Install ARM GCC |
| `STEDGEAI_CORE_DIR not set` | Env var missing | Export STEDGEAI_CORE_DIR or use `stedgeai-use` |
| `toolchain does not match build variant` | Variant/path mismatch | Run `stedgeai-use <variant>` after configuring `stedgeai-use.local.sh` |
| `Invalid version format` | Wrong version string | Use format: `1.0.0.1` |
| `File too small` | Corrupted file | Re-build firmware |
| `Magic number mismatch` | Wrong file format | Check OTA package |

---

## Support Resources

### Documentation
- [STEDGEAI_USE.md](STEDGEAI_USE.md) - STEdgeAI variant switching (`stedgeai-use.sh`)
- [MAKER_USAGE.md](MAKER_USAGE.md) - maker.sh detailed usage 
- [OTA_PACK.md](OTA_PACK.md) - OTA packaging guide 
- [MODEL_PACK.md](MODEL_PACK.md) - AI model generation guide
- [USAGE_EXAMPLES.md](USAGE_EXAMPLES.md) - Complete usage examples

### Tools & Downloads
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
- [ARM GCC Toolchain](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain)
- [ST Edge AI Tools](https://www.st.com/en/embedded-software/x-cube-ai.html)

### Contact
For issues or questions:
1. Check troubleshooting section
2. Review relevant documentation
3. Contact development team

---

---

**Version**: 1.0  
**Last Updated**: October 2025  
**Maintained by**: CamThink
