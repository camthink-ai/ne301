#!/usr/bin/env python3
"""
OTA Bundle Packer — builds the NE301 one-click full upgrade package.

Bundle layout (see Custom/Core/System/ota_bundle.h and
docs/NE301_OTA_Bundle_Design.md):

    [ 4096-byte bundle header ]
    [ model _pkg.bin  ]   burn order fixed: Model -> WEB -> WiFi -> APP -> FSBL
    [ web    _pkg.bin ]
    [ wifi   _pkg.bin ]
    [ app    _pkg.bin ]
    [ fsbl   _pkg.bin ]

Each component is the unmodified output of `make pkg` (1KB ota_header_t +
payload); this script only parses their headers to fill the bundle entry
table, so the device-side validation chain is reused verbatim.

The partition table is parsed from mem_map.h and embedded in the header. The
bundle always burns FORCED at the addresses of the table it carries (no AB
slots, no layout-version signalling — the device erases OTA info at
bundle/begin and every firmware is re-burned); the device validates the table
itself (fixed partitions must not move) and resolves the final addresses
from it.

Usage:
    python ota_bundle_packer.py <build_dir> -o out.bin [--mem-map path]
                                [--exclude fsbl,wifi] [--board-flash 128]
"""

import argparse
import os
import re
import struct
import sys
import time
import zlib

BUNDLE_HEADER_SIZE = 4096
BUNDLE_MAGIC = 0x4F544146          # "OTAF"
BUNDLE_HEADER_VER = 0x0100
BUNDLE_TYPE_FULL = 0x01
# Device model id stamped at 0x24 — the device rejects a bundle built for
# another model at precheck. Default for standalone runs; `make pkg-bundle`
# always passes -m from the root Makefile's DEVICE_MODEL (single source).
DEVICE_MODEL = 0x3010              # NE301

OTA_MAGIC_NUMBER = 0x4F544155      # "OTAU"
OTA_HEADER_SIZE = 1024

# fw_type -> (label, partition for addr_override, mem_map prefix, pkg glob)
# Burn order = list order (least -> most critical, see design doc 5.5).
FW_SPECS = [
    (0x04, "model", "AI_1",   "ne301_Model_v*_pkg.bin"),
    (0x03, "web",   "WEB",    "ne301_Web_v*_pkg.bin"),
    (0x08, "wifi",  "WIFI_FW", "ne301_Wifi_flash_v*_pkg.bin"),
    (0x02, "app",   "APP1",   "ne301_App_signed_v*_pkg.bin"),
    (0x01, "fsbl",  "FSBL",   "ne301_FSBL_signed_v*_pkg.bin"),
]

# mem_map macro prefix -> bundle part id
PART_IDS = [
    ("FSBL", 0), ("NVS", 1), ("OTA", 2), ("SWAP", 3), ("RESERVE1", 4),
    ("APP1", 5), ("APP2", 6), ("AI_1", 7), ("AI_2", 8), ("WEB", 9),
    ("WIFI_FW", 10), ("LITTLEFS", 11), ("RESERVE2", 12),
]


def _resolve_macro(name, raw_defs, cache, resolving=None):
    """Evaluate a #define body like '(0x70080000U - 0x70000000U)' to an int.

    Bodies in mem_map.h are arithmetic expressions over hex/dec literals and
    other macros; identifiers are expanded recursively. The whitelist regex
    before eval() only permits numbers and + - * / ( ) so nothing else in the
    header can execute.
    """
    resolving = resolving or set()
    if name in cache:
        return cache[name]
    if name not in raw_defs:
        raise RuntimeError(f"unknown macro: {name}")
    if name in resolving:
        raise RuntimeError(f"circular macro definition: {name}")
    resolving = set(resolving)
    resolving.add(name)

    expr = re.sub(r"(?<=\d)[uUlL]+", "", raw_defs[name])  # strip 0x..U suffixes

    # token-wise expansion: hex/dec literals pass through untouched, only
    # real identifiers are substituted (a naive [A-Za-z_]\w* sub would also
    # match the "x..." tail inside a hex literal like 0x70000000)
    token_re = re.compile(r"0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*")
    literal_re = re.compile(r"0[xX][0-9a-fA-F]+|\d+")

    def expand(match):
        token = match.group(0)
        if literal_re.fullmatch(token):
            return token
        if token in raw_defs:
            return str(_resolve_macro(token, raw_defs, cache, resolving))
        raise RuntimeError(f"unknown identifier '{token}' in #define {name}")

    expanded = token_re.sub(expand, expr)
    if not re.fullmatch(r"[0-9a-fA-FxX+\-*/() ]+", expanded):
        raise RuntimeError(f"unsupported expression in #define {name}: {raw_defs[name]}")
    value = eval(expanded, {"__builtins__": {}}, {})  # noqa: S307 (whitelisted)
    cache[name] = value
    return value


