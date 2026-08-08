#!/usr/bin/env python3
"""Backward-compatible profile catalog entry point.

The implementation lives in :mod:`emulator.ovd_core`; this file remains as a
small Python-only entry point for existing documentation and integrations.
"""

from __future__ import annotations

import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

try:
    from .ovd_cli import main
except ImportError:  # Direct ``python emulator/profile_catalog.py`` execution.
    from emulator.ovd_cli import main


if __name__ == "__main__":
    sys.exit(main(["profiles", *sys.argv[1:]]))
