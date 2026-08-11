#!/usr/bin/env python3
"""Create the small Omega initrd format consumed by kernel/sys/initrd.cpp."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER = struct.Struct("<I")
FILE_HEADER = struct.Struct("<I64sII")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("init", type=Path)
    args = parser.parse_args()

    payload = args.init.read_bytes()
    header_size = HEADER.size + FILE_HEADER.size
    image = bytearray(header_size + len(payload))
    image[: HEADER.size] = HEADER.pack(1)
    name = b"init\0" + b"\0" * (64 - len("init") - 1)
    image[HEADER.size : header_size] = FILE_HEADER.pack(0xBF, name, header_size, len(payload))
    image[header_size:] = payload
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)


if __name__ == "__main__":
    main()
