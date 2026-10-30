import argparse
import hashlib
import zlib

from anchor_keygen import (ecdsa_sign, der_sig_bytes, ec_mul, spki_der,
                           spki_sha256, ecdsa_verify, GX, GY, N)

PUBLISHER_ID = "test-publisher"
APP_ID = "hello-app"
VERSION = (1, 0, 0)
BOARD_ID = 0x00003010
PSRAM_MIB = 64
REQUIRED_HOST_ABI = 0x00020000
REQUIRED_HOST_CAPS = 0x0000003F
EXEC_BYTES = 4
TARGET_ADDR = 0x93E00000
RUN_PROFILE = 1
GOLDEN_EVENT_MAX = 2048
GOLDEN_STATE_QUOTA = 4096
GOLDEN_REPORT_MAX = 6144
ARCHIVE_EVENT_MAX = 1024
ARCHIVE_REPORT_MAX = 4096

CONTAINER_MAGIC_V2 = bytes([0x4E, 0x45, 0x41, 0x50, 0x50, 0x02, 0x00, 0x00])
MANIFEST_MAGIC_V2 = b"NMF2"
MANIFEST_SIZE_V2 = 192
NATIVE_PAYLOAD = bytes([0x00, 0x20, 0x70, 0x47])
IMAGE_HEADER_SIZE = 32
IMAGE_FORMAT_VERSION = 1

GOLDEN_PACKAGE_SHA256 = "3766e8afc39b267c26bece857e4177248f847aab265ce1b9c3dbf18b27c9eb3e"
GOLDEN_TBS_SHA256 = "e14e2cce8b3b0ecfa1d0af0538e931d3403c318a665126288c5c82b96ffbd278"
D1_SPKI_SHA256 = "5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3"
GOLDEN_SIG_DER = bytes.fromhex(
    "3044022011e9d2635e2036fb0bcbd1107a5e6d272c150473a8c1e029887604f9aa754737"
    "02205b6ca9206755a161913fcf9cdf57c789b363420bc16a74438bc439aeb80d11b0"
)


def native_image(payload):
    header = (
        (0x3141454E).to_bytes(4, "little")
        + IMAGE_HEADER_SIZE.to_bytes(2, "little")
        + IMAGE_FORMAT_VERSION.to_bytes(2, "little")
        + REQUIRED_HOST_ABI.to_bytes(4, "little")
        + TARGET_ADDR.to_bytes(4, "little")
        + len(payload).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (zlib.crc32(payload) & 0xFFFFFFFF).to_bytes(4, "little")
    )
    return header + payload


def build_manifest(signer_spki_sha, event_max, state_quota, report_max):
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
    manifest[72:76] = REQUIRED_HOST_CAPS.to_bytes(4, "little")
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


def build_package(d, spki_sha, event_max, state_quota, report_max):
    manifest = build_manifest(spki_sha, event_max, state_quota, report_max)
    image = native_image(NATIVE_PAYLOAD)
    header = CONTAINER_MAGIC_V2
    header += MANIFEST_SIZE_V2.to_bytes(4, "little")
    header += len(image).to_bytes(4, "little")
    tbs = header + manifest + image
    if d == 1 and (event_max, state_quota, report_max) == (GOLDEN_EVENT_MAX,
                                                           GOLDEN_STATE_QUOTA,
                                                           GOLDEN_REPORT_MAX):
        return tbs, GOLDEN_SIG_DER
    r, s = ecdsa_sign(d, tbs)
    return tbs, der_sig_bytes(r, s)


