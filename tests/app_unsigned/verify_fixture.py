import hashlib
import struct
import sys
import zlib
from pathlib import Path


MAGIC = bytes.fromhex("4e45415050050000")
NATIVE_MAGIC = bytes.fromhex("4e45415050050000")
UI_MAGIC = b"NEUI\x02\x00\x00\x00"


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def fail(message):
    raise ValueError(message)


def parse_native(data):
    if len(data) < 16 + 208 + 32 or data[:8] != NATIVE_MAGIC:
        fail("native member header")
    manifest_len = u32(data, 8)
    native_len = u32(data, 12)
    if manifest_len != 208 or len(data) != 16 + manifest_len + native_len:
        fail("native member bounds")
    manifest = data[16:16 + manifest_len]
    image = data[16 + manifest_len:]
    header = image[:32]
    payload = image[32:]
    if manifest[:4] != b"NMF5" or any(manifest[4:20]) or any(manifest[128:160]) or any(manifest[192:208]):
        fail("native manifest reserved fields")
    if u16(manifest, 52) == 0:
        fail("Native version")
    app_id = manifest[20:52].split(b"\x00", 1)[0]
    if not app_id or manifest[20 + len(app_id):52] != bytes(32 - len(app_id)):
        fail("native App ID slot")
    if any(byte not in b"abcdefghijklmnopqrstuvwxyz0123456789-" for byte in app_id):
        fail("native App ID characters")
    if hashlib.sha256(image).digest() != manifest[96:128]:
        fail("native digest")
    if len(image) != u32(manifest, 80) or len(image) != 32 + u32(manifest, 84):
        fail("native member length")
    if (header[:4] != b"NEA1" or u16(header, 4) != 32 or u16(header, 6) != 1 or
            u32(header, 8) != 0x00040000 or u32(header, 12) != u32(manifest, 92) or
            u32(header, 16) != len(payload) or u32(header, 20) != u32(manifest, 88) or
            u32(header, 24) != 0 or zlib.crc32(payload) != u32(header, 28)):
        fail("native image header")
    if (u32(manifest, 60) != 0x00003010 or u32(manifest, 64) != 64 or
            u32(manifest, 68) != 0x00040000 or u32(manifest, 160) != 3):
        fail("native target or Host ABI policy")
    return app_id, manifest, hashlib.sha256(data).digest()


def parse_ui(data, app_id, native_digest, native_manifest):
    if len(data) < 16 + 136 or data[:8] != UI_MAGIC:
        fail("UI member header")
    manifest_len = u32(data, 8)
    payload_len = u32(data, 12)
    if manifest_len < 175 or manifest_len > 16384 or payload_len < 1 or payload_len > 2097152:
        fail("UI member limits")
    if len(data) != 16 + manifest_len + payload_len:
        fail("UI member bounds")
    manifest = data[16:16 + manifest_len]
    payload = data[16 + manifest_len:]
    if any(manifest[0:16]) or any(manifest[48:80]) or any(manifest[130:136]):
        fail("UI trust fields")
    if manifest[16:48].split(b"\x00", 1)[0] != app_id:
        fail("UI App ID")
    if manifest[80:112] != native_digest:
        fail("UI Native binding")
    if tuple(u16(manifest, 124 + 2 * i) for i in range(3)) != tuple(u16(native_manifest, 52 + 2 * i) for i in range(3)):
        fail("UI Native version")
    count = u16(manifest, 118)
    if count < 1 or count > 64 or u32(manifest, 120) != 1:
        fail("UI file count or ABI")
    offset = 136
    payload_offset = 0
    total_payload = 0
    last_path = None
    index_count = 0
    seen_paths = set()
    for _ in range(count):
        if offset + 38 > manifest_len:
            fail("UI record header")
        path_len = u16(manifest, offset)
        file_len = u32(manifest, offset + 2)
        digest = manifest[offset + 6:offset + 38]
        path = manifest[offset + 38:offset + 38 + path_len]
        if (not path_len or path_len > 160 or offset + 38 + path_len > manifest_len or
                not file_len or file_len > 1048576):
            fail("UI record bounds")
        if (any(byte < 0x21 or byte > 0x7E for byte in path) or path.startswith(b"/") or
                path.endswith(b"/") or b"\\" in path or
                any(part in (b"", b".", b"..") or
                    part[0] not in b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" or
                    any(byte not in b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
                        for byte in part) for part in path.split(b"/"))):
            fail("UI path")
        if last_path is not None and path <= last_path:
            fail("UI path order")
        if path.lower() in seen_paths:
            fail("UI duplicate path")
        seen_paths.add(path.lower())
        if path == b"index.html":
            index_count += 1
        file_data = payload[payload_offset:payload_offset + file_len]
        if len(file_data) != file_len or hashlib.sha256(file_data).digest() != digest:
            fail("UI file digest")
        payload_offset += file_len
        total_payload += file_len
        offset += 38 + path_len
        last_path = path
    if (offset != manifest_len or payload_offset != payload_len or
            total_payload != payload_len or index_count != 1):
        fail("UI member totals")
    return tuple(u16(manifest, 112 + 2 * i) for i in range(3))


def verify(path):
    data = Path(path).read_bytes()
    if len(data) < 16 or data[:8] != MAGIC:
        fail("container magic")
    native_len = u32(data, 8)
    ui_len = u32(data, 12)
    end_native = 16 + native_len
    if native_len < 256 or ui_len < 190 or end_native > len(data) or end_native + ui_len != len(data):
        fail("container member bounds")
    app_id, native_manifest, native_digest = parse_native(data[16:end_native])
    ui_version = parse_ui(data[end_native:], app_id, native_digest, native_manifest)
    if not all(ui_version):
        fail("UI version")
    print("PASS NEAPP05", len(data), hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_fixture.py PACKAGE")
    verify(sys.argv[1])
