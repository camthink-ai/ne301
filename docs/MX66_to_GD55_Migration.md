# MX66UM1G45G ↔ GD55LX01GE Dual-Chip Auto-Adaptive Driver

## Overview

The XSPI NOR Flash driver now supports **both** Macronix MX66UM1G45G (OctaBus) and GigaDevice GD55LX01GE (JEDEC Xccela) via automatic chip detection at boot. No compile-time selection needed.

- **Date**: 2026-06-22
- **Branch**: `halow-ver`
- **Datasheets**: `Docs/MX66UM1G45G, 1.8V, 1Gb, v1.1.pdf`, `Docs/DS-00683-GD55LX01GE-Rev1.4.pdf`

---

## 1. Chip Protocol Differences

| Feature | MX66UM1G45G | GD55LX01GE |
|---------|-------------|------------|
| **Protocol** | Macronix OctaBus (proprietary) | JEDEC Xccela (JESD251) |
| **OPI instruction width** | 16-bit (2 bytes) | 8-bit (1 byte) |
| **Instruction phase** | DTR | STR (S-D-D format) |
| **DQS pre-cycle** | Supported (CR2 bit0) | Not supported |
| **MemoryType** | `MACRONIX` | `MICRON` |
| **WEL after config write** | Preserved (stays 1) | Cleared (goes to 0) |
| **Default address mode** | 3-byte | 3-byte (ADS=0) |
| **Config register** | CR2 at `0x72`, value `0x02` | Volatile CFG at `0x81`, value `0xE7` |
| **Config address width** | 32-bit | **24-bit** |

### Command Comparison

| Operation | MX66 OPI | GD55 OPI |
|-----------|---------|---------|
| Write Enable | `0x06F9` | `0x06` |
| Read Status Reg | `0x05FA` | `0x05` |
| DTR Read | `0xEE11` | `0xFD` |
| Page Program | `0x12ED` | `0x8E` |
| Sector Erase 4K | `0x21DE` | `0x21` |
| Enter DTR OPI | `0x72` (CR2) | `0x81` (Volatile CFG) |

### Dummy Cycles

| Operation | MX66 | GD55 |
|-----------|------|------|
| RDSR (OPI DTR) | 6 | 8 |
| DTR Read | 6 | 16 |

---

## 2. Auto-Detection Flow

```
MX_XSPI2_Init()
  ├─ HAL_XSPI_Init(MICRON)            ← neutral MemoryType for SPI detection
  ├─ HAL_XSPIM_Config()               ← FSBL only (guarded by XSPI_SKIP_XSPIM_CONFIG)
  ├─ XSPI_NOR_DetectChip()            ← JEDEC 0x9F Read ID, SPI 1-line
  │     ├─ MFI=0xC2 → MX66UM1G45G
  │     └─ MFI=0xC8 → GD55LX01GE
  ├─ If MX66: Abort→DeInit→MemoryType=MACRONIX→Init→Config
  └─ XSPI_NOR_OctalDTRModeCfg()      ← chip-specific OPI configuration
```

### Manufacturer IDs

| Chip | MFI |
|------|-----|
| MX66UM1G45G | `0xC2` (Macronix) |
| GD55LX01GE | `0xC8` (GigaDevice) |

---

## 3. Chip Type Enum

```c
typedef enum {
    XSPI_NOR_CHIP_UNKNOWN = 0,
    XSPI_NOR_CHIP_MX66,
    XSPI_NOR_CHIP_GD55,
} XSPI_NOR_ChipType;

extern XSPI_NOR_ChipType xspi_nor_chip;
```

All OPI functions branch on `xspi_nor_chip` to select:

| Parameter | MX66 | GD55 |
|-----------|------|------|
| InstructionWidth | `16_BITS` | `8_BITS` |
| InstructionDTRMode | `ENABLE` | `DISABLE` |
| Commands | `MX66_OCTAL_*` | `GD55_OCTAL_*` |
| Read Dummy | 6 | 16 |
| RDSR Dummy | 6 | 8 |
| Config command | `0x72` | `0x81` |
| Config addr width | 32-bit | 24-bit |
| Config value | `0x02` | `0xE7` |
| Config done poll | WEL=1 | WIP=0 |

---

## 4. FSBL → Appli Handoff

```
FSBL main.c:
  BOOT_Copy_Application()
  XSPI_NOR_DisableMemoryMappedMode()
  XSPI_NOR_ResetAndDeInit()         ← Reset chip to SPI + DeInit XSPI2
  BOOT_Jump_Application()

XSPI_NOR_ResetAndDeInit():
  ├─ GD55: 66h→99h on 8-line STR    (JEDEC standard)
  ├─ MX66: 0x6699→0x9966 on 8-line DTR (OctaBus 2-byte)
  ├─ HAL_Delay(1)
  └─ HAL_XSPI_DeInit(&hxspi2)       ← clocks off, GPIOs de-init, IRQ disabled
```

