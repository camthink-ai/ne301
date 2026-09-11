# Maker Script - Complete Usage Guide

> **Chinese Version**: See [MAKER_USAGE_CN.md](MAKER_USAGE_CN.md)

## Overview

`maker.sh` is a unified tool for STM32N6570 development operations including flashing, signing, and file format conversion.

## Prerequisites

### Required Tools

1. **STM32CubeProgrammer** - For flashing firmware
   - Download: https://www.st.com/en/development-tools/stm32cubeprog.html
   - Required: `STM32_Programmer_CLI` in PATH

2. **STM32 Signing Tool** - For signing binaries
   - Included with STM32CubeProgrammer
   - Required: `STM32_SigningTool_CLI` in PATH

3. **ARM GCC Toolchain** - For binary conversion
   - Download: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain
   - Required: `arm-none-eabi-objcopy` in PATH

### Environment Variables

```bash
# Optional: Set custom tool paths
export STM32_PROGRAMMER_CLI="/path/to/STM32_Programmer_CLI"
export STM32_SIGNING_TOOL="/path/to/STM32_SigningTool_CLI"
export OBJCOPY="/path/to/arm-none-eabi-objcopy"
```

### External Loader

For QSPI flash memory operations:
- **Required**: `MX66UW1G45G_STM32N6570-DK.stldr`
- **Auto-search**: STM32CubeProgrammer installation directory
- **Manual**: Place in `ExternalLoader/` directory

---

## Commands

### 1. flash - Flash Firmware

Flash firmware to device using ST-LINK debugger (SWD) with external loader support for QSPI flash.

#### Syntax

```bash
./maker.sh flash <file> [address]
```

#### Parameters

| Parameter | Required | Description | Example |
|-----------|----------|-------------|---------|
| `file` | Yes | Firmware file (.bin or .hex) | `firmware.bin` |
| `address` | For .bin | Flash address (hex) | `0x70100000` |

**Note**: `.hex` files contain address information, so address parameter is optional.

#### Examples

```bash
# Flash binary file (address required)
./maker.sh flash firmware.bin 0x70100000

# Flash hex file (address auto-detected)
./maker.sh flash firmware.hex

# Flash FSBL
./maker.sh flash ../bin/ne301_FSBL_signed_v1.0.0.1_ota.bin 0x70000000

# Flash APP to Slot A
./maker.sh flash ../bin/ne301_Appli_signed_v1.0.0.1_ota.bin 0x70100000

# Flash APP to Slot B
./maker.sh flash ../bin/ne301_Appli_signed_v1.0.0.1_ota.bin 0x70500000

# Flash Web Assets
./maker.sh flash ../Web/firmware_assets/web-assets_v1.5.0.88_ota.bin 0x71900000

# Flash AI Model
./maker.sh flash ../bin/model_yolov8_uf_od_coco_v3.0.2.45_ota.bin 0x70900000
```

#### Flash Memory Map

| Partition | Address | Size | Purpose |
|-----------|---------|------|---------|
| FSBL | 0x70000000 | 512KB | First Stage Boot Loader |
| NVS | 0x70080000 | 64KB | Non-Volatile Storage |
| OTA | 0x70090000 | 8KB | OTA State Information |
| APP1 (Slot A) | 0x70100000 | 4MB | Main Application |
| APP2 (Slot B) | 0x70500000 | 4MB | Main Application |
| WEB | 0x71900000 | 1MB | Web User Interface |
| AI_1 | 0x70900000 | 8MB | AI Model Slot 1 |
| AI_2 | 0x71100000 | 8MB | AI Model Slot 2 |
| LittleFS | 0x71D00000 | 32MB | File System |

---

### 2. sign - Sign Binary File

Sign binary file for secure boot using STM32 Signing Tool.

#### Syntax

```bash
./maker.sh sign <bin_file>
```

#### Parameters

| Parameter | Required | Description | Example |
|-----------|----------|-------------|---------|
| `bin_file` | Yes | Binary file to sign | `firmware.bin` |

#### Output

Creates `<filename>_signed.bin` in the same directory.

#### Examples

```bash
# Sign application binary
./maker.sh sign ../Appli/Debug/ne301_Appli.bin
# Output: ../Appli/Debug/ne301_Appli_signed.bin

# Sign FSBL
./maker.sh sign ../FSBL/Debug/ne301_FSBL.bin
# Output: ../FSBL/Debug/ne301_FSBL_signed.bin
```

