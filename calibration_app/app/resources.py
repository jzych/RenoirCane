from pathlib import Path
import sys


def asset_path(name: str) -> Path:
    """Return an application asset path in source and PyInstaller builds."""
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent))
    return root / name
