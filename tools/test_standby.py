"""Compile and run standby C behavior tests using an installed host compiler."""

import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / "test" / "host_power"
BUILD = ROOT / ".pio" / "host_power"


def main():
    compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if compiler is None:
        raise SystemExit("A native C compiler (clang, cc, or gcc) is required")
    BUILD.mkdir(parents=True, exist_ok=True)
    includes = [TEST / "stubs"] + [
        ROOT / "components" / name / "include"
        for name in ("app_config", "power", "bluetooth")
    ]
    suites = {
        "button": [TEST / "test_button.c", ROOT / "components/power/power_button.c",
                   ROOT / "components/bluetooth/bluetooth_audio_policy.c"],
    }
    for name, sources in suites.items():
        executable = BUILD / (name + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror"]
        command += ["-I" + str(path) for path in includes]
        command += [str(path) for path in sources] + ["-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
