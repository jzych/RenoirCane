# Android Serial Startup Design

## Goal

Allow the calibration APK to start on Android without loading desktop serial-port
enumeration code.

## Design

`main.py` will create the selected serial backend through two lazy factory
functions. The Android factory imports and returns `AndroidUsbSerialBackend`;
the desktop factory imports and returns `WindowsSerialBackend`. The existing
`make_serial_backend` decision remains responsible for selecting exactly one
factory from Kivy's platform name.

This preserves the desktop calibration application and ensures Android never
imports `serial.tools.list_ports`, which has no Android implementation.

## Error Handling

Android USB permission and connection failures remain handled by the existing
backend and UI. This change only prevents an unrelated desktop import at
process startup.

## Verification

Add a regression test that imports the Android startup path while making the
Windows backend import fail. The test must prove Android startup does not touch
the Windows backend. Run the calibration test suite, build the APK, install it
over ADB, launch it, and confirm no Python traceback is emitted.
