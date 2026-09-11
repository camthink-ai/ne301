# OTA Header Structure Documentation

## Overview

The OTA (Over-The-Air) firmware package uses a fixed 1024-byte header structure to store firmware metadata, version information, and security data. This document describes the complete structure layout.

## Structure Size

**Total Size: 1024 bytes (0x400)**

All fields use **little-endian** byte order and the structure is **packed** (no padding bytes).

## Memory Layout

```
┌─────────────────────────────────────────────────┐
│  Basic Information        (64 bytes)   0x00-0x3F │
├─────────────────────────────────────────────────┤
│  Firmware Information     (160 bytes)  0x40-0xDF │
├─────────────────────────────────────────────────┤
│  Target Information       (64 bytes)   0xE0-0x11F│
├─────────────────────────────────────────────────┤
│  Dependencies             (64 bytes)   0x120-0x15F│
├─────────────────────────────────────────────────┤
│  Security Information     (416 bytes)  0x160-0x2FF│
├─────────────────────────────────────────────────┤
│  Extension Information    (256 bytes)  0x300-0x3FF│
└─────────────────────────────────────────────────┘
```

## Section Details

### 1. Basic Information Section (64 bytes: 0x00-0x3F)

| Offset | Size | Type    | Name              | Description |
|--------|------|---------|-------------------|-------------|
| 0x00   | 4    | uint32  | magic             | Magic number: 0x4F544155 ("OTAU") |
| 0x04   | 2    | uint16  | header_version    | Header version: 0x0100 (v1.0) |
| 0x06   | 2    | uint16  | header_size       | Header size: 1024 |
| 0x08   | 4    | uint32  | header_crc32      | Header CRC32 checksum |
| 0x0C   | 1    | uint8   | fw_type           | Firmware type (see types below) |
| 0x0D   | 1    | uint8   | encrypt_type      | Encryption type |
| 0x0E   | 1    | uint8   | compress_type     | Compression type |
| 0x0F   | 1    | uint8   | reserved1         | Reserved |
| 0x10   | 4    | uint32  | timestamp         | Unix timestamp |
| 0x14   | 4    | uint32  | sequence          | Sequence number |
| 0x18   | 4    | uint32  | total_package_size| Total size (header + firmware) |
| 0x1C   | 4    | uint32  | device_model      | Device model id (0x3010 = NE301, 0 = unstamped/legacy); mismatched packages are rejected |
| 0x20   | 32   | uint8[] | reserved2         | Reserved bytes |

**Firmware Types:**
- 0x00: Unknown
- 0x01: FSBL (First Stage Boot Loader)
- 0x02: Application
- 0x03: Web Assets
- 0x04: AI Model
- 0x05: Configuration
- 0x06: Patch
- 0x07: Full Package

**Encryption Types:**
- 0x00: None
- 0x01: AES-128
- 0x02: AES-256

**Compression Types:**
- 0x00: None
- 0x01: GZIP
- 0x02: LZ4

### 2. Firmware Information Section (160 bytes: 0x40-0xDF)

| Offset | Size | Type    | Name              | Description |
|--------|------|---------|-------------------|-------------|
| 0x40   | 32   | char[]  | fw_name           | Firmware name (null-terminated) |
| 0x60   | 64   | char[]  | fw_desc           | Firmware description |
| 0xA0   | 8    | uint8[] | fw_ver            | Version [major, minor, patch, build, 0, 0, 0, 0] |
| 0xA8   | 8    | uint8[] | min_ver           | Minimum compatible version |
| 0xB0   | 4    | uint32  | fw_size           | Original firmware size |
| 0xB4   | 4    | uint32  | fw_size_compressed| Compressed firmware size |
| 0xB8   | 4    | uint32  | fw_crc32          | Firmware CRC32 checksum |
| 0xBC   | 32   | uint8[] | fw_hash           | SHA256 hash of firmware |
| 0xDC   | 4    | uint8[] | reserved3         | Reserved |

**Version Format:**
- Each version is stored as 8 bytes: `[major, minor, patch, build, 0, 0, 0, 0]`
- Example: Version 1.2.3.4 = `[0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00]`

### 3. Target Information Section (64 bytes: 0xE0-0x11F)

| Offset | Size | Type    | Name              | Description |
|--------|------|---------|-------------------|-------------|
| 0xE0   | 4    | uint32  | part_table_crc    | CRC32 of the mem_map.h partition table this package was built for (0 = not stamped) |
| 0xE4   | 4    | uint32  | target_size       | Target region size |
| 0xE8   | 4    | uint32  | target_offset     | Target offset address |
| 0xEC   | 16   | char[]  | target_partition  | Target partition name |
| 0xFC   | 4    | uint32  | hw_version        | Hardware version requirement |
| 0x100  | 4    | uint32  | chip_id           | Chip ID requirement |
| 0x104  | 28   | uint8[] | reserved4         | Reserved |

### 4. Dependencies Section (64 bytes: 0x120-0x15F)

| Offset | Size | Type    | Name                  | Description |
|--------|------|---------|-----------------------|-------------|
| 0x120  | 64   | uint8[] | reserved_dependencies | Reserved for future use |

### 5. Security Information Section (416 bytes: 0x160-0x2FF)

| Offset | Size | Type    | Name      | Description |
|--------|------|---------|-----------|-------------|
| 0x160  | 416  | uint8[] | reserved5 | Reserved for security features |

*This section can be used for digital signatures, certificates, public keys, etc.*

### 6. Extension Information Section (256 bytes: 0x300-0x3FF)

| Offset | Size | Type    | Name      | Description |
|--------|------|---------|-----------|-------------|
| 0x300  | 256  | uint8[] | reserved6 | Reserved for custom extensions |

