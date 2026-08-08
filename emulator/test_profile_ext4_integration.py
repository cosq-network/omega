from __future__ import annotations

import unittest

from .ovd_core import ProfileCatalog


class ProfileExt4Tests(unittest.TestCase):
    def test_native_profiles_require_ext4(self):
        for profile in ProfileCatalog().profiles():
            if profile["backend"] == "qemu":
                self.assertEqual(profile["filesystem"]["system"], "ext4")
                self.assertEqual(profile["storage"]["container_format"], "raw")


if __name__ == "__main__": unittest.main()
