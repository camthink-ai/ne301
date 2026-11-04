#!/usr/bin/env python3
"""Unsigned NEAPP05 session vectors (native-container form per admission spec)."""
import argparse
import hashlib
import importlib.util
import os
import struct

SPEC = importlib.util.spec_from_file_location("gf", os.path.join(
    os.path.dirname(__file__), "..", "app_unsigned", "gen_fixture.py"))
GF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GF)

KEYSPEC = importlib.util.spec_from_file_location("anchor_keygen", os.path.join(
    os.path.dirname(__file__), "anchor_keygen.py"))
KEYGEN = importlib.util.module_from_spec(KEYSPEC)
KEYSPEC.loader.exec_module(KEYGEN)

CAP_SESSION = 0x20
CAP_AI_EVENTS = 0x04
CAP_REPORT_SUBMIT = 0x08
CAP_CONTROL_RECEIVE = 0x40
CAP_LIVE_PUBLISH = 0x200
CAP_LOG = 0x01
CAP_UNKNOWN2 = 0x02
BASE_CAPS = (CAP_LOG | CAP_UNKNOWN2 | CAP_SESSION | CAP_CONTROL_RECEIVE | CAP_AI_EVENTS)
REPORT_CAPS = BASE_CAPS | CAP_REPORT_SUBMIT
LIVE64_CAPS = (CAP_LOG | CAP_UNKNOWN2 | CAP_SESSION | CAP_CONTROL_RECEIVE |
               CAP_LIVE_PUBLISH)


def build(version=(1, 0, 1), caps=BASE_CAPS,
          psram_mib=64, event_max=1576, report_max=0, state_quota=0,
          control_max=512, live_publish_max=0, live_track_max=0,
          image_tag=0, patch_psram=None, patch_native_sha=None,
          bad_manifest_magic=False):
    data = bytearray(GF.native_package(version=version, image_tag=image_tag))
    m = 16
    struct.pack_into("<I", data, m + 64, psram_mib)
    struct.pack_into("<I", data, m + 72, caps)
    struct.pack_into("<I", data, m + 164, event_max)
    struct.pack_into("<I", data, m + 168, state_quota)
    struct.pack_into("<I", data, m + 172, report_max)
    struct.pack_into("<I", data, m + 176, control_max)
    struct.pack_into("<I", data, m + 184, live_publish_max)
    struct.pack_into("<I", data, m + 188, live_track_max)
    if patch_psram is not None:
        struct.pack_into("<I", data, m + 64, patch_psram)
    if patch_native_sha is not None:
        data[m + 96:m + 128] = patch_native_sha
    if bad_manifest_magic:
        data[m:m + 4] = b"NMF4"
    return bytes(data)


VARIANTS = [
    ("SESS_PKG", dict()),
    ("SESS_PKG_ALT", dict(image_tag=1)),
    ("SESS_PKG_REPORT", dict(caps=REPORT_CAPS, report_max=1576)),
    ("SESS_PKG_ENABLE", dict(caps=0x173, event_max=0, state_quota=2048)),
    ("SESS_PKG_LIVE64", dict(caps=LIVE64_CAPS, event_max=0, control_max=32,
                             live_publish_max=9008, live_track_max=64)),
    ("SESS_PKG_LIVE_SMALL", dict(caps=LIVE64_CAPS, event_max=0, control_max=32,
                                 live_publish_max=312, live_track_max=1)),
    ("SESS_PKG_LIVE_ONLY", dict(caps=0x223, event_max=0, control_max=0,
                                live_publish_max=9008, live_track_max=64)),
    ("SESS_PKG_OLD", dict(version=(1, 0, 0))),
    ("SESS_PKG_FULLCAPS", dict(caps=0x400)),
    ("SESS_PKG_NOSESSION", dict(caps=CAP_AI_EVENTS, control_max=0)),
    ("SESS_PKG_SMALL_EM", dict(patch_psram=128)),
    ("SESS_PKG_D2", dict(patch_native_sha=hashlib.sha256(b"foreign-native").digest())),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="session_vectors.h")
    args = parser.parse_args()

    d1 = 1
    q1 = KEYGEN.ec_mul(d1, (KEYGEN.GX, KEYGEN.GY))
    spki1 = KEYGEN.spki_der(q1)

    variants = [(name, build(**kwargs)) for name, kwargs in VARIANTS]
    parts = ["#ifndef SESSION_VECTORS_H", "#define SESSION_VECTORS_H", "",
             "#include <stddef.h>", "#include <stdint.h>", ""]
    for name, data in variants:
        parts.append(f"#define {name}_LEN {len(data)}u")
    parts.append(f"#define SESS_D1_SPKI_DER_LEN {len(spki1)}u")
    parts.append("")
    for name, data in variants:
        parts.append(f"static const uint8_t {name}[] = {{")
        for i in range(0, len(data), 12):
            parts.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",")
        parts.append("};")
        parts.append("")
    for name, data in (("SESS_D1_SPKI_DER", spki1),):
        parts.append(f"static const uint8_t {name}[] = {{")
        for i in range(0, len(data), 12):
            parts.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",")
        parts.append("};")
        parts.append("")
    parts.append("#endif")
    parts.append("")
    with open(args.output, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
