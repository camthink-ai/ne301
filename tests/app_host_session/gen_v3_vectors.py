import argparse
import hashlib
import os
import stat
import zlib

from anchor_keygen import der_sig_bytes, ec_mul, ecdsa_sign, load_signing_key, spki_der, spki_sha256, GX, GY, N

CONTAINER_MAGIC = bytes.fromhex("4e45415050030000")
MANIFEST_MAGIC = b"NMF3"
MANIFEST_SIZE = 192
ABI_VERSION = 0x00030000
BOARD_ID = 0x00003010
PSRAM_MIB = 64
TARGET_ADDR = 0x93E00000
PUBLISHER_ID = "ne301-exp-pub"
APP_ID = "hello-app"


def native_image(payload, native_format_version=1, native_abi=ABI_VERSION,
                 native_target_addr=TARGET_ADDR, native_image_size=None,
                 native_entry_offset=0, native_reserved=0, native_crc32=None):
    image_size = len(payload) if native_image_size is None else native_image_size
    crc = (zlib.crc32(payload) & 0xFFFFFFFF) if native_crc32 is None else native_crc32
    header = (
        (0x3141454E).to_bytes(4, "little")
        + (32).to_bytes(2, "little")
        + native_format_version.to_bytes(2, "little")
        + native_abi.to_bytes(4, "little")
        + native_target_addr.to_bytes(4, "little")
        + image_size.to_bytes(4, "little")
        + native_entry_offset.to_bytes(4, "little")
        + native_reserved.to_bytes(4, "little")
        + crc.to_bytes(4, "little")
    )
    return header + payload


