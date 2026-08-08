"""Executable launcher entry point; use ``python -m emulator.ovd_run run ...``."""

from __future__ import annotations

import sys

try:
    from .ovd_cli import main
except ImportError:
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from emulator.ovd_cli import main


if __name__ == "__main__":
    arguments = sys.argv[1:]
    if not arguments or arguments[0] != "run":
        arguments.insert(0, "start")
    else:
        arguments[0] = "start"
    sys.exit(main(arguments))
