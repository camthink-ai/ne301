import hashlib
import struct
import zlib
from pathlib import Path

import verify_fixture


ROOT = Path(__file__).resolve().parent


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def assert_other_fields_valid(data):
    assert data[:8] == verify_fixture.MAGIC
    native_len, ui_len = struct.unpack_from("<II", data, 8)
    assert len(data) == 16 + native_len + ui_len

    native = data[16:16 + native_len]
    native_manifest = native[16:224]
    native_image = native[224:]
    assert native[:8] == verify_fixture.NATIVE_MAGIC
    assert u32(native, 8) == 208 and len(native) == 16 + 208 + u32(native, 12)
    assert hashlib.sha256(native_image).digest() == native_manifest[96:128]
    assert zlib.crc32(native_image[32:]) == u32(native_image, 28)

    ui = data[16 + native_len:]
    ui_manifest_len, ui_payload_len = struct.unpack_from("<II", ui, 8)
    ui_manifest = ui[16:16 + ui_manifest_len]
    ui_payload = ui[16 + ui_manifest_len:]
    assert ui[:8] == verify_fixture.UI_MAGIC
    assert len(ui) == 16 + ui_manifest_len + ui_payload_len == ui_len
    assert ui_manifest[80:112] == hashlib.sha256(native).digest()
    assert ui_manifest[16:48] == native_manifest[20:52]
    assert ui_manifest[124:130] == native_manifest[52:58]

    path_len = u16(ui_manifest, 136)
    file_len = u32(ui_manifest, 138)
    digest = ui_manifest[142:174]
    path = ui_manifest[174:174 + path_len]
    assert path == b"index.html" and file_len == len(ui_payload) == ui_payload_len
    assert hashlib.sha256(ui_payload).digest() == digest


def expect_version_rejection(filename, kind, expected_version, expected_error):
    path = ROOT / filename
    data = path.read_bytes()
    assert_other_fields_valid(data)
    native_len = u32(data, 8)
    native_manifest = data[32:240]
    ui_start = 16 + native_len
    ui_manifest_len = u32(data, ui_start + 8)
    ui_manifest = data[ui_start + 16:ui_start + 16 + ui_manifest_len]
    if kind == "native":
        actual = tuple(u16(native_manifest, 52 + 2 * index) for index in range(3))
    else:
        actual = tuple(u16(ui_manifest, 112 + 2 * index) for index in range(3))
    assert actual == expected_version

    try:
        verify_fixture.verify(path)
    except ValueError as error:
        assert str(error) == expected_error, (filename, str(error))
    else:
        raise AssertionError(f"{filename}: expected {expected_error} rejection")
    print(f"PASS version rejection {filename}: {expected_error}")


def main():
    expect_version_rejection("single-file-v5-ui-version-1.0.0.neapp", "ui",
                             (1, 0, 0), "UI version")
    expect_version_rejection("single-file-v5-ui-version-1.1.0.neapp", "ui",
                             (1, 1, 0), "UI version")
    expect_version_rejection("single-file-v5-native-major-0.neapp", "native",
                             (0, 1, 1), "Native version")
    print("PASS version boundary negative cases: 3")


if __name__ == "__main__":
    main()
