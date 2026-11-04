#!/usr/bin/env python3
"""Unsigned NEUI fixtures for the sidecar lifecycle suite."""
import argparse
import hashlib
import importlib.util
import os

SOURCE = os.path.join(os.path.dirname(__file__), "..", "app_sidecar", "gen_sidecar_fixtures.py")
SPEC = importlib.util.spec_from_file_location("sidecar_fixture_source", SOURCE)
FIXTURES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURES)

BRIDGE_SOURCE = os.path.join(os.path.dirname(__file__), "..", "web_sidecar_bridge",
                             "gen_serving_fixtures.py")
BRIDGE_SPEC = importlib.util.spec_from_file_location("unsigned_sidecar_fixture", BRIDGE_SOURCE)
BRIDGE = importlib.util.module_from_spec(BRIDGE_SPEC)
BRIDGE_SPEC.loader.exec_module(BRIDGE)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    os.makedirs(args.output, exist_ok=True)
    small = FIXTURES.make_files(FIXTURES.SMALL_FILES)

    files = [(b"index.html", FIXTURES.deterministic_content("index.html", 1048576))]
    paths = [("assets/%03d.js" % i).encode() for i in range(63)]
    remaining = 2097152 - 1048576
    for i, path in enumerate(paths):
        size = remaining // 63 + (1 if i < remaining % 63 else 0)
        files.append((path, FIXTURES.deterministic_content(path.decode(), size)))
    files.sort(key=lambda item: item[0])
    limit = BRIDGE.build_unsigned_bundle(files, version=(3, 1, 1))
    with open(os.path.join(args.output, "positive_limit_64.bin"), "wb") as stream:
        stream.write(limit)

    changed = [(path, bytes([data[0] ^ 1]) + data[1:])
               if path == b"assets/app.min.js" else (path, data)
               for path, data in small]
    upgrade = BRIDGE.build_unsigned_bundle(changed, version=(1, 5, 1))
    conflict = BRIDGE.build_unsigned_bundle(changed, version=(1, 4, 2))
    downgrade = BRIDGE.build_unsigned_bundle(small, version=(1, 4, 1))
    nested = BRIDGE.build_unsigned_bundle(
        sorted(small + [(b"docs/index.html",
                         FIXTURES.deterministic_content("docs/index.html", 128))],
               key=lambda item: item[0]))
    duplicate = BRIDGE.build_unsigned_bundle(
        sorted(small + [(b"index.html",
                         FIXTURES.deterministic_content("duplicate-index", 128))],
               key=lambda item: item[0]))
    for name, data in (("positive_upgrade.bin", upgrade),
                       ("positive_version_conflict.bin", conflict),
                       ("positive_downgrade.bin", downgrade),
                       ("positive_nested_index.bin", nested),
                       ("negative_duplicate_root_index.bin", duplicate)):
        with open(os.path.join(args.output, name), "wb") as stream:
            stream.write(data)

    small_bundle = BRIDGE.build_unsigned_bundle(small, version=(1, 4, 2))
    with open(os.path.join(args.output, "positive_small.bin"), "wb") as stream:
        stream.write(small_bundle)

    ui_tbs = hashlib.sha256(small_bundle).hexdigest()
    lines = ["#ifndef LIFECYCLE_VECTORS_H", "#define LIFECYCLE_VECTORS_H", ""]
    lines.append("static const uint8_t LC_VEC_UI_TBS_SMALL[] = {")
    for i in range(0, len(ui_tbs), 2):
        lines.append("    0x%s," % ui_tbs[i:i + 2])
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "lifecycle_vectors.h"), "w") as stream:
        stream.write("\n".join(lines) + "\n")
    print("lifecycle fixtures written to", args.output)


if __name__ == "__main__":
    main()
