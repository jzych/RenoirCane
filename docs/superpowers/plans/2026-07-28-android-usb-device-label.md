# Android USB Device Label Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Android Refresh list an attached USB serial device without crashing on PyJNIus class-name access.

**Architecture:** Keep the existing Android bridge and backend interfaces unchanged.  Change only `_PyjniusAndroidUsbBridge._describe()` so its label derives from `UsbDevice.getVendorId()` and `getProductId()`, then cover that bridge-level behavior with a fake driver whose only API is `getDevice()`.

**Tech Stack:** Python 3, pytest, Kivy, PyJNIus, Android USB host API, Buildozer.

## Global Constraints

- Device labels must be exactly `USB serial — <vendor-id>:<product-id>` with lowercase, four-digit hexadecimal IDs.
- Do not call `getClass()`, `getSimpleName()`, or any driver-class API during Android USB device discovery.
- Preserve existing USB discovery and connection error behavior.
- Verify on Samsung SM-A037G with the attached TinyPICO USB serial device (`1a86:55d4`).

---

### Task 1: Generate stable Android USB labels

**Files:**
- Modify: `calibration_app/tests/test_android_usb_serial.py`
- Modify: `calibration_app/app/serial_backends/android_usb_serial.py:109-115`

**Interfaces:**
- Consumes: a driver object exposing `getDevice()`; the returned USB-device object exposes `getDeviceId()`, `getVendorId()`, and `getProductId()`.
- Produces: `_PyjniusAndroidUsbBridge._describe(raw_driver: Any) -> _AndroidDriver` with a label that is independent of the Java driver class.

- [ ] **Step 1: Write the failing regression test**

Add minimal fake Android device and driver classes that deliberately omit `getClass()`:

```python
from app.serial_backends.android_usb_serial import _PyjniusAndroidUsbBridge


class DescriptorOnlyDevice:
    def getDeviceId(self):
        return 42

    def getVendorId(self):
        return 0x1A86

    def getProductId(self):
        return 0x55D4


class DescriptorOnlyDriver:
    def getDevice(self):
        return DescriptorOnlyDevice()


def test_describe_android_driver_uses_usb_ids_without_class_name_access():
    driver = _PyjniusAndroidUsbBridge._describe(DescriptorOnlyDriver())

    assert driver.device_id == 42
    assert driver.label == "USB serial — 1a86:55d4"
```

- [ ] **Step 2: Run the regression test and verify it fails**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_android_usb_serial.py::test_describe_android_driver_uses_usb_ids_without_class_name_access -q'
```

Expected: FAIL with an `AttributeError` because `DescriptorOnlyDriver` has no `getClass()`.

- [ ] **Step 3: Implement the minimal label change**

Replace the label construction in `_describe()` with:

```python
device = raw_driver.getDevice()
label = f"USB serial — {device.getVendorId():04x}:{device.getProductId():04x}"
return _AndroidDriver(raw_driver, int(device.getDeviceId()), label)
```

- [ ] **Step 4: Run the focused test and full suite**

Run:

```powershell
wsl.exe -d Ubuntu bash -lc 'cd /mnt/m/CodingProjects/RenoirCane/calibration_app && /tmp/renoircane-calibration-tests/bin/python -m pytest tests/test_android_usb_serial.py::test_describe_android_driver_uses_usb_ids_without_class_name_access -q && /tmp/renoircane-calibration-tests/bin/python -m pytest tests -q'
```

Expected: the regression test and the entire suite pass.

- [ ] **Step 5: Rebuild, install, and verify on the phone**

Run the existing WSL Buildozer debug build, install `calibration_app/bin/canecalibration-0.1.0-arm64-v8a_armeabi-v7a-debug.apk` through the connected ADB device, open it, and press Refresh with the TinyPICO attached.

Expected: Refresh lists `USB serial — 1a86:55d4` and Android logcat contains no Python traceback.

- [ ] **Step 6: Commit the implementation**

```powershell
git add calibration_app/app/serial_backends/android_usb_serial.py calibration_app/tests/test_android_usb_serial.py docs/superpowers/plans/2026-07-28-android-usb-device-label.md
git commit -m "fix: label Android USB serial devices safely"
```