def parse_mem_map(path, board_flash=128):
    """Tiny conditional-#if-aware #define scanner for mem_map.h.

    Only the BOARD_FLASH_SIZE condition matters for the partition table; the
    PSRAM branch is layout-irrelevant and both sides define the same
    partition macros, so a single boolean stack keyed on the flash condition
    is enough. #define bodies are arithmetic expressions (see _resolve_macro).
    """
    raw_defs = {}
    active = [True]
    in_flash_cond = [False]
    in_block_comment = False

    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            # skip block comments (mem_map.h's ASCII layout table contains #if lines)
            if in_block_comment:
                if "*/" in s:
                    s = s.split("*/", 1)[1].strip()
                    in_block_comment = False
                else:
                    continue
            if "/*" in s:
                in_block_comment = "*/" not in s
                s = s.split("/*", 1)[0].strip()
                if not s:
                    continue

            if s.startswith("#if"):
                cond = "BOARD_FLASH_SIZE" in s and "== 128" in s
                if s.startswith("#ifndef") or s.startswith("#ifdef"):
                    # include guards etc. — always active
                    active.append(active[-1])
                else:
                    active.append(active[-1] and (cond == (board_flash == 128)))
                in_flash_cond.append(cond)
            elif s.startswith("#else"):
                if in_flash_cond[-1]:
                    active[-1] = active[-2] and (board_flash != 128)
            elif s.startswith("#endif"):
                active.pop()
                in_flash_cond.pop()
            elif active[-1] and s.startswith("#define"):
                m = re.match(r"#define\s+(\w+)\s+(.+?)(?://.*)?$", s)
                if m:
                    raw_defs[m.group(1)] = m.group(2).strip()

    cache = {}
    defines = {}
    for prefix, _ in PART_IDS:
        for suffix in ("_BASE", "_SIZE"):
            key = f"{prefix}{suffix}"
            try:
                defines[key] = _resolve_macro(key, raw_defs, cache)
            except RuntimeError:
                pass  # reported collectively below

    missing = [name for name, _ in PART_IDS
               if f"{name}_BASE" not in defines or f"{name}_SIZE" not in defines]
    if missing:
        raise RuntimeError(f"mem_map parse failed, missing: {missing}")
    return defines


def parse_pkg_header(path):
    """Extract (fw_type, fw_ver[8], fw_crc32, total_size) from a _pkg.bin."""
    with open(path, "rb") as f:
        head = f.read(OTA_HEADER_SIZE)
    if len(head) < OTA_HEADER_SIZE:
        raise RuntimeError(f"{path}: shorter than an OTA header")
    magic, = struct.unpack_from("<I", head, 0x00)
    if magic != OTA_MAGIC_NUMBER:
        raise RuntimeError(f"{path}: bad OTA magic 0x{magic:08X}")
    fw_type, = struct.unpack_from("<B", head, 0x0C)
    fw_ver = head[0xA0:0xA8]
    fw_crc32, = struct.unpack_from("<I", head, 0xB8)
    total_size, = struct.unpack_from("<I", head, 0x18)
    file_size = os.path.getsize(path)
    if total_size != file_size:
        raise RuntimeError(f"{path}: header total_package_size={total_size} != file size {file_size}")
    return fw_type, fw_ver, fw_crc32, file_size


def find_pkg(build_dir, glob):
    import glob as globmod
    matches = [p for p in globmod.glob(os.path.join(build_dir, glob))
               if "SensorExt" not in os.path.basename(p)]
    if len(matches) > 1:
        raise RuntimeError(
            f"multiple pkg files match {glob} in {build_dir}:\n"
            f"    " + "\n    ".join(matches) +
            "\n  stale packages from earlier builds confuse the bundle. "
            "Re-run `make pkg` (it prunes old versions) or delete the outdated files.")
    return matches[0] if matches else None


