#!/usr/bin/env python3
"""Emit unsigned NEAPP05 v5 vectors for the floor and pause-fence tests."""
import importlib.util
import os

SPEC = importlib.util.spec_from_file_location("gf", os.path.join(
    os.path.dirname(__file__), "..", "app_unsigned", "gen_fixture.py"))
GF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GF)


def build(version, image_tag, app_id=GF.APP_ID):
    saved = GF.APP_ID
    GF.APP_ID = app_id
    try:
        return GF.native_package(version=version, image_tag=image_tag)
    finally:
        GF.APP_ID = saved


VECTORS = [
    ("V5_GENERIC_PACKAGE", build((2, 0, 0), 0)),
    ("V5_ENABLE_PACKAGE", build((2, 0, 0), 3)),
    ("V5_UPGRADE_PACKAGE", build((3, 0, 0), 1)),
    ("V5_DOWNGRADE_PACKAGE", build((1, 0, 0), 2)),
    ("V5_OTHER_APP_PACKAGE", build((2, 0, 0), 4, app_id=b"different-app")),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "build")
    os.makedirs(out, exist_ok=True)
    lines = ["#ifndef V5_VECTORS_H", "#define V5_VECTORS_H", ""]
    for name, data in VECTORS:
        lines.append("#define %s_LEN %du" % (name, len(data)))
        lines.append("static const uint8_t %s[] = {" % name)
        for i in range(0, len(data), 12):
            chunk = ", ".join("0x%02x" % b for b in data[i:i + 12])
            lines.append("    %s," % chunk)
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    path = os.path.join(out, "v5_vectors.h")
    with open(path, "w") as stream:
        stream.write("\n".join(lines) + "\n")
    print("wrote", path)


if __name__ == "__main__":
    main()
