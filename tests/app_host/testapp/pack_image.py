import struct
import subprocess
import sys
import zlib

APP_HOST_IMAGE_MAGIC = 0x3141454E
APP_HOST_IMAGE_FORMAT_VERSION = 1
APP_HOST_ABI_VERSION = 0x00010000
APP_HOST_EXEC_BASE = 0x93E00000


def main():
    elf_path, payload_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    nm_out = subprocess.check_output(["arm-none-eabi-nm", "-g", elf_path], text=True)
    entry_addr = None
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "app_entry":
            entry_addr = int(parts[0], 16)
    if entry_addr is None:
        sys.exit("app_entry symbol not found")

    payload = open(payload_path, "rb").read()
    entry_offset = entry_addr - APP_HOST_EXEC_BASE
    if entry_offset < 0 or entry_offset >= len(payload) or entry_offset % 2 != 0:
        sys.exit("entry offset out of range: %d (payload %d)" % (entry_offset, len(payload)))

    header = struct.pack(
        "<IHHIIIIII",
        APP_HOST_IMAGE_MAGIC,
        32,
        APP_HOST_IMAGE_FORMAT_VERSION,
        APP_HOST_ABI_VERSION,
        APP_HOST_EXEC_BASE,
        len(payload),
        entry_offset,
        0,
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    assert len(header) == 32

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(payload)
    print("packed %s: payload %d bytes, entry_offset %d, crc 0x%08x"
          % (out_path, len(payload), entry_offset, zlib.crc32(payload) & 0xFFFFFFFF))


if __name__ == "__main__":
    main()
