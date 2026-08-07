#!/usr/bin/env python3
"""Deterministic OVD profile catalog and native artifact helper.

The catalog is deliberately implemented with the Python standard library so
that the emulator remains usable on a fresh host.  External profiles are
described and rendered, but are never silently treated as generic QEMU
machines.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(os.environ.get("OMEGA_PROJECT_ROOT", Path(__file__).resolve().parents[1]))
PROFILE_DIR = Path(os.environ.get("OMEGA_PROFILE_DIR", ROOT / "emulator" / "profiles"))
CATALOG_PATH = Path(os.environ.get("OMEGA_PROFILE_CATALOG", PROFILE_DIR / "catalog.json"))
BUILD_ROOT = Path(os.environ.get("OMEGA_BUILD_ROOT", ROOT / "build"))
IMAGE_ROOT = Path(os.environ.get("OMEGA_IMAGE_ROOT", ROOT / "disk_images"))
SCHEMA_VERSION = 1
ARCHES = {"x86_64", "aarch64", "riscv64"}
BACKENDS = {"qemu", "qemu-vmapple", "android-avd"}
STATUSES = {"supported", "planned", "experimental", "conditional-external", "external-adapter", "deprecated", "retired"}
FORM_FACTORS = {"desktop", "laptop", "tablet", "mobile", "development-board", "server"}
POLICIES = {"require", "build-if-missing", "build-if-stale", "always-build", "reuse-verified"}
NATIVE_ARCH_EXECUTABLES = {"x86_64": "qemu-system-x86_64", "aarch64": "qemu-system-aarch64", "riscv64": "qemu-system-riscv64"}


class CatalogError(ValueError):
    pass


def read_json(path: Path) -> dict[str, Any]:
    try:
        with path.open(encoding="utf-8") as stream:
            value = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        raise CatalogError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise CatalogError(f"JSON root must be an object: {path}")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CatalogError(message)


def validate_profile(profile: dict[str, Any]) -> None:
    required = {"profile_id", "profile_version", "schema_version", "display_name", "form_factor", "architecture", "backend", "status", "qemu", "memory", "storage", "filesystem", "artifacts"}
    missing = sorted(required - profile.keys())
    require(not missing, f"{profile.get('profile_id', '<unknown>')}: missing fields: {', '.join(missing)}")
    pid = profile["profile_id"]
    require(isinstance(pid, str) and re.fullmatch(r"[a-z0-9][a-z0-9._-]{2,127}", pid) is not None, f"invalid profile_id: {pid!r}")
    require(isinstance(profile["profile_version"], str) and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", profile["profile_version"]) is not None, f"{pid}: invalid profile_version")
    require(profile["schema_version"] == SCHEMA_VERSION, f"{pid}: unsupported schema_version {profile['schema_version']}")
    require(profile["form_factor"] in FORM_FACTORS, f"{pid}: unsupported form_factor")
    arch = profile["architecture"]
    require(arch in ARCHES, f"{pid}: unsupported architecture {arch!r}")
    backend = profile["backend"]
    require(backend in BACKENDS, f"{pid}: unsupported backend {backend!r}")
    require(profile["status"] in STATUSES, f"{pid}: unsupported status")
    for section in ("qemu", "memory", "storage", "filesystem", "artifacts"):
        require(isinstance(profile[section], dict), f"{pid}: {section} must be an object")
    memory = profile["memory"]
    for key in ("minimum_mb", "default_mb", "maximum_mb"):
        require(isinstance(memory.get(key), int) and memory[key] > 0, f"{pid}: memory.{key} must be positive")
    require(memory["minimum_mb"] <= memory["default_mb"] <= memory["maximum_mb"], f"{pid}: invalid memory range")
    storage = profile["storage"]
    require(storage.get("container_format") == "raw", f"{pid}: native profile storage container_format must be raw") if backend == "qemu" else None
    require(isinstance(storage.get("image_size_mb"), int) and storage["image_size_mb"] > 0, f"{pid}: storage.image_size_mb must be positive")
    fs = profile["filesystem"]
    if backend == "qemu":
        require(fs.get("system") == "ext4", f"{pid}: native Omega system filesystem must be ext4")
        require(fs.get("boot") in {"fat32", "ext4"}, f"{pid}: boot filesystem must be fat32 or ext4")
    artifacts = profile["artifacts"]
    require(artifacts.get("policy") in POLICIES, f"{pid}: invalid artifact policy")
    require(isinstance(artifacts.get("kernel"), dict) and isinstance(artifacts.get("disk"), dict), f"{pid}: artifacts.kernel and artifacts.disk are required")
    require(artifacts["disk"].get("filesystem") == fs.get("system"), f"{pid}: disk filesystem must match filesystem.system")
    qemu = profile["qemu"]
    require(isinstance(qemu.get("executable"), str) and isinstance(qemu.get("machine"), str), f"{pid}: qemu executable and machine are required")
    if backend == "qemu":
        require(qemu["executable"] == NATIVE_ARCH_EXECUTABLES[arch], f"{pid}: qemu executable does not match architecture")
        expected_machine = {"x86_64": {"q35", "pc"}, "aarch64": {"virt"}, "riscv64": {"virt"}}[arch]
        require(qemu["machine"] in expected_machine, f"{pid}: unsupported native machine {qemu['machine']!r} for {arch}")
    elif backend == "android-avd":
        require(qemu["executable"] == "emulator" and qemu["machine"] == "avd", f"{pid}: Android profiles must use the emulator/avd adapter")
    elif backend == "qemu-vmapple":
        require(qemu["machine"] == "vmapple" and arch == "aarch64", f"{pid}: VMApple requires AArch64 vmapple")


def load_catalog() -> dict[str, Any]:
    catalog = read_json(CATALOG_PATH)
    require(catalog.get("catalog_version") == 1, "unsupported catalog_version")
    profiles = catalog.get("profiles")
    require(isinstance(profiles, list) and profiles, "catalog.profiles must be a non-empty array")
    ids: set[str] = set()
    for profile in profiles:
        require(isinstance(profile, dict), "each catalog profile must be an object")
        validate_profile(profile)
        require(profile["profile_id"] not in ids, f"duplicate profile_id: {profile['profile_id']}")
        ids.add(profile["profile_id"])
    catalog["profiles"] = sorted(profiles, key=lambda item: item["profile_id"])
    return catalog


def profile_or_error(profile_id: str) -> dict[str, Any]:
    for profile in load_catalog()["profiles"]:
        if profile["profile_id"] == profile_id:
            return profile
    raise CatalogError(f"profile not found: {profile_id}")


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_revision() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return "working-tree"


def artifact_paths(profile: dict[str, Any]) -> tuple[Path, Path]:
    arch = profile["architecture"]
    pid = profile["profile_id"]
    return BUILD_ROOT / arch / "omega.elf", IMAGE_ROOT / f"omega-{pid}-ext4.raw"


def build_kernel(profile: dict[str, Any], dry_run: bool) -> tuple[Path, dict[str, Any]]:
    kernel, _ = artifact_paths(profile)
    manifest_path = kernel.with_suffix(".artifact.json")
    expected = {"kind": "omega-kernel", "profile_id": profile["profile_id"], "architecture": profile["architecture"], "profile_version": profile["profile_version"], "source_revision": source_revision(), "toolchain": str(ROOT / "cmake" / f"{profile['architecture']}-toolchain.cmake")}
    current = read_json(manifest_path) if manifest_path.exists() else {}
    stale = not kernel.is_file() or any(current.get(key) != value for key, value in expected.items())
    if stale and not dry_run:
        cmake = shutil.which("cmake")
        require(cmake is not None, "cmake is required to build a missing or stale Omega kernel")
        kernel.parent.mkdir(parents=True, exist_ok=True)
        build_dir = kernel.parent
        subprocess.run([cmake, "-S", str(ROOT), "-B", str(build_dir), f"-DCMAKE_TOOLCHAIN_FILE={expected['toolchain']}", f"-DARCH={profile['architecture']}"], check=True)
        subprocess.run([cmake, "--build", str(build_dir)], check=True)
        require(kernel.is_file(), f"kernel build completed without {kernel}")
        expected["sha256"] = sha256_file(kernel)
        manifest_path.write_text(json.dumps(expected, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return kernel, {"path": str(kernel), "manifest": str(manifest_path), "status": "stale" if stale else "verified", "expected": expected}


def resolve_artifacts(profile: dict[str, Any], dry_run: bool) -> dict[str, Any]:
    if profile["backend"] != "qemu":
        return {"profile_id": profile["profile_id"], "backend": profile["backend"], "status": "external-artifact-required", "policy": profile["artifacts"]["policy"]}
    kernel, kernel_info = build_kernel(profile, dry_run)
    _, image = artifact_paths(profile)
    image_manifest = image.with_suffix(".artifact.json")
    image_info: dict[str, Any] = {"path": str(image), "manifest": str(image_manifest), "filesystem": "ext4"}
    kernel_sha = sha256_file(kernel) if kernel.is_file() else "unavailable"
    expected_image = {"kind": "omega-system-image", "profile_id": profile["profile_id"], "profile_version": profile["profile_version"], "filesystem": "ext4", "kernel_sha256": kernel_sha, "size_mb": profile["storage"]["image_size_mb"]}
    current_image = read_json(image_manifest) if image_manifest.exists() else {}
    stale_image = not image.is_file() or any(current_image.get(key) != value for key, value in expected_image.items())
    image_info["status"] = "stale" if stale_image else "verified"
    image_info["expected"] = expected_image
    if stale_image:
        image_info["required_tools"] = ["mke2fs or mkfs.ext4"]
        if not dry_run:
            mkfs = shutil.which("mke2fs") or shutil.which("mkfs.ext4")
            require(mkfs is not None, "mke2fs or mkfs.ext4 is required to create the default ext4 system image")
            image.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="omega-ext4-stage-") as stage_name:
                stage = Path(stage_name)
                boot = stage / "boot"
                boot.mkdir(parents=True)
                shutil.copy2(kernel, boot / "omega.elf")
                temporary_image = image.with_name(f".{image.name}.tmp")
                try:
                    with temporary_image.open("wb") as stream:
                        stream.truncate(profile["storage"]["image_size_mb"] * 1024 * 1024)
                    subprocess.run([mkfs, "-t", "ext4", "-L", profile["filesystem"].get("label", "OMEGA_ROOT"), "-d", str(stage), str(temporary_image)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    temporary_image.replace(image)
                finally:
                    temporary_image.unlink(missing_ok=True)
            expected_image["sha256"] = sha256_file(image)
            image_manifest.write_text(json.dumps(expected_image, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            image_info["status"] = "built"
    return {"profile_id": profile["profile_id"], "backend": "qemu", "filesystem": "ext4", "kernel": kernel_info, "disk": image_info}


def render(profile: dict[str, Any]) -> dict[str, Any]:
    qemu = profile["qemu"]
    args = [qemu["executable"], "-name", f"omega-profile-{profile['profile_id']}", "-m", str(profile["memory"]["default_mb"]), "-serial", "stdio"]
    if profile["backend"] == "qemu":
        args += ["-M", qemu["machine"], "-cpu", qemu["cpu"], "-kernel", "<resolved-omega.elf>"]
        if profile["architecture"] == "x86_64":
            args += ["-vga", "std", "-display", "none"]
        elif profile["display"]["backend"] == "none":
            args += ["-nographic"]
        else:
            args += ["-device", "virtio-gpu-pci", "-display", "none"]
    else:
        args += ["--avd", f"<{profile['profile_id']}>" ] if profile["backend"] == "android-avd" else ["-M", qemu["machine"]]
    return {"profile_id": profile["profile_id"], "backend": profile["backend"], "architecture": profile["architecture"], "status": profile["status"], "arguments": args, "filesystem": profile["filesystem"], "dependencies": {"artifact_policy": profile["artifacts"]["policy"]}}


def output(value: Any, as_json: bool) -> None:
    if as_json:
        print(json.dumps(value, indent=2, sort_keys=True))
    else:
        print(value if isinstance(value, str) else json.dumps(value, indent=2, sort_keys=True))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Omega Virtual Device profile catalog")
    sub = parser.add_subparsers(dest="command", required=True)
    listed = sub.add_parser("list"); listed.add_argument("--json", action="store_true"); listed.add_argument("--tsv", action="store_true")
    shown = sub.add_parser("show"); shown.add_argument("--profile", required=True); shown.add_argument("--json", action="store_true")
    valid = sub.add_parser("validate"); valid.add_argument("--profile"); valid.add_argument("--json", action="store_true")
    rendered = sub.add_parser("render"); rendered.add_argument("--profile", required=True); rendered.add_argument("--json", action="store_true")
    artifacts = sub.add_parser("artifacts"); artifacts.add_argument("--profile", required=True); artifacts.add_argument("--dry-run", action="store_true"); artifacts.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        catalog = load_catalog()
        if args.command == "list":
            value = [{"profile_id": p["profile_id"], "display_name": p["display_name"], "architecture": p["architecture"], "backend": p["backend"], "status": p["status"], "default_ram_mb": p["memory"]["default_mb"], "image_size_mb": p["storage"]["image_size_mb"], "native_creation": p["backend"] == "qemu"} for p in catalog["profiles"]]
        elif args.command == "show":
            value = profile_or_error(args.profile)
        elif args.command == "validate":
            if args.profile:
                validate_profile(profile_or_error(args.profile)); value = {"valid": True, "profile_id": args.profile}
            else:
                value = {"valid": True, "profiles": len(catalog["profiles"]), "catalog": str(CATALOG_PATH)}
        elif args.command == "render":
            value = render(profile_or_error(args.profile))
        else:
            value = resolve_artifacts(profile_or_error(args.profile), args.dry_run)
        if args.command == "list" and args.tsv:
            for item in value:
                print("\t".join(str(item[key]).lower() if isinstance(item[key], bool) else str(item[key]) for key in ("profile_id", "display_name", "architecture", "backend", "status", "default_ram_mb", "image_size_mb", "native_creation")))
        else:
            output(value, getattr(args, "json", False) or args.command in {"render", "artifacts"})
        return 0
    except CatalogError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
