#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA Package Verification Tool
Verifies OTA package header structure and displays information
"""

import sys
import os
import struct
import zlib
import hashlib
from datetime import datetime
import io

# Fix encoding for Windows console
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

OTA_HEADER_SIZE = 1024
OTA_MAGIC_NUMBER = 0x4F544155  # "OTAU"

FW_TYPE_NAMES = {
    0x00: "Unknown",
    0x01: "FSBL",
    0x02: "Application",
    0x03: "Web Assets",
    0x04: "AI Model",
    0x05: "Configuration",
    0x06: "Patch",
    0x07: "Full Package",
    0x08: "WiFi (SiWG917)",
}

BUNDLE_HEADER_SIZE = 4096
BUNDLE_MAGIC = 0x4F544146  # "OTAF"
PART_NAMES = ["FSBL", "NVS", "OTA", "SWAP", "RESERVE1", "APP1", "APP2",
              "AI_1", "AI_2", "WEB", "WIFI", "LITTLEFS", "RESERVE2"]
IMMOVABLE_PARTS = {0}  # FSBL: the boot ROM address is silicon-fixed; may never move
DATA_BEARING_PARTS = {1, 2, 3, 4, 11, 12}  # NVS/OTA/SWAP/reserves/LITTLEFS: may move —
# the device allows it and the UI warns (settings/storage get reset on upgrade)


def verify_bundle(filename):
    """Verify a full OTA bundle (header + embedded per-firmware packages)."""
    try:
        with open(filename, 'rb') as f:
            data = f.read()

        print("=" * 70)
        print("OTA Bundle Verification")
        print("=" * 70)

        if len(data) < BUNDLE_HEADER_SIZE:
            print(f"[FAIL] File too small for a bundle header ({len(data)} bytes)")
            return False

        hdr = data[:BUNDLE_HEADER_SIZE]
        magic, header_version, header_size, header_crc32 = struct.unpack_from('<IHHI', hdr, 0)
        timestamp, entry_count, payload_total = struct.unpack_from('<III', hdr, 0x10)
        payload = data[BUNDLE_HEADER_SIZE:]
        ok = True

        if magic != BUNDLE_MAGIC:
            print(f"[FAIL] Invalid bundle magic: 0x{magic:08X} (expected 0x{BUNDLE_MAGIC:08X} OTAF)")
            return False
        print("[PASS] Magic: 0x{:08X} (OTAF)".format(magic))
        if header_size != BUNDLE_HEADER_SIZE or header_version != 0x0100:
            print(f"[FAIL] header size/version: {header_size}/0x{header_version:04X}")
            return False
        hdr_for_crc = bytearray(hdr)
        struct.pack_into('<I', hdr_for_crc, 8, 0)
        calc_crc = zlib.crc32(bytes(hdr_for_crc)) & 0xFFFFFFFF
        if calc_crc != header_crc32:
            print(f"[FAIL] Header CRC32: 0x{header_crc32:08X} (calculated 0x{calc_crc:08X})")
            return False
        print("[PASS] Header CRC32: 0x{:08X}".format(header_crc32))

        bundle_model, = struct.unpack_from('<I', hdr, 0x24)
        model_note = "unstamped (legacy)" if bundle_model == 0 else (
            "NE301" if bundle_model == 0x3010 else "unknown model")
        print(f"Device Model:         0x{bundle_model:04X} ({model_note})")

        # ---- partition table ----
        part_count, = struct.unpack_from('<H', hdr, 0x180)
        parts = []
        for i in range(min(part_count, 16)):
            pid, _r1, _r2, base, size = struct.unpack_from('<BBHII', hdr, 0x184 + i * 12)
            parts.append((pid, base, size))
        # The device burns at the addresses this table carries. FSBL is the
        # only immovable partition (silicon boot address); data-bearing
        # partitions may move — warn since their content resets on upgrade.
        try:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            from ota_bundle_packer import parse_mem_map, PART_IDS
            mm_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                   "Custom", "Common", "Inc", "mem_map.h")
            mm = parse_mem_map(mm_path)
            mm_by_id = {pid: (mm[f"{prefix}_BASE"], mm[f"{prefix}_SIZE"])
                        for prefix, pid in PART_IDS}
        except Exception as e:
            mm_by_id = None
            print(f"[WARN] mem_map.h not parsable ({e}); immovable-partition equality unchecked")
        seen = set()
        for pid, base, size in parts:
            if pid >= len(PART_NAMES) or pid in seen:
                print(f"[FAIL] bad/duplicate partition id {pid}")
                ok = False
                continue
            seen.add(pid)
            if mm_by_id is None or pid not in (IMMOVABLE_PARTS | DATA_BEARING_PARTS):
                continue
            if (base, size) != mm_by_id.get(pid):
                if pid in IMMOVABLE_PARTS:
                    print(f"[FAIL] {PART_NAMES[pid]} moved "
                          f"(0x{base:08X}+0x{size:X} != mem_map "
                          f"0x{mm_by_id[pid][0]:08X}+0x{mm_by_id[pid][1]:X}) — boot ROM address is fixed")
                    ok = False
                else:
                    print(f"[WARN] {PART_NAMES[pid]} moved vs this tree's mem_map "
                          f"(allowed; its data resets on upgrade)")
        for i in range(len(parts)):
            for j in range(i + 1, len(parts)):
                a0, a1 = parts[i][1], parts[i][1] + parts[i][2]
                b0, b1 = parts[j][1], parts[j][1] + parts[j][2]
                if a0 < b1 and b0 < a1:
                    print(f"[FAIL] partitions overlap: {PART_NAMES[parts[i][0]]} / {PART_NAMES[parts[j][0]]}")
                    ok = False
        print(f"[{'PASS' if ok else 'FAIL'}] Partition table: {part_count} entries")

        # ---- entries + embedded packages ----
        if not (0 < entry_count <= 10):
            print(f"[FAIL] entry_count {entry_count}")
            return False
        if len(payload) != payload_total:
            print(f"[FAIL] payload size {len(payload)} != header payload_total {payload_total}")
            ok = False

        prev_end = 0
        for i in range(entry_count):
            off = 0x40 + i * 32
            fw_type, eflags, _res = struct.unpack_from('<BBH', hdr, off)
            version = hdr[off + 4:off + 12]
            eoff, esize, ecrc, addr = struct.unpack_from('<IIII', hdr, off + 12)
            label = FW_TYPE_NAMES.get(fw_type, f"0x{fw_type:02X}")

            problems = []
            if eoff < prev_end:
                problems.append("overlap/unsorted")
            if eoff + esize > len(payload):
                problems.append("out of payload range")
            prev_end = eoff + esize

            blob = payload[eoff:eoff + esize]
            if len(blob) == esize and esize >= OTA_HEADER_SIZE:
                inner_magic, = struct.unpack_from('<I', blob, 0)
                inner_type, = struct.unpack_from('<B', blob, 0x0C)
                inner_total, = struct.unpack_from('<I', blob, 0x18)
                inner_crc, = struct.unpack_from('<I', blob, 0xB8)
                inner_ver = blob[0xA0:0xA8]
                if inner_magic != OTA_MAGIC_NUMBER:
                    problems.append("inner magic")
                if inner_type != fw_type:
                    problems.append("inner fw_type")
                if inner_total != esize:
                    problems.append("inner total size")
                if inner_crc != ecrc:
                    problems.append("entry crc != inner fw_crc32")
                if inner_ver != version:
                    problems.append("entry version != inner fw_ver")
                body_crc = zlib.crc32(blob[OTA_HEADER_SIZE:]) & 0xFFFFFFFF
                if body_crc != inner_crc:
                    problems.append("payload CRC32")
            else:
                problems.append("entry size")

            ver = f"{version[0]}.{version[1]}.{version[2]}.{version[3] | (version[4] << 8)}"
            status = "PASS" if not problems else "FAIL: " + ", ".join(problems)
            if problems:
                ok = False
            print(f"[{status}] entry {i}: {label:<14} v{ver:<14} {esize:>9} bytes"
                  f"{f'  direct->0x{addr:08X}' if addr else ''}")

        print("\n" + "=" * 70)
        print(("[SUCCESS] Bundle Verification PASSED" if ok else "[FAILURE] Bundle Verification FAILED"))
        print("=" * 70)
        return ok

    except FileNotFoundError:
        print(f"Error: File not found: {filename}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def verify_ota_package(filename):
    """Verify and display OTA package information"""
    try:
        with open(filename, 'rb') as f:
            header = f.read(OTA_HEADER_SIZE)
            firmware = f.read()
        
        if len(header) < OTA_HEADER_SIZE:
            print(f"Error: File too small (header: {len(header)} < {OTA_HEADER_SIZE} bytes)")
            return False
        
        # Parse header
        offset = 0
        
        # Basic Information
        magic = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        header_version = struct.unpack_from('<H', header, offset)[0]
        offset += 2
        header_size = struct.unpack_from('<H', header, offset)[0]
        offset += 2
        header_crc32 = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        fw_type = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        encrypt_type = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        compress_type = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        reserved1 = struct.unpack_from('<B', header, offset)[0]
        offset += 1
        timestamp = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        sequence = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        total_package_size = struct.unpack_from('<I', header, offset)[0]
        offset += 4
        offset += 33  # reserved2
        
        # Firmware Information (starting at 0x40)
        fw_name = header[0x40:0x60].rstrip(b'\x00').decode('utf-8', errors='ignore')
        fw_desc = header[0x60:0xA0].rstrip(b'\x00').decode('utf-8', errors='ignore')
        
        # Version info at 0xA0
        fw_ver = list(struct.unpack_from('<8B', header, 0xA0))
        min_ver = list(struct.unpack_from('<8B', header, 0xA8))
        
        fw_size = struct.unpack_from('<I', header, 0xB0)[0]
        fw_size_compressed = struct.unpack_from('<I', header, 0xB4)[0]
        fw_crc32_stored = struct.unpack_from('<I', header, 0xB8)[0]
        fw_hash_stored = header[0xBC:0xDC]
        
        # Verify magic number
        print("=" * 70)
        print("OTA Package Verification")
        print("=" * 70)
        
        if magic != OTA_MAGIC_NUMBER:
            print(f"[FAIL] Invalid magic number: 0x{magic:08X} (expected: 0x{OTA_MAGIC_NUMBER:08X})")
            return False
        print(f"[PASS] Magic number: 0x{magic:08X} (OTAU)")
        
        # Verify header size
        if header_size != OTA_HEADER_SIZE:
            print(f"[FAIL] Invalid header size: {header_size} (expected: {OTA_HEADER_SIZE})")
            return False
        print(f"[PASS] Header size: {header_size} bytes")
        
        # Verify header CRC32
        header_for_crc = bytearray(header)
        struct.pack_into('<I', header_for_crc, 8, 0)
        calculated_header_crc = zlib.crc32(bytes(header_for_crc)) & 0xFFFFFFFF
        if calculated_header_crc != header_crc32:
            print(f"[FAIL] Header CRC32 mismatch: 0x{header_crc32:08X} (calculated: 0x{calculated_header_crc:08X})")
            return False
        print(f"[PASS] Header CRC32: 0x{header_crc32:08X}")
        
        # Verify firmware CRC32
        calculated_fw_crc = zlib.crc32(firmware) & 0xFFFFFFFF
        if calculated_fw_crc != fw_crc32_stored:
            print(f"[FAIL] Firmware CRC32 mismatch: 0x{fw_crc32_stored:08X} (calculated: 0x{calculated_fw_crc:08X})")
            return False
        print(f"[PASS] Firmware CRC32: 0x{fw_crc32_stored:08X}")
        
        # Verify firmware SHA256
        calculated_fw_hash = hashlib.sha256(firmware).digest()
        if calculated_fw_hash != fw_hash_stored:
            print(f"[FAIL] Firmware SHA256 mismatch")
            print(f"  Stored:     {fw_hash_stored.hex()}")
            print(f"  Calculated: {calculated_fw_hash.hex()}")
            return False
        print(f"[PASS] Firmware SHA256: {calculated_fw_hash.hex()[:32]}...")
        
        # Display information
        print("\n" + "=" * 70)
        print("Package Information")
        print("=" * 70)
        print(f"Firmware Type:        {FW_TYPE_NAMES.get(fw_type, f'Unknown (0x{fw_type:02X})')}")
        print(f"Firmware Name:        {fw_name}")
        print(f"Description:          {fw_desc}")
        print(f"Version:              {fw_ver[0]}.{fw_ver[1]}.{fw_ver[2]}.{fw_ver[3]}")
        print(f"Min Compatible Ver:   {min_ver[0]}.{min_ver[1]}.{min_ver[2]}.{min_ver[3]}")
        print(f"Timestamp:            {datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"Sequence:             {sequence}")
        print(f"Firmware Size:        {fw_size:,} bytes")
        print(f"Total Package Size:   {total_package_size:,} bytes")
        device_model, = struct.unpack_from('<I', header, 0x1C)
        model_note = "unstamped (legacy)" if device_model == 0 else (
            "NE301" if device_model == 0x3010 else "unknown model")
        print(f"Device Model:         0x{device_model:04X} ({model_note})")
        print(f"Encryption:           {'None' if encrypt_type == 0 else f'Type {encrypt_type}'}")
        print(f"Compression:          {'None' if compress_type == 0 else f'Type {compress_type}'}")
        
        print("\n" + "=" * 70)
        print("[SUCCESS] OTA Package Verification PASSED")
        print("=" * 70)
        return True
        
    except FileNotFoundError:
        print(f"Error: File not found: {filename}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python verify_ota_package.py <ota_package_or_bundle.bin>")
        print("       (bundle files are auto-detected by the OTAF magic)")
        return 1

    filename = sys.argv[1]
    try:
        with open(filename, 'rb') as f:
            file_magic = struct.unpack('<I', f.read(4))[0]
    except (FileNotFoundError, struct.error):
        print(f"Error: cannot read {filename}")
        return 1

    if file_magic == BUNDLE_MAGIC:
        success = verify_bundle(filename)
    else:
        success = verify_ota_package(filename)
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
