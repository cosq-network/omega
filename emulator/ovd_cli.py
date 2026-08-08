"""Python command-line interface for Omega Virtual Devices."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .ovd_core import ARCHITECTURES, EmulatorError, OVDManager


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="omega-ovd")
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create"); create.add_argument("--name", required=True); create.add_argument("--arch", choices=sorted(ARCHITECTURES), default="x86_64"); create.add_argument("--machine"); create.add_argument("--cpu"); create.add_argument("--ram", type=int, default=1024); create.add_argument("--disk", type=int, default=64); create.add_argument("--storage", default="virtio"); create.add_argument("--network", default="none"); create.add_argument("--display"); create.add_argument("--initrd", default=""); create.add_argument("--boot-image", type=Path); create.add_argument("--allow-blank", action="store_true")
    profile_create = sub.add_parser("create-from-profile"); profile_create.add_argument("--profile", required=True); profile_create.add_argument("--name", required=True); profile_create.add_argument("--ram", type=int); profile_create.add_argument("--disk", type=int)
    profiles = sub.add_parser("profiles"); profiles.add_argument("action", choices=["list", "show", "validate", "render", "artifacts"]); profiles.add_argument("--profile"); profiles.add_argument("--json", action="store_true"); profiles.add_argument("--tsv", action="store_true"); profiles.add_argument("--dry-run", action="store_true")
    for name in ("list", "show", "validate", "status", "logs"):
        command = sub.add_parser(name); command.add_argument("--name") ; command.add_argument("--json", action="store_true")
    machines = sub.add_parser("machines"); machines.add_argument("--arch", required=True, choices=sorted(ARCHITECTURES)); machines.add_argument("--json", action="store_true")
    start = sub.add_parser("start"); start.add_argument("--name", required=True); start.add_argument("--gpu", action="store_const", const="true", default="auto"); start.add_argument("--no-gpu", action="store_const", const="false", dest="gpu"); start.add_argument("--machine"); start.add_argument("--storage"); start.add_argument("--storage-image", type=Path); start.add_argument("--network"); start.add_argument("--initrd", type=Path); start.add_argument("--readonly", action="store_true"); start.add_argument("--ephemeral", action="store_true"); start.add_argument("--qmp", action="store_true"); start.add_argument("--vnc", type=int, metavar="DISPLAY"); start.add_argument("--no-clipboard", action="store_false", dest="clipboard"); start.set_defaults(clipboard=True); start.add_argument("--daemon", action="store_true"); start.add_argument("--dry-run", action="store_true")
    stop = sub.add_parser("stop"); stop.add_argument("--name", required=True); stop.add_argument("--force", action="store_true")
    delete = sub.add_parser("delete"); delete.add_argument("--name", required=True); delete.add_argument("--force", action="store_true")
    clone = sub.add_parser("clone"); clone.add_argument("--name", required=True); clone.add_argument("--new-name", required=True)
    export = sub.add_parser("export"); export.add_argument("--name", required=True); export.add_argument("--output", type=Path, required=True)
    imp = sub.add_parser("import"); imp.add_argument("--archive", type=Path, required=True); imp.add_argument("--name", required=True)
    snap = sub.add_parser("snapshot"); snap.add_argument("action", choices=["create", "list", "apply"]); snap.add_argument("--name", required=True); snap.add_argument("--snapshot")
    args = parser.parse_args(argv); manager = OVDManager()
    try:
        if args.command == "create": value = manager.create(args.name, args.arch, args.ram, args.disk, args.storage, args.network, args.display, args.initrd, args.machine, args.boot_image, args.allow_blank, args.cpu); print(f"[+] Created OVD '{value.name}'.")
        elif args.command == "create-from-profile": value = manager.create_from_profile(args.profile, args.name, args.ram, args.disk); print(f"[+] Created OVD '{value.name}' from profile '{args.profile}'.")
        elif args.command == "profiles":
            if args.action == "list": value = manager.catalog.list()
            elif args.action == "show": value = manager.catalog.get(args.profile)
            elif args.action == "validate":
                if args.profile:
                    manager.catalog.get(args.profile)
                    value = {"valid": True, "profile_id": args.profile}
                else:
                    value = {"valid": True, "profiles": len(manager.catalog.profiles())}
            elif args.action == "render": value = manager.catalog.render(args.profile)
            else: value = manager.catalog.artifact_status(args.profile, args.dry_run)
            if args.tsv and args.action == "list": print("\n".join("\t".join(str(item[key]) for key in ("profile_id", "display_name", "architecture", "backend", "status", "default_ram_mb", "image_size_mb", "native_creation")) for item in value))
            else: print(json.dumps(value, indent=2, sort_keys=True) if args.json or args.action != "list" else "\n".join(item["profile_id"] for item in value))
        elif args.command == "machines": value = manager.qemu.machines(args.arch); print(json.dumps(value, indent=2) if args.json else "\n".join(f"{x['name']}\t{x['description']}" for x in value))
        elif args.command == "list": value = manager.list(); print(json.dumps(value, indent=2) if args.json else "\n".join(f"{x['name']} ({x['arch']}, {x['state']})" for x in value))
        elif args.command == "show": value = manager.validate(args.name, skip_image=True).config; print(json.dumps(value, indent=2) if args.json else "\n".join(f"{k}={v}" for k, v in value.items()))
        elif args.command == "validate": manager.validate(args.name); print(f"[PASS] OVD '{args.name}' configuration is valid.")
        elif args.command == "status": print("running" if manager.running(manager.load(args.name)) else "stopped")
        elif args.command == "logs": print(manager.load(args.name).log_path.read_text(encoding="utf-8", errors="replace"))
        elif args.command == "start":
            value = manager.start(args.name, gpu=args.gpu, machine=args.machine, storage=args.storage, storage_image=args.storage_image, network=args.network, initrd=args.initrd, readonly=args.readonly, ephemeral=args.ephemeral, qmp=args.qmp, vnc=args.vnc, clipboard=args.clipboard, daemon=args.daemon, dry_run=args.dry_run); print("QEMU command:", " ".join(value))
        elif args.command == "stop": manager.stop(args.name, args.force); print(f"[+] Stopped '{args.name}'.")
        elif args.command == "delete": manager.delete(args.name, args.force); print(f"[+] Deleted '{args.name}'.")
        elif args.command == "clone": manager.clone(args.name, args.new_name); print(f"[+] Cloned '{args.name}' to '{args.new_name}'.")
        elif args.command == "export": manager.export(args.name, args.output); print(f"[+] Exported '{args.name}' to {args.output}.")
        elif args.command == "import": manager.import_archive(args.archive, args.name); print(f"[+] Imported '{args.name}'.")
        elif args.command == "snapshot": value = manager.snapshot(args.action, args.name, args.snapshot); print("\n".join(value or []))
        return 0
    except (EmulatorError, OSError, ValueError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr); return 1


if __name__ == "__main__": sys.exit(main())
