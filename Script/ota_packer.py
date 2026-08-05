#!/usr/bin/env python3
"""
OTA firmware package creation tool
Used to add 1K header information to bin files and generate OTA firmware packages

Structure Layout (1024 bytes total):
- Basic Information:     64 bytes  (0x00-0x3F)
- Firmware Information:  160 bytes (0x40-0xDF)
- Target Information:    64 bytes  (0xE0-0x11F)
- Dependencies:          64 bytes  (0x120-0x15F)
- Security:              416 bytes (0x160-0x2FF)
- Extensions:            259 bytes (0x300-0x402)
"""

import os
import sys
import struct
import hashlib
import zlib
import time
import argparse
from typing import Optional, Dict, Any
from dataclasses import dataclass
from pathlib import Path

# Constants definition - must match ota_header.h
OTA_HEADER_SIZE = 1024
OTA_MAGIC_NUMBER = 0x4F544155  # "OTAU"
OTA_HEADER_VERSION = 0x0100    # v1.0
OTA_MAX_NAME_LEN = 32
OTA_MAX_DESC_LEN = 64
OTA_HASH_SIZE = 32
OTA_SIGNATURE_SIZE = 256
OTA_PARTITION_NAME_LEN = 16

# Firmware type mapping
FW_TYPE_MAP = {
    'fsbl': 0x01,
    'app': 0x02,
    'web': 0x03,
    'ai_model': 0x04,
    'config': 0x05,
    'patch': 0x06,
    'full': 0x07,
    'wifi': 0x08,
}

@dataclass
class FirmwareInfo:
    """Firmware information"""
    name: str
    description: str
    fw_type: int
    version: tuple = (1, 0, 0, 0)  # (major, minor, patch, build)
    suffix: str = ''  # Optional version suffix

