"""Cross-platform Omega Virtual Device management core.

The core deliberately uses Python filesystem, archive, socket, and process
APIs. It never invokes a shell. QEMU and CMake remain optional direct-process
backends because they are the emulator/build providers, not shell utilities.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import tarfile
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# Optional psutil import for robust PID validation on macOS/Linux.
try:
    import psutil
except ImportError:
    psutil = None


ARCHITECTURES = {"x86_64", "aarch64", "armv7", "riscv64"}
STORAGE_PROFILES = {"auto", "virtio", "ahci", "usb", "sd", "optical", "none"}
NETWORK_PROFILES = {"none", "user", "socket"}
DISPLAY_PROFILES = {"standard-vga", "simplefb", "virtio-gpu", "none"}
NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
SCHEMA_VERSION = 1


class EmulatorError(RuntimeError):
    """A user-facing, recoverable emulator error."""


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def root_from_environment() -> Path:
    return Path(os.environ.get("OMEGA_OVD_ROOT", project_root() / "emulator" / "ovd")).expanduser().resolve()


def build_root() -> Path:
    return Path(os.environ.get("OMEGA_BUILD_ROOT", project_root() / "build")).expanduser().resolve()


def image_root() -> Path:
    return Path(os.environ.get("OMEGA_IMAGE_ROOT", project_root() / "disk_images")).expanduser().resolve()


def validate_name(name: str) -> str:
    if not NAME_PATTERN.fullmatch(name or ""):
        raise EmulatorError(f"Invalid OVD name '{name}'. Use 1-64 letters, numbers, '.', '_' or '-'.")
    return name


def validate_uint(label: str, value: int | str, minimum: int, maximum: int) -> int:
    try:
        number = int(value)
    except (TypeError, ValueError) as exc:
        raise EmulatorError(f"{label} must be an integer.") from exc
    if number < minimum or number > maximum:
        raise EmulatorError(f"{label} must be between {minimum} and {maximum}.")
    return number


def require_choice(label: str, value: str, choices: set[str]) -> str:
    if value not in choices:
        raise EmulatorError(f"Unsupported {label} '{value}'. Supported: {', '.join(sorted(choices))}.")
    return value


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as stream:
        stream.write(content)
        temporary = Path(stream.name)
    temporary.replace(path)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_file_atomic(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copy2(source, temporary)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def parse_config(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise EmulatorError(f"Missing configuration: {path}")
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def write_config(path: Path, values: dict[str, str]) -> None:
    atomic_write(path, "".join(f"{key}={value}\n" for key, value in values.items()))


def config_value(values: dict[str, str], field: str, default: str = "") -> str:
    aliases = {
        "name": ("ovd.name", ""), "arch": ("ovd.arch", ""), "machine": ("ovd.machine", ""), "cpu": ("ovd.cpu", ""),
        "ram": ("ovd.ram_mb", values.get("ovd.ram", default)),
        "disk": ("ovd.disk_mb", values.get("ovd.disk", default)),
        "storage": ("ovd.storage.profile", values.get("ovd.storage", "auto")),
        "image": ("ovd.storage.image", "userdata.img"),
        "readonly": ("ovd.storage.readonly", "false"),
        "display": ("ovd.display.profile", ""), "network": ("ovd.network.profile", "none"),
        "initrd": ("ovd.initrd", ""), "profile_id": ("ovd.profile.id", ""),
        "filesystem": ("ovd.filesystem.system", ""), "boot_filesystem": ("ovd.filesystem.boot", ""),
        "artifact_policy": ("ovd.artifacts.policy", "require"), "bootable": ("ovd.bootable", "false"),
    }
    key, fallback = aliases.get(field, (field, default))
    return values.get(key, fallback)


def safe_child(root: Path, relative: str) -> Path:
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as exc:
        raise EmulatorError("A storage path must remain inside the OVD directory.") from exc
    return candidate


@dataclass
class OVD:
    name: str
    path: Path
    config: dict[str, str]

    @property
    def arch(self) -> str: return config_value(self.config, "arch")
    @property
    def ram(self) -> int: return int(config_value(self.config, "ram", "1024"))
    @property
    def disk(self) -> int: return int(config_value(self.config, "disk", "64"))
    @property
    def storage(self) -> str: return config_value(self.config, "storage", "auto")
    @property
    def machine(self) -> str: return config_value(self.config, "machine") or ("q35" if self.arch == "x86_64" else "virt")
    @property
    def cpu(self) -> str: return config_value(self.config, "cpu") or ({"x86_64": "max", "aarch64": "cortex-a57", "armv7": "cortex-a7", "riscv64": "rv64"}[self.arch])
    @property
    def image(self) -> Path: return safe_child(self.path, config_value(self.config, "image", "userdata.img"))
    @property
    def state_path(self) -> Path: return self.path / "state"
    @property
    def pid_path(self) -> Path: return self.state_path / "qemu.pid"
    @property
    def log_path(self) -> Path: return self.state_path / "qemu.log"
    @property
    def command_path(self) -> Path: return self.state_path / "command.json"
    @property
    def qmp_path(self) -> Path: return self.state_path / "qmp.sock"
    @property
    def process_path(self) -> Path: return self.state_path / "process.json"
    @property
    def lifecycle_path(self) -> Path: return self.state_path / "lifecycle.json"


class ProfileCatalog:
    def __init__(self, root: Path | None = None):
        self.root = root or project_root()
        self.path = self.root / "emulator" / "profiles" / "catalog.json"

    def load(self) -> dict[str, Any]:
        try:
            value = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise EmulatorError(f"Unable to read profile catalog: {self.path}") from exc
        if not isinstance(value.get("profiles"), list):
            raise EmulatorError("Profile catalog has no profiles list.")
        for profile in value["profiles"]:
            self.validate(profile)
        return value

    @staticmethod
    def validate(profile: dict[str, Any]) -> None:
        required = {"profile_id", "architecture", "backend", "qemu", "memory", "storage", "filesystem", "artifacts"}
        missing = required - set(profile)
        if missing: raise EmulatorError(f"Profile is missing fields: {', '.join(sorted(missing))}")
        if profile["architecture"] not in ARCHITECTURES: raise EmulatorError(f"Unsupported profile architecture: {profile['architecture']}")
        if not NAME_PATTERN.fullmatch(str(profile["profile_id"])): raise EmulatorError("Invalid profile_id.")
        if profile["backend"] not in {"qemu", "qemu-board", "android-avd", "qemu-vmapple"}: raise EmulatorError(f"Unsupported profile backend: {profile['backend']}")
        qemu = profile["qemu"]
        if not isinstance(qemu, dict) or not qemu.get("executable") or not NAME_PATTERN.fullmatch(str(qemu.get("machine", ""))) or not NAME_PATTERN.fullmatch(str(qemu.get("cpu", ""))):
            raise EmulatorError(f"{profile['profile_id']}: invalid QEMU definition")
        memory = profile["memory"]; storage = profile["storage"]
        if not isinstance(memory, dict) or not isinstance(storage, dict): raise EmulatorError(f"{profile['profile_id']}: invalid memory/storage definition")
        minimum = validate_uint("profile minimum memory", memory.get("minimum_mb", 0), 1, 1048576)
        default = validate_uint("profile default memory", memory.get("default_mb", 0), minimum, 1048576)
        validate_uint("profile maximum memory", memory.get("maximum_mb", 0), default, 1048576)
        transport = storage.get("transport"); size = validate_uint("profile image size", storage.get("image_size_mb", 0), 1, 16777216)
        if profile["backend"] in {"qemu", "qemu-board"} and transport not in STORAGE_PROFILES - {"auto"}: raise EmulatorError(f"{profile['profile_id']}: unsupported storage transport")
        if storage.get("container_format") not in {"raw", "avd"}: raise EmulatorError(f"{profile['profile_id']}: unsupported container format")
        filesystem = profile["filesystem"]
        if not isinstance(filesystem, dict) or not filesystem.get("system") or not filesystem.get("boot"): raise EmulatorError(f"{profile['profile_id']}: invalid filesystem definition")
        if profile["filesystem"].get("system") == "ext4" and profile["backend"] == "qemu":
            if profile["storage"].get("container_format") != "raw": raise EmulatorError(f"{profile['profile_id']}: native ext4 images must be raw")
        display = profile.get("display", {})
        if not isinstance(display, dict) or not display.get("backend"): raise EmulatorError(f"{profile['profile_id']}: invalid display definition")
        if not isinstance(profile.get("communications", {}), dict): raise EmulatorError(f"{profile['profile_id']}: invalid communications definition")

    def profiles(self) -> list[dict[str, Any]]: return self.load()["profiles"]
    def get(self, profile_id: str) -> dict[str, Any]:
        for profile in self.profiles():
            if profile["profile_id"] == profile_id: return profile
        raise EmulatorError(f"Unknown profile '{profile_id}'.")

    def list(self) -> list[dict[str, Any]]:
        result = []
        for p in self.profiles():
            artifacts = self.artifact_status(p["profile_id"])["disk"]["status"] if p["backend"] == "qemu" else "external"
            result.append({"profile_id": p["profile_id"], "display_name": p["display_name"], "architecture": p["architecture"],
                           "backend": p["backend"], "status": p["status"], "artifact_status": artifacts,
                           "default_ram_mb": p["memory"]["default_mb"], "image_size_mb": p["storage"]["image_size_mb"], "native_creation": p["backend"] == "qemu"})
        return result

    def render(self, profile_id: str) -> dict[str, Any]:
        p = self.get(profile_id); q = p["qemu"]
        args = [q["executable"], "-name", f"omega-profile-{profile_id}", "-m", str(p["memory"]["default_mb"]), "-serial", "stdio"]
        if p["backend"] == "qemu":
            args += ["-M", q["machine"], "-cpu", q["cpu"], "-kernel", "<resolved-omega.elf>"]
            args += ["-vga", "std", "-display", "none"] if p["architecture"] == "x86_64" else ["-nographic"]
        elif p["backend"] == "android-avd": args += ["--avd", f"<{profile_id}>"]
        else: args += ["-M", q["machine"]]
        return {"profile_id": profile_id, "architecture": p["architecture"], "backend": p["backend"], "status": p["status"], "arguments": args, "filesystem": p["filesystem"]}

    def artifact_status(self, profile_id: str, dry_run: bool = True) -> dict[str, Any]:
        p = self.get(profile_id)
        if p["backend"] != "qemu": return {"profile_id": profile_id, "backend": p["backend"], "status": "external-artifact-required"}
        arch = p["architecture"]; kernel = build_root() / arch / "omega.elf"; image = image_root() / f"omega-{profile_id}-ext4.raw"; fallback = image_root() / f"omega-{arch}-bootable.img"
        selected = image if image.is_file() else fallback
        filesystem = "ext4" if selected == image else "fat32"
        disk_status = "verified" if image.is_file() else ("bootable-fallback" if fallback.is_file() else "stale")
        def checked_artifact(path: Path, kind: str) -> dict[str, Any]:
            sidecar = path.with_suffix(path.suffix + ".sha256")
            digest = sha256(path) if path.is_file() else ""
            expected = sidecar.read_text(encoding="utf-8").split()[0].lower() if sidecar.is_file() else ""
            status = "verified" if path.is_file() and (not expected or expected == digest) else ("digest-mismatch" if path.is_file() else "stale")
            return {"path": str(path), "kind": kind, "status": status, "sha256": digest, "expected_sha256": expected, "manifest": str(sidecar)}
        kernel_info = checked_artifact(kernel, "omega-kernel")
        disk_info = checked_artifact(selected, "omega-system-image")
        if disk_info["status"] == "digest-mismatch": disk_status = "digest-mismatch"
        return {"profile_id": profile_id, "backend": "qemu", "filesystem": filesystem,
                "kernel": kernel_info, "disk": {**disk_info, "filesystem": filesystem, "status": disk_status if disk_info["status"] != "stale" else disk_status,
                                                    "ext4_path": str(image), "fallback_path": str(fallback), "dry_run": dry_run}}


class QemuBackend:
    def __init__(self, manager: "OVDManager"): self.manager = manager

    @staticmethod
    def executable(arch: str) -> str: return "qemu-system-arm" if arch == "armv7" else f"qemu-system-{arch}"

    @classmethod
    def machine_help(cls, arch: str, timeout: float = 5.0) -> str:
        """Return QEMU's installed machine catalog without invoking a shell."""
        require_choice("architecture", arch, ARCHITECTURES)
        try:
            result = subprocess.run([cls.executable(arch), "-machine", "help"],
                                    check=True, capture_output=True, text=True,
                                    timeout=timeout, shell=False)
        except FileNotFoundError as exc:
            raise EmulatorError(f"QEMU executable not found: {cls.executable(arch)}") from exc
        except subprocess.CalledProcessError as exc:
            detail = (exc.stderr or exc.stdout or "QEMU returned an error").strip()
            raise EmulatorError(f"Unable to query QEMU machines for {arch}: {detail}") from exc
        except subprocess.TimeoutExpired as exc:
            raise EmulatorError(f"Timed out querying QEMU machines for {arch}.") from exc
        return result.stdout or result.stderr

    @classmethod
    def machines(cls, arch: str, timeout: float = 5.0) -> list[dict[str, str]]:
        machines: list[dict[str, str]] = []
        for line in cls.machine_help(arch, timeout).splitlines():
            stripped = line.strip()
            if not stripped or stripped.lower().startswith(("supported machines", "name", "qemu")):
                continue
            fields = stripped.split(None, 1)
            if fields and fields[0] not in {"-", "Machine"}:
                machines.append({"name": fields[0], "description": fields[1] if len(fields) > 1 else ""})
        return machines

    def command(self, ovd: OVD, *, gpu: str = "auto", storage: str | None = None, storage_image: Path | None = None,
                network: str | None = None, initrd: Path | None = None, readonly: bool = False,
                ephemeral: bool = False, qmp: bool = False, vnc: int | None = None,
                clipboard: bool = True, machine: str | None = None) -> list[str]:
        arch = ovd.arch; selected_storage = storage or ovd.storage; selected_network = network or config_value(ovd.config, "network", "none")
        require_choice("storage profile", selected_storage, STORAGE_PROFILES)
        require_choice("network profile", selected_network, NETWORK_PROFILES)
        image = storage_image or ovd.image; display = config_value(ovd.config, "display", ""); selected_machine = machine or ovd.machine
        board_limits = {"raspi4b": {"storage": {"sd", "none"}, "network": {"none"}, "gpu": False},
                        "raspi1ap": {"storage": {"sd", "none"}, "network": {"none"}, "gpu": False},
                        "raspi0": {"storage": {"sd", "none"}, "network": {"none"}, "gpu": False},
                        "bpim2u": {"storage": {"sd", "none"}, "network": {"none"}, "gpu": False},
                        "orangepi-pc": {"storage": {"sd", "none"}, "network": {"none"}, "gpu": False}}
        limits = board_limits.get(selected_machine)
        if limits and selected_storage not in limits["storage"]: raise EmulatorError(f"Machine '{selected_machine}' does not support storage profile '{selected_storage}'.")
        if limits and selected_network not in limits["network"]: raise EmulatorError(f"Machine '{selected_machine}' does not support network profile '{selected_network}'.")
        if limits and (gpu == "true" or vnc is not None) and not limits["gpu"]: raise EmulatorError(f"Machine '{selected_machine}' has no supported QEMU GPU/VNC path.")
        args: list[str]
        if arch == "x86_64": args = [self.executable(arch), "-name", f"omega-{ovd.name}", "-M", selected_machine, "-cpu", ovd.cpu, "-m", str(ovd.ram), "-kernel", str(self.manager.kernel(arch)), "-serial", "stdio", "-vga", "std", "-display", "none"]
        elif arch in {"aarch64", "armv7"}: args = [self.executable(arch), "-name", f"omega-{ovd.name}", "-M", selected_machine, "-cpu", ovd.cpu, "-m", str(ovd.ram), "-kernel", str(self.manager.kernel(arch)), "-nographic"]
        else: args = [self.executable(arch), "-name", f"omega-{ovd.name}", "-M", selected_machine, "-cpu", ovd.cpu, "-bios", "default", "-m", str(ovd.ram), "-kernel", str(self.manager.kernel(arch)), "-nographic"]
        if vnc is not None or gpu == "true" or (gpu == "auto" and display in {"standard-vga", "virtio-gpu"}):
            if arch == "x86_64": args[args.index("-display") + 1] = self.manager.display_backend()
            elif "-nographic" in args: args.remove("-nographic"); args += ["-device", "virtio-gpu-pci", "-display", self.manager.display_backend()]
        drive = f"file={image},format=raw,if=none,id=storage0" + (",readonly=on" if readonly else "")
        if selected_storage == "auto": args += ["-drive", f"file={image},format=raw,index=0,media=disk"]
        elif selected_storage == "virtio": args += ["-drive", drive, "-device", ("virtio-blk-pci,disable-modern=on" if arch == "x86_64" else "virtio-blk-device") + ",drive=storage0"]
        elif selected_storage == "ahci": args += ["-drive", f"file={image},format=raw,if=ide,index=0,media=disk"]
        elif selected_storage == "usb": args += ["-drive", drive, "-device", "usb-storage,drive=storage0"]
        elif selected_storage == "sd": args += ["-drive", f"file={image},format=raw,if=sd,index=0,media=disk"]
        elif selected_storage == "optical": args += ["-drive", f"file={image},format=raw,if=none,id=storage0,media=cdrom,readonly=on", "-device", "ide-cd,drive=storage0"]
        if selected_network != "none":
            device = "virtio-net-pci" if arch == "x86_64" else "virtio-net-device"
            if selected_network == "user":
                netdev = ["-netdev", "user,id=net0"]
            elif platform.system() == "Windows":
                netdev = ["-netdev", f"socket,id=net0,listen=127.0.0.1:{self._tcp_port(ovd, 'network.port')}"]
            else:
                netdev = ["-netdev", f"socket,id=net0,listen=unix:{ovd.state_path / 'network.sock'}"]
            args += netdev + ["-device", f"{device},netdev=net0"]
        if initrd: args += ["-initrd", str(initrd)]
        if ephemeral: args += ["-snapshot"]
        if qmp:
            endpoint = f"tcp:127.0.0.1:{self._tcp_port(ovd, 'qmp.port')}" if platform.system() == "Windows" else f"unix:{ovd.qmp_path}"
            args += ["-qmp", f"{endpoint},server=on,wait=off"]
        if vnc is not None:
            vnc = validate_uint("VNC display", vnc, 0, 99)
            args += ["-vnc", f"127.0.0.1:{vnc}"]
            if clipboard:
                args += ["-device", "qemu-xhci", "-device", "usb-tablet", "-device", "usb-kbd"]
        return args

    @staticmethod
    def _tcp_port(ovd: OVD, filename: str) -> int:
        path = ovd.state_path / filename
        try:
            port = int(path.read_text(encoding="utf-8"))
            if 1024 <= port <= 65535:
                return port
        except (OSError, ValueError):
            pass
        # A stable per-OVD port avoids Unix sockets on Windows and keeps dry
        # runs deterministic. QEMU reports a clear bind error if a user has
        # already occupied the selected port.
        digest = hashlib.sha256(f"{ovd.name}:{filename}".encode()).digest()
        port = 40000 + (int.from_bytes(digest[:2], "big") % 20000)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(str(port), encoding="utf-8")
        return port

    def run(self, ovd: OVD, args: list[str], daemon: bool = False) -> subprocess.Popen[bytes]:
        ovd.state_path.mkdir(parents=True, exist_ok=True)
        atomic_write(ovd.lifecycle_path, json.dumps({"state": "starting", "started_at": time.time()}, indent=2) + "\n")
        log = ovd.log_path.open("ab")
        try:
            process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, shell=False, start_new_session=True)
        finally:
            # The child owns the duplicated descriptor after Popen returns.
            log.close()
        identity = {"pid": process.pid, "executable": str(Path(args[0]).resolve()) if Path(args[0]).is_file() else args[0],
                    "command_sha256": hashlib.sha256(json.dumps(args, separators=(",", ":")).encode()).hexdigest(),
                    "started_at": time.time()}
        atomic_write(ovd.process_path, json.dumps(identity, indent=2) + "\n")
        atomic_write(ovd.pid_path, str(process.pid) + "\n")
        atomic_write(ovd.lifecycle_path, json.dumps({"state": "running", **identity}, indent=2) + "\n")
        return process

    @staticmethod
    def stop(ovd: OVD, force: bool = False) -> None:
        metadata: dict[str, Any] = {}
        if ovd.process_path.is_file():
            try: metadata = json.loads(ovd.process_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError): metadata = {}
        try: pid = int(metadata.get("pid", ovd.pid_path.read_text(encoding="utf-8")))
        except (OSError, TypeError, ValueError): pid = 0
        if pid:
            try:
                if not OVDManager.process_matches(ovd, pid):
                    raise EmulatorError("Refusing to stop a process that no longer matches this OVD.")
                # QEMU is launched in a new session. Terminating the session
                # prevents helper processes from surviving the OVD.
                if os.name != "nt":
                    os.killpg(pid, signal.SIGKILL if force else signal.SIGTERM)
                else:
                    os.kill(pid, signal.SIGKILL if force else signal.SIGTERM)
            except OSError: pass
        ovd.pid_path.unlink(missing_ok=True); ovd.process_path.unlink(missing_ok=True); ovd.qmp_path.unlink(missing_ok=True)
        ovd.lifecycle_path.unlink(missing_ok=True)


