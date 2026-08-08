from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from .ovd_core import EmulatorError, OVDManager, QemuBackend


class OVDManagerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.manager = OVDManager(Path(self.temp.name) / "ovd")

    def tearDown(self): self.temp.cleanup()

    def test_create_validate_list_and_clone(self):
        self.manager.create("test-device", "x86_64", 512, 4, "virtio", "none", allow_blank=True)
        self.assertEqual(self.manager.list()[0]["name"], "test-device")
        self.manager.validate("test-device")
        self.manager.clone("test-device", "clone-device")
        self.assertEqual(len(self.manager.list()), 2)

    def test_rejects_unsafe_names_and_paths(self):
        with self.assertRaises(EmulatorError): self.manager.create("../unsafe")
        self.manager.create("safe-device")
        config = self.manager.load("safe-device").config
        config["ovd.storage.image"] = "../outside.img"
        from .ovd_core import write_config
        write_config(self.manager.path("safe-device") / "config.ini", config)
        with self.assertRaises(EmulatorError): self.manager.validate("safe-device", skip_image=True)

    def test_dry_run_is_argument_vector(self):
        self.manager.create("dry-run")
        command = self.manager.start("dry-run", dry_run=True)
        self.assertEqual(command[0], "qemu-system-x86_64")
        self.assertNotIn("bash", command)
        self.assertTrue(self.manager.load("dry-run").command_path.is_file())

    def test_machine_selection_and_vnc_input_devices(self):
        self.manager.create("interactive", arch="x86_64", machine="q35")
        command = self.manager.start("interactive", machine="q35", vnc=2, clipboard=True, dry_run=True)
        self.assertEqual(command[command.index("-M") + 1], "q35")
        self.assertEqual(command[command.index("-vnc") + 1], "127.0.0.1:2")
        self.assertIn("usb-tablet", command)
        self.assertIn("usb-kbd", command)

    def test_qemu_machine_catalog_is_argument_based(self):
        original = QemuBackend.machine_help
        try:
            QemuBackend.machine_help = classmethod(lambda cls, arch, timeout=5.0: "Supported machines are:\nq35 Q35 chipset\npc i440FX\n")
            machines = QemuBackend.machines("x86_64")
        finally:
            QemuBackend.machine_help = original
        self.assertEqual([item["name"] for item in machines], ["q35", "pc"])

    def test_readiness_reports_kernel_image_qemu_and_machine_checks(self):
        self.manager.create("ready-check", disk=64)
        readiness = self.manager.readiness("ready-check")
        self.assertEqual(readiness["machine"], "q35")
        self.assertIn("kernel", readiness["checks"])
        self.assertIn("disk_image", readiness["checks"])

    def test_kernel_build_uses_direct_cmake_processes(self):
        with tempfile.TemporaryDirectory() as build_root:
            calls = []

            def fake_run(command, **kwargs):
                calls.append((command, kwargs))
                if "--build" in command:
                    output = Path(build_root) / "x86_64" / "omega.elf"
                    output.parent.mkdir(parents=True, exist_ok=True); output.write_bytes(b"kernel")
                return __import__("subprocess").CompletedProcess(command, 0, "", "")

            with patch.dict(os.environ, {"OMEGA_BUILD_ROOT": build_root}), patch("shutil.which", return_value="cmake"), patch("subprocess.run", side_effect=fake_run):
                result = self.manager.build_kernel("x86_64", force=True)
            self.assertTrue(result.is_file())
            self.assertEqual(len(calls), 2)
            self.assertTrue(all(call[1]["shell"] is False for call in calls))

    def test_snapshot_and_archive(self):
        self.manager.create("archive-device", disk=2, allow_blank=True)
        self.manager.snapshot("create", "archive-device", "baseline")
        self.assertEqual(self.manager.snapshot("list", "archive-device"), ["baseline"])
        archive = Path(self.temp.name) / "device.tar.gz"
        self.manager.export("archive-device", archive)
        self.manager.import_archive(archive, "imported-device")
        self.assertTrue(self.manager.load("imported-device").image.is_file())

    def test_process_identity_record_is_written_and_mismatches_are_rejected(self):
        ovd = self.manager.create("identity-device", disk=1, allow_blank=True)
        process = type("FakeProcess", (), {"pid": 999999})()
        with patch("emulator.ovd_core.subprocess.Popen", return_value=process):
            self.manager.qemu.run(ovd, ["qemu-system-x86_64", "-name", "omega-identity-device"])
        self.assertTrue(ovd.process_path.is_file())
        self.assertEqual(__import__("json").loads(ovd.process_path.read_text())["pid"], 999999)
        self.assertFalse(self.manager.running(ovd))

    def test_start_requires_runtime_artifacts_for_real_launch(self):
        self.manager.create("runtime-check", disk=1, allow_blank=True)
        with patch.object(self.manager, "kernel", return_value=Path(self.temp.name) / "missing.elf"):
            with self.assertRaises(EmulatorError):
                self.manager.start("runtime-check")


if __name__ == "__main__": unittest.main()
