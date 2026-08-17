#!/usr/bin/env python3
"""Create the small Omega initrd format consumed by kernel/sys/initrd.cpp.

The legacy two-positional-argument form remains supported. Additional files
can be supplied as ``--file /path/in/initrd=host-file``.
"""

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
    parser.add_argument("--file", action="append", default=[], metavar="NAME=PATH")
    args = parser.parse_args()

    files: list[tuple[str, bytes]] = [("init", args.init.read_bytes())]
    for item in args.file:
        if "=" not in item:
            parser.error(f"--file must be NAME=PATH: {item}")
        name, source = item.split("=", 1)
        name = name.strip().lstrip("/")
        if not name or len(name.encode()) >= 64 or ".." in name.split("/"):
            parser.error(f"invalid initrd name: {name}")
        files.append((name, Path(source).read_bytes()))

    header_size = HEADER.size + FILE_HEADER.size * len(files)
    image = bytearray(header_size + sum(len(payload) for _, payload in files))
    image[: HEADER.size] = HEADER.pack(len(files))
    cursor = header_size
    for index, (name_text, payload) in enumerate(files):
        name = name_text.encode() + b"\0" * (64 - len(name_text.encode()))
        offset = cursor
        record = FILE_HEADER.pack(0xBF, name, offset, len(payload))
        begin = HEADER.size + index * FILE_HEADER.size
        image[begin : begin + FILE_HEADER.size] = record
        image[cursor : cursor + len(payload)] = payload
        cursor += len(payload)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)


if __name__ == "__main__":
    main()
