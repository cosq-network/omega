#!/usr/bin/env python3
"""Create and validate deterministic Omega SDK application manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

TARGETS = {
    "x86_64-omega",
    "aarch64-omega",
    "riscv64-omega",
}
PROFILES = {"omega-c", "omega-cpp", "posix-static"}


def digest(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


def load_manifest(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("manifest root must be an object")
    return value


def validate(path: Path, root: Path) -> None:
    manifest = load_manifest(path)
    required = {"format", "name", "version", "target", "abi_version", "profile", "entry", "artifacts"}
    missing = required - manifest.keys()
    if missing:
        raise ValueError(f"missing fields: {', '.join(sorted(missing))}")
    if manifest["format"] != 1:
        raise ValueError("unsupported manifest format")
    if manifest["target"] not in TARGETS:
        raise ValueError("unsupported target")
    if manifest["profile"] not in PROFILES:
        raise ValueError("unsupported static SDK profile")
    entry = manifest["entry"]
    artifacts = manifest["artifacts"]
    if not isinstance(entry, str) or not isinstance(artifacts, dict) or entry not in artifacts:
        raise ValueError("entry must name an artifact")
    for name, record in artifacts.items():
        candidate = Path(name)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise ValueError(f"unsafe artifact path: {name}")
        if not isinstance(record, dict) or not isinstance(record.get("sha256"), str):
            raise ValueError(f"invalid artifact record: {name}")
        actual = digest(root / candidate)
        if actual["sha256"] != record["sha256"] or actual["size"] != record.get("size"):
            raise ValueError(f"artifact digest or size mismatch: {name}")


def create(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    artifacts = {}
    for relative in args.artifact:
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise ValueError(f"unsafe artifact path: {relative}")
        artifacts[relative] = digest(root / candidate)
    manifest = {
        "format": 1,
        "name": args.name,
        "version": args.version,
        "target": args.target,
        "abi_version": args.abi_version,
        "profile": args.profile,
        "entry": args.entry,
        "permissions": sorted(set(args.permission)),
        "artifacts": artifacts,
    }
    if args.entry not in artifacts:
        raise ValueError("--entry must name one of the --artifact values")
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    validate(args.output, root)


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    create_parser = sub.add_parser("create")
    create_parser.add_argument("--root", type=Path, required=True)
    create_parser.add_argument("--output", type=Path, required=True)
    create_parser.add_argument("--name", required=True)
    create_parser.add_argument("--version", required=True)
    create_parser.add_argument("--target", choices=sorted(TARGETS), required=True)
    create_parser.add_argument("--profile", choices=sorted(PROFILES), required=True)
    create_parser.add_argument("--abi-version", default="omega-abi-1")
    create_parser.add_argument("--entry", required=True)
    create_parser.add_argument("--artifact", action="append", required=True)
    create_parser.add_argument("--permission", action="append", default=[])
    create_parser.set_defaults(function=create)
    validate_parser = sub.add_parser("validate")
    validate_parser.add_argument("manifest", type=Path)
    validate_parser.add_argument("--root", type=Path, required=True)
    validate_parser.set_defaults(function=lambda args: validate(args.manifest, args.root))
    args = parser.parse_args()
    try:
        args.function(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print("[PASS] Omega SDK manifest validated")


if __name__ == "__main__":
    main()