Both chips must be reset from OPI mode to SPI before Appli boots, because **neither** MX66 OctaBus nor GD55 Xccela responds to SPI 1-line 0x9F while in OPI mode.

---

## 5. Appli: Shared FSBL Driver

Appli no longer has its own xspim files. It reuses FSBL's via relative paths:

| File | Change |
|------|--------|
| `Appli/Core/Src/xspim.c` | **Deleted** |
| `Appli/Core/Inc/xspim.h` | **Deleted** |
| `Appli/Core/Src/main.c` | `#include "../FSBL/Core/Inc/xspim.h"` |
| `Appli/Makefile` | `C_SOURCES += ../FSBL/Core/Src/xspim.c` |
| `Appli/Makefile` | `C_DEFS += -DXSPI_SKIP_XSPIM_CONFIG` |

`XSPI_SKIP_XSPIM_CONFIG` prevents `HAL_XSPIM_Config` from being called in Appli. This is necessary because `HAL_XSPIM_Config` internally tries to disable **all** XSPI instances with clocks enabled — including XSPI1 (PSRAM) which FSBL left in memory-mapped mode. Clearing XSPI1's EN bit while it's serving AXI requests causes a bus hang.

---

## 6. Final XSPI2 Init Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| MemoryType | `MICRON` (detected: `MACRONIX` if MX66) | DQS sample timing fix for GD55 |
| ClockMode | `MODE_0` | Unchanged from MX66 |
| DHQC | `ENABLE` | Unchanged from MX66 |
| SampleShifting | `NONE` | Unchanged from MX66 |
| ClockPrescaler | `1` | Unchanged from MX66 |

---

## 7. Files Modified

| File | Changes |
|------|---------|
| `FSBL/Core/Inc/xspim.h` | Dual-chip macros, chip enum, `READ_ID_CMD`, `XSPI_NOR_ResetAndDeInit` |
| `FSBL/Core/Src/xspim.c` | Auto-detection, all OPI functions branch on chip type, reset+deinit |
| `FSBL/Core/Src/main.c` | `XSPI_NOR_ResetAndDeInit()` after NOR use |
| `Appli/Core/Inc/xspim.h` | **Deleted** |
| `Appli/Core/Src/xspim.c` | **Deleted** |
| `Appli/Core/Src/main.c` | `#include "../FSBL/Core/Inc/xspim.h"` |
| `Appli/Makefile` | Source: `../FSBL/Core/Src/xspim.c`, Define: `XSPI_SKIP_XSPIM_CONFIG` |

---

## 8. Pitfalls Encountered

1. **OPI command width 2→1 byte**: MX66 OctaBus uses 2-byte commands (e.g. `0x06F9` for WREN), GD55 Xccela uses 1-byte (`0x06`).

2. **Instruction DTR→STR**: Xccela instruction phase is always STR (S-D-D format), not DTR like OctaBus.

3. **RDSR address phase**: GD55 OPI RDSR format is `8-0-(8d)` (no address). MX66 requires 4-byte address.

4. **Config reg 24-bit address**: GD55 defaults to 3-byte address mode (ADS=0). Writing the config register with 32-bit address shifts the data → writes `0x00` to Byte<0> instead of `0xE7`.

5. **MemoryType MACRONIX→MICRON**: The most subtle bug. `MACRONIX` type has internal DQS edge optimization for MX66's DQS pre-cycle. Applied to GD55 (no pre-cycle), it causes DTR read byte-swapping (e.g. `STM2` → `TS2M`, image size `0x00395020` → `0x39002050`).

6. **WEL behavior after config write**: GD55 clears WEL; MX66 preserves it. Polling WEL after GD55 config write hangs forever. Use WIP=0 instead.

7. **FSBL→Appli OPI mode persistence**: Both chips stay in OPI mode after FSBL configures them. Appli must detect them via SPI 0x9F, which fails in OPI mode. Fixed by `XSPI_NOR_ResetAndDeInit()` in FSBL before jump.

8. **HAL_XSPIM_Config re-entry hangs**: This function disables all XSPI instances before reconfiguration. In Appli, XSPI1 (PSRAM) is still in memory-mapped mode serving code execution. Clearing XSPI1 CR_EN hangs the bus. Fixed with `XSPI_SKIP_XSPIM_CONFIG`.

9. **MX66 OPI mode SPI incompatibility**: Despite early assumption, MX66 in OctaBus OPI mode also fails to respond to SPI 1-line commands (including 0x9F Read ID). Both chips now reset via chip-specific sequences.

10. **Chip-specific software reset**: GD55 uses JEDEC 66h/99h on 8-line STR. MX66 in OctaBus mode needs 0x6699/0x9966 on 8-line DTR (2-byte format).
