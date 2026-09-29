"""Development helper: build, flash, test and monitor the cane firmware.

    python tools/dev.py build [ENV ...]        build firmware envs (default: all, including the Unity test image)
    python tools/dev.py flash [ENV]            upload ENV (default: tinypico)
    python tools/dev.py test device [-f NAME]  run Unity tests on the device, then reflash tinypico
    python tools/dev.py test host [SUITE]      run host pytest suites: audio, app (default: both)
    python tools/dev.py monitor [--send CMD]   reset the device and print its log
    python tools/dev.py verify                 build all, host tests, device tests, flash tinypico

The serial port defaults to $RENOIR_PORT or COM4 and can be set with --port.
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_DIR = ROOT / "calibration_app"
APP_PYTHON = APP_DIR / ".venv" / "Scripts" / "python.exe"
PYTEST_TMP = ROOT / ".pio" / "pytest-tmp"
FIRMWARE_ENVS = ("tinypico", "tinypico_calibration")
TEST_ENV = "tinypico_test"
ALL_ENVS = FIRMWARE_ENVS + (TEST_ENV,)
TRANSIENT_ERRORS = ("WinError 32", "WinError 145", "SCons.Tool.FortranCommon")
BUILD_ATTEMPTS = 3


class StepFailed(Exception):
    pass


def run(cmd, cwd=ROOT, retry_transient=False):
    attempts = BUILD_ATTEMPTS if retry_transient else 1
    for attempt in range(1, attempts + 1):
        print(f"[dev] $ {' '.join(str(part) for part in cmd)}", flush=True)
        process = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        output = []
        for line in process.stdout:
            sys.stdout.write(line)
            output.append(line)
        if process.wait() == 0:
            return
        text = "".join(output)
        if attempt < attempts and any(error in text for error in TRANSIENT_ERRORS):
            print(f"[dev] locked build files, retrying ({attempt + 1}/{attempts})", flush=True)
            time.sleep(2)
            continue
        raise StepFailed(f"exit code {process.returncode}: {' '.join(str(part) for part in cmd)}")


def pio(*args, retry_transient=False):
    run([sys.executable, "-m", "platformio", *args], retry_transient=retry_transient)


def build(envs):
    for env in envs or ALL_ENVS:
        if env == TEST_ENV:
            pio("test", "-e", TEST_ENV, "--without-uploading", "--without-testing", retry_transient=True)
        else:
            pio("run", "-e", env, retry_transient=True)


def flash(env, port):
    pio("run", "-e", env, "-t", "upload", "--upload-port", port, retry_transient=True)


def test_device(port, filters, reflash):
    try:
        args = ["test", "-e", TEST_ENV, "--upload-port", port, "--test-port", port]
        for name in filters:
            args += ["-f", name]
        pio(*args, retry_transient=True)
    finally:
        if reflash:
            print("[dev] restoring tinypico firmware", flush=True)
            flash("tinypico", port)


def test_host(suites):
    PYTEST_TMP.mkdir(parents=True, exist_ok=True)
    for suite in suites or ("audio", "app"):
        if suite == "audio":
            run([sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider",
                 f"--basetemp={PYTEST_TMP / 'audio'}", "test/test_audio_asset"])
        elif not APP_PYTHON.exists():
            raise StepFailed(f"{APP_PYTHON} missing; create it with Python 3.13 (see .tasks/database.md)")
        else:
            run([APP_PYTHON, "-m", "pytest", "-q", "-p", "no:cacheprovider",
                 f"--basetemp={PYTEST_TMP / 'app'}"], cwd=APP_DIR)


def monitor(port, baud, seconds, send, after, reset):
    import serial

    with serial.Serial(port, baud, timeout=0.05) as device:
        if reset:
            device.dtr = False
            device.rts = True
            time.sleep(0.1)
            device.rts = False
        start = time.time()
        seen = ""
        sent = send is None
        while seconds <= 0 or time.time() - start < seconds:
            chunk = device.read(4096).decode("utf-8", "replace")
            if chunk:
                sys.stdout.write(chunk)
                sys.stdout.flush()
                seen = (seen + chunk)[-4096:]
            if not sent and (after is None or after in seen):
                device.write(send.encode() + b"\r\n")
                print(f"\n[dev] sent '{send}' at {time.time() - start:.2f}s", flush=True)
                sent = True


def verify(port):
    build(ALL_ENVS)
    test_host(())
    test_device(port, (), reflash=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=os.environ.get("RENOIR_PORT", "COM4"))
    commands = parser.add_subparsers(dest="command", required=True)

    build_cmd = commands.add_parser("build")
    build_cmd.add_argument("envs", nargs="*", choices=ALL_ENVS + ("all",), metavar="ENV")

    flash_cmd = commands.add_parser("flash")
    flash_cmd.add_argument("env", nargs="?", default="tinypico", choices=FIRMWARE_ENVS)

    test_cmd = commands.add_parser("test")
    test_targets = test_cmd.add_subparsers(dest="target", required=True)
    device_cmd = test_targets.add_parser("device")
    device_cmd.add_argument("-f", "--filter", action="append", default=[], help="Unity test folder, e.g. test_calibration_protocol")
    device_cmd.add_argument("--no-reflash", action="store_true", help="leave the test image on the device")
    host_cmd = test_targets.add_parser("host")
    host_cmd.add_argument("suites", nargs="*", choices=("audio", "app"), metavar="SUITE")

    monitor_cmd = commands.add_parser("monitor")
    monitor_cmd.add_argument("--seconds", type=float, default=20.0, help="0 = until Ctrl+C")
    monitor_cmd.add_argument("--baud", type=int, default=115200, help="921600 for tinypico_calibration")
    monitor_cmd.add_argument("--send", help="console command to send, e.g. 'bt reconnect'")
    monitor_cmd.add_argument("--after", help="send only once this text appears in the log")
    monitor_cmd.add_argument("--no-reset", action="store_true")

    commands.add_parser("verify")

    args = parser.parse_args()
    try:
        if args.command == "build":
            build(() if "all" in args.envs else args.envs)
        elif args.command == "flash":
            flash(args.env, args.port)
        elif args.command == "test" and args.target == "device":
            test_device(args.port, args.filter, not args.no_reflash)
        elif args.command == "test":
            test_host(args.suites)
        elif args.command == "monitor":
            monitor(args.port, args.baud, args.seconds, args.send, args.after, not args.no_reset)
        else:
            verify(args.port)
    except StepFailed as error:
        print(f"[dev] FAILED: {error}", flush=True)
        return 1
    except KeyboardInterrupt:
        return 130
    print(f"[dev] OK: {args.command}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
