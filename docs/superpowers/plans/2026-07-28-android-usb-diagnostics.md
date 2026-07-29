# Android USB Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit bounded Android logcat diagnostics that locate the first missing or invalid calibration USB boundary.

**Architecture:** Add a small `app.diagnostics` adapter that writes `CaneDiag` Android log entries through `android.util.Log` when PyJNIus is available and falls back to Python logging in unit tests. The Android port and reader call the adapter for port configuration, non-empty reads, writes, packet decoding, and parser corruption changes.

**Tech Stack:** Python 3, PyJNIus, Android logcat, pytest, Buildozer.

## Global Constraints

- Android entries must use the logcat tag `CaneDiag`.
- Log only non-empty reads; cap hexadecimal previews at 32 bytes.
- Do not alter reset behavior, DTR/RTS, protocol framing, UI, firmware, or retry logic.
- Test on Samsung SM-A037G with the TinyPICO running `tinypico_calibration` firmware.

---

### Task 1: Provide a portable Android diagnostic adapter

**Files:**

- Create: `calibration_app/app/diagnostics.py`
- Create: `calibration_app/tests/test_diagnostics.py`

**Interfaces:**

- Produces: `diagnose(message: str) -> None`, which emits `CaneDiag` Android INFO entries when Android classes are available.
- Consumes: a textual diagnostic message supplied by serial components.

- [ ] **Step 1: Write the failing adapter test**

Inject a fake Android logger into the adapter and assert tag and message:

```python
def test_diagnose_emits_the_canediag_android_log_tag(monkeypatch):
    recorded = []
    monkeypatch.setattr(diagnostics, "_android_info", lambda tag, message: recorded.append((tag, message)))

    diagnostics.diagnose("USB read bytes=12")

    assert recorded == [("CaneDiag", "USB read bytes=12")]
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_diagnostics.py::test_diagnose_emits_the_canediag_android_log_tag -q'
```

Expected: FAIL because `app.diagnostics` does not exist.

- [ ] **Step 3: Implement the minimal adapter**

Implement `_android_info(tag, message)` with `jnius.autoclass("android.util.Log").i(tag, message)`. `diagnose()` must call it with `"CaneDiag"`; if Android/PyJNIus is unavailable, log the same message with Python `logging.getLogger("CaneDiag").info()`.

- [ ] **Step 4: Run the focused adapter test**

Run the same command from Step 2.

Expected: PASS.

### Task 2: Instrument Android USB and parser boundaries

**Files:**

- Modify: `calibration_app/tests/test_android_usb_serial.py`
- Modify: `calibration_app/tests/test_reader.py`
- Modify: `calibration_app/app/serial_backends/android_usb_serial.py:31-44,90-106`
- Modify: `calibration_app/app/reader.py:32-39`

**Interfaces:**

- Consumes: `diagnose(message: str) -> None`.
- Produces: bounded `CaneDiag` messages for port open/configuration, write, non-empty read, decoded packet counts, and parser bad-frame count changes.

- [ ] **Step 1: Write failing boundary-log tests**

Add these Android-port fakes and test:

```python
class RawPortWithBytes:
    def read(self, buffer, _timeout):
        buffer[:3] = b"\x01\x02\x03"
        return 3


def test_android_port_logs_nonempty_read_without_changing_returned_bytes(monkeypatch):
    messages = []
    monkeypatch.setattr(android_usb_serial, "diagnose", messages.append)

    assert _AndroidPort(RawPortWithBytes()).read(16) == b"\x01\x02\x03"
    assert messages == ["USB read bytes=3 hex=010203"]
```

Add a reader backend that returns one valid metadata frame and then marks itself
disconnected, plus this test:

```python
def test_reader_logs_decoded_packet_count(monkeypatch):
    messages = []
    monkeypatch.setattr(reader_module, "diagnose", messages.append)
    backend = MetadataFrameBackend()
    reader = CalibrationReader(backend, CalibrationController())

    reader.start()
    assert backend.read_called.wait(timeout=1.0)
    reader.stop()

    assert "Reader decoded packets=1" in messages
```

- [ ] **Step 2: Run the named tests and verify they fail**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_android_usb_serial.py::test_android_port_logs_nonempty_read_without_changing_returned_bytes tests/test_reader.py::test_reader_logs_decoded_packet_count -q'
```

Expected: FAIL because the port and reader do not call `diagnose()`.

- [ ] **Step 3: Implement bounded diagnostics**

Add a private formatter:

```python
def _preview(data: bytes) -> str:
    return data[:32].hex()
```

Log `USB open device=<id> baud=<baudrate>` after `setParameters()`, log writes before the raw write, and log reads only when `received > 0`. In `CalibrationReader._run()`, store the parser bad-packet count before feeding data, log decoded packet count when nonzero, and log the new bad-packet count only when it changed. Preserve the bytes delivered to the controller and all exception behavior.

- [ ] **Step 4: Run focused diagnostics tests and full suite**

Run the two new named tests, then:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests -q'
```

Expected: all tests pass.

- [ ] **Step 5: Build, install, and collect one Android trace**

Build the debug APK locally, install it through wireless ADB, clear logcat, then connect to the TinyPICO once. Retrieve:

```powershell
adb -s 192.168.0.237:39247 logcat -d -s CaneDiag
```

Expected: the trace establishes whether metadata request bytes leave the app, bytes return from USB, frames decode, or corruption occurs.

- [ ] **Step 6: Commit the diagnostic implementation and plan**

```powershell
git add calibration_app/app/diagnostics.py calibration_app/app/serial_backends/android_usb_serial.py calibration_app/app/reader.py calibration_app/tests/test_diagnostics.py calibration_app/tests/test_android_usb_serial.py calibration_app/tests/test_reader.py docs/superpowers/plans/2026-07-28-android-usb-diagnostics.md
git commit -m "chore: instrument Android USB calibration traffic"
```
