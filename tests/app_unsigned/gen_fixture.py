import hashlib
import struct
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MAGIC = bytes.fromhex("4e45415050050000")
APP_ID = b"line-crossing"
UI_PAYLOAD = b"hello"


def put16(buf, offset, value):
    struct.pack_into("<H", buf, offset, value)


def put32(buf, offset, value):
    struct.pack_into("<I", buf, offset, value)


def native_package(version=(1, 0, 0), image_tag=0):
    native_payload = bytes((i * 17 + 3 + image_tag) & 255 for i in range(64))
    native = bytearray(32)
    native[0:4] = b"NEA1"
    put16(native, 4, 32)
    put16(native, 6, 1)
    put32(native, 8, 0x00040000)
    put32(native, 12, 0x93E00000)
    put32(native, 16, len(native_payload))
    put32(native, 20, 0)
    put32(native, 24, 0)
    put32(native, 28, zlib.crc32(native_payload))
    image = bytes(native) + native_payload
    manifest = bytearray(208)
    manifest[0:4] = b"NMF5"
    manifest[20:20 + len(APP_ID)] = APP_ID
    for index, part in enumerate(version):
        put16(manifest, 52 + index * 2, part)
    put32(manifest, 60, 0x00003010)
    put32(manifest, 64, 64)
    put32(manifest, 68, 0x00040000)
    put32(manifest, 72, 0x00000020)
    put32(manifest, 76, len(native_payload))
    put32(manifest, 80, len(image))
    put32(manifest, 84, len(native_payload))
    put32(manifest, 88, 0)
    put32(manifest, 92, 0x93E00000)
    manifest[96:128] = hashlib.sha256(image).digest()
    put32(manifest, 160, 3)
    return MAGIC + struct.pack("<II", len(manifest), len(image)) + bytes(manifest) + image


def ui_bundle(native, native_version=(1, 0, 0), version=(1, 2, 3), payload=UI_PAYLOAD):
    native_digest = hashlib.sha256(native).digest()
    path = b"index.html"
    record = struct.pack("<HI", len(path), len(payload)) + hashlib.sha256(payload).digest() + path
    manifest = bytearray(136)
    manifest[16:16 + len(APP_ID)] = APP_ID
    manifest[80:112] = native_digest
    for index, part in enumerate(version):
        put16(manifest, 112 + index * 2, part)
    put16(manifest, 118, 1)
    put32(manifest, 120, 1)
    for index, part in enumerate(native_version):
        put16(manifest, 124 + index * 2, part)
    manifest.extend(record)
    return b"NEUI\x02\x00\x00\x00" + struct.pack("<II", len(manifest), len(payload)) + bytes(manifest) + payload


def package(native_version=(1, 0, 0), image_tag=0, ui_version=(1, 2, 3), ui_payload=UI_PAYLOAD):
    native = native_package(native_version, image_tag)
    ui = ui_bundle(native, native_version, ui_version, ui_payload)
    return MAGIC + struct.pack("<II", len(native), len(ui)) + native + ui


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    native = native_package()
    ui = ui_bundle(native)
    (ROOT / "native-v5.neapp").write_bytes(native)
    (ROOT / "ui-v2.neui").write_bytes(ui)
    (ROOT / "single-file-v5.neapp").write_bytes(MAGIC + struct.pack("<II", len(native), len(ui)) + native + ui)
    (ROOT / "single-file-v5-upgrade.neapp").write_bytes(
        package((1, 1, 0), 1, (1, 2, 4), b"hello-upgrade"))
    (ROOT / "single-file-v5-ui-version-1.0.0.neapp").write_bytes(
        package(ui_version=(1, 0, 0)))
    (ROOT / "single-file-v5-ui-version-1.1.0.neapp").write_bytes(
        package(ui_version=(1, 1, 0)))
    (ROOT / "single-file-v5-native-major-0.neapp").write_bytes(
        package(native_version=(0, 1, 1)))


if __name__ == "__main__":
    main()