class OTAPacker:
    """OTA firmware package creator"""
    
    def __init__(self):
        self.header_data = bytearray(OTA_HEADER_SIZE)
    
    def _pack_string(self, data: str, max_len: int) -> bytes:
        """Pack string, ensure it doesn't exceed maximum length"""
        encoded = data.encode('utf-8')[:max_len-1]
        return encoded + b'\x00' * (max_len - len(encoded))
    
    def _calculate_crc32(self, data: bytes) -> int:
        """Calculate CRC32 checksum"""
        return zlib.crc32(data) & 0xFFFFFFFF
    
    def _calculate_sha256(self, data: bytes) -> bytes:
        """Calculate SHA256 hash"""
        return hashlib.sha256(data).digest()
    
    def create_header(self, fw_info: FirmwareInfo, fw_data: bytes) -> bytes:
        """
        Create OTA Header matching ota_header_t structure
        Total: 1024 bytes
        """
        # Clear header data
        self.header_data = bytearray(OTA_HEADER_SIZE)
        offset = 0
        
        # ========== Basic Information Section (64 bytes): 0x00-0x3F ==========
        struct.pack_into('<I', self.header_data, offset, OTA_MAGIC_NUMBER)  # 0x00: magic
        offset += 4
        struct.pack_into('<H', self.header_data, offset, OTA_HEADER_VERSION)  # 0x04: header_version
        offset += 2
        struct.pack_into('<H', self.header_data, offset, OTA_HEADER_SIZE)  # 0x06: header_size
        offset += 2
        # 0x08: header_crc32 will be filled later
        offset += 4
        struct.pack_into('<B', self.header_data, offset, fw_info.fw_type)  # 0x0C: fw_type
        offset += 1
        struct.pack_into('<B', self.header_data, offset, 0)  # 0x0D: encrypt_type (none)
        offset += 1
        struct.pack_into('<B', self.header_data, offset, 0)  # 0x0E: compress_type (none)
        offset += 1
        struct.pack_into('<B', self.header_data, offset, 0)  # 0x0F: reserved1
        offset += 1
        struct.pack_into('<I', self.header_data, offset, int(time.time()))  # 0x10: timestamp
        offset += 4
        struct.pack_into('<I', self.header_data, offset, 1)  # 0x14: sequence
        offset += 4
        # 0x18: total_package_size (firmware + header)
        total_size = len(fw_data) + OTA_HEADER_SIZE
        struct.pack_into('<I', self.header_data, offset, total_size)
        offset += 4
        # 0x1C-0x3F: reserved2 (36 bytes) already zero
        offset += 36
        
        # ========== Firmware Information Section (160 bytes): 0x40-0xDF ==========
        assert offset == 0x40, f"Offset mismatch at firmware section: {offset:04X} != 0x40"
        
        # 0x40-0x5F: fw_name (32 bytes)
        fw_name_bytes = self._pack_string(fw_info.name, OTA_MAX_NAME_LEN)
        self.header_data[offset:offset+OTA_MAX_NAME_LEN] = fw_name_bytes
        offset += OTA_MAX_NAME_LEN
        
        # 0x60-0x9F: fw_desc (64 bytes)
        # Construct fw_desc: "description (version_suffix)" or just "description"
        major, minor, patch, build = fw_info.version
        version_str = f"{major}.{minor}.{patch}.{build}"
        if fw_info.suffix:
            version_str = f"{version_str}_{fw_info.suffix}"
        fw_desc = f"{fw_info.description} ({version_str})"
        fw_desc_bytes = self._pack_string(fw_desc, OTA_MAX_DESC_LEN)
        self.header_data[offset:offset+OTA_MAX_DESC_LEN] = fw_desc_bytes
        offset += OTA_MAX_DESC_LEN
        
        # 0xA0-0xA7: fw_ver[8] - version as byte array
        # Format: [major, minor, patch, build_low, build_high, 0, 0, 0]
        # This allows BUILD numbers up to 65535 while maintaining backward compatibility
        # (older firmware only reads build_low, newer reads build_low + build_high*256)
        major, minor, patch, build = fw_info.version[0], fw_info.version[1], fw_info.version[2], fw_info.version[3]
        struct.pack_into('<B', self.header_data, offset + 0, min(major, 255))
        struct.pack_into('<B', self.header_data, offset + 1, min(minor, 255))
        struct.pack_into('<B', self.header_data, offset + 2, min(patch, 255))
        struct.pack_into('<B', self.header_data, offset + 3, build & 0xFF)         # build low byte
        struct.pack_into('<B', self.header_data, offset + 4, (build >> 8) & 0xFF)  # build high byte
        offset += 8
        
        # 0xA8-0xAF: min_ver[8] - same format as fw_ver
        struct.pack_into('<B', self.header_data, offset + 0, min(major, 255))
        struct.pack_into('<B', self.header_data, offset + 1, min(minor, 255))
        struct.pack_into('<B', self.header_data, offset + 2, min(patch, 255))
        struct.pack_into('<B', self.header_data, offset + 3, build & 0xFF)
        struct.pack_into('<B', self.header_data, offset + 4, (build >> 8) & 0xFF)
        offset += 8
        
        # 0xB0: fw_size
        struct.pack_into('<I', self.header_data, offset, len(fw_data))
        offset += 4
        # 0xB4: fw_size_compressed
        struct.pack_into('<I', self.header_data, offset, len(fw_data))
        offset += 4
        # 0xB8: fw_crc32
        fw_crc32 = self._calculate_crc32(fw_data)
        struct.pack_into('<I', self.header_data, offset, fw_crc32)
        offset += 4
        
        # 0xBC-0xDB: fw_hash (32 bytes) - SHA256 of firmware
        fw_hash = self._calculate_sha256(fw_data)
        self.header_data[offset:offset+32] = fw_hash
        offset += 32
        
        # 0xDC-0xDF: reserved3 (4 bytes)
        offset += 4
        
        # ========== Target Information Section (64 bytes): 0xE0-0x11F ==========
        offset += 64  # All zeros for now
        
        # ========== Dependency Information Section (64 bytes): 0x120-0x15F ==========
        offset += 64  # All zeros (reserved)
        
        # ========== Security Information Section (416 bytes): 0x160-0x2FF ==========
        offset += 416  # All zeros (reserved)
        
        # ========== Extension Information Section (256 bytes): 0x300-0x3FF ==========
        offset += 256  # All zeros (reserved)
        
        # Verify we're at exactly 1024 bytes
        assert offset == OTA_HEADER_SIZE, f"Header size mismatch: {offset} != {OTA_HEADER_SIZE}"
        
        # Calculate and fill header CRC32 (excluding CRC32 field itself)
        header_for_crc = bytearray(self.header_data)
        struct.pack_into('<I', header_for_crc, 8, 0)  # Set CRC32 field to 0
        header_crc32 = self._calculate_crc32(bytes(header_for_crc))
        struct.pack_into('<I', self.header_data, 8, header_crc32)
        
        return bytes(self.header_data)
    
    def pack_firmware(self, input_file: str, output_file: str, fw_info: FirmwareInfo) -> bool:
        """Pack firmware file"""
        try:
            # Read original firmware file
            with open(input_file, 'rb') as f:
                fw_data = f.read()
            
            print(f"Reading firmware file: {input_file} ({len(fw_data)} bytes)")
            
            # Create header
            header = self.create_header(fw_info, fw_data)
            
            # Write OTA firmware package (header + firmware data)
            with open(output_file, 'wb') as f:
                f.write(header)   # 1K header information
                f.write(fw_data)  # Original firmware data
            
            print(f"Generated OTA firmware package: {output_file} ({len(header) + len(fw_data)} bytes)")
            print(f"  - Header: {len(header)} bytes")
            print(f"  - Firmware data: {len(fw_data)} bytes")
            
            return True
            
        except Exception as e:
            print(f"Error: {e}")
            return False
    

