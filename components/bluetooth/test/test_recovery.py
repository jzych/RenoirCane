"""Native Bluetooth fault injection: python3 components/bluetooth/test/test_recovery.py."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / ".pio" / "host_power"
BUILD.mkdir(parents=True, exist_ok=True)
compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
if compiler is None:
    raise SystemExit("A native C compiler is required (use WSL on Windows)")
includes = [ROOT / "test/host_power/stubs"] + [
    ROOT / "components" / name / "include" for name in ("bluetooth", "audio", "app_config")
]
executable = BUILD / "bluetooth_recovery"
subprocess.run([
    compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
    *["-I" + str(path) for path in includes],
    str(ROOT / "components/bluetooth/test/test_recovery.c"),
    str(ROOT / "components/bluetooth/bluetooth_audio_policy.c"),
    str(ROOT / "components/audio/audio.c"), "-o", str(executable),
], check=True)
subprocess.run([str(executable)], check=True)
