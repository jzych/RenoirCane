# Android Serial Startup Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start the calibration APK on Android without importing desktop serial-port enumeration.

**Architecture:** `main.py` will define two lazy backend factories and pass them to the existing platform selector. Importing the Android app selects and imports only the Android USB backend; the Windows backend remains available to the desktop application.

**Tech Stack:** Python 3.14, Kivy, pytest, Buildozer, Android Debug Bridge.

## Global Constraints

- Keep `WindowsSerialBackend` behavior unchanged for desktop calibration.
- On Android, do not import `app.serial_backends.windows_serial` during application startup.
- Preserve existing Android USB permission and connection behavior.
- Verify the packaged APK has no Python startup traceback on the connected device.

---

### Task 1: Isolate Desktop Backend Import

**Files:**
- Modify: `calibration_app/main.py:1-28`
- Modify: `calibration_app/tests/test_main.py:1-55`

**Interfaces:**
- Consumes: `make_serial_backend(platform_name, android_factory, windows_factory)` from `app.platform`.
- Produces: `_make_android_backend() -> AndroidUsbSerialBackend` and `_make_windows_backend() -> WindowsSerialBackend` for `CalibrationRoot`.

- [ ] **Step 1: Write the failing test**

```python
def test_main_imports_without_windows_serial_module(self):
    script = "import sys; sys.modules['app.serial_backends.windows_serial'] = None; import main"
    result = subprocess.run(
        [sys.executable, "-c", script],
        cwd=Path(__file__).parents[1],
        capture_output=True,
        text=True,
    )
    self.assertEqual(result.returncode, 0, result.stderr)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest calibration_app/tests/test_main.py::CalibrationAppTests::test_main_imports_without_windows_serial_module -v`

Expected: FAIL because the module-level `WindowsSerialBackend` import is blocked.

- [ ] **Step 3: Write minimal implementation**

```python
def _make_android_backend() -> AndroidUsbSerialBackend:
    from app.serial_backends.android_usb_serial import AndroidUsbSerialBackend
    return AndroidUsbSerialBackend()


def _make_windows_backend() -> WindowsSerialBackend:
    from app.serial_backends.windows_serial import WindowsSerialBackend
    return WindowsSerialBackend()
```

Remove the module-level backend imports and pass these factories to `make_serial_backend`.

- [ ] **Step 4: Run test suite to verify it passes**

Run: `python -m pytest calibration_app/tests -v`

Expected: PASS with the new startup-isolation test and all existing calibration tests.

- [ ] **Step 5: Build and validate on Android**

Run: `buildozer android debug`, install the generated APK with `adb install -r`, launch `org.renoircane.canecalibration/org.kivy.android.PythonActivity`, and inspect `adb logcat -s python`.

Expected: the app remains running after launch and the Python log contains no traceback or `list_ports_posix` import error.

- [ ] **Step 6: Commit**

```bash
git add calibration_app/main.py calibration_app/tests/test_main.py
git commit -m "fix: isolate desktop serial backend on Android"
```

## Self-Review

- Spec coverage: Task 1 implements lazy platform imports, leaves USB error handling untouched, tests the blocked Windows import, runs the full suite, and validates the actual APK on Android.
- Placeholder scan: no placeholders or deferred implementation instructions remain.
- Type consistency: the factories conform to the existing zero-argument callable contract consumed by `make_serial_backend`.
