#!/usr/bin/env python3
"""Create integrity-correct and deliberately invalid NEAPP05 test pairs."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parent
FIXTURES = ROOT / "fixtures"
FIXED_UI_MANIFEST = 136
PRODUCT_PATH = "product-version.json"


def read_package(path: Path) -> tuple[bytes, bytearray, bytearray]:
    data = path.read_bytes()
    if len(data) < 16 or data[:8] != b"NEAPP\x05\x00\x00":
        raise ValueError(f"not an unsigned NEAPP05 package: {path}")
    native_len, ui_len = struct.unpack_from("<II", data, 8)
    if 16 + native_len + ui_len != len(data):
        raise ValueError(f"invalid outer lengths: {path}")
    return data[:8], bytearray(data[16 : 16 + native_len]), bytearray(data[16 + native_len :])


def parse_ui(ui: bytes) -> tuple[bytes, bytearray, list[tuple[str, bytes]]]:
    if len(ui) < 16 or ui[:8] != b"NEUI\x02\x00\x00\x00":
        raise ValueError("not an unsigned NEUI02 bundle")
    manifest_len, payload_len = struct.unpack_from("<II", ui, 8)
    if 16 + manifest_len + payload_len != len(ui) or manifest_len < FIXED_UI_MANIFEST:
        raise ValueError("invalid UI lengths")
    manifest = bytearray(ui[16 : 16 + manifest_len])
    file_count = struct.unpack_from("<H", manifest, 118)[0]
    offset = FIXED_UI_MANIFEST
    payload_offset = 16 + manifest_len
    files: list[tuple[str, bytes]] = []
    for _ in range(file_count):
        path_len = struct.unpack_from("<H", manifest, offset)[0]
        file_len = struct.unpack_from("<I", manifest, offset + 2)[0]
        path = manifest[offset + 38 : offset + 38 + path_len].decode("ascii")
        files.append((path, ui[payload_offset : payload_offset + file_len]))
        offset += 38 + path_len
        payload_offset += file_len
    if offset != manifest_len or payload_offset != len(ui):
        raise ValueError("invalid UI file table")
    return ui[:8], manifest[:FIXED_UI_MANIFEST], files


def build_ui(
    magic: bytes,
    fixed: bytearray,
    files: list[tuple[str, bytes]],
    native: bytes,
    ui_version: tuple[int, int, int] | None = None,
) -> bytes:
    files = sorted(files, key=lambda item: item[0].encode("ascii"))
    manifest = bytearray(fixed)
    manifest[80:112] = hashlib.sha256(native).digest()
    if ui_version is not None:
        struct.pack_into("<HHH", manifest, 112, *ui_version)
    native_version = struct.unpack_from("<HHH", native, 16 + 52)
    struct.pack_into("<HHH", manifest, 124, *native_version)
    struct.pack_into("<H", manifest, 118, len(files))
    payload = bytearray()
    for path, content in files:
        path_bytes = path.encode("ascii")
        payload.extend(struct.pack("<HI", len(path_bytes), len(content)))
        payload.extend(hashlib.sha256(content).digest())
        payload.extend(path_bytes)
    manifest.extend(payload)
    file_payload = b"".join(content for _, content in files)
    return magic + struct.pack("<II", len(manifest), len(file_payload)) + manifest + file_payload


def build_package(outer_magic: bytes, native: bytes, ui: bytes) -> bytes:
    return outer_magic + struct.pack("<II", len(native), len(ui)) + native + ui


def rebind(
    src: Path,
    dst: Path,
    *,
    product: bytes | None = None,
    add_product: bytes | None = None,
    tamper_product: bool = False,
    change_index: bool = False,
    native_version: tuple[int, int, int] | None = None,
    ui_version: tuple[int, int, int] | None = None,
    required_host_caps: int | None = None,
) -> None:
    outer_magic, native, ui = read_package(src)
    ui_magic, fixed, files = parse_ui(ui)
    if native_version is not None:
        struct.pack_into("<HHH", native, 16 + 52, *native_version)
    if required_host_caps is not None:
        struct.pack_into("<I", native, 16 + 72, required_host_caps)
    if product is not None:
        files = [(p, data) for p, data in files if p != PRODUCT_PATH]
        files.append((PRODUCT_PATH, product))
    if add_product is not None and not any(p == PRODUCT_PATH for p, _ in files):
        files.append((PRODUCT_PATH, add_product))
    if change_index:
        files = [
            (p, (b"!" + data[1:]) if p == "index.html" and data else data)
            for p, data in files
        ]
    result_ui = build_ui(ui_magic, fixed, files, native, ui_version)
    if tamper_product:
        marker = b'{"product_version":"1.0.1"}'
        pos = result_ui.find(marker)
        if pos < 0:
            raise ValueError("canonical product carrier was not found")
        result_ui = result_ui[:pos] + b'{"product_version":"1.0.0"}' + result_ui[pos + len(marker) :]
    dst.write_bytes(build_package(outer_magic, native, result_ui))


def remove_product(src: Path, dst: Path) -> None:
    outer_magic, native, ui = read_package(src)
    ui_magic, fixed, files = parse_ui(ui)
    files = [(p, data) for p, data in files if p != PRODUCT_PATH]
    dst.write_bytes(build_package(outer_magic, native, build_ui(ui_magic, fixed, files, native)))


def copy_package(src: Path, dst: Path, *, tamper_product: bool = False) -> None:
    outer_magic, native, ui = read_package(src)
    if tamper_product:
        ui_magic, fixed, files = parse_ui(ui)
        result_ui = build_ui(ui_magic, fixed, files, native)
        marker = b'{"product_version":"1.0.1"}'
        pos = result_ui.find(marker)
        if pos < 0:
            raise ValueError("canonical product carrier was not found")
        result_ui = result_ui[:pos] + b'{"product_version":"1.0.0"}' + result_ui[pos + len(marker) :]
        dst.write_bytes(build_package(outer_magic, native, result_ui))
        return
    dst.write_bytes(src.read_bytes())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    product = FIXTURES / "line-crossing-1.0.1.neapp"
    legacy = FIXTURES / "line-crossing-4.2.12.neapp"
    rebind(product, out / "unsupported-capability.neapp", required_host_caps=0xFFF)
    rebind(product, out / "bad-encoding.neapp", product=b'{"product_version":"01.0.1"}')
    remove_product(product, out / "missing-carrier.neapp")
    copy_package(product, out / "tampered-carrier.neapp", tamper_product=True)
    rebind(product, out / "same-version-different-content.neapp", change_index=True)
    rebind(product, out / "lower-product.neapp", product=b'{"product_version":"1.0.0"}')
    rebind(
        product,
        out / "lower-product-new-components.neapp",
        product=b'{"product_version":"1.0.0"}',
        native_version=(4, 2, 15),
        ui_version=(1, 2, 16),
    )
    rebind(legacy, out / "legacy-with-stray-carrier.neapp", add_product=b'{"product_version":"1.0.1"}')
    rebind(legacy, out / "legacy-with-malformed-stray.neapp", add_product=b"not-json")
    copy_package(product, out / "valid-product.neapp")
    copy_package(legacy, out / "legacy.neapp")
    for path in sorted(out.glob("*.neapp")):
        print(f"{path.name}: {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