**Typical Workflow:**
```bash
# 1. Build
make -C ../Appli clean all

# 2. Sign
./maker.sh sign ../Appli/Debug/ne301_Appli.bin

# 3. Create OTA package
python ota_packer.py ../Appli/Debug/ne301_Appli_signed.bin \
    -o ../Appli/Debug/ne301_Appli_signed_v1.0.0.1_ota.bin \
    -t app \
    -n "APP" \
    -d "Main Application" \
    -v 1.0.0.1

# 4. Flash
./maker.sh flash ../Appli/Debug/ne301_Appli_signed_v1.0.0.1_ota.bin 0x70100000
```

---

### 3. hex - Convert Binary to Hex

Convert binary file to Intel HEX format with specified address.

#### Syntax

```bash
./maker.sh hex <bin_file> <address>
```

#### Parameters

| Parameter | Required | Description | Example |
|-----------|----------|-------------|---------|
| `bin_file` | Yes | Binary file to convert | `firmware.bin` |
| `address` | Yes | Starting address (hex) | `0x70100000` |

#### Output

Creates `<filename>.hex` in the same directory.

#### Examples

```bash
# Convert application binary
./maker.sh hex ../Appli/Debug/ne301_Appli.bin 0x70100000
# Output: ../Appli/Debug/ne301_Appli.hex

# Convert FSBL binary
./maker.sh hex ../FSBL/Debug/ne301_FSBL.bin 0x70000000
# Output: ../FSBL/Debug/ne301_FSBL.hex

# Convert Web assets
./maker.sh hex ../bin/web-assets.bin 0x71900000
# Output: ../bin/web-assets.hex
```

**Use Case**: Create HEX files for external programmers or production tools.

---

## Troubleshooting

### Issue 1: STM32_Programmer_CLI not found

**Error:**
```
[ERROR] STM32_Programmer_CLI not found!
```

**Solution:**
```bash
# 1. Install STM32CubeProgrammer
# Download from: https://www.st.com/stm32cubeprog

# 2. Add to PATH
export PATH="/path/to/STM32CubeProgrammer/bin:$PATH"

# 3. Or set environment variable
export STM32_PROGRAMMER_CLI="/path/to/STM32_Programmer_CLI"

# 4. Verify
STM32_Programmer_CLI --version
```

### Issue 2: External Loader Not Found

**Error:**
```
[WARNING] External loader not found, continuing without external loader...
```

**Solution:**
```bash
# 1. Check STM32CubeProgrammer installation
ls "$(dirname $(which STM32_Programmer_CLI))/ExternalLoader/"

# 2. Or copy loader to local directory
mkdir -p ExternalLoader
cp /path/to/MX66UW1G45G_STM32N6570-DK.stldr ExternalLoader/

# 3. Verify
ls -la ExternalLoader/
```

### Issue 3: Device Not Connected

**Error:**
```
Error: No ST-LINK detected
```

**Solution:**
```bash
# 1. Check device connection
STM32_Programmer_CLI -l

# 2. Check USB cable
# - Use data cable, not charging cable

# 3. Try different USB port

# 4. Check device power
# - Ensure device is powered on
```

### Issue 4: Flash Operation Failed

**Error:**
```
[ERROR] Flashing failed!
```

**Solution:**
```bash
# 1. Verify file exists
ls -lh firmware.bin

# 2. Check address is valid
# Must be hex format: 0x70100000

# 3. Check device developer mode
# - Ensure device is in developer mode (BOOT0=0, BOOT1=1)

# 4. Verify file is OTA packaged
hexdump -C firmware.bin | head -n 1
# Should show: 55 41 54 4F (OTAU magic)

# 5. Try erasing first
STM32_Programmer_CLI -c port=SWD -e all

# 6. Reset device and retry
```

### Issue 5: Permission Denied

**Error:**
```
./maker.sh: Permission denied
```

**Solution:**
```bash
# Add execute permission
chmod +x maker.sh

# Or run with bash
bash maker.sh flash firmware.bin 0x70100000
```

---

**Version**: 1.0  
**Last Updated**: October 2025  
**Maintained by**: CamThink