def signed_package(private_key, public_key, caps=0x63, event_max=0, state_quota=0,
                   report_max=0, control_max=32, snapshot_max=0, run_profile=2,
                   manifest_abi=ABI_VERSION, native_abi=ABI_VERSION,
                   reserved=0, publisher_id=PUBLISHER_ID, target_board_id=BOARD_ID,
                   native_format_version=1, native_target_addr=TARGET_ADDR,
                   native_image_size=None, native_entry_offset=0, native_reserved=0,
                   native_crc32=None, manifest_file_len=None, manifest_image_size=None,
                   manifest_entry_offset=None, manifest_target_addr=None,
                   manifest_exec_region_bytes=None, manifest_image_sha=None,
                   required_psram_mib=PSRAM_MIB):
    payload = bytes((0x00, 0x20, 0x70, 0x47, 0x03, 0x03, 0x03, 0x03))
    image = bytearray(native_image(payload, native_format_version, native_abi,
                                   native_target_addr, native_image_size,
                                   native_entry_offset, native_reserved, native_crc32))
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:4] = MANIFEST_MAGIC
    publisher = publisher_id.encode("ascii")
    app = APP_ID.encode("ascii")
    manifest[4:4 + len(publisher)] = publisher
    manifest[20:20 + len(app)] = app
    manifest[52:54] = (3).to_bytes(2, "little")
    manifest[54:56] = (0).to_bytes(2, "little")
    manifest[56:58] = (0).to_bytes(2, "little")
    manifest[60:64] = target_board_id.to_bytes(4, "little")
    manifest[64:68] = required_psram_mib.to_bytes(4, "little")
    manifest[68:72] = manifest_abi.to_bytes(4, "little")
    manifest[72:76] = caps.to_bytes(4, "little")
    exec_region_bytes = len(payload) if manifest_exec_region_bytes is None else manifest_exec_region_bytes
    file_len = len(image) if manifest_file_len is None else manifest_file_len
    manifest_size = len(payload) if manifest_image_size is None else manifest_image_size
    entry_offset = native_entry_offset if manifest_entry_offset is None else manifest_entry_offset
    target_addr = native_target_addr if manifest_target_addr is None else manifest_target_addr
    manifest[76:80] = exec_region_bytes.to_bytes(4, "little")
    manifest[80:84] = file_len.to_bytes(4, "little")
    manifest[84:88] = manifest_size.to_bytes(4, "little")
    manifest[88:92] = entry_offset.to_bytes(4, "little")
    manifest[92:96] = target_addr.to_bytes(4, "little")
    manifest[96:128] = (hashlib.sha256(image).digest() if manifest_image_sha is None
                         else manifest_image_sha)
    manifest[128:160] = spki_sha256(public_key)
    manifest[160:164] = run_profile.to_bytes(4, "little")
    manifest[164:168] = event_max.to_bytes(4, "little")
    manifest[168:172] = state_quota.to_bytes(4, "little")
    manifest[172:176] = report_max.to_bytes(4, "little")
    manifest[176:180] = control_max.to_bytes(4, "little")
    manifest[180:184] = snapshot_max.to_bytes(4, "little")
    manifest[184] = reserved
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
    fingerprint = spki_sha256(public_key)
    foreign_key = load_signing_key(args.foreign_key)
    packages = {
        "V3_GENERIC_MINIMAL_PACKAGE": signed_package(private_key, public_key),
        "V3_GENERIC_SMALL_EVENT_PACKAGE": signed_package(
            private_key, public_key, caps=0x67, event_max=1, control_max=32),
        "V3_RESOURCE_PACKAGE": signed_package(
            private_key, public_key, caps=0xFF, event_max=1576, state_quota=256,
            report_max=6144, control_max=512, snapshot_max=2048),
        "V3_LINE_CROSSING_PACKAGE": signed_package(
            private_key, public_key, caps=0xFF, event_max=2048, state_quota=4096,
            report_max=6144, control_max=512, snapshot_max=2048),
        "V3_MISSING_BIT5_PACKAGE": signed_package(
            private_key, public_key, caps=0x43, control_max=32),
        "V3_MISSING_BIT6_PACKAGE": signed_package(
            private_key, public_key, caps=0x23, control_max=0),
        "V3_UNKNOWN_CAP_PACKAGE": signed_package(
            private_key, public_key, caps=0x163, control_max=32),
        "V3_BAD_PROFILE_PACKAGE": signed_package(
            private_key, public_key, run_profile=1),
        "V3_NO_CONTROL_QUOTA_PACKAGE": signed_package(
            private_key, public_key, control_max=0),
        "V3_SMALL_CONTROL_QUOTA_PACKAGE": signed_package(
            private_key, public_key, control_max=16),
        "V3_SNAPSHOT_WITHOUT_BIT7_PACKAGE": signed_package(
            private_key, public_key, snapshot_max=32),
        "V3_BIT7_WITHOUT_SNAPSHOT_PACKAGE": signed_package(
            private_key, public_key, caps=0xE3, snapshot_max=0),
        "V3_LINE_CROSSING_SMALL_EVENT_PACKAGE": signed_package(
            private_key, public_key, caps=0xFF, event_max=1575, state_quota=4096,
            report_max=6144, control_max=512, snapshot_max=2048),
        "V3_LINE_CROSSING_SMALL_REPORT_PACKAGE": signed_package(
            private_key, public_key, caps=0xFF, event_max=2048, state_quota=4096,
            report_max=6143, control_max=512, snapshot_max=2048),
        "V3_BAD_MANIFEST_ABI_PACKAGE": signed_package(
            private_key, public_key, manifest_abi=0x00020000),
        "V3_BAD_NATIVE_ABI_PACKAGE": signed_package(
            private_key, public_key, native_abi=0x00020000),
        "V3_BAD_NATIVE_FORMAT_PACKAGE": signed_package(
            private_key, public_key, native_format_version=2),
        "V3_BAD_NATIVE_RESERVED_PACKAGE": signed_package(
            private_key, public_key, native_reserved=1),
        "V3_BAD_NATIVE_IMAGE_SIZE_PACKAGE": signed_package(
            private_key, public_key, native_image_size=7),
        "V3_BAD_NATIVE_ENTRY_MISMATCH_PACKAGE": signed_package(
            private_key, public_key, native_entry_offset=2, manifest_entry_offset=0),
        "V3_BAD_NATIVE_ENTRY_RANGE_PACKAGE": signed_package(
            private_key, public_key, native_entry_offset=8),
        "V3_BAD_NATIVE_TARGET_MISMATCH_PACKAGE": signed_package(
            private_key, public_key, native_target_addr=TARGET_ADDR + 0x100,
            manifest_target_addr=TARGET_ADDR),
        "V3_BAD_NATIVE_CRC_PACKAGE": signed_package(
            private_key, public_key, native_crc32=1),
        "V3_BAD_NATIVE_SHA_PACKAGE": signed_package(
            private_key, public_key, manifest_image_sha=bytes(32)),
        "V3_BAD_MANIFEST_FILE_LEN_PACKAGE": signed_package(
            private_key, public_key, manifest_file_len=41),
        "V3_BAD_MANIFEST_EXEC_SIZE_PACKAGE": signed_package(
            private_key, public_key, manifest_exec_region_bytes=2097153),
        "V3_UNSUPPORTED_TARGET_ADDR_PACKAGE": signed_package(
            private_key, public_key, native_target_addr=TARGET_ADDR + 0x100),
        "V3_UNSUPPORTED_PSRAM_PACKAGE": signed_package(
            private_key, public_key, required_psram_mib=65),
        "V3_GENERIC_BUSINESS_QUOTAS_PACKAGE": signed_package(
            private_key, public_key, caps=0x7F, event_max=16, state_quota=16,
            report_max=16, control_max=32),
        "V3_BAD_RESERVED_PACKAGE": signed_package(
            private_key, public_key, reserved=1),
        "V3_WRONG_PUBLISHER_PACKAGE": signed_package(
            private_key, public_key, publisher_id="other-publisher"),
        "V3_FOREIGN_SIGNATURE_PACKAGE": signed_package(
            foreign_key, public_key),
        "V3_TARGET_MISMATCH_PACKAGE": signed_package(
            private_key, public_key, target_board_id=BOARD_ID + 1),
    }
    packages["V3_TAMPERED_TBS_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_TAMPERED_TBS_PACKAGE"][16 + MANIFEST_SIZE + 32] ^= 0x01
    packages["V3_TAMPERED_TBS_PACKAGE"] = bytes(packages["V3_TAMPERED_TBS_PACKAGE"])
    packages["V3_TAMPERED_SIGNATURE_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_TAMPERED_SIGNATURE_PACKAGE"][-1] ^= 0x01
    packages["V3_TAMPERED_SIGNATURE_PACKAGE"] = bytes(packages["V3_TAMPERED_SIGNATURE_PACKAGE"])
    packages["V3_SMALL_CONTROL_TAMPERED_PACKAGE"] = bytearray(packages["V3_SMALL_CONTROL_QUOTA_PACKAGE"])
    packages["V3_SMALL_CONTROL_TAMPERED_PACKAGE"][-1] ^= 0x01
    packages["V3_SMALL_CONTROL_TAMPERED_PACKAGE"] = bytes(packages["V3_SMALL_CONTROL_TAMPERED_PACKAGE"])
    packages["V3_DER_TAIL_PACKAGE"] = packages["V3_GENERIC_MINIMAL_PACKAGE"] + b"\x00"
    generic = packages["V3_GENERIC_MINIMAL_PACKAGE"]
    tbs_len = 16 + MANIFEST_SIZE + int.from_bytes(generic[12:16], "little")
    packages["V3_DER_NONMINIMAL_PACKAGE"] = generic[:tbs_len] + b"\x30\x81" + generic[tbs_len + 1:]
    packages["V3_DER_SCALAR_OUT_OF_RANGE_PACKAGE"] = generic[:tbs_len] + der_sig_bytes(N, 1)
    packages["V3_BAD_MAGIC_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_BAD_MAGIC_PACKAGE"][5] = 0x04
    packages["V3_BAD_MAGIC_PACKAGE"] = bytes(packages["V3_BAD_MAGIC_PACKAGE"])
    packages["V3_BAD_MANIFEST_LENGTH_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_BAD_MANIFEST_LENGTH_PACKAGE"][8:12] = (191).to_bytes(4, "little")
    packages["V3_BAD_MANIFEST_LENGTH_PACKAGE"] = bytes(packages["V3_BAD_MANIFEST_LENGTH_PACKAGE"])
    packages["V3_BAD_MANIFEST_MAGIC_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_BAD_MANIFEST_MAGIC_PACKAGE"][16] ^= 0x01
    packages["V3_BAD_MANIFEST_MAGIC_PACKAGE"] = bytes(packages["V3_BAD_MANIFEST_MAGIC_PACKAGE"])
    packages["V3_NATIVE_FILE_OVER_LIMIT_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_NATIVE_FILE_OVER_LIMIT_PACKAGE"][12:16] = (2097153).to_bytes(4, "little")
    packages["V3_NATIVE_FILE_OVER_LIMIT_PACKAGE"] = bytes(packages["V3_NATIVE_FILE_OVER_LIMIT_PACKAGE"])
    packages["V3_NATIVE_FILE_TOO_SHORT_PACKAGE"] = bytearray(packages["V3_GENERIC_MINIMAL_PACKAGE"])
    packages["V3_NATIVE_FILE_TOO_SHORT_PACKAGE"][12:16] = (31).to_bytes(4, "little")
    packages["V3_NATIVE_FILE_TOO_SHORT_PACKAGE"] = bytes(packages["V3_NATIVE_FILE_TOO_SHORT_PACKAGE"])

    parts = ["#ifndef V3_VECTORS_H", "#define V3_VECTORS_H", "",
             "#include <stddef.h>", "#include <stdint.h>", "",
             "#define V3_TEST_SPKI_DER_LEN %du" % len(spki_der(public_key)),
             "#define V3_TEST_SPKI_SHA256_LEN %du" % len(fingerprint), ""]
    parts.append(c_array("V3_TEST_SPKI_DER", spki_der(public_key)))
    parts.append("")
    parts.append(c_array("V3_TEST_SPKI_SHA256", fingerprint))
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


if __name__ == "__main__":
    main()