def c_array(name, data, const=True):
    decl = "static const" if const else "static"
    lines = [f"{decl} uint8_t {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="v2_vectors.h")
    args = parser.parse_args()

    d1 = 1
    q1 = ec_mul(d1, (GX, GY))
    spki1 = spki_der(q1)
    spki1_sha = spki_sha256(q1)
    assert hashlib.sha256(spki1).hexdigest() == D1_SPKI_SHA256

    golden_tbs, golden_sig = build_package(d1, spki1_sha,
                                           GOLDEN_EVENT_MAX,
                                           GOLDEN_STATE_QUOTA,
                                           GOLDEN_REPORT_MAX)
    golden = golden_tbs + golden_sig
    assert hashlib.sha256(golden_tbs).hexdigest() == GOLDEN_TBS_SHA256
    assert hashlib.sha256(golden).hexdigest() == GOLDEN_PACKAGE_SHA256
    assert ecdsa_verify(q1, golden_tbs, GOLDEN_SIG_DER)

    sig_body = GOLDEN_SIG_DER[2:2 + GOLDEN_SIG_DER[1]]
    r_len = sig_body[1]
    golden_r = int.from_bytes(sig_body[2:2 + r_len], "big")
    s_pos = 2 + r_len
    s_len = sig_body[s_pos + 1]
    golden_s = int.from_bytes(sig_body[s_pos + 2:s_pos + 2 + s_len], "big")
    high_s = golden_tbs + der_sig_bytes(golden_r, (N - golden_s) % N)
    assert high_s != golden
    assert ecdsa_verify(q1, golden_tbs, high_s[len(golden_tbs):])

    archive_tbs = bytearray(golden_tbs)
    archive_tbs[180:184] = ARCHIVE_EVENT_MAX.to_bytes(4, "little")
    archive_tbs[188:192] = ARCHIVE_REPORT_MAX.to_bytes(4, "little")
    archive_tbs = bytes(archive_tbs)
    ar, as_ = ecdsa_sign(d1, archive_tbs)
    archive = archive_tbs + der_sig_bytes(ar, as_)

    d2 = 2
    q2 = ec_mul(d2, (GX, GY))
    spki2 = spki_der(q2)
    spki2_sha = spki_sha256(q2)
    assert spki2_sha != spki1_sha

    parts = []
    parts.append("#ifndef V2_VECTORS_H")
    parts.append("#define V2_VECTORS_H")
    parts.append("")
    parts.append("#include <stddef.h>")
    parts.append("#include <stdint.h>")
    parts.append("")
    parts.append("#define V2_GOLDEN_PACKAGE_LEN %du" % len(golden))
    parts.append("#define V2_GOLDEN_TBS_LEN %du" % len(golden_tbs))
    parts.append("#define V2_HIGH_S_PACKAGE_LEN %du" % len(high_s))
    parts.append("#define V2_ARCHIVE_PACKAGE_LEN %du" % len(archive))
    parts.append("#define V2_D1_SPKI_DER_LEN %du" % len(spki1))
    parts.append("#define V2_D1_SPKI_SHA256_LEN 32u")
    parts.append("#define V2_D2_SPKI_DER_LEN %du" % len(spki2))
    parts.append("#define V2_D2_SPKI_SHA256_LEN 32u")
    parts.append("")
    parts.append(c_array("V2_GOLDEN_PACKAGE", golden))
    parts.append("")
    parts.append(c_array("V2_GOLDEN_TBS", golden_tbs))
    parts.append("")
    parts.append(c_array("V2_GOLDEN_TBS_SHA256", hashlib.sha256(golden_tbs).digest()))
    parts.append("")
    parts.append(c_array("V2_HIGH_S_PACKAGE", high_s))
    parts.append("")
    parts.append(c_array("V2_ARCHIVE_PACKAGE", archive))
    parts.append("")
    parts.append(c_array("V2_D1_SPKI_DER", spki1))
    parts.append("")
    parts.append(c_array("V2_D1_SPKI_SHA256", spki1_sha))
    parts.append("")
    parts.append(c_array("V2_D2_SPKI_DER", spki2))
    parts.append("")
    parts.append(c_array("V2_D2_SPKI_SHA256", spki2_sha))
    parts.append("")
    parts.append("#endif")
    parts.append("")

    with open(args.output, "w", encoding="ascii") as handle:
        handle.write("\n".join(parts))
    print("wrote %s golden=%d high_s=%d archive=%d" %
          (args.output, len(golden), len(high_s), len(archive)))


if __name__ == "__main__":
    main()