class OVDManager:
    """High‑level manager for Omega Virtual Devices.

    It encapsulates OVD lifecycle operations (create, start, stop, export,
    import) and provides a thin façade over the underlying :class:`QemuBackend`.
    The class is deliberately lightweight so that it can be instantiated from
    scripts, the CLI, or programmatic callers.
    """

    def __init__(self, root: Path | None = None):
        self.root = (root or root_from_environment()).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.catalog = ProfileCatalog(project_root())
        self.qemu = QemuBackend(self)

    def path(self, name: str) -> Path: return self.root / validate_name(name)
    def kernel(self, arch: str) -> Path: return build_root() / arch / "omega.elf"
    def kernel_status(self, arch: str) -> dict[str, Any]:
        require_choice("architecture", arch, ARCHITECTURES)
        path = self.kernel(arch)
        return {"architecture": arch, "path": str(path), "exists": path.is_file(), "size": path.stat().st_size if path.is_file() else 0,
                "modified": path.stat().st_mtime if path.is_file() else None}

    def build_kernel(self, arch: str, *, force: bool = False, timeout: float = 900.0) -> Path:
        """Configure and build one Omega kernel using direct CMake processes."""
        require_choice("architecture", arch, ARCHITECTURES)
        kernel = self.kernel(arch)
        if kernel.is_file() and not force:
            return kernel
        cmake = shutil.which("cmake")
        if not cmake:
            raise EmulatorError("CMake is required to build an Omega kernel.")
        toolchain = project_root() / "cmake" / f"{arch}-toolchain.cmake"
        if not toolchain.is_file():
            raise EmulatorError(f"Missing toolchain file: {toolchain}")
        build_dir = build_root() / arch
        commands = [
            [cmake, "-S", str(project_root()), "-B", str(build_dir), "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain), "-DARCH=" + arch],
            [cmake, "--build", str(build_dir)],
        ]
        for command in commands:
            try:
                result = subprocess.run(command, cwd=project_root(), check=False, capture_output=True, text=True, timeout=timeout, shell=False)
            except subprocess.TimeoutExpired as exc:
                raise EmulatorError(f"Timed out while building the {arch} Omega kernel.") from exc
            if result.returncode != 0:
                output = (result.stdout + "\n" + result.stderr).strip()
                raise EmulatorError(f"Kernel build failed for {arch}:\n{output[-4000:]}")
        if not kernel.is_file():
            raise EmulatorError(f"Build completed but kernel was not produced: {kernel}")
        return kernel

    def readiness(self, name: str) -> dict[str, Any]:
        ovd = self.validate(name, skip_image=True)
        image_exists = ovd.image.is_file()
        qemu_name = self.qemu.executable(ovd.arch)
        qemu_path = shutil.which(qemu_name)
        machine_available = None
        machine_error = ""
        if qemu_path:
            try:
                machine_available = ovd.machine in {item["name"] for item in self.qemu.machines(ovd.arch)}
            except EmulatorError as exc:
                machine_error = str(exc)
        checks = {
            "configuration": True,
            "kernel": self.kernel(ovd.arch).is_file(),
            "disk_image": image_exists,
            "qemu": bool(qemu_path),
            "machine": machine_available is True if machine_available is not None else False,
        }
        return {"name": name, "architecture": ovd.arch, "machine": ovd.machine, "kernel": str(self.kernel(ovd.arch)),
                "image": str(ovd.image), "qemu": qemu_path or qemu_name, "machine_error": machine_error,
                "checks": checks, "ready": all(checks.values())}
    def display_backend(self) -> str:
        if platform.system() == "Darwin": return "cocoa"
        if platform.system() == "Linux" and os.environ.get("DISPLAY"): return "sdl"
        return "none"

    def load(self, name: str) -> OVD:
        path = self.path(name)
        if not path.is_dir(): raise EmulatorError(f"OVD '{name}' does not exist.")
        return OVD(name, path, parse_config(path / "config.ini"))

    @staticmethod
    def process_matches(ovd: OVD, pid: int) -> bool:
        """Verify that a persisted PID still belongs to this OVD.

        The original implementation relied on reading ``/proc/<pid>/cmdline`` which
        is unavailable on macOS.  We now fall back to using ``psutil`` (if installed)
        to inspect the process command line, otherwise we simply verify that the PID
        exists and matches the stored metadata.
        """
        if pid <= 0:
            return False
        # Quick existence check – works on all POSIX platforms.
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        if not ovd.process_path.is_file():
            # Legacy state is accepted for inspection, but never provides the
            # strong identity guarantee of the new process record.
            return False
        try:
            metadata = json.loads(ovd.process_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return False
        if int(metadata.get("pid", -1)) != pid:
            return False
        # Platform‑specific command‑line verification.
        if psutil is not None:
            try:
                proc = psutil.Process(pid)
                cmdline = " ".join(proc.cmdline())
                expected = str(metadata.get("executable", ""))
                if expected and expected not in cmdline:
                    return False
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                return False
        else:
            # Fallback for systems without /proc (e.g., macOS) or psutil.
            proc_cmdline = Path(f"/proc/{pid}/cmdline")
            if proc_cmdline.is_file():
                try:
                    command = proc_cmdline.read_bytes().replace(b"\0", b" ").strip()
                    expected = str(metadata.get("executable", "")).encode()
                    if expected and expected not in command:
                        return False
                except OSError:
                    return False
        return True

    def validate(self, name: str, skip_image: bool = False) -> OVD:
        ovd = self.load(name); c = ovd.config
        if config_value(c, "name") != name: raise EmulatorError("Config name mismatch.")
        require_choice("architecture", ovd.arch, ARCHITECTURES); validate_uint("RAM", ovd.ram, 128, 1048576); validate_uint("Disk", ovd.disk, 1, 16777216)
        require_choice("storage profile", ovd.storage, STORAGE_PROFILES); require_choice("network profile", config_value(c, "network", "none"), NETWORK_PROFILES)
        display = config_value(c, "display"); display and require_choice("display profile", display, DISPLAY_PROFILES)
        if not NAME_PATTERN.fullmatch(ovd.cpu): raise EmulatorError(f"Invalid QEMU CPU '{ovd.cpu}'.")
        if config_value(c, "profile_id"):
            filesystem = config_value(c, "filesystem")
            if filesystem not in {"ext4", "fat32"}:
                raise EmulatorError("Profile-backed Omega images must use ext4 or an explicitly bootable FAT32 image.")
            if filesystem == "fat32" and config_value(c, "bootable", "false") != "true":
                raise EmulatorError("Profile-backed FAT32 images must be marked bootable.")
        image = ovd.image
        if not skip_image and not image.is_file(): raise EmulatorError(f"Storage image is missing: {image}")
        return ovd

    def create(self, name: str, arch: str = "x86_64", ram: int = 1024, disk: int = 64, storage: str = "virtio", network: str = "none", display: str | None = None, initrd: str = "", machine: str | None = None, boot_image: str | Path | None = None, allow_blank: bool = False, cpu: str | None = None) -> OVD:
        validate_name(name); require_choice("architecture", arch, ARCHITECTURES); validate_uint("RAM", ram, 128, 1048576); validate_uint("Disk", disk, 1, 16777216); require_choice("storage profile", storage, STORAGE_PROFILES); require_choice("network profile", network, NETWORK_PROFILES)
        display = display or ("standard-vga" if arch == "x86_64" else "simplefb"); require_choice("display profile", display, DISPLAY_PROFILES)
        machine = machine or ("q35" if arch == "x86_64" else "virt")
        cpu = cpu or {"x86_64": "max", "aarch64": "cortex-a57", "armv7": "cortex-a7", "riscv64": "rv64"}[arch]
        if not NAME_PATTERN.fullmatch(machine): raise EmulatorError(f"Invalid QEMU machine '{machine}'.")
        if not NAME_PATTERN.fullmatch(cpu): raise EmulatorError(f"Invalid QEMU CPU '{cpu}'.")
        if not NAME_PATTERN.fullmatch(cpu): raise EmulatorError(f"Invalid QEMU CPU '{cpu}'.")
        self._validate_machine_if_available(arch, machine)
        if initrd and not Path(initrd).is_file(): raise EmulatorError(f"Initrd not found: {initrd}")
        path = self.path(name)
        if path.exists(): raise EmulatorError(f"OVD '{name}' already exists.")
        target_size = disk * 1024 * 1024
        candidate = Path(boot_image).expanduser() if boot_image else image_root() / f"omega-{arch}-bootable.img"
        if boot_image and not candidate.is_file():
            raise EmulatorError(f"Boot image not found: {candidate}")
        if candidate.is_file() and candidate.stat().st_size > target_size and (boot_image or not allow_blank):
            raise EmulatorError(f"Boot image is larger than the requested {disk} MB disk: {candidate}")
        path.mkdir(parents=True); (path / "state").mkdir()
        values = {"ovd.schema": str(SCHEMA_VERSION), "ovd.name": name, "ovd.arch": arch, "ovd.machine": machine, "ovd.cpu": cpu, "ovd.ram_mb": str(ram), "ovd.disk_mb": str(disk), "ovd.storage.profile": storage, "ovd.storage.image": "userdata.img", "ovd.storage.format": "raw", "ovd.storage.readonly": "false", "ovd.display.profile": display, "ovd.network.profile": network, "ovd.initrd": initrd, "ovd.bootable": "false"}
        write_config(path / "config.ini", values)
        target = path / "userdata.img"
        if candidate.is_file() and candidate.stat().st_size <= target_size:
            with candidate.open("rb") as source, target.open("wb") as destination:
                shutil.copyfileobj(source, destination)
                destination.truncate(target_size)
            values["ovd.bootable"] = "true"
            values["ovd.boot.image"] = str(candidate.resolve())
            write_config(path / "config.ini", values)
        elif allow_blank:
            with target.open("wb") as stream: stream.truncate(target_size)
        else:
            raise EmulatorError(f"No bootable Omega image found for {arch}. Expected {candidate}; use allow_blank only for a non-bootable test disk.")
        return self.load(name)

    def create_from_profile(self, profile_id: str, name: str, ram: int | None = None, disk: int | None = None) -> OVD:
        p = self.catalog.get(profile_id)
        if p["backend"] != "qemu": raise EmulatorError(f"Profile backend '{p['backend']}' requires an external adapter or board-specific kernel.")
        image = image_root() / f"omega-{profile_id}-ext4.raw"
        if not image.is_file():
            # The bootable image generator is the portable source of the
            # architecture-specific fallback image. Never create a zero-filled
            # disk and claim that it is bootable.
            image = image_root() / f"omega-{p['architecture']}-bootable.img"
        if not image.is_file():
            ext4_path = image_root() / f"omega-{profile_id}-ext4.raw"
            bootable_path = image_root() / f"omega-{p['architecture']}-bootable.img"
            raise EmulatorError(f"No bootable Omega image found for profile '{profile_id}'. Expected {ext4_path} or {bootable_path}.")
        size = p["storage"]["image_size_mb"]
        if disk is not None and int(disk) != size: raise EmulatorError(f"Profile disk size must remain {size} MB.")
        if image.stat().st_size > size * 1024 * 1024:
            raise EmulatorError(f"Profile image is larger than its configured {size} MB disk: {image}")
        selected_machine = p["qemu"]["machine"]
        self._validate_machine_if_available(p["architecture"], selected_machine)
        profile_network = p.get("communications", {}).get("network", [{}])[0].get("mode", "none")
        if profile_network not in NETWORK_PROFILES: profile_network = "none"
        self.create(name, p["architecture"], ram or p["memory"]["default_mb"], size, p["storage"]["transport"], profile_network, p["display"]["backend"], machine=selected_machine, cpu=p["qemu"].get("cpu"))
        ovd = self.load(name)
        target_name = "system.ext4" if image.name.endswith("-ext4.raw") else "bootable.img"
        copy_file_atomic(image, ovd.path / target_name)
        with (ovd.path / target_name).open("ab") as target:
            target.truncate(size * 1024 * 1024)
        ovd.config.update({"ovd.storage.image": target_name, "ovd.filesystem.system": "ext4" if target_name == "system.ext4" else "fat32", "ovd.filesystem.boot": "fat32", "ovd.profile.id": profile_id, "ovd.artifacts.policy": "build-if-stale", "ovd.artifacts.source": str(image)})
        write_config(ovd.path / "config.ini", ovd.config)
        return self.load(name)

    @staticmethod
    def _validate_machine_if_available(arch: str, machine: str) -> None:
        try:
            available = {item["name"] for item in QemuBackend.machines(arch)}
        except EmulatorError:
            return
        if available and machine not in available:
            raise EmulatorError(f"QEMU machine '{machine}' is not available for {arch}.")

    def list(self) -> list[dict[str, Any]]:
        result = []
        for path in sorted(self.root.iterdir()):
            if not (path / "config.ini").is_file(): continue
            try: ovd = self.validate(path.name, skip_image=True)
            except EmulatorError: continue
            result.append({"name": ovd.name, "arch": ovd.arch, "ram_mb": ovd.ram, "disk_mb": ovd.disk, "storage": ovd.storage, "profile": config_value(ovd.config, "profile_id"), "state": "running" if self.running(ovd) else "stopped"})
        return result

    @staticmethod
    def running(ovd: OVD) -> bool:
        if not ovd.pid_path.is_file(): return False
        try: pid = int(ovd.pid_path.read_text(encoding="utf-8"))
        except (OSError, ValueError): return False
        return OVDManager.process_matches(ovd, pid)

    def start(self, name: str, **options: Any) -> list[str]:
        ovd = self.validate(name, skip_image=bool(options.get("storage_image")))
        if self.running(ovd): raise EmulatorError(f"OVD '{name}' is already running.")
        if not options.get("dry_run"):
            if not self.kernel(ovd.arch).is_file(): raise EmulatorError(f"Omega kernel is missing: {self.kernel(ovd.arch)}")
            if not shutil.which(self.qemu.executable(ovd.arch)): raise EmulatorError(f"QEMU executable not found: {self.qemu.executable(ovd.arch)}")
            if options.get("storage_image") is None and not ovd.image.is_file(): raise EmulatorError(f"Storage image is missing: {ovd.image}")
        if options.get("storage") is not None: require_choice("storage profile", options["storage"], STORAGE_PROFILES)
        if options.get("network") is not None: require_choice("network profile", options["network"], NETWORK_PROFILES)
        if options.get("machine") is not None:
            if not NAME_PATTERN.fullmatch(options["machine"]): raise EmulatorError(f"Invalid QEMU machine '{options['machine']}'.")
            self._validate_machine_if_available(ovd.arch, options["machine"])
        if options.get("storage_image") is not None and not Path(options["storage_image"]).is_file():
            raise EmulatorError(f"Storage image not found: {options['storage_image']}")
        if options.get("initrd") is not None and not Path(options["initrd"]).is_file():
            raise EmulatorError(f"Initrd not found: {options['initrd']}")
        command_options = {key: value for key, value in options.items() if key not in {"dry_run", "daemon"}}
        args = self.qemu.command(ovd, **command_options); atomic_write(ovd.command_path, json.dumps(args, indent=2) + "\n")
        if options.get("dry_run"): return args
        self.qemu.run(ovd, args, bool(options.get("daemon"))); return args

    def stop(self, name: str, force: bool = False) -> None: self.qemu.stop(self.load(name), force)
    def delete(self, name: str, force: bool = False) -> None:
        ovd = self.load(name)
        if self.running(ovd) and not force: raise EmulatorError("OVD is running; stop it or use force.")
        if self.running(ovd): self.stop(name, True)
        shutil.rmtree(ovd.path)
    def clone(self, source: str, new_name: str) -> OVD:
        src = self.validate(source); dst = self.path(new_name)
        if dst.exists(): raise EmulatorError(f"Destination OVD '{new_name}' already exists.")
        shutil.copytree(src.path, dst, ignore=shutil.ignore_patterns("state")); (dst / "state").mkdir()
        cfg = parse_config(dst / "config.ini"); cfg["ovd.name"] = validate_name(new_name); write_config(dst / "config.ini", cfg); return self.load(new_name)
    def export(self, name: str, archive: Path) -> None:
        ovd = self.validate(name); archive = archive.expanduser().resolve(); archive.parent.mkdir(parents=True, exist_ok=True)
        runtime_names = {"qemu.pid", "process.json", "lifecycle.json", "qmp.sock", "network.sock", "qmp.port", "network.port"}
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(ovd.path, arcname=name, recursive=True, filter=lambda info: None if Path(info.name).name in runtime_names else info)
    def import_archive(self, archive: Path, name: str) -> OVD:
        validate_name(name); destination = self.path(name)
        if destination.exists(): raise EmulatorError("Destination OVD already exists.")
        with tempfile.TemporaryDirectory(prefix="omega-ovd-import-") as temp:
            temp_path = Path(temp); root_names: set[str] = set()
            with tarfile.open(archive, "r:gz") as tar:
                for member in tar.getmembers():
                    parts = Path(member.name).parts
                    if not parts or any(part in {"", ".", ".."} for part in parts) or Path(member.name).is_absolute(): raise EmulatorError("Archive contains an unsafe path.")
                    if member.issym() or member.islnk():
                        raise EmulatorError("Archive links are not allowed.")
                    root_names.add(parts[0])
                if len(root_names) != 1: raise EmulatorError("Archive must contain one OVD root.")
                # Avoid TarFile.extractall(filter=...), which is unavailable on
                # Python 3.10/3.11 even though those versions are supported.
                for member in tar.getmembers():
                    target = (temp_path / member.name).resolve()
                    target.relative_to(temp_path.resolve())
                    if member.isdir(): target.mkdir(parents=True, exist_ok=True)
                    elif member.isfile():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        source = tar.extractfile(member)
                        if source is None: raise EmulatorError("Archive contains an unreadable file.")
                        with target.open("wb") as output: shutil.copyfileobj(source, output)
                    else: raise EmulatorError("Archive contains an unsupported member type.")
            source = temp_path / next(iter(root_names))
            if not (source / "config.ini").is_file(): raise EmulatorError("Archive does not contain config.ini.")
            cfg = parse_config(source / "config.ini"); cfg["ovd.name"] = name; write_config(source / "config.ini", cfg)
            candidate = destination.with_name(f".{destination.name}.import")
            shutil.rmtree(candidate, ignore_errors=True); shutil.move(str(source), str(candidate))
            try:
                imported = OVD(name, candidate, parse_config(candidate / "config.ini"))
                self.validate_at(imported)
                candidate.replace(destination)
            except Exception:
                shutil.rmtree(candidate, ignore_errors=True); raise
        return self.validate(name)

    def validate_at(self, ovd: OVD, skip_image: bool = False) -> OVD:
        """Validate an OVD object at a temporary path before publishing it."""
        if config_value(ovd.config, "name") not in {ovd.name, ""}:
            raise EmulatorError("Config name mismatch.")
        require_choice("architecture", ovd.arch, ARCHITECTURES)
        validate_uint("RAM", ovd.ram, 128, 1048576); validate_uint("Disk", ovd.disk, 1, 16777216)
        require_choice("storage profile", ovd.storage, STORAGE_PROFILES)
        if not skip_image and not ovd.image.is_file(): raise EmulatorError(f"Storage image is missing: {ovd.image}")
        return ovd
    def snapshot(self, action: str, name: str, snapshot: str | None = None) -> list[str] | None:
        ovd = self.validate(name); directory = ovd.state_path / "snapshots"; directory.mkdir(parents=True, exist_ok=True)
        if action == "create":
            if not snapshot or not NAME_PATTERN.fullmatch(snapshot): raise EmulatorError("Invalid snapshot name.")
            copy_file_atomic(ovd.image, directory / f"{snapshot}.img")
        elif action == "apply":
            if not snapshot: raise EmulatorError("Snapshot name is required.")
            copy_file_atomic(directory / f"{snapshot}.img", ovd.image)
        elif action == "list": return sorted(p.stem for p in directory.glob("*.img"))
        else: raise EmulatorError(f"Unknown snapshot action '{action}'.")
        return None
