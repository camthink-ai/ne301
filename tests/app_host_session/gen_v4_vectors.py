import argparse
import hashlib
import os
import stat
import zlib

from anchor_keygen import der_sig_bytes, ec_mul, ecdsa_sign, load_signing_key, spki_der, spki_sha256, GX, GY

CONTAINER_MAGIC = bytes.fromhex("4e45415050040000")
MANIFEST_MAGIC = b"NMF4"
MANIFEST_SIZE = 208
ABI_VERSION = 0x00040000
BOARD_ID = 0x00003010
PSRAM_MIB = 64
TARGET_ADDR = 0x93E00000
PUBLISHER_ID = "ne301-exp-pub"
APP_ID = "hello-app"


def native_image(private_abi=ABI_VERSION, payload=None):
    if payload is None:
        payload = bytes((0x00, 0x20, 0x70, 0x47, 0x03, 0x03, 0x03, 0x03))
    header = (
        (0x3141454E).to_bytes(4, "little")
        + (32).to_bytes(2, "little")
        + (1).to_bytes(2, "little")
        + private_abi.to_bytes(4, "little")
        + TARGET_ADDR.to_bytes(4, "little")
        + len(payload).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (0).to_bytes(4, "little")
        + (zlib.crc32(payload) & 0xFFFFFFFF).to_bytes(4, "little")
    )
    return header + payload


def signed_package(private_key, public_key, caps=0x63, event_max=0,
                   state_quota=0, report_max=0, control_max=32,
                   snapshot_max=0, live_publish_max=0, live_track_max=0,
                   manifest_abi=ABI_VERSION, native_abi=ABI_VERSION,
                   publisher_id=PUBLISHER_ID, board_id=BOARD_ID,
                   version_minor=0):
    image = native_image(native_abi)
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:4] = MANIFEST_MAGIC
    manifest[4:20] = publisher_id.encode("ascii").ljust(16, b"\0")
    manifest[20:52] = APP_ID.encode("ascii").ljust(32, b"\0")
    manifest[52:54] = (4).to_bytes(2, "little")
    manifest[54:56] = version_minor.to_bytes(2, "little")
    manifest[60:64] = board_id.to_bytes(4, "little")
    manifest[64:68] = PSRAM_MIB.to_bytes(4, "little")
    manifest[68:72] = manifest_abi.to_bytes(4, "little")
    manifest[72:76] = caps.to_bytes(4, "little")
    manifest[76:80] = len(image[32:]).to_bytes(4, "little")
    manifest[80:84] = len(image).to_bytes(4, "little")
    manifest[84:88] = len(image[32:]).to_bytes(4, "little")
    manifest[88:92] = (0).to_bytes(4, "little")
    manifest[92:96] = TARGET_ADDR.to_bytes(4, "little")
    manifest[96:128] = hashlib.sha256(image).digest()
    manifest[128:160] = spki_sha256(public_key)
    manifest[160:164] = (3).to_bytes(4, "little")
    manifest[164:168] = event_max.to_bytes(4, "little")
    manifest[168:172] = state_quota.to_bytes(4, "little")
    manifest[172:176] = report_max.to_bytes(4, "little")
    manifest[176:180] = control_max.to_bytes(4, "little")
    manifest[180:184] = snapshot_max.to_bytes(4, "little")
    manifest[184:188] = live_publish_max.to_bytes(4, "little")
    manifest[188:192] = live_track_max.to_bytes(4, "little")
    tbs = (CONTAINER_MAGIC + MANIFEST_SIZE.to_bytes(4, "little")
           + len(image).to_bytes(4, "little") + manifest + image)
    r, s = ecdsa_sign(private_key, tbs)
    return tbs + der_sig_bytes(r, s)


