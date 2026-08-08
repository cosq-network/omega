"""Executable manager entry point; use ``python -m emulator.ovd_manager``."""

try:
    from .ovd_cli import main
except ImportError:
    from pathlib import Path
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from emulator.ovd_cli import main

if __name__ == "__main__":
    import sys
    sys.exit(main())
