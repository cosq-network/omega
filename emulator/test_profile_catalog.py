from __future__ import annotations

import unittest

from .ovd_core import ProfileCatalog


class ProfileCatalogTests(unittest.TestCase):
    def setUp(self): self.catalog = ProfileCatalog()
    def test_catalog_validates(self):
        profiles = self.catalog.profiles()
        self.assertGreaterEqual(len(profiles), 12)
        ids = {profile["profile_id"] for profile in profiles}
        self.assertTrue({"aarch64-raspi4b-qemu", "armv7-raspi1ap-qemu", "armv7-raspi0-qemu", "armv7-bananapi-m2u-qemu", "armv7-orangepi-pc-qemu"} <= ids)
        self.assertEqual(self.catalog.get("aarch64-raspi4b-qemu")["backend"], "qemu")
        self.assertEqual(self.catalog.get("armv7-orangepi-pc-qemu")["backend"], "qemu-board")
    def test_render_is_deterministic(self):
        first = self.catalog.render("riscv64-virt-minimal")
        second = self.catalog.render("riscv64-virt-minimal")
        self.assertEqual(first, second)
        self.assertEqual(first["arguments"][0], "qemu-system-riscv64")


if __name__ == "__main__": unittest.main()
