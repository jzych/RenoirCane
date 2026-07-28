# Android USB Diagnostics Design

## Goal

Identify where calibration traffic stops between the Android USB serial port,
the background reader, and the binary protocol parser without altering the
application UI or the calibration protocol.

## Scope

Diagnostic messages will be emitted only to Android logcat with the tag
`CaneDiag`. They are temporary troubleshooting instrumentation, but they are
safe to keep enabled because they log only non-empty serial reads and compact
metadata about frames.

## Logged Boundaries

- Android USB port open: USB device ID and configured baud rate.
- Android USB write: byte count and a bounded hexadecimal prefix of the
  metadata request.
- Android USB read: byte count and a bounded hexadecimal prefix, only when
  one or more bytes are returned.
- Reader/parser: count and description of decoded packets, plus parser bad
  packet-count changes.
- Controller: metadata, sample-batch, and status packet receipt with payload
  length.

All payload previews are capped at 32 bytes. Empty read timeouts are not
logged, so the normal 100 ms polling loop cannot flood logcat.

## Non-Goals

The diagnostic build will not reset the device, alter DTR/RTS, retry writes,
change packet framing, display counters in the UI, or modify firmware.

## Verification

Unit tests will prove the diagnostic formatting uses the `CaneDiag` logger
without changing data returned by read/write methods. The full calibration-app
test suite will run. A locally built APK will be installed on the Samsung
SM-A037G; one Connect attempt with the verified calibration TinyPICO will be
captured through `adb logcat -s CaneDiag` to identify the first missing or
invalid boundary.
