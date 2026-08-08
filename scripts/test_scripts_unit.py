#!/usr/bin/env python3
"""Python unit-test entry point for emulator and script-facing contracts."""

from __future__ import annotations

import unittest
from pathlib import Path
import sys


# Make direct execution independent of the caller's current directory.
PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromNames([
        "emulator.test_ovd_unit",
        "emulator.test_profile_catalog",
        "emulator.test_profile_ext4_integration",
        "emulator.test_vnc",
        "emulator.test_gui_module",
    ])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