def c_array(name, data):
    lines = ["static const uint8_t %s[] = {" % name]
    for i in range(0, len(data), 12):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--key", required=True)
    parser.add_argument("--foreign-key", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    for key_path in (args.key, args.foreign_key):
        if stat.S_IMODE(os.stat(key_path).st_mode) != 0o600:
            raise SystemExit("signing key mode must be 0600")
    private_key = load_signing_key(args.key)
    public_key = ec_mul(private_key, (GX, GY))
    foreign_key = load_signing_key(args.foreign_key)
    foreign_public_key = ec_mul(foreign_key, (GX, GY))
    packages = {
        "V4_GENERIC_PACKAGE": signed_package(private_key, public_key),
        "V4_GENERIC_NO_CONTROL_PACKAGE": signed_package(
            private_key, public_key, caps=0x23, control_max=0),
        "V4_ENABLE_PACKAGE": signed_package(
            private_key, public_key, caps=0x173, state_quota=256, control_max=48),
        "V4_LIVE_PACKAGE": signed_package(
            private_key, public_key, caps=0x263, control_max=32,
            live_publish_max=312, live_track_max=1),
        "V4_LIVE_64_PACKAGE": signed_package(
            private_key, public_key, caps=0x263, control_max=32,
            live_publish_max=9008, live_track_max=64),
        "V4_UNKNOWN_CAP_PACKAGE": signed_package(
            private_key, public_key, caps=0x463),
        "V4_ENABLE_MISSING_DEPS_PACKAGE": signed_package(
            private_key, public_key, caps=0x163, control_max=48),
        "V4_ENABLE_SMALL_CONTROL_PACKAGE": signed_package(
            private_key, public_key, caps=0x173, state_quota=256, control_max=47),
        "V4_LIVE_SMALL_CLOSURE_PACKAGE": signed_package(
            private_key, public_key, caps=0x263, control_max=32,
            live_publish_max=9007, live_track_max=64),
        "V4_LIVE_ZERO_TRACK_PACKAGE": signed_package(
            private_key, public_key, caps=0x263, control_max=32,
            live_publish_max=312, live_track_max=0),
        "V4_BAD_MANIFEST_ABI_PACKAGE": signed_package(
            private_key, public_key, manifest_abi=0x00030000),
        "V4_BAD_NATIVE_ABI_PACKAGE": signed_package(
            private_key, public_key, native_abi=0x00030000),
        "V4_WRONG_PUBLISHER_PACKAGE": signed_package(
            private_key, public_key, publisher_id="other-publisher"),
        "V4_WRONG_TARGET_PACKAGE": signed_package(
            private_key, public_key, board_id=BOARD_ID + 1),
        "V4_NEXT_GENERATION_PACKAGE": signed_package(
            private_key, public_key, version_minor=1),
        "V4_ROTATED_KEY_PACKAGE": signed_package(
            foreign_key, foreign_public_key, version_minor=1),
        "V4_FOREIGN_SIGNATURE_PACKAGE": signed_package(foreign_key, public_key),
    }
    generic = packages["V4_GENERIC_PACKAGE"]
    tbs_len = 16 + MANIFEST_SIZE + int.from_bytes(generic[12:16], "little")
    packages["V4_TAMPERED_TBS_PACKAGE"] = bytearray(generic)
    packages["V4_TAMPERED_TBS_PACKAGE"][224 + 32] ^= 1
    packages["V4_TAMPERED_TBS_PACKAGE"] = bytes(packages["V4_TAMPERED_TBS_PACKAGE"])
    packages["V4_BAD_MAGIC_PACKAGE"] = bytearray(generic)
    packages["V4_BAD_MAGIC_PACKAGE"][5] = 3
    packages["V4_BAD_MAGIC_PACKAGE"] = bytes(packages["V4_BAD_MAGIC_PACKAGE"])
    packages["V4_BAD_MANIFEST_LENGTH_PACKAGE"] = bytearray(generic)
    packages["V4_BAD_MANIFEST_LENGTH_PACKAGE"][8:12] = (192).to_bytes(4, "little")
    packages["V4_BAD_MANIFEST_LENGTH_PACKAGE"] = bytes(packages["V4_BAD_MANIFEST_LENGTH_PACKAGE"])
    packages["V4_BAD_RESERVED_PACKAGE"] = bytearray(generic)
    packages["V4_BAD_RESERVED_PACKAGE"][16 + 192] = 1
    packages["V4_BAD_RESERVED_PACKAGE"] = bytes(packages["V4_BAD_RESERVED_PACKAGE"])
    packages["V4_TAMPERED_SIGNATURE_PACKAGE"] = bytearray(generic)
    packages["V4_TAMPERED_SIGNATURE_PACKAGE"][-1] ^= 1
    packages["V4_TAMPERED_SIGNATURE_PACKAGE"] = bytes(packages["V4_TAMPERED_SIGNATURE_PACKAGE"])
    packages["V4_TRAILING_BYTE_PACKAGE"] = generic + b"\0"
    parts = ["#ifndef V4_VECTORS_H", "#define V4_VECTORS_H", "",
             "#include <stddef.h>", "#include <stdint.h>", "",
             "#define V4_TEST_SPKI_DER_LEN %du" % len(spki_der(public_key)),
             "#define V4_TEST_SPKI_SHA256_LEN 32u", ""]
    parts.append(c_array("V4_TEST_SPKI_DER", spki_der(public_key)))
    parts.append("")
    parts.append(c_array("V4_TEST_SPKI_SHA256", spki_sha256(public_key)))
    parts.append("")
    for name, package in packages.items():
        parts.append("#define %s_LEN %du" % (name, len(package)))
    parts.append("")
    for name, package in packages.items():
        parts.append(c_array(name, package))
        parts.append("")
    parts.extend(("#endif", ""))
    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts))
    print("generated %d signed v4 vectors" % len(packages))


if __name__ == "__main__":
    main()