def main():
    parser = argparse.ArgumentParser(
        description='OTA firmware package creation tool',
        epilog='Example: python ota_packer.py firmware.bin -o output.bin -t app -n "APP" -d "Description" -v 1.0.0.1'
    )
    parser.add_argument('input', nargs='?', help='Input bin file path')
    parser.add_argument('-o', '--output', help='Output OTA firmware package path (REQUIRED)')
    parser.add_argument('-n', '--name', help='Firmware name (REQUIRED)')
    parser.add_argument('-d', '--desc', help='Firmware description (REQUIRED)')
    parser.add_argument('-t', '--type', choices=list(FW_TYPE_MAP.keys()), help='Firmware type (REQUIRED)')
    parser.add_argument('-v', '--version', help='Version number (REQUIRED, format: major.minor.patch.build)')
    parser.add_argument('-s', '--suffix', help='Version suffix (optional, e.g., alpha, beta, rc1)')
    parser.add_argument('--clean', action='store_true', help='Remove all generated OTA package files (*_ota.bin)')
    parser.add_argument('--force', action='store_true', help='Force clean without confirmation (use with --clean)')
    
    args = parser.parse_args()
    
    # Validate arguments for clean mode
    if args.clean:
        # Clean mode doesn't need other arguments
        pass
    else:
        # Normal mode requires all parameters
        if not args.input:
            parser.error('input file is required')
            return 1
        if not args.output:
            parser.error('-o/--output is required')
            return 1
        if not args.type:
            parser.error('-t/--type is required')
            return 1
        if not args.name:
            parser.error('-n/--name is required')
            return 1
        if not args.desc:
            parser.error('-d/--desc is required')
            return 1
        if not args.version:
            parser.error('-v/--version is required')
            return 1
    
    packer = OTAPacker()
    
    if args.clean:
        # Clean mode - remove all OTA packages
        ota_files = [f for f in os.listdir('.') if f.endswith('_ota.bin')]
        
        if not ota_files:
            print("No OTA package files found to clean.")
            return 0
        
        print(f"Found {len(ota_files)} OTA package file(s) to remove:")
        for ota_file in ota_files:
            print(f"  - {ota_file}")
        
        # Ask for confirmation unless --force is used
        if not args.force:
            try:
                response = input("\nAre you sure you want to delete these files? [y/N]: ")
                if response.lower() not in ['y', 'yes']:
                    print("Clean operation cancelled.")
                    return 0
            except (KeyboardInterrupt, EOFError):
                print("\nClean operation cancelled.")
                return 0
        else:
            print("\nForce mode: Skipping confirmation...")
        
        # Remove files
        removed_count = 0
        failed_count = 0
        for ota_file in ota_files:
            try:
                os.remove(ota_file)
                print(f"  ✓ Removed: {ota_file}")
                removed_count += 1
            except Exception as e:
                print(f"  ✗ Failed to remove {ota_file}: {e}")
                failed_count += 1
        
        print(f"\nClean completed: {removed_count} removed, {failed_count} failed")
        return 0 if failed_count == 0 else 1
    
    else:
        # Single file processing mode with all required parameters
        if not os.path.exists(args.input):
            print(f"Error: Input file does not exist: {args.input}")
            return 1
        
        # Parse version
        try:
            version_parts = [int(x) for x in args.version.split('.')]
            if len(version_parts) != 4:
                print("Error: Version must have exactly 4 parts (major.minor.patch.build)")
                print("Example: 1.0.0.1")
                return 1
        except ValueError:
            print("Error: Incorrect version format, should be major.minor.patch.build")
            print("Example: 1.0.0.1")
            return 1
        
        # Create firmware information with all required fields
        fw_info = FirmwareInfo(
            name=args.name,
            description=args.desc,
            fw_type=FW_TYPE_MAP[args.type],
            version=tuple(version_parts),
            suffix=args.suffix if args.suffix else ''
        )
        
        # Use specified output filename
        output_file = args.output
        
        print(f"Firmware information:")
        print(f"  Name: {fw_info.name}")
        print(f"  Description: {fw_info.description}")
        print(f"  Type: {args.type} ({fw_info.fw_type})")
        version_display = '.'.join(map(str, fw_info.version))
        if fw_info.suffix:
            version_display = f"{version_display}_{fw_info.suffix}"
        print(f"  Version: {version_display}")
        print(f"  Output file: {output_file}")
        
        if not packer.pack_firmware(args.input, output_file, fw_info):
            return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
