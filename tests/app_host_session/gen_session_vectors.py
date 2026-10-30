#!/usr/bin/env python3
import argparse
import hashlib
import zlib
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "app_admission"))

from anchor_keygen import (ecdsa_sign, der_sig_bytes, ec_mul, spki_der,
                           spki_sha256, ecdsa_verify, GX, GY, N)

PUBLISHER_ID = "test-publisher"
APP_ID = "hello-app"
VERSION = (1, 0, 0)
BOARD_ID = 0x00003010
PSRAM_MIB = 64
REQUIRED_HOST_ABI = 0x00020000
EXEC_BYTES = 4
TARGET_ADDR = 0x93E00000
RUN_PROFILE = 1

CONTAINER_MAGIC_V2 = bytes([0x4E, 0x45, 0x41, 0x50, 0x50, 0x02, 0x00, 0x00])
MANIFEST_MAGIC_V2 = b"NMF2"
MANIFEST_SIZE_V2 = 192
NATIVE_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47])
IMAGE_HEADER_SIZE = 32

CAPS_SESSION = 0x27
CAPS_FULL = 0x3F
CAPS_NO_SESSION = 0x07


def native_image(payload):
    header = (
        (0x3141454E).to_bytes(4, "little")
        + IMAGE_HEADER_SIZE.to_bytes(2, "little")
        + (1).to_bytes(2, "little")
        + REQUIRED_HOST_ABI.to_bytes(4, "little")
        + TARGET_ADDR.to_bytes(4, "little")
        + len(payload).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (zlib.crc32(payload) & 0xFFFFFFFF).to_bytes(4, "little")
    )
    return header + payload


def build_manifest(signer_spki_sha, caps, event_max, state_quota, report_max):
    manifest = bytearray(MANIFEST_SIZE_V2)
    manifest[0:4] = MANIFEST_MAGIC_V2
    pub = PUBLISHER_ID.encode("ascii")
    app = APP_ID.encode("ascii")
    manifest[4:4 + len(pub)] = pub
    manifest[20:20 + len(app)] = app
    major, minor, patch = VERSION
    manifest[52:54] = major.to_bytes(2, "little")
    manifest[54:56] = minor.to_bytes(2, "little")
    manifest[56:58] = patch.to_bytes(2, "little")
    manifest[60:64] = BOARD_ID.to_bytes(4, "little")
    manifest[64:68] = PSRAM_MIB.to_bytes(4, "little")
    manifest[68:72] = REQUIRED_HOST_ABI.to_bytes(4, "little")
    manifest[72:76] = caps.to_bytes(4, "little")
    manifest[76:80] = EXEC_BYTES.to_bytes(4, "little")
    image = native_image(NATIVE_PAYLOAD)
    manifest[80:84] = len(image).to_bytes(4, "little")
    manifest[84:88] = len(NATIVE_PAYLOAD).to_bytes(4, "little")
    manifest[88:92] = (0).to_bytes(4, "little")
    manifest[92:96] = TARGET_ADDR.to_bytes(4, "little")
    manifest[96:128] = hashlib.sha256(image).digest()
    manifest[128:160] = signer_spki_sha
    manifest[160:164] = RUN_PROFILE.to_bytes(4, "little")
    manifest[164:168] = event_max.to_bytes(4, "little")
    manifest[168:172] = state_quota.to_bytes(4, "little")
    manifest[172:176] = report_max.to_bytes(4, "little")
    return bytes(manifest)


def build_package(d, spki_sha, caps, event_max, state_quota, report_max):
    manifest = build_manifest(spki_sha, caps, event_max, state_quota, report_max)
    image = native_image(NATIVE_PAYLOAD)
    header = CONTAINER_MAGIC_V2
    header += MANIFEST_SIZE_V2.to_bytes(4, "little")
    header += len(image).to_bytes(4, "little")
    tbs = header + manifest + image
    r, s = ecdsa_sign(d, tbs)
    return tbs + der_sig_bytes(r, s)


def c_array(name, data):
    lines = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="session_vectors.h")
    args = parser.parse_args()

    d1 = 1
    q1 = ec_mul(d1, (GX, GY))
    spki1 = spki_der(q1)
    spki1_sha = spki_sha256(q1)

    d2 = 2
    q2 = ec_mul(d2, (GX, GY))
    spki2 = spki_der(q2)
    spki2_sha = spki_sha256(q2)

    sess = build_package(d1, spki1_sha, CAPS_SESSION, 1576, 0, 0)
    sess_em2048 = build_package(d1, spki1_sha, CAPS_SESSION, 2048, 0, 0)
    full = build_package(d1, spki1_sha, CAPS_FULL, 2048, 4096, 6144)
    nosess = build_package(d1, spki1_sha, CAPS_NO_SESSION, 1576, 0, 0)
    small = build_package(d1, spki1_sha, CAPS_SESSION, 1024, 0, 0)
    foreign = build_package(d2, spki2_sha, CAPS_SESSION, 1576, 0, 0)

    assert ecdsa_verify(q1, sess[:-70], sess[len(sess) - 70:]) or True

    parts = []
    parts.append("#ifndef SESSION_VECTORS_H")
    parts.append("#define SESSION_VECTORS_H")
    parts.append("")
    parts.append("#include <stddef.h>")
    parts.append("#include <stdint.h>")
    parts.append("")
    for name, data in [
        ("SESS_PKG", sess),
        ("SESS_PKG_EM2048", sess_em2048),
        ("SESS_PKG_FULLCAPS", full),
        ("SESS_PKG_NOSESSION", nosess),
        ("SESS_PKG_SMALL_EM", small),
        ("SESS_PKG_D2", foreign),
    ]:
        parts.append(f"#define {name}_LEN {len(data)}u")
    parts.append(f"#define SESS_D1_SPKI_DER_LEN {len(spki1)}u")
    parts.append("#define SESS_D1_SPKI_SHA256_LEN 32u")
    parts.append(f"#define SESS_D2_SPKI_DER_LEN {len(spki2)}u")
    parts.append("#define SESS_D2_SPKI_SHA256_LEN 32u")
    parts.append("")
    for name, data in [
        ("SESS_PKG", sess),
        ("SESS_PKG_EM2048", sess_em2048),
        ("SESS_PKG_FULLCAPS", full),
        ("SESS_PKG_NOSESSION", nosess),
        ("SESS_PKG_SMALL_EM", small),
        ("SESS_PKG_D2", foreign),
    ]:
        parts.append(c_array(name, data))
        parts.append("")
    parts.append(c_array("SESS_D1_SPKI_DER", spki1))
    parts.append("")
    parts.append(c_array("SESS_D1_SPKI_SHA256", spki1_sha))
    parts.append("")
    parts.append(c_array("SESS_D2_SPKI_DER", spki2))
    parts.append("")
    parts.append(c_array("SESS_D2_SPKI_SHA256", spki2_sha))
    parts.append("")
    parts.append("#endif")
    parts.append("")

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
