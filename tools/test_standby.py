"""Run standby and firmware C behavior tests (use Linux/WSL on Windows).

First build tinypico_test to install Unity into .pio/libdeps/tinypico_test.
Then run `python3 tools/test_standby.py` with a native C compiler installed.
"""

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
        for name in ("app_config", "power", "bluetooth", "audio", "accelerometer", "status_led", "calibration", "impact")
    ]
    unity = ROOT / ".pio/libdeps/tinypico_test/Unity/src"
    includes.append(unity)
    suites = {
        "power": [TEST / "test_power.c", ROOT / "components/power/power_button.c"],
        "power_disabled": [TEST / "test_power.c", ROOT / "components/power/power_button.c"],
        "accelerometer": [TEST / "test_accelerometer.c"],
        "bluetooth": [TEST / "test_bluetooth.c", ROOT / "components/audio/audio.c",
                      ROOT / "components/bluetooth/bluetooth_audio_policy.c"],
        "button": [TEST / "test_button.c", ROOT / "components/power/power_button.c",
                   ROOT / "components/bluetooth/bluetooth_audio_policy.c"],
        "unity": [TEST / "unity_runner.c", unity / "unity.c",
                  ROOT / "test/test_calibration_protocol/test_calibration_protocol.c",
                  ROOT / "components/calibration/calibration_protocol.c",
                  ROOT / "components/calibration/calibration_playback.c",
                  ROOT / "components/impact/impact.c", ROOT / "components/audio/audio.c",
                  ROOT / "components/audio/impact_pcm.S", ROOT / "components/power/power_button.c",
                  ROOT / "components/bluetooth/bluetooth_audio_policy.c"],
    }
    for name, sources in suites.items():
        executable = BUILD / (name + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror"]
        if name in ("power", "power_disabled"):
            command += ["-DAPP_FEATURE_SLEEP_SWITCH=" + ("0" if name == "power_disabled" else "1")]
        if name == "unity":
            asset = (ROOT / "components/audio/impact.pcm").as_posix()
            command += ['-DPCM_ASSET_PATH="' + asset + '"']
            if os.name != "nt":
                command += ["-Wl,-z,noexecstack"]
        command += ["-I" + str(path) for path in includes]
        command += [str(path) for path in sources] + ["-o", str(executable)]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