## C Structure Definition

```c
typedef struct __attribute__((packed)) {
    /* Basic Information (64 bytes) */
    uint32_t magic;                         // 0x00
    uint16_t header_version;                // 0x04
    uint16_t header_size;                   // 0x06
    uint32_t header_crc32;                  // 0x08
    uint8_t  fw_type;                       // 0x0C
    uint8_t  encrypt_type;                  // 0x0D
    uint8_t  compress_type;                 // 0x0E
    uint8_t  reserved1;                     // 0x0F
    uint32_t timestamp;                     // 0x10
    uint32_t sequence;                      // 0x14
    uint32_t total_package_size;            // 0x18
    uint32_t device_model;                  // 0x1C
    uint8_t  reserved2[32];                 // 0x20-0x3F
    
    /* Firmware Information (160 bytes) */
    char     fw_name[32];                   // 0x40-0x5F
    char     fw_desc[64];                   // 0x60-0x9F
    uint8_t  fw_ver[8];                     // 0xA0-0xA7
    uint8_t  min_ver[8];                    // 0xA8-0xAF
    uint32_t fw_size;                       // 0xB0
    uint32_t fw_size_compressed;            // 0xB4
    uint32_t fw_crc32;                      // 0xB8
    uint8_t  fw_hash[32];                   // 0xBC-0xDB
    uint8_t  reserved3[4];                  // 0xDC-0xDF
    
    /* Target Information (64 bytes) */
    uint32_t part_table_crc;                // 0xE0
    uint32_t target_size;                   // 0xE4
    uint32_t target_offset;                 // 0xE8
    char     target_partition[16];          // 0xEC-0xFB
    uint32_t hw_version;                    // 0xFC
    uint32_t chip_id;                       // 0x100
    uint8_t  reserved4[28];                 // 0x104-0x11F
    
    /* Dependencies (64 bytes) */
    uint8_t  reserved_dependencies[64];     // 0x120-0x15F
    
    /* Security (416 bytes) */
    uint8_t  reserved5[416];                // 0x160-0x2FF
    
    /* Extensions (256 bytes) */
    uint8_t  reserved6[256];                // 0x300-0x3FF
} ota_header_t;
```

## Python Structure Parsing

```python
import struct

def parse_ota_header(header_bytes):
    """Parse OTA header (1024 bytes)"""
    
    # Basic Information
    magic = struct.unpack_from('<I', header_bytes, 0x00)[0]
    header_version = struct.unpack_from('<H', header_bytes, 0x04)[0]
    header_size = struct.unpack_from('<H', header_bytes, 0x06)[0]
    header_crc32 = struct.unpack_from('<I', header_bytes, 0x08)[0]
    fw_type = struct.unpack_from('<B', header_bytes, 0x0C)[0]
    timestamp = struct.unpack_from('<I', header_bytes, 0x10)[0]
    total_size = struct.unpack_from('<I', header_bytes, 0x18)[0]
    
    # Firmware Information
    fw_name = header_bytes[0x40:0x60].rstrip(b'\x00').decode('utf-8')
    fw_desc = header_bytes[0x60:0xA0].rstrip(b'\x00').decode('utf-8')
    fw_ver = list(struct.unpack_from('<8B', header_bytes, 0xA0))
    fw_size = struct.unpack_from('<I', header_bytes, 0xB0)[0]
    fw_crc32 = struct.unpack_from('<I', header_bytes, 0xB8)[0]
    fw_hash = header_bytes[0xBC:0xDC]
    
    return {
        'magic': magic,
        'fw_type': fw_type,
        'fw_name': fw_name,
        'fw_desc': fw_desc,
        'fw_version': f"{fw_ver[0]}.{fw_ver[1]}.{fw_ver[2]}.{fw_ver[3]}",
        'fw_size': fw_size,
        'timestamp': timestamp,
    }
```

## CRC32 Calculation

### Header CRC32
The header CRC32 is calculated over the **entire 1024-byte header** with the CRC32 field itself set to 0:

```python
import zlib

def calculate_header_crc32(header_bytes):
    header_copy = bytearray(header_bytes)
    struct.pack_into('<I', header_copy, 0x08, 0)  # Zero out CRC32 field
    return zlib.crc32(bytes(header_copy)) & 0xFFFFFFFF
```

### Firmware CRC32
The firmware CRC32 is calculated over the actual firmware binary data (excluding the header):

```python
def calculate_firmware_crc32(firmware_bytes):
    return zlib.crc32(firmware_bytes) & 0xFFFFFFFF
```

## SHA256 Hash

The firmware SHA256 hash is calculated over the firmware binary data:

```python
import hashlib

def calculate_firmware_sha256(firmware_bytes):
    return hashlib.sha256(firmware_bytes).digest()
```

## Compatibility Notes

1. **No Nested Structures**: All fields are plain types (uint8_t, uint16_t, uint32_t, char arrays)
2. **No Enums in Structure**: Uses uint8_t with #define constants instead
3. **Packed Structure**: No padding bytes, guaranteed 1024 bytes
4. **Little Endian**: All multi-byte values use little-endian byte order
5. **Cross-Platform**: Can be easily parsed in C, Python, JavaScript, etc.

## Tools

### Create OTA Package
```bash
python ota_packer.py firmware.bin -o output_ota.bin \
    -t app -n "MyApp" -d "Description" -v 1.0.0.1
```

### Verify OTA Package
```bash
python verify_ota_package.py output_ota.bin
```

## Version History

- **v1.0** (2025-10-16): Initial structure definition
  - Fixed 1024-byte header
  - No nested structures or enums
  - Version stored as byte array
  - SHA256 hash integrated into firmware section

