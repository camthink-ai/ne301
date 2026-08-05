#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pack multiple .bin files in the build directory into a single .hex file.
Automatically select the latest version of each file.
"""

import os
import sys
import re
import glob
import struct
from datetime import datetime
from pathlib import Path
from typing import Optional, Tuple, List

# Address mapping configuration
ADDRESS_MAP = {
    'FSBL': {
        'pattern': 'ne301_FSBL_signed.bin',
        'address': 0x70000000,
        'required': True
    },
    'NVS': {
        'pattern': None,  # NVS area: 64KB of blank data
        'address': 0x70080000,
        'size': 0x10000,  # 64KB
        'required': False
    },
    'OTA': {
        'pattern': None,  # OTA area: 8KB of blank data
        'address': 0x70090000,
        'size': 0x2000,  # 8KB
        'required': False
    },
    'APP': {
        'pattern': 'ne301_App_signed_v*_pkg.bin',
        'address': 0x70100000,
        'required': True
    },
    'WEB': {
        'pattern': 'ne301_Web_v*_pkg.bin',
        'address': 0x71900000,
        'required': True
    },
    'MODEL': {
        'pattern': 'ne301_Model_v*_pkg.bin',
        'address': 0x70900000,
        'required': True
    }
}


def parse_version(filename: str) -> Optional[Tuple[int, int, int, int]]:
    """
    Parse version number from file name.
    Example: ne301_App_signed_v1.0.1.1234_pkg.bin -> (1, 0, 1, 1234)
    """
    match = re.search(r'v(\d+)\.(\d+)\.(\d+)\.(\d+)', filename)
    if match:
        return tuple(int(x) for x in match.groups())
    return None


def parse_wifi_version(filename: str) -> Optional[Tuple[int, int, int, int, int, int, int]]:
    """
    Parse version number from WiFi firmware file name.
    Example: SiWG917-B.2.14.5.2.0.7.rps -> (2, 14, 5, 2, 0, 7)
    """
    match = re.search(r'SiWG917-B\.(\d+)\.(\d+)\.(\d+)\.(\d+)\.(\d+)\.(\d+)\.rps', filename)
    if match:
        return tuple(int(x) for x in match.groups())
    return None


def find_latest_wifi_firmware(project_root: Path) -> Optional[Path]:
    """
    Find the latest WiFi firmware file (.rps file).
    """
    wifi_dir = project_root / 'Custom' / 'Common' / 'Lib' / 'SiliconLabs_SDK' / 'firmware'
    if not wifi_dir.exists():
        return None
    
    # Find all matching .rps files
    files = list(wifi_dir.glob('SiWG917-B.*.rps'))
    if not files:
        return None
    
    # Sort by version number
    files_with_version = []
    for f in files:
        version = parse_wifi_version(f.name)
        if version:
            files_with_version.append((version, f))
    
    if files_with_version:
        # Sort by version in descending order
        files_with_version.sort(key=lambda x: x[0], reverse=True)
        return files_with_version[0][1]
    else:
        # If there is no version, sort by modification time
        files.sort(key=lambda x: x.stat().st_mtime, reverse=True)
        return files[0]


def find_latest_file(build_dir: Path, pattern: str) -> Optional[Path]:
    """
    Find the latest file matching the pattern (sorted by version).
    """
    files = list(build_dir.glob(pattern))
    if not files:
        return None
    
    # If version is present, sort by version
    files_with_version = []
    for f in files:
        version = parse_version(f.name)
        if version:
            files_with_version.append((version, f))
    
    if files_with_version:
        # Sort by version in descending order
        files_with_version.sort(key=lambda x: x[0], reverse=True)
        return files_with_version[0][1]
    else:
        # If there is no version, sort by modification time
        files.sort(key=lambda x: x.stat().st_mtime, reverse=True)
        return files[0]


# WiFi flash layout (must match wifi.h on the device side)
# [0 .. 31]      : flash_header_t (32 bytes)
# [32 .. 32+N-1] : original WiFi firmware (.rps), internally containing a 64-byte FW header + payload
WIFI_FLASH_BASE_ADDR = 0x71A00000
WIFI_FLASH_HEADER_SIZE = 32
WIFI_FLASH_VALID_FLAGS = 0x20060123


def calculate_checksum(data: bytes) -> int:
    """
    Calculate the checksum of an Intel HEX record.
    Checksum = 0xFF - (sum of all bytes & 0xFF) + 1
    Or more simply: checksum = (~sum(data) + 1) & 0xFF
    """
    return (0x100 - (sum(data) & 0xFF)) & 0xFF


def stm32_crc32_mpeg2(data: bytes) -> int:
    """
    Calculate a CRC32 compatible with the STM32 CRC peripheral (CRC32-MPEG2 style).

    Parameters corresponding to HAL configuration:
      - Polynomial: 0x04C11DB7
      - Initial value: 0xFFFFFFFF (DefaultInitValueUse)
      - No input/output bit reversal
      - No final XOR (XorOut = 0x00000000)
      - Input data fed as bytes (InputDataFormat = BYTES)
    """
    poly = 0x04C11DB7
    crc = 0xFFFFFFFF

    for b in data:
        crc ^= (b << 24) & 0xFFFFFFFF
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ poly) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF

    # HAL does not apply an additional final XOR, return directly
    return crc & 0xFFFFFFFF


def build_wifi_flash_image(project_root: Path) -> Optional[dict]:
    """
    Build the WiFi flash image: flash_header_t (32B) + original .rps firmware.

    This is byte-for-byte identical to what gets embedded in the WIFI region of
    the full HEX file (see pack_bin_to_hex), so anything flashed from this image
    matches the conversion script's final output exactly.

    Returns a dict with the image bytes and metadata, or None if no .rps found:
      data            : bytes (flash_header + raw .rps)
      rps_path        : Path to the resolved .rps file
      rps_name        : .rps filename (e.g. SiWG917-B.2.15.5.0.0.2.rps)
      version_4part   : 'X.Y.Z.B' derived for the OTA fw_ver field
      version_full    : full SiWG version stem (e.g. SiWG917-B.2.15.5.0.0.2)
      fw_total_size   : len(raw .rps)
      fw_crc          : STM32 CRC32-MPEG2 over the raw .rps (flash_header.fw_crc)
    """
    wifi_firmware = find_latest_wifi_firmware(project_root)
    if wifi_firmware is None:
        return None

    try:
        with open(wifi_firmware, 'rb') as f:
            raw_wifi_data = f.read()
    except Exception as e:
        print(f"[ERROR] Failed to read WiFi firmware {wifi_firmware}: {e}")
        return None

    fw_total_size = len(raw_wifi_data)
    # flash_header.fw_crc uses the same CRC32-MPEG2 as the STM32 HAL peripheral
    fw_crc = stm32_crc32_mpeg2(raw_wifi_data)

    # flash_header_t (little-endian, packed): valid_flags, fw_total_size, fw_crc, reserved[5]
    flash_header = struct.pack(
        "<III5I",
        WIFI_FLASH_VALID_FLAGS,
        fw_total_size,
        fw_crc,
        0, 0, 0, 0, 0,
    )

    # Derive a 4-part version for the OTA fw_ver field. The SiWG917 .rps has a
    # 6-component version (Major.Minor.Patch.Security.Customer.Build).  We use
    # Major.Minor.Patch.(Security*100 + Build) so comparisons stay monotonic.
    # Example: 2.14.5.2.0.7  -> 2.14.5.207
    #          2.15.5.0.0.2  -> 2.15.5.2
    # The full SiWG917 string is preserved in the OTA package description.
    v6 = parse_wifi_version(wifi_firmware.name)
    if v6:
        major, minor, patch = v6[0], v6[1], v6[2]
        security = v6[3]
        build = v6[5]
        version_4part = f"{major}.{minor}.{patch}.{security * 100 + build}"
    else:
        version_4part = "0.0.0.0"
    version_full = wifi_firmware.stem  # e.g. SiWG917-B.2.15.5.0.0.2

    return {
        'data': flash_header + raw_wifi_data,
        'rps_path': wifi_firmware,
        'rps_name': wifi_firmware.name,
        'version_4part': version_4part,
        'version_full': version_full,
        'fw_total_size': fw_total_size,
        'fw_crc': fw_crc,
    }


def write_extended_linear_address(hex_file, address: int) -> None:
    """
    Write an extended linear address record (type 04).
    Used to set the upper 16 bits of a 32-bit address.
    """
    high_addr = (address >> 16) & 0xFFFF
    record = bytes([0x02, 0x00, 0x00, 0x04,  # length=2, address=0x0000, type=04
                    (high_addr >> 8) & 0xFF, high_addr & 0xFF])
    checksum = calculate_checksum(record)
    hex_file.write(f":{record.hex().upper()}{checksum:02X}\n")


def write_data_record(hex_file, address: int, data: bytes, current_high_addr_ref: list) -> None:
    """
    Write a data record (type 00).
    If data crosses a 64KB boundary, an extended address record will be inserted automatically.
    current_high_addr_ref: [int] current high 16 bits of the extended address (passed by reference)
    """
    # Write at most 16 bytes at a time (Intel HEX standard)
    offset = 0
    while offset < len(data):
        chunk_size = min(16, len(data) - offset)
        chunk = data[offset:offset + chunk_size]
        # Calculate current address
        current_addr = address + offset
        current_high = (current_addr >> 16) & 0xFFFF
        low_addr = current_addr & 0xFFFF
        
        # If the high 16 bits of the address change, insert an extended address record
        if current_high_addr_ref[0] != current_high:
            write_extended_linear_address(hex_file, current_addr)
            current_high_addr_ref[0] = current_high
        
        record = bytes([chunk_size, (low_addr >> 8) & 0xFF, low_addr & 0xFF, 0x00]) + chunk
        checksum = calculate_checksum(record)
        
        hex_file.write(f":{record.hex().upper()}{checksum:02X}\n")
        offset += chunk_size


def write_end_of_file(hex_file) -> None:
    """
    Write an End-of-File record (type 01).
    """
    record = bytes([0x00, 0x00, 0x00, 0x01])
    checksum = calculate_checksum(record)
    hex_file.write(f":{record.hex().upper()}{checksum:02X}\n")


def pack_bin_to_hex(build_dir: Path, output_file: Path, include_wifi: bool = False, project_root: Path = None) -> bool:
    """
    Pack multiple .bin files into a single .hex file.
    include_wifi: whether to include WiFi firmware
    project_root: project root directory, used to find WiFi firmware
    """
    print("=" * 60)
    if include_wifi:
        print("Packing firmware into HEX file (including WiFi firmware)")
    else:
        print("Packing firmware into HEX file")
    print("=" * 60)
    
    # Find all required files
    files_to_pack = []
    base_address = 0
    
    for name, config in ADDRESS_MAP.items():
        if config['pattern'] is None:
            # Handle blank data filling (flash-erased value is 0xFF)
            print(f"\n[{name}]")
            print(f"  Address: 0x{config['address']:08X}")
            print(f"  Size: {config['size']} bytes (blank data, filled with 0xFF)")
            files_to_pack.append({
                'name': name,
                'address': config['address'],
                'data': bytes([0xFF] * config['size']),
                'file': None
            })
            continue
        
        pattern = config['pattern']
        file_path = find_latest_file(build_dir, pattern)
        
        if file_path is None:
            if config['required']:
                print(f"\n[ERROR] Required file not found: {pattern}")
                return False
            else:
                print(f"\n[WARNING] File not found: {pattern} (skipping)")
                continue
        
        # Read file content
        try:
            with open(file_path, 'rb') as f:
                data = f.read()
        except Exception as e:
            print(f"\n[ERROR] Failed to read file {file_path}: {e}")
            return False
        
        print(f"\n[{name}]")
        print(f"  File: {file_path.name}")
        print(f"  Address: 0x{config['address']:08X}")
        print(f"  Size: {len(data)} bytes")
        
        files_to_pack.append({
            'name': name,
            'address': config['address'],
            'data': data,
            'file': file_path
        })
    
    # If including WiFi firmware, add WiFi firmware (with flash header)
    if include_wifi and project_root:
        wifi_info = build_wifi_flash_image(project_root)
        if wifi_info:
            print(f"\n[WIFI]")
            print(f"  File: {wifi_info['rps_name']}")
            print(f"  Base address: 0x{WIFI_FLASH_BASE_ADDR:08X}")
            print(f"  Original size: {wifi_info['fw_total_size']} bytes")
            print(f"  flash_header.valid_flags: 0x{WIFI_FLASH_VALID_FLAGS:08X}")
            print(f"  flash_header.fw_total_size: {wifi_info['fw_total_size']} bytes")
            print(f"  flash_header.fw_crc: 0x{wifi_info['fw_crc']:08X}")
            print(f"  Total programmed size (including flash header): {len(wifi_info['data'])} bytes")

            files_to_pack.append({
                'name': 'WIFI',
                'address': WIFI_FLASH_BASE_ADDR,
                'data': wifi_info['data'],
                'file': wifi_info['rps_path'],
            })
        else:
            print(f"\n[WARNING] WiFi firmware file not found")
            print("  Continue packing without WiFi firmware")
    
    # Sort by address
    files_to_pack.sort(key=lambda x: x['address'])
    
    # Check for address overlap
    for i in range(len(files_to_pack) - 1):
        current = files_to_pack[i]
        next_item = files_to_pack[i + 1]
        current_end = current['address'] + len(current['data'])
        if current_end > next_item['address']:
            print(f"\n[WARNING] Address overlap detected:")
            print(f"  {current['name']} end address: 0x{current_end:08X}")
            print(f"  {next_item['name']} start address: 0x{next_item['address']:08X}")
    
    # Write HEX file
    print(f"\nGenerating HEX file: {output_file}")
    
    try:
        with open(output_file, 'w', encoding='ascii') as hex_file:
            # Use a list to pass by reference so write_data_record can modify it
            current_high_addr = [None]
            
            for item in files_to_pack:
                item_address = item['address']
                item_data = item['data']
                
                # Check whether the high 16 bits of the address have changed
                item_high = (item_address >> 16) & 0xFFFF
                if current_high_addr[0] != item_high:
                    # Write extended linear address record (type 04)
                    write_extended_linear_address(hex_file, item_address)
                    current_high_addr[0] = item_high
                
                # Write data record (type 00)
                # If data crosses a 64KB boundary, extended address records are inserted automatically
                write_data_record(hex_file, item_address, item_data, current_high_addr)
            
            # Write End-of-File record (type 01)
            write_end_of_file(hex_file)
        
        # Get resulting file size
        output_size = output_file.stat().st_size
        print(f"\n[SUCCESS] HEX file generated successfully!")
        print(f"  Output file: {output_file}")
        print(f"  File size: {output_size} bytes")
        print("=" * 60)
        return True
        
    except Exception as e:
        print(f"\n[ERROR] Failed to generate HEX file: {e}")
        return False


def pack_wakecore_to_hex(build_dir: Path, output_file: Path) -> bool:
    """
    Pack the WakeCore .bin file into a separate .hex file.
    """
    print("=" * 60)
    print("Packing WakeCore into HEX file")
    print("=" * 60)
    
    # WakeCore configuration
    WAKECORE_PATTERN = 'ne301_WakeCore.bin'
    WAKECORE_ADDRESS = 0x8000000
    
    # Find WakeCore file
    wakecore_file = find_latest_file(build_dir, WAKECORE_PATTERN)
    
    if wakecore_file is None:
        print(f"\n[ERROR] WakeCore file not found: {WAKECORE_PATTERN}")
        return False
    
    # Read file content
    try:
        with open(wakecore_file, 'rb') as f:
            data = f.read()
    except Exception as e:
        print(f"\n[ERROR] Failed to read file {wakecore_file}: {e}")
        return False
    
    print(f"\n[WakeCore]")
    print(f"  File: {wakecore_file.name}")
    print(f"  Address: 0x{WAKECORE_ADDRESS:08X}")
    print(f"  Size: {len(data)} bytes")
    
    # Write HEX file
    print(f"\nGenerating HEX file: {output_file}")
    
    try:
        with open(output_file, 'w', encoding='ascii') as hex_file:
            # Use a list to pass by reference so write_data_record can modify it
            current_high_addr = [None]
            
            # Check the high 16 bits of the address
            item_high = (WAKECORE_ADDRESS >> 16) & 0xFFFF
            if current_high_addr[0] != item_high:
                # Write extended linear address record (type 04)
                write_extended_linear_address(hex_file, WAKECORE_ADDRESS)
                current_high_addr[0] = item_high
            
            # Write data record (type 00)
            # If data crosses a 64KB boundary, extended address records are inserted automatically
            write_data_record(hex_file, WAKECORE_ADDRESS, data, current_high_addr)
            
            # Write End-of-File record (type 01)
            write_end_of_file(hex_file)
        
        # Get resulting file size
        output_size = output_file.stat().st_size
        print(f"\n[SUCCESS] HEX file generated successfully!")
        print(f"  Output file: {output_file}")
        print(f"  File size: {output_size} bytes")
        print("=" * 60)
        return True
        
    except Exception as e:
        print(f"\n[ERROR] Failed to generate HEX file: {e}")
        return False


def main():
    """
    Main function.
    """
    # Get script directory
    script_dir = Path(__file__).parent
    # project root is the parent of the Script directory
    project_root = script_dir.parent
    # build directory is under the project root
    build_dir = project_root / 'build'

    # ---- Mode: emit only the WiFi flash image as a .bin (flash_header_t + .rps) ----
    # This is the exact content written to the WIFI flash region, used by both
    # `make flash-wifi` (program to 0x71A00000) and `make pkg-wifi` (wrap with the
    # 1KB OTA header for WEB verification; the header itself is NOT written to flash).
    if len(sys.argv) > 1 and sys.argv[1] == '--wifi-image':
        output_file = Path(sys.argv[2]) if len(sys.argv) > 2 else (build_dir / 'ne301_Wifi_flash.bin')
        if not output_file.is_absolute():
            output_file = project_root / output_file

        print("=" * 60)
        print("Building WiFi flash image (flash_header_t + .rps)")
        print("=" * 60)

        wifi_info = build_wifi_flash_image(project_root)
        if wifi_info is None:
            print("[ERROR] WiFi firmware (.rps) not found")
            return 1

        output_file.parent.mkdir(parents=True, exist_ok=True)
        with open(output_file, 'wb') as f:
            f.write(wifi_info['data'])

        print(f"\n[WIFI]")
        print(f"  File: {wifi_info['rps_name']}")
        print(f"  Base address: 0x{WIFI_FLASH_BASE_ADDR:08X}")
        print(f"  Original size: {wifi_info['fw_total_size']} bytes")
        print(f"  flash_header.valid_flags: 0x{WIFI_FLASH_VALID_FLAGS:08X}")
        print(f"  flash_header.fw_total_size: {wifi_info['fw_total_size']} bytes")
        print(f"  flash_header.fw_crc: 0x{wifi_info['fw_crc']:08X}")
        print(f"  Total image size (including flash header): {len(wifi_info['data'])} bytes")
        print(f"  OTA version: {wifi_info['version_4part']} ({wifi_info['version_full']})")
        print(f"\n[SUCCESS] WiFi flash image generated: {output_file}")
        print("=" * 60)
        return 0

    # ---- Mode: print WiFi metadata "<4part-version> <rps-stem>" for the Makefile ----
    if len(sys.argv) > 1 and sys.argv[1] == '--wifi-meta':
        wifi_info = build_wifi_flash_image(project_root)
        if wifi_info is None:
            print("0.0.0.0 UNKNOWN")
            return 1
        # Two whitespace-free tokens so the Makefile can pick them with $(word).
        print(f"{wifi_info['version_4part']} {wifi_info['version_full']}")
        return 0

    # Check if build directory exists
    if not build_dir.exists():
        print(f"[ERROR] build directory does not exist: {build_dir}")
        return 1

    pack_date = datetime.now().strftime('%Y%m%d')
    out_suffix = f'_{pack_date}'
    
    # Check whether WakeCore should be packed
    if len(sys.argv) > 1 and sys.argv[1] == '--wakecore':
        # Pack WakeCore into a separate HEX file
        output_file = build_dir / f'ne301_WakeCore{out_suffix}.hex'
        if len(sys.argv) > 2:
            output_file = Path(sys.argv[2])
            if not output_file.is_absolute():
                output_file = build_dir / output_file
        
        if pack_wakecore_to_hex(build_dir, output_file):
            return 0
        else:
            return 1
    else:
        # Get project root directory
        project_root = script_dir.parent
        
        # Output file for main firmware (without WiFi)
        output_file_main = build_dir / f'ne301_Main{out_suffix}.hex'
        
        # Output file for main firmware (with WiFi)
        output_file_main_wifi = build_dir / f'ne301_Main_WiFi{out_suffix}.hex'
        
        # If an output file argument is provided, pack only one file
        if len(sys.argv) > 1:
            output_file = Path(sys.argv[1])
            if not output_file.is_absolute():
                output_file = build_dir / output_file
            
            # Decide whether to include WiFi based on file name
            include_wifi = 'WiFi' in output_file.name or 'wifi' in output_file.name.lower()
            
            # Perform packing
            if pack_bin_to_hex(build_dir, output_file, include_wifi, project_root):
                return 0
            else:
                return 1
        else:
            # By default, pack two versions
            success = True
            
            # 1. Pack main firmware without WiFi
            print("\n" + "=" * 60)
            print("Packing main firmware (without WiFi)")
            print("=" * 60)
            if not pack_bin_to_hex(build_dir, output_file_main, False, project_root):
                success = False
            
            # 2. Pack main firmware with WiFi
            print("\n" + "=" * 60)
            print("Packing main firmware (with WiFi)")
            print("=" * 60)
            if not pack_bin_to_hex(build_dir, output_file_main_wifi, True, project_root):
                success = False
            
            if success:
                return 0
            else:
                return 1


if __name__ == '__main__':
    sys.exit(main())