def build_bundle(build_dir, out_path, mem_map_path, exclude, board_flash, model_id=0x3010):
    defines = parse_mem_map(mem_map_path, board_flash)

    # ---- collect entries in burn order ----
    entries = []      # (fw_type_label, fw_type, fw_ver, file, crc, size)
    payload = bytearray()
    for fw_type, label, part_prefix, glob in FW_SPECS:
        if label in exclude:
            print(f"  - {label}: excluded by --exclude")
            continue
        path = find_pkg(build_dir, glob)
        if not path:
            raise RuntimeError(f"missing pkg for '{label}' (expected {glob} in {build_dir}); "
                               f"run `make pkg` first")
        pkg_type, fw_ver, fw_crc32, file_size = parse_pkg_header(path)
        if pkg_type != fw_type:
            raise RuntimeError(f"{path}: inner fw_type 0x{pkg_type:02X} != expected 0x{fw_type:02X} for '{label}'")
        with open(path, "rb") as f:
            blob = f.read()
        entries.append({
            "label": label, "fw_type": fw_type, "fw_ver": fw_ver,
            "offset": len(payload), "size": len(blob),
            "crc": fw_crc32, "part_prefix": part_prefix,
        })
        payload += blob

    if not entries:
        raise RuntimeError("no firmware entries selected")

    # ---- partition table (authoritative: the device burns at these addresses) ----
    parts = []
    for prefix, part_id in PART_IDS:
        parts.append((part_id, defines[f"{prefix}_BASE"], defines[f"{prefix}_SIZE"]))

    hdr = bytearray(BUNDLE_HEADER_SIZE)

    struct.pack_into("<I", hdr, 0x00, BUNDLE_MAGIC)
    struct.pack_into("<H", hdr, 0x04, BUNDLE_HEADER_VER)
    struct.pack_into("<H", hdr, 0x06, BUNDLE_HEADER_SIZE)
    struct.pack_into("<B", hdr, 0x0C, BUNDLE_TYPE_FULL)
    struct.pack_into("<I", hdr, 0x10, int(time.time()))
    struct.pack_into("<I", hdr, 0x14, len(entries))
    struct.pack_into("<I", hdr, 0x18, len(payload))
    struct.pack_into("<I", hdr, 0x24, model_id)

    for i, e in enumerate(entries):
        off = 0x40 + i * 32
        # informational: the device re-resolves the address from the partition
        # table at precheck; this is what the verifier prints for humans
        addr_override = defines[f"{e['part_prefix']}_BASE"]
        struct.pack_into("<B", hdr, off + 0, e["fw_type"])
        struct.pack_into("<B", hdr, off + 1, 0)
        struct.pack_into("<H", hdr, off + 2, 0)
        hdr[off + 4:off + 12] = e["fw_ver"]
        struct.pack_into("<I", hdr, off + 12, e["offset"])
        struct.pack_into("<I", hdr, off + 16, e["size"])
        struct.pack_into("<I", hdr, off + 20, e["crc"])
        struct.pack_into("<I", hdr, off + 24, addr_override)

    struct.pack_into("<H", hdr, 0x180, len(parts))
    for i, (part_id, base, size) in enumerate(parts):
        off = 0x184 + i * 12
        struct.pack_into("<B", hdr, off + 0, part_id)
        struct.pack_into("<I", hdr, off + 4, base)
        struct.pack_into("<I", hdr, off + 8, size)

    hdr_for_crc = bytearray(hdr)
    struct.pack_into("<I", hdr_for_crc, 0x08, 0)
    struct.pack_into("<I", hdr, 0x08, zlib.crc32(bytes(hdr_for_crc)) & 0xFFFFFFFF)

    with open(out_path, "wb") as f:
        f.write(bytes(hdr))
        f.write(bytes(payload))

    print(f"Bundle created: {out_path} ({BUNDLE_HEADER_SIZE + len(payload)} bytes)")
    print(f"  mode: FORCED DIRECT (no AB slots, no layout versions)")
    for i, e in enumerate(entries):
        v = f"{e['fw_ver'][0]}.{e['fw_ver'][1]}.{e['fw_ver'][2]}.{e['fw_ver'][3] | (e['fw_ver'][4] << 8)}"
        addr_override, = struct.unpack_from("<I", hdr, 0x40 + i * 32 + 24)
        print(f"  - {e['label']:<6} v{v}  {e['size']:>9} bytes  offset {e['offset']}  -> 0x{addr_override:08X}")
    return True


def main():
    parser = argparse.ArgumentParser(description="NE301 OTA bundle packer")
    parser.add_argument("build_dir", help="directory containing *_pkg.bin from `make pkg`")
    parser.add_argument("-o", "--output", required=True, help="output bundle file")
    parser.add_argument("--mem-map", default=os.path.join(os.path.dirname(__file__), "..", "Custom", "Common", "Inc", "mem_map.h"),
                        help="path to mem_map.h (partition table source of truth)")
    parser.add_argument("--exclude", default="", help="comma-separated firmware labels to exclude (fsbl,app,web,model,wifi)")
    parser.add_argument("--board-flash", type=int, default=128, choices=[64, 128],
                        help="BOARD_FLASH_SIZE variant used to parse mem_map.h (default 128, matches Makefile COMMON_DEFS)")
    parser.add_argument("-m", "--model", type=lambda s: int(s, 0), default=DEVICE_MODEL,
                        help="device model id stamped at 0x24 (hex; 0x3010 = NE301, 0 = unstamped)")
    args = parser.parse_args()

    exclude = {s.strip() for s in args.exclude.split(",") if s.strip()}

    try:
        ok = build_bundle(args.build_dir, args.output, os.path.abspath(args.mem_map),
                          exclude, args.board_flash, args.model)
    except RuntimeError as e:
        print(f"Error: {e}")
        return 1
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
