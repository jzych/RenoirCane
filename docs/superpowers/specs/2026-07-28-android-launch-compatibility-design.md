# Android launch compatibility design

## Goal

Produce a calibration APK that starts on Android 17 and targets the stable
Android 16 API level.

## Root cause

`main.py` imports `WindowsSerialBackend` during application startup. That
module imports `serial`, but Buildozer currently packages only `python3`,
`kivy`, and `pyjnius`. The missing `pyserial` module makes the Python activity
terminate before the first screen is shown.

The existing APK declares `targetSdkVersion=33`; the approved target is API 36
while keeping `minSdkVersion=24`.

## Design

Keep the shared serial-backend imports unchanged. Add `pyserial` to
`buildozer.spec`'s `requirements` list so every startup import is packaged.
Set `android.api = 36`; Buildozer installs the matching SDK platform when it
is not already cached. Do not set API 37 because it is the Android 17 preview
target and would opt the Kivy runtime into additional untested behavior
changes.

## Verification

Build a debug APK and inspect it with `aapt dump badging`. It must declare
`targetSdkVersion:'36'`, `sdkVersion:'24'`, and retain both Android ABI
variants. Confirm the copied app dependency set contains `serial` and that
the feature-worktree source is clean apart from the intended committed files.
