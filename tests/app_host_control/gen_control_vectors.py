#!/usr/bin/env python3
import hashlib
import zlib
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "app_admission"))

from anchor_keygen import (ecdsa_sign, der_sig_bytes, ec_mul, spki_der,
                           spki_sha256, GX, GY, N)

PUBLISHER_ID = "test-publisher"
APP_ID = "line-crossing"
VERSION = (3, 0, 0)
BOARD_ID = 0x00003010
PSRAM_MIB = 64
REQUIRED_HOST_ABI = 0x00030000
EXEC_BYTES = 4
TARGET_ADDR = 0x93E00000
RUN_PROFILE = 2

CONTAINER_MAGIC_V3 = bytes([0x4E, 0x45, 0x41, 0x50, 0x50, 0x03, 0x00, 0x00])
MANIFEST_MAGIC_V3 = b"NMF3"
MANIFEST_SIZE_V3 = 192
NATIVE_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47])
IMAGE_HEADER_SIZE = 32

D = 1

CAPS_FULL_NO_REPORTS = 0xF7
CAPS_MINIMAL_CONTROL = 0x63
CAPS_UNKNOWN_BIT = 0x1F7
CAPS_NO_STATE = 0xE7


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


def build_manifest(signer_spki_sha, caps, event_max, state_quota, report_max,
                   control_max, snapshot_max):
    manifest = bytearray(MANIFEST_SIZE_V3)
    manifest[0:4] = MANIFEST_MAGIC_V3
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
    manifest[176:180] = control_max.to_bytes(4, "little")
    manifest[180:184] = snapshot_max.to_bytes(4, "little")
    return bytes(manifest)


def build_package(d, caps, event_max, state_quota, report_max,
                  control_max, snapshot_max):
    q = ec_mul(d, (GX % N, GY % N))
    spki_sha = spki_sha256(q)
    manifest = build_manifest(spki_sha, caps, event_max, state_quota, report_max,
                              control_max, snapshot_max)
    image = native_image(NATIVE_PAYLOAD)
    header = CONTAINER_MAGIC_V3
    header += MANIFEST_SIZE_V3.to_bytes(4, "little")
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
    q = ec_mul(D, (GX % N, GY % N))
    spki = spki_der(q)
    spki_sha = spki_sha256(q)

    pkg_full = build_package(D, CAPS_FULL_NO_REPORTS, 2048, 4096, 0, 256, 1024)
    pkg_minimal = build_package(D, CAPS_MINIMAL_CONTROL, 0, 0, 0, 32, 0)
    pkg_unknown = build_package(D, CAPS_UNKNOWN_BIT, 2048, 4096, 0, 256, 1024)
    pkg_overquota = build_package(D, CAPS_FULL_NO_REPORTS, 2048, 4096, 0, 1024, 1024)
    pkg_nostate = build_package(D, CAPS_NO_STATE, 2048, 0, 0, 256, 1024)

    parts = []
    parts.append("#ifndef CONTROL_VECTORS_H")
    parts.append("#define CONTROL_VECTORS_H")
    parts.append("")
    parts.append("#include <stdint.h>")
    parts.append("#include <stddef.h>")
    parts.append("")
    parts.append(f"#define CTL_D1_SPKI_SHA256_LEN {len(spki_sha)}u")
    parts.append(f"#define CTL_V3_FULL_PACKAGE_LEN {len(pkg_full)}u")
    parts.append(f"#define CTL_V3_MINIMAL_PACKAGE_LEN {len(pkg_minimal)}u")
    parts.append(f"#define CTL_V3_UNKNOWN_CAP_PACKAGE_LEN {len(pkg_unknown)}u")
    parts.append(f"#define CTL_V3_OVER_QUOTA_PACKAGE_LEN {len(pkg_overquota)}u")
    parts.append(f"#define CTL_V3_NO_STATE_PACKAGE_LEN {len(pkg_nostate)}u")
    parts.append("")
    parts.append(c_array("CTL_D1_SPKI_SHA256", spki_sha))
    parts.append("")
    parts.append(c_array("CTL_V3_FULL_PACKAGE", pkg_full))
    parts.append("")
    parts.append(c_array("CTL_V3_MINIMAL_PACKAGE", pkg_minimal))
    parts.append("")
    parts.append(c_array("CTL_V3_UNKNOWN_CAP_PACKAGE", pkg_unknown))
    parts.append("")
    parts.append(c_array("CTL_V3_OVER_QUOTA_PACKAGE", pkg_overquota))
    parts.append("")
    parts.append(c_array("CTL_V3_NO_STATE_PACKAGE", pkg_nostate))
    parts.append("")
    parts.append("#endif")
    parts.append("")

    out = os.path.join(os.path.dirname(__file__), "control_vectors.h")
    with open(out, "w") as f:
        f.write("\n".join(parts))
    print(f"wrote {out}")
    print(f"full={len(pkg_full)} minimal={len(pkg_minimal)} "
          f"unknown={len(pkg_unknown)} overquota={len(pkg_overquota)} "
          f"nostate={len(pkg_nostate)}")


if __name__ == "__main__":
    main()
